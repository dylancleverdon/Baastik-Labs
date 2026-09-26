#pragma once

#include "Widgets.h"
#include "perform/ProgramLibrary.h"

namespace norg
{
    class NorgProcessor;
}

namespace norg::ui
{
    // The program section: the OLED, five program buttons, Page and Bank, Live and Store.
    //
    // Pick a program with its button; Page and Bank browse (the OLED lists that page) until a
    // program button is pressed. Store blinks while you choose where to store (with the same
    // buttons), and a second press names and stores it. Click the OLED for everything else:
    // browse all programs, undo/redo, compare, copy/paste a section, rename.
    class ProgramPanel final : public juce::Component, private juce::ChangeListener, private juce::Timer
    {
    public:
        explicit ProgramPanel (NorgProcessor&);
        ~ProgramPanel() override;

        void resized() override;

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
        void timerCallback() override;

        void refresh();
        void programButtonClicked (int slot);
        void stepPage (int delta);
        void stepBank();
        void storeClicked();
        void showMenu();
        void askForName (const juce::String& title, const juce::String& initial,
                         std::function<void (const juce::String&)> done);
        perform::Location target (int slot) const;

        NorgProcessor& processor;
        Oled display;
        juce::OwnedArray<LedButton> programButtons;
        LedButton pageDown { "Page -" }, pageUp { "Page +" }, bankButton { "Bank" };
        LedButton liveButton { "Live" }, storeButton { "Store" };

        // What the buttons are pointing at: the current program's page, or one being browsed.
        int viewBank = 0, viewPage = 0;
        bool browsing = false, storing = false, blink = false;
        perform::Location storeTarget;
        std::unique_ptr<juce::AlertWindow> nameWindow;
    };
}
