# GNSS Live Tracker changelog

## 0.32.2

- Edit MQTT passwords in a prefilled masked field with temporary show/hide controls. Clearing the field removes the password; remove the separate removal checkbox.
- Present data, command and Home Assistant topics in individual rows, including the exact discovery and birth topics. Keep the table stable during status refreshes.
- Skip MQTT satellite snapshot work while MQTT is disabled, share stored-configuration validation and remove unused translations and form state.
- Preserve MQTT settings layout, Wi-Fi profiles, LoRaWAN parameters and recordings.

## 0.32.1

- Use singular check-age labels for one second, minute and hour in German and English.

## 0.32.0

- Keep Automatic/On/Off hotspot selections after restart. Highlight the current mode; USER can temporarily enable the hotspot without replacing the saved selection.
- Confirm Internet state changes with a second check one minute later, for both outages and recovery. Add colored status and readable check ages; simplify the German and English help text.
- Add a persistent position-sending switch, independent of GNSS and recording. Internet change reports are available only while position sending is off; position reports can carry Internet status instead.
- Include hotspot mode and position-sending preferences in settings backups. Preserve existing profiles, radio parameters and recording formats.

## 0.31.1

- Remove a stale manual-check progress message; the live Internet badge and check details show the actual result.

## 0.31.0

- Add Automatic/On/Off hotspot modes until restart, with USER activation and masked password editing. Remove the redundant new-profile reset button.
- Add optional background Internet checks and optional LoRaWAN availability reports. Three failed checks confirm an outage, two successful checks confirm recovery. Standard position packets reuse two flag bits without growing; optional eight-byte FPort 12 reports respect radio scheduling and replace outdated pending status.
- Keep Internet checks and reporting disabled by default. Extend the shared TTN decoder and settings backup for the new options.

## 0.30.5

- Edit saved Wi-Fi profiles within their cards, with masked, prefilled passwords and show/hide controls. Clearing a password saves an open network; unchanged passwords are preserved.
- Offer Reconnect for the active profile and confirm connection interruptions. Adding profiles remains a separate form.

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
