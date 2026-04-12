/*
 * OSC-Muis - Niels van der Hulst 2026
 *
 * ESP32-C3 OSC Button Controller
 * Target: Seeed XIAO ESP32C3
 *
 * Sends OSC messages when buttons are pressed.
 * Includes captive portal for WiFi configuration and battery status display.
 *
 * Required libraries:
 * - OSC by Adrian Freed / CNMAT (install via Library Manager)
 * - ESPAsyncWebServer (install via Library Manager or GitHub)
 * - AsyncTCP (required by ESPAsyncWebServer, install from GitHub)
 */

#include <WiFiUdp.h>
#include <OSCMessage.h>
#include <ESPAsyncWebServer.h>
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "wifi_manager.h"
#include "osc_manager.h"

// === Configuration ===
// OSC port is now configurable via web interface (default: 8001 for LuPlayer)

// Button pins (directly next to 5V and GND on XIAO ESP32C3)
const int BUTTON_1_PIN = D1;  // GPIO3
const int BUTTON_2_PIN = D2;  // GPIO4

// Reed switch pin — NO switch between D3 and GND
// No magnet (in use): switch open → HIGH (pull-up) → normal operation
// Magnet present (on dock): switch closes → LOW → deep sleep
const int REED_SWITCH_PIN = D3;  // GPIO5

// Set to false to disable dock detection when the reed sensor is not installed.
// An unconnected pin (or open switch with wire) can pick up noise from the
// charger and falsely trigger deep sleep — the wire acts as an antenna.
// When re-enabling, consider these countermeasures:
//   1. Add a 10k external pull-up resistor from D3 to 3.3V (much stronger than
//      the internal ~45k, making noise-induced LOW readings very unlikely)
//   2. Increase DOCK_DEBOUNCE_MS to 2000-3000ms (a real magnet easily sustains
//      LOW that long, but noise won't)
//   3. Both — belt and suspenders
const bool REED_SENSOR_ENABLED = false;

// Debounce settings
// This is a cooldown after the initial press — the ISR fires instantly (no lag),
// but then ignores all edges (including release bounce) for this duration.
const unsigned long DEBOUNCE_MS = 800;
const unsigned long DOCK_DEBOUNCE_MS = 500;  // Reed switch debounce for dock detection

// === Global variables ===
WiFiUDP udp;
WiFiManager wifiManager;
OSCManager oscManager;
AsyncEventSource events("/events");  // SSE endpoint — replaces HTTP polling

volatile bool button1Pressed = false;
volatile bool button2Pressed = false;
volatile unsigned long lastButton1Interrupt = 0;
volatile unsigned long lastButton2Interrupt = 0;

// === Interrupt handlers (with debounce) ===
void IRAM_ATTR onButton1Press() {
    unsigned long now = millis();
    if (now - lastButton1Interrupt > DEBOUNCE_MS) {
        button1Pressed = true;
        lastButton1Interrupt = now;
    }
}

void IRAM_ATTR onButton2Press() {
    unsigned long now = millis();
    if (now - lastButton2Interrupt > DEBOUNCE_MS) {
        button2Pressed = true;
        lastButton2Interrupt = now;
    }
}

// === OSC Functions ===
void sendOSCButton(int buttonNumber) {
    oscManager.sendButton(udp, buttonNumber);
}

// === Button Handling ===
void handleButtons() {
    // Handle button 1 (debouncing done in ISR)
    if (button1Pressed) {
        button1Pressed = false;
        if (digitalRead(BUTTON_1_PIN) == LOW) {
            sendOSCButton(1);
        }
    }

    // Handle button 2 (debouncing done in ISR)
    if (button2Pressed) {
        button2Pressed = false;
        if (digitalRead(BUTTON_2_PIN) == LOW) {
            sendOSCButton(2);
        }
    }
}


// === Deep Sleep ===
void enterDeepSleep() {
    Serial.println("Dock detected — entering deep sleep");
    Serial.println("Press button 1 (D1) to wake");
    Serial.flush();

    // Clean WiFi shutdown
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    // Configure D1 (GPIO3) as wake source — wake on LOW (button press)
    esp_deep_sleep_enable_gpio_wakeup(BIT(D1), ESP_GPIO_WAKEUP_GPIO_LOW);

    esp_deep_sleep_start();
}

// === Battery Reading ===
// Piecewise-linear LiPo discharge curve: resting cell voltage (mV) -> SoC (%).
// Rough single-cell LiPo under light load; the knee around 3.7 V is what
// makes the linear 3.20-4.20 V mapping misleading at the ends of the range.
static int lipoPercentFromMillivolts(int mv) {
    struct Point { int mv; int pct; };
    static const Point curve[] = {
        {3300,   0},
        {3400,   5},
        {3500,  10},
        {3600,  20},
        {3700,  40},
        {3800,  55},
        {3900,  65},
        {4000,  80},
        {4100,  90},
        {4200, 100},
    };
    const int n = sizeof(curve) / sizeof(curve[0]);
    if (mv <= curve[0].mv) return 0;
    if (mv >= curve[n - 1].mv) return 100;
    for (int i = 1; i < n; i++) {
        if (mv <= curve[i].mv) {
            const Point& a = curve[i - 1];
            const Point& b = curve[i];
            return a.pct + (long)(mv - a.mv) * (b.pct - a.pct) / (b.mv - a.mv);
        }
    }
    return 100;
}

int getBatteryPercent() {
    // analogReadMilliVolts() applies the factory eFuse ADC calibration, so we
    // skip the raw->mV math and get a much more accurate absolute voltage.
    long sumMv = 0;
    for (int i = 0; i < 16; i++) sumMv += analogReadMilliVolts(A0);
    int adcMv = sumMv / 16;
    // 2x 220k resistor divider: Vbat = ADC voltage * 2
    int batMv = adcMv * 2;

    // Exponential moving average across calls (alpha = 0.25). Hides single-
    // sample jitter from WiFi TX bursts and the high-impedance divider while
    // still tracking real movement within a handful of updates.
    static int emaMv = 0;
    if (emaMv == 0) {
        emaMv = batMv;  // seed on first call so we don't creep up from zero
    } else {
        emaMv = (batMv + emaMv * 3) / 4;
    }

    return lipoPercentFromMillivolts(emaMv);
}

// === Setup ===
void setup() {
    // === EARLY DOCK CHECK ===
    // Before Serial.begin() (which can wait up to 3s for USB CDC) or any WiFi init,
    // check whether we're booting in the dock. If so, sleep immediately — otherwise
    // every dock-while-powered cycle would burn ~10 s of WiFi setup.
    //
    // We still need to configure D1 (wake source) so we can come back out of sleep:
    // pull-up enabled and latched, just like the normal sleep path.
    pinMode(BUTTON_1_PIN, INPUT_PULLUP);
    if (REED_SENSOR_ENABLED) pinMode(REED_SWITCH_PIN, INPUT_PULLUP);
    delayMicroseconds(100);  // Let pull-ups settle before reading

    if (REED_SENSOR_ENABLED && digitalRead(REED_SWITCH_PIN) == LOW) {
        // Docked — sleep immediately. No serial output (Serial not yet up).
        gpio_hold_en(GPIO_NUM_3);  // hold D1 pull-up across deep sleep
        gpio_deep_sleep_hold_en();
        esp_deep_sleep_enable_gpio_wakeup(BIT(D1), ESP_GPIO_WAKEUP_GPIO_LOW);
        esp_deep_sleep_start();
    }

    Serial.begin(115200);
    // Wait for USB CDC to be ready (essential for ESP32-C3 native USB)
    unsigned long serialStart = millis();
    while (!Serial && (millis() - serialStart < 3000)) {
        delay(10);
    }
    delay(500);  // Extra delay for serial monitor to attach

    Serial.println("\n\n=== ESP32-C3 OSC Button Controller ===");
    Serial.println("Serial connected!");

    // Button 2 pin (BUTTON_1_PIN and REED_SWITCH_PIN already configured above)
    pinMode(BUTTON_2_PIN, INPUT_PULLUP);

    // Latch the button pads so their pull-ups survive deep sleep.
    // D1 is the wake source; D2 is held too so its input doesn't float during sleep
    // (saves a tiny bit of leakage current). REED_SWITCH_PIN doesn't need this —
    // we're never asleep when the reed switch matters.
    gpio_hold_en(GPIO_NUM_3);  // D1 / BUTTON_1_PIN
    gpio_hold_en(GPIO_NUM_4);  // D2 / BUTTON_2_PIN
    gpio_deep_sleep_hold_en();

    // Attach interrupts for immediate response
    attachInterrupt(digitalPinToInterrupt(BUTTON_1_PIN), onButton1Press, FALLING);
    attachInterrupt(digitalPinToInterrupt(BUTTON_2_PIN), onButton2Press, FALLING);

    // Configure and start WiFi manager
    WiFiManagerConfig wifiConfig = {
        .apSSID = "OSC-MUIS",
        .apPassword = "oscbuttons",  // Min 8 characters, or "" for open network
        .apChannel = 6,
        .countryCode = "NL",
        .portalTitle = "OSC-MUIS",
        .portalSubtitle = "Button Controller",
        .displayPort = 0  // Will show configured OSC port in portal
    };
    wifiManager.begin(wifiConfig);

    // Initialize OSC manager (registers web endpoints and template callback)
    oscManager.begin(wifiManager.getWebServer(), wifiManager);

    // Button status endpoint (kept for external/debug use)
    AsyncWebServer& server = wifiManager.getWebServer();
    server.on("/buttonstatus", HTTP_GET, [](AsyncWebServerRequest *request) {
        String json = "{\"button1\":";
        json += (digitalRead(BUTTON_1_PIN) == LOW) ? "true" : "false";
        json += ",\"button2\":";
        json += (digitalRead(BUTTON_2_PIN) == LOW) ? "true" : "false";
        json += "}";
        request->send(200, "application/json", json);
    });

    // Server-Sent Events — pushes button + battery state over a single persistent
    // connection instead of the client polling /buttonstatus every 500 ms.
    // This avoids the memory fragmentation that kills ESPAsyncWebServer over hours.
    events.onConnect([](AsyncEventSourceClient *client) {
        // Send current state immediately so the UI doesn't show stale data
        String btn = "{\"button1\":";
        btn += (digitalRead(BUTTON_1_PIN) == LOW) ? "true" : "false";
        btn += ",\"button2\":";
        btn += (digitalRead(BUTTON_2_PIN) == LOW) ? "true" : "false";
        btn += "}";
        client->send(btn.c_str(), "buttons", millis(), 10000);

        String bat = String(wifiManager.getBatteryPercent());
        client->send(bat.c_str(), "battery", millis());
    });
    server.addHandler(&events);

    // Now that all routes are registered, start the web server.
    // (Routes must be added before begin() — onNotFound can otherwise intercept them.)
    wifiManager.startWebServer();

    // Start UDP for OSC (use configured port for listening, though we mainly send)
    udp.begin(oscManager.getPort());

    Serial.println("Ready! Waiting for button presses...");
}

// === Main Loop ===
void loop() {
    // Process WiFi manager (captive portal, DNS, connection monitoring)
    wifiManager.loop();

    // Update battery level periodically and push to connected web clients
    static unsigned long lastBatteryUpdate = 0;
    if (millis() - lastBatteryUpdate > 10000) {
        int pct = getBatteryPercent();
        wifiManager.setBatteryPercent(pct);
        events.send(String(pct).c_str(), "battery", millis());
        lastBatteryUpdate = millis();
    }

    // Push button state changes to connected web clients
    static bool lastBtn1State = false, lastBtn2State = false;
    bool btn1 = (digitalRead(BUTTON_1_PIN) == LOW);
    bool btn2 = (digitalRead(BUTTON_2_PIN) == LOW);
    if (btn1 != lastBtn1State || btn2 != lastBtn2State) {
        lastBtn1State = btn1;
        lastBtn2State = btn2;
        String json = "{\"button1\":";
        json += btn1 ? "true" : "false";
        json += ",\"button2\":";
        json += btn2 ? "true" : "false";
        json += "}";
        events.send(json.c_str(), "buttons", millis());
    }

    // Handle any pending button presses
    handleButtons();

    // Handle test request from web interface
    if (oscManager.checkAndClearTestRequest()) {
        sendOSCButton(1);
    }

    // Check reed switch for dock detection (magnet closes NO switch → LOW)
    if (REED_SENSOR_ENABLED) {
        static unsigned long reedLowSince = 0;
        if (digitalRead(REED_SWITCH_PIN) == LOW) {
            if (reedLowSince == 0) reedLowSince = millis();
            if (millis() - reedLowSince > DOCK_DEBOUNCE_MS) {
                enterDeepSleep();
            }
        } else {
            reedLowSince = 0;
        }
    }
}
