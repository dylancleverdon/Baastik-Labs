#include "tutti/Als.h"

namespace tutti::als
{
    namespace
    {
        juce::String childValue (const juce::XmlElement& e, const char* tag)
        {
            if (auto* c = e.getChildByName (tag))
                return c->getStringAttribute ("Value");
            return {};
        }

        void setChildValue (juce::XmlElement& e, const char* tag, const juce::String& value)
        {
            auto* c = e.getChildByName (tag);
            if (c == nullptr)
                c = e.createNewChildElement (tag);
            c->setAttribute ("Value", value);
        }

        // A Set refers to files for samples (SampleRef) and Max for Live devices (MxPatchRef). Its
        // other FileRefs only say which preset or groove something was loaded from: Live doesn't
        // need those files, and they often no longer exist.
        void collect (juce::XmlElement& e, const juce::String& parent, std::vector<FileRef>& out)
        {
            if (e.hasTagName ("FileRef"))
            {
                if (parent != "SampleRef" && parent != "MxPatchRef")
                    return;
                FileRef ref;
                ref.element = &e;
                ref.path = childValue (e, "Path");
                ref.relative = childValue (e, "RelativePath");
                ref.relativeType = childValue (e, "RelativePathType").getIntValue();
                ref.size = childValue (e, "OriginalFileSize").getLargeIntValue();
                if (ref.path.isNotEmpty() || ref.relative.isNotEmpty())
                    out.push_back (ref);
                return;
            }
            for (auto* c : e.getChildIterator())
                collect (*c, e.getTagName(), out);
        }

        juce::String trackName (const juce::XmlElement& track)
        {
            if (auto* name = track.getChildByName ("Name"))
            {
                const auto effective = childValue (*name, "EffectiveName");
                if (effective.isNotEmpty())
                    return effective;
                return childValue (*name, "UserName");
            }
            return {};
        }
    }

    std::unique_ptr<juce::XmlElement> parse (const juce::MemoryBlock& gzipped, juce::String& error)
    {
        juce::MemoryInputStream raw (gzipped, false);
        juce::GZIPDecompressorInputStream unzip (&raw, false, juce::GZIPDecompressorInputStream::gzipFormat);
        const auto text = unzip.readEntireStreamAsString();
        if (text.isEmpty())
        {
            error = "not a Live Set (couldn't unpack it)";
            return nullptr;
        }
        auto xml = juce::parseXML (text);
        if (xml == nullptr || ! xml->hasTagName ("Ableton"))
        {
            error = "not a Live Set (no <Ableton> root)";
            return nullptr;
        }
        return xml;
    }

    std::unique_ptr<juce::XmlElement> load (const juce::File& file, juce::String& error)
    {
        juce::MemoryBlock data;
        if (! file.loadFileAsData (data))
        {
            error = "couldn't read " + file.getFullPathName();
            return nullptr;
        }
        return parse (data, error);
    }

    juce::MemoryBlock toGzip (const juce::XmlElement& root)
    {
        juce::MemoryOutputStream out;
        {
            juce::GZIPCompressorOutputStream zip (out, 6, juce::GZIPCompressorOutputStream::windowBitsGZIP);
            const auto text = root.toString (juce::XmlElement::TextFormat().withoutHeader().singleLine());
            zip << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n" << text;
            zip.flush();
        }
        return out.getMemoryBlock();
    }

    bool save (const juce::XmlElement& root, const juce::File& file)
    {
        const auto data = toGzip (root);
        auto temp = file.getSiblingFile (file.getFileName() + ".tutti-tmp");
        if (! temp.replaceWithData (data.getData(), data.getSize()))
            return false;
        return temp.moveFileTo (file);
    }

    std::vector<FileRef> fileRefs (juce::XmlElement& root)
    {
        std::vector<FileRef> out;
        collect (root, {}, out);
        return out;
    }

    juce::File resolve (const FileRef& ref, const juce::File& setFolder)
    {
        if (ref.path.isNotEmpty() && juce::File::isAbsolutePath (ref.path))
        {
            juce::File f (ref.path);
            if (f.existsAsFile())
                return f;
        }
        if (ref.relative.isNotEmpty())
        {
            auto f = setFolder.getChildFile (ref.relative.replaceCharacter ('\\', '/'));
            if (f.existsAsFile())
                return f;
        }
        return {};
    }

    void rewrite (FileRef& ref, const juce::File& absolute, const juce::File& setFolder)
    {
        auto& e = *ref.element;
        const auto relative = absolute.isAChildOf (setFolder) ? absolute.getRelativePathFrom (setFolder).replaceCharacter ('\\', '/')
                                                              : juce::String();
        setChildValue (e, "Path", absolute.getFullPathName());
        if (relative.isNotEmpty())
        {
            setChildValue (e, "RelativePathType", "3"); // relative to the Set's project folder
            setChildValue (e, "RelativePath", relative);
        }
        else
        {
            setChildValue (e, "RelativePathType", "0");
            setChildValue (e, "RelativePath", "");
        }
        ref.path = absolute.getFullPathName();
        ref.relative = relative;
    }

    std::vector<Track> tracks (const juce::XmlElement& root)
    {
        std::vector<Track> out;
        auto* set = root.getChildByName ("LiveSet");
        if (set == nullptr)
            return out;
        if (auto* list = set->getChildByName ("Tracks"))
            for (auto* t : list->getChildIterator())
            {
                juce::String kind;
                if (t->hasTagName ("MidiTrack"))
                    kind = "midi";
                else if (t->hasTagName ("AudioTrack"))
                    kind = "audio";
                else if (t->hasTagName ("GroupTrack"))
                    kind = "group";
                else if (t->hasTagName ("ReturnTrack"))
                    kind = "return";
                else
                    continue;
                out.push_back ({ kind, trackName (*t) });
            }
        for (const char* tag : { "MasterTrack", "MainTrack" })
            if (auto* m = set->getChildByName (tag))
                out.push_back ({ "master", trackName (*m) });
        return out;
    }

    juce::String liveVersion (const juce::XmlElement& root)
    {
        const auto creator = root.getStringAttribute ("Creator"); // "Ableton Live 12.1.5"
        auto version = creator.fromLastOccurrenceOf (" ", false, false).trim();
        return version.containsOnly ("0123456789.") ? version : juce::String();
    }

    juce::StringArray plugins (const juce::XmlElement& root)
    {
        juce::StringArray names;
        std::function<void (const juce::XmlElement&)> walk = [&] (const juce::XmlElement& e) {
            if (e.getTagName().endsWith ("PluginInfo"))
            {
                auto name = childValue (e, "Name");
                if (name.isEmpty())
                    name = childValue (e, "PlugName");
                if (name.isNotEmpty())
                    names.addIfNotAlreadyThere (name);
                return;
            }
            for (auto* c : e.getChildIterator())
                walk (*c);
        };
        walk (root);
        names.sort (true);
        return names;
    }

    std::vector<PluginRef> pluginRefs (const juce::XmlElement& root)
    {
        std::vector<PluginRef> out;
        std::function<void (const juce::XmlElement&)> walk = [&] (const juce::XmlElement& e) {
            const auto tag = e.getTagName();
            if (tag.endsWith ("PluginInfo"))
            {
                auto name = childValue (e, "Name");
                if (name.isEmpty())
                    name = childValue (e, "PlugName");
                const juce::String format = tag.startsWith ("Vst3") ? "VST3" : tag.startsWith ("Au") ? "AU" : "VST";
                PluginRef ref { name, format };
                if (name.isNotEmpty() && std::find (out.begin(), out.end(), ref) == out.end())
                    out.push_back (ref);
                return;
            }
            for (auto* c : e.getChildIterator())
                walk (*c);
        };
        walk (root);
        return out;
    }

    double matchScore (const std::vector<Track>& fileTracks, const juce::StringArray& liveTrackNames)
    {
        juce::StringArray fileNames;
        for (const auto& t : fileTracks)
            if (t.kind != "return" && t.kind != "master")
                fileNames.add (t.name);
        if (fileNames.isEmpty() && liveTrackNames.isEmpty())
            return 1.0;
        const int n = std::max (fileNames.size(), liveTrackNames.size());
        int same = 0;
        for (int i = 0; i < std::min (fileNames.size(), liveTrackNames.size()); ++i)
            if (fileNames[i] == liveTrackNames[i])
                ++same;
        return n == 0 ? 0.0 : static_cast<double> (same) / n;
    }
}
