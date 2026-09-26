; Windows installer for Serum Preset Generator (Inno Setup 6).
;   iscc /DAppVersion=0.1.5 /DArtefacts=<...\Release> /DOutputDir=<dir> /DOutputName=<name> installer.iss
; Installs the VST3 into the standard VST3 folder and the standalone app into
; Program Files\Baastik Labs.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
AppId={{6E1F2B7A-4C3D-4F8E-9A21-5B7C0D3E9F14}
AppName=Serum Preset Generator
AppVersion={#AppVersion}
AppVerName=Serum Preset Generator {#AppVersion}
AppPublisher=Baastik Labs
AppPublisherURL=https://github.com/dylancleverdon/Baastik-Labs
DefaultDirName={autopf}\Baastik Labs\Serum Preset Generator
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
; Updating in place: close DAWs holding the old plugin, or replace it on reboot.
CloseApplications=yes
RestartApplications=no
UninstallDisplayName=Serum Preset Generator

[Files]
Source: "{#Artefacts}\VST3\Serum Preset Generator.vst3\*"; DestDir: "{commoncf64}\VST3\Serum Preset Generator.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs restartreplace
Source: "{#Artefacts}\Standalone\Serum Preset Generator.exe"; DestDir: "{app}"; Flags: ignoreversion restartreplace

[Icons]
Name: "{autoprograms}\Baastik Labs\Serum Preset Generator"; Filename: "{app}\Serum Preset Generator.exe"

[Messages]
FinishedLabel=Serum Preset Generator is installed.%n%nRestart your DAW (or rescan plugins) and add it from the Baastik Labs plugins. It updates itself from now on.
