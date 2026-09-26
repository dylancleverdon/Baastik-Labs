#pragma once

#include "AboutPanel.h"
#include "FxPanels.h"
#include "NorgKeyboard.h"
#include "ProgramPanel.h"
#include "SectionPanels.h"
#include "Widgets.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace norg
{
    class NorgProcessor;
}

namespace norg::ui
{
    // The whole instrument face, laid out at a fixed logical size and scaled by the editor.
    class MainPanel final : public juce::Component
    {
    public:
        static constexpr int logicalWidth = 1400;
        static constexpr int logicalHeight = 780;

        explicit MainPanel (NorgProcessor&);
        ~MainPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void modeChanged (int mode);
        juce::Rectangle<int> panelArea() const;
        juce::Rectangle<int> keyboardArea() const;

        NorgProcessor& processor;

        NorgLogo logo;
        ProgramPanel programs;
        ChoiceButtons modeButtons;
        Knob masterVolume;
        LedButton systemButton { "System" };
        TempoPanel tempo;

        StagePanel stagePanel;
        ElectroPanel electroPanel;

        NorgKeyboard keyboard;
        AboutPanel about;

        std::unique_ptr<juce::ParameterAttachment> modeAttachment;
        int currentMode = 0;
    };
}
