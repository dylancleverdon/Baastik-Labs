#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <mutex>

// Tutti Stand-in.
//
// Capture mode (this computer owns the plugin before it): passes audio through and, while the
// song plays, records it with its song position and tempo. Each stretch of playback becomes a
// segment that the Tutti app shares.
//
// Play mode (this computer doesn't own that plugin): ignores its silent input and plays the
// shared segments at the song position, so the track sounds as it does for the owner.
//
// The "Link" parameter holds a random id the Remote Script can read, so the Tutti app can tell
// which device in the shared song each Stand-in is.
class StandInProcessor : public juce::AudioProcessor
{
public:
    StandInProcessor();
    ~StandInProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tutti Stand-in"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // For the editor.
    juce::String statusText() const;
    juce::String detailText() const;
    bool connected() const { return isConnected.load(); }

    enum class Mode { idle, capture, play };

    struct Segment
    {
        juce::AudioBuffer<float> audio;
        double rate = 44100.0;
        double startBeat = 0.0, lengthBeats = 0.0, tempo = 120.0;
        bool stale = false;
    };
    using SegmentList = std::vector<std::shared_ptr<const Segment>>;

private:
    class Bridge;
    class Recorder;

    void setLink (float value);
    void playSegments (juce::AudioBuffer<float>&, double ppq, double bpm);

    juce::AudioParameterFloat* link = nullptr;
    std::unique_ptr<Bridge> bridge;
    std::unique_ptr<Recorder> recorder;

    std::atomic<int> mode { (int) Mode::idle };
    std::atomic<bool> isConnected { false };
    std::atomic<bool> playing { false };
    std::atomic<double> hostRate { 44100.0 };

    // Play mode: the segments, swapped in by the bridge thread, read by the audio thread.
    juce::SpinLock segmentsLock;
    std::shared_ptr<const SegmentList> segments;
    juce::String who;           // whose recording we play
    std::atomic<int> staleCount { 0 };
    mutable std::mutex textLock;

    friend class StandInEditor;
};
