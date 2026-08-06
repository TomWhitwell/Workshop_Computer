# Flash alloy.uf2 to a Workshop Computer in BOOTSEL mode.
#
# Usage: powershell -ExecutionPolicy Bypass -File flash.ps1
# Hold BOOTSEL while plugging in USB-C; the script waits for the
# RPI-RP2 drive to appear, copies the UF2, and confirms.

$ErrorActionPreference = 'Stop'

$uf2 = Join-Path $PSScriptRoot 'build\alloy.uf2'
if (-not (Test-Path $uf2)) {
    Write-Error "Not found: $uf2 - build the firmware first (cmake --build build)."
}

Write-Host 'Waiting for RPI-RP2 bootloader drive (hold BOOTSEL while plugging in USB)...'
$drive = $null
while (-not $drive) {
    $drive = Get-CimInstance Win32_LogicalDisk |
        Where-Object { $_.VolumeName -eq 'RPI-RP2' } |
        Select-Object -First 1
    if (-not $drive) { Start-Sleep -Milliseconds 500 }
}

Write-Host "Found bootloader at $($drive.DeviceID) - copying alloy.uf2..."
Copy-Item $uf2 "$($drive.DeviceID)\"
Write-Host 'Flashed. The module will reboot into the new firmware.'
