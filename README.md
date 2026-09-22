# Conserve Naija — Wokwi sim assets

ESP32 firmware and wiring diagram for the [experimental Wokwi viewer](https://wokwi.com/experimental/viewer). Compiled locally so Wokwi cloud compilers are not used.

**Firmware:** `wokwi-0.4.2` (mixed-waste sort, Conserve OTP idle screen, 12s API timeout)
**API:** the deployed FastAPI service, or `http://127.0.0.1:8080` for local development
**Device:** Yaba / `CN-MACHINE-001` / `cn-dev-yaba-device-key`

The platform currently has one Conserve Site: **Yaba**. The firmware authenticates
with its device credential and reports only physical measurements; the backend
validates them, applies organisation pricing, and calculates Conserve Points.

## Files

| File             | Purpose                                          |
| ---------------- | ------------------------------------------------ |
| `sketch.ino`     | Firmware source (canonical copy)                 |
| `src/main.ino`   | Symlink to `sketch.ino`, the PlatformIO entry    |
| `platformio.ini` | Board, framework and library dependencies        |
| `diagram.json`   | Wokwi wiring: ESP32, 20x4 I2C LCD, keypad, scale |
| `wokwi.toml`     | Viewer wiring for the local build outputs        |
| `firmware.bin`   | Prebuilt image loaded by the public viewer       |

## Device protocol

The firmware calls these endpoints with `Authorization: Device <credential>`:

```
POST /iot/devices/me/heartbeat
POST /iot/devices/me/telemetry
POST /iot/devices/me/sessions/claim              { "code": "482731" }
POST /iot/devices/me/sessions/{id}/progress      { "stage": "sorting" }
POST /iot/devices/me/sessions/{id}/measurement   { "fractions": [...] }
```

The `/iot/devices/me/...` paths are compatibility aliases for the canonical
`/api/v1/device/...` routes, kept so this prebuilt firmware keeps working.

## Rebuilding

The API host is compiled into the image, so rebuild after the backend moves:

```sh
pio run
cp .pio/build/esp32dev/firmware.bin firmware.bin
```

Point the firmware at a different backend without editing the source:

```sh
PLATFORMIO_BUILD_FLAGS='-DCN_API_HOST="https://your-backend.example.com"' pio run
```

For a backend running on your own machine, Wokwi reaches it through
`host.wokwi.internal`:

```sh
PLATFORMIO_BUILD_FLAGS='-DCN_API_HOST="http://host.wokwi.internal:8080"' pio run
```

Commit the rebuilt `firmware.bin` so the viewer picks it up.

## Pitfalls

**Edit `sketch.ino`, never `src/main.ino`.** PlatformIO compiles `src/main.ino`,
which is a symlink to `sketch.ino`. If that symlink is ever replaced by a real
file the two silently diverge, `pio run` succeeds, and the firmware ships with
none of your changes. After building, confirm the version actually landed:

```sh
strings .pio/build/esp32dev/firmware.elf | grep -o 'wokwi-0\.[0-9]\.[0-9]'
```

**The device protocol is camelCase.** The machine sends and reads `weightKg`,
`fillPercent`, `firmwareVersion`, `conservePoints`, and `depositId`. The backend
accepts either spelling on the way in and replies in camelCase, so an image
already running in the field keeps working. Changing a field name here without
matching the backend silently breaks deposits with a `422`.

## Viewer

https://wokwi.com/experimental/viewer?diagram=https%3A%2F%2Fraw.githubusercontent.com%2Fiflames1%2Fconserve-naija-wokwi%2Fmain%2Fdiagram.json&firmware=https%3A%2F%2Fraw.githubusercontent.com%2Fiflames1%2Fconserve-naija-wokwi%2Fmain%2Ffirmware.bin
