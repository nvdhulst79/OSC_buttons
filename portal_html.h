// OSC-Muis - Niels van der Hulst 2026

#ifndef PORTAL_HTML_H
#define PORTAL_HTML_H

const char PORTAL_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>%PORTAL_TITLE%</title>
    <style>
        * { box-sizing: border-box; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
        body { margin: 0; padding: 20px; background: #1a1a2e; color: #eee; }
        .container { max-width: 400px; margin: 0 auto; }
        h1 { color: #00d4aa; margin-bottom: 5px; }
        .status { background: #16213e; padding: 15px; border-radius: 8px; margin: 15px 0; }
        .status-row { display: flex; justify-content: space-between; margin: 8px 0; }
        .label { color: #888; }
        .value { color: #00d4aa; font-weight: bold; }
        .battery { font-size: 1.5em; }
        .section { background: #16213e; padding: 15px; border-radius: 8px; margin: 15px 0; }
        h2 { margin-top: 0; color: #fff; font-size: 1.1em; }
        select, input[type="password"] {
            width: 100%%; padding: 12px; margin: 8px 0;
            border: 1px solid #333; border-radius: 4px;
            background: #0f0f23; color: #eee; font-size: 16px;
        }
        button {
            width: 100%%; padding: 14px; margin: 8px 0;
            border: none; border-radius: 4px;
            font-size: 16px; cursor: pointer;
            transition: opacity 0.2s;
        }
        button:hover { opacity: 0.9; }
        .btn-primary { background: #00d4aa; color: #000; }
        .btn-secondary { background: #333; color: #eee; }
        .btn-danger { background: #e74c3c; color: #fff; }
        .networks { max-height: 200px; overflow-y: auto; }
        .network {
            padding: 10px; margin: 5px 0; background: #0f0f23;
            border-radius: 4px; cursor: pointer;
        }
        .network:hover { background: #1a1a3e; }
        .network-name { font-weight: bold; }
        .network-signal { color: #888; font-size: 0.9em; }
        .hidden { display: none !important; }
        .message { padding: 10px; border-radius: 4px; margin: 10px 0; }
        .message.success { background: #00d4aa33; border: 1px solid #00d4aa; }
        .message.error { background: #e74c3c33; border: 1px solid #e74c3c; }
        .loader { border: 3px solid #333; border-top: 3px solid #00d4aa;
            border-radius: 50%%; width: 20px; height: 20px;
            animation: spin 1s linear infinite; display: inline-block; }
        @keyframes spin { 0%% { transform: rotate(0deg); } 100%% { transform: rotate(360deg); } }
    </style>
</head>
<body>
    <div class="container">
        <h1>%PORTAL_TITLE%</h1>
        <p style="color: #888; margin-top: 0;">%PORTAL_SUBTITLE%</p>

        <div class="status">
            <div class="status-row">
                <span class="label">Battery</span>
                <span class="value battery">%BATTERY%%%</span>
            </div>
            <div class="status-row">
                <span class="label">Mode</span>
                <span class="value">%MODE%</span>
            </div>
            <div class="status-row %STA_STATUS_CLASS%">
                <span class="label">Connected to</span>
                <span class="value">%STA_SSID%</span>
            </div>
            <div class="status-row %STA_STATUS_CLASS%">
                <span class="label">Station IP</span>
                <span class="value">%STA_IP%</span>
            </div>
        </div>

        <div class="status" style="margin-top: 20px;">
            <div class="status-row">
                <span class="label">AP Network</span>
                <span class="value">%AP_SSID%</span>
            </div>
            <div class="status-row">
                <span class="label">AP IP</span>
                <span class="value">%AP_IP%</span>
            </div>
            <div class="status-row">
                <span class="label">AP Clients</span>
                <span class="value">%AP_CLIENTS%</span>
            </div>
        </div>


        <div class="section">
            <h2>WiFi Configuration</h2>
            <div id="scanResult"></div>
            <button class="btn-secondary" onclick="scanNetworks()">
                <span id="scanText">Scan for Networks</span>
                <span id="scanLoader" class="loader hidden"></span>
            </button>

            <div id="connectForm" class="hidden">
                <input type="text" id="ssid" placeholder="Network name" readonly>
                <input type="password" id="password" placeholder="Password">
                <button class="btn-primary" onclick="connectWiFi()">Connect</button>
            </div>

            <div id="disconnectBtn" class="%DISCONNECT_CLASS%">
                <button class="btn-danger" onclick="disconnectWiFi()">Disconnect from WiFi</button>
            </div>
        </div>

        <div class="section">
            <h2>OSC Configuration</h2>
            <div class="status-row">
                <span class="label">Port</span>
                <span class="value">%OSC_PORT%</span>
            </div>
            <div class="status-row">
                <span class="label">Current Target</span>
                <span class="value" id="oscCurrentTarget">%OSC_TARGET_IP%:%OSC_PORT%</span>
            </div>
            <div class="status-row">
                <span class="label">Address Format</span>
                <span class="value" id="oscCurrentFormat">%OSC_ADDRESS_FORMAT%</span>
            </div>

            <input type="text" id="oscTargetIP" placeholder="Target IP (empty = broadcast)" value="">
            <input type="number" id="oscPort" placeholder="Port (default: 8001)" value="%OSC_PORT%" min="1" max="65535">
            <select id="oscFormat">
                <option value="/kmpush">/kmpushX (standard OSC)</option>
                <option value="kmpush">kmpushX (no leading slash)</option>
                <option value="/km/push/">/km/push/X (path style)</option>
            </select>
            <button class="btn-primary" onclick="saveOSC()">Save OSC Settings</button>
            <button class="btn-secondary" onclick="testOSC()">Test Button 1</button>
            <div id="oscMessage"></div>
        </div>

        <p style="color: #555; font-size: 0.75em; text-align: center; margin-top: 30px;">OSC-Muis - Niels van der Hulst 2026</p>
    </div>

    <script>
        let scanRetries = 0;
        const maxRetries = 10;
        let scanning = false;

        function showScanButton() {
            document.getElementById('scanText').classList.remove('hidden');
            document.getElementById('scanLoader').classList.add('hidden');
            scanning = false;
        }

        function showScanning() {
            document.getElementById('scanText').classList.add('hidden');
            document.getElementById('scanLoader').classList.remove('hidden');
            scanning = true;
        }

        function showNetworks(networks) {
            let html = '<div class="networks">';
            networks.forEach(n => {
                const ssid = n.ssid.replace(/'/g, "\\'");
                html += '<div class="network" onclick="selectNetwork(\'' + ssid + '\')">' +
                    '<span class="network-name">' + n.ssid + '</span>' +
                    '<span class="network-signal">' + n.rssi + ' dBm ' + (n.secure ? '&#x1f512;' : '') + '</span>' +
                    '</div>';
            });
            html += '</div>';
            document.getElementById('scanResult').innerHTML = html;
        }

        function scanNetworks() {
            if (scanning) return;  // Prevent double-clicks
            showScanning();
            document.getElementById('scanResult').innerHTML = '';
            scanRetries = 0;
            doScan();
        }

        function doScan() {
            fetch('/scan')
                .then(function(r) { return r.json(); })
                .then(function(data) {
                    // Check if still scanning
                    if (data && data.status === 'scanning') {
                        scanRetries++;
                        if (scanRetries < maxRetries) {
                            setTimeout(doScan, 1000);
                            return;
                        }
                        showScanButton();
                        document.getElementById('scanResult').innerHTML =
                            '<div class="message error">Scan timeout - try again</div>';
                        return;
                    }

                    // Got results
                    showScanButton();

                    if (!Array.isArray(data) || data.length === 0) {
                        document.getElementById('scanResult').innerHTML =
                            '<div class="message error">No networks found</div>';
                        return;
                    }

                    showNetworks(data);
                })
                .catch(function(e) {
                    showScanButton();
                    document.getElementById('scanResult').innerHTML =
                        '<div class="message error">Scan failed: ' + e.message + '</div>';
                });
        }

        // No auto-scan — captive portal mini-browsers have unreliable fetch

        function selectNetwork(ssid) {
            document.getElementById('ssid').value = ssid;
            document.getElementById('connectForm').classList.remove('hidden');
            document.getElementById('password').focus();
        }

        function connectWiFi() {
            const ssid = document.getElementById('ssid').value;
            const password = document.getElementById('password').value;

            fetch('/connect', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: `ssid=${encodeURIComponent(ssid)}&password=${encodeURIComponent(password)}`
            })
            .then(r => r.json())
            .then(result => {
                if (result.success) {
                    document.getElementById('scanResult').innerHTML =
                        '<div class="message success">Connected! Page will reload...</div>';
                    setTimeout(() => location.reload(), 2000);
                } else {
                    document.getElementById('scanResult').innerHTML =
                        `<div class="message error">${result.message}</div>`;
                }
            });
        }

        function disconnectWiFi() {
            fetch('/disconnect', {method: 'POST'})
                .then(() => location.reload());
        }

        function saveOSC() {
            const port = document.getElementById('oscPort').value;
            const targetip = document.getElementById('oscTargetIP').value;
            const addressFormat = document.getElementById('oscFormat').value;

            fetch('/osc', {
                method: 'POST',
                headers: {'Content-Type': 'application/x-www-form-urlencoded'},
                body: `port=${port}&targetip=${encodeURIComponent(targetip)}&addressFormat=${encodeURIComponent(addressFormat)}`
            })
            .then(r => r.json())
            .then(result => {
                if (result.success) {
                    document.getElementById('oscMessage').innerHTML =
                        '<div class="message success">Settings saved! Restart device to apply.</div>';
                    document.getElementById('oscCurrentTarget').textContent =
                        (targetip || 'broadcast') + ':' + port;
                    document.getElementById('oscCurrentFormat').textContent = addressFormat;
                }
            });
        }

        function testOSC() {
            fetch('/testosc', {method: 'POST'})
            .then(r => r.json())
            .then(result => {
                document.getElementById('oscMessage').innerHTML =
                    '<div class="message success">Sent: ' + result.address + ' to ' + result.target + '</div>';
            })
            .catch(e => {
                document.getElementById('oscMessage').innerHTML =
                    '<div class="message error">Test failed</div>';
            });
        }

        // Load current OSC format into dropdown
        window.addEventListener('load', function() {
            const currentFormat = '%OSC_ADDRESS_FORMAT%';
            const select = document.getElementById('oscFormat');
            for (let i = 0; i < select.options.length; i++) {
                if (select.options[i].value === currentFormat) {
                    select.selectedIndex = i;
                    break;
                }
            }
            // Load target IP
            const target = '%OSC_TARGET_IP%';
            if (target !== 'broadcast') {
                document.getElementById('oscTargetIP').value = target;
            }
        });
    </script>
</body>
</html>
)rawliteral";

#endif
