# GNSS Live Tracker 0.30.5 · release candidate

Version 0.30.5 adds inline Wi-Fi profile editing, explicit reconnect confirmation and masked password editing. An empty password saves an open network. OTA installation, settings/recording retention and editing/cancel/reconnect-warning behavior have been checked on the EU868 tracker; credential changes and connection interruptions are covered by simulated UI tests.

Version 0.30.4 fixes receive-window restoration. Upgrading an older saved LoRaWAN session triggers a fresh OTAA join once, so receive timing is negotiated again. This requires network coverage; Wi-Fi configuration and recorded tracks/history are retained. Active RX timing is visible in the LoRaWAN and Diagnostics tabs.

## What this candidate changes

- Self-contained V2 source layout: decoder and web assets are inside the repository.
- V2-only root project, tests and release automation; unused upstream applications, examples, components and manuals removed.
- Matching first-install and OTA artifacts, checksums and version/target metadata.
- Beginner setup instructions in English and German; upstream provenance documented without distributing obsolete manuals.
- Project provenance and applicable component license notices retained and extended.

Wi-Fi settings and recording formats remain compatible with 0.30.1. The receive-context migration introduced in 0.30.4 affects only the LoRaWAN session. The internal OTA project identifier is retained.

## Support and validation

- Target: Heltec Wireless Tracker V2, ESP32-S3, 8 MiB flash, UC6580, SX1262, 160×80 TFT.
- Development hardware: board marked Wireless Tracker Rev. 2.0. Other physical V2 revisions have not been independently validated.
- EU868: practical join/uplink/downlink operation tested during development. Other selectable regions are **experimental**, with host simulations only.
- OTA, Wi-Fi persistence, GNSS/display, recording and browser operations have been tested on the development tracker. Version 0.30.4 has passed an EU868 OTA upgrade and fresh join, with 5/6-second receive windows and an accepted network ADR downlink. Clean USB installation and complete recording-data restore still await physical validation.
- **Pending:** clean USB first installation through ESPHome Web; complete first-run setup; restoring actual full recording areas from backup; interruption/recovery checks where safe. Backup export, preflight, simulated-flash restore and settings restore have been tested. No second-device migration has been physically tested.
- A host or clean-source build is not a radio certification, regional conformance test, or substitute for these hardware checks.

## Known limitations

- First-install `.factory.bin` replaces all settings and recordings. Existing installations should use `.ota.bin` after exporting a backup.
- Local HTTP UI has no login; hotspot password is optional. Backups are unencrypted. See SECURITY.md.
- Map tiles require Internet access in the viewing browser. Orbit data are calculated, not extra GNSS measurements. Satellite/catalogue matches can be uncertain and are marked accordingly.
- Browser-only recording requires an open, running browser page. Tracker recording is independent. Browser caches can be cleared or evicted by the browser; the tracker remains the source for its recordings.
- MQTT is plain TCP; no on-device VPN, direct device firmware URL download, or language-pack upload is included.

This preview is intended for testing and feedback. Installation and full-restore behavior remain experimental until independently validated.
