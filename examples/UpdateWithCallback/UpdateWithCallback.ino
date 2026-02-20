/**
 * @file UpdateWithCallback.ino
 * @brief Example demonstrating progress callbacks and error handling
 * 
 * This example shows how to use progress callbacks to monitor firmware
 * download progress and handle errors gracefully.
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

#if GIT_FIRMWARE_HTTP_ONLY
  const char* GITHUB_LATEST_URL =
    "http://ux-standards.org/hw/firmware/latest.json";
#else
  const char* GITHUB_LATEST_URL =
    "https://raw.githubusercontent.com/mLihs/PulseFanSync/main/firmware/latest.json";
#endif

// Create firmware update instance
GitFirmwareUpdate fwUpdate(FW_CURRENT_VERSION, GITHUB_LATEST_URL);

// Progress callback function
void onProgress(int percent, size_t bytesRead, size_t totalBytes) {
  // Update progress display
  static int lastPercent = -1;
  
  if (percent != lastPercent) {
    lastPercent = percent;
    
    if (totalBytes > 0) {
      LOGD_F("Download progress: %d%% (%u / %u bytes)", percent, (unsigned)bytesRead, (unsigned)totalBytes);
      
      // Optional: Update a progress bar or LED indicator here
      // For example, update an OLED display or set LED brightness
    } else {
      LOGD_F("Downloaded: %u bytes (size unknown)", (unsigned)bytesRead);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  LOGI(F("\n=== GitFirmwareUpdate Callback Example ===\n"));

  // Configure firmware update settings
  fwUpdate.setProgressCallback(onProgress);  // Register progress callback
  fwUpdate.setTimeout(60000);                 // Set 60 second timeout
  fwUpdate.setRetryCount(2);                 // Retry up to 2 times on failure
  fwUpdate.setCertificateValidation(false);   // Skip cert validation (default)

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
    LOGI(F("✓ New firmware version found!"));
    LOGI_F("  Remote version: %s", fwUpdate.getRemoteVersion());
    LOGI_F("  Firmware URL: %s", fwUpdate.getFirmwareUrl());
    
    const char* notes = fwUpdate.getReleaseNotes();
    if (notes && notes[0] != '\0') {
      LOGI_F("  Release notes: %s", notes);
    }
    LOGI(F(""));

    // Perform the update with progress reporting
    LOGI(F("Starting firmware update..."));
    LOGI(F("Progress will be reported via callback."));
    LOGI(F("Do not power off the device during update."));
    LOGI(F(""));

    if (fwUpdate.performUpdate()) {
      // This line is never reached - device restarts on success
      LOGI(F("Update completed successfully!"));
    } else {
      // Handle update failure
      LOGE(F("✗ Update failed!"));
      LOGE_F("  Error: %s", fwUpdate.getLastErrorString());
      LOGE_F("  Error code: %d", fwUpdate.getLastError());
      
      // You can retry the update here if needed
      // LOGI(F("Retrying update..."));
      // delay(5000);
      // fwUpdate.performUpdate();
    }
  } else {
    // Check why update check failed
    GitFirmwareUpdate::UpdateError error = fwUpdate.getLastError();
    
    if (error == GitFirmwareUpdate::NO_UPDATE_AVAILABLE) {
      LOGI(F("✓ Device is up to date."));
      LOGI_F("  Current version: %s", FW_CURRENT_VERSION);
      LOGI_F("  Remote version: %s", fwUpdate.getRemoteVersion());
    } else {
      LOGE(F("✗ Failed to check for updates."));
      LOGE_F("  Error: %s", fwUpdate.getLastErrorString());
      LOGE_F("  Error code: %d", error);
    }
  }
}

void loop() {
  // Nothing to do here - update happens in setup()
  // You can implement periodic update checks here if needed
  
  delay(10000);
  
  // Example: Check for updates every hour
  // static unsigned long lastCheck = 0;
  // if (millis() - lastCheck > 3600000) {  // 1 hour
  //   lastCheck = millis();
  //   if (fwUpdate.checkForUpdate()) {
  //     fwUpdate.performUpdate();
  //   }
  // }
}

