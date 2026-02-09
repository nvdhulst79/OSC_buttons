// OSC-Muis - Niels van der Hulst 2026

#ifndef OSC_MANAGER_H
#define OSC_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include <ESPAsyncWebServer.h>
#include <WiFiUdp.h>
#include <vector>

// Forward declaration
class WiFiManager;

class OSCManager {
public:
    OSCManager();

    // Initialize OSC manager with web server and wifi manager references
    void begin(AsyncWebServer& webServer, WiFiManager& wifiManager);

    // Configuration
    void setPort(int port);
    int getPort() const;
    void setTargetIP(const String& ip);
    String getTargetIP() const;
    void setAddressFormat(const String& format);
    String getAddressFormat() const;
    void setButton1Channel(int channel);
    int getButton1Channel() const;
    void setButton2Channel(int channel);
    int getButton2Channel() const;

    // Get target IPs for sending (uses WiFiManager's broadcast IPs)
    std::vector<IPAddress> getTargetIPAddresses() const;

    // Format OSC address for button
    String formatAddress(int buttonNumber) const;

    // Test request handling
    bool checkAndClearTestRequest();

    // Send OSC button press message
    // Handles formatting, broadcasting to all targets, and logging
    void sendButton(WiFiUDP& udp, int buttonNumber);

private:
    WiFiManager* _wifiManager;

    struct {
        String targetIP;          // Target IP for OSC (empty = broadcast)
        int port;                 // OSC port (default 8001 for LuPlayer)
        String addressFormat;     // LuPlayer mode: "kmpush" (Keyboard Mapped), "8faderspush" (Eight Faders), or custom
        int button1Channel;       // Channel number for button 1 (default 1)
        int button2Channel;       // Channel number for button 2 (default 2)
        volatile bool testRequested;  // Test trigger flag (set by web UI, cleared by main loop)
    } _state;

    Preferences _preferences;

    void loadSettings();
    void saveSettings();
    void registerWebEndpoints(AsyncWebServer& webServer);
};

#endif
