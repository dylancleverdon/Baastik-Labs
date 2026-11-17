#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace theme
{
    inline const juce::Colour background { 0xff15161a };
    inline const juce::Colour card { 0xff1f2128 };
    inline const juce::Colour cardHi { 0xff2a2d36 };
    inline const juce::Colour text { 0xffededf0 };
    inline const juce::Colour dim { 0xff9a9dab };
    inline const juce::Colour faint { 0xff5c5f6b };
    inline const juce::Colour good { 0xff7ed957 };
    inline const juce::Colour warn { 0xffffc94a };
    inline const juce::Colour bad { 0xffff6b6b };
    inline const juce::Colour accent { 0xffff6b4a };

    // One colour per member (index stored in settings and shared with the others).
    inline juce::Colour member (int index)
    {
        static const juce::uint32 colours[] = { 0xffff6b4a, 0xff4aa8ff, 0xff7ed957, 0xffffc94a,
                                                0xffb57cff, 0xffff5da2, 0xff4ae3d2, 0xffff9f43 };
        return juce::Colour (colours[((index % 8) + 8) % 8]);
    }

    // Typographic symbols, built from UTF-8 explicitly (JUCE asserts on raw non-ASCII literals).
    inline juce::String middot() { return juce::String::fromUTF8 ("\xc2\xb7"); }
    inline juce::String play()   { return juce::String::fromUTF8 ("\xe2\x96\xb6"); }
    inline juce::String recDot() { return juce::String::fromUTF8 ("\xe2\x97\x8f"); }

    inline juce::Font font (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (size).withStyle (bold ? "Bold" : "Regular"));
    }

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel()
        {
            setColourScheme ({ background, card, card, faint, text, accent, juce::Colours::white, cardHi, text });
            setColour (juce::TextButton::buttonColourId, cardHi);
            setColour (juce::TextButton::textColourOffId, text);
            setColour (juce::TextEditor::backgroundColourId, card);
            setColour (juce::TextEditor::outlineColourId, faint);
            setColour (juce::TextEditor::focusedOutlineColourId, accent);
            setColour (juce::ToggleButton::textColourId, text);
            setColour (juce::ToggleButton::tickColourId, accent);
            setColour (juce::ScrollBar::thumbColourId, faint);
        }

        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& colour, bool over, bool down) override
        {
            auto r = b.getLocalBounds().toFloat().reduced (0.5f);
            auto c = colour;
            if (down)
                c = c.brighter (0.15f);
            else if (over)
                c = c.brighter (0.08f);
            g.setColour (c);
            g.fillRoundedRectangle (r, 7.0f);
        }

        juce::Font getTextButtonFont (juce::TextButton&, int height) override
        {
            return font (juce::jmin (14.0f, height * 0.5f), true);
        }
    };
}
