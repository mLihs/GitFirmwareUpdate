# BasicUpdate Example

## HTTP vs HTTPS

**Standard:** HTTP (~88 KB kleinere Binary)

| Modus | build_opt.h | Größe |
|-------|-------------|-------|
| **HTTP** (Standard) | keine | ~88 KB kleiner |
| **HTTPS** | Inhalt: `-DGIT_FIRMWARE_USE_HTTPS` | vollständige TLS-Unterstützung |

### Auf HTTPS umstellen
`build_opt.h` im Sketch-Ordner erstellen mit Inhalt:
```
-DGIT_FIRMWARE_USE_HTTPS
```
