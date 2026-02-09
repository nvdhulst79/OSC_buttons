// OSC-Muis - Niels van der Hulst 2026

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <vector>

// Configuration structure for WiFi manager
struct WiFiManagerConfig {
    const char* apSSID;
    const char* apPassword;      // Min 8 chars, or empty for open network
    uint8_t apChannel;           // WiFi channel (1-13)
    const char* countryCode;     // Country code for WiFi regulations (e.g., "NL", "US")
    const char* portalTitle;     // Title shown in captive portal
    const char* portalSubtitle;  // Subtitle shown in captive portal
    int displayPort;             // Port number to display in portal (e.g., OSC port)
};

// Runtime state of the WiFi manager
struct WiFiManagerState {
    String staSSID;
    String staPassword;
    bool staEnabled;
    bool staConnected;
    int batteryPercent;
    IPAddress broadcastIP;       // Current broadcast address

    // OSC settings
    String oscTargetIP;          // Target IP for OSC (empty = broadcast)
    int oscPort;                 // OSC port (default 8001 for LuPlayer)
    String oscAddressFormat;     // Address format: "kmpush" or "/kmpush" or "/km/push/"

    // Test trigger flag (set by web UI, cleared by main loop)
    volatile bool oscTestRequested;
};

// Default configuration values
#define WIFI_MANAGER_DEFAULT_CHANNEL 6
#define WIFI_MANAGER_DEFAULT_COUNTRY "NL"

class WiFiManager {
public:
    WiFiManager();

    // Initialize with configuration (call in setup)
    void begin(const WiFiManagerConfig& config);

    // Process WiFi manager tasks (call in loop)
    void loop();

    // Get current broadcast IP (updates when STA connects/disconnects)
    IPAddress getBroadcastIP() const;

    // Get AP IP address
    IPAddress getAPIP() const;

    // Get number of connected clients
    int getClientCount() const;

    // Check if connected to external WiFi (STA mode)
    bool isSTAConnected() const;

    // Get STA IP address (if connected)
    IPAddress getSTAIP() const;

    // Set battery percentage for portal display
    void setBatteryPercent(int percent);

    // Get battery percentage
    int getBatteryPercent() const;

    // Get current state (for advanced use)
    const WiFiManagerState& getState() const;

    // OSC configuration
    void setOSCPort(int port);
    int getOSCPort() const;
    void setOSCTargetIP(const String& ip);
    String getOSCTargetIP() const;
    IPAddress getOSCTargetIPAddress() const;  // Returns broadcast IP if target is empty
    std::vector<IPAddress> getOSCTargetIPAddresses() const;  // Returns all broadcast IPs (AP + STA when in dual mode)
    void setOSCAddressFormat(const String& format);
    String getOSCAddressFormat() const;
    String formatOSCAddress(int buttonNumber) const;  // Build address for button
    bool checkAndClearTestRequest();  // Check if test was requested, clears flag

private:
    WiFiManagerConfig _config;
    WiFiManagerState _state;

    DNSServer _dnsServer;
    AsyncWebServer _webServer;
    Preferences _preferences;

    void setupAccessPoint();
    void initCaptivePortal();
    void loadSavedWiFi();
    void connectToSavedWiFi();
    void updateConnectionStatus();
};

#endif
