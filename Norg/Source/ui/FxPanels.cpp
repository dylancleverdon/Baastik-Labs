#include "FxPanels.h"
#include "PluginProcessor.h"

namespace norg::ui
{
    namespace
    {
        constexpr int knobWidth = 66, buttonWidth = 60, padding = 20;
    }

    //==============================================================================
    void ColumnFrame::addKnob (Knob& k)
    {
        addAndMakeVisible (k);
        columns.push_back ({ &k, nullptr, true });
    }

    void ColumnFrame::addButton (juce::Component& c)
    {
        addAndMakeVisible (c);
        columns.push_back ({ &c, nullptr, false });
    }

    void ColumnFrame::addStack (juce::Component& top, juce::Component& bottom)
    {
        addAndMakeVisible (top);
        addAndMakeVisible (bottom);
        columns.push_back ({ &top, &bottom, false });
    }

    int ColumnFrame::naturalWidth() const
    {
        int width = padding;
        for (const auto& c : columns)
            width += c.knob ? knobWidth : buttonWidth;
        return width;
    }

    void ColumnFrame::resized()
    {
        auto area = content();
        const float scale = static_cast<float> (area.getWidth()) / static_cast<float> (juce::jmax (1, naturalWidth() - padding));

        float x = static_cast<float> (area.getX());
        for (const auto& c : columns)
        {
            const float w = static_cast<float> (c.knob ? knobWidth : buttonWidth) * scale;
            auto cell = juce::Rectangle<float> (x, static_cast<float> (area.getY()), w, static_cast<float> (area.getHeight())).toNearestInt();
            x += w;

            if (c.knob)
                c.top->setBounds (cell.withSizeKeepingCentre (juce::jmin (cell.getWidth(), 72), juce::jmin (cell.getHeight(), 90)));
            else if (c.bottom == nullptr)
                c.top->setBounds (cell.withSizeKeepingCentre (juce::jmin (cell.getWidth(), 60), juce::jmin (cell.getHeight(), 48)));
            else
            {
                const int w2 = juce::jmin (cell.getWidth(), 60);
                c.top->setBounds (cell.removeFromTop (cell.getHeight() / 2).withSizeKeepingCentre (w2, juce::jmin (cell.getHeight(), 44)));
                c.bottom->setBounds (cell.withSizeKeepingCentre (w2, juce::jmin (cell.getHeight(), 44)));
            }
        }
    }

    //==============================================================================
    ModFxPanel::ModFxPanel (juce::AudioProcessorValueTreeState& s, juce::String title, P on, P src, P typ, P rt, P amt)
        : ColumnFrame (std::move (title)),
          onButton (s, paramId (on), "On"),
          source (s, paramId (src), "Source"),
          type (s, paramId (typ), "Type"),
          rate (s, paramId (rt), "Rate"),
          amount (s, paramId (amt), "Amount")
    {
        sourceButton = &source;
        addStack (onButton, source);
        addButton (type);
        addKnob (rate);
        addKnob (amount);
    }

    AmpEqPanel::AmpEqPanel (juce::AudioProcessorValueTreeState& s)
        : ColumnFrame ("Amp / EQ"),
          onButton (s, paramId (P::ampOn), "On"),
          source (s, paramId (P::ampSource), "Source"),
          type (s, paramId (P::ampType), "Type"),
          drive (s, paramId (P::ampDrive), "Drive"),
          bass (s, paramId (P::eqBass), "Bass", true),
          mid (s, paramId (P::eqMid), "Mid", true),
          midFreq (s, paramId (P::eqMidFreq), "Mid Freq"),
          treble (s, paramId (P::eqTreble), "Treble", true)
    {
        sourceButton = &source;
        addStack (onButton, source);
        addButton (type);
        for (auto* k : { &drive, &bass, &mid, &midFreq, &treble })
            addKnob (*k);
    }

    RotaryPanel::RotaryPanel (juce::AudioProcessorValueTreeState& s)
        : ColumnFrame ("Rotary"),
          onButton (s, paramId (P::rotaryOn), "On"),
          fast (s, paramId (P::rotaryFast), "Fast"),
          stop (s, paramId (P::rotaryStop), "Stop"),
          source (s, paramId (P::rotarySource), "Source"),
          drive (s, paramId (P::rotaryDrive), "Drive")
    {
        sourceButton = &source;
        addStack (onButton, source);
        addStack (fast, stop);
        addKnob (drive);
    }

    DelayPanel::DelayPanel (juce::AudioProcessorValueTreeState& s)
        : ColumnFrame ("Delay"),
          onButton (s, paramId (P::delayOn), "On"),
          sync (s, paramId (P::delaySync), "Sync"),
          pingPong (s, paramId (P::delayPingPong), "Ping-Pong"),
          source (s, paramId (P::delaySource), "Source"),
          division (s, paramId (P::delayDivision), "Division"),
          time (s, paramId (P::delayTime), "Time"),
          feedback (s, paramId (P::delayFeedback), "Feedback"),
          mix (s, paramId (P::delayMix), "Mix"),
          tone (s, paramId (P::delayTone), "Analog")
    {
        sourceButton = &source;
        addStack (onButton, source);
        addStack (sync, pingPong);
        addKnob (time); // shares its column with the division button
        addKnob (feedback);
        addKnob (mix);
        addKnob (tone);
        addChildComponent (division);

        syncWatch = std::make_unique<juce::ParameterAttachment> (*s.getParameter (paramId (P::delaySync)), [this] (float v)
        {
            synced = v >= 0.5f;
            time.setVisible (! synced);
            division.setVisible (synced);
        });
        syncWatch->sendInitialUpdate();
    }

    void DelayPanel::resized()
    {
        ColumnFrame::resized();
        division.setBounds (time.getBounds().withSizeKeepingCentre (60, 48));
    }

    CompPanel::CompPanel (juce::AudioProcessorValueTreeState& s)
        : ColumnFrame ("Comp"),
          onButton (s, paramId (P::compOn), "On"),
          fast (s, paramId (P::compFast), "Fast"),
          amount (s, paramId (P::compAmount), "Amount")
    {
        addStack (onButton, fast);
        addKnob (amount);
    }

    ReverbPanel::ReverbPanel (juce::AudioProcessorValueTreeState& s)
        : ColumnFrame ("Reverb"),
          onButton (s, paramId (P::reverbOn), "On"),
          bright (s, paramId (P::reverbBright), "Bright"),
          type (s, paramId (P::reverbType), "Type"),
          amount (s, paramId (P::reverbAmount), "Amount")
    {
        addStack (onButton, bright);
        addButton (type);
        addKnob (amount);
    }

    //==============================================================================
    TempoPanel::TempoPanel (NorgProcessor& p)
        : SectionFrame ("Tempo"),
          processor (p),
          hostButton (p.state(), paramId (P::clockHostSync), "Host")
    {
        addAndMakeVisible (tapButton);
        addAndMakeVisible (hostButton);
        tapButton.onClick = [this] { tap(); };
        shownTempo = processor.clockTempo();
        followingHost = processor.clockFollowingHost();
        startTimerHz (30);
    }

    TempoPanel::~TempoPanel() = default;

    void TempoPanel::tap()
    {
        const auto tempo = tapper.tap (juce::Time::getMillisecondCounterHiRes() * 0.001);
        if (! tempo)
            return;

        // Tapping takes over from the host's tempo.
        if (auto* host = processor.state().getParameter (paramId (P::clockHostSync)); host != nullptr && processor.clockFollowingHost())
            host->setValueNotifyingHost (0.0f);

        if (auto* bpm = processor.state().getParameter (paramId (P::clockBpm)))
        {
            bpm->beginChangeGesture();
            bpm->setValueNotifyingHost (bpm->convertTo0to1 (static_cast<float> (*tempo)));
            bpm->endChangeGesture();
        }
    }

    void TempoPanel::timerCallback()
    {
        const double beat = processor.clockBeat();
        const bool lit = beat - std::floor (beat) < 0.12;
        const double tempo = processor.clockTempo();
        const bool host = processor.clockFollowingHost();

        if (lit != beatLit || std::abs (tempo - shownTempo) > 0.05 || host != followingHost)
        {
            beatLit = lit;
            shownTempo = tempo;
            followingHost = host;
            repaint (readoutArea().toNearestInt().expanded (4));
        }
    }

    juce::Rectangle<float> TempoPanel::readoutArea() const
    {
        return content().withTrimmedRight (128).toFloat().withSizeKeepingCentre (static_cast<float> (content().getWidth() - 128), 40.0f);
    }

    void TempoPanel::paint (juce::Graphics& g)
    {
        SectionFrame::paint (g);

        auto readout = readoutArea();
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (readout.translated (0.0f, 1.5f), 4.0f);
        g.setColour (colours::oledBack);
        g.fillRoundedRectangle (readout, 4.0f);

        auto inner = readout.reduced (8.0f, 4.0f);
        NorgLookAndFeel::drawLed (g, inner.removeFromLeft (10.0f).getCentre(), 3.5f, beatLit, colours::ledRed);
        inner.removeFromLeft (6.0f);

        g.setColour (colours::oledText);
        g.setFont (Fonts::labelBold (20.0f));
        g.drawText (juce::String (shownTempo, shownTempo < 100.0 ? 1 : 0), inner.removeFromTop (24.0f), juce::Justification::centredLeft, false);
        g.setColour (colours::oledDim);
        g.setFont (Fonts::display (10.0f));
        g.drawText (followingHost ? "BPM  HOST" : "BPM", inner, juce::Justification::centredLeft, false);
    }

    void TempoPanel::resized()
    {
        auto area = content();
        auto buttons = area.removeFromRight (124);
        tapButton.setBounds (buttons.removeFromLeft (62).withSizeKeepingCentre (58, 46));
        hostButton.setBounds (buttons.withSizeKeepingCentre (58, 46));
    }

    //==============================================================================
    EffectsRows::EffectsRows (juce::AudioProcessorValueTreeState& s, bool electro)
        : effect1 (s, "Effect 1", P::fx1On, P::fx1Source, P::fx1Type, P::fx1Rate, P::fx1Amount),
          effect2 (s, "Effect 2", P::fx2On, P::fx2Source, P::fx2Type, P::fx2Rate, P::fx2Amount),
          ampEq (s),
          rotary (s),
          delay (s),
          comp (s),
          reverb (s)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &effect1, &effect2, &ampEq, &rotary, &delay, &comp, &reverb })
            addAndMakeVisible (c);

        if (electro)
            for (auto* f : std::initializer_list<ColumnFrame*> { &effect1, &effect2, &ampEq, &rotary, &delay })
                f->setSourceNames ({ "Organ", "Piano", "Sample" });
    }

    void EffectsRows::resized()
    {
        // Each row shares its width in proportion to what each section needs.
        const auto layoutRow = [] (juce::Rectangle<int> row, std::initializer_list<ColumnFrame*> frames)
        {
            constexpr int gap = 14;
            int natural = 0;
            for (auto* f : frames)
                natural += f->naturalWidth();
            const float scale = static_cast<float> (row.getWidth() - gap * static_cast<int> (frames.size() - 1)) / static_cast<float> (natural);

            for (auto* f : frames)
            {
                f->setBounds (row.removeFromLeft (juce::roundToInt (static_cast<float> (f->naturalWidth()) * scale)));
                row.removeFromLeft (gap);
            }
        };

        auto area = getLocalBounds();
        layoutRow (area.removeFromTop (rowHeight), { &effect1, &effect2, &ampEq });
        area.removeFromTop (rowGap);
        layoutRow (area.removeFromTop (rowHeight), { &rotary, &delay, &comp, &reverb });
    }
}
