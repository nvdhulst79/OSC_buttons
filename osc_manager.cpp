// OSC-Muis - Niels van der Hulst 2026

#include "osc_manager.h"
#include "wifi_manager.h"
#include <OSCMessage.h>

// Static instance pointer for web callbacks
static OSCManager* _oscInstance = nullptr;

// Static template processor callback for WiFiManager
static String oscTemplateProcessor(const String& var) {
    if (!_oscInstance) return String();

    if (var == "OSC_PORT") return String(_oscInstance->getPort());
    if (var == "OSC_TARGET_IP") {
        String target = _oscInstance->getTargetIP();
        return target.length() > 0 ? target : "broadcast";
    }
    if (var == "OSC_ADDRESS_FORMAT") return _oscInstance->getAddressFormat();

    return String();  // Variable not handled
}

OSCManager::OSCManager() {
    _wifiManager = nullptr;
    _state.port = 8001;  // LuPlayer default incoming port
    _state.targetIP = "";  // Empty = broadcast
    _state.addressFormat = "/kmpush";  // Default format for Keyboard Mapped mode
    _state.testRequested = false;
}

void OSCManager::begin(AsyncWebServer& webServer, WiFiManager& wifiManager) {
    _wifiManager = &wifiManager;
    _oscInstance = this;

    // Load saved settings
    loadSettings();

    // Register web endpoints
    registerWebEndpoints(webServer);

    // Register template processor callback with WiFiManager
    wifiManager.registerTemplateCallback(oscTemplateProcessor);

    Serial.printf("OSC configured: port=%d, target=%s, format=%s\n",
        _state.port,
        _state.targetIP.length() > 0 ? _state.targetIP.c_str() : "broadcast",
        _state.addressFormat.c_str());
}

void OSCManager::loadSettings() {
    _preferences.begin("osc", true);
    _state.port = _preferences.getInt("port", 8001);
    _state.targetIP = _preferences.getString("targetip", "");
    _state.addressFormat = _preferences.getString("addrfmt", "/kmpush");
    _preferences.end();
}

void OSCManager::saveSettings() {
    _preferences.begin("osc", false);
    _preferences.putInt("port", _state.port);
    _preferences.putString("targetip", _state.targetIP);
    _preferences.putString("addrfmt", _state.addressFormat);
    _preferences.end();
}

void OSCManager::registerWebEndpoints(AsyncWebServer& webServer) {
    // Get OSC settings
    webServer.on("/osc", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!_oscInstance) {
            request->send(500, "application/json", "{\"error\":\"OSC not initialized\"}");
            return;
        }
        String json = "{";
        json += "\"port\":" + String(_oscInstance->_state.port) + ",";
        json += "\"targetip\":\"" + _oscInstance->_state.targetIP + "\",";
        json += "\"addressFormat\":\"" + _oscInstance->_state.addressFormat + "\"";
        json += "}";
        request->send(200, "application/json", json);
    });

    // Save OSC settings
    webServer.on("/osc", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!_oscInstance) {
            request->send(500, "application/json", "{\"error\":\"OSC not initialized\"}");
            return;
        }

        bool changed = false;

        if (request->hasParam("port", true)) {
            int port = request->getParam("port", true)->value().toInt();
            if (port > 0 && port < 65536) {
                _oscInstance->_state.port = port;
                changed = true;
            }
        }

        if (request->hasParam("targetip", true)) {
            _oscInstance->_state.targetIP = request->getParam("targetip", true)->value();
            changed = true;
        }

        if (request->hasParam("addressFormat", true)) {
            _oscInstance->_state.addressFormat = request->getParam("addressFormat", true)->value();
            changed = true;
        }

        if (changed) {
            _oscInstance->saveSettings();

            Serial.printf("OSC settings saved: port=%d, target=%s, format=%s\n",
                _oscInstance->_state.port,
                _oscInstance->_state.targetIP.length() > 0 ? _oscInstance->_state.targetIP.c_str() : "broadcast",
                _oscInstance->_state.addressFormat.c_str());
        }

        request->send(200, "application/json", "{\"success\":true}");
    });

    // Test OSC - sets a flag that the main sketch checks
    webServer.on("/testosc", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!_oscInstance || !_oscInstance->_wifiManager) {
            request->send(500, "application/json", "{\"error\":\"OSC not initialized\"}");
            return;
        }

        String address = _oscInstance->formatAddress(1);
        std::vector<IPAddress> targets = _oscInstance->getTargetIPAddresses();

        // Build response with all targets
        String json = "{\"address\":\"" + address + "\",\"targets\":[";
        for (size_t i = 0; i < targets.size(); i++) {
            if (i > 0) json += ",";
            json += "\"" + targets[i].toString() + ":" + String(_oscInstance->_state.port) + "\"";
        }
        json += "]}";

        request->send(200, "application/json", json);

        // Set flag for main loop to send test message
        _oscInstance->_state.testRequested = true;
        Serial.println("OSC test requested via web UI");
    });
}

void OSCManager::setPort(int port) {
    _state.port = port;
}

int OSCManager::getPort() const {
    return _state.port;
}

void OSCManager::setTargetIP(const String& ip) {
    _state.targetIP = ip;
}

String OSCManager::getTargetIP() const {
    return _state.targetIP;
}

void OSCManager::setAddressFormat(const String& format) {
    _state.addressFormat = format;
}

String OSCManager::getAddressFormat() const {
    return _state.addressFormat;
}

std::vector<IPAddress> OSCManager::getTargetIPAddresses() const {
    std::vector<IPAddress> targets;

    // If custom target IP is specified, use only that
    if (_state.targetIP.length() > 0) {
        IPAddress ip;
        if (ip.fromString(_state.targetIP)) {
            targets.push_back(ip);
            return targets;
        }
    }

    // Broadcasting mode - get all broadcast IPs from WiFi manager
    if (_wifiManager) {
        return _wifiManager->getBroadcastIPAddresses();
    }

    // Fallback
    targets.push_back(IPAddress(192, 168, 4, 255));
    return targets;
}

String OSCManager::formatAddress(int buttonNumber) const {
    String addr = _state.addressFormat;

    // If format ends with a number placeholder or nothing, append the button number
    // Supported formats: "/kmpush", "kmpush", "/km/push/"
    if (addr.endsWith("/")) {
        // Format like "/km/push/" -> "/km/push/1"
        addr += String(buttonNumber);
    } else {
        // Format like "/kmpush" or "kmpush" -> "/kmpush1" or "kmpush1"
        addr += String(buttonNumber);
    }

    return addr;
}

void OSCManager::sendButton(WiFiUDP& udp, int buttonNumber) {
    // Build the OSC address using configured format
    String address = formatAddress(buttonNumber);

    // Get all target IPs (will be multiple when in AP+STA mode)
    std::vector<IPAddress> targets = getTargetIPAddresses();

    // Send to all targets
    for (const IPAddress& targetIP : targets) {
        OSCMessage msg(address.c_str());
        msg.add(1.0f);  // Send a single float value of 1.0 (common trigger format)

        udp.beginPacket(targetIP, _state.port);
        msg.send(udp);
        udp.endPacket();
        msg.empty();

        Serial.printf("OSC sent: %s -> %s:%d (value=1.0)\n",
            address.c_str(), targetIP.toString().c_str(), _state.port);
    }
}

bool OSCManager::checkAndClearTestRequest() {
    if (_state.testRequested) {
        _state.testRequested = false;
        return true;
    }
    return false;
}
