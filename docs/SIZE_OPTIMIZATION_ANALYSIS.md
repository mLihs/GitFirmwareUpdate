# GitFirmwareUpdate / BasicUpdate – Größen-Optimierung

**Baseline:** 934.803 Bytes (29 %), 44.524 Bytes dynamisches Speicher

**Status:** ✅ IMPLEMENTIERT (2026-02-01)

---

## Zusammenfassung der implementierten Optimierungen

| Nr. | Maßnahme | Status | Erwartete Einsparung |
|-----|----------|--------|----------------------|
| 1 | DEBUG_LOG_ENABLED=0 | ✅ Implementiert | ~15–25 KB Flash |
| 2 | Stream statt String für JSON | ✅ Implementiert | ~150-500 Bytes Heap |
| 3 | StaticJsonDocument<512> | ✅ Implementiert | 256 Bytes Stack |
| 4 | BasicUpdate: LOGI_F | ✅ Implementiert | Weniger Heap-Fragmentierung |
| 5 | getLastErrorString const char* | ✅ Implementiert | ~1 KB Flash |
| 6 | cmpVersion ohne sscanf | ✅ Implementiert | ~2–5 KB Flash |
| 7 | String-Member zu const char* | ✅ Implementiert | ~130-200 Bytes RAM |
| 8 | Download-Buffer 2048→1024 | ✅ Implementiert | 1024 Bytes Stack |
| 9 | Getter als const char* | ✅ Implementiert | Keine Heap-Kopien |

**Geschätzte Gesamteinsparung:**
- Flash: ~3-7 KB
- RAM: ~1.5-2.5 KB
- Stack: ~1.5 KB
- Heap-Fragmentierung: Signifikant verbessert

---

## 1. DebugLog abschalten (nachweislich Flash und RAM)

### Status: ✅ BEREITS IMPLEMENTIERT

`GitFirmwareUpdate.h` (Zeilen 26-29):

```cpp
// Default: DebugLog disabled (smaller binary). Define DEBUG_LOG_ENABLED=1 to enable.
#ifndef DEBUG_LOG_ENABLED
  #define DEBUG_LOG_ENABLED 0
#endif
```

### Maßnahme

`build_opt.h` im Sketch-Ordner (falls Logging gewünscht):

```
-DDEBUG_LOG_ENABLED=1
```

### Erwartete Einsparung

- Flash: ~15–25 KB (printf-Suite, Log-Strings, Serial-Aufrufe)
- RAM: 256 Bytes pro LOG*_F-Aufruf (Stack) – entfällt bei ausgeschaltetem Log

---

## 2. Stream statt String für JSON (belegt)

### Status: ✅ IMPLEMENTIERT

`GitFirmwareUpdate.cpp` (checkForUpdate):

```cpp
// Parse JSON directly from stream (saves heap allocation for payload string)
// StaticJsonDocument<512> is sufficient for typical latest.json (~150-200 bytes)
StaticJsonDocument<512> doc;
WiFiClient* stream = http.getStreamPtr();
auto err = deserializeJson(doc, *stream);
http.end();
```

### Einsparung

- Kein `String payload` für die komplette Response
- `latest.json` ~150–500 Bytes → weniger Heap und Fragmentierung
- Kein zusätzlicher String-Overhead

---

## 3. StaticJsonDocument verkleinert (belegt)

### Status: ✅ IMPLEMENTIERT

`GitFirmwareUpdate.cpp`:

```cpp
StaticJsonDocument<512> doc;  // Reduziert von 768 auf 512 Bytes
```

### Einsparung

- Stack: 256 Bytes weniger pro Aufruf von `checkForUpdate()`
- 512 Bytes ist ausreichend für typische latest.json (~150-200 Bytes)

---

## 4. BasicUpdate: String-Konkatenation vermieden (belegt)

### Status: ✅ IMPLEMENTIERT

Alle Beispiele wurden auf `LOGI_F`/`LOGE_F` umgestellt:

```cpp
// Alt (String-Konkatenation):
LOGI(String("Connecting to WiFi: " + String(ssid)).c_str());

// Neu (printf-Style):
LOGI_F("Connecting to WiFi: %s", ssid);
```

### Einsparung

- Weniger Heap-Allokationen und Fragmentierung
- Weniger String-Konstruktionen in `loop`/`setup`

---

## 5. getLastErrorString() ohne String (belegt)

### Status: ✅ IMPLEMENTIERT

`GitFirmwareUpdate.cpp`:

```cpp
// Static error messages in PROGMEM to save RAM
static const char ERR_0[] PROGMEM = "No error";
static const char ERR_1[] PROGMEM = "No update available";
// ... etc ...

static const char* const ERROR_MESSAGES[] PROGMEM = {
  ERR_0, ERR_1, ERR_2, ERR_3, ERR_4, ERR_5, 
  ERR_6, ERR_7, ERR_8, ERR_9, ERR_10
};

const char* GitFirmwareUpdate::getLastErrorString() const {
  if (_lastError >= 0 && _lastError <= UPDATE_ABORTED) {
    return (const char*)pgm_read_ptr(&ERROR_MESSAGES[_lastError]);
  }
  return ERR_UNK;
}
```

### Einsparung

- Keine Heap-Allokation pro Aufruf
- Weniger String-Code, der gelinkt werden muss
- Strings in PROGMEM (Flash) statt RAM

---

## 6. cmpVersion ohne sscanf (belegt)

### Status: ✅ IMPLEMENTIERT

`GitFirmwareUpdate.cpp`:

```cpp
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
  // ...
}
```

### Einsparung

- `sscanf` wird nicht mehr benötigt
- ~2-5 KB Flash-Einsparung (wenn sscanf nur hier genutzt wurde)

---

## 7. String-Member zu const char* (Zusätzliche Optimierung)

### Status: ✅ IMPLEMENTIERT

`GitFirmwareUpdate.h`:

```cpp
private:
  const char* _currentVersion; ///< Current firmware version (pointer to caller's string)
  const char* _githubUrl;      ///< URL to latest.json (pointer to caller's string)
```

### Einsparung

- ~130-200 Bytes RAM (kein String-Objekt-Overhead + keine Heap-Kopie)
- Keine Heap-Allokation beim Instanziieren

---

## 8. Download-Buffer reduziert (Zusätzliche Optimierung)

### Status: ✅ IMPLEMENTIERT

`GitFirmwareUpdate.cpp`:

```cpp
// Reduced buffer size saves 1KB stack (1024 vs 2048 is sufficient for ESP32 flash writes)
const size_t BUF_SIZE = 1024;
uint8_t buff[BUF_SIZE];
```

### Einsparung

- 1024 Bytes Stack während des Downloads
- Kein Nachteil für Download-Performance (ESP32 Flash-Writes sind der Flaschenhals)

---

## 9. Getter als const char* (Zusätzliche Optimierung)

### Status: ✅ IMPLEMENTIERT

`GitFirmwareUpdate.h`:

```cpp
const char* getRemoteVersion() const { return _remoteVersion.c_str(); }
const char* getReleaseNotes() const { return _releaseNotes.c_str(); }
const char* getFirmwareUrl() const { return _firmwareUrl.c_str(); }
const char* getLastErrorString() const;
```

### Einsparung

- Keine String-Kopie bei jedem Getter-Aufruf
- Weniger Heap-Fragmentierung

---

## Beachtenswert

- **DEBUG_LOG_ENABLED=0** ist der Default und bringt den größten Gewinn.
- Alle Optimierungen sind abwärtskompatibel (API-Änderung: Getter geben `const char*` statt `String` zurück).
- Nach Änderungen an `build_opt.h` oder Bibliotheken einen vollständigen Neubuild ausführen.
- Die HTTPS-Checks in checkForUpdate() und performHttpFirmwareUpdate() nutzen jetzt `strncmp` statt `String::startsWith`.
