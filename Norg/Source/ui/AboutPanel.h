#pragma once

#include "Widgets.h"
#include "common/UpdaterBridge.h"

namespace norg::ui
{
    // The "System" overlay: version info, auto-update controls, release notes and credits.
    class AboutPanel final : public juce::Component, private juce::Timer
    {
    public:
        AboutPanel();

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void visibilityChanged() override;

        std::function<void()> onClose;

    private:
        void timerCallback() override { refresh(); }
        void refresh();
        juce::Rectangle<int> card() const;

        UpdaterBridge bridge;
        UpdaterBridge::Status status;
        juce::String actionMessage;

        LedButton autoUpdateButton { "Auto-update" };
        juce::TextButton checkNowButton { "Check now" };
        juce::TextButton rollbackButton { "Roll back" };
        juce::TextButton closeButton { "Close" };
        juce::TextButton uninstallButton { "Uninstall..." };
        juce::TextEditor notes;
    };
}
