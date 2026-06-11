# ChairCheck

ChairCheck is a small device that tells you whether a space is being used. It is
a tiny camera board (an AI-Thinker ESP32-CAM) connected to a motion sensor (a
PIR sensor). Once it is running, it puts a live video feed and a simple
"motion / no motion" reading onto a web page that you can open in any browser on
the same Wi-Fi.

This download contains everything you need to get the device running:

- The program that runs on the device (called the firmware).
- Helper scripts that install the required tools and copy the program onto the
  device for you, so you do not have to do those steps by hand.

You do not need to understand how the device works inside to follow this guide.
Just go through the steps in order.

## What you need

| Part | What it is |
| ---- | ---------- |
| AI-Thinker ESP32-CAM | A small camera board. Important: it has no USB port, so you cannot plug it straight into your computer. You connect it through a separate adapter (see the next row). |
| PIR motion sensor (for example, an HC-SR501) | A small sensor with three pins (power, ground, and a signal pin) that goes high when it detects movement. |
| A serial adapter | A small part that lets your computer talk to the camera board over USB. You can use a USB-to-TTL (also called FTDI) adapter, or a spare ESP32 DevKit board used as a bridge. |
| A Windows computer | The setup scripts in this download are written for Windows PowerShell. |

You connect the motion sensor to the camera board like this: power pin to 5V,
ground pin to GND, and the signal pin to the pin labeled GPIO13. There is an
optional status light you can add on pin GPIO12. The full wiring picture, the
adapter wiring, and the button-press steps for loading the program are in
[docs/integration.md](docs/integration.md). Read that file before you wire
anything.

## What is in this download

```
chaircheck/
  README.md                 this file
  docs/integration.md       full wiring, connection, and setup details
  firmware/
    pir_node/               the motion-sensor-only program (Stage A)
    esp32cam_pir/           the full camera plus motion-sensor program (Stage B)
  scripts/
    setup_arduino.ps1       installs the build tools for you (run this once)
    flash_pir_node.ps1      loads the motion-sensor-only program onto the device
    flash_esp32cam.ps1      loads the full camera program onto the device
```

## How to set it up

You set the device up in two stages. Stage A proves the motion sensor works on
its own. Stage B adds the camera. Doing it in this order makes problems easy to
find: if something is wrong, you know which part to check.

In the commands below, `COM5` is the name Windows gives to your serial adapter.
Your number may be different. To find yours, open Device Manager, expand "Ports
(COM & LPT)", and read the COM number next to your adapter. Replace `COM5` with
that number.

### Step 1: install the tools (do this once)

Open PowerShell, move into this folder, and run the setup script:

```powershell
pwsh -File scripts/setup_arduino.ps1
```

This installs the software needed to build and load the program. It also creates
two settings files (named `secrets.h`) from templates. You will put your Wi-Fi
information into those files in the next step.

### Step 2: enter your Wi-Fi details

Open the settings files that were just created and fill in your Wi-Fi name and
password:

- `firmware/pir_node/secrets.h` for Stage A. Fill in `WIFI_SSID` (your Wi-Fi
  name) and `WIFI_PASS` (your Wi-Fi password).
- `firmware/esp32cam_pir/secrets.h` for Stage B. This file has settings for
  three different ways to connect. You only fill in the one you plan to use.
  The three ways are explained in Step 4.

These settings files stay on your computer and are never uploaded to GitHub, so
your Wi-Fi password stays private.

### Step 3 (Stage A): load and test the motion sensor

Run this command, replacing `COM5` with your port number:

```powershell
pwsh -File scripts/flash_pir_node.ps1 -Port COM5 -Monitor
```

The `-Monitor` part opens a text window that shows messages from the device.
Wave your hand in front of the motion sensor. You should see the word `MOTION`
appear in that window each time it detects movement. You can also open
`http://<device-ip>/pir` in a browser, where `<device-ip>` is the address the
device prints in that text window. It will return a short line of text that
shows `1` when there is motion and `0` when there is none.

### Step 4 (Stage B): load the full camera program

Now you choose how the device connects to Wi-Fi. There are three choices. Pick
one and run only its command (replace `COM5` with your port number).

Choice 1: the device makes its own private Wi-Fi. This is the most private
option, and it is the easiest one to start with. After it starts, you join the
device's Wi-Fi from your computer and open `http://192.168.4.1/`.

```powershell
pwsh -File scripts/flash_esp32cam.ps1 -Port COM5 -SoftAP -Monitor
```

Choice 2: the device joins your normal home or office Wi-Fi. Use this when you
filled in `WIFI_SSID` and `WIFI_PASS`.

```powershell
pwsh -File scripts/flash_esp32cam.ps1 -Port COM5 -Monitor
```

Choice 3: the device joins a school or campus Wi-Fi that asks for a username and
password (for example, eduroam). Use this when you filled in the `EAP_` settings.

```powershell
pwsh -File scripts/flash_esp32cam.ps1 -Port COM5 -Eduroam -Monitor
```

After the program loads, the device prints its web address in the text window.
Open that address in a browser to see the live video and the motion status.

## What you can open in a browser

Once the device is running, open these addresses in any browser on the same
network. Replace `192.168.4.1` with your device's address if it is different
(`192.168.4.1` is the address when you use Choice 1 above).

- `http://192.168.4.1/` shows the main page with the live video and the current
  motion status.
- `http://192.168.4.1/capture` shows a single still photo.
- `http://192.168.4.1:81/stream` shows only the live video.

The device also answers a few text-only addresses (`/pir`, `/status`, and
`/healthz`) that other programs can read. You do not need these to use the
device by hand.

## A note on privacy and safety

The device's web pages have no password on them. When you use Choice 1 (the
device's own private Wi-Fi), only people who know that Wi-Fi password can see the
video, and the video never leaves the device's own network. This is why Choice 1
is the most private. If you put the device on a shared network instead, anyone on
that network who knows its address could view the video. For that reason, use
Choice 1 unless you have a specific reason to put it on a shared network. More
detail is in [docs/integration.md](docs/integration.md).
