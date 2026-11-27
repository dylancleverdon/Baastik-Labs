#pragma once

#include "SharedBus.h"

#include <memory>
#include <mutex>
#include <string>

namespace cuemix
{
// The writing end of one channel: what a CueMix Send does with the bus.
//
// claim/release/setName/setChannelId run on the message thread; write() runs on the audio thread
// and never locks or allocates.
class SendChannel
{
public:
    explicit SendChannel(std::shared_ptr<SharedBus> bus);
    ~SendChannel();

    // Claims a free slot (or one left behind by a process that died). If another live Send is
    // already publishing `wantedId` (a duplicated track), a new id is chosen. Returns false when
    // all slots are taken.
    bool claim(uint64_t wantedId, const std::string& name);
    void release();

    bool isClaimed() const noexcept { return slotIndex.load(std::memory_order_acquire) >= 0; }
    int slot() const noexcept { return slotIndex.load(std::memory_order_acquire); }
    uint64_t channelId() const noexcept { return id; }

    void setName(const std::string& name);
    void setColour(uint32_t argb);
    void setSampleRate(double sampleRate);

    // Audio thread. `right` may be null (mono: left is used for both sides).
    void write(const float* left, const float* right, int numFrames) noexcept;

    static uint64_t randomId();

private:
    std::shared_ptr<SharedBus> bus;
    std::atomic<int> slotIndex { -1 };
    uint64_t id = 0;
    uint64_t token = 0;
    uint32_t rate = 0;
    std::mutex controlLock; // serialises the message-thread calls (hosts call some from other threads)
};
} // namespace cuemix
