#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Norg's look: a red stage-keyboard panel, white silkscreen, red LEDs and a small OLED.
namespace norg::ui
{
    namespace colours
    {
        inline const juce::Colour panelRed      { 0xffc81e2b };
        inline const juce::Colour panelRedDeep  { 0xffa5141f };
        inline const juce::Colour panelRedLight { 0xffdb2a36 };
        inline const juce::Colour cheek         { 0xff1b1b1d };
        inline const juce::Colour cheekEdge     { 0xff2e2e31 };
        inline const juce::Colour silkscreen    { 0xfff6f2ea };
        inline const juce::Colour silkscreenDim { 0xb3f6f2ea };
        inline const juce::Colour ledRed        { 0xffff3b22 };
        inline const juce::Colour ledOff        { 0xff5e1015 };
        inline const juce::Colour ledGreen      { 0xff5dff6a };
        inline const juce::Colour keyCap        { 0xff262628 };
        inline const juce::Colour keyCapHi      { 0xff3a3a3d };
        inline const juce::Colour knobBody      { 0xff161617 };
        inline const juce::Colour oledBack      { 0xff040506 };
        inline const juce::Colour oledText      { 0xffe6f1ff };
        inline const juce::Colour oledDim       { 0xff7f8c99 };
        inline const juce::Colour keybed        { 0xff0e0e0f };
    }

    struct Fonts
    {
        static juce::Font logo (float height);        // the "norg" wordmark
        static juce::Font label (float height);       // silkscreen labels
        static juce::Font labelBold (float height);   // section titles
        static juce::Font display (float height);     // OLED text
    };

    // Draws text in the silkscreen style: uppercase, lightly tracked.
    void drawSilkscreen (juce::Graphics&, const juce::String& text, juce::Rectangle<float> area,
                         juce::Justification = juce::Justification::centred, float height = 11.0f,
                         bool bold = false, juce::Colour = colours::silkscreen);
}
