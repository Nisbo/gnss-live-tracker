# Localization

The built-in UI languages are German (`de`) and English (`en`). English is the default for a new browser and for a newly configured display. A manual choice
is stored under `tracker-language` in this browser, for this tracker address.
Switching language repaints the existing view without reloading the page, clearing
track/history caches or restarting pending data reads. The selector is disabled during a firmware
upload or an active backup/restore. Local tracks, chart histories and other browser
preferences use their existing storage keys. Device configuration and measurement
formats are independent of the UI language.

Both complete catalogs ship with the firmware. No internet connection or external
translation service is needed. A browser reload or tracker restart cannot remove
the built-in texts. Display language is separate, configured in the Display tab
and saved in the existing NVS display record. The record remains version 5 and
76 bytes; a previously reserved zero byte is the language field (`0=de`, `1=en`).
Version-5 records and backup files retain an explicitly stored language. Older
records without a language field migrate to English while retaining other display
choices.
No partition layout or track/history record format changes are required.

## Source files

- `main/web/i18n.js`: registration, language selection, English fallback, named
  parameters, plural rules, safe text/attribute rendering and the pack interface.
- `main/web/locales/ui.json`: editable static HTML text catalogs. Regenerate with
  `python3 tools/generate_i18n_ui.py` after changes.
- `main/web/i18n-inline.js`: live status and WLAN actions in the page's inline JS.
- `main/web/i18n-dashboard.js`: dashboard, track and statistical recording UI.
- `main/web/i18n-sky.js`: sky, diagnostics, orbital views and satellite labels.
- `main/web/i18n-settings.js`: firmware, display, credentials, backup, ADR and
  timezone UI. Keep `tools/timezones.template.js` aligned with `timezones.js`.
- `main/web/i18n-server.js`: exact backend error-message mappings. Wire errors
  and protocol schemas remain unchanged.
- `main/display_language.h`: compact ASCII labels for the physical display.

Stable semantic keys belong to namespaces. Prefer complete sentence templates
with named parameters over concatenated fragments. A plural entry has an `other`
form and may include `one`, `zero`, `two`, `few` and `many`; `count` selects the
form using the chosen locale. Dates and human-facing number formats use the
selected locale. Measurement timestamps, API identifiers, satellite catalog
identities, CSV schemas and credentials are not translated.

For HTML, use `data-i18n` on text-only elements and `data-i18n-title`,
`data-i18n-aria-label` or `data-i18n-placeholder` for attributes. Do not put
`data-i18n` on a container with controls or structured child content: applying
its translation replaces its text content. Text is never interpreted as HTML.
User-entered text and values are not modified by translation.

## Firmware memory and delivery

CMake runs `tools/generate_i18n_bundle.py` to create a deterministic gzip bundle
from the runtime and built-in catalogs. `/i18n.js` serves those compressed bytes
directly from flash, with `Content-Encoding: gzip`. Decompression, dictionaries
and string interpolation run in the browser. The ESP32 does not allocate a copy
of the catalog or run a compressor. The physical display uses constant labels
and the existing frame buffer. OTA slot sizes and persistent data areas stay as
before.

## Future additional languages

`TrackerI18n.registerPack()` is a validated runtime extension point. Its schema:

```json
{
  "format_version": 1,
  "language": "fr",
  "label": "Français",
  "locale": "fr-FR",
  "messages": {"core.language": "Langue"}
}
```

A complete pack is validated before registration. Missing keys fall back to
English, then German. A saved language choice can be resolved when its pack is
registered at startup. The current firmware has no upload endpoint, extra-pack
flash writer, or browser-only pack import. A later device storage layer must
serve persisted packs after boot and offline reload, register them before UI
initialization, and preserve existing track/history areas. Storage allocation,
backup behavior and the upload interface are intentionally left for that work.
This web pack extension does not change the compact physical display catalog.

## Validation

`tests/test_i18n.cjs` checks selection, fallback, named parameters, plurals,
validation, safe output and blocked browser storage. `test_i18n_settings.cjs`
checks settings outputs in both languages and coverage of actual C error strings.
`test_i18n_bundle.py` checks deterministic gzip, byte-exact decompression and
source/generated catalog agreement. Existing module tests exercise both locales;
the dashboard test verifies that a language switch leaves CSV bytes intact and
retains 14,000 cached samples without additional network calls. Integration tests
also check active tab, draft forms and live/offline state across language changes.
Display policy tests check record compatibility and language bounds. Browser QA
covers all tabs, reload persistence, German/English switching and mobile layout.
