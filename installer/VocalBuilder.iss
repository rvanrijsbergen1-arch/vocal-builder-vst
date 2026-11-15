; Vocal Builder — Windows installer (Inno Setup 6, free).
; Build: ISCC.exe /DAppVersion=0.1.0 /DDist=<path to dist> installer\VocalBuilder.iss
; dist layout (made by the CI workflow):
;   app\VocalEngine.exe, app\onnxruntime.dll
;   models\separation\spleeter_2stems.onnx, models\pitch\fcpe.onnx, models\encoders\*.onnx, models\voices\<id>\...
;   vst3\Vocal Builder.vst3\...
;   licences\*, vc_redist.x64.exe

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef Dist
  #define Dist "..\dist"
#endif

[Setup]
AppId={{7C2F7E1B-4F0B-4C55-9C64-2C5B8E1A9D10}
AppName=Vocal Builder
AppVersion={#AppVersion}
AppPublisher=Vocal Builder
DefaultDirName={autopf}\Vocal Builder
DisableDirPage=yes
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=admin
OutputBaseFilename=VocalBuilder-{#AppVersion}-win64-setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Vocal Builder {#AppVersion}
; Add your own end-user licence agreement here: LicenseFile=EULA.txt
; Code signing (optional, needs a certificate): SignTool=signtool sign /fd sha256 /tr http://timestamp.digicert.com /td sha256 $f

[Files]
Source: "{#Dist}\app\*"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Dist}\models\*"; DestDir: "{app}\models"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Dist}\licences\*"; DestDir: "{app}\licences"; Flags: ignoreversion
Source: "{#Dist}\vst3\Vocal Builder.vst3\*"; DestDir: "{commoncf64}\VST3\Vocal Builder.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Dist}\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Registry]
; The plugin finds the engine and models through this key.
Root: HKLM64; Subkey: "Software\Vocal Builder"; ValueType: string; ValueName: "InstallDir"; ValueData: "{app}"; Flags: uninsdeletekey
Root: HKLM64; Subkey: "Software\Vocal Builder"; ValueType: string; ValueName: "Version"; ValueData: "{#AppVersion}"

[Run]
; Microsoft Visual C++ runtime needed by onnxruntime.dll (skipped by the redistributable if already present).
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing Microsoft Visual C++ runtime..."; Flags: waituntilterminated

[UninstallDelete]
Type: dirifempty; Name: "{app}\models"

[Messages]
FinishedLabel=Vocal Builder is installed. In FL Studio open Options > Manage plugins, click "Find more plugins" (or rescan), then add "Vocal Builder" to a mixer insert. Activate your licence with the Licence button in the plugin.
