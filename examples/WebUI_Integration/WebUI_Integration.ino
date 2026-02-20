/**
 * @file WebUI_Integration.ino
 * @brief Example showing GitFirmwareUpdate with PulseFanSyncWebUi
 * 
 * This example demonstrates how to integrate GitFirmwareUpdate with
 * the WebUI using PulseFanSyncWebUi library. The WebUI HTML is embedded
 * in the PulseFanSyncWebUi library as PROGMEM.
 * 
 * Hardware: ESP32
 * 
 * Required libraries:
 * - GitFirmwareUpdate
 * - PulseFanSyncWebUi
 * - WiFi (ESP32 Arduino Core)
 * - WiFiManager (optional, for WiFi setup)
 * - ArduinoJson
 * 
 * Setup:
 * 1. Update FW_CURRENT_VERSION and GITHUB_LATEST_URL
 * 2. Upload and connect to WiFi
 * 3. Open http://esp32-setup.local or the device IP in browser
 */

#include <WiFi.h>
#include <PulseFanSyncWebUi.h>

// HTTP = default. For HTTPS: create build_opt.h with -DGIT_FIRMWARE_USE_HTTPS
#include <GitFirmwareUpdate.h>
#include <DebugLog.h>

// Include FirmwareUpdateIntegration
#include "integrations/FirmwareUpdateIntegration.h"

// Optional: WiFiManager for WiFi setup
// Uncomment to use WiFiManager instead of hardcoded credentials
// #include <WiFiManager.h>
// WiFiManager wm;

// Firmware update configuration
#define FW_CURRENT_VERSION "1.0.0"  // Current firmware version on device

#if GIT_FIRMWARE_HTTP_ONLY
  const char* GITHUB_LATEST_URL =
    "http://ux-standards.org/hw/firmware/latest.json";
#else
  const char* GITHUB_LATEST_URL =
    "https://raw.githubusercontent.com/mLihs/PulseFanSync/main/firmware/latest.json";
#endif

// WiFi credentials (if not using WiFiManager)
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Create WebUI instance (owns WebServer)
PulseFanSyncWebUi webUI;

// Create firmware update instance
GitFirmwareUpdate fwUpdate(FW_CURRENT_VERSION, GITHUB_LATEST_URL);

// Integration will be created in setup() after webUI is initialized
FirmwareUpdateIntegration* fwUpdateIntegration = nullptr;

void setup() {
  Serial.begin(115200);
  delay(1000);

  LOGI(F("\n=== GitFirmwareUpdate WebUI Integration Example ===\n"));

  // Connect to WiFi
  LOGI_F("Connecting to WiFi: %s", ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  // Alternative: Use WiFiManager (uncomment if using)
  // wm.setConfigPortalTimeout(180);
  // if (!wm.autoConnect("ESP32-Setup")) {
  //   LOGE(F("Failed to connect, restarting..."));
  //   delay(3000);
  //   ESP.restart();
  // }
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println();
  LOGI_F("WiFi connected! IP address: %s", WiFi.localIP().toString().c_str());

  // Initialize WebUI (handles mDNS, WebServer, HTML serving)
  if (webUI.begin("esp32-setup")) {
    LOGI(F("WebUI initialized successfully"));
  } else {
    LOGE(F("WebUI initialization failed!"));
    return;  // Can't continue without WebUI
  }

  // Create firmware update integration (must be after webUI.begin())
  static FirmwareUpdateIntegration fwIntegration(webUI.getServer(), fwUpdate);
  fwUpdateIntegration = &fwIntegration;

  // Register firmware update integration
  webUI.registerFirmwareUpdateIntegration(fwUpdateIntegration);
  
  // Inject firmware version for display in WebUI
  webUI.injectFirmwareVersion(FW_CURRENT_VERSION);

  // Note: Root routes (/ and /setup) are automatically registered by webUI.begin()
  // No need to register them manually

  LOGI(F("Web server started!"));
  LOGI_F("Open http://esp32-setup.local or http://%s in your browser", WiFi.localIP().toString().c_str());
  LOGI(F(""));
}

void loop() {
  // Handle WebServer requests (compatible with WiFiManager)
  webUI.handleClient();
  
  // If using WiFiManager, also call:
  // wm.process();
  
  delay(10);
}
