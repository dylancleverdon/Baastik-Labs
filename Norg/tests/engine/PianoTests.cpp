#include "TestHelpers.h"
#include "engine/piano/ModeledPiano.h"
#include "engine/piano/PianoCommon.h"
#include "engine/piano/StringResonance.h"

#include <catch2/catch_test_macros.hpp>

using namespace norg;

namespace
{
    // RMS of a piano engine after a note is released, with the given pedal position.
    float tailAfterRelease (piano::PianoEngine& engine, int note, float pedal)
    {
        engine.reset();
        engine.setPedal (pedal);
        engine.noteOn (note, 0.8f);
        std::vector<float> l (48000), r (48000);
        engine.render (l.data(), r.data(), 4800);
        engine.noteOff (note);
        std::fill (l.begin(), l.end(), 0.0f);
        engine.render (l.data(), r.data(), 24000); // half a second later...
        std::fill (l.begin(), l.end(), 0.0f);
        engine.render (l.data(), r.data(), 4800);
        double sum = 0.0;
        for (int i = 0; i < 4800; ++i)
            sum += static_cast<double> (l[static_cast<size_t> (i)]) * l[static_cast<size_t> (i)];
        return static_cast<float> (std::sqrt (sum / 4800.0));
    }
}

TEST_CASE ("damper curve: key held or pedal down means free strings", "[piano]")
{
    CHECK (piano::damperAmount (true, 0.0f) == 0.0f);
    CHECK (piano::damperAmount (false, 1.0f) == 0.0f);
    CHECK (piano::damperAmount (false, 0.0f) == 1.0f);
    const float half = piano::damperAmount (false, 0.5f);
    CHECK ((half > 0.2f && half < 0.8f));
}

TEST_CASE ("stretch tuning: bass flat, treble sharp", "[piano]")
{
    CHECK (piano::stretchCents (69) == 0.0f);
    CHECK (piano::stretchCents (21) < -5.0f);
    CHECK (piano::stretchCents (108) > 10.0f);
}

TEST_CASE ("half-pedal gives a release between damped and free", "[piano]")
{
    piano::ModeledPiano grand;
    grand.prepare (48000.0, 48000);

    const float up = tailAfterRelease (grand, 60, 0.0f);
    const float half = tailAfterRelease (grand, 60, 0.5f);
    const float down = tailAfterRelease (grand, 60, 1.0f);

    INFO ("up " << up << "  half " << half << "  down " << down);
    CHECK (up < half);
    CHECK (half < down);
    CHECK (up < down * 0.01f);
}

TEST_CASE ("the top octave has no dampers", "[piano]")
{
    piano::ModeledPiano grand;
    grand.prepare (48000.0, 48000);

    const float damped = tailAfterRelease (grand, 84, 0.0f);
    const float undamped = tailAfterRelease (grand, 96, 0.0f);
    CHECK (piano::ModeledPiano::isUndamped (96));
    CHECK_FALSE (piano::ModeledPiano::isUndamped (84));
    CHECK (undamped > damped * 10.0f);
}

TEST_CASE ("every piano type sounds, stays finite and falls silent", "[piano]")
{
    for (int type = 0; type < 5; ++type)
    {
        DYNAMIC_SECTION ("type " << type)
        {
            NorgProcessor processor;
            test::prepare (processor);
            test::setParam (processor, "organ_on", 0.0f);
            test::setParam (processor, "piano_on", 1.0f);
            test::setParam (processor, "piano_type", static_cast<float> (type));
            test::setParam (processor, "piano_sampled", 0.0f); // modelled, no library needed
            test::setParam (processor, "piano_timbre", 3.0f);

            juce::MidiBuffer midi;
            for (int n : { 28, 40, 52, 60, 64, 67, 72, 88, 100 })
                midi.addEvent (juce::MidiMessage::noteOn (1, n, 1.0f), 10);
            const auto held = test::render (processor, midi, 12000);
            CHECK (held.peak > 0.02f);
            CHECK (held.peak < 1.2f);
            CHECK_FALSE (held.hasNonFinite);

            midi.clear();
            for (int n : { 28, 40, 52, 60, 64, 67, 72, 88, 100 })
                midi.addEvent (juce::MidiMessage::noteOff (1, n), 0);
            test::render (processor, midi, 48000 * 6);
            midi.clear();
            const auto after = test::render (processor, midi, 4800);
            CHECK (after.peak < 1.0e-3f);
        }
    }
}

TEST_CASE ("string resonance rings with the pedal down and stops when damped", "[piano]")
{
    const auto ringAfterImpulse = [] (float pedal)
    {
        piano::StringResonance res;
        res.prepare (48000.0);
        std::array<bool, 128> keys {};
        res.setDamping (keys, pedal);

        // One second of a C4 partial from some other struck note, then silence.
        std::vector<float> l (72000, 0.0f), r (72000, 0.0f);
        for (size_t i = 0; i < 48000; ++i)
            l[i] = r[i] = 0.3f * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * 261.63 * static_cast<double> (i) / 48000.0));
        res.process (l.data(), r.data(), 72000);

        float late = 0.0f, peak = 0.0f;
        for (size_t i = 0; i < l.size(); ++i)
        {
            peak = juce::jmax (peak, std::abs (l[i]));
            if (i > 48000 + 14400) // 0.3 s after the tone stopped
                late = juce::jmax (late, std::abs (l[i]));
        }
        return std::pair { late, peak };
    };

    const auto [freeRing, freePeak] = ringAfterImpulse (1.0f);
    const auto [dampedRing, dampedPeak] = ringAfterImpulse (0.0f);

    INFO ("free " << freeRing << "  damped " << dampedRing);
    CHECK (freeRing > 0.01f);                   // the C4 string keeps singing after the tone stops...
    CHECK (dampedRing < freeRing * 1.0e-3f);    // ...unless the dampers are down
    CHECK (freePeak < 1.5f);
}

TEST_CASE ("every clav pickup and filter combination stays in range", "[piano]")
{
    for (int pickup = 0; pickup < 4; ++pickup)
        for (int filter = 0; filter < 4; ++filter)
        {
            NorgProcessor processor;
            test::prepare (processor);
            test::setParam (processor, "organ_on", 0.0f);
            test::setParam (processor, "piano_on", 1.0f);
            test::setParam (processor, "piano_type", 4.0f);
            test::setParam (processor, "piano_clav_pickup", static_cast<float> (pickup));
            test::setParam (processor, "piano_clav_filter", static_cast<float> (filter));

            juce::MidiBuffer midi;
            for (int n : { 40, 52, 59, 64, 67, 76 })
                midi.addEvent (juce::MidiMessage::noteOn (1, n, 1.0f), 10);
            const auto result = test::render (processor, midi, 24000);

            INFO ("pickup " << pickup << " filter " << filter << " peak " << result.peak);
            CHECK (result.peak > 0.05f);
            CHECK (result.peak < 1.0f);
        }
}
