#include "TestHelpers.h"
#include "perform/ProgramLibrary.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace norg;
using perform::Location;
using perform::Program;

// These tests store programs only in bank H, so every other test's processor still starts on the
// Init sound at A:1:1 of the (empty) test library.
namespace
{
    Location bankH (int page, int slot) { return Location::program (7, page, slot); }

    double rmsOf (const test::RenderResult& r, int from, int to)
    {
        double sum = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = from; i < to; ++i)
                sum += static_cast<double> (r.audio.getSample (ch, i)) * r.audio.getSample (ch, i);
        return std::sqrt (sum / (2.0 * (to - from)));
    }

    Program organOnly()
    {
        Program p;
        p.setName ("Organ only");
        p.set (P::reverbOn, 0);
        p.set (P::rotaryOn, 0);
        return p;
    }

    Program tineOnly()
    {
        Program p;
        p.setName ("Tine only");
        p.set (P::organOn, 0);
        p.set (P::pianoOn, 1);
        p.set (P::pianoType, 2);
        p.set (P::reverbOn, 0);
        return p;
    }
}

TEST_CASE ("program locations have labels and indexes", "[programs]")
{
    CHECK (Location::program (0, 0, 0).label() == "A:1:1");
    CHECK (Location::program (7, 9, 4).label() == "H:10:5");
    CHECK (Location::liveSlot (2).label() == "Live 3");

    for (int i : { 0, 1, 49, 50, 399 })
        CHECK (Location::fromIndex (i).index() == i);
    CHECK_FALSE (Location::program (8, 0, 0).isValid());
    CHECK (Location::program (1, 2, 3) != Location::program (1, 2, 4));
}

TEST_CASE ("programs store only what differs from the defaults", "[programs]")
{
    Program p;
    CHECK (p.toValueTree().getNumProperties() == 0);
    CHECK (p.get (P::organVolume) == Catch::Approx (spec (P::organVolume).defaultValue));

    p.set (P::organVolume, 0, 0.3f);
    p.set (P::pianoOn, 1, 1.0f); // Panel B
    CHECK (p.get (P::organVolume) == Catch::Approx (0.3f));
    CHECK (p.get (P::pianoOn, 1) == Catch::Approx (1.0f));
    CHECK (p.get (P::pianoOn, 0) == Catch::Approx (0.0f));

    p.set (P::organVolume, 0, spec (P::organVolume).defaultValue); // back to default: dropped
    CHECK (p.toValueTree().getNumProperties() == 1);

    p.set (P::delayFeedback, 0, 7.0f); // clamped to the parameter's range
    CHECK (p.get (P::delayFeedback) == Catch::Approx (spec (P::delayFeedback).maxValue));

    CHECK_FALSE (Program::isInProgram (P::masterVolume));
    CHECK (Program::isInProgram (P::mode));
}

TEST_CASE ("copying a part copies that part only", "[programs]")
{
    Program from, to;
    from.set (P::organModel, 0, 2);
    from.set (P::organDbI4, 0, 6);
    from.set (P::pianoType, 0, 4);
    from.setLibrary ("lib_grand", "/some/grand.sfz");

    to.copyPart (perform::Part::organ, from, 0, 0);
    CHECK (to.get (P::organModel) == Catch::Approx (2));
    CHECK (to.get (P::organDbI4) == Catch::Approx (6));
    CHECK (to.get (P::pianoType) == Catch::Approx (spec (P::pianoType).defaultValue));
    CHECK (to.getLibrary ("lib_grand").isEmpty());

    to.copyPart (perform::Part::piano, from, 0, 1); // into Panel B
    CHECK (to.get (P::pianoType, 1) == Catch::Approx (4));
    CHECK (to.getLibrary ("b_lib_grand") == "/some/grand.sfz");
}

TEST_CASE ("the program library starts with factory programs and keeps what's stored", "[programs]")
{
    const juce::TemporaryFile file (".norglib");
    file.getFile().deleteFile();

    {
        perform::ProgramLibrary library (file.getFile());
        CHECK (file.getFile().existsAsFile());
        CHECK (library.get (Location::program (0, 0, 0)).getName() == "Norg Rock B3");
        CHECK (library.get (Location::program (0, 0, 2)).get (P::pianoOn) == Catch::Approx (1.0f));
        CHECK (library.get (Location::program (5, 5, 0)).displayName() == "Init");

        auto p = tineOnly();
        library.store (Location::program (1, 3, 2), p);
        library.store (Location::liveSlot (4), organOnly());
    }

    perform::ProgramLibrary reopened (file.getFile());
    CHECK (reopened.get (Location::program (1, 3, 2)).sameSoundAs (tineOnly()));
    CHECK (reopened.get (Location::program (1, 3, 2)).getName() == "Tine only");
    CHECK (reopened.get (Location::liveSlot (4)).getName() == "Organ only");

    // Another Norg stores a program: this one sees it without restarting.
    perform::ProgramLibrary other (file.getFile());
    juce::Thread::sleep (1100); // file times have one-second resolution on some filesystems
    other.rename (Location::program (1, 3, 2), "Renamed elsewhere");
    CHECK (reopened.reloadIfChangedOnDisk());
    CHECK (reopened.get (Location::program (1, 3, 2)).getName() == "Renamed elsewhere");
}

TEST_CASE ("loading, editing and storing programs", "[programs]")
{
    NorgProcessor processor;
    test::prepare (processor);

    CHECK (processor.currentLocation() == Location::program (0, 0, 0));
    CHECK (processor.currentProgramName() == "Init");
    CHECK_FALSE (processor.isModified());

    processor.programLibrary().store (bankH (0, 0), tineOnly());
    processor.loadProgram (bankH (0, 0));
    CHECK (processor.currentProgramName() == "Tine only");
    CHECK (test::getParam (processor, "piano_on") == Catch::Approx (1.0f));
    CHECK (test::getParam (processor, "organ_on") == Catch::Approx (0.0f));
    CHECK_FALSE (processor.isModified());

    // A tweak marks the program as edited; storing it clears that.
    test::setParam (processor, "piano_volume", 0.4f);
    CHECK (processor.isModified());
    processor.storeProgram (bankH (0, 1), "Quieter tine");
    CHECK_FALSE (processor.isModified());
    CHECK (processor.currentLocation() == bankH (0, 1));
    CHECK (processor.programLibrary().get (bankH (0, 1)).get (P::pianoVolume) == Catch::Approx (0.4f));

    // Loading resets everything the program doesn't mention, including the edit.
    processor.loadProgram (bankH (0, 0));
    CHECK (test::getParam (processor, "piano_volume") == Catch::Approx (spec (P::pianoVolume).defaultValue));
}

TEST_CASE ("a held note keeps its old sound across a program change", "[programs][seamless]")
{
    NorgProcessor processor;
    test::prepare (processor);
    processor.programLibrary().store (bankH (1, 0), organOnly());
    processor.programLibrary().store (bankH (1, 1), tineOnly());
    processor.loadProgram (bankH (1, 0));

    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
    const auto organ = test::render (processor, midi, 4800);
    const double organLevel = rmsOf (organ, 2400, 4800);
    REQUIRE (organLevel > 0.01);

    // Switch to a program with the organ off: the held organ note carries on regardless.
    processor.loadProgram (bankH (1, 1));
    const auto held = test::render (processor, {}, 9600);
    CHECK (rmsOf (held, 4800, 9600) > organLevel * 0.8);

    // A new note plays the new sound, alongside the held one.
    midi.clear();
    midi.addEvent (juce::MidiMessage::noteOn (1, 72, 0.9f), 0);
    const auto layered = test::render (processor, midi, 4800);
    CHECK (rmsOf (layered, 2400, 4800) > rmsOf (held, 4800, 9600));

    // Releasing the held note lets the old sound go.
    midi.clear();
    midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
    midi.addEvent (juce::MidiMessage::noteOff (1, 72), 0);
    test::render (processor, midi, 48000 * 3);
    const auto after = test::render (processor, {}, 4800);
    CHECK (after.peak < 1.0e-3f);
}

TEST_CASE ("the sustain pedal carries across a program change", "[programs][seamless]")
{
    NorgProcessor processor;
    test::prepare (processor);
    processor.programLibrary().store (bankH (2, 0), tineOnly());
    auto clav = tineOnly();
    clav.set (P::pianoType, 4);
    processor.programLibrary().store (bankH (2, 1), clav);
    processor.loadProgram (bankH (2, 0));

    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 0);
    test::render (processor, midi, 480);

    processor.loadProgram (bankH (2, 1));

    // Pedal still down: a note played and released on the new sound keeps ringing.
    midi.clear();
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
    midi.addEvent (juce::MidiMessage::noteOff (1, 60), 480);
    const auto sustained = test::render (processor, midi, 24000);
    CHECK (rmsOf (sustained, 19200, 24000) > 0.005);

    midi.clear();
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), 0);
    test::render (processor, midi, 48000 * 2);
    CHECK (test::render (processor, {}, 4800).peak < 1.0e-3f);
}

TEST_CASE ("MIDI program change and bank select load programs", "[programs]")
{
    NorgProcessor processor;
    test::prepare (processor);
    processor.programLibrary().store (Location::program (7, 1, 3), tineOnly());

    // Bank H is 7; program 8 is page 2, program 4.
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 0, 7), 0);
    midi.addEvent (juce::MidiMessage::programChange (1, 8), 1);
    test::render (processor, midi, 256);
    processor.processPendingProgramChange();

    CHECK (processor.currentLocation() == Location::program (7, 1, 3));
    CHECK (processor.currentProgramName() == "Tine only");
}

TEST_CASE ("undo and redo step through edits", "[programs]")
{
    NorgProcessor processor;
    auto* volume = processor.state().getParameter ("organ_volume");
    const float original = volume->getValue();

    volume->beginChangeGesture();
    volume->setValueNotifyingHost (0.2f);
    volume->endChangeGesture();
    volume->beginChangeGesture();
    volume->setValueNotifyingHost (0.1f);
    volume->endChangeGesture();

    REQUIRE (processor.canUndo());
    processor.undo();
    CHECK (volume->getValue() == Catch::Approx (0.2f));
    processor.undo();
    CHECK (volume->getValue() == Catch::Approx (original));
    CHECK_FALSE (processor.canUndo());

    processor.redo();
    CHECK (volume->getValue() == Catch::Approx (0.2f));
}

TEST_CASE ("compare switches between the stored program and the edit", "[programs]")
{
    NorgProcessor processor;
    processor.programLibrary().store (bankH (3, 0), tineOnly());
    processor.loadProgram (bankH (3, 0));

    test::setParam (processor, "piano_type", 4.0f);
    processor.toggleCompare();
    CHECK (processor.isComparing());
    CHECK (test::getParam (processor, "piano_type") == Catch::Approx (2.0f));
    processor.toggleCompare();
    CHECK_FALSE (processor.isComparing());
    CHECK (test::getParam (processor, "piano_type") == Catch::Approx (4.0f));
    CHECK (processor.isModified());
}

TEST_CASE ("Live mode keeps edits without storing", "[programs]")
{
    NorgProcessor processor;
    processor.setLiveMode (true);
    CHECK (processor.currentLocation() == Location::liveSlot (0));

    test::setParam (processor, "organ_model", 2.0f);
    processor.loadProgram (Location::liveSlot (1)); // leaving the slot saves it
    CHECK (test::getParam (processor, "organ_model") == Catch::Approx (spec (P::organModel).defaultValue));

    processor.loadProgram (Location::liveSlot (0));
    CHECK (test::getParam (processor, "organ_model") == Catch::Approx (2.0f));

    processor.setLiveMode (false);
    CHECK_FALSE (processor.isLiveMode());

    // Leave the shared test library's Live slot as we found it.
    processor.programLibrary().store (Location::liveSlot (0), Program());
}

TEST_CASE ("a part can be copied from one program and pasted into another", "[programs]")
{
    NorgProcessor processor;
    auto organ = organOnly();
    organ.set (P::organModel, 3);
    organ.set (P::organDbI5, 7);
    processor.programLibrary().store (bankH (4, 0), organ);
    processor.programLibrary().store (bankH (4, 1), tineOnly());

    processor.loadProgram (bankH (4, 0));
    processor.copyPart (perform::Part::organ);
    CHECK_FALSE (processor.canPaste (perform::Part::piano));

    processor.loadProgram (bankH (4, 1));
    processor.pastePart (perform::Part::organ);
    CHECK (test::getParam (processor, "organ_model") == Catch::Approx (3.0f));
    CHECK (test::getParam (processor, "organ_db1_5") == Catch::Approx (7.0f));
    CHECK (test::getParam (processor, "organ_on") == Catch::Approx (1.0f)); // the organ's own switch comes too
    CHECK (test::getParam (processor, "piano_on") == Catch::Approx (1.0f)); // the piano is untouched
    CHECK (processor.isModified());
}

TEST_CASE ("a project remembers its program and edit state", "[programs][state]")
{
    juce::MemoryBlock saved;
    {
        NorgProcessor processor;
        processor.programLibrary().store (bankH (5, 0), tineOnly());
        processor.loadProgram (bankH (5, 0));
        test::setParam (processor, "piano_volume", 0.33f);
        processor.getStateInformation (saved);
    }

    NorgProcessor restored;
    restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    CHECK (restored.currentLocation() == bankH (5, 0));
    CHECK (restored.currentProgramName() == "Tine only");
    CHECK (restored.isModified());
    CHECK (test::getParam (restored, "piano_volume") == Catch::Approx (0.33f));
}

TEST_CASE ("program labels parse back into locations", "[programs]")
{
    CHECK (Location::fromLabel ("A:1:1") == Location::program (0, 0, 0));
    CHECK (Location::fromLabel ("h:10:5") == Location::program (7, 9, 4));
    CHECK (Location::fromLabel ("Live 3") == Location::liveSlot (2));
    CHECK_FALSE (Location::fromLabel ("I:1:1").has_value());
    CHECK_FALSE (Location::fromLabel ("A:11:1").has_value());
    CHECK_FALSE (Location::fromLabel ("Live 6").has_value());
    CHECK_FALSE (Location::fromLabel ("organ").has_value());
}
