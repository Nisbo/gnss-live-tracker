# GNSS Live Tracker changelog

## 0.30.4

- Require a fresh OTAA join for sessions without trustworthy receive-window parameters, including older version-1 context records. This prevents restored sessions from using the default 1/2-second windows when the network expects 5/6 seconds. Wi-Fi settings and recordings are retained.
- Show active RX1/RX2 delays in ADR diagnostics and detailed receive parameters in the Diagnostics tab.

## 0.30.3 · V2-only publication tree

Normal root project layout; unused upstream application, components, examples and manuals omitted. Current setup guides and release packaging target V2 only.

## 0.30.2 · release preparation

Self-contained V2 build, matching factory/OTA packaging, host-test and release automation, beginner English/German guides, retained upstream attribution and updated security/validation documentation. No persisted-format or application-feature changes.

## 0.30.1

Keep loaded charts when storage is briefly busy. Prune normal ring-buffer rollover without restarting recent-first background loading; reject regressing status snapshots.

## 0.30.0

Load selected recent statistics first, backfill older records, remember the chart period and persist a per-device browser mirror. Visible and complete CSV exports wait for their corresponding data.

## 0.29.x

Offline German/English web and display language support, with language changes preserving loaded history and current UI drafts.

Earlier V2 development introduced local tracks/history, sky and orbit views, configurable Wi-Fi/display/LoRaWAN, MQTT, credential setup, OTA and backup/restore. The upstream project and starting commit are recorded in docs/UPSTREAM.md.
