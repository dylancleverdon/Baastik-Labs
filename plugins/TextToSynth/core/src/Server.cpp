#include "tts/Server.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <random>
#include <sstream>

#include "serum2/Schema.h"
#include "serumgen/Generator.h"
#include "tts/Content.h"
#include "tts/Describe.h"
#include "tts/Edits.h"

namespace tts
{
namespace
{
constexpr const char* kAuthor = "Text To Synth · Baastik Labs";
constexpr const char* kLatestProtocol = "2025-06-18";

std::string u8(const fs::path& p)
{
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

std::string argString(const Json& args, const char* key, bool required = true)
{
    if (!args.contains(key) || args[key].is_null())
    {
        if (required)
            throw std::invalid_argument(std::string("missing \"") + key + "\"");
        return {};
    }
    if (!args[key].is_string())
        throw std::invalid_argument(std::string("\"") + key + "\" must be text");
    return args[key].get<std::string>();
}

Json textResult(const std::string& text, bool isError = false)
{
    return { { "content", Json::array({ { { "type", "text" }, { "text", text } } }) }, { "isError", isError } };
}

std::string bullets(const std::vector<std::string>& lines)
{
    std::string out;
    for (const auto& l : lines)
        out += "- " + l + "\n";
    return out;
}

std::string reportText(const EditReport& report)
{
    std::string out;
    if (!report.changes.empty())
        out += "Changes:\n" + bullets(report.changes);
    if (!report.warnings.empty())
        out += "Heads-up:\n" + bullets(report.warnings);
    return out;
}

std::string reloadHint(const fs::path& file)
{
    return "In Serum 2: open the preset browser → User → Text To Synth → \"" + u8(file.stem())
         + "\". After each edit, click the preset again to reload it.";
}

// Compact one-line-per-section parameter reference for the guide.
std::string parameterReference(const TargetResolver& resolver)
{
    std::ostringstream out;
    const auto reference = resolver.reference();
    for (const auto& [owner, entry] : reference.items())
    {
        out << owner;
        if (entry.contains("note"))
            out << " — " << entry["note"].get<std::string>();
        out << "\n";
        for (const auto& [name, p] : entry["params"].items())
        {
            out << "  " << name << ": ";
            const auto kind = p.value("kind", std::string());
            if (kind == "choice")
            {
                out << "one of ";
                std::string values;
                const auto options = p.value("values", Json::array());
                for (const auto& v : options)
                    values += (values.empty() ? "" : "|") + v.get<std::string>();
                out << values;
            }
            else if (kind == "on/off")
                out << "on/off";
            else
            {
                out << "number";
                if (p.contains("min") && p.contains("max"))
                    out << " " << formatValue(p["min"]) << " to " << formatValue(p["max"]);
            }
            if (p.contains("default") && !p["default"].is_null())
                out << ", default " << formatValue(p["default"]);
            if (p.value("modulatable", false))
                out << ", modulatable";
            out << "\n";
        }
    }
    return out.str();
}
} // namespace

McpServer::McpServer(PresetStore& store, std::string version, UpdateService* updates)
    : store_(store), version_(std::move(version)), updates_(updates)
{
}

std::string McpServer::instructions()
{
    return "Text To Synth (by Baastik Labs) designs Xfer Serum 2 presets from plain-English descriptions and edits them "
           "conversationally.\n"
           "1. Once per conversation, call sound_design_guide to learn the parameter names and the word → parameter "
           "vocabulary.\n"
           "2. To make a sound, call create_preset with a short name, the user's description, a starting point "
           "(a recipe close to the sound, or init) and edits that realise the description.\n"
           "3. For feedback like \"drier\", \"too harsh\" or \"more movement\", look at the current state (describe_preset, "
           "unless you just saw it) and call edit_preset with small relative edits from the vocabulary. Several small "
           "steps beat one big jump.\n"
           "4. restore_version undoes; list_versions shows the history.\n"
           "You can't hear the result: after each change, tell the user what you changed in musical terms, remind them to "
           "click the preset again in Serum's browser to reload it, and ask how it sounds.";
}

Json McpServer::tools()
{
    const Json presetArg = { { "type", "string" },
                             { "description", "Preset name (for presets Text To Synth made) or a full path to any .SerumPreset" } };
    const Json editsArg = {
        { "type", "array" },
        { "description",
          "Edits, applied in order; all succeed or none are saved. Each is one of: "
          "{\"target\":\"filter1.cutoff\",\"set\":0.4} | {\"target\":\"fx.reverb.mix\",\"add\":-10} | "
          "{\"target\":\"fx.reverb.mix\",\"scale\":0.5} | {\"add_fx\":\"reverb\",\"params\":{\"size\":60,\"mix\":25}} | "
          "{\"remove_fx\":\"distortion\"} | {\"mod\":{\"source\":\"lfo1\",\"target\":\"filter1.cutoff\",\"amount\":30}} | "
          "{\"unmod\":{\"source\":\"lfo1\",\"target\":\"filter1.cutoff\"}} | "
          "{\"wavetable\":{\"osc\":\"A\",\"table\":\"analog_basic\",\"frame\":\"saw\"}} | "
          "{\"lfo_shape\":{\"lfo\":1,\"shape\":\"sine\"}} | {\"rename\":\"New Name\"}. "
          "See sound_design_guide for every target." },
        { "items", { { "type", "object" } } },
    };
    const auto tool = [](const char* name, const char* title, const char* description, Json properties,
                         std::vector<std::string> required, bool readOnly) {
        Json schema = { { "type", "object" }, { "properties", std::move(properties) } };
        if (!required.empty())
            schema["required"] = required;
        return Json { { "name", name },
                      { "title", title },
                      { "description", description },
                      { "inputSchema", std::move(schema) },
                      { "annotations", { { "readOnlyHint", readOnly } } } };
    };

    Json list = Json::array();
    list.push_back(tool("sound_design_guide", "Sound design guide",
                        "Parameter names, ranges and the word-to-parameter vocabulary (darker, drier, less harsh, wider...). "
                        "Call once per conversation before creating or editing.",
                        { { "section",
                            { { "type", "string" },
                              { "enum", { "all", "vocabulary", "parameters" } },
                              { "description", "Default all" } } } },
                        {}, true));
    list.push_back(tool("list_starting_points", "List starting points",
                        "Recipes (reese, supersaw, 808, pads, plucks...), categories, genres and wavetables that "
                        "create_preset can start from.",
                        Json::object(), {}, true));
    list.push_back(tool(
        "create_preset", "Create preset",
        "Makes a new Serum 2 preset from a starting point plus edits and saves it to Serum's User/Text To Synth folder.",
        { { "name", { { "type", "string" }, { "description", "Preset name, e.g. \"BA Dark Reese\"" } } },
          { "description", { { "type", "string" }, { "description", "The user's description, stored in the preset" } } },
          { "start",
            { { "type", "object" },
              { "description",
                "Where to begin. {\"recipe\":\"reese\",\"genre\":\"dnb\"} uses the Baastik generator's recipe; "
                "{\"category\":\"pad\"} a category; {\"preset\":\"name or path\"} copies an existing preset; "
                "omit or {\"init\":true} for Serum's init patch. Optional \"seed\" makes generation repeatable." } } },
          { "edits", editsArg } },
        { "name" }, false));
    list.push_back(tool("describe_preset", "Describe preset",
                        "Readable summary of a preset: oscillators, filters, envelopes, LFO routes, FX chain and mix "
                        "levels, macros. Uses the same names edits accept.",
                        { { "preset", presetArg } }, { "preset" }, true));
    list.push_back(tool("edit_preset", "Edit preset",
                        "Applies edits and saves the next version. Presets outside the Text To Synth folder are never "
                        "changed; a copy is made there instead.",
                        { { "preset", presetArg },
                          { "edits", editsArg },
                          { "note", { { "type", "string" }, { "description", "What the user asked for, e.g. \"drier, less harsh\"" } } } },
                        { "preset", "edits" }, false));
    list.push_back(tool("list_versions", "List versions", "The version history of a Text To Synth preset.",
                        { { "preset", presetArg } }, { "preset" }, true));
    list.push_back(tool("restore_version", "Restore version",
                        "Undo: makes an earlier version current again (saved as a new version, so nothing is lost).",
                        { { "preset", presetArg },
                          { "version", { { "type", "integer" }, { "description", "Default: the version before the current one" } } } },
                        { "preset" }, false));
    list.push_back(tool("list_presets", "List presets", "Presets Text To Synth has made, with their version counts.",
                        Json::object(), {}, true));
    list.push_back(tool("check_for_updates", "Check for updates", "Checks whether a newer Text To Synth is available.",
                        Json::object(), {}, true));
    list.push_back(tool("install_update", "Install update",
                        "Downloads the newest Text To Synth installer and opens it. Only when the user asks to update.",
                        Json::object(), {}, false));
    return list;
}

// -------------------------------------------------------------- protocol

std::optional<Json> McpServer::handle(const Json& message)
{
    if (message.is_array())
    {
        Json responses = Json::array();
        for (const auto& m : message)
            if (auto r = handle(m))
                responses.push_back(std::move(*r));
        if (responses.empty())
            return std::nullopt;
        return responses;
    }
    if (!message.is_object() || !message.contains("method"))
    {
        if (message.is_object() && (message.contains("result") || message.contains("error")))
            return std::nullopt; // a response to something we never send
        return Json { { "jsonrpc", "2.0" }, { "id", nullptr }, { "error", { { "code", -32600 }, { "message", "Invalid request" } } } };
    }

    const auto method = message["method"].get<std::string>();
    const bool isNotification = !message.contains("id");
    const Json id = isNotification ? Json(nullptr) : message["id"];
    const Json params = message.value("params", Json::object());
    if (isNotification)
        return std::nullopt; // notifications/initialized, cancelled, ...

    const auto reply = [&](Json result) { return Json { { "jsonrpc", "2.0" }, { "id", id }, { "result", std::move(result) } }; };
    const auto fail = [&](int code, const std::string& text) {
        return Json { { "jsonrpc", "2.0" }, { "id", id }, { "error", { { "code", code }, { "message", text } } } };
    };

    if (method == "initialize")
    {
        auto requested = params.value("protocolVersion", std::string(kLatestProtocol));
        if (requested != "2024-11-05" && requested != "2025-03-26" && requested != "2025-06-18")
            requested = kLatestProtocol;
        return reply({ { "protocolVersion", requested },
                       { "capabilities", { { "tools", { { "listChanged", false } } } } },
                       { "serverInfo", { { "name", "text-to-synth" }, { "title", "Text To Synth by Baastik Labs" }, { "version", version_ } } },
                       { "instructions", instructions() } });
    }
    if (method == "ping")
        return reply(Json::object());
    if (method == "tools/list")
        return reply({ { "tools", tools() } });
    if (method == "tools/call")
    {
        if (!params.contains("name") || !params["name"].is_string())
            return fail(-32602, "tools/call needs a tool name");
        return reply(callTool(params["name"].get<std::string>(), params.value("arguments", Json::object())));
    }
    if (method == "resources/list")
        return reply({ { "resources", Json::array() } });
    if (method == "prompts/list")
        return reply({ { "prompts", Json::array() } });
    return fail(-32601, "Method not found: " + method);
}

void McpServer::run(std::istream& in, std::ostream& out)
{
    std::string line;
    while (std::getline(in, line))
    {
        if (line.find_first_not_of(" \t\r\n") == std::string::npos)
            continue;
        std::optional<Json> response;
        try
        {
            response = handle(Json::parse(line));
        }
        catch (const Json::parse_error&)
        {
            response = Json { { "jsonrpc", "2.0" }, { "id", nullptr }, { "error", { { "code", -32700 }, { "message", "Parse error" } } } };
        }
        catch (const std::exception& e)
        {
            response = Json { { "jsonrpc", "2.0" }, { "id", nullptr }, { "error", { { "code", -32603 }, { "message", e.what() } } } };
        }
        if (response)
        {
            out << response->dump(-1, ' ', false, Json::error_handler_t::replace) << "\n";
            out.flush();
        }
    }
}

Json McpServer::callTool(const std::string& name, const Json& args)
{
    std::string text;
    try
    {
        if (name == "sound_design_guide")
            text = guide(args);
        else if (name == "list_starting_points")
            text = startingPoints();
        else if (name == "create_preset")
            text = createPreset(args);
        else if (name == "describe_preset")
            text = describe(args);
        else if (name == "edit_preset")
            text = editPreset(args);
        else if (name == "list_versions")
            text = listVersions(args);
        else if (name == "restore_version")
            text = restoreVersion(args);
        else if (name == "list_presets")
            text = listPresets();
        else if (name == "check_for_updates")
            text = updates_ ? updates_->checkNow() : "This build of Text To Synth has no updater (developer build).";
        else if (name == "install_update")
            text = updates_ ? updates_->installUpdate() : "This build of Text To Synth has no updater (developer build).";
        else
            return textResult("Unknown tool \"" + name + "\"", true);
    }
    catch (const std::exception& e)
    {
        return textResult(std::string("Nothing was saved. ") + e.what(), true);
    }
    if (updates_ && name != "check_for_updates" && name != "install_update")
        if (const auto notice = updates_->notice(); !notice.empty())
            text += "\n\n[" + notice + "]";
    return textResult(text);
}

// ----------------------------------------------------------------- tools

std::string McpServer::guide(const Json& args)
{
    const auto section = args.value("section", std::string("all"));
    const auto& resolver = PresetEditor::defaultResolver();
    std::string out;
    if (section == "all" || section == "vocabulary")
    {
        const auto content = Content::load(Content::cacheFile(store_.paths().data));
        out += "# Vocabulary (content v" + std::to_string(content.value("version", 0)) + ")\n";
        Json vocab = content;
        vocab.erase("version");
        vocab.erase("minEngine");
        out += vocab.dump(1) + "\n\n";
    }
    if (section == "all" || section == "parameters")
    {
        out += "# Targets (owner.param). Numbers are in each parameter's own units.\n";
        out += parameterReference(resolver);
        std::string rates;
        for (const auto& [n, v] : resolver.schema().lfoSyncRates())
            rates += (rates.empty() ? "" : ", ") + n;
        std::string shapes;
        for (const auto& s : PresetEditor::lfoShapes())
            shapes += (shapes.empty() ? "" : ", ") + s;
        out += "\nLFO rates: set lfoN.rate to a synced value (" + rates + ") or lfoN.hz to free-running Hz.\n";
        out += "LFO shapes (lfoN.shape or lfo_shape edit): " + shapes + "\n";
        out += "Mod sources: lfo1-lfo10, env2-env4 (env1 is the amp envelope), macro1-macro8, velocity, modwheel, "
               "aftertouch, keytrack, random. Mod amount is -100..100 (% of the target's range).\n";
        out += "FX: add_fx appends to the main rack (rack 0) unless you give rack 1/2 (FX buses) or a position. "
               "A second unit of a type is fx.<type>2.\n";
    }
    return out;
}

std::string McpServer::startingPoints()
{
    const auto& content = serumgen::ContentLibrary::builtin();
    std::ostringstream out;
    out << "Recipes (start: {\"recipe\": id}):\n";
    for (const auto& id : content.recipeIds())
        out << "  " << id << " — " << content.recipe(id).value("name", id) << "\n";
    out << "\nCategories (start: {\"category\": id}): ";
    std::string cats;
    for (const auto& c : content.categories())
        cats += (cats.empty() ? "" : ", ") + c.id;
    out << cats << "\nGenres (add \"genre\": id to a recipe/category start): ";
    std::string genres;
    for (const auto& g : content.genres())
        genres += (genres.empty() ? "" : ", ") + g.id;
    out << genres << "\n\nWavetables ({\"wavetable\": {\"osc\": \"A\", \"table\": id, \"frame\": name}}):\n";
    for (const auto& wt : serum2::Schema::builtin().wavetables())
    {
        out << "  " << wt.id << " — " << wt.path;
        if (!wt.tags.empty())
        {
            std::string tags;
            for (const auto& t : wt.tags)
                tags += (tags.empty() ? "" : ", ") + t;
            out << " [" << tags << "]";
        }
        if (!wt.knownFrames.empty())
        {
            std::string frames;
            for (const auto& [f, pos] : wt.knownFrames)
                frames += (frames.empty() ? "" : ", ") + f;
            out << " frames: " << frames;
        }
        out << "\n";
    }
    out << "\nOr start from any existing preset: {\"preset\": \"name or path\"}.";
    return out.str();
}

std::string McpServer::createPreset(const Json& args)
{
    const auto name = argString(args, "name");
    const auto description = argString(args, "description", false);
    const Json start = args.value("start", Json::object());
    if (!start.is_object())
        throw std::invalid_argument("start must be an object, e.g. {\"recipe\": \"reese\"}");

    serum2::Preset preset;
    std::string origin = "Serum init patch";
    if (start.contains("preset"))
    {
        const auto file = store_.resolve(start["preset"].get<std::string>());
        preset = store_.load(file);
        origin = "a copy of \"" + presetNameOf(preset) + "\"";
    }
    else if (start.contains("recipe") || start.contains("category"))
    {
        const auto& content = serumgen::ContentLibrary::builtin();
        serumgen::Settings settings;
        settings.mode = serumgen::Mode::Guided;
        settings.chaos = start.value("chaos", 0.1);
        settings.author = kAuthor;
        if (start.contains("recipe"))
        {
            settings.recipe = start["recipe"].get<std::string>();
            if (content.recipe(settings.recipe).empty())
            {
                std::string ids;
                for (const auto& id : content.recipeIds())
                    ids += (ids.empty() ? "" : ", ") + id;
                throw std::invalid_argument("unknown recipe \"" + settings.recipe + "\"; recipes: " + ids);
            }
        }
        if (start.contains("category"))
        {
            settings.category = start["category"].get<std::string>();
            if (content.categoryData(settings.category).empty())
                throw std::invalid_argument("unknown category \"" + settings.category + "\"; see list_starting_points");
        }
        if (start.contains("genre"))
        {
            settings.genre = start["genre"].get<std::string>();
            if (content.genreData(settings.genre).empty())
                throw std::invalid_argument("unknown genre \"" + settings.genre + "\"; see list_starting_points");
        }
        settings.seed = start.contains("seed") ? start["seed"].get<std::uint64_t>() : std::random_device {}();
        auto result = serumgen::Generator(content).generate(settings);
        preset = std::move(result.preset);
        origin = "the " + (result.recipe.empty() ? result.category + " profile" : "\"" + result.recipe + "\" recipe")
               + (result.genre.empty() ? "" : " (" + result.genre + ")") + ", seed " + std::to_string(result.seed);
    }
    else
        preset = serum2::Schema::initPreset();

    setPresetName(preset, name);
    for (auto* j : { &preset.metadata, &preset.data })
    {
        (*j)["presetAuthor"] = kAuthor;
        (*j)["presetDescription"] = description.empty() ? std::string("Made with Text To Synth by Baastik Labs") : description;
        auto tags = j->value("tags", Json::array());
        if (!tags.is_array())
            tags = Json::array();
        if (std::find(tags.begin(), tags.end(), Json("Text To Synth")) == tags.end())
            tags.push_back("Text To Synth");
        (*j)["tags"] = tags;
    }

    EditReport report;
    if (args.contains("edits") && !args["edits"].is_null())
        report = PresetEditor(preset).apply(args["edits"]);

    const auto file = store_.create(preset, description.empty() ? "created" : "created: " + description);
    const auto saved = store_.load(file);
    std::string out = "Created \"" + presetNameOf(saved) + "\" (version 1) from " + origin + ".\nFile: " + u8(file) + "\n\n";
    out += reportText(report);
    out += "\nCurrent state:\n" + describePreset(saved, PresetEditor::defaultResolver());
    out += "\n" + reloadHint(file);
    return out;
}

std::string McpServer::describe(const Json& args)
{
    const auto file = store_.resolve(argString(args, "preset"));
    const auto preset = store_.load(file);
    std::string out = describePreset(preset, PresetEditor::defaultResolver());
    if (store_.owns(file))
    {
        const auto versions = store_.versions(file);
        if (!versions.empty())
            out += "\nVersion " + std::to_string(versions.back().version) + " of " + std::to_string(versions.size()) + ".";
    }
    else
        out += "\n(Not a Text To Synth preset: edits will save a copy in the Text To Synth folder.)";
    return out;
}

std::string McpServer::editPreset(const Json& args)
{
    auto file = store_.resolve(argString(args, "preset"));
    const auto note = args.value("note", std::string("edit"));
    if (!args.contains("edits"))
        throw std::invalid_argument("missing \"edits\"");

    const auto original = store_.load(file);
    auto edited = original;
    const auto report = PresetEditor(edited).apply(args["edits"]);

    std::string copied;
    if (!store_.owns(file))
    {
        // Never touch factory or third-party presets: work on a copy.
        auto copy = original;
        for (auto* j : { &copy.metadata, &copy.data })
            (*j)["presetAuthor"] = kAuthor;
        const auto source = file;
        file = store_.create(copy, "copied from " + u8(source));
        setPresetName(edited, presetNameOf(store_.load(file)));
        for (auto* j : { &edited.metadata, &edited.data })
            (*j)["presetAuthor"] = kAuthor;
        copied = "The original wasn't changed; this edit is saved as a copy.\n";
    }
    const int version = store_.commit(file, edited, note);
    std::string out = "Saved \"" + presetNameOf(edited) + "\" version " + std::to_string(version) + " (" + note + ").\n";
    out += copied + "File: " + u8(file) + "\n\n" + reportText(report);
    if (args.value("describe", false))
        out += "\nCurrent state:\n" + describePreset(store_.load(file), PresetEditor::defaultResolver());
    out += "\n" + reloadHint(file);
    return out;
}

std::string McpServer::listVersions(const Json& args)
{
    const auto file = store_.resolve(argString(args, "preset"));
    const auto versions = store_.versions(file);
    if (versions.empty())
        return "No history for \"" + u8(file.stem()) + "\" (not made by Text To Synth).";
    std::string out = "History of \"" + u8(file.stem()) + "\":\n";
    for (const auto& v : versions)
        out += "  v" + std::to_string(v.version) + "  " + v.time + "  " + v.note + (&v == &versions.back() ? "  ← current" : "") + "\n";
    return out;
}

std::string McpServer::restoreVersion(const Json& args)
{
    const auto file = store_.resolve(argString(args, "preset"));
    if (!store_.owns(file))
        throw std::invalid_argument("only Text To Synth presets have versions");
    const auto versions = store_.versions(file);
    if (versions.size() < 2 && !args.contains("version"))
        throw std::invalid_argument("there's no earlier version to go back to");
    const int target = args.contains("version") ? args["version"].get<int>() : versions[versions.size() - 2].version;
    const int now = store_.restore(file, target);
    return "Restored version " + std::to_string(target) + " of \"" + u8(file.stem()) + "\" (now saved as version "
         + std::to_string(now) + ").\n\n" + describePreset(store_.load(file), PresetEditor::defaultResolver()) + "\n"
         + reloadHint(file);
}

std::string McpServer::listPresets()
{
    const auto files = store_.list();
    std::string out = "Folder: " + u8(store_.paths().presets) + "\n";
    if (files.empty())
        return out + "No presets yet.";
    for (const auto& f : files)
    {
        const auto v = store_.versions(f);
        out += "  " + u8(f.stem()) + (v.empty() ? "" : "  (v" + std::to_string(v.back().version) + ")") + "\n";
    }
    return out;
}
} // namespace tts
