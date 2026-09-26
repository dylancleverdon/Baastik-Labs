#include "TestHelpers.h"
#include "engine/MasterClock.h"
#include "engine/NorgEngine.h"
#include "engine/fx/Effects.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_dsp/juce_dsp.h>

using namespace norg;

namespace
{
    constexpr double rate = 48000.0;
    constexpr int blockSize = 256;

    struct Stereo
    {
        explicit Stereo (int n) : l (static_cast<size_t> (n), 0.0f), r (static_cast<size_t> (n), 0.0f) {}
        int size() const { return static_cast<int> (l.size()); }
        std::vector<float> l, r;
    };

    Stereo sine (float hz, float amplitude, int n)
    {
        Stereo s (n);
        for (int i = 0; i < n; ++i)
            s.l[static_cast<size_t> (i)] = s.r[static_cast<size_t> (i)]
                = amplitude * std::sin (dsp::twoPi * hz * static_cast<float> (i) / static_cast<float> (rate));
        return s;
    }

    Stereo noise (float amplitude, int n)
    {
        Stereo s (n);
        dsp::Noise gen (1234u);
        for (int i = 0; i < n; ++i)
        {
            s.l[static_cast<size_t> (i)] = amplitude * gen.next();
            s.r[static_cast<size_t> (i)] = amplitude * gen.next();
        }
        return s;
    }

    // Runs `s` through the block in host-sized chunks; `atSample` is called before each chunk.
    void run (fx::FxBlock& block, Stereo& s, const std::function<void (int)>& atSample = {})
    {
        for (int start = 0; start < s.size(); start += blockSize)
        {
            if (atSample)
                atSample (start);
            const int n = juce::jmin (blockSize, s.size() - start);
            block.process (s.l.data() + start, s.r.data() + start, n);
        }
    }

    float peakOf (const std::vector<float>& x, int from = 0, int to = -1)
    {
        float p = 0.0f;
        for (int i = from; i < (to < 0 ? static_cast<int> (x.size()) : to); ++i)
            p = juce::jmax (p, std::abs (x[static_cast<size_t> (i)]));
        return p;
    }

    bool allFinite (const Stereo& s)
    {
        for (size_t i = 0; i < s.l.size(); ++i)
            if (! std::isfinite (s.l[i]) || ! std::isfinite (s.r[i]))
                return false;
        return true;
    }

    // Amplitude of one frequency (Goertzel), over the given range.
    double amplitudeAt (const std::vector<float>& x, double hz, int from, int to)
    {
        const double w = juce::MathConstants<double>::twoPi * hz / rate;
        const double coeff = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;
        for (int i = from; i < to; ++i)
        {
            const double s0 = x[static_cast<size_t> (i)] + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
        return 2.0 * std::sqrt (juce::jmax (0.0, power)) / (to - from);
    }

    double gainDbAt (fx::FxBlock& block, float hz)
    {
        block.reset();
        auto s = sine (hz, 0.1f, 24000);
        run (block, s);
        return 20.0 * std::log10 (amplitudeAt (s.l, hz, 12000, 24000) / 0.1);
    }

    // A dozen short windows of RMS, in dB, for measuring decays.
    std::vector<double> envelopeDb (const std::vector<float>& x, int window)
    {
        std::vector<double> env;
        for (size_t start = 0; start + static_cast<size_t> (window) <= x.size(); start += static_cast<size_t> (window))
        {
            double sum = 0.0;
            for (size_t i = start; i < start + static_cast<size_t> (window); ++i)
                sum += static_cast<double> (x[i]) * x[i];
            env.push_back (10.0 * std::log10 (sum / window + 1.0e-30));
        }
        return env;
    }
}

TEST_CASE ("every effect type stays finite and bounded, and is transparent when off", "[fx]")
{
    struct Case
    {
        const char* name;
        std::function<std::unique_ptr<fx::FxBlock>()> make;
    };

    std::vector<Case> cases;
    for (int t = 0; t < 5; ++t)
        cases.push_back ({ "effect 1", [t]
        {
            auto e = std::make_unique<fx::ModEffect1>();
            e->set (static_cast<fx::ModEffect1::Type> (t), 0.8f, 1.0f);
            return std::unique_ptr<fx::FxBlock> (std::move (e));
        } });
    for (int t = 0; t < 4; ++t)
        cases.push_back ({ "effect 2", [t]
        {
            auto e = std::make_unique<fx::ModEffect2>();
            e->set (static_cast<fx::ModEffect2::Type> (t), 0.8f, 1.0f);
            return std::unique_ptr<fx::FxBlock> (std::move (e));
        } });
    for (int t = 0; t < 4; ++t)
        cases.push_back ({ "amp", [t]
        {
            auto e = std::make_unique<fx::AmpEq>();
            e->set (static_cast<fx::AmpEq::Type> (t), 1.0f, 6.0f, 6.0f, 3000.0f, 6.0f);
            return std::unique_ptr<fx::FxBlock> (std::move (e));
        } });
    cases.push_back ({ "compressor", []
    {
        auto e = std::make_unique<fx::Compressor>();
        e->set (1.0f, true);
        return std::unique_ptr<fx::FxBlock> (std::move (e));
    } });
    cases.push_back ({ "delay", []
    {
        auto e = std::make_unique<fx::StereoDelay>();
        e->set (0.05f, 0.95f, 1.0f, true, 0.0f);
        return std::unique_ptr<fx::FxBlock> (std::move (e));
    } });
    for (int t = 0; t < 3; ++t)
        cases.push_back ({ "reverb", [t]
        {
            auto e = std::make_unique<fx::Reverb>();
            e->set (static_cast<fx::Reverb::Type> (t), 1.0f, t == 1);
            return std::unique_ptr<fx::FxBlock> (std::move (e));
        } });

    for (size_t c = 0; c < cases.size(); ++c)
    {
        DYNAMIC_SECTION (cases[c].name << " #" << c)
        {
            auto block = cases[c].make();
            block->setEnabled (true);
            block->prepareBlock (rate, blockSize);

            auto loud = noise (0.5f, 48000);
            run (*block, loud);
            CHECK (allFinite (loud));
            CHECK (peakOf (loud.l) < 3.0f);
            CHECK (peakOf (loud.r) < 3.0f);

            // Switch off, let any tail ring out, then it must pass audio untouched.
            block->setEnabled (false);
            Stereo silence (48000 * 25); // the 0.95-feedback delay rings for a long time

            run (*block, silence);
            CHECK_FALSE (block->isRunning());

            // The mod effects' delay lines keep filling while off, so they switch in smoothly.
            const auto input = sine (440.0f, 0.3f, 4800);
            auto output = input;
            run (*block, output);
            CHECK (output.l == input.l);
            CHECK (output.r == input.r);
        }
    }
}

TEST_CASE ("switching an effect on and off fades instead of clicking", "[fx]")
{
    fx::ModEffect2 chorus;
    chorus.set (fx::ModEffect2::Type::chorus, 0.6f, 1.0f);
    chorus.prepareBlock (rate, blockSize);

    auto s = sine (220.0f, 0.5f, 48000);
    run (chorus, s, [&] (int start)
    {
        if (start == 12032) chorus.setEnabled (true);
        if (start == 30208) chorus.setEnabled (false);
    });

    // The largest sample-to-sample step of a 220 Hz sine at 0.5 is ~0.014; a hard switch to the
    // chorused signal jumps by ~0.3.
    float maxStep = 0.0f;
    for (size_t i = 1; i < s.l.size(); ++i)
        maxStep = juce::jmax (maxStep, std::abs (s.l[i] - s.l[i - 1]));
    CHECK (maxStep < 0.05f);
}

TEST_CASE ("the delay repeats on time, ping-pongs, and keeps ringing after it is switched off", "[fx]")
{
    fx::StereoDelay delay;
    delay.setEnabled (true);
    delay.prepareBlock (rate, blockSize);
    delay.set (0.25f, 0.5f, 1.0f, true, 0.0f);

    Stereo s (48000 * 2);
    s.l[0] = s.r[0] = 1.0f;
    run (delay, s, [&] (int start)
    {
        if (start >= 30000 && start < 30000 + blockSize)
            delay.setEnabled (false); // after the second repeat
    });

    // First repeat on the left at exactly 250 ms, the second on the right at 500 ms.
    CHECK (s.l[12000] == Catch::Approx (1.0f).margin (0.01f));
    CHECK (peakOf (s.r, 11000, 13000) < 0.01f);
    CHECK (peakOf (s.r, 23800, 24200) > 0.3f);
    CHECK (peakOf (s.l, 23000, 25000) < 0.01f);

    // Switched off at ~625 ms, but the third repeat (750 ms) still sounds and is quieter.
    const float third = peakOf (s.l, 35800, 36200);
    CHECK (third > 0.1f);
    CHECK (third < peakOf (s.r, 23800, 24200));
}

TEST_CASE ("synced delay divisions are in beats", "[fx]")
{
    CHECK (delayDivisionBeats (0) == Catch::Approx (1.0f));
    CHECK (delayDivisionBeats (1) == Catch::Approx (0.75f));
    CHECK (delayDivisionBeats (2) == Catch::Approx (0.5f));
    CHECK (delayDivisionBeats (3) == Catch::Approx (2.0f / 3.0f));
    CHECK (delayDivisionBeats (6) == Catch::Approx (2.0f));
}

TEST_CASE ("reverb sizes decay in order and the tail ends", "[fx]")
{
    std::array<double, 3> decayTimes {};
    for (int t = 0; t < 3; ++t)
    {
        fx::Reverb reverb;
        reverb.setEnabled (true);
        reverb.prepareBlock (rate, blockSize);
        reverb.set (static_cast<fx::Reverb::Type> (t), 1.0f, false);

        Stereo s (48000 * 6);
        s.l[0] = s.r[0] = 1.0f;
        run (reverb, s);
        CHECK (allFinite (s));

        // Time from the loudest 10 ms window until 30 dB below it, doubled: an RT60 estimate.
        s.l[0] = 0.0f;
        const auto env = envelopeDb (s.l, 480);
        const auto loudest = std::max_element (env.begin(), env.end());
        const auto quieter = std::find_if (loudest, env.end(), [&] (double v) { return v < *loudest - 30.0; });
        REQUIRE (quieter != env.end());
        decayTimes[static_cast<size_t> (t)] = 2.0 * static_cast<double> (quieter - loudest) * 0.01;
        CHECK (reverb.hasTail() == false);
    }

    INFO ("RT60 room " << decayTimes[0] << " stage " << decayTimes[1] << " hall " << decayTimes[2]);
    CHECK (decayTimes[0] < decayTimes[1]);
    CHECK (decayTimes[1] < decayTimes[2]);
    CHECK (decayTimes[0] > 0.3);
    CHECK (decayTimes[2] < 5.0);
}

TEST_CASE ("bright reverb has more treble than dark", "[fx]")
{
    std::array<double, 2> treble {};
    for (int bright = 0; bright < 2; ++bright)
    {
        fx::Reverb reverb;
        reverb.setEnabled (true);
        reverb.prepareBlock (rate, blockSize);
        reverb.set (fx::Reverb::Type::stage, 1.0f, bright == 1);
        const auto dry = noise (0.2f, 48000);
        auto s = dry;
        run (reverb, s);
        std::vector<float> wet (s.l.size());
        for (size_t i = 0; i < wet.size(); ++i)
            wet[i] = s.l[i] - dry.l[i];
        const auto rmsBetween = [&] (const std::vector<float>& x)
        {
            // Treble energy: a crude high-pass (first difference) then RMS.
            double sum = 0.0;
            for (size_t i = 24001; i < 48000; ++i)
                sum += static_cast<double> (x[i] - x[i - 1]) * (x[i] - x[i - 1]);
            return std::sqrt (sum / 24000.0);
        };
        treble[static_cast<size_t> (bright)] = rmsBetween (wet) / juce::jmax (1.0e-9, [&]
        {
            double sum = 0.0;
            for (size_t i = 24000; i < 48000; ++i)
                sum += static_cast<double> (wet[i]) * wet[i];
            return std::sqrt (sum / 24000.0);
        }());
    }
    INFO ("treble ratio dark " << treble[0] << " bright " << treble[1]);
    CHECK (treble[1] > treble[0] * 1.3);
}

TEST_CASE ("the compressor narrows the gap between loud and quiet", "[fx]")
{
    const auto levelOut = [] (float amplitude)
    {
        fx::Compressor comp;
        comp.setEnabled (true);
        comp.prepareBlock (rate, blockSize);
        comp.set (0.8f, false);
        auto s = sine (1000.0f, amplitude, 24000);
        run (comp, s);
        return 20.0 * std::log10 (amplitudeAt (s.l, 1000.0, 12000, 24000));
    };

    const double spread = levelOut (0.8f) - levelOut (0.08f); // 20 dB in
    CHECK (spread < 12.0);
    CHECK (spread > 2.0);
}

TEST_CASE ("the EQ boosts and cuts where it should", "[fx]")
{
    fx::AmpEq eq;
    eq.setEnabled (true);
    eq.prepareBlock (rate, blockSize);

    eq.set (fx::AmpEq::Type::eqOnly, 0.0f, 0.0f, 0.0f, 1000.0f, 0.0f);
    CHECK (gainDbAt (eq, 1000.0f) == Catch::Approx (0.0).margin (0.1));

    eq.set (fx::AmpEq::Type::eqOnly, 0.0f, 12.0f, 0.0f, 1000.0f, 0.0f);
    CHECK (gainDbAt (eq, 40.0f) == Catch::Approx (12.0).margin (1.5));
    CHECK (gainDbAt (eq, 3000.0f) == Catch::Approx (0.0).margin (0.7));

    eq.set (fx::AmpEq::Type::eqOnly, 0.0f, 0.0f, -10.0f, 2000.0f, 0.0f);
    CHECK (gainDbAt (eq, 2000.0f) == Catch::Approx (-10.0).margin (0.5));

    eq.set (fx::AmpEq::Type::eqOnly, 0.0f, 0.0f, 0.0f, 1000.0f, 12.0f);
    CHECK (gainDbAt (eq, 14000.0f) == Catch::Approx (12.0).margin (1.5));
    CHECK (gainDbAt (eq, 200.0f) == Catch::Approx (0.0).margin (0.7));
}

TEST_CASE ("an amp driven hard keeps aliasing low", "[fx]")
{
    // A 2.9 kHz sine driven into the Small amp makes lots of harmonics. Whatever lands between
    // the harmonics (below 20 kHz) is aliasing.
    fx::AmpEq amp;
    amp.setEnabled (true);
    amp.prepareBlock (rate, blockSize);
    amp.set (fx::AmpEq::Type::small, 1.0f, 0.0f, 0.0f, 1000.0f, 0.0f);

    constexpr int order = 15, size = 1 << order;
    const float hz = 2900.0f;
    auto s = sine (hz, 0.5f, size + 8192);
    run (amp, s);

    std::vector<float> spectrum (2 * size, 0.0f);
    for (int i = 0; i < size; ++i)
    {
        const float window = 0.5f - 0.5f * std::cos (dsp::twoPi * static_cast<float> (i) / size);
        spectrum[static_cast<size_t> (i)] = window * s.l[static_cast<size_t> (i + 8192)];
    }
    juce::dsp::FFT (order).performFrequencyOnlyForwardTransform (spectrum.data());

    double harmonic = 0.0, alias = 0.0;
    const double binHz = rate / size;
    for (int bin = 2; bin < static_cast<int> (20000.0 / binHz); ++bin)
    {
        const double f = bin * binHz;
        const double nearest = std::round (f / hz) * hz;
        const double power = static_cast<double> (spectrum[static_cast<size_t> (bin)]) * spectrum[static_cast<size_t> (bin)];
        (std::abs (f - nearest) < 6.0 * binHz ? harmonic : alias) += power;
    }

    const double aliasDb = 10.0 * std::log10 (alias / harmonic);
    INFO ("alias energy " << aliasDb << " dB");
    CHECK (aliasDb < -40.0);
}

TEST_CASE ("the rotary works from the effects chain and can take the piano", "[fx][organ]")
{
    const auto modulationDepth = [] (NorgProcessor& p)
    {
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 72, 0.9f), 0);
        const auto r = test::render (p, midi, 48000 * 2);
        const auto* left = r.audio.getReadPointer (0);
        const auto* right = r.audio.getReadPointer (1);

        // Envelope of the left channel in 20 ms windows over the second second (the fast
        // rotor turns in about 150 ms).
        double lo = 1.0e9, hi = 0.0, diff = 0.0;
        for (int w = 48000; w + 960 <= 96000; w += 960)
        {
            double sum = 0.0;
            for (int i = w; i < w + 960; ++i)
            {
                sum += static_cast<double> (left[i]) * left[i];
                diff = juce::jmax (diff, static_cast<double> (std::abs (left[i] - right[i])));
            }
            lo = juce::jmin (lo, sum);
            hi = juce::jmax (hi, sum);
        }
        return std::pair { 10.0 * std::log10 (hi / juce::jmax (lo, 1.0e-20)), diff };
    };

    SECTION ("organ through the rotary: swirling, and different left and right")
    {
        NorgProcessor processor;
        test::prepare (processor);
        test::setParam (processor, "reverb_on", 0.0f);
        test::setParam (processor, "rotary_fast", 1.0f);
        const auto [depth, difference] = modulationDepth (processor);
        CHECK (depth > 3.0);
        CHECK (difference > 0.01);
    }

    SECTION ("rotary off: the organ is steady and mono")
    {
        NorgProcessor processor;
        test::prepare (processor);
        test::setParam (processor, "reverb_on", 0.0f);
        test::setParam (processor, "rotary_on", 0.0f);
        const auto [depth, difference] = modulationDepth (processor);
        CHECK (depth < 1.0);
        CHECK (difference == 0.0);
    }

    SECTION ("rotary on the piano instead")
    {
        NorgProcessor processor;
        test::prepare (processor);
        test::setParam (processor, "reverb_on", 0.0f);
        test::setParam (processor, "organ_on", 0.0f);
        test::setParam (processor, "piano_on", 1.0f);
        test::setParam (processor, "piano_type", 2.0f); // tine EP: steady sustain
        test::setParam (processor, "rotary_source", 1.0f);
        test::setParam (processor, "rotary_fast", 1.0f);
        const auto [depth, difference] = modulationDepth (processor);
        CHECK (difference > 0.001);
        CHECK (depth > 2.0);
    }
}

TEST_CASE ("an effect only processes the section it is assigned to", "[fx]")
{
    const auto render = [] (bool tremolo)
    {
        NorgProcessor processor;
        test::prepare (processor);
        test::setParam (processor, "reverb_on", 0.0f);
        test::setParam (processor, "rotary_on", 0.0f);
        test::setParam (processor, "fx1_on", tremolo ? 1.0f : 0.0f);
        test::setParam (processor, "fx1_source", 1.0f); // piano, which is off
        test::setParam (processor, "fx1_amount", 1.0f);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
        return test::render (processor, midi, 24000);
    };

    const auto dry = render (false);
    const auto wet = render (true);
    for (int i = 0; i < 24000; ++i)
        REQUIRE (dry.audio.getSample (0, i) == wet.audio.getSample (0, i));
}

TEST_CASE ("tempo-synced delay follows the master clock", "[fx][clock]")
{
    NorgProcessor processor;
    test::prepare (processor);
    for (auto [id, value] : std::initializer_list<std::pair<const char*, float>> {
             { "reverb_on", 0.0f }, { "organ_on", 0.0f }, { "piano_on", 1.0f }, { "piano_type", 4.0f },
             { "delay_on", 1.0f }, { "delay_source", 1.0f }, { "delay_sync", 1.0f }, { "delay_division", 0.0f },
             { "delay_feedback", 0.0f }, { "delay_mix", 1.0f }, { "clock_bpm", 100.0f } })
        test::setParam (processor, id, value);

    // A short clav stab; at 100 bpm a synced quarter-note echo comes 600 ms later.
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
    midi.addEvent (juce::MidiMessage::noteOff (1, 60), 2400);
    const auto r = test::render (processor, midi, 48000);

    const auto windowRms = [&] (int from, int to)
    {
        double sum = 0.0;
        for (int i = from; i < to; ++i)
            sum += static_cast<double> (r.audio.getSample (0, i)) * r.audio.getSample (0, i); // ping-pong: first echo left
        return std::sqrt (sum / (to - from));
    };

    const double beforeEcho = windowRms (22000, 28000);
    const double echo = windowRms (28800, 31200);
    INFO ("before " << beforeEcho << " echo " << echo);
    CHECK (echo > 4.0 * beforeEcho);
    CHECK (processor.clockTempo() == Catch::Approx (100.0));
}

namespace
{
    struct FakePlayHead final : juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override { return info; }
        PositionInfo info;
    };
}

TEST_CASE ("the master clock follows the host, or runs on its own", "[clock]")
{
    MasterClock clock;
    clock.prepare (48000.0);

    FakePlayHead host;
    host.info.setBpm (90.0);
    host.info.setIsPlaying (true);
    host.info.setPpqPosition (8.5);

    clock.advance (&host, true, 120.0, 512);
    CHECK (clock.bpm() == Catch::Approx (90.0));
    CHECK (clock.beat() == Catch::Approx (8.5));
    CHECK (clock.followingHost());

    clock.advance (&host, false, 120.0, 512);
    CHECK (clock.bpm() == Catch::Approx (120.0));
    CHECK_FALSE (clock.followingHost());

    // Free-running: one second at 120 bpm is two beats.
    clock.advance (nullptr, true, 120.0, 48000);
    const double start = clock.beat();
    clock.advance (nullptr, true, 120.0, 48000);
    CHECK (clock.beat() - start == Catch::Approx (2.0));
}

TEST_CASE ("tap tempo averages taps and starts over after a pause", "[clock]")
{
    TapTempo tap;
    CHECK_FALSE (tap.tap (10.0).has_value());
    CHECK (*tap.tap (10.5) == Catch::Approx (120.0));
    CHECK (*tap.tap (11.0) == Catch::Approx (120.0));
    CHECK (*tap.tap (11.6) == Catch::Approx (60.0 / (1.6 / 3.0)));

    CHECK_FALSE (tap.tap (20.0).has_value()); // long pause: a fresh start
    CHECK (*tap.tap (20.6) == Catch::Approx (100.0));
}
