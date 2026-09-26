#pragma once

namespace baastik
{
// A slim bar for the top of a plugin window: announces an update, then shows
// download progress and the result. Hidden until show() is called.
class UpdateBanner : public juce::Component
{
public:
    explicit UpdateBanner (UpdateChecker& checker);

    void showUpdate (const ReleaseInfo& release);
    void showMessage (const juce::String& text);

    void paint (juce::Graphics& g) override;
    void resized() override;

    std::function<void()> onVisibilityChanged;

private:
    void startInstall();
    void setShown (bool shown);

    UpdateChecker& checker_;
    std::optional<ReleaseInfo> release_;
    double progress_ = 0.0;
    juce::Label text_;
    juce::TextButton install_ { "Update now" };
    juce::TextButton later_ { "Later" };
    juce::TextButton skip_ { "Skip version" };
    juce::ProgressBar bar_ { progress_ };
};
} // namespace baastik
