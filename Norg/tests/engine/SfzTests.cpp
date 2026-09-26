#include "engine/piano/SamplePlayer.h"
#include "engine/sfz/SampleLibrary.h"
#include "engine/sfz/SfzParser.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>

using namespace norg;

namespace
{
    struct TempDir
    {
        TempDir() : dir (juce::File::createTempFile ("norg-sfz")) { dir.createDirectory(); }
        ~TempDir() { dir.deleteRecursively(); }
        juce::File dir;
    };

    // Writes a WAV containing a sine of the given frequency and level.
    void writeSine (const juce::File& file, double freq, float level, int bits, int channels, double seconds = 1.0)
    {
        file.getParentDirectory().createDirectory();
        const double rate = 48000.0;
        const int frames = static_cast<int> (seconds * rate);
        juce::AudioBuffer<float> audio (channels, frames);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < frames; ++i)
                audio.setSample (ch, i, level * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * freq * i / rate)));

        file.deleteFile();
        std::unique_ptr<juce::OutputStream> out (file.createOutputStream());
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions().withSampleRate (rate)
                                                    .withNumChannels (channels).withBitsPerSample (bits));
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (audio, 0, frames);
    }

    float renderPeak (piano::SamplePlayer& player, int samples)
    {
        std::vector<float> l (static_cast<size_t> (samples)), r (static_cast<size_t> (samples));
        player.render (l.data(), r.data(), samples);
        float peak = 0.0f;
        for (float s : l)
            peak = juce::jmax (peak, std::abs (s));
        return peak;
    }
}

TEST_CASE ("note names follow the SFZ convention (c4 = 60)", "[sfz]")
{
    CHECK (sfz::parseNoteName ("c4") == 60);
    CHECK (sfz::parseNoteName ("C#4") == 61);
    CHECK (sfz::parseNoteName ("db4") == 61);
    CHECK (sfz::parseNoteName ("a0") == 21);
    CHECK (sfz::parseNoteName ("c-1") == 0);
    CHECK (sfz::parseNoteName ("64") == 64);
    CHECK (sfz::parseNoteName ("h3") == -1);
}

TEST_CASE ("SFZ headers, inheritance, defines and paths", "[sfz]")
{
    TempDir tmp;
    const auto sfzFile = tmp.dir.getChildFile ("Test Piano.sfz");
    const juce::String text = R"(
        // a comment
        #define $EXT wav
        <control> default_path=samples\
        <global> ampeg_release=0.8 amp_veltrack=90
        <group> lovel=1 hivel=64 /* soft layer */
        <region> sample=A4 soft.$EXT key=a4
        <region> sample=C5 soft.$EXT lokey=b4 hikey=c#5 pitch_keycenter=c5 tune=-5
        <group> lovel=65 hivel=127 volume=-3
        <region> sample=A4 loud.$EXT key=69
        <group> trigger=release rt_decay=6
        <region> sample=rel.$EXT lokey=0 hikey=127
    )";

    const auto result = sfz::parse (text, sfzFile);
    REQUIRE (result.regions.size() == 4);

    const auto& soft = result.regions[0];
    CHECK (soft.sample == tmp.dir.getChildFile ("samples/A4 soft.wav").getFullPathName());
    CHECK (soft.loKey == 69);
    CHECK (soft.hiKey == 69);
    CHECK (soft.hiVel == 64);
    CHECK (std::abs (soft.ampegRelease - 0.8f) < 1.0e-6f);
    CHECK (std::abs (soft.ampVeltrack - 90.0f) < 1.0e-6f);

    const auto& c5 = result.regions[1];
    CHECK (c5.loKey == 71);
    CHECK (c5.hiKey == 73);
    CHECK (c5.keyCenter == 72);
    CHECK (std::abs (c5.tuneCents + 5.0f) < 1.0e-6f);

    const auto& loud = result.regions[2];
    CHECK (loud.loVel == 65);
    CHECK (std::abs (loud.volumeDb + 3.0f) < 1.0e-6f);

    const auto& rel = result.regions[3];
    CHECK (rel.trigger == sfz::Trigger::release);
    CHECK (std::abs (rel.rtDecay - 6.0f) < 1.0e-6f);
    CHECK (rel.loVel == 1); // the new <group> reset the velocity range
}

TEST_CASE ("SFZ instruments load WAVs of every supported depth and play the right layer", "[sfz]")
{
    TempDir tmp;
    writeSine (tmp.dir.getChildFile ("s/soft.wav"), 440.0, 0.2f, 16, 1);
    writeSine (tmp.dir.getChildFile ("s/loud.wav"), 440.0, 0.9f, 24, 2);
    writeSine (tmp.dir.getChildFile ("s/rel.wav"), 880.0, 0.5f, 32, 1, 0.3);

    const auto sfzFile = tmp.dir.getChildFile ("layers.sfz");
    sfzFile.replaceWithText (R"(
        <control> default_path=s/
        <global> amp_veltrack=0 ampeg_release=0.2
        <region> sample=soft.wav key=69 lovel=1 hivel=64
        <region> sample=loud.wav key=69 lovel=65 hivel=127
        <region> sample=rel.wav key=69 trigger=release
    )");

    std::atomic<bool> cancel { false };
    juce::String error;
    auto instrument = sfz::Instrument::load (sfzFile, cancel, error);
    REQUIRE (instrument != nullptr);
    CHECK (instrument->sources.size() == 3);
    CHECK (instrument->sources[1]->numChannels() == 2);

    piano::SamplePlayer player;
    player.prepare (48000.0, 512);
    player.setGain (1.0f);
    player.setPianoDamping (false);
    player.setInstrument (instrument);

    player.noteOn (69, 30.0f / 127.0f);
    const float softPeak = renderPeak (player, 4800);
    player.allNotesOff();
    renderPeak (player, 48000);

    player.noteOn (69, 1.0f);
    const float loudPeak = renderPeak (player, 4800);
    CHECK (std::abs (softPeak - 0.2f) < 0.02f);
    CHECK (std::abs (loudPeak - 0.9f) < 0.05f);

    // Key up with the pedal up: the note is damped and the release sample plays.
    player.noteOff (69);
    renderPeak (player, 48000);
    CHECK_FALSE (player.isActive());
}

TEST_CASE ("the sample player transposes from the key centre", "[sfz]")
{
    TempDir tmp;
    writeSine (tmp.dir.getChildFile ("a.wav"), 440.0, 0.5f, 16, 1);
    const auto sfzFile = tmp.dir.getChildFile ("one.sfz");
    sfzFile.replaceWithText ("<region> sample=a.wav lokey=0 hikey=127 pitch_keycenter=69 amp_veltrack=0");

    std::atomic<bool> cancel { false };
    juce::String error;
    auto instrument = sfz::Instrument::load (sfzFile, cancel, error);
    REQUIRE (instrument != nullptr);

    piano::SamplePlayer player;
    player.prepare (48000.0, 4096);
    player.setInstrument (instrument);
    player.noteOn (81, 1.0f); // an octave up

    std::vector<float> l (4096), r (4096);
    player.render (l.data(), r.data(), 4096);

    // Count zero crossings: an octave up doubles them (440 Hz -> 880 Hz).
    int crossings = 0;
    for (size_t i = 1; i < l.size(); ++i)
        crossings += (l[i - 1] < 0.0f) != (l[i] < 0.0f);
    const double freq = crossings / 2.0 / (4096.0 / 48000.0);
    CHECK (std::abs (freq - 880.0) < 20.0);
}
