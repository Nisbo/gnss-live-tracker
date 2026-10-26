> [!WARNING]
> **Work in progress — testers welcome.**
> This project is under active development. Expect bugs, unfinished features and potentially unreliable behavior. Please back up existing data before testing.

# GNSS Live Tracker

Firmware for the **Heltec Wireless Tracker V2 (ESP32-S3, 8 MiB flash)**, based on [HTIT Tracker v1.2 for ESP-IDF](https://github.com/jbjardine/HTIT-Tracker-v1.2-forIDF) and adapted for the V2 hardware.

**Have a Wireless Tracker V1.2? Use the [original V1.2 project](https://github.com/jbjardine/HTIT-Tracker-v1.2-forIDF). This repository targets the V2.**

Current preview: **0.30.5**. Clean USB installation and complete recording-data restore are still awaiting physical validation; see the [known limitations](docs/RELEASE-NOTES.md).

- [Install and set up · English](docs/INSTALL.en.md)
- [Installieren und einrichten · Deutsch](docs/INSTALL.de.md)
- [Build and test from source](docs/BUILD.md)
- [Known limitations and validation status](docs/RELEASE-NOTES.md)
- [Security and data handling](docs/SECURITY.md)
- [Project origin, changes and third-party notices](docs/UPSTREAM.md)

## What it does

- Real GNSS positions, fix status, satellite systems, signal levels and a sky view.
- Position tracks and satellite-count history recorded on the tracker independently of the browser; configurable recording destinations and intervals.
- Local web UI in English and German, with charts, map, 3D Earth and satellite ground tracks.
- Eight Wi-Fi profiles, automatic reconnection, Wi-Fi scanning and a configurable setup hotspot.
- LoRaWAN Class A / OTAA, selectable regional plans, data rates, ADR, packet profiles and position interval; shared TTN decoder.
- MQTT telemetry and Home Assistant discovery, with optional credentials and configurable port.
- Configurable display pages, rotation, clock, timeout and USER-button controls.
- File-based OTA updates and selective backup/restore.

GNSS, display, local recording and the web UI work without a LoRaWAN account or gateway. LoRaWAN starts only after the user configures credentials. No personal Wi-Fi or LoRaWAN credentials are compiled into the firmware.

## Hardware and support

Target: **Wireless Tracker V2**, with ESP32-S3, SX1262, UC6580 and 160×80 TFT, 8 MiB flash. This build is not for the V1.x board. ESP Web Tools identifies the ESP32 chip family, not the exact Heltec board; verify the board revision before installation.

**EU868 is field-tested. Every other selectable region is experimental and has not been tested over the air.** See [regional details](docs/REGIONS.md). The application uses the inherited LoRaWAN 1.0.2 stack; this is not a LoRaWAN certification claim.

The interface is served by the tracker. Maps require Internet access in the viewing browser when loaded; orbit data can be downloaded or imported in the browser. Ordinary GNSS/status/display operation does not need Internet access.

## Installation files

Use a matching version of the release files:

| File | Use |
| --- | --- |
| `gnss-live-tracker-vVERSION.factory.bin` | First USB installation through [ESPHome Web](https://web.esphome.io/); replaces **all** settings and recordings |
| `gnss-live-tracker-vVERSION.ota.bin` | Update an existing installation using its Firmware tab; preserves settings and recordings |
| `ttn-decoder.js` | One decoder for all supported packet profiles |
| `SHA256SUMS` / `release.json` | File checksums, version, target and validation status |

A complete factory image is 8 MiB, including blank user-storage areas. Never use it for a data-preserving update. Backups are unencrypted and contain credentials; keep them private.

## Repository layout

- `main/`: V2 application, local website, languages and shared TTN decoder.
- `components/`: the five driver/MAC components required by this V2 build.
- `tests/` and `tools/`: host checks and generators for the included public assets.
- `docs/`: current V2 setup, development, security and regional references.
- `scripts/`: build, test and release helpers.

Build from the repository root with `idf.py build` or `scripts/build-v2.sh`.
ESP-IDF v5.4, Python, Node.js and a host compiler are external development dependencies.
No old application, sample projects or obsolete manuals are included. Upstream
provenance is documented below and in `docs/UPSTREAM.md`.

## Origin and license

Based on Jean-Baptiste Robert-Jardine's [HTIT Tracker v1.2 for ESP-IDF](https://github.com/jbjardine/HTIT-Tracker-v1.2-forIDF), starting from commit `9a7a9210a124d1c100d1dc97bfd4efedd5c4ea8b`. This is a V2 derivative, not an official Heltec firmware release. The original Apache-2.0 license and copyright notices are retained. Additional component licenses apply; see [LICENSE](LICENSE), [NOTICE](NOTICE) and [UPSTREAM](docs/UPSTREAM.md).
