#include "ProgramPanel.h"
#include "PluginProcessor.h"

namespace norg::ui
{
    using perform::Location;

    ProgramPanel::ProgramPanel (NorgProcessor& p) : processor (p)
    {
        addAndMakeVisible (display);
        display.onClick = [this] { showMenu(); };
        display.setMouseCursor (juce::MouseCursor::PointingHandCursor);

        for (int i = 0; i < Location::slots; ++i)
        {
            auto* b = programButtons.add (new LedButton (juce::String (i + 1)));
            b->onClick = [this, i] { programButtonClicked (i); };
            addAndMakeVisible (b);
        }

        pageDown.onClick = [this] { stepPage (-1); };
        pageUp.onClick = [this] { stepPage (1); };
        bankButton.onClick = [this] { stepBank(); };
        liveButton.onClick = [this]
        {
            if (storing)
            {
                storeTarget = storeTarget.live ? Location::program (viewBank, viewPage, 0) : Location::liveSlot (0);
                refresh();
            }
            else
                processor.setLiveMode (! processor.isLiveMode());
        };
        storeButton.onClick = [this] { storeClicked(); };

        for (auto* b : { &pageDown, &pageUp, &bankButton, &liveButton, &storeButton })
            addAndMakeVisible (b);

        const auto where = processor.currentLocation();
        viewBank = where.bank;
        viewPage = where.page;

        processor.addChangeListener (this);
        startTimerHz (4);
        refresh();
    }

    ProgramPanel::~ProgramPanel()
    {
        processor.removeChangeListener (this);
    }

    Location ProgramPanel::target (int slot) const
    {
        const bool live = storing ? storeTarget.live : processor.isLiveMode();
        return live ? Location::liveSlot (slot) : Location::program (viewBank, viewPage, slot);
    }

    void ProgramPanel::programButtonClicked (int slot)
    {
        if (storing)
        {
            storeTarget = target (slot);
            refresh();
            return;
        }

        browsing = false;
        processor.loadProgram (target (slot));
    }

    void ProgramPanel::stepPage (int delta)
    {
        if (! storing && processor.isLiveMode())
            return; // Live has a single page
        viewPage = (viewPage + delta + Location::pages) % Location::pages;
        browsing = ! storing;
        if (storing)
            storeTarget = Location::program (viewBank, viewPage, storeTarget.live ? 0 : storeTarget.slot);
        refresh();
    }

    void ProgramPanel::stepBank()
    {
        if (! storing && processor.isLiveMode())
            return;
        viewBank = (viewBank + 1) % Location::banks;
        viewPage = 0;
        browsing = ! storing;
        if (storing)
            storeTarget = Location::program (viewBank, viewPage, storeTarget.live ? 0 : storeTarget.slot);
        refresh();
    }

    void ProgramPanel::storeClicked()
    {
        if (! storing)
        {
            storing = true;
            browsing = false;
            storeTarget = processor.currentLocation();
            viewBank = storeTarget.bank;
            viewPage = storeTarget.page;
            refresh();
            return;
        }

        const auto destination = storeTarget;
        askForName ("Store to " + destination.label(), processor.currentProgramName() == "Init" ? juce::String() : processor.currentProgramName(),
                    [this, destination] (const juce::String& name)
        {
            processor.storeProgram (destination, name);
        });
        storing = false;
        refresh();
    }

    void ProgramPanel::askForName (const juce::String& title, const juce::String& initial,
                                   std::function<void (const juce::String&)> done)
    {
        nameWindow = std::make_unique<juce::AlertWindow> (title, "Program name:", juce::MessageBoxIconType::NoIcon, this);
        nameWindow->addTextEditor ("name", initial);
        nameWindow->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        nameWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        nameWindow->enterModalState (true, juce::ModalCallbackFunction::create ([this, done] (int result)
        {
            if (result == 1 && nameWindow != nullptr)
                done (nameWindow->getTextEditorContents ("name").trim());
            nameWindow.reset();
        }), false);
    }

    void ProgramPanel::showMenu()
    {
        if (storing)
        {
            storing = false; // clicking the display cancels a store
            refresh();
            return;
        }

        const bool electro = processor.state().getParameter (paramId (P::mode))->getValue() >= 0.5f;
        const std::pair<perform::Part, const char*> parts[] = {
            { perform::Part::organ, "Organ" }, { perform::Part::piano, "Piano" },
            { electro ? perform::Part::sample : perform::Part::synth, electro ? "Sample" : "Synth" },
            { perform::Part::effects, "Effects" } };

        juce::PopupMenu menu;
        auto& library = processor.programLibrary();

        juce::PopupMenu all;
        for (int bank = 0; bank < Location::banks; ++bank)
        {
            juce::PopupMenu bankMenu;
            for (int page = 0; page < Location::pages; ++page)
            {
                juce::PopupMenu pageMenu;
                for (int slot = 0; slot < Location::slots; ++slot)
                {
                    const auto where = Location::program (bank, page, slot);
                    pageMenu.addItem (where.label() + "  " + library.get (where).displayName(), true,
                                      where == processor.currentLocation(), [this, where] { browsing = false; processor.loadProgram (where); });
                }
                bankMenu.addSubMenu ("Page " + juce::String (page + 1), pageMenu);
            }
            all.addSubMenu ("Bank " + juce::String::charToString (static_cast<juce::juce_wchar> ('A' + bank)), bankMenu);
        }
        juce::PopupMenu live;
        for (int slot = 0; slot < Location::liveSlots; ++slot)
        {
            const auto where = Location::liveSlot (slot);
            live.addItem (where.label() + "  " + library.get (where).displayName(), true,
                          where == processor.currentLocation(), [this, where] { processor.loadProgram (where); });
        }
        all.addSubMenu ("Live", live);
        menu.addSubMenu ("Programs", all);
        menu.addSeparator();

        menu.addItem ("Undo", processor.canUndo(), false, [this] { processor.undo(); });
        menu.addItem ("Redo", processor.canRedo(), false, [this] { processor.redo(); });
        menu.addItem ("Compare with stored", processor.isModified() || processor.isComparing(), processor.isComparing(),
                      [this] { processor.toggleCompare(); });
        menu.addSeparator();

        juce::PopupMenu copy, paste;
        for (const auto& [part, name] : parts)
        {
            copy.addItem (juce::String ("Copy ") + name, [this, p = part] { processor.copyPart (p); });
            paste.addItem (juce::String ("Paste ") + name, processor.canPaste (part), false, [this, p = part] { processor.pastePart (p); });
        }
        menu.addSubMenu ("Copy section", copy);
        menu.addSubMenu ("Paste section", paste);
        menu.addSeparator();

        menu.addItem ("Rename...", [this]
        {
            const auto where = processor.currentLocation();
            askForName ("Rename " + where.label(), processor.currentProgramName(), [this, where] (const juce::String& name)
            {
                processor.programLibrary().rename (where, name);
                processor.loadProgram (where);
            });
        });
        menu.addItem ("Start from Init sound", [this] { processor.initSound(); });

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&display));
    }

    void ProgramPanel::timerCallback()
    {
        blink = ! blink;
        if (storing)
            storeButton.setLedOverride (blink);
        liveButton.setLedOverride (storing ? storeTarget.live : processor.isLiveMode());
    }

    void ProgramPanel::refresh()
    {
        const auto where = processor.currentLocation();
        if (! browsing && ! storing)
        {
            viewBank = where.bank;
            viewPage = where.page;
        }

        const bool live = storing ? storeTarget.live : where.live;
        const auto highlightFor = [&] (const Location& l) -> int
        {
            if (l.live != live)
                return -1;
            return l.live || (l.bank == viewBank && l.page == viewPage) ? l.slot : -1;
        };

        const int lit = storing ? highlightFor (storeTarget) : highlightFor (where);
        for (int i = 0; i < programButtons.size(); ++i)
            programButtons[i]->setLedOverride (i == lit);

        bankButton.setButtonText ("Bank " + juce::String::charToString (static_cast<juce::juce_wchar> ('A' + viewBank)));
        storeButton.setLedOverride (storing ? std::optional<bool> (blink) : std::optional<bool> (false));
        liveButton.setLedOverride (live);
        for (auto* b : { &pageDown, &pageUp, &bankButton })
            b->setEnabled (storing || ! where.live);

        const bool electro = processor.state().getParameter (paramId (P::mode))->getValue() >= 0.5f;
        const juce::String mode = electro ? "ELECTRO" : "STAGE";

        if (storing)
        {
            display.setText ("Store to " + storeTarget.label() + "?",
                             "STORE  |  choose a program, press Store again",
                             "Now there: " + processor.programLibrary().get (storeTarget).displayName());
        }
        else if (browsing)
        {
            juce::StringArray names;
            for (int slot = 0; slot < Location::slots; ++slot)
                names.add (processor.programLibrary().get (Location::program (viewBank, viewPage, slot)).displayName());
            display.setList (mode + "  |  " + Location::program (viewBank, viewPage, 0).label().upToLastOccurrenceOf (":", false, false)
                               + "  press 1-5", names, highlightFor (where));
        }
        else
        {
            const auto status = juce::String (processor.isComparing() ? "  COMPARE" : (processor.isModified() ? "  EDITED" : ""));
            display.setText (processor.currentProgramName(), mode + "  |  PROGRAM " + where.label() + status);
        }
    }

    void ProgramPanel::resized()
    {
        auto area = getLocalBounds();
        display.setBounds (area.removeFromTop (78));
        area.removeFromTop (6);

        auto row = area.removeFromTop (44);
        const int w = row.getWidth() / Location::slots;
        for (auto* b : programButtons)
            b->setBounds (row.removeFromLeft (w).withSizeKeepingCentre (juce::jmin (w, 46), 44));

        area.removeFromTop (4);
        row = area.removeFromTop (44);
        for (auto* b : { &pageDown, &pageUp, &bankButton, &liveButton, &storeButton })
            b->setBounds (row.removeFromLeft (w).withSizeKeepingCentre (juce::jmin (w, 46), 44));
    }
}
