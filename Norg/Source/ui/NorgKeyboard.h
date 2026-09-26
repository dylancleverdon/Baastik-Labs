#pragma once

#include "NorgTheme.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace norg::ui
{
    // The on-screen keybed: ivory whites, glossy blacks, red key-down glow, no note labels.
    class NorgKeyboard final : public juce::MidiKeyboardComponent
    {
    public:
        explicit NorgKeyboard (juce::MidiKeyboardState&);

        juce::String getWhiteNoteText (int) override { return {}; }
        void drawWhiteNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                            juce::Colour lineColour, juce::Colour textColour) override;
        void drawBlackNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                            juce::Colour fillColour) override;
    };
}
