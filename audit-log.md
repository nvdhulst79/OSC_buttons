# OSC_buttons — Code Audit

Ranked by severity. File/line references are relative to the sketch root.

## Critical

### 1. Blocking calls inside AsyncWebServer handlers ✅ RESOLVED
- [wifi_manager.cpp:211-214](wifi_manager.cpp#L211-L214): `/connect` handler blocks up to 10 seconds (20 × `delay(500)`).
- [wifi_manager.cpp:253](wifi_manager.cpp#L253): `/disconnect` calls `setupAccessPoint()`, which contains ~1.2 s of `delay()`.

ESPAsyncWebServer handlers run on a single async task — blocking them starves other requests and can trigger the async TCP watchdog. This is the worst offender in the codebase.

**Fix:** kick off the WiFi state change from `loop()` via a flag, and respond to the HTTP request immediately (e.g. "connecting…" + a separate `/status` poll).

### 2. `apShutdownTime` never re-armed after reconnect ✅ RESOLVED
- [wifi_manager.cpp:344-354](wifi_manager.cpp#L344-L354)

When the STA link comes back up in `updateConnectionStatus()`, `staConnected` is set true but `apShutdownTime` stays at 0. Result: once you lose STA and re-acquire it, the AP never shuts down again until the next reboot — defeating the power-saving path.

**Fix:** set `_state.apShutdownTime = millis() + 600000;` in the reconnect branch.

## High

### 3. Deep-sleep wake may not hold its pull-up ✅ RESOLVED
- [OSC_buttons.ino:103-105](OSC_buttons.ino#L103-L105)

On ESP32-C3, the internal pull-up set with `pinMode(..., INPUT_PULLUP)` is driven by the IO MUX, which is gated off in deep sleep. You should enable the RTC/digital pad pull-up and hold it explicitly before sleeping:

```cpp
gpio_pullup_en(GPIO_NUM_3);
gpio_pulldown_dis(GPIO_NUM_3);
gpio_hold_en(GPIO_NUM_3);
gpio_deep_sleep_hold_en();
```

Otherwise D1 can float and either fail to wake, wake spuriously, or draw extra current.

### 4. `connectToSavedWiFi()` blocks `setup()` for up to 10 seconds ✅ RESOLVED
- [wifi_manager.cpp:296-301](wifi_manager.cpp#L296-L301)

Nothing else runs during this period (no button handling, no portal serving). If the saved network is flaky, every cold boot stalls.

**Fix:** start the connection async and let `updateConnectionStatus()` in `loop()` observe when it completes.

### 5. Web handlers added after `_webServer.begin()` ✅ RESOLVED
- [wifi_manager.cpp:268](wifi_manager.cpp#L268) calls `_webServer.begin()` inside `initCaptivePortal()`.
- [OSC_buttons.ino:155](OSC_buttons.ino#L155) registers `/osc`, `/testosc` afterwards.

ESPAsyncWebServer tolerates this in most cases, but the `onNotFound` catch-all added earlier can intercept those routes depending on version. Safer to register all routes before `begin()`.

## Medium

### 6. Broadcast address hard-codes /24 ✅ RESOLVED
- [wifi_manager.cpp:219](wifi_manager.cpp#L219)
- [wifi_manager.cpp:305-306](wifi_manager.cpp#L305-L306)
- [wifi_manager.cpp:347-348](wifi_manager.cpp#L347-L348)
- [wifi_manager.cpp:424-425](wifi_manager.cpp#L424-L425)

`broadcast[3] = 255` is wrong on any subnet that isn't /24. Use `WiFi.broadcastIP()` or compute `ip | ~subnetMask`.

### 7. `millis() > _state.apShutdownTime` is not overflow-safe ✅ RESOLVED
- [wifi_manager.cpp:332](wifi_manager.cpp#L332)

Use `(int32_t)(millis() - _state.apShutdownTime) > 0`. Low practical risk for a battery device that reboots often, but still wrong.

### 8. Invalid custom target IP silently falls back to broadcast ✅ RESOLVED
- [osc_manager.cpp:217-223](osc_manager.cpp#L217-L223)

If the user types `192.168.1` or anything malformed, `fromString` fails and the code silently broadcasts instead — confusing because the UI still shows their bad IP. Reject on save, or surface the fallback in the status UI.

### 9. `/osc` GET doesn't echo button channels ✅ RESOLVED
- [osc_manager.cpp:82-86](osc_manager.cpp#L82-L86)

The POST endpoint accepts `button1Channel` / `button2Channel`, but the GET endpoint only returns `port` / `targetip` / `addressFormat`. The web UI can't round-trip the channel values.

### 10. Duplicate `MDNS.begin()` without `MDNS.end()` ✅ RESOLVED
- [wifi_manager.cpp:222-225](wifi_manager.cpp#L222-L225)
- [wifi_manager.cpp:351-354](wifi_manager.cpp#L351-L354)

Can call `MDNS.begin("osc-muis")` a second time if you connect via `/connect` and later reconnect. Some versions of ESPmDNS fail the second begin silently.

### 11. Boot-while-docked burns energy ✅ RESOLVED
- [OSC_buttons.ino:119-193](OSC_buttons.ino#L119-L193)

On wake, the full WiFi AP + saved-STA connect runs before the reed switch is checked at the top of `loop()`. Cheap fix: read `REED_SWITCH_PIN` immediately in `setup()` and sleep again before touching WiFi.

## Low

### 12. `WiFi.scanNetworks(true)` called while in AP-only mode
- [wifi_manager.cpp:271](wifi_manager.cpp#L271)

If `connectToSavedWiFi()` failed and reverted to `WIFI_AP`, the initial scan can fail. The `/scan` endpoint corrects this on demand, so the only symptom is an empty first scan.

### 13. `BIT(D1)` for deep-sleep mask
- [OSC_buttons.ino:103](OSC_buttons.ino#L103)

Works today (`BIT(n) = 1UL << n`), but `esp_deep_sleep_enable_gpio_wakeup` takes `uint64_t`. Use `1ULL << D1` to be explicit and future-proof.

### 14. `udp.begin(oscManager.getPort())` uses the port snapshot
- [OSC_buttons.ino:158](OSC_buttons.ino#L158)

If the OSC port is changed via the web UI, the local bind isn't updated. Harmless because you only send, but misleading if you ever add inbound listening.

### 15. `getBatteryPercent()` is a stub that always returns 100
- [OSC_buttons.ino:109-116](OSC_buttons.ino#L109-L116)

Known TODO; worth flagging since the portal will always look fully charged.

### 16. `enterDeepSleep()` doesn't explicitly tear down the AP
- [OSC_buttons.ino:99-100](OSC_buttons.ino#L99-L100)

Calls `WiFi.disconnect(true)` + `WiFi.mode(WIFI_OFF)` but not `WiFi.softAPdisconnect(true)`. Usually harmless (mode-off kills the AP too), but add it for clarity.

### 17. Debounce state not reset on wake
`lastButton1Interrupt` / `lastButton2Interrupt` are zeroed at boot, so the first post-wake press always passes. OK in practice, noted for completeness.
