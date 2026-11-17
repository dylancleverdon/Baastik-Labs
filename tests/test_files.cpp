#include "tutti/Als.h"
#include "tutti/AudioFiles.h"
#include "tutti/Tailscale.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>

using namespace tutti;

namespace
{
    struct TempDir
    {
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("tutti-tests-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
        TempDir() { dir.createDirectory(); }
        ~TempDir() { dir.deleteRecursively(); }
    };

    juce::File writeSine (const juce::File& file, double rate, int channels, int64_t frames)
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate)
                                                      .withNumChannels (channels).withBitsPerSample (24));
        REQUIRE (writer != nullptr);
        juce::AudioBuffer<float> buffer (channels, static_cast<int> (frames));
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < frames; ++i)
                buffer.setSample (c, i, 0.5f * std::sin (2.0f * juce::MathConstants<float>::pi * 440.0f * (float) i / (float) rate));
        writer->writeFromAudioSampleBuffer (buffer, 0, static_cast<int> (frames));
        return file;
    }

    const char* setXml (const juce::String& samplePath)
    {
        static juce::String text;
        text = R"(<?xml version="1.0" encoding="UTF-8"?>
<Ableton MajorVersion="5" MinorVersion="12.0_12117" Creator="Ableton Live 12.1.5" Revision="">
  <LiveSet>
    <Tracks>
      <MidiTrack Id="8"><Name><EffectiveName Value="Drums"/><UserName Value="Drums"/></Name>
        <DeviceChain><Devices><PluginDevice Id="1"><PluginDesc><Vst3PluginInfo Id="0"><Name Value="Serum 2"/></Vst3PluginInfo></PluginDesc></PluginDevice></Devices></DeviceChain>
      </MidiTrack>
      <AudioTrack Id="9"><Name><EffectiveName Value="Vox"/><UserName Value=""/></Name>
        <DeviceChain><MainSequencer><Sample><ArrangerAutomation><Events><AudioClip Id="0" Time="16">
          <SampleRef><FileRef>
            <RelativePathType Value="3"/><RelativePath Value="Samples/Recorded/take.wav"/>
            <Path Value=")" + samplePath + R"("/><Type Value="1"/><LivePackName Value=""/><LivePackId Value=""/>
            <OriginalFileSize Value="1234"/><OriginalCrc Value="99"/>
          </FileRef></SampleRef>
        </AudioClip></Events></ArrangerAutomation></Sample></MainSequencer></DeviceChain>
      </AudioTrack>
      <ReturnTrack Id="2"><Name><EffectiveName Value="A-Reverb"/></Name></ReturnTrack>
    </Tracks>
    <MasterTrack><Name><EffectiveName Value="Main"/></Name></MasterTrack>
  </LiveSet>
</Ableton>)";
        return text.toRawUTF8();
    }
}

TEST_CASE ("Live Sets: read tracks, plugins and file references, then repoint them", "[als]")
{
    TempDir tmp;
    auto sample = writeSine (tmp.dir.getChildFile ("take.wav"), 48000, 2, 4800);
    auto xml = juce::parseXML (setXml (sample.getFullPathName()));
    REQUIRE (xml);
    auto setFile = tmp.dir.getChildFile ("Song.als");
    REQUIRE (als::save (*xml, setFile));

    juce::String error;
    auto loaded = als::load (setFile, error);
    REQUIRE (loaded);
    CHECK (als::liveVersion (*loaded) == "12.1.5");
    CHECK (als::plugins (*loaded) == juce::StringArray { "Serum 2" });

    const auto tracks = als::tracks (*loaded);
    REQUIRE (tracks.size() == 4);
    CHECK (tracks[0].kind == "midi");
    CHECK (tracks[0].name == "Drums");
    CHECK (tracks[1].name == "Vox");
    CHECK (tracks[2].kind == "return");
    CHECK (tracks[3].kind == "master");
    CHECK (als::matchScore (tracks, { "Drums", "Vox" }) == 1.0);
    CHECK (als::matchScore (tracks, { "Drums", "Bass", "Keys" }) < 0.5);

    auto refs = als::fileRefs (*loaded);
    REQUIRE (refs.size() == 1);
    CHECK (als::resolve (refs[0], tmp.dir) == sample);

    auto project = tmp.dir.getChildFile ("Tutti/Song");
    auto moved = project.getChildFile ("Samples/Tutti/take.wav");
    REQUIRE (cloneOrCopy (sample, moved));
    als::rewrite (refs[0], moved, project);
    REQUIRE (als::save (*loaded, project.getChildFile ("Song.als")));

    auto again = als::load (project.getChildFile ("Song.als"), error);
    REQUIRE (again);
    auto refs2 = als::fileRefs (*again);
    CHECK (refs2[0].path == moved.getFullPathName());
    CHECK (refs2[0].relative == "Samples/Tutti/take.wav");
    CHECK (als::resolve (refs2[0], project) == moved);

    CHECK_FALSE (als::load (tmp.dir.getChildFile ("missing.als"), error));
    tmp.dir.getChildFile ("junk.als").replaceWithText ("not gzip");
    CHECK_FALSE (als::load (tmp.dir.getChildFile ("junk.als"), error));
}

TEST_CASE ("Store keeps files by hash and checks downloads", "[store]")
{
    TempDir tmp;
    Store store (tmp.dir.getChildFile ("store"));
    auto file = writeSine (tmp.dir.getChildFile ("Kick.wav"), 44100, 1, 2000);
    juce::String error;
    auto hash = store.add (file, error);
    REQUIRE (hash);
    CHECK (hash->size() == 64);
    CHECK (store.has (*hash));
    CHECK (store.fileFor (*hash).getFileName() == "Kick.wav");
    CHECK (store.hashOf (file) == hash);
    CHECK (store.meta (*hash)->name == "Kick.wav");

    // A download that matches lands in the store; one that doesn't is thrown away.
    Store other (tmp.dir.getChildFile ("other"));
    AudioMeta m;
    m.hash = *hash;
    m.name = "Kick.wav";
    other.writeMeta (m);
    auto part = other.partialFor (*hash, "full");
    part.getParentDirectory().createDirectory();
    REQUIRE (file.copyFileTo (part));
    CHECK (other.commit (part, *hash, "full", error));
    CHECK (other.has (*hash));

    auto bad = other.partialFor (std::string (64, 'a'), "full");
    bad.replaceWithText ("nope");
    CHECK_FALSE (other.commit (bad, std::string (64, 'a'), "full", error));
    CHECK_FALSE (bad.exists());
}

TEST_CASE ("Previews are small and decode to the exact original length", "[audio]")
{
    TempDir tmp;
    Store store (tmp.dir.getChildFile ("store"));
    const int64_t frames = 59257; // 1.2345 s at 48 kHz: not a multiple of any codec block
    auto file = writeSine (tmp.dir.getChildFile ("Vox take 3.wav"), 48000, 2, frames);
    juce::String error;
    auto hash = store.add (file, error);
    REQUIRE (hash);
    REQUIRE (audio::ensurePreview (store, *hash, error));

    auto meta = store.meta (*hash);
    REQUIRE (meta);
    CHECK (meta->rate == 48000);
    CHECK (meta->channels == 2);
    CHECK (meta->frames == frames);
    CHECK (meta->previewSize > 0);
    CHECK (meta->previewSize * 4 < meta->size); // much smaller than the WAV
    CHECK (meta->previewHash == Store::sha256 (store.previewOggFor (*hash)));

    auto wav = store.previewWavFor (*hash);
    REQUIRE (audio::decodePreview (store.previewOggFor (*hash), { meta->rate, meta->channels, meta->frames, meta->bits }, wav, error));
    auto info = audio::probe (wav);
    REQUIRE (info);
    CHECK (info->frames == frames);
    CHECK (info->rate == 48000);
    CHECK (info->channels == 2);
    CHECK (wav.getFileName() == "Vox take 3 (preview).wav");
    CHECK (store.bestFor (*hash) == store.fileFor (*hash));
}

TEST_CASE ("Tailscale status is parsed", "[tailscale]")
{
    const char* json = R"({
      "BackendState": "Running",
      "Self": {"HostName": "dylan-mbp", "TailscaleIPs": ["100.64.0.1", "fd7a::1"], "UserID": 1, "Online": true},
      "Peer": {
        "nodekey:a": {"HostName": "sam-pc", "DNSName": "sam-pc.tail.ts.net.", "TailscaleIPs": ["100.64.0.2"], "UserID": 2, "Online": true, "OS": "windows"},
        "nodekey:b": {"HostName": "old-laptop", "TailscaleIPs": ["100.64.0.3"], "UserID": 1, "Online": false}
      },
      "User": {"1": {"DisplayName": "Dylan"}, "2": {"DisplayName": "Sam", "LoginName": "sam@example.com"}}
    })";
    auto status = tailscale::parse (json);
    REQUIRE (status);
    CHECK (status->running());
    CHECK (status->selfIp4 == "100.64.0.1");
    CHECK (status->selfUser == "Dylan");
    REQUIRE (status->peers.size() == 2);
    const auto& sam = status->peers[0].hostName == "sam-pc" ? status->peers[0] : status->peers[1];
    CHECK (sam.ip4 == "100.64.0.2");
    CHECK (sam.user == "Sam");
    CHECK (sam.online);
    CHECK_FALSE (tailscale::parse ("not json"));
}
