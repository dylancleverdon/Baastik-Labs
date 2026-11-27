#pragma once

#include "SharedBus.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace cuemix
{
// The audio side of CueMix: reads every channel on the bus and builds kNumMixes stereo mixes.
//
// Timing: the DAW may run a Send before or after the mixer within the same audio callback, and the
// order can change between callbacks. Each channel's read cursor starts on the newest block the
// mixer can see. If the order later flips, the channel underruns once (one silent block) and then
// settles one buffer behind its Send, which it can always read. So a cue path is delayed by zero
// or one buffer, never more, and never drifts.
//
// Control (gains, masters) is set from the message thread with atomics; the audio thread ramps
// every gain change over one block, so nothing clicks.
class MixEngine
{
public:
    explicit MixEngine(std::shared_ptr<SharedBus> bus);

    void prepare(double sampleRate, int maxBlockSize);

    // Audio thread. out[m][0..1] are the left/right pointers of mix m; a null pair skips that mix.
    // numFrames must be <= the prepared max block size.
    void process(float* const (*out)[2], int numFrames) noexcept;

    // --- control (any thread) ---
    // Gains of `slot` in `mix`, valid only while the slot has `generation`. Until a slot's
    // current generation has gains, it is silent in every mix.
    void setCell(int mix, int slot, uint32_t generation, float gainLeft, float gainRight) noexcept;
    void setMaster(int mix, float gain, bool limiterOn) noexcept;
    void setLimiterCeiling(float linear) noexcept { ceiling.store(linear); }

    // --- telemetry (read by the UI; peaks reset when read) ---
    float takeSlotPeak(int slot) noexcept { return slotPeak[size_t(slot)].exchange(0.0f); }
    float takeMixPeak(int mix) noexcept { return mixPeak[size_t(mix)].exchange(0.0f); }
    float mixGainReduction(int mix) const noexcept { return mixReduction[size_t(mix)].load(); }
    // How many frames behind its Send each channel was read in the last block (0 or one buffer).
    int slotLagFrames(int slot) const noexcept { return slotLag[size_t(slot)].load(); }
    uint32_t slotUnderruns(int slot) const noexcept { return underruns[size_t(slot)].load(); }
    bool slotSampleRateMismatch(int slot) const noexcept { return rateMismatch[size_t(slot)].load(); }

    double sampleRate() const noexcept { return rate; }

private:
    struct Cursor
    {
        uint32_t generation = 0;
        bool synced = false;
        uint64_t readPos = 0;
    };

    struct CellTarget
    {
        std::atomic<float> left { 0.0f };
        std::atomic<float> right { 0.0f };
    };

    std::shared_ptr<SharedBus> bus;
    double rate = 48000.0;
    int maxBlock = 0;
    float releaseCoef = 0.0f;

    std::array<Cursor, kMaxChannels> cursors {};
    std::array<std::atomic<uint32_t>, kMaxChannels> mappedGeneration {};
    std::array<std::array<CellTarget, kMaxChannels>, kNumMixes> targets {};
    std::array<std::array<std::array<float, 2>, kMaxChannels>, kNumMixes> current {};

    std::array<std::atomic<float>, kNumMixes> masterTarget {};
    std::array<std::atomic<bool>, kNumMixes> limiterEnabled {};
    std::array<float, kNumMixes> masterCurrent {};
    std::array<float, kNumMixes> limiterGain {};
    std::atomic<float> ceiling { 0.891f }; // -1 dBFS

    std::vector<float> scratchL, scratchR;

    std::array<std::atomic<float>, kMaxChannels> slotPeak {};
    std::array<std::atomic<float>, kNumMixes> mixPeak {};
    std::array<std::atomic<float>, kNumMixes> mixReduction {};
    std::array<std::atomic<int>, kMaxChannels> slotLag {};
    std::array<std::atomic<uint32_t>, kMaxChannels> underruns {};
    std::array<std::atomic<bool>, kMaxChannels> rateMismatch {};
};

// Peak limiter used on every mix: instant attack (the gain is computed from the same sample it is
// applied to, so the output never exceeds the ceiling), smooth release, no added latency.
struct LimiterMath
{
    static float process(float& gain, float ceiling, float releaseCoef, float& left, float& right) noexcept
    {
        const float peak = std::max(std::abs(left), std::abs(right));
        const float wanted = peak > ceiling ? ceiling / peak : 1.0f;
        if (wanted < gain)
            gain = wanted;
        else
            gain += (1.0f - gain) * releaseCoef;
        left *= gain;
        right *= gain;
        // Rounding can leave a hair over the ceiling.
        left = std::clamp(left, -ceiling, ceiling);
        right = std::clamp(right, -ceiling, ceiling);
        return gain;
    }
};
} // namespace cuemix
