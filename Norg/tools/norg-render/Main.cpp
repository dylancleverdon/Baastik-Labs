// norg-render: plays MIDI through the real Norg processor and writes a WAV file.
//
//   norg-render --out demo.wav [--seconds 8] [--rate 48000] [--block 256]
//               [--program A:1:3 | "Live 2" | init]  (the sound to start from; default: init, every parameter at its default)
//               [--set param_id=value ...]      (plain parameter values, e.g. --set mode=1 --set organ_volume=0.6)
//               [--midi song.mid]               (otherwise a short built-in demo phrase is played)
//               [--library grand=/path/x.sfz]   (sample library for the grand / upright / sample slot)
//   norg-render --screenshot panel.png [--width 1400] [--set ...]   (renders the editor to a PNG instead)

#include "PluginProcessor.h"

#include <iostream>

namespace
{
    juce::String argValue (const juce::StringArray& args, const juce::String& name, const juce::String& fallback = {})
    {
        const int i = args.indexOf (name);
        return i >= 0 && i + 1 < args.size() ? args[i + 1] : fallback;
    }

    // A few bars of chords and a little melody, enough to hear any section.
    juce::MidiMessageSequence demoPhrase()
    {
        juce::MidiMessageSequence seq;
        const auto chord = [&seq] (double start, double length, std::initializer_list<int> notes, float velocity)
        {
            for (int n : notes)
            {
                seq.addEvent (juce::MidiMessage::noteOn (1, n, velocity), start);
                seq.addEvent (juce::MidiMessage::noteOff (1, n), start + length);
            }
        };

        chord (0.0, 1.8, { 48, 55, 60, 64, 67 }, 0.8f);
        chord (2.0, 1.8, { 45, 52, 57, 60, 64 }, 0.7f);
        chord (4.0, 0.9, { 41, 53, 57, 60, 65 }, 0.9f);
        chord (5.0, 0.9, { 43, 55, 59, 62, 67 }, 0.6f);
        int step = 0;
        for (int n : { 72, 74, 76, 79, 76, 74, 72 })
            chord (6.0 + 0.2 * step++, 0.18, { n }, 0.75f);
        seq.updateMatchedPairs();
        return seq;
    }

    juce::MidiMessageSequence loadMidi (const juce::File& file)
    {
        juce::MidiMessageSequence merged;
        juce::FileInputStream in (file);
        juce::MidiFile midi;
        if (! in.openedOk() || ! midi.readFrom (in))
            return merged;

        midi.convertTimestampTicksToSeconds();
        for (int t = 0; t < midi.getNumTracks(); ++t)
            merged.addSequence (*midi.getTrack (t), 0.0);
        merged.updateMatchedPairs();
        return merged;
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juce;

    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (argv[i]);

    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argValue (args, "--out", "norg-render.wav"));
    const double rate = argValue (args, "--rate", "48000").getDoubleValue();
    const int block = argValue (args, "--block", "256").getIntValue();
    const double seconds = argValue (args, "--seconds", "8").getDoubleValue();

    std::unique_ptr<juce::AudioProcessor> plugin (createPluginFilter());
    auto* norg = dynamic_cast<norg::NorgProcessor*> (plugin.get());
    if (norg == nullptr)
        return 1;

    // Start from a known sound: Init unless a program is named.
    if (const auto name = argValue (args, "--program", "init"); name.equalsIgnoreCase ("init"))
        norg::perform::apply (norg::perform::Program(), norg->state());
    else if (const auto location = norg::perform::Location::fromLabel (name))
        norg->loadProgram (*location);
    else
    {
        std::cerr << "unknown program: " << name << " (use A:1:1 .. H:10:5, \"Live 1\" .. \"Live 5\" or init)" << std::endl;
        return 1;
    }

    for (int i = 0; i < args.size(); ++i)
    {
        if (args[i] != "--set" || i + 1 >= args.size())
            continue;

        const auto id = args[i + 1].upToFirstOccurrenceOf ("=", false, false);
        const auto value = args[i + 1].fromFirstOccurrenceOf ("=", false, false).getFloatValue();
        if (auto* param = norg->state().getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
        else
            std::cerr << "unknown parameter: " << id << std::endl;
    }

    if (const auto shot = argValue (args, "--screenshot"); shot.isNotEmpty())
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (plugin->createEditorAndMakeActive());
        const int width = argValue (args, "--width", "1400").getIntValue();
        editor->setSize (width, width * editor->getHeight() / juce::jmax (1, editor->getWidth()));
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);

        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (shot);
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat png;
        if (! stream.openedOk() || ! png.writeImageToStream (image, stream))
            return 1;
        std::cout << "wrote " << file.getFullPathName() << std::endl;
        plugin->editorBeingDeleted (editor.get());
        return 0;
    }

    for (int i = 0; i < args.size(); ++i)
    {
        if (args[i] != "--library" || i + 1 >= args.size())
            continue;
        const auto use = args[i + 1].upToFirstOccurrenceOf ("=", false, false);
        const auto path = juce::File::getCurrentWorkingDirectory().getChildFile (args[i + 1].fromFirstOccurrenceOf ("=", false, false)).getFullPathName();
        norg->setLibraryChoice (0, use == "upright" ? norg::NorgProcessor::LibraryUse::upright
                                 : use == "sample" ? norg::NorgProcessor::LibraryUse::sample
                                                   : norg::NorgProcessor::LibraryUse::grand, path);
    }

    const auto loadStart = juce::Time::getMillisecondCounterHiRes();
    if (! norg->libraryManager().waitUntilIdle (10 * 60 * 1000))
        std::cerr << "sample libraries are still loading" << std::endl;
    for (int slot = 0; slot < 3; ++slot)
    {
        const auto st = norg->libraryManager().status (slot);
        if (st.state == norg::sfz::LibraryManager::State::ready)
            std::cout << "library slot " << slot << ": " << st.name << " loaded in "
                      << juce::roundToInt (juce::Time::getMillisecondCounterHiRes() - loadStart) << " ms" << std::endl;
        else if (st.state == norg::sfz::LibraryManager::State::failed)
            std::cout << "library slot " << slot << " failed: " << st.error << std::endl;
    }

    const auto midiPath = argValue (args, "--midi");
    const auto sequence = midiPath.isNotEmpty() ? loadMidi (juce::File::getCurrentWorkingDirectory().getChildFile (midiPath))
                                                : demoPhrase();

    plugin->setPlayConfigDetails (0, 2, rate, block);
    plugin->prepareToPlay (rate, block);

    const auto totalSamples = static_cast<int> (seconds * rate);
    juce::AudioBuffer<float> result (2, totalSamples);
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    int eventIndex = 0;

    for (int start = 0; start < totalSamples; start += block)
    {
        const int n = juce::jmin (block, totalSamples - start);
        buffer.setSize (2, n, false, false, true);
        midi.clear();

        while (eventIndex < sequence.getNumEvents())
        {
            const auto& message = sequence.getEventPointer (eventIndex)->message;
            const auto samplePos = static_cast<int> (message.getTimeStamp() * rate);
            if (samplePos >= start + n)
                break;
            midi.addEvent (message, juce::jmax (0, samplePos - start));
            ++eventIndex;
        }

        plugin->processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            result.copyFrom (ch, start, buffer, ch, 0, n);
    }

    plugin->releaseResources();

    out.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (out.createOutputStream());
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions()
                                                   .withSampleRate (rate)
                                                   .withNumChannels (2)
                                                   .withBitsPerSample (24));
    if (writer == nullptr)
    {
        std::cerr << "could not write " << out.getFullPathName() << std::endl;
        return 1;
    }
    writer->writeFromAudioSampleBuffer (result, 0, totalSamples);

    std::cout << "wrote " << out.getFullPathName() << "  peak " << result.getMagnitude (0, totalSamples)
              << "  rms " << result.getRMSLevel (0, 0, totalSamples) << std::endl;
    return 0;
}
