#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "serum2/PresetFile.h"

namespace
{
baastik::UpdaterConfig updaterConfig()
{
    return { "serum-preset-generator", "Serum Preset Generator", baastik::Version::parse (SPG_VERSION_STRING),
             juce::URL (SPG_UPDATE_MANIFEST_URL) };
}

HistoryEntry makeEntry (const juce::File& file, const serumgen::Result& result, const serumgen::ContentLibrary& content)
{
    const auto label = [] (const auto& list, const std::string& id) {
        for (const auto& item : list)
            if (item.id == id)
                return juce::String (item.name);
        return juce::String (id);
    };
    HistoryEntry e;
    e.file = file;
    e.name = result.name;
    e.description = label (content.categories(), result.category);
    if (! result.genre.empty())
        e.description << " / " << label (content.genres(), result.genre);
    if (! result.recipe.empty())
        e.description << " / " << juce::String (content.recipe (result.recipe).value ("name", result.recipe));
    e.description << "  (seed " << juce::String (result.seed) << ")";
    for (const auto& line : result.summary)
        e.summary.add (line);
    return e;
}
} // namespace

SerumPresetGeneratorProcessor::SerumPresetGeneratorProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      updater (updaterConfig())
{
    outputFolder = defaultOutputFolder();
    settings.chaos = 0.15;
}

bool SerumPresetGeneratorProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
        && layouts.getMainInputChannelSet() == out;
}

juce::File SerumPresetGeneratorProcessor::defaultOutputFolder()
{
    const auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    for (const auto* serumFolder : { "Xfer/Serum 2 Presets/Presets/User", "Xfer/Serum Presets/Presets/User" })
    {
        const auto dir = docs.getChildFile (serumFolder);
        if (dir.isDirectory())
            return dir.getChildFile ("Baastik");
    }
    return docs.getChildFile ("Baastik Labs").getChildFile ("Serum Presets");
}

std::optional<serum2::Preset> SerumPresetGeneratorProcessor::basePreset() const
{
    const auto* entry = selected();
    if (entry == nullptr || ! entry->file.existsAsFile())
        return std::nullopt;
    try
    {
        return serum2::readPresetFile (entry->file.getFullPathName().toStdString());
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}

juce::String SerumPresetGeneratorProcessor::generate()
{
    const auto base = basePreset();
    if (settings.mode == serumgen::Mode::Mutate && ! base)
        return "Mutate needs a base: generate or load a preset and select it first.";
    if (! outputFolder.createDirectory())
        return "Couldn't create the output folder " + outputFolder.getFullPathName();

    const auto library = content.current();
    const serumgen::Generator generator (*library);
    juce::Random random;
    for (int i = 0; i < std::max (1, batchCount); ++i)
    {
        if (randomizeSeed || i > 0)
            settings.seed = randomizeSeed ? static_cast<std::uint64_t> (random.nextInt64() & 0x7fffffffffffffffLL) % 1000000000ULL
                                          : settings.seed + 1;
        try
        {
            const auto result = generator.generate (settings, base ? &*base : nullptr);
            auto file = outputFolder.getChildFile (serumgen::presetFileName (result.name));
            for (int n = 2; file.exists(); ++n)
                file = outputFolder.getChildFile (serumgen::presetFileName (result.name + " " + std::to_string (n)));
            serum2::writePresetFile (file.getFullPathName().toStdString(), result.preset);
            history_.insert (history_.begin(), makeEntry (file, result, *library));
        }
        catch (const std::exception& e)
        {
            return juce::String ("Generation failed: ") + e.what();
        }
    }
    // While locks or Mutate are using the base, keep it selected so the next
    // click builds on the same preset; otherwise select the newest result.
    bool usesBase = settings.mode == serumgen::Mode::Mutate;
    for (const auto g : serumgen::kAllGroups)
        usesBase = usesBase || settings.isLocked (g);
    if (base && usesBase)
        selected_ = std::min (static_cast<int> (history_.size()) - 1, selected_ + std::max (1, batchCount));
    else
        selected_ = 0;
    if (onHistoryChanged)
        onHistoryChanged();
    return {};
}

juce::String SerumPresetGeneratorProcessor::loadBase (const juce::File& file)
{
    try
    {
        const auto preset = serum2::readPresetFile (file.getFullPathName().toStdString());
        HistoryEntry e;
        e.file = file;
        e.name = juce::String (preset.metadata.value ("presetName", file.getFileNameWithoutExtension().toStdString()));
        e.description = "Loaded from disk";
        history_.insert (history_.begin(), e);
        selected_ = 0;
        if (onHistoryChanged)
            onHistoryChanged();
        return {};
    }
    catch (const std::exception& e)
    {
        return "Couldn't read that preset: " + juce::String (e.what());
    }
}

const HistoryEntry* SerumPresetGeneratorProcessor::selected() const
{
    return selected_ >= 0 && selected_ < static_cast<int> (history_.size()) ? &history_[static_cast<std::size_t> (selected_)] : nullptr;
}

void SerumPresetGeneratorProcessor::select (int index)
{
    selected_ = index;
}

void SerumPresetGeneratorProcessor::removeFromHistory (int index, bool deleteFile)
{
    if (index < 0 || index >= static_cast<int> (history_.size()))
        return;
    if (deleteFile)
        history_[static_cast<std::size_t> (index)].file.deleteFile();
    history_.erase (history_.begin() + index);
    if (selected_ >= static_cast<int> (history_.size()))
        selected_ = static_cast<int> (history_.size()) - 1;
    if (onHistoryChanged)
        onHistoryChanged();
}

juce::AudioProcessorEditor* SerumPresetGeneratorProcessor::createEditor()
{
    return new SerumPresetGeneratorEditor (*this);
}

void SerumPresetGeneratorProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("SerumPresetGenerator");
    state.setProperty ("mode", juce::String (std::string (serumgen::modeId (settings.mode))), nullptr);
    state.setProperty ("category", juce::String (settings.category), nullptr);
    state.setProperty ("genre", juce::String (settings.genre), nullptr);
    state.setProperty ("chaos", settings.chaos, nullptr);
    state.setProperty ("seed", juce::String (settings.seed), nullptr);
    state.setProperty ("randomizeSeed", randomizeSeed, nullptr);
    state.setProperty ("batch", batchCount, nullptr);
    state.setProperty ("output", outputFolder.getFullPathName(), nullptr);
    juce::StringArray locked;
    for (const auto g : serumgen::kAllGroups)
        if (settings.isLocked (g))
            locked.add (juce::String (std::string (serumgen::groupId (g))));
    state.setProperty ("locked", locked.joinIntoString (","), nullptr);

    juce::ValueTree historyTree ("History");
    for (const auto& e : history_)
    {
        juce::ValueTree item ("Preset");
        item.setProperty ("file", e.file.getFullPathName(), nullptr);
        item.setProperty ("name", e.name, nullptr);
        item.setProperty ("description", e.description, nullptr);
        item.setProperty ("summary", e.summary.joinIntoString ("\n"), nullptr);
        historyTree.appendChild (item, nullptr);
    }
    state.appendChild (historyTree, nullptr);
    state.setProperty ("selected", selected_, nullptr);

    juce::MemoryOutputStream out (destData, false);
    state.writeToStream (out);
}

void SerumPresetGeneratorProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto state = juce::ValueTree::readFromData (data, static_cast<size_t> (sizeInBytes));
    if (! state.isValid())
        return;
    if (const auto mode = serumgen::modeFromId (state.getProperty ("mode", "guided").toString().toStdString()))
        settings.mode = *mode;
    settings.category = state.getProperty ("category", "any").toString().toStdString();
    settings.genre = state.getProperty ("genre", "none").toString().toStdString();
    settings.chaos = state.getProperty ("chaos", 0.15);
    settings.seed = static_cast<std::uint64_t> (state.getProperty ("seed", "1").toString().getLargeIntValue());
    randomizeSeed = state.getProperty ("randomizeSeed", true);
    batchCount = state.getProperty ("batch", 1);
    const auto output = state.getProperty ("output", {}).toString();
    if (output.isNotEmpty())
        outputFolder = juce::File (output);
    const auto locked = juce::StringArray::fromTokens (state.getProperty ("locked", {}).toString(), ",", {});
    for (const auto g : serumgen::kAllGroups)
        settings.setLocked (g, locked.contains (juce::String (std::string (serumgen::groupId (g)))));

    history_.clear();
    for (const auto& item : state.getChildWithName ("History"))
    {
        HistoryEntry e;
        e.file = juce::File (item.getProperty ("file").toString());
        e.name = item.getProperty ("name").toString();
        e.description = item.getProperty ("description").toString();
        e.summary = juce::StringArray::fromLines (item.getProperty ("summary").toString());
        if (e.file.existsAsFile())
            history_.push_back (e);
    }
    selected_ = std::min (static_cast<int> (state.getProperty ("selected", -1)), static_cast<int> (history_.size()) - 1);
    if (onHistoryChanged)
        onHistoryChanged();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SerumPresetGeneratorProcessor();
}
