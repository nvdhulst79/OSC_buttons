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
- Optional connection to an existing WiFi network (AP + Station mode)
- Test button in the web interface to verify OSC connectivity
- Battery level display (requires external voltage divider, see below)
- All settings persist across reboots in flash memory

## Hardware

### Required

- Seeed XIAO ESP32-C3
- 1 or 2 momentary push buttons (normally open)

### Wiring

The buttons connect between the designated pins and GND. Internal pull-up resistors are used, so no external resistors are needed.

```
Button 1: D1 (GPIO3) ---[button]--- GND
Button 2: D2 (GPIO4) ---[button]--- GND
```

### Battery monitoring (optional)

Battery reading is stubbed out in the code. To enable it, connect a voltage divider from your battery to pin A0 and uncomment the ADC reading in `getBatteryPercent()`.

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