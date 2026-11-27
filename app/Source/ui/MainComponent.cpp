#include "MainComponent.h"

#include "Pages.h"

using tutti::Session;

namespace
{
    constexpr int headerHeight = 54;
    constexpr int footerHeight = 46;

    juce::String barText (double bar) { return "bar " + juce::String ((int) std::floor (bar)); }
}

MainComponent::MainComponent (AppActions& a) : actions (a)
{
    viewport.setViewedComponent (&rows, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);

    settings = std::make_unique<SettingsPage> (actions, [this] { showPage (Page::main); });
    suggestions = std::make_unique<SuggestionPage> (actions, [this] { showPage (Page::main); });
    settingsView.setViewedComponent (settings.get(), false);
    settingsView.setScrollBarsShown (true, false);
    settingsView.setScrollBarThickness (8);
    addChildComponent (settingsView);
    addChildComponent (*suggestions);

    for (auto* b : { &suggestButton, &settingsButton })
    {
        b->setColour (juce::TextButton::buttonColourId, theme::card);
        addAndMakeVisible (b);
    }
    suggestButton.onClick = [this] { showPage (currentPage == Page::suggest ? Page::main : Page::suggest); };
    settingsButton.onClick = [this] { showPage (currentPage == Page::settings ? Page::main : Page::settings); };
    setWantsKeyboardFocus (true);
    setSize (440, 700);
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    // Cmd (Ctrl on Windows) + 1/2/3/, : jump between screens, like the View menu says.
    if (! key.getModifiers().isCommandDown())
        return false;
    switch (key.getKeyCode())
    {
        case '1': showPage (Page::main); return true;
        case '2': if (state.engine.role == Session::Role::player1 || state.engine.role == Session::Role::guest) showPage (Page::versions); return true;
        case '3': showPage (Page::suggest); return true;
        case ',': showPage (Page::settings); return true;
        default: return false;
    }
}

MainComponent::~MainComponent() = default;

void MainComponent::showPage (Page p)
{
    currentPage = p;
    viewport.setVisible (p == Page::main || p == Page::versions);
    settingsView.setVisible (p == Page::settings);
    suggestions->setVisible (p == Page::suggest);
    update (state);
}

void MainComponent::update (const AppState& s)
{
    state = s;
    if (currentPage == Page::main || currentPage == Page::versions)
    {
        rows.setRows (currentPage == Page::main ? buildRows (s) : buildVersionRows (s));
        rows.layout (viewport.getMaximumVisibleWidth());
    }
    settings->update (s);
    suggestions->update (s);
    repaint();
}

void MainComponent::chooseSong()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose the Live Set to share", juce::File(), "*.als");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) {
                              if (fc.getResult().existsAsFile())
                                  actions.startSession (fc.getResult());
                          });
}

namespace
{
    juce::String megabytes (int64_t bytes)
    {
        return juce::String ((double) bytes / (1024.0 * 1024.0), bytes < 10 * 1024 * 1024 ? 1 : 0) + " MB";
    }

    // "Downloading from Sam: 34 of 120 MB · 2.1 MB/s · about 40 s left", or why it's stuck.
    Row transferRow (const std::string& id, const tutti::EngineView::Transfer& t, bool sending)
    {
        Row r { id, Row::Style::card,
                juce::String (sending ? "Sending" : "Downloading") + (t.who.empty() ? juce::String() : juce::String (sending ? " to " : " from ") + juce::String (t.who)) };
        juce::String d = megabytes (t.done) + " of " + megabytes (t.total);
        if (t.stalled)
            d << "  " << theme::middot() << "  nothing for " << (int) t.quietFor << " s: Tutti is asking again"
              << (sending ? "" : " (check the other computer is on and connected)");
        else if (t.bytesPerSecond > 0)
        {
            d << "  " << theme::middot() << "  " << megabytes ((int64_t) t.bytesPerSecond) << "/s";
            if (t.secondsLeft >= 0)
                d << "  " << theme::middot() << "  about " << (t.secondsLeft < 90 ? juce::String ((int) t.secondsLeft + 1) + " s"
                                                                                  : juce::String ((int) (t.secondsLeft / 60) + 1) + " min")
                  << " left";
        }
        else
            d << "  " << theme::middot() << "  starting...";
        r.detail = d;
        r.progress = t.total > 0 ? (float) ((double) t.done / (double) t.total) : 0.0f;
        r.progressBad = t.stalled;
        return r;
    }
}

std::vector<Row> MainComponent::buildRows (const AppState& s)
{
    std::vector<Row> out;
    const auto& e = s.engine;
    auto& act = actions;
    auto add = [&] (Row r) { out.push_back (std::move (r)); };

    if (e.role == Session::Role::idle)
    {
        // --- setup
        if (! e.tailscaleRunning || ! e.liveConnected)
            add ({ "h-setup", Row::Style::heading, "Get set up" });
        if (! e.tailscaleInstalled && ! e.tailscaleRunning)
            add ({ "ts", Row::Style::warning, "Install Tailscale",
                   "Tutti reaches your friends through Tailscale, a free private network. Install it, sign in, and accept the invite from whoever set it up.",
                   {}, false, false, { { "Get Tailscale", [&act] { act.openUrl ("https://tailscale.com/download"); }, true } } });
        else if (! e.tailscaleRunning)
            add ({ "ts", Row::Style::warning, "Tailscale is off", "Open Tailscale and sign in, then your friends' sessions appear here." });
        else
            add ({ "ts", Row::Style::good, "Tailscale connected", juce::String (e.selfAddress) });

        if (! e.liveConnected)
            add ({ "live", Row::Style::warning, "Connect Ableton Live",
                   "In Live, open Settings > Link, Tempo & MIDI and pick Tutti as a Control Surface (leave Input and Output empty). If Tutti isn't in the list, restart Live once." });
        else if (e.liveNeedsRestart)
            add ({ "live", Row::Style::warning, "Restart Live",
                   "Tutti was updated. Quit Live and open it again so it loads the new Tutti." });
        else
            add ({ "live", Row::Style::good, "Live " + juce::String (e.liveVersion) + " connected", juce::String (e.liveStatus) });

        if (! e.copyOffer.empty())
            add ({ "copy", Row::Style::card, "Keep a complete copy of " + juce::String (e.copyOffer) + "?",
                   "The song and all its audio in one folder (Music/Tutti/Complete copies), ready to open in Live without Tutti.",
                   {}, false, false,
                   { { "No thanks", [&act] { act.dismissCopyOffer(); } },
                     { "Save a complete copy", [&act] { act.saveCompleteCopy(); }, true } } });

        // --- start
        add ({ "h-start", Row::Style::heading, "Start a session" });
        if (e.startStatus.empty())
        {
            std::vector<Row::Action> start { { "Share a song...", [this] { chooseSong(); }, true } };
            if (! e.continueSong.empty())
                start.insert (start.begin(), { "Continue " + juce::String (e.continueName), [&act] { act.continueLastSession(); } });
            add ({ "start", Row::Style::primary, {}, {}, {}, false, false, start });
        }
        else
            add ({ "start", Row::Style::card, juce::String (e.startStatus), "This can take a minute for big songs." });

        // --- join
        add ({ "h-join", Row::Style::heading, "Sessions on your network" });
        int hosting = 0;
        juce::StringArray online;
        for (const auto& p : e.peers)
        {
            if (p.hosting)
            {
                ++hosting;
                const auto peer = p.peer;
                const auto song = p.song;
                add ({ "join-" + p.peer, Row::Style::card, juce::String (p.song),
                       juce::String (p.name) + " is Player 1 " + theme::middot() + " " + juce::String (p.members) + (p.members == 1 ? " person" : " people"),
                       {}, false, false,
                       { { "Join with my copy...", [this, peer, song] { joinWithMyCopy (peer, song); } },
                         { "Join", [&act, peer] { act.join (peer); }, true } } });
            }
            else
                online.add (p.name);
        }
        if (hosting == 0)
            add ({ "nobody", Row::Style::text, "Nobody is hosting right now. When a friend starts a session, it shows up here." });
        if (! online.isEmpty())
            add ({ "online", Row::Style::text, "Also online: " + online.joinIntoString (", ") });
        return out;
    }

    if (e.role == Session::Role::joining)
    {
        add ({ "joining", Row::Style::card, "Waiting for Player 1 to let you in...", {}, {}, false, false,
               { { "Cancel", [&act] { act.leave(); } } } });
        return out;
    }
    if (e.download.active)
    {
        auto r = transferRow ("download", e.download, false);
        if (e.download.stalled && ! e.startStatus.empty() && e.download.quietFor > 60)
            r.actions.push_back ({ "Open with what's here", [&act] { act.openJoinedSongNow(); } });
        out.push_back (r);
    }
    if (e.upload.active)
        out.push_back (transferRow ("upload", e.upload, true));

    // --- in a session
    const bool iAmP1 = e.role == Session::Role::player1;
    std::string p1Name;
    for (const auto& m : e.members)
        if (m.isPlayer1)
            p1Name = m.isMe ? "you" : m.info.name;
    add ({ "song", Row::Style::heading, juce::String (e.session.song) });

    if (! e.liveConnected)
        add ({ "live", Row::Style::warning, "Live isn't connected", "Open Live, with Tutti chosen as a Control Surface." });
    else if (e.liveNeedsRestart)
        add ({ "live", Row::Style::warning, "Restart Live", "Tutti was updated. Quit Live and open it again so it loads the new Tutti." });
    else if (e.liveBound)
    {
        juce::String detail = "Player 1: " + juce::String (p1Name);
        if (e.previewsWaiting + e.filesWaiting > 0)
            detail << " " << theme::middot() << " getting " << (e.previewsWaiting + e.filesWaiting) << " audio file(s)";
        add ({ "live", Row::Style::good, "In sync", detail });
    }
    else
        add ({ "live", Row::Style::warning, juce::String (e.liveStatus),
               e.startStatus.empty() ? juce::String() : juce::String (e.startStatus), {}, false, false,
               e.liveStatus.find ("Open") != std::string::npos
                   ? std::vector<Row::Action> { { "Open the song", [&act] { act.openSong(); }, true } }
                   : std::vector<Row::Action> {} });

    // --- prompts
    auto nameOf = [&] (const std::string& peer) {
        for (const auto& m : e.members)
            if (m.info.peer == peer)
                return juce::String (m.info.name);
        for (const auto& p : e.peers)
            if (p.peer == peer)
                return juce::String (p.name);
        return juce::String ("Someone");
    };
    for (const auto& peer : e.joinRequests)
        add ({ "jr-" + peer, Row::Style::warning, nameOf (peer) + " wants to join", {}, {}, false, false,
               { { "Not now", [&act, peer] { act.approveJoin (peer, false); } },
                 { "Always let in", [&act, peer] { act.trustAndLetIn (peer); } },
                 { "Let in", [&act, peer] { act.approveJoin (peer, true); }, true } } });
    for (const auto& peer : e.p1Requests)
        add ({ "pr-" + peer, Row::Style::warning, nameOf (peer) + " asks to be Player 1",
               "Player 1's edits win when two people change the same thing.", {}, false, false,
               { { "Not now", [&act, peer] { act.answerPlayer1 (peer, false); } },
                 { "Allow", [&act, peer] { act.answerPlayer1 (peer, true); }, true } } });
    if (! e.playRequestBy.empty())
        add ({ "play-req", Row::Style::warning, juce::String (e.playRequestBy) + " wants to play together",
               "From " + barText (e.playRequestPos) + ". Your own playback stops and joins theirs.", {}, false, false,
               { { "Skip", [&act] { act.answerPlay (false); } }, { "Join", [&act] { act.answerPlay (true); }, true } } });
    if (e.canTakeOver)
        add ({ "takeover", Row::Style::warning, "Player 1 isn't responding",
               "If they're not coming back, you can take over so everyone keeps working.", {}, false, false,
               { { "Take over", [&act] { act.takeOver(); }, true } } });
   #if JUCE_MAC
    const juce::String key = "Cmd+S";
   #else
    const juce::String key = "Ctrl+S";
   #endif
    if (e.updateWhenStopped)
        add ({ "update", Row::Style::card, "An update is ready",
               "It loads as soon as you stop playing, so nothing is interrupted." });
    else if (e.updateAvailable)
    {
        const juce::String who (e.updateBy.empty() ? "Someone" : e.updateBy);
        if (! e.saveToShare.empty())
        {
            std::vector<Row::Action> actions { { "Get them without saving", [&act] { act.updateNow(); } } };
            if (e.canSaveForYou)
                actions.push_back ({ "Save & get them", [&act] { act.saveAndShare(); }, true });
            add ({ "update", Row::Style::warning, who + " saved changes",
                   "Save first (" + key + " in Live) so your plugin changes aren't lost: yours and theirs are combined.", {}, false, false,
                   actions });
        }
        else
            add ({ "update", Row::Style::warning, who + " saved changes",
                   "Get them now. If you changed automation or plugin settings since your last save, press " + key
                       + " in Live instead: your changes and theirs are combined.", {}, false, false,
                   { { "Get the changes", [&act] { act.updateNow(); }, true } } });
    }
    if (e.staleCount > 0)
        add ({ "stale", Row::Style::warning,
               juce::String (e.staleCount) + (e.staleCount == 1 ? " change needs an update" : " changes need an update"),
               "Some edits (automation, plugin sounds, groups...) can't be copied live. Save in Live (Cmd+S / Ctrl+S) to share yours." });

   #if JUCE_MAC
    const juce::String saveKey = "Cmd+S";
   #else
    const juce::String saveKey = "Ctrl+S";
   #endif
    if (! e.saveToShare.empty())
    {
        juce::StringArray what;
        for (const auto& w : e.saveToShare)
            what.add (juce::String (w));
        std::vector<Row::Action> saveNow;
        if (e.canSaveForYou)
            saveNow.push_back ({ "Save & share now", [&act] { act.saveAndShare(); }, true });
        add ({ "save-share", Row::Style::warning, "Not shared yet: " + what.joinIntoString (", "),
               "Plugins and their sounds only reach the others with a save. "
                   + juce::String (e.canSaveForYou && e.autoSave && ! e.saveNeedsPermission
                                       ? "Tutti saves for you once you stop tweaking, at most every "
                                             + (e.autoSaveSeconds < 60 ? juce::String (e.autoSaveSeconds) + " s" : juce::String (e.autoSaveSeconds / 60) + " min")
                                             + " (Settings), while Live is in front, on the session song and not playing."
                                       : "Press " + saveKey + " in Live."),
               {}, false, false, saveNow });
    }
    for (const auto& w : e.waitingForSave)
        add ({ "wait-" + w.byName + w.what, Row::Style::card, juce::String (w.byName) + " added " + juce::String (w.what),
               "It arrives when " + juce::String (w.byName) + " saves in Live. Then save (" + saveKey + ") or update to get it." });
    if (e.liveBound && ! e.songFile.empty())
    {
        const juce::File song (juce::String (e.songFile));
        add ({ "saving", Row::Style::card, "Saving",
               "Press " + saveKey + " in Live as usual: it saves Tutti's copy (" + song.getFileName() + " in Music/Tutti/"
                   + song.getParentDirectory().getFileName() + ") and Tutti shares it with everyone. Don't use Save As.",
               {}, false, false,
               e.canSaveForYou ? std::vector<Row::Action> { { "Show the file", [&act] { act.revealSong(); } },
                                                            { "Save & share now", [&act] { act.saveAndShare(); } } }
                               : std::vector<Row::Action> { { "Show the file", [&act] { act.revealSong(); } } } });
    }

    if (! e.pluginProblems.empty())
    {
        juce::StringArray lines;
        for (const auto& p : e.pluginProblems)
            lines.add (juce::String (p));
        add ({ "plugins", Row::Style::warning, "Plugins that won't load here",
               lines.joinIntoString ("\n") + "\nEveryone else still hears them; the Tutti Stand-in can share a recording of those tracks.",
               {}, false, false, { { "OK", [&act] { act.dismissPluginProblems(); } } } });
    }

    // --- people
    add ({ "h-people", Row::Style::heading, "In this session" });
    for (const auto& m : e.members)
    {
        Row r { "m-" + m.info.peer, Row::Style::member, juce::String (m.info.name) + (m.isMe ? " (you)" : "") };
        r.dot = theme::member (m.info.color);
        r.crown = m.isPlayer1;
        r.dim = ! m.info.online;
        if (! m.info.online)
            r.detail = "offline";
        else if (! m.trackName.empty())
            r.detail = "on " + juce::String (m.trackName) + " " + theme::middot() + " " + barText (m.bar)
                       + (m.recording ? "  " + theme::recDot() + " rec" : (m.playing ? "  " + theme::play() : juce::String()));
        else
            r.detail = "here";
        if (! m.isMe && m.info.online && ! m.trackName.empty())
        {
            const auto peer = m.info.peer;
            r.actions.push_back ({ "Go to", [&act, peer] { act.jumpTo (peer); } });
        }
        if (iAmP1 && ! m.isMe && m.info.online && ! e.handingOver)
        {
            const auto peer = m.info.peer;
            r.actions.push_back ({ "Make Player 1", [&act, peer] { act.handOver (peer); } });
        }
        add (r);
    }

    // --- messages
    add ({ "h-messages", Row::Style::heading, "Messages" });
    for (size_t i = 0; i < e.messages.size() && i < 5; ++i)
    {
        const auto& m = e.messages[i];
        std::vector<Row::Action> go;
        if (! m.track.empty())
        {
            const auto track = m.track;
            const auto time = m.time;
            go.push_back ({ "Go to", [&act, track, time] { act.jumpToSpot (track, time); } });
        }
        add ({ "msg-" + std::to_string (i), Row::Style::card, juce::String (m.who) + ": " + juce::String (m.text),
               juce::String (m.when), {}, false, false, go });
    }
    add ({ "msg-actions", Row::Style::quiet, {}, {}, {}, false, false,
           { { "Message...", [this] { askForMessage(); } }, { "Ping my spot", [&act] { act.pingMySpot(); } } } });

    if (! e.pluginTable.empty())
    {
        juce::StringArray lines;
        for (const auto& l : e.pluginTable)
            lines.add (juce::String (l));
        add ({ "plugin-table", Row::Style::card, "Plugins in this song", lines.joinIntoString ("\n") });
    }

    // --- recent activity
    if (! e.activity.empty())
    {
        add ({ "h-recent", Row::Style::heading, "Recent" });
        juce::StringArray lines;
        for (const auto& a : e.activity)
            lines.add (juce::String (a));
        add ({ "recent", Row::Style::text, lines.joinIntoString ("\n") });
    }

    // --- together
    std::vector<Row::Action> buttons { { "Play together", [&act] { act.playTogether(); }, true },
                                       { "Stop", [&act] { act.stopTogether(); } } };
    add ({ "play", Row::Style::primary, {}, {}, {}, false, false, buttons });
    if (! iAmP1 && ! e.updateAvailable)
        add ({ "refresh", Row::Style::quiet, {}, {}, {}, false, false,
               { { "Reload the latest saved version", [&act] { act.updateNow(); } } } });
    if (! iAmP1)
        add ({ "ask-p1", Row::Style::quiet, {}, {}, {}, false, false,
               { { e.askedForP1 ? "Asked to be Player 1..." : "Ask to be Player 1", [&act] { act.requestPlayer1(); } } } });

    // --- ideas
    if (! e.ideas.empty())
        add ({ "h-ideas", Row::Style::heading, "Ideas" });
    for (const auto& idea : e.ideas)
    {
        const auto track = idea.track;
        std::vector<Row::Action> ideaActions;
        if (iAmP1)
        {
            ideaActions.push_back ({ "Reject", [&act, track] { act.rejectIdea (track); } });
            ideaActions.push_back ({ "Accept", [&act, track] { act.acceptIdea (track); }, true });
        }
        else if (std::any_of (e.members.begin(), e.members.end(), [&] (const auto& m) { return m.isMe && m.info.peer == idea.by; }))
            ideaActions.push_back ({ "Discard", [&act, track] { act.rejectIdea (track); } });
        add ({ "idea-" + idea.track, Row::Style::card, juce::String (idea.byName) + "'s idea for " + juce::String (idea.origName),
               "Solo \"" + juce::String (idea.name) + "\" in Live to hear it. Player 1 decides.", {}, false, false, ideaActions });
    }
    if (e.undoSteps > 0)
        add ({ "undo", Row::Style::quiet, {}, {}, {}, false, false,
               { { "Undo my last change", [&act] { act.undoMine(); } } } });
    add ({ "copy-now", Row::Style::quiet, {}, {}, {}, false, false,
           { { "Save a complete copy", [&act] { act.saveCompleteCopy(); } } } });
    add ({ "make-idea", Row::Style::quiet, {}, {}, {}, false, false,
           { { "Try an idea on the selected track", [&act] { act.makeIdea(); } } } });
    add ({ "versions", Row::Style::quiet, {}, {}, {}, false, false,
           { { "Versions (" + juce::String ((int) e.versions.size()) + ")", [this] { showPage (Page::versions); } } } });
    add ({ "leave", Row::Style::quiet, {}, {}, {}, false, false, { { "Leave session", [&act] { act.leave(); } } } });
    return out;
}

std::vector<Row> MainComponent::buildVersionRows (const AppState& s)
{
    std::vector<Row> out;
    auto& act = actions;
    const auto& e = s.engine;
    const bool iAmP1 = e.role == Session::Role::player1;
    out.push_back ({ "h-versions", Row::Style::heading, "Versions of " + juce::String (e.session.song) });
    out.push_back ({ "about", Row::Style::text,
                     "Every save is kept, so nothing is ever lost. Open a copy to listen to an old version; "
                     "Player 1 can bring one back for everyone." });
    for (const auto& v : e.versions)
    {
        const auto id = v.id;
        Row r { "v-" + v.id, Row::Style::card,
                juce::String (v.when) + (v.label.empty() ? juce::String() : "  " + theme::middot() + "  " + juce::String (v.label)),
                juce::String (v.summary) + (v.current ? "  " + theme::middot() + "  in your Live now" : juce::String()) };
        r.actions.push_back ({ "Open a copy", [&act, id] { act.openVersionCopy (id); } });
        if (iAmP1 && ! v.current)
            r.actions.push_back ({ "Restore", [&act, id] { act.restoreVersion (id); } });
        out.push_back (r);
    }
    out.push_back ({ "label", Row::Style::quiet, {}, {}, {}, false, false,
                     { { "Name the latest version...", [this] { askForLabel(); } } } });
    out.push_back ({ "back", Row::Style::quiet, {}, {}, {}, false, false, { { "Back", [this] { showPage (Page::main); } } } });
    return out;
}

void MainComponent::joinWithMyCopy (const std::string& peer, const std::string& song)
{
    // "This is my copy of the same song": Tutti takes the audio from this project and keeps the
    // session's version in it as "<song> (Tutti).als" (the person's own .als is never touched).
    chooser = std::make_unique<juce::FileChooser> ("Choose your project folder for " + juce::String (song), juce::File());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this, peer, song] (const juce::FileChooser& fc) {
                              if (fc.getResult().isDirectory())
                                  actions.joinWithMyCopy (peer, song, fc.getResult());
                          });
}

void MainComponent::askForMessage()
{
    labelPrompt = std::make_unique<juce::AlertWindow> ("Message everyone", "", juce::MessageBoxIconType::NoIcon);
    labelPrompt->addTextEditor ("text", {}, {});
    labelPrompt->addButton ("Send", 1, juce::KeyPress (juce::KeyPress::returnKey));
    labelPrompt->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    labelPrompt->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result) {
        if (result == 1 && labelPrompt != nullptr)
        {
            const auto text = labelPrompt->getTextEditorContents ("text").trim();
            if (text.isNotEmpty())
                actions.sendMessage (text.toStdString());
        }
    }), false);
}

void MainComponent::askForLabel()
{
    labelPrompt = std::make_unique<juce::AlertWindow> ("Name this version", "For example \"Before the new chorus\".",
                                                       juce::MessageBoxIconType::NoIcon);
    labelPrompt->addTextEditor ("label", {}, {});
    labelPrompt->addButton ("Save name", 1, juce::KeyPress (juce::KeyPress::returnKey));
    labelPrompt->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    labelPrompt->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result) {
        if (result == 1 && labelPrompt != nullptr)
        {
            const auto text = labelPrompt->getTextEditorContents ("label").trim();
            if (text.isNotEmpty())
                actions.labelVersion (text.toStdString());
        }
    }), false);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    // Header: name and two status dots.
    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (18, 0);
    g.setColour (theme::text);
    g.setFont (theme::font (22.0f, true));
    g.drawText ("Tutti", header, juce::Justification::centredLeft);

    auto pill = [&] (juce::Rectangle<int> area, const juce::String& label, juce::Colour c) {
        g.setColour (c);
        g.fillEllipse (area.removeFromLeft (8).withSizeKeepingCentre (8, 8).toFloat());
        g.setColour (theme::dim);
        g.setFont (theme::font (12.0f));
        g.drawText (label, area.withTrimmedLeft (6), juce::Justification::centredLeft);
    };
    const auto& e = state.engine;
    auto right = header.removeFromRight (180);
    pill (right.removeFromLeft (95), "Tailscale", e.tailscaleRunning ? theme::good : theme::warn);
    pill (right, "Live", e.liveBound ? theme::good : (e.liveConnected ? theme::warn : theme::faint));

    // Footer: the latest notice.
    auto footer = getLocalBounds().removeFromBottom (footerHeight).reduced (18, 0);
    footer.removeFromRight (250);
    g.setColour (theme::dim);
    g.setFont (theme::font (12.0f));
    if (! e.notices.empty())
        g.drawFittedText (juce::String (e.notices.front()), footer, juce::Justification::centredLeft, 2);
    g.setColour (theme::card);
    g.drawHorizontalLine (headerHeight - 1, 0.0f, (float) getWidth());
    g.drawHorizontalLine (getHeight() - footerHeight, 0.0f, (float) getWidth());
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (headerHeight);
    auto footer = r.removeFromBottom (footerHeight).reduced (14, 8);
    settingsButton.setBounds (footer.removeFromRight (90));
    footer.removeFromRight (8);
    suggestButton.setBounds (footer.removeFromRight (140));
    auto body = r.reduced (14, 6);
    viewport.setBounds (body);
    settingsView.setBounds (body);
    settings->setSize (body.getWidth() - 10, juce::jmax (body.getHeight(), SettingsPage::contentHeight));
    suggestions->setBounds (body);
    rows.layout (viewport.getMaximumVisibleWidth());
}
