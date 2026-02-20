/**
 * @file GitFirmwareUpdate.cpp
 * @brief Implementation of GitFirmwareUpdate class
 */

#include "GitFirmwareUpdate.h"
#include <Stream.h>
#include <string.h>

GitFirmwareUpdate::GitFirmwareUpdate(const char* currentVersion, const char* githubUrl)
  : _currentVersion(currentVersion),  // Store pointer directly (no String copy)
    _githubUrl(githubUrl),            // Store pointer directly (no String copy)
    _remoteVersion(),
    _releaseNotes(),
    _firmwareUrl(),
    _lastError(NO_ERROR),
    _lastErrorDetail{0},
    _progressCallback(nullptr),
    _serverHandleCallback(nullptr),
    _timeoutMs(30000),
    _retryCount(0),
    _validateCert(false),
    _abortFlag(false),
    _isUpdating(false),
    _currentBytesRead(0),
    _totalBytes(0),
    _currentPercent(0) {
}

bool GitFirmwareUpdate::checkForUpdate() {
  _lastError = NO_ERROR;
  _lastErrorDetail[0] = '\0';
  _abortFlag = false;
  _remoteVersion = "";
  _releaseNotes = "";
  _firmwareUrl = "";

#if GIT_FIRMWARE_HTTP_ONLY
  if (strncmp(_githubUrl, "https://", 8) == 0) {
    setError(INVALID_URL, "HTTPS not supported in HTTP-only build");
    return false;
  }
#endif

  // Use appropriate client based on URL scheme (HTTP vs HTTPS)
  // HTTP saves ~30 KB heap by avoiding TLS buffers
  // IMPORTANT: Declare clients BEFORE HTTPClient to ensure correct destructor order
  // (HTTPClient must be destroyed first while client is still valid)
  bool isHttps = (strncmp(_githubUrl, "https://", 8) == 0);
#if !GIT_FIRMWARE_HTTP_ONLY
  WiFiClientSecure secureClient;
#endif
  WiFiClient plainClient;
  
  HTTPClient http;
  http.setTimeout(_timeoutMs);
  http.setReuse(false);  // Disable connection reuse for stability
  
  bool beginOk = false;
  if (isHttps) {
#if !GIT_FIRMWARE_HTTP_ONLY
    if (!_validateCert) {
      secureClient.setInsecure();  // Skip certificate validation
    }
    beginOk = http.begin(secureClient, _githubUrl);
#else
    setError(INVALID_URL, "HTTPS not supported in HTTP-only build");
    return false;
#endif
  } else {
    beginOk = http.begin(plainClient, _githubUrl);
  }
  
  if (!beginOk) {
    setError(NETWORK_ERROR, "Failed to begin HTTP connection");
    return false;
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    setError(HTTP_ERROR, "HTTP request failed");
    LOGE_F("[GitFirmwareUpdate] HTTP Error: %d", httpCode);
    
    // Always call http.end() to free resources
    // Modern ESP32 HTTPClient handles cleanup safely even after failed connections
    http.end();
    
    // For connection failures (-1, -5, etc.), the WiFi stack may be in a bad state
    // Log additional debug info
    if (httpCode < 0) {
      LOGE_F("[GitFirmwareUpdate] Connection failed (code %d). FreeHeap: %u",
             httpCode, ESP.getFreeHeap());
    }
    
    return false;
  }

  // Parse JSON directly from stream (saves heap allocation for payload string)
  // StaticJsonDocument<512> is sufficient for typical latest.json (~150-200 bytes)
  StaticJsonDocument<512> doc;
  WiFiClient* stream = http.getStreamPtr();
  auto err = deserializeJson(doc, *stream);
  http.end();
  
  LOGD(F("[GitFirmwareUpdate] latest.json parsed from stream"));
  if (err) {
    setError(JSON_PARSE_ERROR, "Failed to parse JSON");
    LOGE_F("[GitFirmwareUpdate] JSON Error: %s", err.c_str());
    return false;
  }

  // Parse JSON with graceful handling of missing keys
  _remoteVersion = doc["version"] | "";
  _firmwareUrl = doc["url"] | "";
  _releaseNotes = doc["notes"] | "";

  if (_remoteVersion.length() == 0 || _firmwareUrl.length() == 0) {
    setError(INVALID_VERSION, "Invalid latest.json: missing version or URL");
    LOGE(F("[GitFirmwareUpdate] Invalid latest.json: missing required fields"));
    return false;
  }
  
  // Optional: Warn if version doesn't match URL tag (e.g., version "1.0.2" but URL has "1.0.1")
  // This is a warning, not an error, as the URL might be correct but tag might differ
  if (_firmwareUrl.indexOf(_remoteVersion) == -1) {
    LOGW_F("[GitFirmwareUpdate] Warning: Version '%s' not found in URL '%s'", 
           _remoteVersion.c_str(), _firmwareUrl.c_str());
  }

  // Validate version format (basic check for x.y.z)
  if (_remoteVersion.indexOf('.') == -1) {
    setError(INVALID_VERSION, "Invalid version format");
    return false;
  }

  LOGI_F("[GitFirmwareUpdate] Current: %s, Remote: %s", _currentVersion, _remoteVersion.c_str());
  
  if (_releaseNotes.length() > 0) {
    LOGI_F("[GitFirmwareUpdate] Release Notes: %s", _releaseNotes.c_str());
  }

  int cmp = cmpVersion(_remoteVersion, String(_currentVersion));
  if (cmp <= 0) {
    LOGI(F("[GitFirmwareUpdate] No newer version available."));
    _lastError = NO_UPDATE_AVAILABLE;
    return false;
  }

  LOGI(F("[GitFirmwareUpdate] New version found!"));
  return true;
}

bool GitFirmwareUpdate::performUpdate() {
  if (!checkForUpdate()) {
    return false;
  }

  return performHttpFirmwareUpdate(_firmwareUrl);
}

bool GitFirmwareUpdate::downloadAndInstall(const String& url) {
  if (url.isEmpty()) {
    setError(INVALID_URL, "URL is empty");
    return false;
  }

  return performHttpFirmwareUpdate(url);
}

void GitFirmwareUpdate::setProgressCallback(ProgressCallback callback) {
  _progressCallback = callback;
}

void GitFirmwareUpdate::setServerHandleCallback(ServerHandleCallback callback) {
  _serverHandleCallback = callback;
}

void GitFirmwareUpdate::setTimeout(uint32_t timeoutMs) {
  _timeoutMs = timeoutMs;
}

void GitFirmwareUpdate::setRetryCount(uint8_t count) {
  _retryCount = count;
}

void GitFirmwareUpdate::setCertificateValidation(bool validate) {
  _validateCert = validate;
}

// Static error messages in PROGMEM to save RAM
static const char ERR_0[] PROGMEM = "No error";
static const char ERR_1[] PROGMEM = "No update available";
static const char ERR_2[] PROGMEM = "Network connection failed";
static const char ERR_3[] PROGMEM = "HTTP request failed";
static const char ERR_4[] PROGMEM = "Failed to parse JSON response";
static const char ERR_5[] PROGMEM = "Invalid version string format";
static const char ERR_6[] PROGMEM = "Firmware download failed";
static const char ERR_7[] PROGMEM = "Flash write operation failed";
static const char ERR_8[] PROGMEM = "Invalid firmware URL";
static const char ERR_9[] PROGMEM = "Firmware size validation failed";
static const char ERR_10[] PROGMEM = "Update was aborted";
static const char ERR_UNK[] PROGMEM = "Unknown error";

static const char* const ERROR_MESSAGES[] PROGMEM = {
  ERR_0, ERR_1, ERR_2, ERR_3, ERR_4, ERR_5, 
  ERR_6, ERR_7, ERR_8, ERR_9, ERR_10
};

const char* GitFirmwareUpdate::getLastErrorString() const {
  // Return stored detail message if set (e.g. "HTTPS not supported in HTTP-only build")
  if (_lastErrorDetail[0] != '\0') {
    return _lastErrorDetail;
  }
  if (_lastError >= 0 && _lastError <= UPDATE_ABORTED) {
    return (const char*)pgm_read_ptr(&ERROR_MESSAGES[_lastError]);
  }
  return ERR_UNK;
}

// Parse version string "x.y.z" without sscanf (saves ~2-5KB by avoiding scanf family)
static void parseVersion(const char* s, int v[3]) {
  v[0] = v[1] = v[2] = 0;
  if (!s || !*s) return;
  
  v[0] = atoi(s);
  const char* p = strchr(s, '.');
  if (p) {
    v[1] = atoi(p + 1);
    p = strchr(p + 1, '.');
    if (p) {
      v[2] = atoi(p + 1);
    }
  }
}

int GitFirmwareUpdate::cmpVersion(const String& a, const String& b) {
  int ma[3], mb[3];
  parseVersion(a.c_str(), ma);
  parseVersion(b.c_str(), mb);
  for (int i = 0; i < 3; i++) {
    if (ma[i] != mb[i]) return ma[i] - mb[i];
  }
  return 0;
}

bool GitFirmwareUpdate::performHttpFirmwareUpdate(const String& url) {
  if (url.isEmpty()) {
    setError(INVALID_URL, "URL is empty");
    return false;
  }

#if GIT_FIRMWARE_HTTP_ONLY
  if (url.startsWith("https://")) {
    setError(INVALID_URL, "HTTPS not supported in HTTP-only build");
    _isUpdating = false;
    return false;
  }
#endif

  _isUpdating = true;
  _abortFlag = false;
  _lastError = NO_ERROR;
  _lastErrorDetail[0] = '\0';

  LOGI_F("[GitFirmwareUpdate] Starting firmware update from: %s", url.c_str());

  uint8_t retryAttempt = 0;
  bool success = false;

  while (retryAttempt <= _retryCount && !success && !_abortFlag) {
    if (retryAttempt > 0) {
      LOGW_F("[GitFirmwareUpdate] Retry attempt %u/%u", retryAttempt, _retryCount);
      delay(1000);  // Wait before retry
    }

    // Use appropriate client based on URL scheme (HTTP vs HTTPS)
    // HTTP saves ~30 KB heap by avoiding TLS buffers
    // IMPORTANT: Declare clients BEFORE HTTPClient to ensure correct destructor order
    // (HTTPClient must be destroyed first while client is still valid)
    bool isHttps = url.startsWith("https://");
#if !GIT_FIRMWARE_HTTP_ONLY
    WiFiClientSecure secureClient;
#endif
    WiFiClient plainClient;
    
    HTTPClient http;
    http.setTimeout(_timeoutMs);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);  // Important for GitHub redirects
    http.setReuse(false);  // Disable connection reuse for stability
    
    LOGI(F("[GitFirmwareUpdate] Connecting to server..."));
    bool beginOk = false;
    if (isHttps) {
#if !GIT_FIRMWARE_HTTP_ONLY
      if (!_validateCert) {
        secureClient.setInsecure();  // Skip certificate validation
      }
      beginOk = http.begin(secureClient, url);
#else
      setError(INVALID_URL, "HTTPS not supported in HTTP-only build");
      retryAttempt++;
      continue;
#endif
    } else {
      beginOk = http.begin(plainClient, url);
    }
    
    if (!beginOk) {
      setError(NETWORK_ERROR, "Failed to begin HTTP connection");
      retryAttempt++;
      continue;
    }

    LOGI(F("[GitFirmwareUpdate] Downloading firmware..."));
    int httpCode = http.GET();
    LOGD_F("[GitFirmwareUpdate] HTTP Code: %d", httpCode);
    
    if (httpCode != HTTP_CODE_OK) {
      setError(HTTP_ERROR, "HTTP request failed");
      LOGE_F("[GitFirmwareUpdate] HTTP Error, Code=%d", httpCode);
      
      // Always call http.end() to free resources
      // Modern ESP32 HTTPClient handles cleanup safely even after failed connections
      http.end();
      
      // For connection failures, log debug info
      if (httpCode < 0) {
        LOGE_F("[GitFirmwareUpdate] Connection failed. FreeHeap: %u",
               ESP.getFreeHeap());
      }
      
      // Always abort Update if it was started
      if (Update.isRunning()) {
        Update.abort();
      }
      retryAttempt++;
      continue;
    }

    int contentLength = http.getSize();
    bool hasContentLength = contentLength > 0;

    if (hasContentLength) {
      LOGI_F("[GitFirmwareUpdate] Content-Length: %d Bytes", contentLength);
      _totalBytes = contentLength;
    } else {
      LOGI(F("[GitFirmwareUpdate] No Content-Length (chunked or unknown)"));
      _totalBytes = 0;
    }
    
    // Reset progress tracking
    _currentBytesRead = 0;
    _currentPercent = 0;

    Stream* stream = http.getStreamPtr();

    // Initialize update with retry logic for memory allocation
    // The ESP32 Update library needs a large contiguous memory block
    // Memory fragmentation can cause allocation failures, so we retry with delays
    bool updateStarted = false;
    int beginRetries = 0;
    const int MAX_BEGIN_RETRIES = 5;

    while (!updateStarted && beginRetries < MAX_BEGIN_RETRIES) {
      if (beginRetries > 0) {
        delay(200); // Wait before retry to allow memory to settle
      }
      
      updateStarted = Update.begin(hasContentLength ? contentLength : (int)UPDATE_SIZE_UNKNOWN);
      
      if (!updateStarted) {
        beginRetries++;
        LOGW_F("[GitFirmwareUpdate] Update.begin() failed (attempt %d/%d), Error=%u, FreeHeap=%u", 
               beginRetries, MAX_BEGIN_RETRIES, Update.getError(), ESP.getFreeHeap());
      }
    }

    if (!updateStarted) {
      setError(UPDATE_SIZE_ERROR, "Update.begin() failed after retries");
      LOGE_F("[GitFirmwareUpdate] Update.begin() failed after %d attempts, Error=%u, FreeHeap=%u", 
             MAX_BEGIN_RETRIES, Update.getError(), ESP.getFreeHeap());
      // Safe cleanup: http.end() is safe here since we haven't started GET yet
      http.end();
      retryAttempt++;
      continue;
    }

    // Reduced buffer size saves 1KB stack (1024 vs 2048 is sufficient for ESP32 flash writes)
    const size_t BUF_SIZE = 1024;
    uint8_t buff[BUF_SIZE];

    size_t totalRead = 0;
    int lastPercent = -1;

    LOGI(F("[GitFirmwareUpdate] Starting download & flash..."));
    reportProgress(0, hasContentLength ? contentLength : 0);

    while (http.connected() && !_abortFlag) {
      // Wait for data
      if (!stream->available()) {
        if (!http.connected()) break;
        delay(1);
        continue;
      }

      size_t avail = stream->available();
      size_t toRead = (avail > BUF_SIZE) ? BUF_SIZE : avail;

      int c = stream->readBytes(buff, toRead);
      if (c <= 0) {
        LOGE(F("[GitFirmwareUpdate] Read error from stream"));
        break;
      }

      if (Update.write(buff, c) != (size_t)c) {
        setError(FLASH_FAILED, "Update.write() failed");
        LOGE_F("[GitFirmwareUpdate] Update.write() error: %u", Update.getError());
        // Safe cleanup: abort Update before ending HTTP
        Update.abort();
        // Safe cleanup: http.end() is safe here since GET succeeded
        http.end();
        _isUpdating = false;
        _currentBytesRead = 0;
        _totalBytes = 0;
        _currentPercent = 0;
        return false;
      }

      totalRead += c;
      
      // Calculate and report progress
      int percent = 0;
      if (hasContentLength && contentLength > 0) {
        percent = (int)((totalRead * 100) / contentLength);
        percent = constrain(percent, 0, 100);
      }
      
      // Update progress tracking
      _currentBytesRead = totalRead;
      if (hasContentLength) {
        _currentPercent = percent;
      }
      
      // Report progress via callback
      reportProgress(totalRead, hasContentLength ? contentLength : 0);

      // Call server handle callback to keep WebServer responsive (for progress polling)
      if (_serverHandleCallback) {
        _serverHandleCallback();
      }

      // Cooperative yield: allow other tasks (like async_tcp) to run and reset watchdog
      // This prevents watchdog timeout during long downloads
      // Only yield every ~10KB to avoid excessive context switches
      if ((totalRead % 10240) == 0) {
        yield();  // Cooperative yield to FreeRTOS scheduler
      }

      // Check if download is complete
      if (hasContentLength && totalRead >= (size_t)contentLength) {
        break;
      }
    }
    
    // Download complete - ensure 100% progress
    _currentPercent = 100;
    _currentBytesRead = totalRead;
    
    // Report final progress
    reportProgress(totalRead, hasContentLength ? contentLength : 0);

    if (_abortFlag) {
      setError(UPDATE_ABORTED, "Update aborted by user");
      Update.abort();
      // Safe cleanup: http.end() is safe here since GET succeeded
      http.end();
      _isUpdating = false;
      _currentBytesRead = 0;
      _totalBytes = 0;
      _currentPercent = 0;
      return false;
    }

    if (hasContentLength && totalRead != (size_t)contentLength) {
      setError(DOWNLOAD_FAILED, "Incomplete download");
      LOGE_F("[GitFirmwareUpdate] Only %u of %d bytes read", (unsigned)totalRead, contentLength);
      // Safe cleanup: abort Update before ending HTTP
      Update.abort();
      // Safe cleanup: http.end() is safe here since GET succeeded
      http.end();
      _isUpdating = false;
      _currentBytesRead = 0;
      _totalBytes = 0;
      _currentPercent = 0;
      retryAttempt++;
      continue;
    }

    if (!Update.end()) {
      setError(FLASH_FAILED, "Update.end() failed");
      LOGE_F("[GitFirmwareUpdate] Update.end() error: %u", Update.getError());
      // Update.end() failed, but Update may still be in a partial state
      // Try to abort it (safe to call even if already aborted)
      Update.abort();
      // Safe cleanup: http.end() is safe here since GET succeeded
      http.end();
      _isUpdating = false;
      _currentBytesRead = 0;
      _totalBytes = 0;
      _currentPercent = 0;
      retryAttempt++;
      continue;
    }

    http.end();

    if (!Update.isFinished()) {
      setError(FLASH_FAILED, "Update not finished");
      LOGE(F("[GitFirmwareUpdate] Update not finished"));
      retryAttempt++;
      continue;
    }

    success = true;
  }

  // Keep _isUpdating = true during installation phase
  // It will be set to false only if installation fails or completes

  if (!success) {
    _isUpdating = false;
    _currentBytesRead = 0;
    _totalBytes = 0;
    _currentPercent = 0;
    return false;
  }

  LOGI(F("[GitFirmwareUpdate] Update successful – restarting..."));
  // Additional delay to ensure JavaScript has time to transition to INSTALLING state
  delay(1000);
  ESP.restart();
  return true;  // Practically never reached
}

bool GitFirmwareUpdate::getProgress(size_t& bytesRead, size_t& totalBytes, int& percent) const {
  // Return progress if updating OR if we have valid progress data (download just completed)
  if (!_isUpdating && _currentPercent == 0 && _currentBytesRead == 0) {
    return false;
  }
  bytesRead = _currentBytesRead;
  totalBytes = _totalBytes;
  percent = _currentPercent;
  // Return true if updating, or if we have 100% progress (download completed)
  return _isUpdating || (_currentPercent >= 100 && _totalBytes > 0);
}

void GitFirmwareUpdate::reportProgress(size_t bytesRead, size_t totalBytes) {
  int percent = 0;
  if (totalBytes > 0) {
    percent = (int)((bytesRead * 100) / totalBytes);
    percent = constrain(percent, 0, 100);
  }

  // Debug output
  if (totalBytes > 0) {
    LOGV_F("[GitFirmwareUpdate] Progress: %d%% (%u/%u bytes)", percent, (unsigned)bytesRead, (unsigned)totalBytes);
  } else {
    LOGV_F("[GitFirmwareUpdate] Progress: %u bytes", (unsigned)bytesRead);
  }

  // Callback
  if (_progressCallback) {
    _progressCallback(percent, bytesRead, totalBytes);
  }
}

void GitFirmwareUpdate::setError(UpdateError error, const char* message) {
  _lastError = error;
  if (message) {
    strncpy(_lastErrorDetail, message, _lastErrorMsgSize - 1);
    _lastErrorDetail[_lastErrorMsgSize - 1] = '\0';
    LOGE_F("[GitFirmwareUpdate] Error: %s", message);
  } else {
    _lastErrorDetail[0] = '\0';
  }
}
