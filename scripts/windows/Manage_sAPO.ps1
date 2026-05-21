# Manage_sAPO.ps1 - sAPO Administration Utility for KRISHA
# Must run elevated (RunAs Administrator).

param (
    [switch]$Uninstall
)

$RegistryPath = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render"
$BackupPath = "$env:APPDATA\KRISHA\AudioRegistryBackup.reg"
$COMGuid = "{B333158B-C167-4052-A886-D1C4741BC2D1}" # Target COM GUID for KrishaAPO
$DLLPath = "$env:ProgramFiles\KRISHA\KrishaAPO.dll"

# Check administrator privileges
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Error "This script must be run as Administrator."
    Exit 1
}

# Ensure app directory exists
New-Item -ItemType Directory -Force -Path (Split-Path $BackupPath) | Out-Null

function Backup-AudioRegistry {
    if (-not (Test-Path (Split-Path $BackupPath))) {
        New-Item -ItemType Directory -Force -Path (Split-Path $BackupPath) | Out-Null
    }
    Write-Host "Backing up audio registry to $BackupPath..."
    reg export "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio" "$BackupPath" /y | Out-Null
}

function Restore-AudioRegistry {
    if (Test-Path $BackupPath) {
        Write-Host "Restoring audio registry from backup $BackupPath..."
        reg import "$BackupPath" | Out-Null
        Remove-Item -Path $BackupPath -ErrorAction SilentlyContinue
    } else {
        Write-Warning "No audio registry backup found to restore."
    }
}

function Register-COM {
    Write-Host "Registering KrishaAPO COM component..."
    $ClassPath = "HKLM:\SOFTWARE\Classes\CLSID\$COMGuid"
    New-Item -Path $ClassPath -Force | Out-Null
    New-ItemProperty -Path $ClassPath -Name "(Default)" -Value "KRISHA Audio Processing Object" -PropertyType String -Force | Out-Null
    
    $InProcPath = Join-Path $ClassPath "InprocServer32"
    New-Item -Path $InProcPath -Force | Out-Null
    New-ItemProperty -Path $InProcPath -Name "(Default)" -Value "$DLLPath" -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $InProcPath -Name "ThreadingModel" -Value "Both" -PropertyType String -Force | Out-Null
}

function Unregister-COM {
    Write-Host "Unregistering KrishaAPO COM component..."
    $ClassPath = "HKLM:\SOFTWARE\Classes\CLSID\$COMGuid"
    if (Test-Path $ClassPath) {
        Remove-Item -Path $ClassPath -Recurse -Force
    }
}

function Install-sAPO {
    Backup-AudioRegistry
    Register-COM

    Write-Host "Injecting KrishaAPO into rendering endpoints..."
    # Iterate through rendering endpoints and add the FxProperties values
    Get-ChildItem -Path $RegistryPath | ForEach-Object {
        $FxPath = Join-Path $_.PsPath "FxProperties"
        if (Test-Path $FxPath) {
            # Injects the sAPO as system-wide post-mix effect
            New-ItemProperty -Path $FxPath -Name "{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},11" -Value "$COMGuid" -PropertyType String -Force -ErrorAction SilentlyContinue | Out-Null
            Write-Host "Injected into: $_.Name"
        }
    }
    
    # Restart Windows Audio service
    Write-Host "Restarting Windows Audio services..."
    Restart-Service -Name "Audiosrv" -Force
    Write-Host "KRISHA sAPO installation complete!"
}

function Uninstall-sAPO {
    Write-Host "Uninstalling KRISHA sAPO..."
    Unregister-COM
    Restore-AudioRegistry
    
    # Restart Windows Audio service
    Write-Host "Restarting Windows Audio services..."
    Restart-Service -Name "Audiosrv" -Force
    Write-Host "KRISHA sAPO uninstallation complete!"
}

if ($Uninstall) {
    Uninstall-sAPO
} else {
    Install-sAPO
}
