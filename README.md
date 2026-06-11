# ChairCheck

Firmware and flashing tools for the **ChairCheck** occupancy node: an
**AI-Thinker ESP32-CAM** (OV2640 camera) plus a **PIR motion sensor**. The node
exposes everything a host needs over plain HTTP — an MJPEG camera stream and a
JSON motion endpoint — so any client on the network can read presence and video.

This repository is **hardware setup only**: firmware, a pinned Arduino toolchain,
and PowerShell flashing scripts. We bring the node up in two stages so each
sensor is proven before the next is added:

1. **Stage A — PIR first.** Flash the PIR-only sketch, confirm motion over serial
   and HTTP. Works on any ESP32 board.
2. **Stage B — add the camera.** Flash the full ESP32-CAM sketch (MJPEG stream +
   PIR) and view the live node page.

```
chaircheck/
  README.md                 this file
  docs/integration.md       full wiring, flashing, endpoints, network modes
  firmware/
    pir_node/               Stage A: PIR-only sketch (any ESP32 board)
    esp32cam_pir/           Stage B: ESP32-CAM + PIR sketch
  scripts/
    setup_arduino.ps1       install + configure arduino-cli and the ESP32 core
    flash_pir_node.ps1      compile / upload / monitor the PIR-only sketch
    flash_esp32cam.ps1      compile / upload / monitor the ESP32-CAM sketch
```

## Hardware

| Part                       | Notes                                                   |
| -------------------------- | ------------------------------------------------------- |
| AI-Thinker ESP32-CAM       | OV2640, PSRAM. No USB — needs an external serial bridge. |
| PIR sensor (HC-SR501 etc.) | 3-pin: VCC, OUT, GND. Digital HIGH on motion.           |
| Serial bridge              | A USB-TTL/FTDI adapter **or** a spare ESP32 DevKit.     |

**Wiring:** PIR `VCC → 5V`, `GND → GND`, `OUT → GPIO13`. Optional motion LED on
GPIO12 (wired *sourcing*). Full wiring, the bootloader steps, and the
DevKit-as-programmer flow are in [docs/integration.md](docs/integration.md).

## Quickstart

```powershell
# 1. one-time: install + configure arduino-cli and the pinned ESP32 core,
#    and create secrets.h from the templates.
pwsh -File scripts/setup_arduino.ps1

# 2. set Wi-Fi credentials in the generated secrets.h files.
#    firmware/pir_node/secrets.h        (WIFI_SSID / WIFI_PASS)
#    firmware/esp32cam_pir/secrets.h    (AP_*, WIFI_*, and/or EAP_*)
```

### Stage A — PIR only

```powershell
pwsh -File scripts/flash_pir_node.ps1 -Port COM5 -Monitor
```

Wave a hand at the PIR: the serial monitor prints `MOTION` events and
`http://<device-ip>/pir` returns `{"motion":1,"events":N,...}`.

### Stage B — camera + PIR

Pick a network mode at flash time:

```powershell
# Most private: CAM hosts its own hotspot (AP_SSID/AP_PASS) at 192.168.4.1.
# Join that Wi-Fi from your PC and open http://192.168.4.1/
pwsh -File scripts/flash_esp32cam.ps1 -Port COM3 -SoftAP -Monitor

# Home/router network (WIFI_SSID/WIFI_PASS): no network flag.
pwsh -File scripts/flash_esp32cam.ps1 -Port COM3 -Monitor

# eduroam / WPA2-Enterprise (EAP_* in secrets.h): -Eduroam.
pwsh -File scripts/flash_esp32cam.ps1 -Port COM3 -Eduroam -Monitor
```

> The AI-Thinker ESP32-CAM has no USB. Flash it through a USB-TTL/FTDI adapter
> or a spare ESP32 DevKit used as a serial bridge — see
> [docs/integration.md](docs/integration.md) for the wiring and the GPIO0→GND
> bootloader steps.

## HTTP endpoints

The camera firmware runs **two** HTTP servers. The MJPEG stream is isolated on
its own port (81) because its handler is a blocking loop — on a single server it
would starve the control endpoints while a client is connected. Control routes
stay on port 80.

| Port | Route      | Purpose                                                |
| ---- | ---------- | ------------------------------------------------------ |
| 80   | `/`        | HTML status page (embeds the stream from port 81).     |
| 80   | `/pir`     | JSON `{"motion":0\|1,"events":N,"last_motion_ms":..}`.  |
| 80   | `/capture` | Single JPEG still.                                     |
| 80   | `/status`  | JSON device/camera/PIR snapshot.                       |
| 80   | `/healthz` | `ok` liveness probe.                                   |
| 81   | `/stream`  | `multipart/x-mixed-replace` MJPEG (the video source).  |

The PIR-only (Stage A) sketch serves the same `/`, `/pir`, `/status`, and
`/healthz` routes, with a byte-for-byte identical `/pir` payload — so a host
written against either build works against the other.

## Board profiles

Pinned in each sketch's `sketch.yaml` (ESP32 core `esp32:esp32@3.3.8`) so every
flash is reproducible:

- `esp32cam` — AI-Thinker ESP32-CAM (`esp32:esp32:esp32cam`), the target board.
- `esp32dev` — generic ESP32 dev module (`esp32:esp32:esp32`), for the PIR-only
  stage on a bare board.

## Security note

The device HTTP endpoints are **unauthenticated**. In SoftAP mode only devices
with your AP password can reach them and the video never leaves the node's own
hotspot, which is why SoftAP is the recommended private option. On a shared
network anyone who can route to the device IP could view the stream — see
[docs/integration.md](docs/integration.md) §4.
