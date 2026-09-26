#pragma once

#include "NorgLookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace norg::ui
{
    // An LED-ring knob with a silkscreen label underneath, attached to a parameter.
    class Knob final : public juce::Component
    {
    public:
        Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramId, juce::String label,
              bool bipolar = false);

        void paint (juce::Graphics&) override;
        void resized() override;

        juce::Slider slider;

    private:
        juce::String labelText;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    // A panel button: rubber key cap with an LED above it and a label below.
    class LedButton final : public juce::Button
    {
    public:
        explicit LedButton (juce::String label, juce::Colour ledColour = colours::ledRed);

        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

        void setLedOverride (std::optional<bool> lit) { ledOverride = lit; repaint(); }

    private:
        juce::Colour led;
        std::optional<bool> ledOverride;
    };

    // An LED button bound to a bool parameter.
    class ParamLedButton final : public juce::Component
    {
    public:
        ParamLedButton (juce::AudioProcessorValueTreeState&, const juce::String& paramId, juce::String label);
        void resized() override { button.setBounds (getLocalBounds()); }

        LedButton button;

    private:
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    // A row of LED buttons that pick one value of a choice parameter (like a Nord model selector).
    class ChoiceButtons final : public juce::Component
    {
    public:
        ChoiceButtons (juce::AudioProcessorValueTreeState&, const juce::String& paramId, juce::StringArray labels);

        void setColumns (int columns) { numColumns = columns; resized(); }
        void resized() override;

    private:
        void update (float value);

        juce::OwnedArray<LedButton> buttons;
        std::unique_ptr<juce::ParameterAttachment> attachment;
        int numColumns = 0; // 0 = all in one row
    };

    // A button that steps through a choice parameter's values, showing the current one (e.g. V1-C3), showing it as the label.
    class StepButton final : public juce::Component
    {
    public:
        StepButton (juce::AudioProcessorValueTreeState&, const juce::String& paramId, juce::String caption);
        void resized() override { button.setBounds (getLocalBounds()); }

        // Show different names for the choices (e.g. "Sample" for the third source in Electro).
        void setDisplayNames (juce::StringArray names);

    private:
        LedButton button;
        juce::StringArray choices;
        juce::String captionText;
        int current = 0;
        std::unique_ptr<juce::ParameterAttachment> attachment;
    };

    // The small black OLED display.
    class Oled final : public juce::Component
    {
    public:
        void setText (juce::String title, juce::String subtitle, juce::String footer = {});

        // A heading and up to five numbered lines (e.g. the programs on a page), one highlighted.
        void setList (juce::String heading, juce::StringArray lines, int highlighted);

        void paint (juce::Graphics&) override;

        std::function<void()> onClick;
        void mouseUp (const juce::MouseEvent&) override { if (onClick) onClick(); }

    private:
        juce::String titleText, subtitleText, footerText;
        juce::StringArray listLines;
        int listHighlight = -1;
        bool listMode = false;
    };

    // A section outline with its title in the top-left corner.
    class SectionFrame : public juce::Component
    {
    public:
        explicit SectionFrame (juce::String title);
        void paint (juce::Graphics&) override;

        // The area inside the frame, below the title.
        juce::Rectangle<int> content() const;

        void setNote (juce::String text) { noteText = std::move (text); repaint(); }

    private:
        juce::String titleText, noteText;
    };

    // The "norg" wordmark with the model badge ("stage" / "electro").
    class NorgLogo final : public juce::Component
    {
    public:
        void setModel (juce::String model) { modelName = std::move (model); repaint(); }
        void paint (juce::Graphics&) override;

    private:
        juce::String modelName { "stage" };
    };
}
