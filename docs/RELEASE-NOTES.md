# GNSS Live Tracker 0.30.3 · release candidate

## What this candidate changes

- Self-contained V2 source layout: decoder and web assets are inside the repository.
- V2-only root project, tests and release automation; unused upstream applications, examples, components and manuals removed.
- Matching first-install and OTA artifacts, checksums and version/target metadata.
- Beginner setup instructions in English and German; upstream provenance documented without distributing obsolete manuals.
- Project provenance and applicable component license notices retained and extended.

Firmware behavior and persisted data formats remain as in 0.30.1. The internal OTA project identifier is retained.

## Support and validation

- Target: Heltec Wireless Tracker V2, ESP32-S3, 8 MiB flash, UC6580, SX1262, 160×80 TFT.
- Development hardware: board marked Wireless Tracker Rev. 2.0. Other physical V2 revisions have not been independently validated.
- EU868: practical join/uplink/downlink operation tested during development. Other selectable regions are **experimental**, with host simulations only.
- OTA, Wi-Fi persistence, GNSS/display, recording and browser operations have been tested on the development tracker. The exact 0.30.3 candidate has not yet undergone physical acceptance testing.
- **Pending:** clean USB first installation through ESPHome Web; complete first-run setup; restoring actual full recording areas from backup; interruption/recovery checks where safe. Backup export, preflight, simulated-flash restore and settings restore have been tested. No second-device migration has been physically tested.
- A host or clean-source build is not a radio certification, regional conformance test, or substitute for these hardware checks.

## Known limitations

- First-install `.factory.bin` replaces all settings and recordings. Existing installations should use `.ota.bin` after exporting a backup.
- Local HTTP UI has no login; hotspot password is optional. Backups are unencrypted. See SECURITY.md.
- Map tiles require Internet access in the viewing browser. Orbit data are calculated, not extra GNSS measurements. Satellite/catalogue matches can be uncertain and are marked accordingly.
- Browser-only recording requires an open, running browser page. Tracker recording is independent. Browser caches can be cleared or evicted by the browser; the tracker remains the source for its recordings.
- MQTT is plain TCP; no on-device VPN, direct device firmware URL download, or language-pack upload is included.

This preview is intended for testing and feedback. Installation and full-restore behavior remain experimental until independently validated.
