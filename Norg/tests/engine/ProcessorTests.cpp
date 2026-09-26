#include "PluginProcessor.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

using namespace norg;

TEST_CASE ("parameter IDs are stable and Panel B copies are prefixed", "[params]")
{
    CHECK (paramId (P::mode) == "mode");
    CHECK (paramId (P::mode, 1) == "mode"); // global: a single copy
    CHECK (paramId (P::organVolume) == "organ_volume");
    CHECK (paramId (P::organVolume, 1) == "b_organ_volume");

    NorgProcessor processor;
    CHECK (processor.state().getParameter ("organ_volume") != nullptr);
    CHECK (processor.state().getParameter ("b_organ_volume") != nullptr);
    CHECK (processor.state().getParameter ("b_mode") == nullptr);
}

TEST_CASE ("a held note sounds and then decays to silence", "[engine]")
{
    NorgProcessor processor;
    test::prepare (processor);

    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 10);
    const auto held = test::render (processor, midi, 4800);
    CHECK (held.peak > 0.01f);
    CHECK_FALSE (held.hasNonFinite);

    midi.clear();
    midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
    test::render (processor, midi, 48000);
    midi.clear();
    const auto after = test::render (processor, midi, 4800);
    CHECK (after.peak < 1.0e-4f);
}

TEST_CASE ("blocks bigger than prepared are handled", "[engine]")
{
    NorgProcessor processor;
    test::prepare (processor, 48000.0, 128);

    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 64, 0.8f), 3000);
    const auto result = test::render (processor, midi, 4096, 4096);
    CHECK (result.peak > 0.01f);
    CHECK_FALSE (result.hasNonFinite);
}

TEST_CASE ("plugin state survives a save and reload", "[state]")
{
    juce::MemoryBlock saved;
    {
        NorgProcessor a;
        test::setParam (a, "mode", 1.0f);
        test::setParam (a, "organ_volume", 0.3f);
        test::setParam (a, "b_piano_on", 1.0f);
        a.getStateInformation (saved);
    }

    NorgProcessor b;
    b.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    CHECK (juce::roundToInt (test::getParam (b, "mode")) == 1);
    CHECK (std::abs (test::getParam (b, "organ_volume") - 0.3f) < 1.0e-5f);
    CHECK (test::getParam (b, "b_piano_on") > 0.5f);
}
