#pragma once

#include "NorgTheme.h"

namespace norg::ui
{
    class NorgLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        NorgLookAndFeel();

        // Black knob with a white pointer, inside a ring of red LEDs showing the value.
        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                               float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

        juce::Font getPopupMenuFont() override { return Fonts::label (15.0f); }
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

        static void drawLed (juce::Graphics&, juce::Point<float> centre, float radius, bool lit,
                             juce::Colour litColour = colours::ledRed);
    };
}
