/**
 * @file GitFirmwareUpdate.h
 * @brief GitHub-based OTA firmware update library for ESP32
 * 
 * This library provides a simple interface for checking GitHub repositories
 * for firmware updates, downloading firmware binaries via HTTP/HTTPS, and
 * flashing them to ESP32 devices. It assumes WiFi is already connected and
 * handles only the firmware update logic.
 * 
 * Default: HTTP-only (smaller binary ~88KB). Define GIT_FIRMWARE_USE_HTTPS
 * (e.g. in build_opt.h: -DGIT_FIRMWARE_USE_HTTPS) to enable HTTPS.
 *
 * Default: DebugLog disabled (smaller binary). Define DEBUG_LOG_ENABLED=1
 * (e.g. in build_opt.h: -DDEBUG_LOG_ENABLED=1) to enable logging.
 */

#pragma once

// Default: HTTP-only (smaller binary). Define GIT_FIRMWARE_USE_HTTPS to enable HTTPS.
#ifndef GIT_FIRMWARE_USE_HTTPS
  #define GIT_FIRMWARE_HTTP_ONLY 1
#else
  #define GIT_FIRMWARE_HTTP_ONLY 0
#endif

// Default: DebugLog disabled (smaller binary). Define DEBUG_LOG_ENABLED=1 to enable.
#ifndef DEBUG_LOG_ENABLED
  #define DEBUG_LOG_ENABLED 0
#endif

#include <Arduino.h>
#include <WiFi.h>
#if !GIT_FIRMWARE_HTTP_ONLY
  #include <WiFiClientSecure.h>
#endif
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <DebugLog.h>

/**
 * @class GitFirmwareUpdate
 * @brief Handles GitHub-based OTA firmware updates for ESP32
 */
class GitFirmwareUpdate {
public:
  /**
   * @enum UpdateError
   * @brief Error codes for firmware update operations
   */
  enum UpdateError {
    NO_ERROR = 0,              ///< No error occurred
    NO_UPDATE_AVAILABLE,       ///< No newer version available
    NETWORK_ERROR,             ///< Network connection failed
    HTTP_ERROR,                ///< HTTP request failed
    JSON_PARSE_ERROR,          ///< Failed to parse JSON response
    INVALID_VERSION,           ///< Invalid version string format
    DOWNLOAD_FAILED,           ///< Firmware download failed
    FLASH_FAILED,              ///< Flash write operation failed
    INVALID_URL,               ///< Invalid firmware URL
    UPDATE_SIZE_ERROR,         ///< Firmware size validation failed
    UPDATE_ABORTED             ///< Update was aborted by user
  };

  /**
   * @typedef ProgressCallback
   * @brief Callback function type for progress reporting
   * @param percent Progress percentage (0-100)
   * @param bytesRead Number of bytes read so far
   * @param totalBytes Total bytes to read (0 if unknown)
   */
  typedef void (*ProgressCallback)(int percent, size_t bytesRead, size_t totalBytes);

  /**
   * @typedef ServerHandleCallback
   * @brief Callback function type for server handling (e.g., WebServer.handleClient())
   * Called periodically during download to keep server responsive
   */
  typedef void (*ServerHandleCallback)();

  /**
   * @brief Construct a new GitFirmwareUpdate instance
   * 
   * @param currentVersion Current firmware version string (e.g., "1.0.2")
   * @param githubUrl URL to latest.json file on GitHub (raw content)
   */
  GitFirmwareUpdate(const char* currentVersion, const char* githubUrl);

  /**
   * @brief Check GitHub for firmware update availability
   * 
   * Fetches latest.json from GitHub, compares versions, and stores
   * remote version info if available. Does not perform the update.
   * 
   * @return true if a newer version is available
   * @return false if no update available or check failed
   */
  bool checkForUpdate();

  /**
   * @brief Perform the firmware update
   * 
   * Checks for update, and if available, downloads and flashes the firmware.
   * Blocks until update completes, fails, or device restarts.
   * 
   * @return true if update was successful (device will restart)
   * @return false if update failed or no update available
   */
  bool performUpdate();

  /**
   * @brief Download and install firmware from a specific URL
   * 
   * Directly downloads and installs firmware from the provided URL,
   * bypassing the GitHub check. Useful for manual updates or testing.
   * 
   * @param url URL to firmware binary
   * @return true if update was successful (device will restart)
   * @return false if update failed
   */
  bool downloadAndInstall(const String& url);

  /**
   * @brief Set progress callback function
   * 
   * Register a callback function that will be called during firmware
   * download to report progress. Set to nullptr to disable callbacks.
   * 
   * @param callback Function pointer to progress callback
   */
  void setProgressCallback(ProgressCallback callback);

  /**
   * @brief Set server handle callback function
   * 
   * Register a callback function that will be called periodically during
   * firmware download to keep the WebServer responsive (e.g., handleClient()).
   * This allows progress polling to work during long downloads.
   * Set to nullptr to disable.
   * 
   * @param callback Function pointer to server handle callback
   */
  void setServerHandleCallback(ServerHandleCallback callback);

  /**
   * @brief Set HTTP timeout in milliseconds
   * 
   * @param timeoutMs Timeout in milliseconds (default: 30000)
   */
  void setTimeout(uint32_t timeoutMs);

  /**
   * @brief Set number of retry attempts for failed downloads
   * 
   * @param count Number of retries (default: 0, disabled)
   */
  void setRetryCount(uint8_t count);

  /**
   * @brief Enable or disable certificate validation (HTTPS only)
   * 
   * @param validate true to validate certificates, false to skip (default: false)
   * @note No effect when GIT_FIRMWARE_HTTP_ONLY is 1 (HTTP-only mode)
   */
  void setCertificateValidation(bool validate);

  /**
   * @brief Get the last error code
   * 
   * @return UpdateError Error code from last operation
   */
  UpdateError getLastError() const { return _lastError; }

  /**
   * @brief Get human-readable error message
   * 
   * @return const char* Error message string (static, no heap allocation)
   */
  const char* getLastErrorString() const;
  
  /**
   * @brief Get current download progress
   * 
   * @param bytesRead Output parameter for bytes read
   * @param totalBytes Output parameter for total bytes (0 if unknown)
   * @param percent Output parameter for percentage (0-100)
   * @return true if download is in progress, false otherwise
   */
  bool getProgress(size_t& bytesRead, size_t& totalBytes, int& percent) const;

  /**
   * @brief Get remote firmware version from last check
   * 
   * @return const char* version (e.g., "1.0.3"), empty string if not checked
   */
  const char* getRemoteVersion() const { return _remoteVersion.c_str(); }

  /**
   * @brief Get release notes from last check
   * 
   * @return const char* release notes, empty string if not available
   */
  const char* getReleaseNotes() const { return _releaseNotes.c_str(); }

  /**
   * @brief Get firmware URL from last check
   * 
   * @return const char* firmware binary URL, empty string if not checked
   */
  const char* getFirmwareUrl() const { return _firmwareUrl.c_str(); }

  /**
   * @brief Abort current update operation
   * 
   * Sets abort flag to stop current download/update operation.
   * Operation will complete current chunk and then stop.
   */
  void abortUpdate() { _abortFlag = true; }

  /**
   * @brief Check if update is in progress
   * 
   * @return true if update operation is currently running
   */
  bool isUpdating() const { return _isUpdating; }

private:
  const char* _currentVersion; ///< Current firmware version (pointer to caller's string)
  const char* _githubUrl;      ///< URL to latest.json (pointer to caller's string)
  String _remoteVersion;       ///< Remote version from last check
  String _releaseNotes;        ///< Release notes from last check
  String _firmwareUrl;         ///< Firmware binary URL from last check
  
  UpdateError _lastError;      ///< Last error code
  static const size_t _lastErrorMsgSize = 64;
  char _lastErrorDetail[64];   ///< Optional detail message (e.g. "HTTPS not supported in HTTP-only build")
  ProgressCallback _progressCallback; ///< Progress callback function
  ServerHandleCallback _serverHandleCallback; ///< Server handle callback function
  uint32_t _timeoutMs;         ///< HTTP timeout in milliseconds
  uint8_t _retryCount;         ///< Number of retry attempts
  bool _validateCert;          ///< Certificate validation flag
  bool _abortFlag;             ///< Abort flag
  bool _isUpdating;            ///< Update in progress flag
  
  // Progress tracking
  size_t _currentBytesRead;    ///< Current bytes read during download
  size_t _totalBytes;          ///< Total bytes to download (0 if unknown)
  int _currentPercent;         ///< Current download percentage (0-100)

  /**
   * @brief Compare two version strings (x.y.z format)
   * 
   * @param a First version string
   * @param b Second version string
   * @return negative if a < b, zero if a == b, positive if a > b
   */
  static int cmpVersion(const String& a, const String& b);

  /**
   * @brief Perform HTTPS download and flash of firmware
   * 
   * Downloads firmware from the provided URL and flashes it using
   * the Update library. Shows progress via callback and DebugLog.
   * 
   * @param url URL to firmware binary
   * @return true if update successful (device will restart)
   * @return false if update failed
   */
  bool performHttpFirmwareUpdate(const String& url);

  /**
   * @brief Report progress via callback and Serial
   * 
   * @param bytesRead Bytes read so far
   * @param totalBytes Total bytes (0 if unknown)
   */
  void reportProgress(size_t bytesRead, size_t totalBytes);

  /**
   * @brief Set error code and log message
   * 
   * @param error Error code
   * @param message Optional error message
   */
  void setError(UpdateError error, const char* message = nullptr);
};


