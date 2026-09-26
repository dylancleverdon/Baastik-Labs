#pragma once

#include "Widgets.h"
#include "engine/MasterClock.h"
#include "params/Parameters.h"

namespace norg
{
    class NorgProcessor;
}

namespace norg::ui
{
    // A section laid out as columns: a knob, a button, or two buttons stacked. Columns share out
    // the frame's width in proportion to their natural size.
    class ColumnFrame : public SectionFrame
    {
    public:
        using SectionFrame::SectionFrame;

        void resized() override;

        // Width the columns would like, including the frame's padding.
        int naturalWidth() const;

        // Names for the Organ / Piano / Synth-or-Sample source switch.
        void setSourceNames (const juce::StringArray& names) { if (sourceButton != nullptr) sourceButton->setDisplayNames (names); }

    protected:
        void addKnob (Knob&);
        void addButton (juce::Component&);
        void addStack (juce::Component& top, juce::Component& bottom);

        StepButton* sourceButton = nullptr;

    private:
        struct Column
        {
            juce::Component* top = nullptr;
            juce::Component* bottom = nullptr;
            bool knob = false;
        };
        std::vector<Column> columns;
    };

    // Effect 1 (Trem, Pan, Ring, Wah, A-Wah) and Effect 2 (Phaser, Flanger, Chorus, Vibe).
    class ModFxPanel final : public ColumnFrame
    {
    public:
        ModFxPanel (juce::AudioProcessorValueTreeState&, juce::String title, P on, P source, P type, P rate, P amount);

    private:
        ParamLedButton onButton;
        StepButton source, type;
        Knob rate, amount;
    };

    class AmpEqPanel final : public ColumnFrame
    {
    public:
        explicit AmpEqPanel (juce::AudioProcessorValueTreeState&);

    private:
        ParamLedButton onButton;
        StepButton source, type;
        Knob drive, bass, mid, midFreq, treble;
    };

    class RotaryPanel final : public ColumnFrame
    {
    public:
        explicit RotaryPanel (juce::AudioProcessorValueTreeState&);

    private:
        ParamLedButton onButton, fast, stop;
        StepButton source;
        Knob drive;
    };

    // Delay: synced to the master clock (by division) or free (by time).
    class DelayPanel final : public ColumnFrame
    {
    public:
        explicit DelayPanel (juce::AudioProcessorValueTreeState&);
        void resized() override;

    private:
        ParamLedButton onButton, sync, pingPong;
        StepButton source, division;
        Knob time, feedback, mix, tone;
        std::unique_ptr<juce::ParameterAttachment> syncWatch;
        bool synced = true;
    };

    class CompPanel final : public ColumnFrame
    {
    public:
        explicit CompPanel (juce::AudioProcessorValueTreeState&);

    private:
        ParamLedButton onButton, fast;
        Knob amount;
    };

    class ReverbPanel final : public ColumnFrame
    {
    public:
        explicit ReverbPanel (juce::AudioProcessorValueTreeState&);

    private:
        ParamLedButton onButton, bright;
        StepButton type;
        Knob amount;
    };

    // The master clock: a tempo LED that blinks on the beat, the tempo, Tap and Host sync.
    class TempoPanel final : public SectionFrame, private juce::Timer
    {
    public:
        explicit TempoPanel (NorgProcessor&);
        ~TempoPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void timerCallback() override;
        void tap();
        juce::Rectangle<float> readoutArea() const;

        NorgProcessor& processor;
        LedButton tapButton { "Tap" };
        ParamLedButton hostButton;
        TapTempo tapper;
        double shownTempo = 0.0;
        bool beatLit = false, followingHost = false;
    };

    // Both rows of effects, as they sit under the instrument sections.
    class EffectsRows final : public juce::Component
    {
    public:
        EffectsRows (juce::AudioProcessorValueTreeState&, bool electro);
        void resized() override;

        static constexpr int rowHeight = 130;
        static constexpr int rowGap = 10;
        static constexpr int height = 2 * rowHeight + rowGap;

    private:
        ModFxPanel effect1, effect2;
        AmpEqPanel ampEq;
        RotaryPanel rotary;
        DelayPanel delay;
        CompPanel comp;
        ReverbPanel reverb;
    };
}
