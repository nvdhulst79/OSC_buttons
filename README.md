# OSC-Muis

A wireless OSC button controller built on the Seeed XIAO ESP32-C3. Designed to trigger sounds in [LuPlayer](https://luplayer.org/) (Keyboard Mapped or Eight Faders mode) via OSC over WiFi.

## What it does

Press a physical button, and an OSC message is sent over WiFi to a target application (e.g. LuPlayer running on a PC). The device runs its own WiFi access point with a captive portal for configuration — no need to edit code to change settings.

I gutted and modified a cheap computer mouse in order to have an actor discretely trigger some sound effects while on stage. I reused the mouse buttons for the triggers and the USB cable to power the ESP32.

## Features

- 2 button inputs with hardware interrupt-driven, zero-lag response
- Configurable OSC target IP, port, mode, and button channels via web interface
- LuPlayer mode presets: Keyboard Mapped, Eight Faders, or custom format
- Independent channel configuration for each button (e.g., button 1 → channel 5, button 2 → channel 7)
- Automatic broadcasting to multiple networks when in AP + Station mode
- Built-in WiFi access point with captive portal
- Automatic AP shutdown after 10 minutes when connected to WiFi, switching to power-saving STA-only mode with modem sleep
- AP automatically recovers if the WiFi connection is lost
- Optional connection to an existing WiFi network (AP + Station mode) with live connect progress in the UI
- mDNS support: access the web interface at `http://osc-muis.local` when connected to a WiFi network
- Test button in the web interface to verify OSC connectivity
- Live button + battery status in the web UI, pushed via Server-Sent Events (no polling)
- Calibrated LiPo battery level (piecewise curve + smoothing) — requires external voltage divider, see below
- On-demand deep sleep from the web UI ("Sleep Now" button); wake on button press
- Optional dock-based deep sleep via reed switch + magnet (disabled by default, see below)
- All settings persist across reboots in flash memory

## Hardware

### Required

- Seeed XIAO ESP32-C3
- 1 or 2 momentary push buttons (normally open)

### Optional

- NO (normally open) reed switch + neodymium magnet in the charging dock (for automatic dock-sleep — see below)
- 2× 220k resistor voltage divider on A0 (for battery monitoring)

### Wiring

The buttons and reed switch connect between their designated pins and GND. Internal pull-up resistors are used, so no external resistors are needed.

```
Button 1:    D1 (GPIO3) ---[button NO]------- GND
Button 2:    D2 (GPIO4) ---[button NO]------- GND
Reed switch: D3 (GPIO5) ---[reed switch NO]-- GND
```

### Deep sleep

The device can enter deep sleep two ways:

1. **From the web UI** — click **Sleep Now** in the Power section of the captive portal.
2. **Automatically when docked** — via an optional reed switch on D3 (disabled by default, see below).

In either case, waking is done by pressing button 1 (D1). This triggers a full reboot — WiFi reconnects in 2-3 seconds. The first button press is consumed by the wake and does not send an OSC command.

Button pad pull-ups are held across deep sleep (`gpio_hold_en`) so D1 stays high and reliably detects the wake press on ESP32-C3.

### Dock detection (optional, disabled by default)

A normally open (NO) reed switch on D3 can detect when the device is placed on its charging dock (which contains a neodymium magnet). When the magnet closes the reed switch, D3 is pulled LOW and the device enters deep sleep after 500 ms. On boot, the reed switch is checked immediately — before WiFi is brought up — so a dock-while-powered cycle doesn't waste ~10 s of setup.

**This feature is disabled by default** (`REED_SENSOR_ENABLED = false` in `OSC_buttons.ino`). An unconnected or loosely wired D3 acts as an antenna and picks up noise from nearby USB chargers, which can falsely trigger deep sleep. Only enable it once the reed switch is actually installed. For extra robustness, consider adding an external 10k pull-up from D3 to 3.3V and/or increasing `DOCK_DEBOUNCE_MS`.

A NO switch is used so that a broken wire (pin stays HIGH via pull-up) keeps the device awake rather than sleeping — prioritizing reliability during performance over battery life in storage.

### Battery monitoring (optional)

Connect a 2× 220k voltage divider from the battery to pin A0. The firmware reads `analogReadMilliVolts(A0)` (which applies the chip's factory ADC calibration), averages 16 samples, smooths with an exponential moving average, and maps the result to a percentage through a piecewise-linear single-cell LiPo discharge curve. If no divider is installed, the reading will report ~0% — the live level is pushed to the web UI every 10 seconds.

## Software setup

### Arduino IDE

1. Install the ESP32 board package (Espressif Systems) via Boards Manager
2. Select board: **XIAO_ESP32C3**
3. Install the following libraries via Library Manager:
   - **OSC** by Adrian Freed / CNMAT
   - **ESPAsyncWebServer**
   - **AsyncTCP** (dependency of ESPAsyncWebServer)

### Upload

Connect the XIAO ESP32-C3 via USB-C and upload the sketch.

## Usage

### First-time setup

1. Power on the device
2. Connect to the WiFi network **OSC-MUIS** (password: `oscbuttons`)
3. A captive portal page opens automatically
4. Optionally connect the device to your local WiFi network (so it's on the same network as the PC running LuPlayer)
5. Configure OSC settings:
   - **Target IP**: The IP address of the PC running LuPlayer (leave empty for broadcast)
   - **Port**: `8001` (LuPlayer default incoming port)
   - **Mode**: Select from dropdown:
     - **Keyboard Mapped** (`/kmpushX`) - Default for LuPlayer keyboard mapped mode
     - **Eight Faders** (`8faderspushX`) - For LuPlayer eight faders mode
     - **Custom** - Enter your own OSC address format
   - **Button Channels**: Configure which channel each physical button triggers (default: 1 and 2)

### LuPlayer configuration

1. Open LuPlayer and switch to **Keyboard Mapped** or **Eight Faders** mode (match your OSC-Muis mode selection)
2. Enable OSC control in LuPlayer settings
3. Ensure the incoming OSC port matches the port configured on OSC-Muis (default: 8001)
4. Configure LuPlayer channels to match your button channel settings (default: channels 1 and 2)
5. If using Windows, allow UDP traffic on the configured port in Windows Firewall

### Testing

Use the **Test Button 1** in the captive portal to send a test OSC message. Install an OSC monitor like [Protokol](https://hexler.net/protokol) on the PC to verify messages are arriving.

The **Buttons** panel in the portal shows the live state of both physical buttons — useful for verifying wiring without sending OSC. State is pushed over Server-Sent Events (`/events`), so there's no polling overhead. The battery percentage in the header updates the same way.

### Button channel mapping

Each physical button can trigger any channel number (1-99). This is useful when you want to:
- Skip certain channels (e.g., button 1 → channel 5, button 2 → channel 6)
- Use non-sequential channels (e.g., button 1 → channel 1, button 2 → channel 10)
- Match specific sound positions in LuPlayer

**Example**: If Button 1 Channel is set to `5` in Keyboard Mapped mode, pressing physical button 1 will send `/kmpush5`.

## Troubleshooting

- **No response from LuPlayer**: Verify both devices are on the same network. Try setting a specific target IP instead of broadcast. Check Windows Firewall.
- **Double triggers**: The debounce cooldown is set to 800ms. Adjust `DEBOUNCE_MS` in the sketch if needed.
- **Can't find the captive portal**: Connect to the OSC-MUIS WiFi network and navigate to `192.168.4.1` in a browser.
- **AP + Station mode**: When connected to both its own AP network and an external WiFi network, OSC messages are automatically broadcast to both networks. Check the "Test Button 1" response to see all target IPs.

## File structure

| File | Description |
|------|-------------|
| `OSC_buttons.ino` | Main sketch: button handling, setup/loop |
| `wifi_manager.h` | WiFi manager class definition and configuration structs |
| `wifi_manager.cpp` | WiFi AP/STA management, captive portal, network handling |
| `osc_manager.h` | OSC manager class definition for OSC protocol handling |
| `osc_manager.cpp` | OSC message formatting, broadcasting, settings storage |
| `portal_html.h` | Captive portal HTML/CSS/JS (stored in PROGMEM) |