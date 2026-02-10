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
#include "esp_sleep.h"
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

// Debounce settings
// This is a cooldown after the initial press — the ISR fires instantly (no lag),
// but then ignores all edges (including release bounce) for this duration.
const unsigned long DEBOUNCE_MS = 800;
const unsigned long DOCK_DEBOUNCE_MS = 500;  // Reed switch debounce for dock detection

// === Global variables ===
WiFiUDP udp;
WiFiManager wifiManager;
OSCManager oscManager;

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
int getBatteryPercent() {
    // TODO: Implement actual battery reading via ADC
    // Example for voltage divider on A0:
    // int raw = analogRead(A0);
    // float voltage = raw * (3.3 / 4095.0) * 2;  // Assuming 1:1 divider
    // return map(constrain(voltage * 100, 320, 420), 320, 420, 0, 100);
    return 100;  // Placeholder
}

// === Setup ===
void setup() {
    Serial.begin(115200);
    // Wait for USB CDC to be ready (essential for ESP32-C3 native USB)
    unsigned long serialStart = millis();
    while (!Serial && (millis() - serialStart < 3000)) {
        delay(10);
    }
    delay(500);  // Extra delay for serial monitor to attach

    Serial.println("\n\n=== ESP32-C3 OSC Button Controller ===");
    Serial.println("Serial connected!");

    // Configure button pins with internal pullup
    pinMode(BUTTON_1_PIN, INPUT_PULLUP);
    pinMode(BUTTON_2_PIN, INPUT_PULLUP);

    // Reed switch — NO to GND, internal pullup keeps it HIGH when open (normal operation)
    pinMode(REED_SWITCH_PIN, INPUT_PULLUP);

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

    // Start UDP for OSC (use configured port for listening, though we mainly send)
    udp.begin(oscManager.getPort());

    Serial.println("Ready! Waiting for button presses...");
}

// === Main Loop ===
void loop() {
    // Process WiFi manager (captive portal, DNS, connection monitoring)
    wifiManager.loop();

    // Update battery level periodically
    static unsigned long lastBatteryUpdate = 0;
    if (millis() - lastBatteryUpdate > 10000) {
        wifiManager.setBatteryPercent(getBatteryPercent());
        lastBatteryUpdate = millis();
    }

    // Handle any pending button presses
    handleButtons();

    // Handle test request from web interface
    if (oscManager.checkAndClearTestRequest()) {
        sendOSCButton(1);
    }

    // Check reed switch for dock detection (magnet closes NO switch → LOW)
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
