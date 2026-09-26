#include "PianoPanel.h"
#include "PluginProcessor.h"
#include "engine/piano/PianoSection.h"
#include "SamplePacks.h"

namespace norg::ui
{
    namespace
    {
        using Use = NorgProcessor::LibraryUse;
    }

    PianoPanel::PianoPanel (NorgProcessor& p)
        : SectionFrame ("Piano"),
          processor (p),
          onButton (p.state(), paramId (P::pianoOn), "On"),
          volume (p.state(), paramId (P::pianoVolume), "Volume"),
          types (p.state(), paramId (P::pianoType), { "Grand", "Upright", "Tine", "Reed", "Clav" }),
          tineModel (p.state(), paramId (P::pianoTineModel), "Model"),
          reedModel (p.state(), paramId (P::pianoReedModel), "Model"),
          timbre (p.state(), paramId (P::pianoTimbre), "Timbre"),
          clavPickup (p.state(), paramId (P::pianoClavPickup), "Pickup"),
          clavFilter (p.state(), paramId (P::pianoClavFilter), "Filter"),
          samples (p.state(), paramId (P::pianoSampled), "Samples"),
          stringRes (p.state(), paramId (P::pianoStringRes), "Str Res"),
          pedalNoise (p.state(), paramId (P::pianoPedalNoise), "Pdl Noise"),
          softRelease (p.state(), paramId (P::pianoSoftRelease), "Soft Rel"),
          stretch (p.state(), paramId (P::pianoStretch), "Stretch")
    {
        types.setColumns (3);

        for (auto* c : std::initializer_list<juce::Component*> { &onButton, &volume, &types, &tineModel, &reedModel,
                                                                 &timbre, &clavPickup, &clavFilter, &libraryButton,
                                                                 &samples, &stringRes, &pedalNoise, &softRelease, &stretch })
            addAndMakeVisible (c);

        libraryButton.onClick = [this] { showLibraryMenu(); };

        typeWatch = std::make_unique<juce::ParameterAttachment> (
            *p.state().getParameter (paramId (P::pianoType)), [this] (float v) { typeChanged (juce::roundToInt (v)); });
        typeWatch->sendInitialUpdate();

        startTimerHz (3);
    }

    PianoPanel::~PianoPanel() = default;

    void PianoPanel::typeChanged (int newType)
    {
        type = newType;
        const bool acoustic = type <= 1;
        tineModel.setVisible (type == 2);
        reedModel.setVisible (type == 3);
        libraryButton.setVisible (acoustic);
        clavPickup.setVisible (type == 4);
        clavFilter.setVisible (type == 4);
        timbre.setVisible (type == 2 || type == 3);
        samples.setVisible (acoustic);
        stringRes.setEnabled (acoustic);
        timerCallback();
    }

    void PianoPanel::timerCallback()
    {
        // What's actually playing, shown along the bottom of the section.
        juce::String note;
        if (type <= 1)
        {
            const auto slot = type == 0 ? piano::PianoSection::grandSlot (0) : piano::PianoSection::uprightSlot (0);
            const auto status = processor.libraryManager().status (slot);
            const bool wantSamples = processor.state().getParameter (paramId (P::pianoSampled))->getValue() >= 0.5f;
            const auto* kind = type == 0 ? "grand" : "upright";

            if (! wantSamples)
                note = juce::String ("Modelled ") + kind;
            else if (status.state == sfz::LibraryManager::State::empty)
            {
                // Is the background helper still bringing a library down?
                juce::String progress;
                const auto packs = update::parsePackStatus (update::Layout::forCurrentUser().packsStatusJson().loadFileAsString());
                for (const auto& p : packs)
                    if (p.state == "downloading" || p.state == "preparing")
                        progress = (p.state == "downloading" ? "Downloading " : "Preparing ") + p.name + " "
                                 + juce::String (juce::roundToInt (p.progress * 100.0f)) + "%";

                note = progress.isNotEmpty() ? progress + "  (modelled until ready)" : juce::String ("Modelled ") + kind;
            }
            else if (status.state == sfz::LibraryManager::State::loading)
                note = "Loading samples...";
            else if (status.state == sfz::LibraryManager::State::failed)
                note = "Library failed: " + status.error;
            else
                note = status.name;
        }
        else
        {
            static const char* names[] = { "", "", "Tine electric piano", "Reed electric piano", "Clav" };
            note = names[juce::jlimit (0, 4, type)];
        }
        setNote (note);
    }

    void PianoPanel::showLibraryMenu()
    {
        const auto use = type == 0 ? Use::grand : Use::upright;
        const auto current = processor.getLibraryChoice (0, use);
        const auto installed = sfz::LibraryManager::findInstalledLibraries();

        juce::PopupMenu menu;
        menu.addSectionHeader (type == 0 ? "Grand piano library" : "Upright piano library");
        menu.addItem (1, "Automatic (first installed)", true, current.isEmpty());
        menu.addItem (2, "Modelled (no samples)", true, current == "none");
        menu.addSeparator();
        for (int i = 0; i < installed.size(); ++i)
            menu.addItem (100 + i, installed[i].getFileNameWithoutExtension(), true,
                          current == installed[i].getFullPathName());
        if (installed.isEmpty())
            menu.addItem (3, "No libraries installed yet", false, false);
        menu.addSeparator();
        menu.addItem (4, "Load SFZ file...");
        menu.addItem (5, "Show samples folder");

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&libraryButton),
                            [this, use, installed] (int result)
        {
            if (result == 1)       processor.setLibraryChoice (0, use, {});
            else if (result == 2)  processor.setLibraryChoice (0, use, "none");
            else if (result >= 100 && result - 100 < installed.size())
                processor.setLibraryChoice (0, use, installed[result - 100].getFullPathName());
            else if (result == 5)
            {
                auto folder = sfz::LibraryManager::librariesFolder();
                folder.createDirectory();
                folder.revealToUser();
            }
            else if (result == 4)
            {
                chooser = std::make_unique<juce::FileChooser> ("Choose an SFZ instrument", sfz::LibraryManager::librariesFolder(), "*.sfz");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                      [this, use] (const juce::FileChooser& fc)
                {
                    if (fc.getResult().existsAsFile())
                        processor.setLibraryChoice (0, use, fc.getResult().getFullPathName());
                });
            }
            timerCallback();
        });
    }

    void PianoPanel::resized()
    {
        auto area = content();
        auto left = area.removeFromLeft (76);
        onButton.setBounds (left.removeFromTop (50).withSizeKeepingCentre (56, 50));
        left.removeFromTop (6);
        volume.setBounds (left.removeFromTop (92));
        area.removeFromLeft (8);
        area.removeFromBottom (18); // status line

        const int rowHeight = juce::jmin (50, area.getHeight() / 5);
        const int cell = area.getWidth() / 3;
        const auto place = [cell, rowHeight] (juce::Component& c, juce::Rectangle<int> row, int column)
        {
            c.setBounds (row.withX (row.getX() + column * cell).withWidth (cell).withSizeKeepingCentre (juce::jmin (cell, 62), rowHeight));
        };

        types.setBounds (area.removeFromTop (rowHeight * 2));

        auto row3 = area.removeFromTop (rowHeight);
        place (tineModel, row3, 0);
        place (reedModel, row3, 0);
        place (libraryButton, row3, 0);
        place (timbre, row3, 1);
        place (samples, row3, 1);
        place (clavPickup, row3, 1);
        place (clavFilter, row3, 2);

        auto row4 = area.removeFromTop (rowHeight);
        place (stringRes, row4, 0);
        place (pedalNoise, row4, 1);
        place (softRelease, row4, 2);

        auto row5 = area.removeFromTop (rowHeight);
        place (stretch, row5, 0);
    }
}
