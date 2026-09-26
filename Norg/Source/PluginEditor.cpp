#include "PluginEditor.h"

namespace norg
{
    NorgEditor::NorgEditor (NorgProcessor& p)
        : AudioProcessorEditor (p), panel (p)
    {
        setLookAndFeel (&lookAndFeel);
        addAndMakeVisible (panel);

        constexpr double aspect = static_cast<double> (ui::MainPanel::logicalWidth) / ui::MainPanel::logicalHeight;
        setResizable (true, true);
        setResizeLimits (ui::MainPanel::logicalWidth / 2, ui::MainPanel::logicalHeight / 2,
                         ui::MainPanel::logicalWidth * 3 / 2, ui::MainPanel::logicalHeight * 3 / 2);
        getConstrainer()->setFixedAspectRatio (aspect);
        setSize (ui::MainPanel::logicalWidth * 4 / 5, ui::MainPanel::logicalHeight * 4 / 5);
    }

    NorgEditor::~NorgEditor()
    {
        setLookAndFeel (nullptr);
    }

    void NorgEditor::paint (juce::Graphics& g)
    {
        g.fillAll (ui::colours::keybed);
    }

    void NorgEditor::resized()
    {
        const float scale = static_cast<float> (getWidth()) / ui::MainPanel::logicalWidth;
        panel.setBounds (0, 0, ui::MainPanel::logicalWidth, ui::MainPanel::logicalHeight);
        panel.setTransform (juce::AffineTransform::scale (scale));
    }
}
