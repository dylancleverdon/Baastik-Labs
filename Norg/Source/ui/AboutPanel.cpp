#include "AboutPanel.h"
#include "NorgVersion.h"

namespace norg::ui
{
    AboutPanel::AboutPanel()
    {
        autoUpdateButton.onClick = [this]
        {
            bridge.setAutoUpdate (! status.autoUpdate);
            refresh();
        };

        checkNowButton.onClick = [this]
        {
            actionMessage = "Checking for updates...";
            refresh();
            bridge.run ({ "--check-now" }, [this] (bool ok, juce::String message)
            {
                actionMessage = ok ? (message.isNotEmpty() ? message : "Done.") : "Update check failed: " + message;
                refresh();
            });
        };

        rollbackButton.onClick = [this]
        {
            actionMessage = "Rolling back...";
            refresh();
            bridge.run ({ "--rollback" }, [this] (bool ok, juce::String message)
            {
                actionMessage = ok ? "Rolled back. Restart your DAW to use it." : "Rollback failed: " + message;
                refresh();
            });
        };

        closeButton.onClick = [this] { if (onClose) onClose(); };

        notes.setMultiLine (true);
        notes.setReadOnly (true);
        notes.setScrollbarsShown (true);
        notes.setCaretVisible (false);
        notes.setFont (Fonts::display (14.0f));

        for (auto* c : std::initializer_list<juce::Component*> { &autoUpdateButton, &checkNowButton,
                                                                 &rollbackButton, &closeButton, &notes })
            addAndMakeVisible (c);
    }

    void AboutPanel::visibilityChanged()
    {
        if (isVisible())
        {
            refresh();
            startTimer (2000);
        }
        else
        {
            stopTimer();
        }
    }

    void AboutPanel::refresh()
    {
        status = bridge.read();

        autoUpdateButton.setToggleState (status.autoUpdate, juce::dontSendNotification);
        autoUpdateButton.setEnabled (status.available);
        checkNowButton.setEnabled (status.available && ! bridge.isBusy());
        rollbackButton.setEnabled (status.available && status.previousAvailable && ! bridge.isBusy());

        juce::String text;
        if (status.installed && status.installed->notes.isNotEmpty())
            text << "What's new in " << status.installed->version << ":\n" << status.installed->notes;
        else
            text << "Release notes appear here once Norg is installed with the Norg installer.";
        if (notes.getText() != text)
            notes.setText (text, false);

        repaint();
    }

    juce::Rectangle<int> AboutPanel::card() const
    {
        return getLocalBounds().withSizeKeepingCentre (720, 470);
    }

    void AboutPanel::mouseDown (const juce::MouseEvent& e)
    {
        if (! card().contains (e.getPosition()) && onClose)
            onClose();
    }

    void AboutPanel::resized()
    {
        auto area = card().reduced (28);
        area.removeFromTop (150);

        auto controls = area.removeFromTop (54);
        autoUpdateButton.setBounds (controls.removeFromLeft (92));
        controls.removeFromLeft (16);
        checkNowButton.setBounds (controls.removeFromLeft (130).withSizeKeepingCentre (130, 32));
        controls.removeFromLeft (12);
        rollbackButton.setBounds (controls.removeFromLeft (130).withSizeKeepingCentre (130, 32));

        area.removeFromTop (34);
        auto bottom = area.removeFromBottom (44);
        closeButton.setBounds (bottom.removeFromRight (110).withSizeKeepingCentre (110, 32));
        notes.setBounds (area.withTrimmedBottom (8));
    }

    void AboutPanel::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colours::black.withAlpha (0.6f));

        const auto box = card().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (box.translated (0.0f, 4.0f), 10.0f);
        g.setColour (colours::cheek);
        g.fillRoundedRectangle (box, 10.0f);
        g.setColour (colours::panelRed);
        g.fillRoundedRectangle (box.withHeight (8.0f), 4.0f);

        auto area = box.reduced (28.0f);

        // Header: wordmark + version
        auto header = area.removeFromTop (60.0f);
        g.setFont (Fonts::logo (46.0f));
        g.setColour (colours::silkscreen);
        g.drawText ("norg", header.removeFromLeft (140.0f), juce::Justification::centredLeft, false);

        g.setFont (Fonts::labelBold (18.0f));
        g.drawText ("Version " + juce::String (version::string), header.removeFromTop (30.0f),
                    juce::Justification::bottomLeft, false);
        g.setFont (Fonts::display (13.0f));
        g.setColour (colours::oledDim);
        g.drawText ("Build " + juce::String (version::build) + "  |  commit " + juce::String (version::commit).substring (0, 7),
                    header, juce::Justification::topLeft, false);

        // Update status
        area.removeFromTop (8.0f);
        auto statusArea = area.removeFromTop (74.0f);
        drawSilkscreen (g, "Updates", statusArea.removeFromTop (18.0f), juce::Justification::centredLeft, 13.0f, true);

        juce::String line1, line2;
        juce::Colour line1Colour = colours::silkscreen;

        if (! status.available)
        {
            line1 = "Automatic updates work when Norg is installed with the Norg installer on macOS.";
            line1Colour = colours::silkscreenDim;
        }
        else if (status.installed && status.installed->build > version::build)
        {
            line1 = "Norg " + status.installed->version + " is installed. Restart your DAW to use it.";
            line1Colour = colours::ledGreen;
        }
        else if (status.installed)
        {
            line1 = "Installed: Norg " + status.installed->version
                  + (status.autoUpdate ? ". New versions install automatically in the background."
                                       : ". Auto-update is off.");
        }

        line2 = actionMessage.isNotEmpty() ? actionMessage : status.lastLogLine;

        g.setFont (Fonts::display (15.0f));
        g.setColour (line1Colour);
        g.drawText (line1, statusArea.removeFromTop (26.0f), juce::Justification::centredLeft, true);
        g.setColour (colours::oledDim);
        g.drawText (line2, statusArea.removeFromTop (24.0f), juce::Justification::centredLeft, true);

        // Credits along the bottom
        auto credits = box.reduced (28.0f).removeFromBottom (44.0f).withTrimmedRight (130.0f);
        g.setFont (Fonts::display (12.0f));
        g.setColour (colours::oledDim);
        g.drawFittedText ("Norg by Baastik Labs. Handmade in Sweden-ish. A fan project, not affiliated with Clavia.\n"
                          "Built with JUCE. Fonts: Archivo Black and Barlow (SIL OFL). Update signing: Monocypher.",
                          credits.toNearestInt(), juce::Justification::centredLeft, 3);
    }
}
