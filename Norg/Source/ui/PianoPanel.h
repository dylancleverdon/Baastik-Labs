#pragma once

#include "OrganPanel.h"
#include "Widgets.h"

namespace norg
{
    class NorgProcessor;
}

namespace norg::ui
{
    // The piano section: type, model, timbre, clav pickups/filters, realism switches and the
    // sample library used by the Grand and Upright.
    class PianoPanel final : public SectionFrame, private juce::Timer
    {
    public:
        explicit PianoPanel (NorgProcessor&);
        ~PianoPanel() override;

        void resized() override;

    private:
        void timerCallback() override;
        void typeChanged (int type);
        void showLibraryMenu();

        NorgProcessor& processor;
        ParamLedButton onButton;
        Knob volume;
        ChoiceButtons types;
        StepButton tineModel, reedModel, timbre, clavPickup, clavFilter;
        LedButton libraryButton { "Library" };
        ParamLedButton samples, stringRes, pedalNoise, softRelease, stretch;

        int type = 2;
        std::unique_ptr<juce::ParameterAttachment> typeWatch;
        std::unique_ptr<juce::FileChooser> chooser;
    };
}
