# Security and private data · GNSS Live Tracker V2

This describes the current V2 application. Project provenance and inherited licenses are documented in UPSTREAM.md.

- The web interface uses local **HTTP without an application login**. Anyone able to reach it on the home network or hotspot can read data, change configuration, reveal saved credentials and install firmware. The per-boot request token protects requests against accidental/cross-origin submissions; it is not a user password or authorization boundary between local users.
- The initial `Tracker-V2-Setup` hotspot is open. Its name and optional password can be changed. An open configured hotspot can be activated again after a network outage or with USER. The web UI warns about this even when the hotspot is currently off.
- MQTT uses TCP without TLS. Optional MQTT credentials do not encrypt traffic. Keep the tracker on a trusted network; this application does not implement a VPN.
- NVS/flash encryption and Secure Boot are not enabled. The V2 application does not burn encryption eFuses. Saved passwords and flash contents are not encrypted.
- `.gltb` backups are **unencrypted**. They can contain Wi-Fi/MQTT passwords, the LoRaWAN AppKey and precise position history. Do not publish them or include them in support issues. They exclude firmware, browser data and LoRaWAN session keys/counters.
- OTA validates the ESP32-S3 image, size and internal project identifier, then uses the inactive application slot. Boot rollback is enabled; acceptance occurs after stable main-loop operation. This is not cryptographic publisher-signature verification and cannot guarantee recovery from every failure.
- Maps contact OpenStreetMap when explicitly loaded; requests reveal the selected map area to that provider. Orbit downloads contact CelesTrak when requested. Ordinary web assets and languages are served from the tracker.
- No Wi-Fi or LoRaWAN credentials belong in public source or distributable images. Old private test images and local development reports are not release files.

Do not expose the tracker directly to the public Internet. Remote access should use an existing trusted network/VPN outside the tracker.

If reporting an issue, include firmware version, board revision, region, selected data rate/ADR and a redacted description. Check screenshots, logs and JSON for credentials and coordinates first. This repository does not yet designate a private vulnerability-reporting address; do not post secrets in a public issue.
