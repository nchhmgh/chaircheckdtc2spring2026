[CmdletBinding()]
param(
    [string]$Port,
    [ValidateSet("esp32dev", "esp32cam")]
    [string]$Profile = "esp32dev",
    [int]$PirPin,
    [switch]$Monitor
)

$ErrorActionPreference = "Stop"

$machine = [System.Environment]::GetEnvironmentVariable("Path", "Machine")
$user = [System.Environment]::GetEnvironmentVariable("Path", "User")
$env:Path = "$machine;$user;C:\Program Files\Arduino CLI"

$sketchDir = Join-Path $PSScriptRoot "..\firmware\pir_node"
$secrets = Join-Path $sketchDir "secrets.h"
$example = Join-Path $sketchDir "secrets.h.example"
if (-not (Test-Path $secrets)) {
    if (Test-Path $example) {
        Copy-Item $example $secrets
        Write-Host "Created $secrets from template — edit it with your Wi-Fi credentials, then re-run." -ForegroundColor Yellow
    }
    throw "Missing Wi-Fi credentials in firmware/pir_node/secrets.h."
}

$buildArgs = @("compile", "--profile", $Profile, $sketchDir)
if ($PSBoundParameters.ContainsKey("PirPin")) {
    Write-Host "==> Overriding PIR pin -> GPIO$PirPin" -ForegroundColor Cyan
    $buildArgs += @("--build-property", "compiler.cpp.extra_flags=-DPIR_PIN=$PirPin")
}

Write-Host "==> Compiling pir_node (profile: $Profile)..." -ForegroundColor Cyan
arduino-cli @buildArgs

if (-not $Port) {
    Write-Host "Compile OK. Pass -Port COMx to upload." -ForegroundColor Green
    exit 0
}

Write-Host "==> Uploading to $Port ..." -ForegroundColor Cyan
if ($Profile -eq "esp32cam") {
    Write-Host "    (ESP32-CAM: hold GPIO0->GND and tap RESET to enter bootloader.)" -ForegroundColor Yellow
}
arduino-cli upload --profile $Profile -p $Port $sketchDir
Write-Host "Upload complete." -ForegroundColor Green

if ($Monitor) {
    Write-Host "==> Serial monitor @115200 (Ctrl+C to exit). Wave at the PIR to see MOTION events." -ForegroundColor Cyan
    arduino-cli monitor -p $Port -c baudrate=115200
}
