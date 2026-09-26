#include "MainPanel.h"
#include "NorgVersion.h"
#include "PluginProcessor.h"

namespace norg::ui
{
    namespace
    {
        constexpr int cheekWidth = 24;
        constexpr int panelHeight = 462;
        constexpr int leftColumnWidth = 240;
        constexpr int lowestKey = 28;  // E0..E6: a 73-key board
        constexpr int highestKey = 100;
    }

    MainPanel::MainPanel (NorgProcessor& p)
        : processor (p),
          modeButtons (p.state(), paramId (P::mode), { "Stage", "Electro" }),
          masterVolume (p.state(), paramId (P::masterVolume), "Master Level"),
          stagePanel (p.state()),
          electroPanel (p.state()),
          keyboard (p.keyboardState())
    {
        addAndMakeVisible (logo);
        addAndMakeVisible (display);
        addAndMakeVisible (modeButtons);
        addAndMakeVisible (masterVolume);
        addAndMakeVisible (systemButton);
        addChildComponent (stagePanel);
        addChildComponent (electroPanel);

        keyboard.setAvailableRange (lowestKey, highestKey);
        keyboard.setWantsKeyboardFocus (true);
        addAndMakeVisible (keyboard);

        systemButton.onClick = [this]
        {
            about.setVisible (true);
            about.toFront (true);
        };
        about.onClose = [this] { about.setVisible (false); };
        addChildComponent (about);

        auto* modeParam = p.state().getParameter (paramId (P::mode));
        modeAttachment = std::make_unique<juce::ParameterAttachment> (
            *modeParam, [this] (float v) { modeChanged (juce::roundToInt (v)); });
        modeAttachment->sendInitialUpdate();
    }

    MainPanel::~MainPanel() = default;

    void MainPanel::modeChanged (int mode)
    {
        currentMode = mode;
        const bool stage = mode == 0;
        stagePanel.setVisible (stage);
        electroPanel.setVisible (! stage);
        logo.setModel (stage ? "stage" : "electro");
        display.setText ("Norg Init", stage ? "STAGE  |  PROGRAM A:11" : "ELECTRO  |  PROGRAM 1:1",
                         "v" + juce::String (version::string));
        repaint();
    }

    juce::Rectangle<int> MainPanel::panelArea() const
    {
        return getLocalBounds().removeFromTop (panelHeight).reduced (cheekWidth, 0);
    }

    juce::Rectangle<int> MainPanel::keyboardArea() const
    {
        return getLocalBounds().withTrimmedTop (panelHeight).reduced (cheekWidth, 0);
    }

    void MainPanel::resized()
    {
        auto area = panelArea().reduced (14, 12);

        auto left = area.removeFromLeft (leftColumnWidth);
        area.removeFromLeft (14);

        logo.setBounds (left.removeFromTop (58));
        left.removeFromTop (10);
        display.setBounds (left.removeFromTop (78));
        left.removeFromTop (16);
        modeButtons.setBounds (left.removeFromTop (48).withSizeKeepingCentre (150, 48));
        left.removeFromTop (14);
        masterVolume.setBounds (left.removeFromTop (100).withSizeKeepingCentre (96, 100));
        systemButton.setBounds (left.removeFromBottom (48).withSizeKeepingCentre (64, 48));

        stagePanel.setBounds (area);
        electroPanel.setBounds (area);

        auto keys = keyboardArea().reduced (0, 10);
        keys.removeFromLeft (96); // room for the pitch stick and mod wheel
        keyboard.setBounds (keys);
        const int whiteKeys = 43;
        keyboard.setKeyWidth (static_cast<float> (keys.getWidth()) / whiteKeys);

        about.setBounds (getLocalBounds());
    }

    void MainPanel::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat();

        // Keybed and body
        g.setColour (colours::keybed);
        g.fillRect (bounds);

        // Red panel with a gentle top-lit gradient
        const auto panel = bounds.withHeight (static_cast<float> (panelHeight));
        juce::ColourGradient red (colours::panelRedLight, 0.0f, 0.0f, colours::panelRedDeep, 0.0f, panel.getHeight(), false);
        red.addColour (0.55, colours::panelRed);
        g.setGradientFill (red);
        g.fillRect (panel);

        // Fine horizontal brushed texture
        g.setColour (juce::Colours::white.withAlpha (0.018f));
        for (float y = 1.0f; y < panel.getHeight(); y += 3.0f)
            g.drawHorizontalLine (static_cast<int> (y), 0.0f, panel.getWidth());

        // Front lip between panel and keys
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRect (panel.withTop (panel.getBottom() - 4.0f));

        // Dark end cheeks
        for (auto cheek : { bounds.withWidth (static_cast<float> (cheekWidth)),
                            bounds.withLeft (bounds.getRight() - static_cast<float> (cheekWidth)) })
        {
            juce::ColourGradient grad (colours::cheekEdge, cheek.getX(), 0.0f, colours::cheek, cheek.getCentreX(), 0.0f, true);
            g.setGradientFill (grad);
            g.fillRect (cheek);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.drawVerticalLine (static_cast<int> (cheek.getX() < 1.0f ? cheek.getRight() - 1.0f : cheek.getX()),
                                0.0f, bounds.getBottom());
        }

        // Pitch stick / mod wheel well (the controls arrive with the performance update)
        auto well = keyboardArea().reduced (0, 10).removeFromLeft (84).toFloat().reduced (6.0f, 4.0f);
        g.setColour (juce::Colour (0xff1d1d1f));
        g.fillRoundedRectangle (well, 5.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (well, 5.0f, 1.0f);
        g.setFont (Fonts::logo (18.0f));
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.drawText ("norg", well.removeFromBottom (30.0f), juce::Justification::centred, false);

        // Maker's line under the left column
        drawSilkscreen (g, "Baastik Labs", panelArea().reduced (14, 12).removeFromLeft (leftColumnWidth)
                                               .removeFromBottom (18).toFloat().withTrimmedLeft (80.0f),
                        juce::Justification::centredRight, 10.0f, false, colours::silkscreenDim);
    }
}
