// The Tutti app: menu bar icon on macOS, tray icon on Windows, and a small window.
//
//   Tutti                 normal launch (window shown)
//   Tutti --background    start at login: just the menu bar / tray icon
//   Tutti --screenshot D  render every screen to PNGs in folder D, then quit (for checking the UI)

#include <csignal>
#include "AppUpdater.h"
#include "BinaryData.h"
#include "Platform.h"
#include "SuggestionBox.h"
#include "TuttiVersion.h"
#include "ui/MainComponent.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace
{
    juce::Image loadImage (const void* data, int size)
    {
        return juce::ImageFileFormat::loadFrom (data, static_cast<size_t> (size));
    }

    // Actions that do nothing, for screenshots.
    struct NoActions : AppActions
    {
        void startSession (const juce::File&) override {}
        void join (const std::string&) override {}
        void leave() override {}
        void approveJoin (const std::string&, bool) override {}
        void answerPlayer1 (const std::string&, bool) override {}
        void requestPlayer1() override {}
        void handOver (const std::string&) override {}
        void takeOver() override {}
        void playTogether() override {}
        void answerPlay (bool) override {}
        void stopTogether() override {}
        void setPlayMode (const std::string&) override {}
        void openSong() override {}
        void revealSong() override {}
        void makeIdea() override {}
        void acceptIdea (const std::string&) override {}
        void rejectIdea (const std::string&) override {}
        void setIdentity (const std::string&, int) override {}
        void checkForUpdates() override {}
        void rollBack() override {}
        void setAutoUpdate (bool) override {}
        void sendSuggestion (const std::string&, const std::string&, bool) override {}
        void setStartAtLogin (bool) override {}
        void showLogs() override {}
        void openUrl (const juce::String&) override {}
        void updateNow() override {}
        void labelVersion (const std::string&) override {}
        void restoreVersion (const std::string&) override {}
        void openVersionCopy (const std::string&) override {}
        void setBackupFolder (const juce::File&) override {}
        void setSharedFolder (const juce::File&) override {}
    };

    tutti::MemberInfo member (const std::string& peer, const std::string& name, int colour, bool online = true)
    {
        tutti::MemberInfo m;
        m.peer = peer;
        m.name = name;
        m.color = colour;
        m.online = online;
        return m;
    }

    void writeScreenshots (const juce::File& dir)
    {
        dir.createDirectory();
        NoActions actions;
        MainComponent ui (actions);
        ui.setSize (440, 720);

        auto shoot = [&] (const char* name, const AppState& s, MainComponent::Page page = MainComponent::Page::main) {
            ui.showPage (page);
            ui.update (s);
            ui.resized();
            ui.update (s);
            auto image = ui.createComponentSnapshot (ui.getLocalBounds(), true, 2.0f);
            juce::FileOutputStream out (dir.getChildFile (juce::String (name) + ".png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
        };

        AppState s;
        s.updates.installedVersion = tutti::version::string;
        s.updates.status = "Up to date";
        s.engine.myName = "Dylan";
        s.engine.myColor = 0;
        s.engine.playTogetherMode = "ask";
        s.setup.scriptInstalledIn.add ("~/Music/Ableton/User Library/Remote Scripts/Tutti");

        // 1. First run: nothing set up.
        shoot ("1-setup", s);

        // 2. Ready, with a session to join.
        s.engine.tailscaleInstalled = s.engine.tailscaleRunning = true;
        s.engine.selfAddress = "100.101.7.12:47380";
        s.engine.liveConnected = true;
        s.engine.liveVersion = "12.3.2";
        s.engine.liveStatus = "Ready";
        tutti::EngineView::Peer sam;
        sam.peer = "p2";
        sam.name = "Sam";
        sam.hosting = true;
        sam.song = "Night Drive";
        sam.members = 2;
        tutti::EngineView::Peer cara;
        cara.peer = "p3";
        cara.name = "Cara";
        s.engine.peers = { sam, cara };
        s.engine.notices = { "Tailscale connected" };
        shoot ("2-home", s);

        // 3. In a session as Player 1, with someone asking to join and someone asking for Player 1.
        s.engine.role = tutti::Session::Role::player1;
        s.engine.liveBound = true;
        s.engine.session.song = "Night Drive";
        s.engine.session.p1 = "p1";
        tutti::EngineView::Member me { member ("p1", "Dylan", 0), true, true, "Drums", 17.0, true, false };
        tutti::EngineView::Member m2 { member ("p2", "Sam", 1), false, false, "Bass", 9.0, false, false };
        tutti::EngineView::Member m3 { member ("p3", "Cara", 3), false, false, "Vox", 33.0, true, true };
        tutti::EngineView::Member m4 { member ("p4", "Jo", 4, false), false, false, "", 1.0, false, false };
        s.engine.members = { me, m2, m3, m4 };
        s.engine.peers.push_back ({ "p5", "Max" });
        s.engine.joinRequests = { "p5" };
        s.engine.p1Requests = { "p3" };
        s.engine.previewsWaiting = 2;
        s.engine.notices = { "Cara asked to be Player 1." };
        s.engine.songFile = "/Users/dylan/Music/Tutti/Night Drive/Night Drive.als";
        s.engine.saveToShare = { "Serum 2" };
        shoot ("3-session-player1", s);
        s.engine.saveToShare.clear();
        s.engine.waitingForSave = { { "Sam", "Serum 2" } };

        // 4. A guest with an idea to review and a Play together request.
        s.engine.role = tutti::Session::Role::guest;
        s.engine.session.p1 = "p2";
        s.engine.members[0].isPlayer1 = false;
        s.engine.members[1].isPlayer1 = true;
        s.engine.joinRequests.clear();
        s.engine.p1Requests.clear();
        s.engine.previewsWaiting = 0;
        s.engine.playRequestBy = "Sam";
        s.engine.playRequestPos = 17.0;
        s.engine.staleCount = 2;
        s.engine.ideas = { { "t9", "t2", "p1", "Dylan", "Bass (Dylan's idea)", "Bass" } };
        s.engine.notices = { "Sam wants to play together." };
        s.engine.updateAvailable = true;
        s.engine.updateBy = "Sam";
        s.engine.versions = { { "v3", "Tue 17 Nov, 21:40", "Sam", "", "Sam saved (merged with earlier saves)", false },
                              { "v2", "Tue 17 Nov, 21:12", "Dylan", "Before the new chorus", "Dylan saved", true },
                              { "v1", "Tue 17 Nov, 20:30", "Sam", "", "Session started", false } };
        shoot ("4-session-guest", s);
        shoot ("7-versions", s, MainComponent::Page::versions);

        // 5. Settings and the suggestion box.
        s.updates.availableVersion = "0.1.60";
        s.updates.availableNotes = "Faster joins for big songs.";
        s.updates.status = "Tutti 0.1.60 is ready: it installs when you're not in a session";
        s.updates.canRollBack = true;
        s.engine.sharedFolder = "/Users/dylan/Library/CloudStorage/GoogleDrive-dylan/My Drive/Tutti Shared";
        s.engine.sharedFolderFound = true;
        shoot ("5-settings", s, MainComponent::Page::settings);
        s.suggestions.queued = 1;
        shoot ("6-suggestions", s, MainComponent::Page::suggest);
    }
}

#if JUCE_MAC
void installReopenHandler (std::function<void()>); // MacReopen.cpp
#endif
std::string keepRunningInBackground(); // Background.cpp

class TuttiApp : public juce::JUCEApplication, private AppActions, private juce::Timer
{
public:
    const juce::String getApplicationName() override { return "Tutti"; }
    const juce::String getApplicationVersion() override { return tutti::version::string; }
    // Only one Tutti runs at a time, except: an update or roll back starts the new Tutti while the
    // old one is still quitting (initialise waits for it), and screenshots run beside a real Tutti.
    bool moreThanOneInstanceAllowed() override
    {
        const auto args = getCommandLineParameterArray();
        return args.contains ("--updated") || args.contains ("--screenshot");
    }

    void initialise (const juce::String& commandLine) override
    {
       #if ! JUCE_WINDOWS
        std::signal (SIGPIPE, SIG_IGN); // a friend's closed connection must not quit Tutti
       #endif
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        const auto args = juce::StringArray::fromTokens (commandLine, true);
        if (args.contains ("--screenshot"))
        {
            const auto dir = args[args.indexOf ("--screenshot") + 1].unquoted();
            writeScreenshots (juce::File::getCurrentWorkingDirectory().getChildFile (dir));
            quit();
            return;
        }

        if (args.contains ("--updated") && ! takeOverFromPreviousInstance())
        {
            quit(); // another Tutti is running after all: it carries on
            return;
        }

        AppUpdater::cleanUpAfterUpdate();
        engine = std::make_unique<tutti::Engine> (tutti::EngineOptions {});
        engine->onChange = [this] { dirty = true; };
        engine->startup();
        if (const auto result = keepRunningInBackground(); ! result.empty())
            engine->logLine (result);

        AppUpdater::Options updaterOptions;
        updaterOptions.dataDir = engine->dataDir();
        updaterOptions.safeToInstall = [this] { return ! engine->session().inSession(); };
        updaterOptions.beforeRelaunch = [this] {
            stopTimer();
            if (engine != nullptr)
                engine->shutdown();
        };
        updaterOptions.quitForRelaunch = [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); };
        updater = std::make_unique<AppUpdater> (updaterOptions);
        updater->onChange = [this] { dirty = true; };

        suggestions = std::make_unique<SuggestionBox> (engine->dataDir());
        suggestions->onChange = [this] { dirty = true; };

        loadAppSettings();
        installRemoteScript();

        window = std::make_unique<Window> (static_cast<AppActions&> (*this));
        createTray();
        menus = std::make_unique<Menus> (*this);
       #if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu (menus.get());
       #endif
       #if JUCE_MAC
        // Dock icon clicks bring the window back (installed once launching has finished).
        juce::MessageManager::callAsync ([this] {
            installReopenHandler ([this] {
                engine->logLine ("reopened: showing the window");
                showWindow();
            });
        });
       #endif
        if (! args.contains ("--background"))
            showWindow();
        startTimer (20);
    }

    void shutdown() override
    {
        stopTimer();
       #if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu (nullptr);
       #endif
        menus.reset();
        tray.reset();
        window.reset();
        suggestions.reset();
        updater.reset();
        if (engine != nullptr)
            engine->shutdown();
        engine.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const juce::String&) override { showWindow(); }

private:
    // Relaunched by an update: the old Tutti holds the single-instance lock until it has quit and
    // let go of its ports. Wait for that (up to 30 s), then take the lock the usual way so later
    // launches hand over to this Tutti. False if another Tutti still holds it.
    bool takeOverFromPreviousInstance()
    {
        {
            juce::InterProcessLock previous ("juceAppLock_" + getApplicationName());
            if (previous.enter (30 * 1000))
                previous.exit();
        }
        return ! sendCommandLineToPreexistingInstance();
    }

    // ------------------------------------------------------------ window and tray
    struct Window : juce::DocumentWindow
    {
        explicit Window (AppActions& actions)
            : juce::DocumentWindow ("Tutti", theme::background, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton)
        {
            setUsingNativeTitleBar (true);
            content = new MainComponent (actions);
            setContentOwned (content, true);
            setResizable (true, false);
            setResizeLimits (400, 520, 900, 1400);
            centreWithSize (440, 720);
        }
        // Tutti keeps running (a session mustn't end because a window closed). The Dock icon, the
        // menu bar / tray icon, or opening Tutti again brings the window back; Quit quits.
        void closeButtonPressed() override { setVisible (false); }
        MainComponent* content = nullptr;
    };

    struct Tray : juce::SystemTrayIconComponent
    {
        explicit Tray (TuttiApp& a) : app (a)
        {
            auto colour = loadImage (TuttiAppData::traycolor_png, TuttiAppData::traycolor_pngSize);
            auto mono = loadImage (TuttiAppData::traytemplate_png, TuttiAppData::traytemplate_pngSize);
           #if JUCE_MAC
            setIconImage (mono, mono);
           #else
            setIconImage (colour, colour);
           #endif
            setIconTooltip ("Tutti");
        }

        void mouseDown (const juce::MouseEvent&) override
        {
            auto menu = app.sessionMenu (true);
           #if JUCE_MAC
            showDropdownMenu (menu);
           #else
            menu.showMenuAsync (juce::PopupMenu::Options());
           #endif
        }

        TuttiApp& app;
    };

    // The menu bar's Session and View menus (macOS) and the menu bar / tray icon's menu. On a Mac
    // menu items only run when the app has a menu bar model, so the icon's menu needs this too.
    struct Menus : juce::MenuBarModel
    {
        explicit Menus (TuttiApp& a) : app (a) {}
        juce::StringArray getMenuBarNames() override { return { "Session", "View" }; }
        juce::PopupMenu getMenuForIndex (int index, const juce::String&) override
        {
            if (index == 0)
                return app.sessionMenu (false);
            juce::PopupMenu m;
            using P = MainComponent::Page;
            auto page = [this] (P p) { return [this, p] { app.showPage (p); }; };
            auto item = [&] (const char* text, P p, const char* key, bool enabled = true) {
                juce::PopupMenu::Item i (text);
                i.action = page (p);
                i.shortcutKeyDescription = shortcut (key);
                i.isEnabled = enabled;
                m.addItem (std::move (i));
            };
            item ("Session", P::main, "1");
            item ("Versions", P::versions, "2", app.engine->session().inSession());
            item ("Suggestion box", P::suggest, "3");
            item ("Settings", P::settings, ",");
            m.addSeparator();
            m.addItem ("Show logs", [this] { app.showLogs(); });
            return m;
        }
        void menuItemSelected (int, int) override {}
        static juce::String shortcut (const char* key)
        {
           #if JUCE_MAC
            return juce::String::fromUTF8 ("\xe2\x8c\x98") + key;
           #else
            return juce::String ("Ctrl+") + key;
           #endif
        }
        TuttiApp& app;
    };

    juce::PopupMenu sessionMenu (bool forIcon)
    {
        juce::PopupMenu menu;
        const auto v = engine->view();
        const auto& s = engine->session();
        using R = tutti::Session::Role;
        if (forIcon)
        {
            menu.addItem ("Open Tutti", [this] { showWindow(); });
            menu.addSeparator();
            const juce::String status = s.inSession() ? juce::String (v.session.song) + (s.isPlayer1() ? " (you're Player 1)" : "")
                                                      : juce::String (v.role == R::joining ? "Waiting to be let in..." : "Not in a session");
            menu.addItem (juce::PopupMenu::Item (status).setEnabled (false));
            if (s.inSession())
                menu.addItem (juce::PopupMenu::Item (juce::String (v.liveBound ? "Live: in sync" : "Live: " + v.liveStatus)).setEnabled (false));
            menu.addSeparator();
        }
        if (! s.inSession() && v.role != R::joining)
        {
            menu.addItem ("Share a song...", [this] { showWindow(); window->content->chooseSong(); });
            for (const auto& p : v.peers)
                if (p.hosting)
                {
                    const auto peer = p.peer;
                    menu.addItem ("Join " + juce::String (p.song) + " (" + juce::String (p.name) + ")", [this, peer] { join (peer); showWindow(); });
                }
        }
        else if (v.role == R::joining)
            menu.addItem ("Cancel joining", [this] { leave(); });
        else
        {
            menu.addItem ("Play together", [this] { playTogether(); });
            menu.addItem ("Stop playing together", [this] { stopTogether(); });
            if (! s.isPlayer1())
                menu.addItem (juce::PopupMenu::Item (v.askedForP1 ? "Asked to be Player 1..." : "Ask to be Player 1")
                                  .setAction ([this] { requestPlayer1(); }).setEnabled (! v.askedForP1));
            menu.addItem ("Try an idea on the selected track", [this] { makeIdea(); });
            menu.addSeparator();
            menu.addItem ("Open the session song in Live", [this] { openSong(); });
            menu.addItem ("Show the song file", [this] { revealSong(); });
            menu.addItem ("Versions...", [this] { showPage (MainComponent::Page::versions); });
            menu.addSeparator();
            menu.addItem ("Leave session", [this] { leave(); });
        }
        if (forIcon)
        {
            menu.addSeparator();
            menu.addItem ("Suggestion box...", [this] { showPage (MainComponent::Page::suggest); });
            menu.addItem ("Settings...", [this] { showPage (MainComponent::Page::settings); });
            menu.addSeparator();
            menu.addItem ("Quit Tutti", [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); });
        }
        return menu;
    }

    void showPage (MainComponent::Page p)
    {
        showWindow();
        if (window != nullptr)
            window->content->showPage (p);
    }

    void createTray()
    {
       #if JUCE_MAC || JUCE_WINDOWS
        tray = std::make_unique<Tray> (*this);
       #endif
    }

    void showWindow()
    {
        if (window == nullptr)
            return;
        window->setVisible (true);
       #if JUCE_MAC
        juce::Process::makeForegroundProcess();
       #endif
        window->toFront (true);
        dirty = true;
    }

    // ------------------------------------------------------------ periodic work
    void timerCallback() override
    {
        engine->pump();
        updater->tick();
        suggestions->setEndpoint (updater->suggestionsEndpoint());
        suggestions->tick();
        const auto t = juce::Time::getMillisecondCounter();
        if (window != nullptr && window->isVisible() && (dirty || t - lastUiUpdate > 500))
        {
            dirty = false;
            lastUiUpdate = t;
            const auto st = appState();
            window->content->update (st);
            // Rebuild the menu bar only when what it offers changes.
            juce::String sig;
            sig << (int) st.engine.role << (st.engine.askedForP1 ? 1 : 0) << (int) st.engine.peers.size();
            for (const auto& p : st.engine.peers)
                sig << (p.hosting ? 1 : 0) << juce::String (p.song);
            if (menus != nullptr && sig.toStdString() != menuSignature)
            {
                menuSignature = sig.toStdString();
                menus->menuItemsChanged();
            }
        }
    }

    AppState appState() const
    {
        AppState s;
        s.engine = engine->view();
        s.updates = updater->state();
        s.suggestions = suggestions->state();
        s.setup.scriptInstalledIn = scriptInstalledIn;
        s.setup.startAtLogin = startAtLogin;
        return s;
    }

    // ------------------------------------------------------------ setup
    void loadAppSettings()
    {
        auto file = engine->dataDir().getChildFile ("app.json");
        auto j = tutti::parseJson (file.loadFileAsString().toStdString()).value_or (tutti::Json::object());
        startAtLogin = tutti::boolean (j, "login", true);
        // Register (again) at every launch, so a moved app keeps starting at login.
        tutti::platform::setStartAtLogin (juce::File::getSpecialLocation (juce::File::currentExecutableFile), startAtLogin);
    }

    void saveAppSettings()
    {
        tutti::Json j { { "login", startAtLogin } };
        engine->dataDir().getChildFile ("app.json").replaceWithText (juce::String (j.dump (2)));
    }

    void installRemoteScript()
    {
        juce::Thread::launch ([this] {
            auto installed = tutti::platform::installRemoteScript (tutti::platform::bundledRemoteScript());
            juce::MessageManager::callAsync ([this, installed] {
                scriptInstalledIn.clear();
                for (const auto& f : installed)
                    scriptInstalledIn.add (f.getParentDirectory().getParentDirectory().getFullPathName());
                dirty = true;
            });
        });
    }

    // ------------------------------------------------------------ AppActions
    void startSession (const juce::File& als) override
    {
        juce::String error;
        if (! engine->startSession (als, error))
            engine->notice (error.toStdString());
        dirty = true;
    }
    void join (const std::string& peer) override { engine->joinSession (peer); }
    void leave() override { engine->leaveSession(); }
    void approveJoin (const std::string& p, bool a) override { engine->approveJoin (p, a); }
    void answerPlayer1 (const std::string& p, bool a) override { engine->answerPlayer1Request (p, a); }
    void requestPlayer1() override { engine->requestPlayer1(); }
    void handOver (const std::string& p) override { engine->handOver (p); }
    void takeOver() override { engine->takeOver(); }
    void playTogether() override { engine->playTogether(); }
    void answerPlay (bool join) override { engine->answerPlayTogether (join); }
    void stopTogether() override { engine->stopTogether(); }
    void setPlayMode (const std::string& m) override { engine->setPlayTogetherMode (m); }
    void openSong() override { engine->openSongInLive(); }
    void revealSong() override { engine->revealSong(); }
    void makeIdea() override { engine->makeIdeaOnSelectedTrack(); }
    void acceptIdea (const std::string& t) override { engine->acceptIdea (t); }
    void rejectIdea (const std::string& t) override { engine->rejectIdea (t); }
    void setIdentity (const std::string& name, int colour) override { engine->setIdentity (name, colour); }
    void checkForUpdates() override { updater->checkNow(); }
    void rollBack() override { updater->rollBack(); }
    void setAutoUpdate (bool on) override { updater->setAutoUpdate (on); }
    void sendSuggestion (const std::string& kind, const std::string& text, bool withLogs) override
    {
        const auto v = engine->view();
        suggestions->add (kind, text, v.myName, tutti::version::string, tutti::platform::osName().toStdString(), v.liveVersion,
                          withLogs ? collectLogs() : juce::String());
    }

    // The end of each log, newest last: Tutti's, its updater's and Live's own (which has the
    // Remote Script's errors).
    static juce::String tail (const juce::File& f, juce::int64 bytes)
    {
        juce::FileInputStream in (f);
        if (! in.openedOk())
            return "(couldn't read " + f.getFullPathName() + ")\n";
        const auto size = in.getTotalLength();
        if (size > bytes)
            in.setPosition (size - bytes);
        juce::MemoryBlock block;
        in.readIntoMemoryBlock (block);
        auto text = juce::String::createStringFromData (block.getData(), (int) block.getSize());
        if (size > bytes)
            text = text.fromFirstOccurrenceOf ("\n", false, false);
        return text;
    }

    juce::String collectLogs()
    {
        const auto v = engine->view();
        juce::String out;
        out << "Tutti " << tutti::version::string << " on " << tutti::platform::osName() << ", Live " << juce::String (v.liveVersion)
            << ", " << juce::SystemStats::getOperatingSystemName() << "\n"
            << "Live: " << juce::String (v.liveStatus) << " | session: " << juce::String (v.session.song)
            << " | song file: " << juce::String (v.songFile) << "\n";
        const auto logs = tutti::platform::logsDir();
        auto section = [&] (const juce::String& title, const juce::File& f, juce::int64 bytes) {
            out << "\n===== " << title << " (" << f.getFullPathName() << ") =====\n";
            out << (f.existsAsFile() ? tail (f, bytes) : juce::String ("(none)\n"));
        };
        section ("tutti.log", logs.getChildFile ("tutti.log"), 600 * 1024);
        section ("updater.log", logs.getChildFile ("updater.log"), 24 * 1024);
        section ("Live's Log.txt", tutti::platform::liveLogFile(), 400 * 1024);
        return out;
    }
    void setStartAtLogin (bool on) override
    {
        startAtLogin = on;
        tutti::platform::setStartAtLogin (juce::File::getSpecialLocation (juce::File::currentExecutableFile), on);
        saveAppSettings();
        dirty = true;
    }
    void showLogs() override
    {
        auto dir = tutti::platform::logsDir();
        dir.createDirectory();
        dir.startAsProcess();
    }
    void openUrl (const juce::String& url) override { juce::URL (url).launchInDefaultBrowser(); }
    void updateNow() override { engine->updateNow(); }
    void labelVersion (const std::string& label) override { engine->labelVersion (label); }
    void restoreVersion (const std::string& id) override { engine->restoreVersion (id); }
    void openVersionCopy (const std::string& id) override { engine->openVersionCopy (id); }
    void setBackupFolder (const juce::File& folder) override { engine->setBackupFolder (folder); dirty = true; }
    void setSharedFolder (const juce::File& folder) override { engine->setSharedFolder (folder); dirty = true; }

    theme::LookAndFeel lookAndFeel;
    std::unique_ptr<tutti::Engine> engine;
    std::unique_ptr<AppUpdater> updater;
    std::unique_ptr<SuggestionBox> suggestions;
    std::unique_ptr<Window> window;
    std::unique_ptr<Tray> tray;
    std::unique_ptr<Menus> menus;
    std::string menuSignature;
    juce::StringArray scriptInstalledIn;
    bool startAtLogin = true;
    bool dirty = true;
    juce::uint32 lastUiUpdate = 0;
};

START_JUCE_APPLICATION (TuttiApp)
