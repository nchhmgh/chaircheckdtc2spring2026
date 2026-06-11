# ChairCheck: ESP32-CAM + PIR hardware setup

ChairCheck is one real node: an **AI-Thinker ESP32-CAM** (OV2640 camera) plus a
**PIR motion sensor**. The node exposes everything a host needs over plain
HTTP — the camera as an MJPEG `/stream` and the PIR as a JSON `/pir` endpoint.

> Shell commands below are run from the repository root.

We bring the node up in two stages so each sensor is proven before the next is
added:

- **Stage A — PIR first** (`firmware/pir_node/`): a camera-free sketch that
  connects to Wi-Fi and serves only the `/pir`, `/status`, and `/healthz`
  routes. Runs on any ESP32 board (including the ESP32-CAM with the camera
  unused), so you can confirm the PIR wiring before touching the camera.
- **Stage B — camera + PIR** (`firmware/esp32cam_pir/`): the full ESP32-CAM
  sketch that adds the MJPEG `/stream` and `/capture` routes.

```
ESP32-CAM (firmware/esp32cam_pir)
  ├─ GET :81/stream  ──MJPEG──▶  any MJPEG client / person detector
  └─ GET :80/pir     ──JSON───▶  any HTTP poller (motion + event counter)
```

## 1. Hardware

| Part                         | Notes                                                    |
| ---------------------------- | -------------------------------------------------------- |
| AI-Thinker ESP32-CAM         | OV2640, PSRAM. No USB — needs an external serial bridge.  |
| PIR sensor (HC-SR501 etc.)   | 3-pin: VCC, OUT, GND. Digital HIGH on motion.            |
| Serial bridge                | A USB-TTL/FTDI adapter **or** a spare ESP32 DevKit (see below). |

**Wiring**

| PIR pin | ESP32-CAM pin |
| ------- | ------------- |
| VCC     | 5V            |
| GND     | GND           |
| OUT     | GPIO13        |

GPIO13 is a free header pin on the AI-Thinker board (it doubles as an SD data
line, which this firmware does not use). To change it, redefine `PIR_PIN` at the
top of the sketch.

**Optional motion-indicator LED**

The firmware flashes an LED (~4 Hz) on `LED_PIN` while the debounced PIR reports
motion, and holds it off when idle. Default `LED_PIN` is **GPIO12**. Wire it
*sourcing* so the pin stays LOW at boot (GPIO12 is a strapping pin):

| LED leg            | Connect to                          |
| ------------------ | ----------------------------------- |
| Anode (+, long)    | GPIO12 via a 220–330 ohm resistor   |
| Cathode (-, short) | GND                                 |

Set `LED_PIN` to `-1` to disable, or redefine it for a different pin. **Do not**
wire the LED from the GPIO to 3.3V (sinking) — that pulls the strapping pin HIGH
at boot and can stop the board from booting.

Zero-firmware alternative: tie the LED (through a 220–330 ohm resistor) directly
from the PIR `OUT` line to GND. It then lights steadily during each detection
instead of flashing, with no code change.

For flashing, the AI-Thinker board enters bootloader mode when **GPIO0 is tied
to GND** at reset.

**Option A — USB-TTL/FTDI adapter.** Wire `5V→5V`, `GND→GND`, `TX→U0R (GPIO3)`,
`RX→U0T (GPIO1)`. Bridge `GPIO0→GND`, tap RESET, then upload. Remove the GPIO0
jumper and reset to run.

**Option B — spare ESP32 DevKit as a USB-serial bridge.** If you have no FTDI
adapter, an ESP32 DevKit can act as one (this is what this node was validated
with). Hold the DevKit's own chip in reset so only its USB-UART bridge is active,
then cross the UART lines to the CAM:

| DevKit pin | ESP32-CAM pin | Why                                    |
| ---------- | ------------- | -------------------------------------- |
| `EN`       | `GND`         | Disables the DevKit's ESP32 so it stays off the UART. |
| `5V`       | `5V`          | Power the CAM.                         |
| `GND`      | `GND`         | Common ground.                         |
| `TX (GPIO1)` | `U0R (GPIO3)` | DevKit TX → CAM RX.                   |
| `RX (GPIO3)` | `U0T (GPIO1)` | DevKit RX ← CAM TX.                   |

Then bridge the CAM's `GPIO0→GND`, start the upload, and **tap the CAM RESET
button once during the "Connecting..." phase** so it latches into the
bootloader. Keep hands off the board after that until the write finishes —
mistimed resets or power dips over jumper wires cause "chip stopped responding".
Flashing at **115200 baud** (the script default) is far more reliable over
jumper wires than the 460800 default.

> **Windows driver:** the DevKit's CP2102 bridge needs Silicon Labs' CP210x
> driver. If Device Manager shows the COM port with a yellow `!` (problem code
> 28), install the `silabser.inf` driver (e.g. `pnputil /add-driver silabser.inf
> /install` from an elevated prompt) before flashing.

## 2. One-time toolchain setup

The Arduino CLI and the pinned ESP32 core (`esp32:esp32@3.3.8`) are installed by:

```powershell
pwsh -File scripts/setup_arduino.ps1
```

This installs `arduino-cli` (via winget if missing), registers the Espressif
board index, installs the core, and creates `secrets.h` from the templates for
both sketches. The board profiles are pinned in each sketch's `sketch.yaml` so
every build is reproducible.

> Doing it by hand instead:
>
> ```powershell
> winget install --id ArduinoSA.CLI -e
> arduino-cli config init
> arduino-cli config add board_manager.additional_urls `
>   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
> arduino-cli core update-index
> arduino-cli core install esp32:esp32@3.3.8
> ```

## 3. Stage A — flash and test the PIR alone

The PIR-only sketch needs no camera, so it works on a bare ESP32 dev board or
the ESP32-CAM.

1. Edit `firmware/pir_node/secrets.h` with your Wi-Fi SSID/password.
2. Compile + upload + watch the serial monitor:

```powershell
# Bare ESP32 dev board (USB):
pwsh -File scripts/flash_pir_node.ps1 -Port COM5 -Monitor

# ESP32-CAM (no USB): add -Profile esp32cam and use the GPIO0->GND bootloader dance.
pwsh -File scripts/flash_pir_node.ps1 -Port COM5 -Profile esp32cam -Monitor
```

Wave a hand at the PIR. The serial monitor prints `MOTION event #N` lines and
`http://<device-ip>/pir` returns `{"motion":1,"events":N,...}`. The default PIR
pin is **GPIO13**; override it with `-PirPin <gpio>` (e.g. `-PirPin 27` on a bare
dev board).

## 4. Stage B — configure and flash the camera firmware

The camera firmware supports three network modes, selected at flash time. All
credentials live in the gitignored `firmware/esp32cam_pir/secrets.h` (copied
from `secrets.h.example` by `setup_arduino.ps1`).

| Mode | Flash flag | When to use |
| ---- | ---------- | ----------- |
| **SoftAP** | `-SoftAP` | Most private. CAM hosts its own closed hotspot; PC joins it directly. Isolated from any campus/public network. |
| **WPA2-Personal** | *(none)* | A home/router network. CAM gets a DHCP address on your LAN. |
| **WPA2-Enterprise / eduroam** | `-Eduroam` | Campus networks that need a username + password (PEAP/MSCHAPv2). |

> **Security note.** The CAM's HTTP endpoints are **unauthenticated**. On SoftAP
> only devices with your AP password can reach them, and the video never touches
> any other network — that's why SoftAP is the recommended private option. On a
> shared network (eduroam/public), anyone who can route to the CAM's IP could
> view the stream, so prefer SoftAP unless you specifically need LAN reachability.

### 4a. SoftAP — most private, recommended

Set a strong `AP_SSID` / `AP_PASS` in `secrets.h` (WPA2 needs >= 8 chars), then:

```powershell
# -SoftAP adds -DUSE_SOFTAP=1; -Port is the bridge/adapter COM port.
pwsh -File scripts/flash_esp32cam.ps1 -Port COM3 -SoftAP -Monitor
```

The monitor prints `SoftAP up ssid="<your-ap>" ip=192.168.4.1` (the password is
deliberately not echoed). Remove the `GPIO0→GND` jumper, tap CAM RESET, then on
your PC join that Wi-Fi and open <http://192.168.4.1/> — you should see the live
stream. (While joined to the hotspot your PC has no internet; that's expected.)

### 4b. WPA2-Personal STA

1. Set `WIFI_SSID` / `WIFI_PASS` in `secrets.h`.
2. Flash with no network flag (find the COM port in Device Manager):

```powershell
pwsh -File scripts/flash_esp32cam.ps1 -Port COM3 -Monitor
```

`-Monitor` opens the serial monitor at 115200 baud, where the firmware prints
the DHCP IP and the stream/PIR URLs. Note that IP for section 5.

### 4c. WPA2-Enterprise / eduroam

eduroam is per-user authenticated and encrypted (better than an open SSID), but
read the caveats below before relying on it.

1. Fill in `EAP_IDENTITY`, `EAP_USERNAME`, and `EAP_PASSWORD` in `secrets.h`
   (usually all `you@your-domain.edu`). Default method is PEAP/MSCHAPv2; for a
   TTLS/PAP campus add `#define EAP_METHOD WPA2_AUTH_TTLS`.
2. Flash with `-Eduroam`:

```powershell
pwsh -File scripts/flash_esp32cam.ps1 -Port COM3 -Eduroam -Monitor
```

The monitor prints `connecting to enterprise SSID "eduroam" as "you@..."` and
then the DHCP IP once associated.

**eduroam caveats (important):**

- **Client isolation.** Many eduroam deployments block device-to-device traffic,
  so your laptop may *not* be able to reach the CAM's IP even when both are
  connected. If you can't fetch the stream, this is the likely cause — fall back
  to SoftAP (4a).
- **2.4 GHz only.** The ESP32 radio is 2.4 GHz; it will associate with eduroam's
  2.4 GHz BSSIDs (most campuses broadcast both bands).
- **No server-cert validation.** This firmware connects without a CA cert (the
  common eduroam case). That trusts the RADIUS server; add a CA cert via the
  `WiFi.begin(..., ca_pem, ...)` overload if your IT requires it.
- **Unauthenticated endpoints** are exposed to anyone on the network who can
  route to the CAM (see the security note above).

## 5. HTTP endpoints

The firmware runs **two** HTTP servers. The MJPEG stream is isolated on its own
port (81) because its handler is a blocking loop — on a single server it would
starve `/pir` and `/status` while a client is connected, which would break
reading the camera and the PIR at the same time. Control endpoints stay on
port 80:

| Port | Route       | Purpose                                                     |
| ---- | ----------- | ----------------------------------------------------------- |
| 80   | `/`         | HTML status page (embeds the stream from port 81).          |
| 80   | `/pir`      | JSON `{"motion":0\|1,"events":N,"last_motion_ms":..,"uptime_ms":..}`. |
| 80   | `/capture`  | Single JPEG still.                                          |
| 80   | `/status`   | JSON device/camera/PIR snapshot.                            |
| 80   | `/healthz`  | `ok` liveness probe.                                        |
| 81   | `/stream`   | `multipart/x-mixed-replace` MJPEG (the video source).       |

Capture resolution defaults to **SVGA (800x600)**; override at build time with
`-DCAM_FRAMESIZE=FRAMESIZE_HD` (or `FRAMESIZE_UXGA`) for sharper video.

### Verify the device

```powershell
# Liveness + a JSON PIR sample (replace with the device IP, or 192.168.4.1 on SoftAP).
curl http://192.168.4.1/healthz
curl http://192.168.4.1/pir
curl http://192.168.4.1/status

# Grab a still, or open the live stream / status page in a browser.
#   http://192.168.4.1/capture
#   http://192.168.4.1:81/stream
#   http://192.168.4.1/
```

The `/pir` payload exposes an instantaneous `motion` flag plus a monotonic
`events` counter, so a poller can detect motion either by seeing `motion:1` on a
poll or by seeing the `events` counter advance between polls. The PIR-only
(Stage A) sketch serves a byte-for-byte identical `/pir` payload, so anything
written against one build works against the other.

## 6. Firmware reference

| File                                      | Role                                            |
| ----------------------------------------- | ----------------------------------------------- |
| `firmware/pir_node/pir_node.ino`          | Stage A: PIR-only firmware (any ESP32 board).   |
| `firmware/pir_node/sketch.yaml`           | Stage A pinned board profiles.                  |
| `firmware/esp32cam_pir/esp32cam_pir.ino`  | Stage B: camera + PIR + HTTP server firmware.   |
| `firmware/esp32cam_pir/sketch.yaml`       | Stage B pinned board profiles (`esp32cam`, `esp32dev`). |
| `firmware/*/secrets.h.example`            | Wi-Fi credential template (copy to `secrets.h`).|
| `scripts/setup_arduino.ps1`               | Install + configure the Arduino toolchain.      |
| `scripts/flash_pir_node.ps1`              | Compile / upload / monitor the PIR-only sketch. |
| `scripts/flash_esp32cam.ps1`              | Compile / upload / monitor the camera sketch.   |

Build-time knobs (override with `--build-property compiler.cpp.extra_flags=-D<NAME>=<value>`):
`PIR_PIN`, `PIR_DEBOUNCE_MS`, `LED_PIN`, `LED_BLINK_MS`, `HTTP_PORT`,
`STREAM_PORT`, `CAM_FRAMESIZE`, `USE_SOFTAP`, `USE_ENTERPRISE`, `EAP_METHOD`.
