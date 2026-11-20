# Set up Tutti

About ten minutes, once per computer. After this, Tutti keeps itself up to date and there is
nothing else to install or type.

**You need:** Ableton Live 12 Suite (everyone on the **same 12.x update**: check *Live > About
Live* on a Mac or *Help > About Live* on Windows), and a Mac with Apple Silicon (M1 or later,
macOS 11 or later) or a Windows 10/11 PC.

## 1. Tailscale: your group's private network

Tutti computers talk to each other directly through [Tailscale](https://tailscale.com). Nothing
goes through a server, and nobody outside your group can reach your Tutti. The free Personal plan
covers up to 6 people.

**One person sets up the network** (whoever usually hosts):

1. Install Tailscale from [tailscale.com/download](https://tailscale.com/download) and sign in
   (Google, Apple, Microsoft or GitHub account).
2. In the [Tailscale admin console](https://login.tailscale.com/admin/users), click **Invite
   users** and send each friend their invite link.

**Everyone else:**

1. Open your invite link and accept it.
2. Install Tailscale and sign in with the **same account** you accepted the invite with. On a
   Mac, click **Allow** when macOS asks to add a VPN configuration.
3. Leave Tailscale on. Its menu (menu bar on a Mac, system tray on Windows) lists your friends'
   computers.

Already use Tailscale for something else? Tailscale's menu lets you switch between networks:
pick the group's network while you use Tutti.

## 2. Install Tutti

**Mac:** download [Tutti-Installer.pkg](https://github.com/dylancleverdon/Baastik-Labs/releases/download/tutti-channel/Tutti-Installer.pkg)
and open it.

- The first time, macOS says it can't check the installer. Click **Done**, open **System
  Settings > Privacy & Security**, scroll down and click **Open Anyway** next to
  "Tutti-Installer.pkg" (macOS may ask for your Mac's password to confirm).
- Click through the installer. It installs just for you (no admin password) and opens Tutti.
  Its icon (four overlapping circles) is in the Dock and the menu bar.
- Now or later, if macOS asks whether Tutti can find devices on your local network, accept
  incoming connections, or open files in your Documents or Desktop folder, click **Allow**.
  That's how it reaches your friends and copies songs.

**Windows:** download [Tutti-Installer.exe](https://github.com/dylancleverdon/Baastik-Labs/releases/download/tutti-channel/Tutti-Installer.exe)
and run it.

- If your browser warns that the file isn't commonly downloaded, choose **Keep**.
- If Windows says it protected your PC, click **More info**, then **Run anyway**.
- Click **Yes** when Windows asks for permission. That's the only time Tutti needs it: to put its
  helper plugin where Live finds plugins and to let your friends' Tuttis (and only theirs, over
  Tailscale) reach this one.
- Finish with **Open Tutti** ticked. Tutti's icon (four overlapping circles) is in the taskbar
  and the system tray (click the **^** arrow if you don't see it).

Tutti starts by itself when you log in from now on. Closing its window doesn't quit it (a session
keeps going): click Tutti in the Dock, or open it from the Start menu on Windows, to bring the
window back. To quit, use **Quit Tutti** (⌘Q on a Mac, or the tray icon's menu).

## 3. Connect Live

1. **Quit Live and open it again.** Tutti added itself to Live's User Library when it first
   opened; Live only notices new Control Surfaces when it starts.
2. Open Live's settings (*Live > Settings* on a Mac, *Options > Preferences* on Windows), then
   the **Link, Tempo & MIDI** tab. In an empty **Control Surface** slot, choose **Tutti**. Leave
   its Input and Output as **None**.
3. In the **Plug-Ins** tab, make sure **Use VST3 Plug-in System Folders** is on (on a Mac, Audio
   Units work too). That's how Live finds **Tutti Stand-in**, the helper for plugins only some of
   you own.

## 4. Check it

Open Tutti's window. At the top you should see:

- **Tailscale connected**
- **Live 12.x connected**

Then open **Settings** (bottom right) and set your name and colour. That's how everyone sees
you: your colour marks where you are in the song and the ideas you suggest.

You're done. The [README](../README.md#use-it) explains starting and joining a session, saving,
Player 1, Play together and ideas.

## Saving, in short

Save with **Cmd+S / Ctrl+S in Live**, as always. During a session Live has Tutti's copy of the
song open (`Music/Tutti/<song>`), so that's where it saves, and Tutti shares the save with
everyone. Never use *Save As*. If Live asks where to save, it isn't on the session song: click
**Open the song** in Tutti.

## If something's not right

| What you see | What to do |
| --- | --- |
| Tutti's window is gone | It's still running: click Tutti in the Dock (Mac) or open Tutti from the Start menu (Windows). |
| Tutti says **Restart Live** | Tutti updated itself, and Live only loads the new version when it starts. Quit Live and open it again. |
| Windows blocks the installer with no **Run anyway** | That's Smart App Control, which blocks apps that aren't signed. It can be turned off in **Windows Security > App & browser control**, but on many PCs it can't be turned back on, so ask whoever runs the group first. |
| Tutti isn't in Live's Control Surface list | Open Tutti once, then quit and reopen Live. Tutti's **Settings** shows where it put its Live part (the Remote Script). If it says it couldn't, open Live once, then quit and reopen Tutti. |
| **Tailscale is off** | Open Tailscale and sign in. |
| A friend's session doesn't show up | Both of you: Tailscale on, and you see each other's computers in Tailscale's menu; Tutti open. On a Mac with the firewall turned on, click **Allow** when macOS asks whether Tutti may accept incoming connections. |
| **Live isn't connected** | Live is closed, or Tutti isn't chosen as a Control Surface (step 3). |
| Live versions don't match | Everyone updates Live to the same 12.x version. |
| A plugin is missing on your computer | Expected when you don't own it. Whoever owns it puts **Tutti Stand-in** right after it on that track and saves; their Stand-in records whenever they play the song, and you hear that recording. See the README's [Plugins](../README.md#plugins) section. |
| Live asks where to save | Live isn't on the session song (for example, an Untitled Set is open). Click **Open the song** in Tutti, then save with Cmd+S / Ctrl+S. |
| Joining stays on **Downloading** | Wait 30 seconds: Tutti opens the song with whatever has arrived. If it's still stuck, both of you click **Send logs** in the Suggestion box. |
| A friend's plugin change didn't show up | Plugins travel with saves: they save in Live, then you save (or click **Update without saving**). |
| The suggestion box says suggestions are waiting to send | The box isn't connected to the sheet yet (see below). Your suggestions are kept and sent as soon as it is. |
| Something else | Open the **Suggestion box**, describe it, and click **Send logs**. |

## For whoever runs the group (once)

- **Suggestion box:** connect it to the "suggestions sheet tutti" Google Sheet by deploying the
  small script in [`suggestions/`](../suggestions/README.md) (about two minutes in the browser).
  Until then the box keeps suggestions on each computer.
- **Backups (optional):** everyone already keeps every song they've been in, with its audio,
  in `Music/Tutti`. For an extra copy, **Settings > Backups** copies every version (with its
  audio) to a folder such as an external drive while you're Player 1.
- **Logs from Send logs:** they're saved in a **Tutti logs** folder next to the suggestions
  sheet, linked from its Log column, once the sheet's script is updated (see
  [`suggestions/`](../suggestions/README.md#updating-the-script-keeps-the-same-url)).
