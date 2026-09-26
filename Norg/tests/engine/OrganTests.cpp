#include "TestHelpers.h"
#include "engine/organ/OrganSection.h"
#include "engine/organ/TonewheelOrgan.h"

#include <catch2/catch_test_macros.hpp>

using namespace norg;
using namespace norg::organ;

namespace
{
    // Renders `samples` of the tonewheel organ's upper bus, returning its RMS.
    float renderUpperRms (TonewheelOrgan& organ, int samples)
    {
        double sum = 0.0;
        for (int done = 0; done < samples; done += TonewheelOrgan::maxChunk)
        {
            float upper[TonewheelOrgan::maxChunk] {}, lower[TonewheelOrgan::maxChunk] {}, pedal[TonewheelOrgan::maxChunk] {};
            organ.render (upper, lower, pedal, TonewheelOrgan::maxChunk);
            for (float s : upper)
                sum += static_cast<double> (s) * s;
        }
        return static_cast<float> (std::sqrt (sum / samples));
    }
}

TEST_CASE ("tonewheel frequencies follow the gear ratios", "[organ]")
{
    CHECK (TonewheelOrgan::wheelFrequency (46) == 440.0);             // A4, exactly
    CHECK (std::abs (TonewheelOrgan::wheelFrequency (1) - 32.69) < 0.01); // C1

    for (int w = 1; w <= TonewheelOrgan::numWheels; ++w)
    {
        const double equalTempered = 440.0 * std::pow (2.0, (w - 46) / 12.0);
        const double cents = 1200.0 * std::log2 (TonewheelOrgan::wheelFrequency (w) / equalTempered);
        INFO ("wheel " << w << " is " << cents << " cents off");
        CHECK (std::abs (cents) < 2.0);
    }
}

TEST_CASE ("drawbars map to the right wheels, folding back at both ends", "[organ]")
{
    CHECK (TonewheelOrgan::wheelFor (69, 2) == 46);      // A4, 8'
    CHECK (TonewheelOrgan::wheelFor (69, 0) == 34);      // 16' an octave down
    CHECK (TonewheelOrgan::wheelFor (69, 1) == 53);      // 5 1/3' a fifth up
    CHECK (TonewheelOrgan::wheelFor (36, 0) == 1);       // lowest C, 16' is wheel 1

    for (int note = 0; note < 128; ++note)
        for (int d = 0; d < 9; ++d)
        {
            const int w = TonewheelOrgan::wheelFor (note, d);
            CHECK ((w >= 1 && w <= TonewheelOrgan::numWheels));
        }

    // The top of the keyboard folds the 1' drawbar back down an octave.
    CHECK (TonewheelOrgan::wheelFor (96, 8) == TonewheelOrgan::wheelFor (96, 5)); // C7: 1' folds onto the 2' wheel
}

TEST_CASE ("drawbar steps are about 3 dB", "[organ]")
{
    CHECK (OrganSection::drawbarGain (0) == 0.0f);
    CHECK (OrganSection::drawbarGain (8) == 1.0f);
    const float db = 20.0f * std::log10 (OrganSection::drawbarGain (7) / OrganSection::drawbarGain (8));
    CHECK (std::abs (db + 3.0f) < 0.01f);
}

TEST_CASE ("percussion is single-triggered and decays", "[organ]")
{
    TonewheelOrgan organ;
    organ.prepare (48000.0);
    organ.setDrawbars (upperBus, {}); // only the percussion should sound
    organ.setPercussion (true, true, true, false);
    organ.setClick (0.0f);
    organ.setTonewheelMode (2);

    organ.keyDown (60, upperBus);
    const float attack = renderUpperRms (organ, 2400);
    const float later = renderUpperRms (organ, 24000);
    CHECK (attack > 0.05f);
    CHECK (later < attack * 0.5f);

    // A legato key doesn't restart it...
    organ.keyDown (64, upperBus);
    const float legato = renderUpperRms (organ, 2400);
    CHECK (legato < attack * 0.5f);

    // ...but a detached one does.
    organ.keyUp (60);
    organ.keyUp (64);
    renderUpperRms (organ, 4800);
    organ.keyDown (67, upperBus);
    CHECK (renderUpperRms (organ, 2400) > attack * 0.7f);
}

TEST_CASE ("organ split sends notes to pedals, lower and upper", "[organ]")
{
    NorgProcessor processor;
    test::setParam (processor, "organ_split", 1.0f);
    test::setParam (processor, "organ_split_point", 60.0f);
    test::setParam (processor, "organ_pedal_point", 48.0f);

    ParamSnapshot snapshot;
    snapshot.resetToDefaults();
    ParamTable (processor.state()).capture (snapshot);

    OrganSection organ;
    organ.prepare (48000.0, 256);
    organ.setParameters (snapshot, 0);

    CHECK (organ.busForNote (40) == pedalBus);
    CHECK (organ.busForNote (50) == lowerBus);
    CHECK (organ.busForNote (72) == upperBus);
}

TEST_CASE ("every organ model sounds and stays finite", "[organ]")
{
    for (int model = 0; model < 4; ++model)
    {
        DYNAMIC_SECTION ("model " << model)
        {
            NorgProcessor processor;
            test::prepare (processor);
            test::setParam (processor, "organ_model", static_cast<float> (model));
            test::setParam (processor, "organ_preset", 1.0f); // all drawbars out
            test::setParam (processor, "organ_perc_on", 1.0f);
            test::setParam (processor, "organ_vib_on", 1.0f);
            test::setParam (processor, "rotary_fast", 1.0f);
            test::setParam (processor, "rotary_drive", 1.0f);

            juce::MidiBuffer midi;
            for (int n : { 36, 48, 60, 64, 67, 72, 84, 96, 100 })
                midi.addEvent (juce::MidiMessage::noteOn (1, n, 0.8f), 5);
            const auto result = test::render (processor, midi, 24000);

            CHECK (result.peak > 0.02f);
            CHECK (result.peak < 1.5f);
            CHECK_FALSE (result.hasNonFinite);
        }
    }
}
