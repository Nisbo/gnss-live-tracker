# Project origin and third-party components

GNSS Live Tracker is derived from **HTIT Tracker v1.2 - Native ESP-IDF Firmware**, by Jean-Baptiste Robert-Jardine:

- Upstream: https://github.com/jbjardine/HTIT-Tracker-v1.2-forIDF
- Starting commit: `9a7a9210a124d1c100d1dc97bfd4efedd5c4ea8b`
- Original copyright: 2025–2026 Jean-Baptiste Robert-Jardine
- Original project license: Apache License 2.0, preserved unchanged in `LICENSE`.

The V2 project retains and adapts the LoRaMAC port, regional implementation and radio/display drivers. It adds the V2 board/FEM/display handling, GNSS parsing and views, local website, history storage, Wi-Fi management, MQTT, backup/restore and OTA application. It is not a complete independent rewrite of the inherited stack. Modified inherited source files carry a V2 modification notice. Original copyright/license headers remain in place. The original application, examples and obsolete manuals are not part of this V2 repository; consult the linked upstream commit for its history.

## Components shipped or used by the V2 build

| Component | Origin and license | Notice location |
| --- | --- | --- |
| Original firmware and component adaptations | HTIT Tracker v1.2; Apache-2.0 | `LICENSE`, `NOTICE`, inherited file headers |
| LoRaMac-node | Semtech / Alibaba; Revised BSD | `components/loramac/LICENSE.TXT` and file headers |
| AES | Brian Gladman; source-header license terms | `components/loramac/loramac/aes.c`, `NOTICE`, binary package `licenses/AES.NOTICE.txt` |
| CMAC | Lander Casado and Philippas Tsigas; source-header permissions and disclaimers | `components/loramac/loramac/cmac.c`, `NOTICE`, binary package `licenses/CMAC.NOTICE.txt` |
| satellite.js | satellite-js contributors; MIT | `main/web/satellite.LICENSE.txt`, served from the tracker |
| Earth land contours | Natural Earth 1:110m land, public domain; rounded for rendering | https://github.com/nvkelso/natural-earth-vector/blob/master/geojson/ne_110m_land.geojson |
| ESP-IDF and its linked components | Espressif and other contributors; respective upstream licenses | Installed ESP-IDF v5.4 source tree and its license notices |

Only the components required by this V2 build are included. Unused upstream
application/components and old manuals are omitted. Dependencies of the selected
ESP-IDF components remain governed by their own source licenses.

OpenStreetMap map tiles and CelesTrak orbit catalogues are optional external services requested by the browser. They are not firmware source dependencies. Map attribution remains visible in the map; provider availability is outside this project's control.

The public source archive contains the project's sources, assets, decoder, generated timezone/translation data, build helpers and tests. It excludes private headers, local backups, build outputs, credentials, machine SDK installations and private test reports. Generated public files can be used to build offline with an already installed toolchain; regeneration tools may have separate documented dependencies.
