/**
 * @file AsyncWebServerIntegration.ino
 * @brief Example showing integration with ESPAsyncWebServer for web-based updates
 * 
 * This example demonstrates how to integrate GitFirmwareUpdate with
 * ESPAsyncWebServer to provide web-based firmware update functionality.
 * Users can trigger updates via HTTP endpoints.
 * 
 * This is an async version of WebServerIntegration.ino, using ESPAsyncWebServer
 * instead of the synchronous WebServer for better performance and non-blocking
 * operation.
 * 
 * Hardware: ESP32
 * 
 * Required libraries:
 * - GitFirmwareUpdate
 * - ESPAsyncWebServer (https://github.com/me-no-dev/ESPAsyncWebServer)
 * - AsyncTCP (dependency of ESPAsyncWebServer)
 * - WiFi (ESP32 Arduino Core)
 * - ArduinoJson
 */

#include <WiFi.h>
#include <ESPAsyncWebServer.h>

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

// Async web server on port 80
AsyncWebServer server(80);

// Create firmware update instance
GitFirmwareUpdate fwUpdate(FW_CURRENT_VERSION, GITHUB_LATEST_URL);

// Progress tracking for web interface
int updateProgress = 0;
bool updateInProgress = false;
String lastError = "";
bool updateScheduled = false;  // Flag to schedule update from loop()

// FreeRTOS task handle for update task
TaskHandle_t updateTaskHandle = NULL;

// Progress callback for web interface
void onProgress(int percent, size_t bytesRead, size_t totalBytes) {
  updateProgress = percent;
  LOGD_F("Update progress: %d%% (%u bytes)", percent, (unsigned)bytesRead);
}

// Server handle callback - ESPAsyncWebServer doesn't need this, but we keep it
// for compatibility with GitFirmwareUpdate API
void onServerHandle() {
  // ESPAsyncWebServer handles requests asynchronously, so this is a no-op
  // but we keep it for API compatibility
  // Cooperative yield to allow async_tcp task to run
  yield();
}

// Forward declaration
void updateTask(void *parameter);

// Generate HTML page
String generateHTML() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Firmware Update</title>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;max-width:600px;margin:50px auto;padding:20px;background:#f5f5f5;}";
  html += "h1{color:#333;border-bottom:2px solid #007bff;padding-bottom:10px;}";
  html += "h2{color:#555;margin-top:30px;}";
  html += "button{padding:10px 20px;margin:5px;cursor:pointer;background:#007bff;color:white;border:none;border-radius:5px;font-size:14px;}";
  html += "button:hover{background:#0056b3;}";
  html += "button:disabled{background:#ccc;cursor:not-allowed;}";
  html += ".status{padding:15px;margin:15px 0;border-radius:5px;border-left:4px solid;}";
  html += ".success{background:#d4edda;color:#155724;border-color:#28a745;}";
  html += ".error{background:#f8d7da;color:#721c24;border-color:#dc3545;}";
  html += ".info{background:#d1ecf1;color:#0c5460;border-color:#17a2b8;}";
  html += ".warning{background:#fff3cd;color:#856404;border-color:#ffc107;}";
  html += "#status{margin-top:20px;}";
  html += ".version-info{background:white;padding:15px;border-radius:5px;margin:15px 0;}";
  html += "</style></head><body>";
  html += "<h1>Firmware Update</h1>";
  html += "<div class='version-info'>";
  html += "<p><strong>Current Version:</strong> " + String(FW_CURRENT_VERSION) + "</p>";
  html += "</div>";
  
  if (updateInProgress) {
    html += "<div class='status info'>Update in progress: " + String(updateProgress) + "%</div>";
  } else if (lastError.length() > 0) {
    html += "<div class='status error'>Last Error: " + lastError + "</div>";
  }
  
  html += "<h2>Actions</h2>";
  html += "<button onclick='checkUpdate()' id='btnCheck'>Check for Updates</button>";
  html += "<button onclick='startUpdate()' id='btnUpdate' disabled>Start Update</button>";
  html += "<button onclick='getStatus()' id='btnStatus'>Refresh Status</button>";
  
  html += "<h2>Status</h2>";
  html += "<div id='status'></div>";
  
  html += "<script>";
  html += "let updateAvailable = false;";
  html += "function checkUpdate(){";
  html += "  document.getElementById('btnCheck').disabled = true;";
  html += "  document.getElementById('status').innerHTML='<div class=\"status info\">Checking for updates...</div>';";
  html += "  fetch('/api/check').then(r=>r.json()).then(d=>{";
  html += "    updateAvailable = d.hasUpdate;";
  html += "    document.getElementById('btnUpdate').disabled = !d.hasUpdate;";
  html += "    document.getElementById('btnCheck').disabled = false;";
  html += "    const statusClass = d.hasUpdate ? 'info' : 'success';";
  html += "    const statusText = d.hasUpdate ? 'Update available: ' + d.version : 'No update available';";
  html += "    document.getElementById('status').innerHTML='<div class=\"status \"+statusClass+\">'+statusText+'</div>';";
  html += "    if(d.notes) document.getElementById('status').innerHTML+='<div class=\"status info\">Release Notes: '+d.notes+'</div>';";
  html += "  }).catch(e=>{";
  html += "    document.getElementById('btnCheck').disabled = false;";
  html += "    document.getElementById('status').innerHTML='<div class=\"status error\">Error: '+e+'</div>';";
  html += "  });";
  html += "}";
  html += "function startUpdate(){";
  html += "  if(!updateAvailable) return;";
  html += "  if(!confirm('This will restart the device. Continue?')) return;";
  html += "  document.getElementById('btnUpdate').disabled = true;";
  html += "  document.getElementById('btnCheck').disabled = true;";
  html += "  document.getElementById('status').innerHTML='<div class=\"status info\">Starting update... This may take a few minutes.</div>';";
  html += "  fetch('/api/update',{method:'POST'}).then(r=>r.text()).then(d=>{";
  html += "    document.getElementById('status').innerHTML='<div class=\"status info\">'+d+'</div>';";
  html += "    setTimeout(()=>{";
  html += "      document.getElementById('status').innerHTML+='<div class=\"status warning\">If update succeeded, device will restart. If not, check Serial output.</div>';";
  html += "    }, 2000);";
  html += "  }).catch(e=>{";
  html += "    document.getElementById('btnUpdate').disabled = false;";
  html += "    document.getElementById('status').innerHTML='<div class=\"status error\">Error: '+e+'</div>';";
  html += "  });";
  html += "}";
  html += "function getStatus(){";
  html += "  fetch('/api/status').then(r=>r.json()).then(d=>{";
  html += "    let html='<div class=\"version-info\">';";
  html += "    html+='<p><strong>Current Version:</strong> '+d.currentVersion+'</p>';";
  html += "    html+='<p><strong>Remote Version:</strong> '+(d.remoteVersion||'Not checked')+'</p>';";
  html += "    html+='<p><strong>Firmware URL:</strong> '+(d.firmwareUrl||'N/A')+'</p>';";
  html += "    html+='<p><strong>Update In Progress:</strong> '+(d.updateInProgress?'Yes':'No')+'</p>';";
  html += "    if(d.updateInProgress) html+='<p><strong>Progress:</strong> '+d.updateProgress+'%</p>';";
  html += "    html+='</div>';";
  html += "    if(d.releaseNotes) html+='<div class=\"status info\"><strong>Release Notes:</strong> '+d.releaseNotes+'</div>';";
  html += "    document.getElementById('status').innerHTML=html;";
  html += "  }).catch(e=>{";
  html += "    document.getElementById('status').innerHTML='<div class=\"status error\">Error: '+e+'</div>';";
  html += "  });";
  html += "}";
  html += "</script>";
  html += "</body></html>";
  return html;
}

// Root handler
void handleRoot(AsyncWebServerRequest *request) {
  request->send(200, "text/html", generateHTML());
}

// Check for update handler
void handleCheckUpdate(AsyncWebServerRequest *request) {
  LOGD(F("API: Check for update requested"));
  
  if (fwUpdate.checkForUpdate()) {
    String json = "{";
    json += "\"hasUpdate\":true,";
    json += "\"version\":\""; json += fwUpdate.getRemoteVersion(); json += "\",";
    json += "\"url\":\""; json += fwUpdate.getFirmwareUrl(); json += "\",";
    json += "\"notes\":\""; json += fwUpdate.getReleaseNotes(); json += "\"";
    json += "}";
    request->send(200, "application/json", json);
  } else {
    String json = "{";
    json += "\"hasUpdate\":false,";
    json += "\"error\":\""; json += fwUpdate.getLastErrorString(); json += "\"";
    json += "}";
    request->send(200, "application/json", json);
  }
}

// Start update handler - MUST return immediately (HTTP 202)
void handleStartUpdate(AsyncWebServerRequest *request) {
  LOGD(F("API: Start update requested"));
  
  if (updateInProgress || updateScheduled) {
    request->send(200, "text/plain", "Update already in progress or scheduled");
    return;
  }
  
  // Quick check if update is available (this is fast, but still blocking)
  // For production, consider caching this from /api/check
  if (!fwUpdate.checkForUpdate()) {
    String msg = "No update available: ";
    msg += fwUpdate.getLastErrorString();
    request->send(200, "text/plain", msg);
    return;
  }
  
  // Schedule update - do NOT perform it here
  updateScheduled = true;
  updateInProgress = true;
  updateProgress = 0;
  lastError = "";
  
  // Return HTTP 202 Accepted - update will be processed asynchronously
  request->send(202, "text/plain", "Update scheduled. Device will restart when complete.");
  
  // Create FreeRTOS task for update (runs on core 1, separate from async_tcp on core 0)
  // This prevents watchdog timeout on async_tcp task
  if (updateTaskHandle == NULL) {
    xTaskCreatePinnedToCore(
      updateTask,           // Task function
      "UpdateTask",         // Task name
      16384,                // Stack size (16KB - enough for update operations)
      NULL,                 // Parameters
      1,                    // Priority (lower than async_tcp)
      &updateTaskHandle,    // Task handle
      1                     // Core ID (use core 1, async_tcp typically runs on core 0)
    );
    
    if (updateTaskHandle == NULL) {
      updateInProgress = false;
      updateScheduled = false;
      lastError = "Failed to create update task";
      LOGE(F("Failed to create update task"));
    }
  }
}

// Status handler
void handleStatus(AsyncWebServerRequest *request) {
  String json = "{";
  json += "\"currentVersion\":\""; json += FW_CURRENT_VERSION; json += "\",";
  json += "\"remoteVersion\":\""; json += fwUpdate.getRemoteVersion(); json += "\",";
  json += "\"firmwareUrl\":\""; json += fwUpdate.getFirmwareUrl(); json += "\",";
  json += "\"releaseNotes\":\""; json += fwUpdate.getReleaseNotes(); json += "\",";
  json += "\"updateInProgress\":"; json += (updateInProgress ? "true" : "false"); json += ",";
  json += "\"updateProgress\":"; json += String(updateProgress);
  json += "}";
  request->send(200, "application/json", json);
}

// 404 handler
void handleNotFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}

// Update task function - runs in separate FreeRTOS task
void updateTask(void *parameter) {
  LOGD(F("Update task started"));
  
  // Perform update (this will block, but in separate task)
  // This allows async_tcp task to continue running and reset watchdog
  if (!fwUpdate.performUpdate()) {
    updateInProgress = false;
    updateScheduled = false;
    lastError = fwUpdate.getLastErrorString();
    LOGE_F("Update failed: %s", lastError.c_str());
  }
  // If successful, device will restart, so we never reach here
  
  updateTaskHandle = NULL;
  updateScheduled = false;
  vTaskDelete(NULL);  // Delete this task
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  LOGI(F("\n=== GitFirmwareUpdate AsyncWebServer Integration Example ===\n"));

  // Configure firmware update
  fwUpdate.setProgressCallback(onProgress);
  fwUpdate.setServerHandleCallback(onServerHandle);
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

  // Setup async web server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/check", HTTP_GET, handleCheckUpdate);
  server.on("/api/update", HTTP_POST, handleStartUpdate);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.onNotFound(handleNotFound);

  // Start async web server
  server.begin();
  LOGI(F("Async web server started!"));
  LOGI_F("Open http://%s in your browser", WiFi.localIP().toString().c_str());
  LOGI(F(""));
}

void loop() {
  // ESPAsyncWebServer handles requests asynchronously, so no need to call
  // handleClient() in the loop. However, we can add other non-blocking tasks here.
  
  // Alternative: If you prefer to run update in loop() instead of FreeRTOS task,
  // uncomment this and remove the updateTask() function:
  /*
  if (updateScheduled && !updateInProgress) {
    updateInProgress = true;
    if (!fwUpdate.performUpdate()) {
      updateInProgress = false;
      updateScheduled = false;
      lastError = fwUpdate.getLastErrorString();
      LOGE(String("Update failed: " + lastError).c_str());
    }
  }
  */
  
  delay(100);
}

