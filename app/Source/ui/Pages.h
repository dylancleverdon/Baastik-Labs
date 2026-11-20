#pragma once

#include "AppState.h"
#include "Theme.h"

// Your name and colour, Play together, updates, start at login, the Remote Script.
class SettingsPage : public juce::Component
{
public:
    SettingsPage (AppActions&, std::function<void()> onDone);
    void update (const AppState&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    static constexpr int contentHeight = 720;

private:
    juce::Rectangle<int> swatchArea (int index) const;

    AppActions& actions;
    std::function<void()> done;
    AppState state;
    int colour = 0;
    juce::TextEditor name;
    juce::TextButton ask { "Ask me" }, automatic { "Join automatically" }, never { "Never" };
    juce::ToggleButton autoUpdate { "Update automatically" }, startAtLogin { "Start Tutti when I log in" };
    juce::TextButton checkNow { "Check now" }, rollBack { "Roll back" }, logs { "Show logs" }, back { "Done" };
    juce::TextButton chooseBackup { "Backups..." };
    juce::TextButton chooseShared { "Choose folder..." }, findShared { "Find it automatically" };
    juce::Label updateStatus, notes, scriptInfo, backupInfo;
    std::unique_ptr<juce::FileChooser> chooser;
    int nameTop = 0, playTop = 0, updatesTop = 0, setupTop = 0, backupTop = 0;
};

// Ideas and problems for the team's Google Sheet.
class SuggestionPage : public juce::Component
{
public:
    SuggestionPage (AppActions&, std::function<void()> onDone);
    void update (const AppState&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AppActions& actions;
    std::function<void()> done;
    std::string kind = "Idea";
    juce::TextButton idea { "Idea" }, problem { "Something's wrong" }, other { "Other" };
    juce::TextEditor text;
    juce::TextButton send { "Send" }, sendLogs { "Send logs" }, back { "Back" };
    juce::Label status;
};
