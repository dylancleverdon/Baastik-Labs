#pragma once

#include "PluginProcessor.h"
#include "ui/MainPanel.h"
#include "ui/NorgLookAndFeel.h"

namespace norg
{
    // Hosts MainPanel at its fixed logical size and scales it to whatever size the window is.
    class NorgEditor final : public juce::AudioProcessorEditor
    {
    public:
        explicit NorgEditor (NorgProcessor&);
        ~NorgEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        ui::NorgLookAndFeel lookAndFeel;
        ui::MainPanel panel;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NorgEditor)
    };
}
