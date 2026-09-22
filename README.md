# Conserve Naija — Wokwi sim assets

Prebuilt ESP32 firmware for the [experimental Wokwi viewer](https://wokwi.com/experimental/viewer). Compiled locally so Wokwi cloud compilers are not used.

**Firmware:** `wokwi-0.4.1` (mixed-waste sort, Conserve OTP idle screen)  
**API:** the deployed FastAPI service, or `http://127.0.0.1:8080` for local development  
**Device:** Yaba / `CN-MACHINE-001` / `cn-dev-yaba-device-key`

The platform currently has one Conserve Site: **Yaba**. The backend exposes the
legacy `/iot/devices/me/...` compatibility paths used by this prebuilt firmware;
the backend remains responsible for validating measurements and calculating CP.

## Viewer

https://wokwi.com/experimental/viewer?diagram=https%3A%2F%2Fraw.githubusercontent.com%2Fiflames1%2Fconserve-naija-wokwi%2Fmain%2Fdiagram.json&firmware=https%3A%2F%2Fraw.githubusercontent.com%2Fiflames1%2Fconserve-naija-wokwi%2Fmain%2Ffirmware.bin
