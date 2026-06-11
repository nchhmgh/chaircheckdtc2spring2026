[CmdletBinding()]
param(
    [string]$EspCoreVersion = "3.3.8",
    [string]$BoardUrl = "https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json"
)

$ErrorActionPreference = "Stop"

function Add-ToolPaths {

    $machine = [System.Environment]::GetEnvironmentVariable("Path", "Machine")
    $user = [System.Environment]::GetEnvironmentVariable("Path", "User")
    $env:Path = "$machine;$user;C:\Program Files\Arduino CLI"
}

Write-Host "==> Checking for arduino-cli..." -ForegroundColor Cyan
Add-ToolPaths
if (-not (Get-Command arduino-cli -ErrorAction SilentlyContinue)) {
    Write-Host "    not found; installing via winget (ArduinoSA.CLI)" -ForegroundColor Yellow
    winget install --id ArduinoSA.CLI -e --accept-source-agreements --accept-package-agreements --disable-interactivity
    Add-ToolPaths
}
arduino-cli version

Write-Host "==> Configuring board manager index..." -ForegroundColor Cyan
if (-not (Test-Path (Join-Path $env:LOCALAPPDATA "Arduino15\arduino-cli.yaml"))) {
    arduino-cli config init
}
arduino-cli config add board_manager.additional_urls $BoardUrl
arduino-cli core update-index

Write-Host "==> Installing esp32:esp32@$EspCoreVersion core (large download)..." -ForegroundColor Cyan
arduino-cli core install "esp32:esp32@$EspCoreVersion"

Write-Host "==> Verifying ESP32-CAM board is available..." -ForegroundColor Cyan
arduino-cli board listall esp32 | Select-String "esp32cam"

$firmwareDir = Join-Path $PSScriptRoot "..\firmware"
foreach ($node in @("pir_node", "esp32cam_pir")) {
    $secrets = Join-Path $firmwareDir "$node\secrets.h"
    $example = Join-Path $firmwareDir "$node\secrets.h.example"
    if ((Test-Path $example) -and (-not (Test-Path $secrets))) {
        Copy-Item $example $secrets
        Write-Host "==> Created firmware/$node/secrets.h from template." -ForegroundColor Green
    }
}
Write-Host "    EDIT secrets.h before flashing:" -ForegroundColor Yellow
Write-Host "      - SoftAP (private):   AP_SSID / AP_PASS" -ForegroundColor DarkGray
Write-Host "      - home network:       WIFI_SSID / WIFI_PASS" -ForegroundColor DarkGray
Write-Host "      - eduroam (-Eduroam): EAP_IDENTITY / EAP_USERNAME / EAP_PASSWORD" -ForegroundColor DarkGray

Write-Host ""
Write-Host "Done. Next steps (Stage A: PIR first):" -ForegroundColor Green
Write-Host "  1. Edit firmware/pir_node/secrets.h (Wi-Fi credentials)."
Write-Host "  2. Flash:  pwsh -File scripts/flash_pir_node.ps1 -Port COM5 -Monitor"
Write-Host "  3. Test:   curl http://<device-ip>/pir   (wave at the PIR)"
Write-Host ""
Write-Host "Stage B (add the camera) later: scripts/flash_esp32cam.ps1 -SoftAP -Monitor" -ForegroundColor DarkGray
