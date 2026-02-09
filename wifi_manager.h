// OSC-Muis - Niels van der Hulst 2026

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <vector>

// Callback type for custom template variable processing
// Return non-empty string if variable is handled, empty string otherwise
typedef String (*TemplateProcessorCallback)(const String& var);

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

    // AP lifecycle
    bool apActive;               // Whether the AP is currently running
    unsigned long apShutdownTime; // millis() when AP should shut down (0 = no shutdown scheduled)
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

    // Get all broadcast IPs (AP + STA when in dual mode)
    std::vector<IPAddress> getBroadcastIPAddresses() const;

    // Get AP IP address
    IPAddress getAPIP() const;

    // Get number of connected clients
    int getClientCount() const;

    // Check if connected to external WiFi (STA mode)
    bool isSTAConnected() const;

    // Check if AP is currently active
    bool isAPActive() const;

    // Get STA IP address (if connected)
    IPAddress getSTAIP() const;

    // Set battery percentage for portal display
    void setBatteryPercent(int percent);

    // Get battery percentage
    int getBatteryPercent() const;

    // Get current state (for advanced use)
    const WiFiManagerState& getState() const;

    // Register callback for custom template variable processing
    // This allows other modules to provide their own template variables
    void registerTemplateCallback(TemplateProcessorCallback callback);

    // Get web server for registering additional endpoints
    AsyncWebServer& getWebServer();

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
