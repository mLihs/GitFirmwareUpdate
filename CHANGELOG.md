# GitFirmwareUpdate Changelog

## [1.0.4] - 2026-02-01

### Added
- Compile-time HTTP-only mode (default): smaller binary ~88KB, no TLS
- Define `GIT_FIRMWARE_USE_HTTPS` (e.g. in build_opt.h) to enable HTTPS
- DebugLog disabled by default: smaller binary ~7KB
- Define `DEBUG_LOG_ENABLED=1` (e.g. in build_opt.h) to enable logging

### Changed
- HTTP-only is now the default build (was HTTPS)
- DebugLog disabled by default when using GitFirmwareUpdate

## [1.0.3] - 2026-01-26

### Added
- HTTP/HTTPS auto-detection based on URL scheme
- HTTP support saves ~30KB heap by avoiding TLS buffers
- Automatic selection of WiFiClient (HTTP) vs WiFiClientSecure (HTTPS)

### Fixed
- Destructor order bug causing lwIP pbuf_free crash
- Clients now declared before HTTPClient to ensure correct cleanup order

### Notes
- For HTTPS with reduced TLS buffers (4KB instead of 16KB), configure via sdkconfig:
  ```
  CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=4096
  CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=4096
  ```

## [1.0.2] - 2026-01-25

### Added
- Progress tracking via `getProgress()` method
- Server handle callback for WebServer responsiveness during download
- Retry logic for `Update.begin()` memory allocation

### Fixed
- Memory cleanup on failed connections

## [1.0.1] - 2026-01-20

### Added
- Initial release with GitHub-based OTA update support
- Version comparison (semver)
- Progress callbacks
- Certificate validation option
- Abort functionality
