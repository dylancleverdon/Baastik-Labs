#pragma once

#include <juce_core/juce_core.h>

#include <memory>
#include <vector>

// Live Set (.als) files: gzip-compressed XML. Tutti reads them to find the audio a song uses,
// to point those references at Tutti's own copies, and to recognise which song Live has open.
namespace tutti::als
{
    std::unique_ptr<juce::XmlElement> load (const juce::File& file, juce::String& error);
    std::unique_ptr<juce::XmlElement> parse (const juce::MemoryBlock& gzipped, juce::String& error);
    bool save (const juce::XmlElement& root, const juce::File& file);
    juce::MemoryBlock toGzip (const juce::XmlElement& root);

    // One <FileRef> (a sample, a recording, an audio clip's file...).
    struct FileRef
    {
        juce::XmlElement* element = nullptr;
        juce::String path;     // <Path Value>
        juce::String relative; // <RelativePath Value>
        int relativeType = 0;  // <RelativePathType Value>
        juce::int64 size = 0;  // <OriginalFileSize Value>
    };

    std::vector<FileRef> fileRefs (juce::XmlElement& root);

    // Where the file is on this computer: its absolute path, or relative to the Set's folder.
    juce::File resolve (const FileRef&, const juce::File& setFolder);

    // Points a reference at `absolute`, also recorded relative to the Set's folder.
    void rewrite (FileRef&, const juce::File& absolute, const juce::File& setFolder);

    struct Track
    {
        juce::String kind; // midi, audio, group, return, master
        juce::String name;
    };

    std::vector<Track> tracks (const juce::XmlElement& root);

    // "12.1.5" from the Creator attribute ("Ableton Live 12.1.5"), or empty.
    juce::String liveVersion (const juce::XmlElement& root);

    // Names of third-party plugins the Set uses (VST2, VST3, AU).
    juce::StringArray plugins (const juce::XmlElement& root);

    // The same, with the format each instance uses: "VST3", "VST" or "AU".
    struct PluginRef
    {
        juce::String name, format;
        bool operator== (const PluginRef& o) const { return name == o.name && format == o.format; }
    };
    std::vector<PluginRef> pluginRefs (const juce::XmlElement& root);

    // How well an open Set (track names from the Remote Script) matches a file: 0..1.
    double matchScore (const std::vector<Track>& fileTracks, const juce::StringArray& liveTrackNames);
}
