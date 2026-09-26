#include "ProgramLibrary.h"

namespace norg::perform
{
    namespace
    {
        struct Setting
        {
            P param;
            float value;
        };

        Program make (const char* name, std::initializer_list<Setting> settings)
        {
            Program program;
            program.setName (name);
            for (const auto& s : settings)
                program.set (s.param, 0, s.value);
            return program;
        }

        void drawbars (Program& program, std::initializer_list<int> positions, bool presetII = false)
        {
            static constexpr P first[] = { P::organDbI1, P::organDbII1 };
            int i = 0;
            for (int v : positions)
                program.set (static_cast<P> (static_cast<int> (first[presetII ? 1 : 0]) + i++), 0, static_cast<float> (v));
        }

        // Piano-only programs turn the organ off.
        constexpr Setting pianoOnly[] = { { P::organOn, 0 }, { P::pianoOn, 1 } };

        Program piano (const char* name, std::initializer_list<Setting> settings)
        {
            auto program = make (name, settings);
            for (const auto& s : pianoOnly)
                program.set (s.param, 0, s.value);
            return program;
        }
    }

    std::vector<std::pair<Location, Program>> ProgramLibrary::factoryPrograms()
    {
        std::vector<std::pair<Location, Program>> list;
        const auto add = [&list] (int page, int slot, Program program)
        {
            list.emplace_back (Location::program (0, page, slot), std::move (program));
        };

        // --- A:1  the classics ------------------------------------------------------------------
        {
            auto p = make ("Norg Rock B3", { { P::organPercOn, 1 }, { P::organPercThird, 1 }, { P::rotaryDrive, 0.5f },
                                             { P::reverbAmount, 0.2f } });
            drawbars (p, { 8, 8, 8, 0, 0, 0, 0, 0, 0 });
            drawbars (p, { 8, 8, 8, 8, 8, 8, 8, 8, 8 }, true);
            add (0, 0, p);
        }
        {
            auto p = make ("Gospel Perc + Pedals", { { P::organSplit, 1 }, { P::organPedals, 1 }, { P::organPercOn, 1 },
                                                     { P::organPercThird, 0 }, { P::organVibOn, 1 }, { P::organVibMode, 5 },
                                                     { P::rotaryDrive, 0.35f }, { P::reverbAmount, 0.25f } });
            drawbars (p, { 8, 8, 8, 8, 0, 0, 0, 0, 0 });
            add (0, 1, p);
        }
        add (0, 2, piano ("Suitcase Mk I", { { P::pianoType, 2 }, { P::pianoTineModel, 2 }, { P::pianoTimbre, 1 },
                                             { P::fx1On, 1 }, { P::fx1Type, 1 }, { P::fx1Rate, 0.5f }, { P::fx1Amount, 0.6f },
                                             { P::reverbAmount, 0.2f } }));
        add (0, 3, piano ("Dyno Ballad", { { P::pianoType, 2 }, { P::pianoTineModel, 0 }, { P::pianoTimbre, 3 },
                                           { P::fx2On, 1 }, { P::fx2Type, 2 }, { P::fx2Rate, 0.3f }, { P::fx2Amount, 0.4f },
                                           { P::reverbType, 2 }, { P::reverbAmount, 0.3f } }));
        add (0, 4, piano ("Wurl Grit", { { P::pianoType, 3 }, { P::ampOn, 1 }, { P::ampType, 2 }, { P::ampDrive, 0.55f },
                                         { P::fx1On, 1 }, { P::fx1Type, 0 }, { P::fx1Rate, 0.55f }, { P::fx1Amount, 0.4f },
                                         { P::reverbType, 0 }, { P::reverbAmount, 0.25f } }));

        // --- A:2  keys --------------------------------------------------------------------------
        add (1, 0, piano ("Clav Funk", { { P::pianoType, 4 }, { P::pianoClavPickup, 2 }, { P::pianoClavFilter, 2 },
                                         { P::fx1On, 1 }, { P::fx1Type, 4 }, { P::fx1Rate, 0.5f }, { P::fx1Amount, 0.8f },
                                         { P::ampOn, 1 }, { P::ampType, 3 }, { P::ampDrive, 0.3f },
                                         { P::reverbType, 0 }, { P::reverbAmount, 0.15f } }));
        add (1, 1, piano ("Grand Hall", { { P::pianoType, 0 }, { P::reverbType, 2 }, { P::reverbAmount, 0.3f }, { P::reverbBright, 1 } }));
        add (1, 2, piano ("Upright Room", { { P::pianoType, 1 }, { P::reverbType, 0 }, { P::reverbAmount, 0.3f } }));
        add (1, 3, piano ("EP Delay Dream", { { P::pianoType, 2 }, { P::pianoTineModel, 1 }, { P::fx2On, 1 }, { P::fx2Type, 0 },
                                              { P::fx2Rate, 0.25f }, { P::fx2Amount, 0.5f }, { P::delayOn, 1 }, { P::delayDivision, 1 },
                                              { P::delayFeedback, 0.45f }, { P::delayMix, 0.35f }, { P::reverbType, 2 },
                                              { P::reverbAmount, 0.35f } }));
        {
            auto p = make ("Jazz Organ + Grand", { { P::pianoOn, 1 }, { P::pianoType, 0 }, { P::pianoVolume, 0.65f },
                                                   { P::organVolume, 0.6f }, { P::organVibOn, 1 }, { P::organVibMode, 5 },
                                                   { P::organPercOn, 1 }, { P::organPercSoft, 1 }, { P::reverbAmount, 0.25f } });
            drawbars (p, { 8, 8, 8, 0, 0, 0, 0, 0, 0 });
            add (1, 4, p);
        }

        // --- A:3  combo and pipe organs -----------------------------------------------------------
        {
            auto p = make ("Vox 60s", { { P::organModel, 1 }, { P::rotaryOn, 0 }, { P::fx1On, 1 }, { P::fx1Source, 0 },
                                        { P::fx1Type, 0 }, { P::fx1Rate, 0.45f }, { P::fx1Amount, 0.3f },
                                        { P::reverbType, 0 }, { P::reverbAmount, 0.3f } });
            drawbars (p, { 8, 8, 8, 8, 0, 0, 0, 0, 0 });
            add (2, 0, p);
        }
        {
            auto p = make ("Farf Garage", { { P::organModel, 2 }, { P::rotaryOn, 0 }, { P::ampOn, 1 }, { P::ampSource, 0 },
                                            { P::ampType, 2 }, { P::ampDrive, 0.5f }, { P::reverbType, 0 }, { P::reverbAmount, 0.2f } });
            drawbars (p, { 8, 8, 8, 8, 8, 0, 0, 0, 0 });
            add (2, 1, p);
        }
        {
            auto p = make ("Cathedral Pipe", { { P::organModel, 3 }, { P::rotaryOn, 0 }, { P::reverbType, 2 },
                                               { P::reverbAmount, 0.55f } });
            drawbars (p, { 8, 8, 8, 6, 0, 4, 0, 0, 0 });
            add (2, 2, p);
        }
        {
            auto p = make ("Electro B3 Jazz", { { P::mode, 1 }, { P::organPercOn, 1 }, { P::organPercSoft, 1 },
                                                { P::organVibOn, 1 }, { P::organVibMode, 5 }, { P::reverbAmount, 0.2f } });
            drawbars (p, { 8, 8, 8, 0, 0, 0, 0, 0, 0 });
            add (2, 3, p);
        }

        return list;
    }
}
