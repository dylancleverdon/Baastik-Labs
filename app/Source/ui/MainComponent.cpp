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

        // --- start
        add ({ "h-start", Row::Style::heading, "Start a session" });
        if (e.startStatus.empty())
            add ({ "start", Row::Style::primary, {}, {}, {}, false, false,
                   { { "Share a song...", [this] { chooseSong(); }, true } } });
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
                add ({ "join-" + p.peer, Row::Style::card, juce::String (p.song),
                       juce::String (p.name) + " is Player 1 " + theme::middot() + " " + juce::String (p.members) + (p.members == 1 ? " person" : " people"),
                       {}, false, false, { { "Join", [&act, peer] { act.join (peer); }, true } } });
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
    if (e.updateAvailable)
        add ({ "update", Row::Style::warning,
               juce::String (e.updateBy.empty() ? "Someone" : e.updateBy) + " shared changes that need an update",
               "Save in Live (Cmd+S / Ctrl+S) to get them: your own changes are merged in. Updating without saving drops "
               "your unsaved automation and plugin changes.", {}, false, false,
               { { "Update without saving", [&act] { act.updateNow(); } } } });
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
        add ({ "save-share", Row::Style::warning, "Save to share what you added",
               "You added " + what.joinIntoString (", ") + ". Plugins only reach the others when you save: press "
                   + saveKey + " in Live." });
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
               {}, false, false, { { "Show the file", [&act] { act.revealSong(); } } } });
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
        if (iAmP1 && ! m.isMe && m.info.online && ! e.handingOver)
        {
            const auto peer = m.info.peer;
            r.actions.push_back ({ "Make Player 1", [&act, peer] { act.handOver (peer); } });
        }
        add (r);
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
