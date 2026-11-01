/**
 * @file BasicUpdate.ino
 * @brief Basic example demonstrating simple firmware update flow
 * 
 * This example shows the minimal code needed to check for and perform
 * a firmware update from GitHub. It assumes WiFi is already connected.
 * 
 * Hardware: ESP32
 * 
 * Required libraries:
 * - GitFirmwareUpdate
 * - WiFi (ESP32 Arduino Core)
 * - ArduinoJson
 */

#include <WiFi.h>

// HTTP = default. For HTTPS: create build_opt.h with -DGIT_FIRMWARE_USE_HTTPS
#include <GitFirmwareUpdate.h>
#include <DebugLog.h>

// WiFi credentials - replace with your network
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Firmware update configuration
#define FW_CURRENT_VERSION "1.0.0"  // Current firmware version on device

// Default build is HTTP-only (smaller binary). For HTTPS URLs use build_opt.h with:
//   -DGIT_FIRMWARE_USE_HTTPS
#if GIT_FIRMWARE_HTTP_ONLY
  const char* GITHUB_LATEST_URL =
    "http://ux-standards.org/hw/firmware/latest.json";
#else
  const char* GITHUB_LATEST_URL =
    "https://raw.githubusercontent.com/mLihs/PulseFanSync/main/firmware/latest.json";
#endif

// Create firmware update instance
GitFirmwareUpdate fwUpdate(FW_CURRENT_VERSION, GITHUB_LATEST_URL);

void setup() {
  Serial.begin(115200);
  delay(1000);

  LOGI(F("\n=== GitFirmwareUpdate Basic Example ===\n"));

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

  // Check for updates
  LOGI(F("Checking for firmware updates..."));
  LOGI_F("Current version: %s", FW_CURRENT_VERSION);
  LOGI(F(""));

  if (fwUpdate.checkForUpdate()) {
    LOGI(F("New firmware version found!"));
    LOGI_F("Remote version: %s", fwUpdate.getRemoteVersion());
    LOGI_F("Firmware URL: %s", fwUpdate.getFirmwareUrl());
    
    const char* notes = fwUpdate.getReleaseNotes();
    if (notes && notes[0] != '\0') {
      LOGI_F("Release notes: %s", notes);
    }
    LOGI(F(""));

    // Perform the update
    LOGI(F("Starting firmware update..."));
    LOGI(F("This will take a few minutes. Do not power off the device."));
    LOGI(F(""));

    if (fwUpdate.performUpdate()) {
      // This line is never reached - device restarts on success
      LOGI(F("Update completed successfully!"));
    } else {
      LOGE(F("Update failed!"));
      LOGE_F("Error: %s", fwUpdate.getLastErrorString());
      LOGE_F("Error code: %d", fwUpdate.getLastError());
    }
  } else {
    LOGI(F("No update available."));
    LOGI_F("Error (if any): %s", fwUpdate.getLastErrorString());
  }
}

void loop() {
  // Nothing to do here - update happens in setup()
  // If update fails, you can retry by calling checkForUpdate() and performUpdate()
  delay(10000);
  
  // Optional: Check for updates periodically
  // LOGI(F("Checking for updates again..."));
  // if (fwUpdate.checkForUpdate()) {
  //   fwUpdate.performUpdate();
  // }
}


