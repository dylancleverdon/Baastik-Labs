#pragma once

#include "engine/dsp/DspUtils.h"

#include <array>

namespace norg::organ
{
    enum Bus : int { upperBus = 0, lowerBus = 1, pedalBus = 2, numBuses = 3 };

    // Classic tonewheel organ. All 91 tonewheels spin all the time at the real gear-ratio
    // frequencies; each key closes nine contacts (one per drawbar) that feed its wheels onto the
    // drawbar busbars. The contacts close a little unevenly, which is where the key click comes from.
    class TonewheelOrgan
    {
    public:
        static constexpr int numWheels = 91;
        static constexpr int maxChunk = 16;

        // Wheel 1 is C at ~32.7 Hz; wheel 46 is A at exactly 440 Hz.
        static double wheelFrequency (int wheel);

        // Which wheel a key's drawbar (0..8) plays, including foldback at both ends.
        static int wheelFor (int midiNote, int drawbar);

        void prepare (double sampleRate);
        void reset();

        // Linear gains per drawbar (index 0 = 16'). The pedal bus uses [0] = 16' and [1] = 8'.
        void setDrawbars (int bus, const std::array<float, 9>& gains);
        void setPercussion (bool on, bool third, bool fast, bool soft);
        void setClick (float amount) { clickAmount = amount; }
        void setTonewheelMode (int mode); // 0 Vintage 1, 1 Vintage 2, 2 Clean

        void keyDown (int midiNote, int bus);
        void keyUp (int midiNote);
        void allKeysUp();

        // Adds up to maxChunk samples into each bus.
        void render (float* upper, float* lower, float* pedal, int numSamples);

        bool isActive() const;

    private:
        struct Key
        {
            bool goal = false;                  // key is down
            int bus = upperBus;
            std::array<bool, 9> contact {};     // contact currently closed
            std::array<int, 9> delay { -1, -1, -1, -1, -1, -1, -1, -1, -1 }; // samples until a contact reaches the goal (-1 = settled)
            bool listed = false;                // in activeKeys
        };

        void buildTargets();
        void renderBus (int bus, float* out, int numSamples);
        bool anyUpperKeyDown() const;

        double sampleRate = 44100.0;
        std::array<double, numWheels + 1> phase {}, increment {};
        std::array<std::array<float, numWheels + 1>, numBuses> target {}, current {};
        std::array<std::array<float, 9>, numBuses> drawbars {};
        std::array<const dsp::Wavetable<2048>*, numWheels + 1> wheelShape {};

        std::array<Key, 128> keys {};
        std::array<int, 128> activeKeys {};
        int numActiveKeys = 0;

        bool percOn = false, percThird = true, percFast = true, percSoft = false;
        float percEnv = 0.0f;
        float leakage = 0.0f;
        float clickAmount = 0.5f, clickEnv = 0.0f;
        int clickEvents = 0;
        dsp::Svf clickFilter;
        dsp::Noise noise { 0x1234567u };

        dsp::Wavetable<2048> sine, warm;
    };
}
