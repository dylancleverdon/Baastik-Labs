#include "NorgKeyboard.h"

namespace norg::ui
{
    NorgKeyboard::NorgKeyboard (juce::MidiKeyboardState& keyboardState)
        : juce::MidiKeyboardComponent (keyboardState, horizontalKeyboard)
    {
        setScrollButtonsVisible (false);
        setColour (shadowColourId, juce::Colours::black.withAlpha (0.5f));
        setColour (keySeparatorLineColourId, juce::Colour (0xff8f8b84));
    }

    void NorgKeyboard::drawWhiteNote (int, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                                      juce::Colour lineColour, juce::Colour)
    {
        const auto key = area.reduced (0.5f, 0.0f);
        juce::ColourGradient ivory (juce::Colour (0xfff9f7f1), key.getX(), key.getY(),
                                    juce::Colour (0xffe4e0d6), key.getX(), key.getBottom(), false);
        g.setGradientFill (ivory);
        g.fillRect (key);

        // Front edge of the key
        g.setColour (juce::Colour (0xffcfcac0));
        g.fillRect (key.withTop (key.getBottom() - 5.0f));

        if (isDown)
        {
            g.setColour (colours::panelRed.withAlpha (0.55f));
            g.fillRect (key);
        }
        else if (isOver)
        {
            g.setColour (colours::panelRed.withAlpha (0.12f));
            g.fillRect (key);
        }

        g.setColour (lineColour);
        g.fillRect (area.withWidth (1.0f));
    }

    void NorgKeyboard::drawBlackNote (int, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                                      juce::Colour)
    {
        g.setColour (juce::Colours::black);
        g.fillRect (area);

        // Glossy top surface, shorter at the front like a real key
        auto top = area.reduced (area.getWidth() * 0.12f, 0.0f).withTrimmedBottom (area.getHeight() * (isDown ? 0.06f : 0.12f));
        juce::ColourGradient gloss (juce::Colour (isDown ? 0xff3a2022 : 0xff2d2d30), top.getX(), top.getY(),
                                    juce::Colour (0xff09090a), top.getX(), top.getBottom(), false);
        g.setGradientFill (gloss);
        g.fillRect (top);

        if (isDown)
        {
            g.setColour (colours::panelRed.withAlpha (0.6f));
            g.fillRect (top);
        }
        else if (isOver)
        {
            g.setColour (colours::panelRed.withAlpha (0.18f));
            g.fillRect (top);
        }

        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawVerticalLine (static_cast<int> (top.getX() + 1.0f), top.getY(), top.getBottom());
    }
}
