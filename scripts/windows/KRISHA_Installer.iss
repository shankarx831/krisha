; KRISHA_Installer.iss - Windows Installer Configuration for Inno Setup

[Setup]
AppName=KRISHA
AppVersion=1.0.0
AppPublisher=Shankar
DefaultDirName={commonpf}\KRISHA
DefaultGroupName=KRISHA
OutputDir=..\..\
OutputBaseFilename=Install_KRISHA
Compression=lzma
SolidCompression=yes
PrivilegesRequired=admin
SetupIconFile=AppIcon.ico

[Files]
Source: "..\..\apps\windows\bin\Release\KRISHA.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\packages\spokes\windows\build\Release\krisha_apo.dll"; DestName: "KrishaAPO.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "Manage_sAPO.ps1"; DestDir: "{app}"; Flags: ignoreversion

[Run]
; Run the PowerShell sAPO registration script on install
Filename: "powershell.exe"; Parameters: "-ExecutionPolicy Bypass -File ""{app}\Manage_sAPO.ps1"""; Flags: runhidden

[UninstallRun]
; Run the PowerShell sAPO unregistration script on uninstall
Filename: "powershell.exe"; Parameters: "-ExecutionPolicy Bypass -File ""{app}\Manage_sAPO.ps1"" -Uninstall"; Flags: runhidden

[Code]
// Custom code to backup registry before uninstall begins if needed
