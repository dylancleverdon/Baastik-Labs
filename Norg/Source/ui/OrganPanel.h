#pragma once

#include "SectionPanels.h"
#include "Widgets.h"

namespace norg::ui
{
    enum class DrawbarStyle { sliding, leds };

    // One drawbar bound to an Int (0-8) parameter that can be rebound on the fly (upper preset
    // I/II, lower manual, pedals). Draws either as a sliding drawbar or an LED bargraph.
    class Drawbar final : public juce::Component
    {
    public:
        Drawbar (juce::AudioProcessorValueTreeState&, DrawbarStyle, int index);

        void bind (const juce::String& paramId); // empty = disabled
        void setColourIndex (int footageColour) { colourIndex = footageColour; repaint(); }

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        void setValue (int v, bool asGesture);
        int valueFromLedY (float y) const;
        juce::Rectangle<float> travelArea() const;

        juce::AudioProcessorValueTreeState& state;
        DrawbarStyle style;
        int index;
        int colourIndex = 1; // 0 brown, 1 white, 2 black
        int value = 0;
        int dragStartValue = 0;
        std::unique_ptr<juce::ParameterAttachment> attachment;
    };

    // The organ section: model, drawbar presets, drawbars, percussion, vibrato and organ split.
    class OrganPanel final : public SectionFrame
    {
    public:
        OrganPanel (juce::AudioProcessorValueTreeState&, DrawbarStyle);
        ~OrganPanel() override;

        void resized() override;
        void paint (juce::Graphics&) override;

    private:
        enum class Target { upper, lower, pedal };
        void setTarget (Target);
        void rebind();

        juce::AudioProcessorValueTreeState& state;
        DrawbarStyle style;

        ParamLedButton onButton;
        Knob volume, click;
        ChoiceButtons models, presets;
        juce::OwnedArray<Drawbar> drawbars;
        ParamLedButton percOn, percThird, percFast, percSoft, vibOn;
        StepButton vibMode;
        ParamLedButton splitButton, pedalsButton;
        LedButton upperButton { "Upper" }, lowerButton { "Lower" }, pedalButton { "Pedal" };

        Target target = Target::upper;
        int model = 0, preset = 0;
        bool split = false;
        juce::StringArray footageLabels;
        std::unique_ptr<juce::ParameterAttachment> modelWatch, presetWatch, splitWatch;
    };
}
