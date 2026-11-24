#pragma once

#include "AppState.h"
#include "Rows.h"

class SettingsPage;
class SuggestionPage;

// The Tutti window: header with status, the current page, and a footer with the latest notice.
class MainComponent : public juce::Component
{
public:
    explicit MainComponent (AppActions&);
    ~MainComponent() override;

    void update (const AppState&);

    enum class Page { main, settings, suggest, versions };
    void showPage (Page);
    Page page() const { return currentPage; }

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void chooseSong();
    void askForMessage();

private:
    std::vector<Row> buildRows (const AppState&);
    std::vector<Row> buildVersionRows (const AppState&);
    void askForLabel();

    AppActions& actions;
    AppState state;
    Page currentPage = Page::main;

    juce::Viewport viewport;
    RowList rows;
    juce::Viewport settingsView;
    std::unique_ptr<SettingsPage> settings;
    std::unique_ptr<SuggestionPage> suggestions;
    juce::TextButton suggestButton { "Suggestion box" }, settingsButton { "Settings" };
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AlertWindow> labelPrompt;
};
