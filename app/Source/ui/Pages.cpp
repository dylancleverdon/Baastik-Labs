#include "Pages.h"

namespace
{
    void heading (juce::Graphics& g, const juce::String& text, int y, int width)
    {
        g.setColour (theme::dim);
        g.setFont (theme::font (13.0f, true));
        g.drawText (text.toUpperCase(), 2, y, width, 18, juce::Justification::centredLeft);
    }

    void styleLabel (juce::Label& l, float size, juce::Colour c)
    {
        l.setFont (theme::font (size));
        l.setColour (juce::Label::textColourId, c);
        l.setJustificationType (juce::Justification::topLeft);
        l.setMinimumHorizontalScale (1.0f);
    }

    void radio (juce::TextButton& b, bool on)
    {
        b.setColour (juce::TextButton::buttonColourId, on ? theme::accent : theme::cardHi);
    }
}

// ================================================================== settings

SettingsPage::SettingsPage (AppActions& a, std::function<void()> onDone) : actions (a), done (std::move (onDone))
{
    name.setFont (theme::font (15.0f));
    name.setTextToShowWhenEmpty ("Your name", theme::faint);
    name.onReturnKey = [this] { actions.setIdentity (name.getText().trim().toStdString(), colour); };
    name.onFocusLost = name.onReturnKey;
    addAndMakeVisible (name);

    ask.onClick = [this] { actions.setPlayMode ("ask"); };
    automatic.onClick = [this] { actions.setPlayMode ("auto"); };
    never.onClick = [this] { actions.setPlayMode ("never"); };
    for (auto* b : { &ask, &automatic, &never, &checkNow, &rollBack, &logs, &back, &chooseBackup })
        addAndMakeVisible (b);

    chooseBackup.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser> ("Where should Tutti keep a copy of every version?");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc) {
                                  if (fc.getResult().isDirectory())
                                      actions.setBackupFolder (fc.getResult());
                              });
    };

    autoUpdate.onClick = [this] { actions.setAutoUpdate (autoUpdate.getToggleState()); };
    autoSave.onClick = [this] { actions.setAutoSave (autoSave.getToggleState()); };
    autoGet.onClick = [this] { actions.setAutoGet (autoGet.getToggleState()); };
    forgetTrusted.onClick = [this] { actions.forgetTrusted(); };
    addAndMakeVisible (autoGet);
    addChildComponent (forgetTrusted);
    styleLabel (trustedInfo, 13.0f, theme::dim);
    addAndMakeVisible (trustedInfo);
    allowSave.onClick = [this] { actions.allowSaving(); };
    addAndMakeVisible (autoSave);
    addChildComponent (allowSave);
    startAtLogin.onClick = [this] { actions.setStartAtLogin (startAtLogin.getToggleState()); };
    addAndMakeVisible (autoUpdate);
    addAndMakeVisible (startAtLogin);

    checkNow.onClick = [this] { actions.checkForUpdates(); };
    rollBack.onClick = [this] { actions.rollBack(); };
    logs.onClick = [this] { actions.showLogs(); };
    back.onClick = [this] {
        actions.setIdentity (name.getText().trim().toStdString(), colour);
        if (done)
            done();
    };
    back.setColour (juce::TextButton::buttonColourId, theme::accent);

    styleLabel (updateStatus, 14.0f, theme::text);
    styleLabel (notes, 13.0f, theme::dim);
    styleLabel (scriptInfo, 13.0f, theme::dim);
    styleLabel (backupInfo, 13.0f, theme::dim);
    addAndMakeVisible (backupInfo);
    addAndMakeVisible (updateStatus);
    addAndMakeVisible (notes);
    addAndMakeVisible (scriptInfo);
}

void SettingsPage::update (const AppState& s)
{
    state = s;
    if (! name.hasKeyboardFocus (true))
        name.setText (juce::String (s.engine.myName), false);
    colour = s.engine.myColor;
    radio (ask, s.engine.playTogetherMode == "ask");
    radio (automatic, s.engine.playTogetherMode == "auto");
    radio (never, s.engine.playTogetherMode == "never");
    autoUpdate.setToggleState (s.updates.autoUpdate, juce::dontSendNotification);
    startAtLogin.setToggleState (s.setup.startAtLogin, juce::dontSendNotification);
    autoSave.setToggleState (s.engine.autoSave, juce::dontSendNotification);
    autoGet.setToggleState (s.engine.autoGet, juce::dontSendNotification);
    {
        juce::StringArray names;
        for (const auto& n : s.engine.trusted)
            names.add (juce::String (n));
        trustedInfo.setText (names.isEmpty() ? juce::String ("Nobody is trusted yet: use \"Always let in\" when someone asks to join.")
                                             : "Let in without asking: " + names.joinIntoString (", "),
                             juce::dontSendNotification);
        forgetTrusted.setVisible (! names.isEmpty());
    }
    autoSave.setVisible (s.engine.canSaveForYou);
    allowSave.setVisible (s.engine.canSaveForYou && s.engine.saveNeedsPermission);
    rollBack.setEnabled (s.updates.canRollBack && ! s.updates.busy);
    checkNow.setEnabled (! s.updates.busy);

    juce::String status = "Tutti " + juce::String (s.updates.installedVersion);
    if (! s.updates.status.empty())
        status << " " << theme::middot() << " " << juce::String (s.updates.status);
    updateStatus.setText (status, juce::dontSendNotification);
    juce::String n;
    if (! s.updates.availableVersion.empty() && s.updates.availableVersion != s.updates.installedVersion)
        n << "New in " << juce::String (s.updates.availableVersion) << ": " << juce::String (s.updates.availableNotes);
    else if (! s.updates.installedNotes.empty())
        n << "What's new: " << juce::String (s.updates.installedNotes);
    notes.setText (n, juce::dontSendNotification);
    scriptInfo.setText (s.setup.scriptInstalledIn.isEmpty()
                            ? juce::String ("The Tutti Remote Script couldn't be installed into Live's User Library. Check that Live has been opened once on this computer.")
                            : "Remote Script installed in " + s.setup.scriptInstalledIn.joinIntoString (", "),
                        juce::dontSendNotification);
    backupInfo.setText (s.engine.backupFolder.empty()
                            ? juce::String ("Optional. Everyone in a session already keeps the song, every version and its audio on their own computer. "
                                            "For an extra copy, choose a folder such as an external drive: while you're Player 1, every version goes there too.")
                            : "While you're Player 1, every version, with its audio, is also copied to " + juce::String (s.engine.backupFolder) + ".",
                        juce::dontSendNotification);
    repaint();
}

juce::Rectangle<int> SettingsPage::swatchArea (int index) const
{
    return { 6 + index * 34, nameTop + 66, 26, 26 };
}

void SettingsPage::mouseUp (const juce::MouseEvent& e)
{
    for (int i = 0; i < 8; ++i)
        if (swatchArea (i).expanded (3).contains (e.getPosition()))
        {
            colour = i;
            actions.setIdentity (name.getText().trim().toStdString(), colour);
            repaint();
        }
}

void SettingsPage::paint (juce::Graphics& g)
{
    heading (g, "You", nameTop, getWidth());
    for (int i = 0; i < 8; ++i)
    {
        auto r = swatchArea (i).toFloat();
        g.setColour (theme::member (i));
        g.fillEllipse (r);
        if (i == colour)
        {
            g.setColour (theme::text);
            g.drawEllipse (r.expanded (3.0f), 2.0f);
        }
    }
    heading (g, "When someone presses Play together", playTop, getWidth());
    heading (g, "Updates", updatesTop, getWidth());
    heading (g, "Backups", backupTop, getWidth());
    heading (g, "This computer", setupTop, getWidth());
}

void SettingsPage::resized()
{
    const int w = getWidth();
    int y = 4;
    nameTop = y;
    name.setBounds (0, y + 24, w, 32);
    y += 24 + 32 + 50;

    playTop = y;
    auto row = juce::Rectangle<int> (0, y + 24, w, 30);
    const int third = w / 3;
    ask.setBounds (row.removeFromLeft (third).reduced (2, 0));
    automatic.setBounds (row.removeFromLeft (third).reduced (2, 0));
    never.setBounds (row.reduced (2, 0));
    y += 24 + 30 + 18;

    updatesTop = y;
    updateStatus.setBounds (0, y + 22, w, 22);
    notes.setBounds (0, y + 44, w, 36);
    autoUpdate.setBounds (0, y + 82, w, 26);
    auto buttons = juce::Rectangle<int> (0, y + 112, w, 30);
    checkNow.setBounds (buttons.removeFromLeft (120));
    buttons.removeFromLeft (8);
    rollBack.setBounds (buttons.removeFromLeft (110));
    y += 112 + 30 + 18;

    backupTop = y;
    backupInfo.setBounds (0, y + 22, w, 52);
    chooseBackup.setBounds (0, y + 78, 150, 30);
    y += 78 + 30 + 18;

    setupTop = y;
    startAtLogin.setBounds (0, y + 22, w, 26);
    autoSave.setBounds (0, y + 50, w, 26);
    autoGet.setBounds (0, y + 78, w, 26);
    trustedInfo.setBounds (0, y + 108, w - 180, 36);
    forgetTrusted.setBounds (w - 170, y + 108, 170, 30);
    scriptInfo.setBounds (0, y + 150, w, 36);
    logs.setBounds (0, y + 190, 120, 30);
    allowSave.setBounds (128, y + 190, 210, 30);

    back.setBounds (w - 100, getHeight() - 34, 100, 32);
}

// ================================================================== suggestion box

SuggestionPage::SuggestionPage (AppActions& a, std::function<void()> onDone) : actions (a), done (std::move (onDone))
{
    auto pick = [this] (const std::string& k) {
        kind = k;
        radio (idea, kind == "Idea");
        radio (problem, kind == "Problem");
        radio (other, kind == "Other");
    };
    idea.onClick = [pick] { pick ("Idea"); };
    problem.onClick = [pick] { pick ("Problem"); };
    other.onClick = [pick] { pick ("Other"); };
    pick ("Idea");

    text.setMultiLine (true, true);
    text.setReturnKeyStartsNewLine (true);
    text.setFont (theme::font (15.0f));
    text.setTextToShowWhenEmpty ("What would make Tutti better? What went wrong?", theme::faint);

    send.setColour (juce::TextButton::buttonColourId, theme::accent);
    send.onClick = [this] {
        const auto t = text.getText().trim();
        if (t.isEmpty())
            return;
        actions.sendSuggestion (kind, t.toStdString(), false);
        text.clear();
    };
    // Logs: what Tutti and Live wrote down recently, so whoever fixes Tutti can see what happened.
    sendLogs.setTooltip ("Sends what you wrote (if anything) with Tutti's and Live's recent logs.");
    sendLogs.onClick = [this] {
        auto t = text.getText().trim();
        actions.sendSuggestion (t.isEmpty() ? std::string ("Problem") : kind,
                                (t.isEmpty() ? juce::String ("Logs") : t).toStdString(), true);
        text.clear();
    };
    back.onClick = [this] { if (done) done(); };
    styleLabel (status, 13.0f, theme::dim);
    for (juce::Component* c : { (juce::Component*) &idea, (juce::Component*) &problem, (juce::Component*) &other,
                                (juce::Component*) &text, (juce::Component*) &send, (juce::Component*) &sendLogs, (juce::Component*) &back,
                                (juce::Component*) &status })
        addAndMakeVisible (c);
}

void SuggestionPage::update (const AppState& s)
{
    juce::String st;
    if (! s.suggestions.lastResult.empty())
        st << juce::String (s.suggestions.lastResult);
    if (s.suggestions.queued > 0)
        st << (st.isEmpty() ? "" : " ") << s.suggestions.queued << " waiting to send"
           << (s.suggestions.endpointKnown ? "." : " (the suggestion sheet isn't connected yet; they'll go as soon as it is).");
    status.setText (st, juce::dontSendNotification);
}

void SuggestionPage::paint (juce::Graphics& g)
{
    heading (g, "Suggestion box", 4, getWidth());
    g.setColour (theme::dim);
    g.setFont (theme::font (13.0f));
    g.drawFittedText ("Ideas, problems, anything. It goes straight to the team's suggestions sheet, with your name and your Tutti and Live versions. Something went wrong? Click Send logs so it can be fixed.",
                      0, 26, getWidth(), 40, juce::Justification::topLeft, 3);
}

void SuggestionPage::resized()
{
    const int w = getWidth();
    auto row = juce::Rectangle<int> (0, 74, w, 30);
    const int third = w / 3;
    idea.setBounds (row.removeFromLeft (third).reduced (2, 0));
    problem.setBounds (row.removeFromLeft (third).reduced (2, 0));
    other.setBounds (row.reduced (2, 0));
    text.setBounds (0, 114, w, juce::jmax (120, getHeight() - 114 - 100));
    send.setBounds (w - 90, text.getBottom() + 10, 90, 32);
    sendLogs.setBounds (send.getX() - 118, text.getBottom() + 10, 110, 32);
    status.setBounds (0, text.getBottom() + 10, sendLogs.getX() - 10, 60);
    back.setBounds (0, getHeight() - 34, 90, 32);
}
