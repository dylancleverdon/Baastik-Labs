#pragma once

#include <baastik_updater/baastik_updater.h>

#include <filesystem>
#include <mutex>
#include <optional>

#include "tts/Server.h"

// Text To Synth's auto-updater, on the shared Baastik updater:
//   - on launch (at most hourly) it reads update.json from the
//     "text-to-synth-latest" release channel;
//   - a newer version shows up as a one-line notice on every tool result,
//     so Claude tells the user; "update Text To Synth" runs install_update,
//     which downloads the signed installer and opens it;
//   - newer vocabulary content is downloaded silently and used from the next
//     guide call, no reinstall needed.
//
// The updater lives on the JUCE message thread; the MCP server calls in from
// its own thread and waits for the answer.
class JuceUpdates : public tts::UpdateService
{
public:
    JuceUpdates (const juce::String& version, const juce::URL& manifestUrl, std::filesystem::path dataDir);

    // Message thread: the throttled launch check.
    void start();

    std::string notice() override;
    std::string checkNow() override;
    std::string installUpdate() override;

private:
    void handleCheck (std::optional<baastik::ReleaseInfo> update, const juce::var& manifest);
    void updateContent (const juce::var& manifest);

    baastik::UpdateChecker checker_;
    std::filesystem::path dataDir_;
    std::mutex mutex_;
    std::optional<baastik::ReleaseInfo> pending_;
    bool installing_ = false;
};
