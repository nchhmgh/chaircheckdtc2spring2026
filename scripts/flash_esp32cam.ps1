[CmdletBinding()]
param(
    [string]$Port,
    [switch]$SoftAP,
    [switch]$Eduroam,
    [int]$Baud = 115200,
    [switch]$Monitor
)

$ErrorActionPreference = "Stop"

if ($SoftAP -and $Eduroam) {
    throw "-SoftAP and -Eduroam are mutually exclusive; pick one network mode."
}

$machine = [System.Environment]::GetEnvironmentVariable("Path", "Machine")
$user = [System.Environment]::GetEnvironmentVariable("Path", "User")
$env:Path = "$machine;$user;C:\Program Files\Arduino CLI"

$sketchDir = Join-Path $PSScriptRoot "..\firmware\esp32cam_pir"
$secrets = Join-Path $sketchDir "secrets.h"
if (-not (Test-Path $secrets)) {
    throw "Missing $secrets. Run scripts/setup_arduino.ps1 first and set your Wi-Fi credentials."
}

$buildProps = @()
if ($SoftAP) {
    Write-Host "==> SoftAP mode: CAM hosts its own private hotspot at 192.168.4.1" -ForegroundColor Yellow
    $buildProps += "--build-property"
    $buildProps += "compiler.cpp.extra_flags=-DUSE_SOFTAP=1"
}
elseif ($Eduroam) {
    Write-Host "==> Enterprise mode: CAM joins eduroam using EAP_* in secrets.h" -ForegroundColor Yellow
    $buildProps += "--build-property"
    $buildProps += "compiler.cpp.extra_flags=-DUSE_ENTERPRISE=1"
}

if (-not $Port) {
    Write-Host "==> Compiling only (profile: esp32cam)..." -ForegroundColor Cyan
    arduino-cli compile --profile esp32cam @buildProps $sketchDir
    Write-Host "Compile OK. Pass -Port COMx to upload." -ForegroundColor Green
    exit 0
}

Write-Host "==> Compiling (profile: esp32cam)..." -ForegroundColor Cyan
arduino-cli compile --profile esp32cam @buildProps $sketchDir
if ($LASTEXITCODE -ne 0) { throw "compile failed" }

Write-Host "==> Uploading to $Port @ $Baud ..." -ForegroundColor Cyan
Write-Host "    GPIO0->GND, then tap CAM RESET during 'Connecting...'. Hands off after that." -ForegroundColor Yellow
arduino-cli upload --profile esp32cam -p $Port --upload-property "upload.speed=$Baud" $sketchDir
if ($LASTEXITCODE -ne 0) { throw "upload failed" }
Write-Host "Upload complete. Remove GPIO0->GND and tap RESET to run." -ForegroundColor Green

if ($Monitor) {
    Write-Host "==> Serial monitor @115200 (Ctrl+C to exit)..." -ForegroundColor Cyan
    arduino-cli monitor -p $Port -c baudrate=115200
}
