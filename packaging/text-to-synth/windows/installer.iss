; Windows installer for Text To Synth (Inno Setup 6).
;   iscc /DAppVersion=0.1.5 /DBinary=<...\TextToSynth.exe> /DOutputDir=<dir> /DOutputName=<name> installer.iss
; Per-user install (no admin prompt) into %LOCALAPPDATA%\Programs\Baastik Labs,
; then registers the server with Claude Desktop / Claude Code. Uninstalling
; unregisters it; presets in Serum's User\Text To Synth folder are kept.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
AppId={{B3A7E0C2-5D41-4E9B-8F26-1C7A9D4E2F80}
AppName=Text To Synth
AppVersion={#AppVersion}
AppVerName=Text To Synth {#AppVersion}
AppPublisher=Baastik Labs
AppPublisherURL=https://github.com/dylancleverdon/Baastik-Labs
DefaultDirName={localappdata}\Programs\Baastik Labs\Text To Synth
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableWelcomePage=no
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
OutputDir={#OutputDir}
OutputBaseFilename={#OutputName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; Claude keeps the server running: close it so an update can replace it.
CloseApplications=force
RestartApplications=no
UninstallDisplayName=Text To Synth (Baastik Labs)

[Files]
Source: "{#Binary}"; DestDir: "{app}"; DestName: "TextToSynth.exe"; Flags: ignoreversion

[Run]
Filename: "{app}\TextToSynth.exe"; Parameters: "--register"; StatusMsg: "Connecting Text To Synth to Claude..."; Flags: runhidden waituntilterminated

[UninstallRun]
Filename: "{app}\TextToSynth.exe"; Parameters: "--unregister"; RunOnceId: "UnregisterTextToSynth"; Flags: runhidden waituntilterminated

[Messages]
WelcomeLabel2=Describe a sound to Claude, get a Serum 2 preset, then shape it in plain English ("drier", "too harsh", "more movement").%n%nThis connects Text To Synth to Claude Desktop (and Claude Code, if you use it).
FinishedLabel=Text To Synth is installed.%n%nQuit and reopen Claude, then try: "Make me a dark, detuned reese bass."%n%nPresets appear in Serum 2 under User > Text To Synth. Text To Synth updates itself from now on.
