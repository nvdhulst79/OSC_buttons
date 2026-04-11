// OSC-Muis - Niels van der Hulst 2026

#include "wifi_manager.h"
#include "portal_html.h"
#include "esp_wifi.h"
#include <ESPmDNS.h>

// Static instance pointers for callbacks
static WiFiManager* _instance = nullptr;
static const WiFiManagerConfig* _configPtr = nullptr;
static TemplateProcessorCallback _customProcessorCallback = nullptr;

// Template processor for HTML placeholders
static String processTemplate(const String& var) {
    if (!_instance) return String();

    const WiFiManagerState& state = _instance->getState();

    // WiFi-related variables
    if (var == "BATTERY") return String(state.batteryPercent);
    if (var == "MODE") {
        if (!state.apActive && state.staConnected) return "Station (power save)";
        if (state.staConnected) return "AP + Station";
        return "Access Point";
    }
    if (var == "STA_SSID") return state.staConnected ? state.staSSID : "-";
    if (var == "STA_IP") return state.staConnected ? WiFi.localIP().toString() : "-";
    if (var == "STA_STATUS_CLASS") return state.staConnected ? "" : "hidden";
    if (var == "AP_CLIENTS") return String(WiFi.softAPgetStationNum());
    if (var == "AP_SSID") return _configPtr ? String(_configPtr->apSSID) : "";
    if (var == "AP_IP") return WiFi.softAPIP().toString();
    if (var == "DISCONNECT_CLASS") return state.staConnected ? "" : "hidden";
    if (var == "PORTAL_TITLE") return _configPtr ? String(_configPtr->portalTitle) : "WiFi Manager";
    if (var == "PORTAL_SUBTITLE") return _configPtr ? String(_configPtr->portalSubtitle) : "";

    // Try custom processor if registered
    if (_customProcessorCallback) {
        String result = _customProcessorCallback(var);
        if (result.length() > 0) return result;
    }

    return String();
}


WiFiManager::WiFiManager() : _webServer(80) {
    _state.staEnabled = false;
    _state.staConnected = false;
    _state.batteryPercent = 100;
    _state.broadcastIP = IPAddress(192, 168, 4, 255);
    _state.apActive = true;
    _state.apShutdownTime = 0;
    _state.connectRequested = false;
    _state.disconnectRequested = false;
    _state.connectStartTime = 0;
    _state.connectResult = WIFI_CONN_IDLE;
}

void WiFiManager::begin(const WiFiManagerConfig& config) {
    _config = config;
    _instance = this;
    _configPtr = &_config;

    // Load saved WiFi credentials
    loadSavedWiFi();

    // Set up WiFi Access Point
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_AP);
    delay(100);
    setupAccessPoint();

    // Connect to saved WiFi if available
    connectToSavedWiFi();

    // Initialize captive portal
    initCaptivePortal();
}

void WiFiManager::setupAccessPoint() {
    Serial.println("Starting WiFi Access Point...");
    // NOTE: caller is responsible for setting WiFi mode (WIFI_AP or WIFI_AP_STA)
    // and calling WiFi.disconnect() if needed, before calling this function.

    // Set WiFi country code
    if (_config.countryCode && strlen(_config.countryCode) > 0) {
        Serial.printf("Setting WiFi country to %s...\n", _config.countryCode);
        esp_wifi_set_country_code(_config.countryCode, true);
    }

    // Configure AP with static IP
    IPAddress localIP(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);

    Serial.println("Configuring AP IP...");
    WiFi.softAPConfig(localIP, gateway, subnet);
    delay(100);

    // Start AP
    Serial.printf("Starting AP with SSID: %s\n", _config.apSSID);
    bool success;
    if (_config.apPassword && strlen(_config.apPassword) >= 8) {
        success = WiFi.softAP(_config.apSSID, _config.apPassword, _config.apChannel);
    } else {
        success = WiFi.softAP(_config.apSSID, NULL, _config.apChannel);
    }

    delay(1000);  // Give AP time to start

    if (success) {
        Serial.println("AP started successfully!");
        Serial.printf("  SSID: %s\n", _config.apSSID);
        Serial.printf("  IP: %s\n", WiFi.softAPIP().toString().c_str());
        Serial.printf("  Channel: %d\n", WiFi.channel());
        Serial.printf("  MAC: %s\n", WiFi.softAPmacAddress().c_str());
    } else {
        Serial.println("ERROR: AP failed to start!");
    }
}

void WiFiManager::initCaptivePortal() {
    // Start DNS server for captive portal redirect
    _dnsServer.start(53, "*", WiFi.softAPIP());

    // Serve the main portal page
    _webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", PORTAL_HTML, processTemplate);
    });

    // Captive portal detection endpoints
    _webServer.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->redirect("/");
    });
    _webServer.on("/fwlink", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->redirect("/");
    });
    _webServer.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->redirect("/");
    });
    _webServer.on("/canonical.html", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->redirect("/");
    });
    _webServer.on("/success.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "success");
    });
    _webServer.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->redirect("/");
    });

    // Scan for networks
    _webServer.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
        int n = WiFi.scanComplete();

        // Return status if scan not ready yet
        if (n == WIFI_SCAN_FAILED) {
            // Ensure station mode is enabled for scanning
            wifi_mode_t currentMode = WiFi.getMode();
            if (currentMode == WIFI_AP) {
                WiFi.mode(WIFI_AP_STA);
                WiFi.disconnect();  // Disconnect STA but keep mode
            } else if (currentMode == WIFI_AP_STA && !_instance->isSTAConnected()) {
                // If in AP_STA mode but not connected, ensure clean state for scanning
                WiFi.disconnect();
            }
            WiFi.scanNetworks(true);  // Start async scan
            request->send(200, "application/json", "{\"status\":\"scanning\"}");
            return;
        }
        if (n == WIFI_SCAN_RUNNING) {
            request->send(200, "application/json", "{\"status\":\"scanning\"}");
            return;
        }

        // Build results JSON
        String json = "[";
        for (int i = 0; i < n; i++) {
            if (i > 0) json += ",";
            json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",";
            json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
            json += "\"secure\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN) + "}";
        }
        json += "]";
        WiFi.scanDelete();
        request->send(200, "application/json", json);
    });

    // Connect to a network — defers actual work to loop() to keep async handler non-blocking
    _webServer.on("/connect", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!request->hasParam("ssid", true) || !request->hasParam("password", true)) {
            request->send(200, "application/json", "{\"success\":false,\"message\":\"Missing parameters\"}");
            return;
        }

        _state.pendingSSID = request->getParam("ssid", true)->value();
        _state.pendingPassword = request->getParam("password", true)->value();
        _state.connectResult = WIFI_CONN_IDLE;
        _state.connectRequested = true;

        request->send(200, "application/json", "{\"success\":true,\"status\":\"connecting\"}");
    });

    // Poll connection status (called by frontend after POST /connect)
    _webServer.on("/constatus", HTTP_GET, [this](AsyncWebServerRequest *request) {
        const char* status;
        switch (_state.connectResult) {
            case WIFI_CONN_CONNECTING: status = "connecting"; break;
            case WIFI_CONN_SUCCESS:    status = "connected";  break;
            case WIFI_CONN_FAILED:     status = "failed";     break;
            default:                   status = "idle";       break;
        }
        String json = "{\"status\":\"";
        json += status;
        json += "\"";
        if (_state.connectResult == WIFI_CONN_SUCCESS) {
            json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
        }
        json += "}";
        request->send(200, "application/json", json);
    });

    // Disconnect from network — defers actual work to loop()
    _webServer.on("/disconnect", HTTP_POST, [this](AsyncWebServerRequest *request) {
        _state.disconnectRequested = true;
        request->send(200, "application/json", "{\"success\":true}");
    });

    // Handle all other requests
    _webServer.onNotFound([](AsyncWebServerRequest *request) {
        request->redirect("/");
    });

    // NOTE: _webServer.begin() is intentionally NOT called here.
    // External modules (e.g. OSCManager) need a chance to register their
    // routes first; the sketch must call startWebServer() afterwards.

    // Start initial WiFi scan
    WiFi.scanNetworks(true);

    Serial.println("Captive portal initialized");
    Serial.printf("Portal will be available at http://%s\n", WiFi.softAPIP().toString().c_str());
}

void WiFiManager::startWebServer() {
    _webServer.begin();
    Serial.println("Web server started");
}

void WiFiManager::loadSavedWiFi() {
    _preferences.begin("wifi", true);
    _state.staSSID = _preferences.getString("ssid", "");
    _state.staPassword = _preferences.getString("password", "");
    _state.staEnabled = _preferences.getBool("enabled", false);
    _preferences.end();

    if (_state.staEnabled && _state.staSSID.length() > 0) {
        Serial.printf("Found saved WiFi: %s\n", _state.staSSID.c_str());
    }
}

void WiFiManager::connectToSavedWiFi() {
    if (!_state.staEnabled || _state.staSSID.length() == 0) return;

    Serial.printf("Starting async connect to saved WiFi: %s\n", _state.staSSID.c_str());

    // Kick off the connection non-blockingly. The state machine in
    // processWiFiRequests() will observe completion (or timeout) from loop()
    // and handle the success/failure paths uniformly with web-initiated connects.
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(_state.staSSID.c_str(), _state.staPassword.c_str());

    _state.connectResult = WIFI_CONN_CONNECTING;
    _state.connectStartTime = millis();
}

void WiFiManager::loop() {
    // Process DNS requests (only when AP is active)
    if (_state.apActive) {
        _dnsServer.processNextRequest();
    }

    // Handle deferred connect/disconnect requests from async HTTP handlers
    processWiFiRequests();

    // Update connection status
    updateConnectionStatus();

    // Check if it's time to shut down the AP.
    // Use signed-difference comparison so this stays correct across millis() rollover (~49 days).
    if (_state.apShutdownTime > 0 && (int32_t)(millis() - _state.apShutdownTime) > 0 && _state.staConnected) {
        Serial.println("Shutting down AP, switching to STA-only with modem sleep");
        _dnsServer.stop();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        _state.apActive = false;
        _state.apShutdownTime = 0;
        Serial.printf("Now in STA-only mode, IP: %s\n", WiFi.localIP().toString().c_str());
    }
}

void WiFiManager::processWiFiRequests() {
    // Handle a pending disconnect request from /disconnect
    if (_state.disconnectRequested) {
        _state.disconnectRequested = false;
        Serial.println("Processing deferred disconnect request");

        _preferences.begin("wifi", false);
        _preferences.putBool("enabled", false);
        _preferences.end();

        _state.staEnabled = false;
        _state.staConnected = false;
        _state.apShutdownTime = 0;  // Cancel AP shutdown
        _state.connectResult = WIFI_CONN_IDLE;
        MDNS.end();
        WiFi.disconnect(true);
        WiFi.mode(WIFI_AP);

        if (!_state.apActive) {
            _state.apActive = true;
            setupAccessPoint();
            _dnsServer.start(53, "*", WiFi.softAPIP());
        }

        _state.broadcastIP = IPAddress(192, 168, 4, 255);
        Serial.println("Disconnected from WiFi, AP only mode");
    }

    // Handle a pending connect request from /connect
    if (_state.connectRequested) {
        _state.connectRequested = false;
        Serial.printf("Processing deferred connect request to %s\n", _state.pendingSSID.c_str());

        _state.staSSID = _state.pendingSSID;
        _state.staPassword = _state.pendingPassword;
        _state.staEnabled = true;

        // Save credentials
        _preferences.begin("wifi", false);
        _preferences.putString("ssid", _state.staSSID);
        _preferences.putString("password", _state.staPassword);
        _preferences.putBool("enabled", true);
        _preferences.end();

        // Kick off connection (non-blocking — status polled below)
        WiFi.mode(WIFI_AP_STA);
        WiFi.disconnect();
        WiFi.begin(_state.staSSID.c_str(), _state.staPassword.c_str());

        _state.connectResult = WIFI_CONN_CONNECTING;
        _state.connectStartTime = millis();
    }

    // Poll an in-flight connection attempt
    if (_state.connectResult == WIFI_CONN_CONNECTING) {
        if (WiFi.status() == WL_CONNECTED) {
            _state.staConnected = true;
            _state.broadcastIP = WiFi.broadcastIP();  // honors actual subnet mask
            _state.apShutdownTime = millis() + 600000;  // Shut down AP in 10 minutes
            _state.connectResult = WIFI_CONN_SUCCESS;

            // Tear down any previous mDNS instance before re-registering
            // (some ESPmDNS versions silently fail a second begin() otherwise)
            MDNS.end();
            if (MDNS.begin("osc-muis")) {
                MDNS.addService("http", "tcp", 80);
                Serial.println("mDNS started: http://osc-muis.local");
            }
            Serial.printf("Connected to %s, IP: %s\n",
                _state.staSSID.c_str(), WiFi.localIP().toString().c_str());
            Serial.println("AP will shut down in 10 minutes");
        } else if (millis() - _state.connectStartTime > 10000) {
            _state.staConnected = false;
            _state.connectResult = WIFI_CONN_FAILED;
            WiFi.mode(WIFI_AP);
            Serial.println("Deferred connection attempt failed (timeout)");
        }
    }
}

void WiFiManager::updateConnectionStatus() {
    if (_state.staEnabled && !_state.staConnected && WiFi.status() == WL_CONNECTED) {
        _state.staConnected = true;
        _state.broadcastIP = WiFi.broadcastIP();  // honors actual subnet mask
        // Re-arm AP shutdown if AP is still up — otherwise we'd keep the AP forever
        // after a reconnect (since it was zeroed when AP shut down or on disconnect).
        if (_state.apActive) {
            _state.apShutdownTime = millis() + 600000;
            Serial.println("AP shutdown re-armed for 10 minutes");
        }
        Serial.printf("WiFi reconnected, IP: %s\n", WiFi.localIP().toString().c_str());

        // Tear down any previous mDNS instance before re-registering
        MDNS.end();
        if (MDNS.begin("osc-muis")) {
            MDNS.addService("http", "tcp", 80);
            Serial.println("mDNS restarted: http://osc-muis.local");
        }
    } else if (_state.staConnected && WiFi.status() != WL_CONNECTED) {
        _state.staConnected = false;
        _state.broadcastIP = IPAddress(192, 168, 4, 255);
        Serial.println("WiFi connection lost, using AP broadcast");

        // If AP was shut down, bring it back and try to reconnect STA
        if (!_state.apActive) {
            Serial.println("Re-enabling AP and attempting STA reconnect");
            esp_wifi_set_ps(WIFI_PS_NONE);
            WiFi.mode(WIFI_AP_STA);
            delay(100);
            setupAccessPoint();
            _dnsServer.start(53, "*", WiFi.softAPIP());
            _state.apActive = true;
        }

        // Try to reconnect to saved network
        if (_state.staEnabled && _state.staSSID.length() > 0) {
            Serial.printf("Reconnecting to %s...\n", _state.staSSID.c_str());
            WiFi.begin(_state.staSSID.c_str(), _state.staPassword.c_str());
            _state.connectResult = WIFI_CONN_CONNECTING;
            _state.connectStartTime = millis();
        }
    }
}

IPAddress WiFiManager::getBroadcastIP() const {
    return _state.broadcastIP;
}

IPAddress WiFiManager::getAPIP() const {
    return WiFi.softAPIP();
}

int WiFiManager::getClientCount() const {
    return WiFi.softAPgetStationNum();
}

bool WiFiManager::isSTAConnected() const {
    return _state.staConnected;
}

bool WiFiManager::isAPActive() const {
    return _state.apActive;
}

IPAddress WiFiManager::getSTAIP() const {
    return WiFi.localIP();
}

void WiFiManager::setBatteryPercent(int percent) {
    _state.batteryPercent = percent;
}

int WiFiManager::getBatteryPercent() const {
    return _state.batteryPercent;
}

const WiFiManagerState& WiFiManager::getState() const {
    return _state;
}

void WiFiManager::registerTemplateCallback(TemplateProcessorCallback callback) {
    _customProcessorCallback = callback;
}

AsyncWebServer& WiFiManager::getWebServer() {
    return _webServer;
}

std::vector<IPAddress> WiFiManager::getBroadcastIPAddresses() const {
    std::vector<IPAddress> addresses;

    // Get all active network broadcast addresses
    wifi_mode_t mode = WiFi.getMode();

    // Add STA network broadcast if connected (honors actual subnet mask, not just /24)
    if ((mode == WIFI_AP_STA || mode == WIFI_STA) && _state.staConnected) {
        addresses.push_back(WiFi.broadcastIP());
    }

    // Add AP network broadcast if AP is active
    if (mode == WIFI_AP_STA || mode == WIFI_AP) {
        addresses.push_back(IPAddress(192, 168, 4, 255));
    }

    // Fallback to AP broadcast if no networks are available
    if (addresses.empty()) {
        addresses.push_back(IPAddress(192, 168, 4, 255));
    }

    return addresses;
}
