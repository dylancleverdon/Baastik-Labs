#include "SfzParser.h"

#include <array>
#include <map>

namespace norg::sfz
{
    namespace
    {
        using Opcodes = std::map<juce::String, juce::String>;

        juce::String stripComments (const juce::String& text)
        {
            juce::String out;
            out.preallocateBytes (text.getNumBytesAsUTF8());
            auto p = text.getCharPointer();
            bool inBlock = false;

            while (! p.isEmpty())
            {
                const auto c = *p;
                auto next = p;
                ++next;

                if (inBlock)
                {
                    if (c == '*' && *next == '/') { inBlock = false; p = next; ++p; continue; }
                    if (c == '\n') out << '\n';
                    ++p;
                    continue;
                }

                if (c == '/' && *next == '*') { inBlock = true; p = next; ++p; continue; }
                if (c == '/' && *next == '/')
                {
                    while (! p.isEmpty() && *p != '\n')
                        ++p;
                    continue;
                }

                out << juce::String::charToString (c);
                ++p;
            }
            return out;
        }

        // Expands #include and #define (with $variable substitution), line by line.
        void preprocess (const juce::String& text, const juce::File& baseDir, std::map<juce::String, juce::String>& defines,
                         juce::StringArray& linesOut, juce::StringArray& warnings, int depth)
        {
            juce::StringArray lines;
            lines.addLines (stripComments (text));

            const auto substitute = [&defines] (juce::String text)
            {
                // Longest names first, so $A doesn't eat part of $AB.
                if (text.containsChar ('$'))
                    for (auto it = defines.rbegin(); it != defines.rend(); ++it)
                        text = text.replace (it->first, it->second);
                return text;
            };

            for (auto line : lines)
            {
                // A #define's name must not be substituted (it may be redefining that very variable).
                if (line.trimStart().startsWith ("#define"))
                {
                    const auto rest = line.trim().substring (7).trim();
                    const auto name = rest.upToFirstOccurrenceOf (" ", false, false).trim();
                    const auto value = substitute (rest.fromFirstOccurrenceOf (" ", false, false).trim());
                    if (name.startsWithChar ('$'))
                        defines[name] = value;
                    continue;
                }

                line = substitute (line);

                // #include can sit anywhere in a line: split the line around it.
                if (const int inc = line.indexOf ("#include"); inc >= 0)
                {
                    const auto before = line.substring (0, inc);
                    const int open = line.indexOfChar (inc, '"');
                    const int close = open >= 0 ? line.indexOfChar (open + 1, '"') : -1;
                    if (close > open && open >= 0)
                    {
                        const auto name = line.substring (open + 1, close);
                        const auto after = line.substring (close + 1);
                        if (before.trim().isNotEmpty())
                            linesOut.add (before);

                        const auto file = baseDir.getChildFile (name.replaceCharacter ('\\', '/'));
                        if (depth < 8 && file.existsAsFile())
                            preprocess (file.loadFileAsString(), baseDir, defines, linesOut, warnings, depth + 1);
                        else
                            warnings.add ("could not include " + name);

                        if (after.trim().isNotEmpty())
                            preprocess (after, baseDir, defines, linesOut, warnings, depth);
                        continue;
                    }
                }

                linesOut.add (line);
            }
        }

        float toFloat (const juce::String& v) { return v.trim().getFloatValue(); }
        juce::int64 toInt (const juce::String& v) { return v.trim().getLargeIntValue(); }

        struct Control
        {
            juce::String defaultPath;
            int noteOffset = 0;
            int octaveOffset = 0;
            std::array<float, 128> cc {}; // default controller values (set_ccN / set_hdccN), 0..127
        };

        // Returns the key, -1 for "no key" (an explicit -1 in the file), or -2 if unreadable.
        int keyValue (const juce::String& v, const Control& control)
        {
            if (v.trim() == "-1")
                return -1;
            const int n = parseNoteName (v);
            return n < 0 ? -2 : n + control.noteOffset + 12 * control.octaveOffset;
        }

        void apply (Region& r, const juce::String& name, const juce::String& value, const Control& control,
                    const juce::File& baseDir, juce::StringArray& warnings)
        {
            if (name == "sample")
            {
                auto rel = (control.defaultPath + value.trim()).replaceCharacter ('\\', '/');
                r.sample = juce::File::isAbsolutePath (rel) ? rel : baseDir.getChildFile (rel).getFullPathName();
            }
            else if (name == "lokey")            { if (const int k = keyValue (value, control); k >= -1) r.loKey = k; }
            else if (name == "hikey")            { if (const int k = keyValue (value, control); k >= -1) r.hiKey = k; }
            else if (name == "key")
            {
                if (const int k = keyValue (value, control); k >= -1)
                    r.loKey = r.hiKey = r.keyCenter = k;
            }
            else if (name == "sw_last")          r.swLast = keyValue (value, control);
            else if (name == "lorand")           r.loRand = toFloat (value);
            else if (name == "hirand")           r.hiRand = toFloat (value);
            else if (name == "note_polyphony")   r.notePolyphony = static_cast<int> (toInt (value));
            else if (name == "off_time")         r.offTime = juce::jmax (0.001f, toFloat (value));
            else if (name == "pitch_keycenter")  { if (const int k = keyValue (value, control); k >= 0) r.keyCenter = k; }
            else if (name == "lovel")            r.loVel = static_cast<int> (toInt (value));
            else if (name == "hivel")            r.hiVel = static_cast<int> (toInt (value));
            else if (name == "tune" || name == "pitch") r.tuneCents = toFloat (value);
            else if (name == "transpose")        r.transpose = toFloat (value);
            else if (name == "volume" || name == "gain") r.volumeDb = toFloat (value);
            else if (name == "amplitude")        r.volumeDb += 20.0f * std::log10 (juce::jmax (0.0001f, toFloat (value) / 100.0f));
            else if (name == "pan")              r.pan = toFloat (value);
            else if (name == "amp_veltrack")     r.ampVeltrack = toFloat (value);
            else if (name == "ampeg_attack")     r.ampegAttack = toFloat (value);
            else if (name == "ampeg_release")    r.ampegRelease = toFloat (value);
            else if (name == "rt_decay")         r.rtDecay = toFloat (value);
            else if (name == "offset")           r.offset = toInt (value);
            else if (name == "group")            r.group = toInt (value);
            else if (name == "off_by")           r.offBy = toInt (value);
            else if (name == "loop_start" || name == "loopstart") r.loopStart = toInt (value);
            else if (name == "loop_end" || name == "loopend")     r.loopEnd = toInt (value);
            else if (name == "trigger")
            {
                const auto v = value.trim().toLowerCase();
                r.trigger = v == "release" ? Trigger::release : v == "first" ? Trigger::first
                          : v == "legato" ? Trigger::legato : Trigger::attack;
            }
            else if (name == "loop_mode" || name == "loopmode")
            {
                const auto v = value.trim().toLowerCase();
                r.loopMode = v == "one_shot" ? LoopMode::oneShot : v == "loop_continuous" ? LoopMode::continuous
                           : v == "loop_sustain" ? LoopMode::sustain : LoopMode::none;
            }
            else
            {
                // Plenty of opcodes (filters, LFOs, curves...) don't matter for a piano player.
                juce::ignoreUnused (warnings);
            }
        }
    }

    int parseNoteName (const juce::String& text)
    {
        const auto t = text.trim().toLowerCase();
        if (t.isEmpty())
            return -1;

        if (t.containsOnly ("0123456789-"))
            return juce::jlimit (-1, 127, t.getIntValue());

        static const int offsets[] = { 9, 11, 0, 2, 4, 5, 7 }; // a b c d e f g
        const auto letter = t[0];
        if (letter < 'a' || letter > 'g')
            return -1;

        int note = offsets[letter - 'a'];
        int i = 1;
        if (t[i] == '#')      { ++note; ++i; }
        else if (t[i] == 'b') { --note; ++i; }

        const auto octaveText = t.substring (i);
        if (! octaveText.containsOnly ("0123456789-") || octaveText.isEmpty())
            return -1;

        const int midi = (octaveText.getIntValue() + 1) * 12 + note; // c4 = 60
        return midi >= 0 && midi <= 127 ? midi : -1;
    }

    ParseResult parse (const juce::String& text, const juce::File& sfzFile)
    {
        ParseResult result;
        const auto baseDir = sfzFile.getParentDirectory();

        std::map<juce::String, juce::String> defines;
        juce::StringArray lines;
        preprocess (text, baseDir, defines, lines, result.warnings, 0);

        Control control;
        Opcodes global, master, group, region;
        enum class Level { none, controlHeader, globalHeader, masterHeader, groupHeader, regionHeader } level = Level::none;
        bool regionOpen = false;

        const auto finishRegion = [&]
        {
            if (! regionOpen)
                return;
            regionOpen = false;

            // Inner scopes override outer ones.
            Opcodes merged;
            for (const auto* scope : { &global, &master, &group, &region })
                for (const auto& [name, value] : *scope)
                    merged[name] = value;
            region.clear();

            // Keyswitch setup can live in any scope.
            for (const auto& [name, value] : merged)
            {
                if (name == "sw_lokey")        result.swLoKey = keyValue (value, control);
                else if (name == "sw_hikey")   result.swHiKey = keyValue (value, control);
                else if (name == "sw_default") result.swDefault = keyValue (value, control);
            }

            // Controller conditions (loccN / hiccN), judged against the instrument's default CC values:
            // Norg doesn't send those controllers, so regions that need other values never play.
            for (const auto& [name, value] : merged)
            {
                const bool lo = name.startsWith ("locc"), hi = name.startsWith ("hicc");
                if ((lo || hi) && name.substring (4).containsOnly ("0123456789"))
                {
                    const int cc = juce::jlimit (0, 127, name.substring (4).getIntValue());
                    const float v = control.cc[static_cast<size_t> (cc)];
                    if ((lo && v < toFloat (value)) || (hi && v > toFloat (value)))
                        return;
                }
            }

            Region r;
            for (const auto& [name, value] : merged)
                if (! name.contains ("_oncc") && ! name.contains ("_curvecc"))
                    apply (r, name, value, control, baseDir, result.warnings);

            // Opcodes modulated by a controller (X_onccN), at that controller's default value.
            for (const auto& [name, value] : merged)
            {
                const int at = name.indexOf ("_oncc");
                if (at <= 0 || ! name.substring (at + 5).containsOnly ("0123456789"))
                    continue;
                const auto target = name.substring (0, at);
                const float amount = toFloat (value) * control.cc[static_cast<size_t> (juce::jlimit (0, 127, name.substring (at + 5).getIntValue()))] / 127.0f;
                if (target == "amp_veltrack")       r.ampVeltrack += amount;
                else if (target == "ampeg_release") r.ampegRelease += amount;
                else if (target == "ampeg_attack")  r.ampegAttack += amount;
                else if (target == "volume")        r.volumeDb += amount;
                else if (target == "pan")           r.pan += amount;
                else if (target == "offset")        r.offset += static_cast<juce::int64> (amount);
                else if (target == "amplitude" && name.substring (at + 5).getIntValue() != 7) // CC7 is the host's volume
                    r.volumeDb += 20.0f * std::log10 (juce::jmax (0.0001f, amount / 100.0f));
            }

            if (r.sample.isEmpty())
                result.warnings.add ("region without a sample ignored");
            else
                result.regions.push_back (r);
        };

        juce::String pendingName, pendingValue;
        const auto flushOpcode = [&]
        {
            if (pendingName.isEmpty())
                return;

            switch (level)
            {
                case Level::controlHeader:
                    if (pendingName == "default_path")
                    {
                        control.defaultPath = pendingValue.trim().replaceCharacter ('\\', '/');
                        if (control.defaultPath.isNotEmpty() && ! control.defaultPath.endsWithChar ('/'))
                            control.defaultPath << '/';
                    }
                    else if (pendingName.startsWith ("set_hdcc"))
                        control.cc[static_cast<size_t> (juce::jlimit (0, 127, pendingName.substring (8).getIntValue()))] = 127.0f * toFloat (pendingValue);
                    else if (pendingName.startsWith ("set_cc"))
                        control.cc[static_cast<size_t> (juce::jlimit (0, 127, pendingName.substring (6).getIntValue()))] = toFloat (pendingValue);
                    else if (pendingName == "note_offset")   control.noteOffset = pendingValue.getIntValue();
                    else if (pendingName == "octave_offset") control.octaveOffset = pendingValue.getIntValue();
                    break;
                case Level::globalHeader: global[pendingName] = pendingValue; break;
                case Level::masterHeader: master[pendingName] = pendingValue; break;
                case Level::groupHeader:  group[pendingName] = pendingValue; break;
                case Level::regionHeader: region[pendingName] = pendingValue; break;
                case Level::none:   break;
            }
            pendingName.clear();
            pendingValue.clear();
        };

        for (const auto& line : lines)
        {
            // Split the line into headers and whitespace-separated words.
            int pos = 0;
            const int len = line.length();
            while (pos < len)
            {
                if (line[pos] == '<')
                {
                    const int close = line.indexOfChar (pos, '>');
                    if (close < 0)
                        break;

                    flushOpcode();
                    const auto header = line.substring (pos + 1, close).trim().toLowerCase();
                    finishRegion();

                    if (header == "control")     level = Level::controlHeader;
                    else if (header == "global") { level = Level::globalHeader; global.clear(); master.clear(); group.clear(); }
                    else if (header == "master") { level = Level::masterHeader; master.clear(); group.clear(); }
                    else if (header == "group")  { level = Level::groupHeader; group.clear(); }
                    else if (header == "region") { level = Level::regionHeader; regionOpen = true; }
                    else                         level = Level::none; // <curve>, <effect>, <midi>...

                    pos = close + 1;
                    continue;
                }

                if (juce::CharacterFunctions::isWhitespace (line[pos]))
                {
                    ++pos;
                    continue;
                }

                int end = pos;
                while (end < len && ! juce::CharacterFunctions::isWhitespace (line[end]) && line[end] != '<')
                    ++end;
                const auto word = line.substring (pos, end);
                pos = end;

                const int eq = word.indexOfChar ('=');
                if (eq > 0)
                {
                    flushOpcode();
                    pendingName = word.substring (0, eq).toLowerCase();
                    pendingValue = word.substring (eq + 1);
                }
                else if (pendingName.isNotEmpty())
                {
                    pendingValue << " " << word; // sample names with spaces in them
                }
            }

            // An opcode's value never continues onto the next line.
            flushOpcode();
        }

        flushOpcode();
        finishRegion();
        return result;
    }

    ParseResult parseFile (const juce::File& sfzFile)
    {
        return parse (sfzFile.loadFileAsString(), sfzFile);
    }
}
