#include "NorgLookAndFeel.h"

namespace norg::ui
{
    NorgLookAndFeel::NorgLookAndFeel()
    {
        setColour (juce::PopupMenu::backgroundColourId, colours::cheek);
        setColour (juce::PopupMenu::textColourId, colours::silkscreen);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::panelRed);
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::TextButton::buttonColourId, colours::keyCap);
        setColour (juce::TextButton::buttonOnColourId, colours::panelRedDeep);
        setColour (juce::TextButton::textColourOffId, colours::silkscreen);
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::ToggleButton::textColourId, colours::silkscreen);
        setColour (juce::ToggleButton::tickColourId, colours::ledRed);
        setColour (juce::TooltipWindow::backgroundColourId, colours::cheek);
        setColour (juce::TooltipWindow::textColourId, colours::silkscreen);
        setColour (juce::ScrollBar::thumbColourId, colours::silkscreenDim);
        setColour (juce::TextEditor::backgroundColourId, colours::oledBack);
        setColour (juce::TextEditor::textColourId, colours::oledText);
        setColour (juce::TextEditor::outlineColourId, colours::cheekEdge);
    }

    juce::Font NorgLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
    {
        return Fonts::labelBold (juce::jmin (16.0f, static_cast<float> (buttonHeight) * 0.55f));
    }

    void NorgLookAndFeel::drawLed (juce::Graphics& g, juce::Point<float> centre, float radius, bool lit,
                                   juce::Colour litColour)
    {
        const auto area = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);

        if (lit)
        {
            g.setColour (litColour.withAlpha (0.28f));
            g.fillEllipse (area.expanded (radius * 1.1f));
            g.setColour (litColour);
            g.fillEllipse (area);
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.fillEllipse (area.reduced (radius * 0.55f).translated (-radius * 0.15f, -radius * 0.15f));
        }
        else
        {
            g.setColour (colours::ledOff);
            g.fillEllipse (area);
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.drawEllipse (area, 0.6f);
        }
    }

    void NorgLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                            float startAngle, float endAngle, juce::Slider& slider)
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();

        // LED ring
        constexpr int numLeds = 13;
        const float ringRadius = radius * 0.9f;
        const float ledRadius = juce::jmax (1.4f, radius * 0.065f);
        const bool bipolar = slider.getProperties().getWithDefault ("bipolar", false);
        const float litUpTo = sliderPos * (numLeds - 1);

        for (int i = 0; i < numLeds; ++i)
        {
            const float angle = startAngle + (endAngle - startAngle) * static_cast<float> (i) / (numLeds - 1);
            const auto pos = centre.getPointOnCircumference (ringRadius, angle);

            bool lit;
            if (bipolar)
            {
                const float mid = (numLeds - 1) * 0.5f;
                lit = (static_cast<float> (i) >= std::min (mid, litUpTo) - 0.5f
                       && static_cast<float> (i) <= std::max (mid, litUpTo) + 0.5f);
            }
            else
            {
                lit = sliderPos > 0.001f && static_cast<float> (i) <= litUpTo + 0.5f;
            }

            drawLed (g, pos, ledRadius, lit && slider.isEnabled());
        }

        // Knob body
        const float knobRadius = radius * 0.66f;
        const auto knob = juce::Rectangle<float> (knobRadius * 2.0f, knobRadius * 2.0f).withCentre (centre);

        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillEllipse (knob.translated (0.0f, knobRadius * 0.12f).expanded (knobRadius * 0.06f));

        juce::ColourGradient body (juce::Colour (0xff3a3a3c), knob.getX(), knob.getY(),
                                   colours::knobBody, knob.getRight(), knob.getBottom(), false);
        g.setGradientFill (body);
        g.fillEllipse (knob);

        // Knurled rim
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawEllipse (knob, 1.2f);
        const auto cap = knob.reduced (knobRadius * 0.16f);
        juce::ColourGradient capGradient (juce::Colour (0xff2c2c2e), cap.getX(), cap.getY(),
                                          juce::Colour (0xff0c0c0d), cap.getRight(), cap.getBottom(), false);
        g.setGradientFill (capGradient);
        g.fillEllipse (cap);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawEllipse (cap, 1.0f);

        // Pointer
        const float angle = startAngle + sliderPos * (endAngle - startAngle);
        juce::Path pointer;
        const float pointerWidth = juce::jmax (1.6f, knobRadius * 0.12f);
        pointer.addRoundedRectangle (-pointerWidth * 0.5f, -knobRadius * 0.92f, pointerWidth, knobRadius * 0.55f,
                                     pointerWidth * 0.5f);
        g.setColour (slider.isEnabled() ? colours::silkscreen : colours::silkscreenDim);
        g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
    }
}
