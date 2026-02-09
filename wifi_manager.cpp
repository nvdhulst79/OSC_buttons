// OSC-Muis - Niels van der Hulst 2026

#include "wifi_manager.h"
#include "portal_html.h"
#include "esp_wifi.h"

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
    if (var == "MODE") return state.staConnected ? "AP + Station" : "Access Point";
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
}

void WiFiManager::begin(const WiFiManagerConfig& config) {
    _config = config;
    _instance = this;
    _configPtr = &_config;

    // Load saved WiFi credentials
    loadSavedWiFi();

    // Set up WiFi Access Point
    setupAccessPoint();

    // Connect to saved WiFi if available
    connectToSavedWiFi();

    // Initialize captive portal
    initCaptivePortal();
}

void WiFiManager::setupAccessPoint() {
    Serial.println("Starting WiFi Access Point...");

    // Disconnect any previous WiFi state
    WiFi.disconnect(true);
    delay(100);

    // Set WiFi country code
    if (_config.countryCode && strlen(_config.countryCode) > 0) {
        Serial.printf("Setting WiFi country to %s...\n", _config.countryCode);
        esp_wifi_set_country_code(_config.countryCode, true);
    }

    // Configure AP with static IP
    IPAddress localIP(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);

    Serial.println("Setting WiFi mode to AP...");
    WiFi.mode(WIFI_AP);
    delay(100);

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
        request->send_P(200, "text/html", PORTAL_HTML, processTemplate);
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

    // Connect to a network
    _webServer.on("/connect", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!request->hasParam("ssid", true) || !request->hasParam("password", true)) {
            request->send(200, "application/json", "{\"success\":false,\"message\":\"Missing parameters\"}");
            return;
        }

        _state.staSSID = request->getParam("ssid", true)->value();
        _state.staPassword = request->getParam("password", true)->value();
        _state.staEnabled = true;

        // Save credentials
        _preferences.begin("wifi", false);
        _preferences.putString("ssid", _state.staSSID);
        _preferences.putString("password", _state.staPassword);
        _preferences.putBool("enabled", true);
        _preferences.end();

        // Attempt connection
        WiFi.mode(WIFI_AP_STA);
        WiFi.disconnect();  // Ensure clean state before new connection
        WiFi.begin(_state.staSSID.c_str(), _state.staPassword.c_str());

        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            attempts++;
        }

        if (WiFi.status() == WL_CONNECTED) {
            _state.staConnected = true;
            _state.broadcastIP = WiFi.localIP();
            _state.broadcastIP[3] = 255;

            String response = "{\"success\":true,\"ip\":\"" + WiFi.localIP().toString() + "\"}";
            request->send(200, "application/json", response);
            Serial.printf("Connected to %s, IP: %s\n", _state.staSSID.c_str(), WiFi.localIP().toString().c_str());
        } else {
            _state.staConnected = false;
            WiFi.mode(WIFI_AP);
            request->send(200, "application/json", "{\"success\":false,\"message\":\"Connection failed\"}");
        }
    });

    // Disconnect from network
    _webServer.on("/disconnect", HTTP_POST, [this](AsyncWebServerRequest *request) {
        _preferences.begin("wifi", false);
        _preferences.putBool("enabled", false);
        _preferences.end();

        _state.staEnabled = false;
        _state.staConnected = false;
        WiFi.disconnect(true);
        WiFi.mode(WIFI_AP);

        _state.broadcastIP = IPAddress(192, 168, 4, 255);

        request->send(200, "application/json", "{\"success\":true}");
        Serial.println("Disconnected from WiFi, AP only mode");
    });

    // Handle all other requests
    _webServer.onNotFound([](AsyncWebServerRequest *request) {
        request->redirect("/");
    });

    _webServer.begin();

    // Start initial WiFi scan
    WiFi.scanNetworks(true);

    Serial.println("Captive portal started");
    Serial.printf("Portal available at http://%s\n", WiFi.softAPIP().toString().c_str());
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

    Serial.printf("Connecting to saved WiFi: %s\n", _state.staSSID.c_str());
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(_state.staSSID.c_str(), _state.staPassword.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        _state.staConnected = true;
        _state.broadcastIP = WiFi.localIP();
        _state.broadcastIP[3] = 255;
        Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        _state.staConnected = false;
        WiFi.mode(WIFI_AP);  // Revert to AP-only mode when connection fails
        Serial.println("\nFailed to connect, reverted to AP-only mode");
    }
}

void WiFiManager::loop() {
    // Process DNS requests
    _dnsServer.processNextRequest();

    // Update connection status
    updateConnectionStatus();
}

void WiFiManager::updateConnectionStatus() {
    if (_state.staEnabled && !_state.staConnected && WiFi.status() == WL_CONNECTED) {
        _state.staConnected = true;
        _state.broadcastIP = WiFi.localIP();
        _state.broadcastIP[3] = 255;
        Serial.printf("WiFi reconnected, IP: %s\n", WiFi.localIP().toString().c_str());
    } else if (_state.staConnected && WiFi.status() != WL_CONNECTED) {
        _state.staConnected = false;
        _state.broadcastIP = IPAddress(192, 168, 4, 255);
        Serial.println("WiFi connection lost, using AP broadcast");
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

    // Add STA network broadcast if connected
    if ((mode == WIFI_AP_STA || mode == WIFI_STA) && _state.staConnected) {
        IPAddress staBroadcast = WiFi.localIP();
        staBroadcast[3] = 255;
        addresses.push_back(staBroadcast);
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
