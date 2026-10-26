# Build, package and recover · GNSS Live Tracker V2

End users do not need to compile firmware. Start with [English installation](INSTALL.en.md) or [Deutsch](INSTALL.de.md).

## Development dependencies

- ESP-IDF **v5.4**, including its Git submodules and ESP32-S3 tools. Follow the official ESP-IDF installation instructions for your OS, then activate its environment (`export.sh` on Linux/macOS or the ESP-IDF terminal on Windows).
- For host tests: Python 3, Node.js 22 (or a compatible modern Node release), and a C compiler (`cc`, GCC or Clang). The full timezone test also needs Python's IANA timezone database, supplied by the OS or the `tzdata` Python package.
- For packaging: Python with `esptool` 4.x (available in the ESP-IDF v5.4 environment).

This repository contains all project-specific sources, generated web/translation/timezone assets and its TTN decoder. It does not bundle ESP-IDF or compiler/OS tools. Normal builds do not regenerate timezone data or download orbit/map assets.

## Build

From the repository root, in an activated ESP-IDF v5.4 environment:

```sh
idf.py build
```

Linux/macOS convenience wrapper:

```sh
scripts/build-v2.sh
```

The repository root is the V2 project. `main/`, `sdkconfig.defaults` and
`partitions.csv` all belong to the current V2 application. No V1.x build target
or example application is distributed.

A fresh build creates a local ignored `sdkconfig`. Check the generated build remains ESP32-S3, DIO, 80 MHz, 8 MiB, dual 2 MiB OTA slots, rollback on, encryption/Secure Boot off. No private configuration headers are required or compiled. The internal project identifier is kept for OTA compatibility.

The repository's development container is pinned to `espressif/idf:v5.4`. Host-side USB flashing is usually simpler on macOS than passing USB into Docker.

## Run host checks

With `IDF_PATH` set by the ESP-IDF environment and Node/compiler on PATH:

```sh
python scripts/test-v2.py
```

These checks use simulated flash, NVS, HTTP and radio interfaces. They do not connect to the tracker, transmit, erase or restore hardware. Reports go to ignored `.test-output/`; temporary binaries are created in an OS temporary directory. `CC=clang` or `CC=gcc` can select the host compiler. See [test scope](TESTS.md).

## Create distribution files

```sh
python scripts/package-release.py
```

Output: `dist/gnss-live-tracker-vVERSION/` and the matching ZIP. Existing output directories are not overwritten; select another `--output` directory for an independent rebuild. The script reads the build's flash arguments, checks target/version/partition layout and creates:

- `.factory.bin`: a full 8 MiB raw image at offset 0, with blank user storage. **Replaces all device settings and recordings.**
- `.ota.bin`: application only, below the 2 MiB slot limit.
- The shared TTN decoder, setup guides, release notes, licenses, checksums and machine-readable release metadata.

It verifies the application bytes in the factory image equal the OTA application. It also verifies partition/OTA metadata and blank NVS, inactive OTA and recording ranges. Packaging does not access a serial port or network.

Export the current public source snapshot, including pending V2 additions:

```sh
python scripts/export-source.py dist/gnss-live-tracker-source.zip
```

The exporter uses Git's tracked/unignored file inventory and rejects unexpected private/generated file types. It does not include `.git`, SDK installations, local backups, build folders or private configuration headers. A normal public Git checkout also contains the necessary project files after they are committed. Source archives and firmware need their corresponding version and provenance; publishing is a separate action.

## Command-line USB recovery (destructive)

Use this only when a factory reinstall is intended. First export a private backup if the existing firmware is reachable. Connect a USB data cable; hold USER/BOOT, press RST, release USER/BOOT, then identify the serial port.

With esptool 4.x:

```sh
python -m esptool --chip esp32s3 --port YOUR_PORT write_flash \
  --flash_mode dio --flash_freq 80m --flash_size 8MB \
  0x0 PATH_TO_RELEASE.factory.bin
```

Replace `YOUR_PORT` and `PATH_TO_RELEASE.factory.bin` with the actual port/file. Windows ports look like `COM5`; macOS often uses `/dev/cu.usbmodem...`; Linux may use `/dev/ttyACM0`. Close serial monitors first. Do not run the old V1.2 erase/flash instructions.

The complete factory image replaces all 8 MiB, including settings and recordings. After installation, boot normally, connect to the setup hotspot and use the Backup tab to preflight and restore a compatible private backup. Bootloader access normally permits recovery from an application failure; hardware/power/USB faults remain possible. The current application's settings do not program encryption or Secure Boot eFuses.

## Automation and release status

GitHub build automation builds V2, runs host tests and packages candidate files. Tag-triggered release automation creates a **draft prerelease**, not a public stable release. Hardware validation status is listed in RELEASE-NOTES.md. A build does not test installation or restore behavior on a physical tracker.

## Verify a public source snapshot

`python scripts/verify-source.py` exports a source snapshot, builds it in a separate
temporary directory, runs the supported host suite and creates a release package.
Use `--idf-path`, `--idf-tools-path`, `--node` and `--work-dir` when these are not
already supplied by the environment. It never connects to a tracker.

