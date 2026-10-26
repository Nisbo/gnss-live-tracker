# Tests and validation

Run `python scripts/test-v2.py` from an ESP-IDF v5.4 environment with Node.js and a host C compiler. The suite uses simulated flash, NVS, HTTP and radio interfaces; it does not connect to physical hardware or transmit radio traffic.

Coverage includes GNSS parsing and quality/time handling, ring storage and merges, recent-first chart loading, browser caching, regional MAC/packet budgets, simulated session persistence, credentials, backup validation and simulated restore, display rendering, languages, MQTT policy, web views, decoders and release packaging.

Fixtures contain fabricated application state, standard NMEA examples and public satellite-catalogue samples. They are not exported recordings from a deployed tracker. Keys, identifiers and network settings in tests are examples for automated checks.

## Hardware coverage

EU868 join, uplink and downlink operation has been tested during development on a board marked Rev. 2.0. Other selectable regions have host-test coverage only and remain experimental. OTA, GNSS, Wi-Fi, display and recording behavior have been exercised on that development hardware.

A clean USB installation of this preview and full recording-area restore still need physical validation. Backup export, preflight, simulated restore and settings restore have been tested. Second-device migration and other V2 hardware revisions have not been independently validated.

Host-test success does not establish hardware interoperability, regional conformance or radio certification. See [release limitations](RELEASE-NOTES.md).
