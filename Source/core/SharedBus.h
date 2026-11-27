#pragma once

#include "Constants.h"

#include <atomic>
#include <memory>
#include <string>

namespace cuemix
{
// One channel published by a CueMix Send. Lives in shared memory, so it holds only lock-free
// atomics and plain sample data (no pointers, no std types with heap storage).
struct SlotShared
{
    // 0 = free, 1 = claimed, 2 = being claimed (fields are being written).
    std::atomic<uint32_t> state;
    // Bumped on every claim, so readers can tell a new owner from the old one.
    std::atomic<uint32_t> generation;
    std::atomic<int32_t> ownerPid;
    std::atomic<uint32_t> sampleRate;
    std::atomic<uint64_t> ownerToken;   // random per Send instance
    std::atomic<uint64_t> channelId;    // stable id, saved in the Send's state

    // Name, guarded by a sequence lock (odd = being written).
    std::atomic<uint32_t> nameSeq;
    std::atomic<uint32_t> colour;       // 0xAARRGGBB, 0 = none
    std::atomic<uint64_t> name[kNameWords];

    // Audio ring: frames [writePos - kRingFrames, writePos) are valid.
    std::atomic<uint64_t> writePos;
    std::atomic<int64_t> lastWriteNs;   // steady clock, 0 = never written
    float ring[2][kRingFrames];
};

struct BusShared
{
    std::atomic<uint32_t> magic;
    std::atomic<uint32_t> layoutVersion;
    std::atomic<uint32_t> ready;
    uint32_t reserved;
    SlotShared slots[kMaxChannels];
};

static_assert(std::atomic<uint64_t>::is_always_lock_free, "need lock-free 64-bit atomics");
static_assert(std::atomic<int64_t>::is_always_lock_free, "need lock-free 64-bit atomics");
static_assert(std::atomic<uint32_t>::is_always_lock_free, "need lock-free 32-bit atomics");

// A mapping of the shared bus. Every CueMix Send and CueMix in every process on the machine that
// opens the same name sees the same memory. Within one process a mapping is shared.
class SharedBus
{
public:
    // Opens (creating if needed) the bus. Returns null and sets `error` on failure.
    static std::shared_ptr<SharedBus> open(const std::string& name, std::string* error = nullptr);
    // "/cuemix.v1", or $CUEMIX_BUS when set (tests and tools use their own bus).
    static std::string defaultName();
    // Removes the named bus from the system (tests only; live mappings stay valid).
    static void unlink(const std::string& name);

    ~SharedBus();
    SharedBus(const SharedBus&) = delete;
    SharedBus& operator=(const SharedBus&) = delete;

    BusShared& data() noexcept { return *bus; }
    SlotShared& slot(int i) noexcept { return bus->slots[i]; }
    const std::string& name() const noexcept { return busName; }

    // Steady-clock time in ns, comparable across processes on the same machine.
    static int64_t nowNs() noexcept;
    static int32_t currentPid() noexcept;
    static bool processAlive(int32_t pid) noexcept;

private:
    SharedBus() = default;
    BusShared* bus = nullptr;
    std::string busName;
    bool ownsHeapMemory = false;
};

// Name helpers (seqlock). Writers must be serialised by the caller (one owner per slot).
void writeSlotName(SlotShared& slot, const std::string& name) noexcept;
std::string readSlotName(const SlotShared& slot);

// A snapshot of one slot, as seen by the mixer's message thread.
struct SlotInfo
{
    int index = -1;
    bool claimed = false;
    bool active = false;       // written within the last second
    uint32_t generation = 0;
    uint64_t channelId = 0;
    uint32_t sampleRate = 0;
    uint32_t colour = 0;
    std::string name;
};

SlotInfo snapshotSlot(const SharedBus& bus, int index);
} // namespace cuemix
