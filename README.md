# Conserve Naija — Wokwi sim assets

ESP32 firmware and wiring diagram for the [experimental Wokwi viewer](https://wokwi.com/experimental/viewer). Compiled locally so Wokwi cloud compilers are not used.

**Firmware:** `wokwi-0.4.1` (mixed-waste sort, Conserve OTP idle screen)  
**API:** the deployed FastAPI service, or `http://127.0.0.1:8080` for local development  
**Device:** Yaba / `CN-MACHINE-001` / `cn-dev-yaba-device-key`

The platform currently has one Conserve Site: **Yaba**. The firmware authenticates
with its device credential and reports only physical measurements; the backend
validates them, applies organisation pricing, and calculates Conserve Points.

## Files

| File              | Purpose                                            |
| ----------------- | -------------------------------------------------- |
| `sketch.ino`      | Firmware source (canonical copy)                   |
| `src/main.ino`    | Build entrypoint used by PlatformIO               |
| `platformio.ini`  | Board, framework and library dependencies          |
| `diagram.json`    | Wokwi wiring: ESP32, 20x4 I2C LCD, keypad, scale   |
| `wokwi.toml`      | Viewer wiring for the local build outputs          |
| `firmware.bin`    | Prebuilt image loaded by the public viewer         |

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

## Viewer

https://wokwi.com/experimental/viewer?diagram=https%3A%2F%2Fraw.githubusercontent.com%2Fiflames1%2Fconserve-naija-wokwi%2Fmain%2Fdiagram.json&firmware=https%3A%2F%2Fraw.githubusercontent.com%2Fiflames1%2Fconserve-naija-wokwi%2Fmain%2Ffirmware.bin
