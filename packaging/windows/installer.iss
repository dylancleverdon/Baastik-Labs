; Windows installer for Tutti (Inno Setup 6).
;   iscc /DAppVersion=0.1.5 /DPayload=<staged payload folder> /DOutputDir=<dir> /DOutputName=<name> installer.iss
;
; Asks for admin rights once: Live only finds VST3 plugins in the system folder, which is where
; the Tutti Stand-in goes. The app folder and the plugin folder are then made writable for the
; user, so Tutti's own updates never need admin rights again. Tutti itself starts as the person
; who installed it (not as admin) and, on its first run, installs its Remote Script into Live's
; User Library and sets itself to start at login.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
AppId={{6C1E2A57-41D9-4B7E-9E0F-7D7A1B3C9F21}
AppName=Tutti
AppVersion={#AppVersion}
AppVerName=Tutti {#AppVersion}
AppPublisher=Baastik Labs
AppPublisherURL=https://github.com/dylancleverdon/Baastik-Labs
DefaultDirName={autopf}\Baastik Labs\Tutti
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableWelcomePage=no
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputDir={#OutputDir}
OutputBaseFilename={#OutputName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile=compiler:SetupClassicIcon.ico
UninstallDisplayIcon={app}\Tutti.exe
UninstallDisplayName=Tutti (Baastik Labs)
; Tutti runs in the tray: close it so an update can replace it.
CloseApplications=force
RestartApplications=no

[Dirs]
Name: "{app}"; Permissions: users-modify
Name: "{commoncf64}\VST3\Tutti Stand-in.vst3"; Permissions: users-modify; Check: HasPlugin

[Files]
Source: "{#Payload}\Tutti.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Payload}\RemoteScript\*"; DestDir: "{app}\RemoteScript"; Flags: ignoreversion recursesubdirs createallsubdirs
#ifexist Payload + "\Tutti Stand-in.vst3\Contents\x86_64-win\Tutti Stand-in.vst3"
Source: "{#Payload}\Tutti Stand-in.vst3\*"; DestDir: "{commoncf64}\VST3\Tutti Stand-in.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
#endif

[Icons]
Name: "{autoprograms}\Tutti"; Filename: "{app}\Tutti.exe"

[Run]
Filename: "{app}\Tutti.exe"; Description: "Open Tutti"; Flags: nowait postinstall runasoriginaluser

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\Tutti Stand-in.vst3"

[Code]
function HasPlugin: Boolean;
begin
#ifexist Payload + "\Tutti Stand-in.vst3\Contents\x86_64-win\Tutti Stand-in.vst3"
  Result := True;
#else
  Result := False;
#endif
end;

[Messages]
WelcomeLabel2=Tutti lets you and your friends work on the same Ableton Live song at the same time, each on your own computer.%n%nWindows will ask for permission once, to put Tutti's helper plugin where Live looks for plugins.
FinishedLabel=Tutti is installed and lives in your system tray (the four circles).%n%nNext: install Tailscale (tailscale.com/download), sign in and accept your invite. Then in Live open Settings > Link, Tempo & MIDI and choose Tutti as a Control Surface.
