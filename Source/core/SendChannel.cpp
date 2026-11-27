#include "SendChannel.h"

#include <algorithm>
#include <random>

namespace cuemix
{
uint64_t SendChannel::randomId()
{
    static std::mutex m;
    static std::mt19937_64 rng { std::random_device {}() ^ static_cast<uint64_t>(SharedBus::nowNs()) };
    std::lock_guard<std::mutex> lock(m);
    uint64_t v = 0;
    while (v == 0)
        v = rng();
    return v;
}

SendChannel::SendChannel(std::shared_ptr<SharedBus> b) : bus(std::move(b)), token(randomId()) {}

SendChannel::~SendChannel() { release(); }

bool SendChannel::claim(uint64_t wantedId, const std::string& name)
{
    std::lock_guard<std::mutex> lock(controlLock);
    if (bus == nullptr)
        return false;

    // Already claimed: just switch to the wanted id (e.g. state restored after construction).
    const int current = slotIndex.load(std::memory_order_relaxed);
    if (current >= 0)
    {
        auto& s = bus->slot(current);
        s.state.store(0, std::memory_order_release);
        slotIndex.store(-1, std::memory_order_release);
    }

    id = wantedId != 0 ? wantedId : randomId();
    for (int i = 0; i < kMaxChannels; ++i)
    {
        auto& s = bus->slot(i);
        if (s.state.load(std::memory_order_acquire) == 1 && s.channelId.load() == id
            && s.ownerToken.load() != token)
        {
            id = randomId(); // a duplicated track: become a separate channel
            break;
        }
    }

    const int32_t pid = SharedBus::currentPid();
    auto tryTake = [&](int i, uint32_t from) {
        auto& s = bus->slot(i);
        uint32_t expected = from;
        if (!s.state.compare_exchange_strong(expected, 2, std::memory_order_acq_rel))
            return false;
        s.generation.fetch_add(1, std::memory_order_acq_rel);
        s.ownerPid.store(pid, std::memory_order_relaxed);
        s.ownerToken.store(token, std::memory_order_relaxed);
        s.channelId.store(id, std::memory_order_relaxed);
        s.sampleRate.store(rate, std::memory_order_relaxed);
        s.colour.store(0, std::memory_order_relaxed);
        s.lastWriteNs.store(0, std::memory_order_relaxed);
        writeSlotName(s, name);
        s.state.store(1, std::memory_order_release);
        slotIndex.store(i, std::memory_order_release);
        return true;
    };

    for (int i = 0; i < kMaxChannels; ++i)
        if (tryTake(i, 0))
            return true;

    // No free slot: take over one whose owner process has gone (a crashed DAW).
    for (int i = 0; i < kMaxChannels; ++i)
    {
        auto& s = bus->slot(i);
        const int32_t owner = s.ownerPid.load();
        if (s.state.load() == 1 && owner != pid && !SharedBus::processAlive(owner) && tryTake(i, 1))
            return true;
    }
    return false;
}

void SendChannel::release()
{
    std::lock_guard<std::mutex> lock(controlLock);
    const int current = slotIndex.exchange(-1, std::memory_order_acq_rel);
    if (current < 0 || bus == nullptr)
        return;
    auto& s = bus->slot(current);
    if (s.ownerToken.load() == token)
        s.state.store(0, std::memory_order_release);
}

void SendChannel::setName(const std::string& name)
{
    std::lock_guard<std::mutex> lock(controlLock);
    const int current = slotIndex.load(std::memory_order_relaxed);
    if (current >= 0)
        writeSlotName(bus->slot(current), name);
}

void SendChannel::setColour(uint32_t argb)
{
    std::lock_guard<std::mutex> lock(controlLock);
    const int current = slotIndex.load(std::memory_order_relaxed);
    if (current >= 0)
        bus->slot(current).colour.store(argb, std::memory_order_relaxed);
}

void SendChannel::setSampleRate(double sampleRate)
{
    std::lock_guard<std::mutex> lock(controlLock);
    rate = static_cast<uint32_t>(sampleRate + 0.5);
    const int current = slotIndex.load(std::memory_order_relaxed);
    if (current >= 0)
        bus->slot(current).sampleRate.store(rate, std::memory_order_relaxed);
}

void SendChannel::write(const float* left, const float* right, int numFrames) noexcept
{
    const int current = slotIndex.load(std::memory_order_acquire);
    if (current < 0 || numFrames <= 0 || left == nullptr)
        return;
    auto& s = bus->slot(current);
    if (right == nullptr)
        right = left;

    constexpr uint64_t mask = kRingFrames - 1;
    uint64_t pos = s.writePos.load(std::memory_order_relaxed);
    int done = 0;
    while (done < numFrames)
    {
        const int start = static_cast<int>(pos & mask);
        const int chunk = std::min(numFrames - done, kRingFrames - start);
        std::copy_n(left + done, chunk, s.ring[0] + start);
        std::copy_n(right + done, chunk, s.ring[1] + start);
        done += chunk;
        pos += static_cast<uint64_t>(chunk);
    }
    s.writePos.store(pos, std::memory_order_release);
    s.lastWriteNs.store(SharedBus::nowNs(), std::memory_order_relaxed);
}
} // namespace cuemix
