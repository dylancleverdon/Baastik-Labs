#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "PluginProcessor.h"

class GeneratorLookAndFeel : public juce::LookAndFeel_V4
{
public:
    GeneratorLookAndFeel();
};

class SerumPresetGeneratorEditor : public juce::AudioProcessorEditor,
                                   public juce::DragAndDropContainer,
                                   private juce::ListBoxModel
{
public:
    explicit SerumPresetGeneratorEditor (SerumPresetGeneratorProcessor&);
    ~SerumPresetGeneratorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    // ListBoxModel (history)
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void deleteKeyPressed (int lastRowSelected) override;

    void rebuildMenus();
    void syncFromProcessor();
    void refreshHistory();
    void updateModeUi();
    void generate();
    void setStatus (const juce::String& text, bool error = false);
    void showSettingsMenu();
    void checkForUpdates (bool userInitiated);

    SerumPresetGeneratorProcessor& proc_;
    GeneratorLookAndFeel lnf_;

    juce::Label title_, version_, status_;
    juce::TextButton settings_ { "Settings" };
    baastik::UpdateBanner banner_;

    // Left: what to make.
    juce::Label modeLabel_, categoryLabel_, genreLabel_, chaosLabel_, seedLabel_, batchLabel_;
    juce::TextButton random_ { "Random" }, guided_ { "Guided" }, mutate_ { "Mutate" };
    juce::ComboBox category_, genre_;
    juce::Slider chaos_, batch_;
    juce::TextEditor seed_;
    juce::ToggleButton newSeed_ { "New seed each time" };
    juce::TextButton generate_ { "Generate" };
    std::vector<std::string> categoryIds_, genreIds_;

    // Middle: which parts change.
    juce::Label groupsLabel_, groupsHint_, baseLabel_;
    juce::OwnedArray<juce::ToggleButton> groupToggles_;
    juce::TextButton allOn_ { "All" }, allOff_ { "None" }, loadBase_ { "Load preset..." };

    // Right: results.
    juce::Label historyLabel_, dragHint_;
    juce::ListBox history_ { "History", this };
    juce::TextEditor summary_;
    juce::TextButton reveal_ { "Show file" }, delete_ { "Delete" };

    // Footer.
    juce::Label outputLabel_;
    juce::TextButton changeOutput_ { "Change..." };
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SerumPresetGeneratorEditor)
};
