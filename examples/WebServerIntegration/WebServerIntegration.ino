/**
 * @file WebServerIntegration.ino
 * @brief Example showing integration with WebServer for web-based updates
 * 
 * This example demonstrates how to integrate GitFirmwareUpdate with
 * ESP32's WebServer to provide web-based firmware update functionality.
 * Users can trigger updates via HTTP endpoints.
 * 
 * Hardware: ESP32
 * 
 * Required libraries:
 * - GitFirmwareUpdate
 * - WebServer (ESP32 Arduino Core)
 * - WiFi (ESP32 Arduino Core)
 * - ArduinoJson
 */

#include <WiFi.h>
#include <WebServer.h>

// HTTP = default. For HTTPS: create build_opt.h with -DGIT_FIRMWARE_USE_HTTPS
#include <GitFirmwareUpdate.h>
#include <DebugLog.h>

// WiFi credentials - replace with your network
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Firmware update configuration
#define FW_CURRENT_VERSION "1.0.0"  // Current firmware version on device

#if GIT_FIRMWARE_HTTP_ONLY
  const char* GITHUB_LATEST_URL =
    "http://ux-standards.org/hw/firmware/latest.json";
#else
  const char* GITHUB_LATEST_URL =
    "https://raw.githubusercontent.com/mLihs/PulseFanSync/main/firmware/latest.json";
#endif

// Web server on port 80
WebServer server(80);

// Create firmware update instance
GitFirmwareUpdate fwUpdate(FW_CURRENT_VERSION, GITHUB_LATEST_URL);

// Progress tracking for web interface
int updateProgress = 0;
bool updateInProgress = false;
String lastError = "";

// Progress callback for web interface
void onProgress(int percent, size_t bytesRead, size_t totalBytes) {
  updateProgress = percent;
  LOGD_F("Update progress: %d%% (%u bytes)", percent, (unsigned)bytesRead);
}

// Web server handlers
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<title>Firmware Update</title>";
  html += "<style>body{font-family:Arial;max-width:600px;margin:50px auto;padding:20px;}";
  html += "button{padding:10px 20px;margin:5px;cursor:pointer;}";
  html += ".status{padding:10px;margin:10px 0;border-radius:5px;}";
  html += ".success{background:#d4edda;color:#155724;}";
  html += ".error{background:#f8d7da;color:#721c24;}";
  html += ".info{background:#d1ecf1;color:#0c5460;}";
  html += "</style></head><body>";
  html += "<h1>Firmware Update</h1>";
  html += "<p><strong>Current Version:</strong> " + String(FW_CURRENT_VERSION) + "</p>";
  
  if (updateInProgress) {
    html += "<div class='status info'>Update in progress: " + String(updateProgress) + "%</div>";
  } else if (lastError.length() > 0) {
    html += "<div class='status error'>Last Error: " + lastError + "</div>";
  }
  
  html += "<h2>Actions</h2>";
  html += "<button onclick='checkUpdate()'>Check for Updates</button>";
  html += "<button onclick='startUpdate()'>Start Update</button>";
  html += "<button onclick='getStatus()'>Refresh Status</button>";
  
  html += "<h2>Status</h2>";
  html += "<div id='status'></div>";
  
  html += "<script>";
  html += "function checkUpdate(){";
  html += "  fetch('/api/check').then(r=>r.json()).then(d=>{";
  html += "    document.getElementById('status').innerHTML='<div class=\"status '+(d.hasUpdate?'info':'success')+'\">'";
  html += "      +(d.hasUpdate?'Update available: '+d.version:'No update available')+'</div>';";
  html += "  });";
  html += "}";
  html += "function startUpdate(){";
  html += "  document.getElementById('status').innerHTML='<div class=\"status info\">Starting update...</div>';";
  html += "  fetch('/api/update',{method:'POST'}).then(r=>r.text()).then(d=>{";
  html += "    document.getElementById('status').innerHTML='<div class=\"status info\">'+d+'</div>';";
  html += "  });";
  html += "}";
  html += "function getStatus(){";
  html += "  fetch('/api/status').then(r=>r.json()).then(d=>{";
  html += "    let html='<p><strong>Remote Version:</strong> '+(d.remoteVersion||'Not checked')+'</p>';";
  html += "    html+='<p><strong>Firmware URL:</strong> '+(d.firmwareUrl||'N/A')+'</p>';";
  html += "    if(d.releaseNotes) html+='<p><strong>Release Notes:</strong> '+d.releaseNotes+'</p>';";
  html += "    document.getElementById('status').innerHTML=html;";
  html += "  });";
  html += "}";
  html += "</script>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

void handleCheckUpdate() {
  LOGD(F("API: Check for update requested"));
  
  if (fwUpdate.checkForUpdate()) {
    String json = "{";
    json += "\"hasUpdate\":true,";
    json += "\"version\":\""; json += fwUpdate.getRemoteVersion(); json += "\",";
    json += "\"url\":\""; json += fwUpdate.getFirmwareUrl(); json += "\",";
    json += "\"notes\":\""; json += fwUpdate.getReleaseNotes(); json += "\"";
    json += "}";
    server.send(200, "application/json", json);
  } else {
    String json = "{";
    json += "\"hasUpdate\":false,";
    json += "\"error\":\""; json += fwUpdate.getLastErrorString(); json += "\"";
    json += "}";
    server.send(200, "application/json", json);
  }
}

void handleStartUpdate() {
  LOGD(F("API: Start update requested"));
  
  if (updateInProgress) {
    server.send(200, "text/plain", "Update already in progress");
    return;
  }
  
  if (!fwUpdate.checkForUpdate()) {
    String msg = "No update available: ";
    msg += fwUpdate.getLastErrorString();
    server.send(200, "text/plain", msg);
    return;
  }
  
  // Start update in background (non-blocking would require async, but for simplicity we'll block)
  updateInProgress = true;
  updateProgress = 0;
  lastError = "";
  
  server.send(200, "text/plain", "Update started. Device will restart when complete.");
  
  // Perform update (this will restart the device on success)
  if (!fwUpdate.performUpdate()) {
    updateInProgress = false;
    lastError = fwUpdate.getLastErrorString();
    LOGE_F("Update failed: %s", lastError.c_str());
  }
}

void handleStatus() {
  String json = "{";
  json += "\"currentVersion\":\""; json += FW_CURRENT_VERSION; json += "\",";
  json += "\"remoteVersion\":\""; json += fwUpdate.getRemoteVersion(); json += "\",";
  json += "\"firmwareUrl\":\""; json += fwUpdate.getFirmwareUrl(); json += "\",";
  json += "\"releaseNotes\":\""; json += fwUpdate.getReleaseNotes(); json += "\",";
  json += "\"updateInProgress\":"; json += (updateInProgress ? "true" : "false"); json += ",";
  json += "\"updateProgress\":"; json += String(updateProgress);
  json += "}";
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  LOGI(F("\n=== GitFirmwareUpdate WebServer Integration Example ===\n"));

  // Configure firmware update
  fwUpdate.setProgressCallback(onProgress);
  fwUpdate.setTimeout(60000);

  // Connect to WiFi
  LOGI_F("Connecting to WiFi: %s", ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println();
  LOGI_F("WiFi connected! IP address: %s", WiFi.localIP().toString().c_str());
  LOGI(F(""));

  // Setup web server routes
  server.on("/", handleRoot);
  server.on("/api/check", handleCheckUpdate);
  server.on("/api/update", HTTP_POST, handleStartUpdate);
  server.on("/api/status", handleStatus);

  // Start web server
  server.begin();
  LOGI(F("Web server started!"));
  LOGI_F("Open http://%s in your browser", WiFi.localIP().toString().c_str());
  LOGI(F(""));
}

void loop() {
  server.handleClient();
  delay(10);
}


