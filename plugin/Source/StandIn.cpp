#include "StandIn.h"

#include "tutti/Protocol.h"
#include "tutti/SocketIO.h"

namespace
{
    constexpr int kLocalPort = 47381;

    juce::File captureFolder()
    {
        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Tutti Stand-in");
        f.createDirectory();
        return f;
    }
}

// ====================================================================== Bridge
// Talks to the Tutti app on this computer: who we are, what mode to be in, which recordings to
// play, and the segments we record.
class StandInProcessor::Bridge : public juce::Thread
{
public:
    explicit Bridge (StandInProcessor& p) : juce::Thread ("Tutti Stand-in bridge"), owner (p) { startThread(); }

    ~Bridge() override
    {
        signalThreadShouldExit();
        {
            std::lock_guard<std::mutex> g (socketLock);
            if (socket != nullptr)
                tutti::io::shutdownBoth (socket->getRawSocketHandle()); // wakes the reader; released below
        }
        stopThread (3000);
    }

    void send (const tutti::Json& message)
    {
        const auto bytes = tutti::protocol::encode (message);
        std::lock_guard<std::mutex> g (socketLock);
        if (socket != nullptr && socket->isConnected())
            tutti::io::sendAll (socket->getRawSocketHandle(), bytes.data(), (int) bytes.size());
    }

    void sayHello()
    {
        send (tutti::Json { { "type", "hello" }, { "role", "standin" }, { "link", (double) owner.link->get() } });
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            auto s = std::make_unique<juce::StreamingSocket>();
            if (! s->connect ("127.0.0.1", kLocalPort, 1000))
            {
                owner.isConnected = false;
                wait (2000);
                continue;
            }
            tutti::protocol::noSigPipe (*s); // a closed Tutti app must never take Live down with it
            tutti::io::makeBlocking (s->getRawSocketHandle());
            {
                std::lock_guard<std::mutex> g (socketLock);
                socket = std::move (s);
            }
            owner.isConnected = true;
            sayHello();
            readLoop();
            owner.isConnected = false;
            {
                std::lock_guard<std::mutex> g (socketLock);
                socket.reset();
            }
        }
    }

private:
    void readLoop()
    {
        tutti::protocol::Decoder decoder;
        std::vector<char> buffer (1 << 16);
        while (! threadShouldExit())
        {
            juce::StreamingSocket* s = nullptr;
            {
                std::lock_guard<std::mutex> g (socketLock);
                s = socket.get();
            }
            if (s == nullptr)
                return;
            // Our own reads: JUCE's non-blocking read would flip the whole socket to non-blocking
            // and break the sends.
            const int ready = tutti::io::waitReadable (s->getRawSocketHandle(), 250);
            if (ready < 0)
                return;
            if (ready == 0)
                continue;
            const int n = tutti::io::receive (s->getRawSocketHandle(), buffer.data(), (int) buffer.size());
            if (n == -2)
                continue;
            if (n <= 0)
                return;
            decoder.feed (buffer.data(), (size_t) n);
            while (auto frame = decoder.next())
                handle (frame->json);
            if (decoder.failed())
                return;
        }
    }

    void handle (const tutti::Json& m)
    {
        if (tutti::str (m, "type") != "standin")
            return;
        const auto mode = tutti::str (m, "mode");
        owner.mode = (int) (mode == "capture" ? Mode::capture : mode == "play" ? Mode::play : Mode::idle);
        {
            std::lock_guard<std::mutex> g (owner.textLock);
            owner.who = juce::String (tutti::str (m, "who"));
        }

        // Load the recordings here, off the audio thread, then swap them in.
        auto list = std::make_shared<SegmentList>();
        int stale = 0;
        if (const auto& segs = tutti::field (m, "segments"); segs.is_array())
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            for (const auto& j : segs)
            {
                const juce::File file (juce::String (tutti::str (j, "path")));
                auto key = file.getFullPathName() + ":" + juce::String (file.getLastModificationTime().toMilliseconds());
                std::shared_ptr<const Segment> seg;
                if (auto it = cache.find (key); it != cache.end())
                    seg = it->second;
                else if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (file) })
                {
                    auto loaded = std::make_shared<Segment>();
                    const int len = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) reader->sampleRate * 60 * 20);
                    loaded->audio.setSize ((int) reader->numChannels, len);
                    reader->read (&loaded->audio, 0, len, 0, true, true);
                    loaded->rate = reader->sampleRate;
                    seg = loaded;
                    cache[key] = seg;
                }
                if (seg == nullptr)
                    continue;
                auto placed = std::make_shared<Segment> ();
                placed->audio.makeCopyOf (seg->audio, true);
                placed->rate = seg->rate;
                placed->startBeat = tutti::number (j, "start");
                placed->lengthBeats = tutti::number (j, "len");
                placed->tempo = tutti::number (j, "tempo", 120.0);
                placed->stale = tutti::boolean (j, "stale");
                stale += placed->stale ? 1 : 0;
                list->push_back (placed);
            }
        }
        owner.staleCount = stale;
        std::shared_ptr<const SegmentList> next = list;
        {
            const juce::SpinLock::ScopedLockType sl (owner.segmentsLock);
            owner.segments.swap (next);
        }
        // `next` (the old list) is released here, on this thread, not on the audio thread.
    }

    StandInProcessor& owner;
    std::mutex socketLock;
    std::unique_ptr<juce::StreamingSocket> socket;
    std::map<juce::String, std::shared_ptr<const Segment>> cache;
};

// ====================================================================== Recorder
// Owns the capture file: starts one when playback starts, finishes it when playback stops or
// jumps, and reports each finished segment. The audio thread only ever try-locks and writes.
class StandInProcessor::Recorder : public juce::Thread
{
public:
    explicit Recorder (StandInProcessor& p) : juce::Thread ("Tutti Stand-in recorder"), owner (p)
    {
        writerThread.startThread();
        startThread();
    }

    ~Recorder() override
    {
        stopThread (3000);
        finish (false);
        writerThread.stopThread (3000);
    }

    // Audio thread.
    void write (const juce::AudioBuffer<float>& buffer, double ppq, double bpm, double rate)
    {
        const juce::SpinLock::ScopedTryLockType sl (lock);
        if (! sl.isLocked() || writer == nullptr)
            return;
        const double blockBeats = buffer.getNumSamples() / rate * bpm / 60.0;
        if (firstPpq < 0)
        {
            firstPpq = ppq;
            tempo = bpm;
        }
        else if (std::abs (ppq - expectedPpq) > 0.05)
        {
            jumped = true; // looped or moved: this segment ends here
            return;
        }
        expectedPpq = ppq + blockBeats;
        writer->write (buffer.getArrayOfReadPointers(), buffer.getNumSamples());
        written += buffer.getNumSamples();
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            const bool want = owner.mode.load() == (int) Mode::capture && owner.playing.load();
            bool active;
            {
                const juce::SpinLock::ScopedLockType sl (lock);
                active = writer != nullptr;
            }
            if (want && ! active)
                begin();
            else if (active && (! want || jumped.load()))
                finish (true);
            wait (20);
        }
    }

private:
    void begin()
    {
        file = captureFolder().getChildFile ("take-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()) + ".wav");
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        const double rate = owner.hostRate.load();
        auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate)
                                                  .withNumChannels (2).withBitsPerSample (24));
        if (w == nullptr)
            return;
        auto threaded = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (w.release(), writerThread, 1 << 16);
        const juce::SpinLock::ScopedLockType sl (lock);
        writer = std::move (threaded);
        firstPpq = -1;
        expectedPpq = 0;
        written = 0;
        sampleRate = rate;
        jumped = false;
    }

    void finish (bool report)
    {
        std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> done;
        double start, bpm, rate;
        juce::int64 samples;
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            done = std::move (writer);
            start = firstPpq;
            bpm = tempo;
            rate = sampleRate;
            samples = written;
            jumped = false;
        }
        done.reset(); // flushes and closes the file
        if (! report || start < 0 || samples < (juce::int64) (rate * 0.5))
        {
            file.deleteFile();
            return;
        }
        const double lengthBeats = (double) samples / rate * bpm / 60.0;
        if (owner.bridge != nullptr)
            owner.bridge->send (tutti::Json { { "type", "segment" }, { "link", (double) owner.link->get() },
                                              { "path", file.getFullPathName().toStdString() }, { "start", start },
                                              { "len", lengthBeats }, { "tempo", bpm }, { "rate", rate } });
    }

    StandInProcessor& owner;
    juce::TimeSliceThread writerThread { "Tutti Stand-in writer" };
    juce::SpinLock lock;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    juce::File file;
    double firstPpq = -1, expectedPpq = 0, tempo = 120.0, sampleRate = 44100.0;
    juce::int64 written = 0;
    std::atomic<bool> jumped { false };
};

// ====================================================================== processor

StandInProcessor::StandInProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // A random id, readable by the Remote Script (six decimals survive Live's float rounding).
    const float id = (float) (juce::Random::getSystemRandom().nextInt ({ 1, 999999 })) / 1000000.0f;
    addParameter (link = new juce::AudioParameterFloat (juce::ParameterID { "link", 1 }, "Link",
                                                        juce::NormalisableRange<float> (0.0f, 1.0f), id));
    segments = std::make_shared<const SegmentList>();
    bridge = std::make_unique<Bridge> (*this);
    recorder = std::make_unique<Recorder> (*this);
}

StandInProcessor::~StandInProcessor()
{
    recorder.reset();
    bridge.reset();
}

void StandInProcessor::setLink (float value)
{
    link->setValueNotifyingHost (value);
    if (bridge != nullptr)
        bridge->sayHello();
}

void StandInProcessor::prepareToPlay (double sampleRate, int)
{
    hostRate = sampleRate;
}

void StandInProcessor::releaseResources()
{
    playing = false;
}

bool StandInProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
}

void StandInProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    double ppq = 0, bpm = 120;
    bool isPlaying = false;
    if (auto* head = getPlayHead())
        if (auto pos = head->getPosition())
        {
            isPlaying = pos->getIsPlaying();
            ppq = pos->getPpqPosition().orFallback (0.0);
            bpm = pos->getBpm().orFallback (120.0);
        }
    playing = isPlaying;

    switch ((Mode) mode.load())
    {
        case Mode::capture:
            if (isPlaying)
                recorder->write (buffer, ppq, bpm, getSampleRate() > 0 ? getSampleRate() : hostRate.load());
            break; // audio passes through
        case Mode::play:
            playSegments (buffer, ppq, bpm);
            if (! isPlaying)
                buffer.clear();
            break;
        case Mode::idle:
            break;
    }
}

void StandInProcessor::playSegments (juce::AudioBuffer<float>& buffer, double ppq, double bpm)
{
    std::shared_ptr<const SegmentList> list;
    {
        const juce::SpinLock::ScopedTryLockType sl (segmentsLock);
        if (! sl.isLocked())
            return; // being swapped right now: keep what came in for this block
        list = segments;
    }
    buffer.clear();
    if (list == nullptr || list->empty())
        return;
    const double rate = getSampleRate() > 0 ? getSampleRate() : hostRate.load();
    const double beatsPerSample = bpm / 60.0 / rate;
    const int n = buffer.getNumSamples();
    const Segment* seg = nullptr;
    for (int i = 0; i < n; ++i)
    {
        const double beat = ppq + i * beatsPerSample;
        if (seg == nullptr || beat < seg->startBeat || beat >= seg->startBeat + seg->lengthBeats)
        {
            seg = nullptr;
            for (const auto& s : *list)
                if (beat >= s->startBeat && beat < s->startBeat + s->lengthBeats)
                {
                    seg = s.get();
                    break;
                }
        }
        if (seg == nullptr)
            continue;
        const double pos = (beat - seg->startBeat) * 60.0 / seg->tempo * seg->rate;
        const int i0 = (int) pos;
        const float frac = (float) (pos - i0);
        const int len = seg->audio.getNumSamples();
        if (i0 < 0 || i0 + 1 >= len)
            continue;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const auto* src = seg->audio.getReadPointer (std::min (ch, seg->audio.getNumChannels() - 1));
            buffer.setSample (ch, i, src[i0] + frac * (src[i0 + 1] - src[i0]));
        }
    }
}

void StandInProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement state ("TuttiStandIn");
    state.setAttribute ("link", (double) link->get());
    copyXmlToBinary (state, dest);
}

void StandInProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size); xml != nullptr && xml->hasTagName ("TuttiStandIn"))
        setLink ((float) xml->getDoubleAttribute ("link", (double) link->get()));
}

juce::String StandInProcessor::statusText() const
{
    if (! isConnected)
        return "Not connected to Tutti";
    switch ((Mode) mode.load())
    {
        case Mode::capture: return playing ? "Recording this track for the others" : "Ready: play the song to share this track";
        case Mode::play:
        {
            std::lock_guard<std::mutex> g (textLock);
            return who.isEmpty() ? juce::String ("Waiting for a recording of this track")
                                 : "Playing " + who + "'s recording of this track";
        }
        case Mode::idle: return "Put me right after the plugin to stand in for";
    }
    return {};
}

juce::String StandInProcessor::detailText() const
{
    if ((Mode) mode.load() == Mode::play && staleCount.load() > 0)
        return "Out of date: the notes or sound changed since it was recorded. It updates when an owner plays this part again.";
    if ((Mode) mode.load() == Mode::capture)
        return "Friends without this plugin hear what you record here.";
    return {};
}

// ====================================================================== editor

class StandInEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit StandInEditor (StandInProcessor& p) : juce::AudioProcessorEditor (p), proc (p)
    {
        setSize (360, 130);
        startTimerHz (5);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff15161a));
        auto r = getLocalBounds().reduced (16, 12);
        g.setColour (juce::Colour (0xffededf0));
        g.setFont (juce::Font (juce::FontOptions (17.0f).withStyle ("Bold")));
        g.drawText ("Tutti Stand-in", r.removeFromTop (24), juce::Justification::centredLeft);
        g.setColour (proc.connected() ? juce::Colour (0xff7ed957) : juce::Colour (0xffffc94a));
        g.setFont (juce::Font (juce::FontOptions (14.0f)));
        g.drawText (proc.statusText(), r.removeFromTop (22), juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xff9a9dab));
        g.setFont (juce::Font (juce::FontOptions (12.5f)));
        g.drawFittedText (proc.detailText(), r, juce::Justification::topLeft, 3);
    }

private:
    void timerCallback() override { repaint(); }
    StandInProcessor& proc;
};

juce::AudioProcessorEditor* StandInProcessor::createEditor() { return new StandInEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new StandInProcessor(); }
