# GNSS Live Tracker · installation and first setup

For **Heltec Wireless Tracker V2 with ESP32-S3 and 8 MiB flash**. Check that the board says V2. Other ESP32 boards and Tracker V1.x are not supported by this image.

This is the **0.30.3 release candidate**. Clean USB installation and full recording-data restore still need physical acceptance testing. See [validation status](RELEASE-NOTES.md).

## Choose the right file

| Your situation | File and method | Existing data |
| --- | --- | --- |
| First installation or recovery from another firmware | `gnss-live-tracker-v0.30.3.factory.bin`, USB through ESPHome Web | All device settings and recordings are replaced |
| GNSS Live Tracker is already running | `gnss-live-tracker-v0.30.3.ota.bin`, tracker Firmware tab | Device settings and recordings are retained |

Do not upload the factory image to the OTA form. If the device already contains important data, download a backup before any installation.

## 1. First installation over USB

You need the factory BIN from the release package, a USB **data** cable, and a computer with a Web Serial capable browser. Desktop Chrome or Edge are straightforward choices. This USB installation is separate from subsequent setup on a phone.

1. Connect the V2 to the computer. Close serial monitors or other applications using its serial port.
2. Open **https://web.esphome.io/**. Choose **Connect**, then the USB serial device belonging to the tracker. Approve access to that port.
3. Choose **Install**, select our downloaded **`.factory.bin`**, and start installation. Use the file-install action, not **Prepare for first use**, which installs an ESPHome preparation firmware.
4. Keep power connected until the installer finishes. The complete image covers 8 MiB, so transfer can take a while. Wait for the tracker to restart; press **RST** once if necessary.
5. The display should show GNSS Live Tracker information. Continue with the setup hotspot below.

If no port appears or connection fails: verify the data cable, try another USB port, close programs holding the port, and use programming mode: hold **USER/BOOT**, briefly press **RST**, then release USER/BOOT. Select the serial port again; its name can change. Flashing in programming mode does not itself install or erase anything.

ESPHome Web is a browser flasher, not a requirement to use ESPHome firmware or Home Assistant. We provide a complete factory image for it. The exact installer flow remains part of the pending hardware test.

## 2. Join the setup hotspot

On a device without saved Wi-Fi profiles, **`Tracker-V2-Setup`** starts immediately and is initially open, with no password.

1. Join that Wi-Fi network from your computer or phone.
2. If the phone says there is no Internet, choose to stay connected. Temporarily disable automatic switching to another Wi-Fi network if necessary.
3. Open **`http://192.168.4.1`** explicitly in the browser. There is no automatic captive-portal popup and no HTTPS certificate to accept.
4. The interface starts in English. Select **Deutsch** at the top if preferred; the web and physical-display languages are separate settings.

Anyone in range can access an open hotspot while it is active. Its name and optional password can be changed in **Wi-Fi**. The warning remains visible when it is configured open, even while currently off.

## 3. Connect to your home Wi-Fi

1. Open the **Wi-Fi** tab. Available networks can be scanned and selected; a hidden SSID can be typed manually.
2. Enter your **2.4 GHz** Wi-Fi name and password. Save and connect using the controls in the form. The tracker does not connect to 5-GHz-only networks or enterprise sign-in portals.
3. Reconnect your computer/phone to the home network if the setup hotspot disappears.
4. Find the tracker IP on its **Network display page** or in the router's connected-device list. Open `http://` followed by that IP. A reserved DHCP address in the router can keep the address stable.

Up to eight Wi-Fi profiles can be saved, inspected, edited or removed. Saving a profile does not have to connect immediately. Profiles use the SSID and password, not a pinned access point, so multiple APs with the same SSID can be used. If the current network disappears, the tracker tries other saved networks. After 60 seconds without a Wi-Fi IP it opens the configured hotspot and continues trying saved networks.

Hold **USER for 3 seconds** to open the hotspot manually, without deleting Wi-Fi profiles. Its address is `http://192.168.4.1`; use the hotspot name/password you configured, which may differ from the initial defaults.

## 4. Get the first GNSS fix

Place the tracker where its GNSS antenna has a good view of the sky. A first fix can take several minutes; indoor reception varies and is not guaranteed.

The **Live** tab shows fix status, coordinates, satellites reported by the receiver and satellites listed as used in the fix. Reported satellites alone do not prove a valid fix. A positive C/N₀ value confirms a currently tracked signal. The **Sky** tab shows direction, elevation and signal strength.

GNSS, display, tracker recordings and local web views work **without TTN, a gateway or MQTT**. Map tiles require Internet access in the viewing browser when the map is loaded. The 3D Earth tab calculates positions from orbit data downloaded or imported in the browser; those predictions are not additional receiver measurements.

## 5. Optional: send positions to TTN

You need a TTN account and a reachable gateway connected to the relevant network. Merely registering a device does not create radio coverage.

For the field-tested EU868 setup:

1. In the TTN Console create or open an **Application**, then **End devices → Register end device**, and select manual registration.
2. Choose **Europe 863–870 MHz**, the recommended RX2 SF9 plan, **LoRaWAN 1.0.2**, **Regional Parameters 1.0.2 revision B**, **OTAA**, **Class A**.
3. If no JoinEUI is assigned for your programmable device, use **`0000000000000000`** (16 zeros) for this TTN setup. An assigned JoinEUI should be used instead. Use **Generate** in TTN for a DevEUI if you do not already have one, and generate an **AppKey**. These are your device's credentials, not shared Heltec values.
4. In the tracker's **LoRaWAN** tab select the matching region and copy **DevEUI**, **JoinEUI/AppEUI** and **AppKey** into their matching fields. Save. Changed credentials cause a restart followed by a new join attempt. The AppKey can be shown with the reveal button.
5. In TTN choose **Payload formatters → Uplink → Custom JavaScript formatter**. Paste the complete content of **`ttn-decoder.js`** from this release, or download it from the tracker, then save. **The same decoder handles Basic, Standard and System details** and the compact regional variants. Missing fields depend on the packet profile.
6. Keep TTN **Live data** open. Look for a join and then an uplink with decoded position/status. Positions are sent every **5 minutes** by default; a startup status and first-fix report may be additional messages. Without a fix a status message is still sent, but no valid current position should be inferred.

Defaults: **EU868, SF7, ADR off, Standard packet, 5-minute position interval**. SF is a modulation setting, not transmit power. If ADR is enabled, the network and the tracker's loss-of-downlink recovery can change rate/power; review the resulting airtime estimate. TTN Sandbox has a daily airtime policy separate from radio duty-cycle limits; the UI explains and estimates both rather than guaranteeing a daily allowance.

All other selectable regions are **experimental**, without practical join/uplink/downlink tests. Use the region-specific instructions shown in the LoRaWAN tab and [regional notes](REGIONS.md); do not apply the EU868 registration values to every region.

## 6. Recording, display and optional MQTT

**Track** controls position recording; **Live** controls satellite-count history. Each offers **Do not record**, **In this browser only**, or **On the tracker**. Browser recording needs an open, active page; tracker recording continues without a browser. Oldest tracker records are overwritten when their ring fills. Positions need a valid fix; satellite history can continue without one.

The selected chart range loads first; older data are added in the background. A validated per-device browser cache speeds later visits. Browser storage can be removed by private mode, cleanup or eviction; it is not a replacement for an exported backup.

USER controls: when the display is off, a short press wakes it; when on, a single press advances through enabled pages. A double press returns to the configured home/start page. Holding for 3 seconds opens the hotspot. **RST** restarts the device. Configure page order, clock, rotation, brightness and timeout in **Display**.

For MQTT, enter a broker address and port in **MQTT**. Credentials are optional if your broker permits anonymous connections. Home Assistant discovery is optional. MQTT uses plain TCP, not TLS; it does not require or use TTN. Network outages do not stop tracker recording.

## 7. Updates and backups

Before a major change open **Backup**, include the desired recordings and download a `.gltb` file. Store it privately: it contains saved credentials and potentially precise locations, unencrypted. Browser-only histories, browser preferences and firmware are not included.

For later firmware updates open **Firmware**, select the release's **`.ota.bin`**, then install. Keep power connected through upload, restart and confirmation. Settings and tracker recordings remain. Reload the web view after confirmation. A file selected by URL is downloaded by the browser and needs a source allowing browser downloads; manual file selection is the simplest path.

To restore, install the same or a compatible newer GNSS Live Tracker version first, open **Backup**, select the `.gltb`, review the groups/data, and use **Check selection without writing**. Only proceed with **Restore selection** when you want selected settings/data to be replaced. Recording areas are replaced, not merged. The tracker restarts; changed Wi-Fi credentials apply afterward. On interruption, use the same backup and resume as instructed. LoRaWAN sessions are not restored; the device joins again where necessary. Do not run two devices with the same restored LoRaWAN identity simultaneously.

Actual complete recording-area restore is still pending physical validation in this candidate. Check [release notes](RELEASE-NOTES.md) before depending on it.

## Troubleshooting and recovery

| Symptom | Check |
| --- | --- |
| USB device missing | Data cable, port, another serial application; USER/BOOT + RST programming mode |
| Setup page does not open | Stay on tracker Wi-Fi; enter `http://192.168.4.1`; no captive popup is expected |
| Home-network page unreachable | Phone/computer on same reachable network; current tracker IP on display/router; USER 3 seconds for hotspot |
| No position | Fix status, antenna view, startup time; a satellite count alone is not a fix |
| No TTN data | Gateway coverage/connection, matching region/FSB, credentials and Live data; local “sent” is not a receipt confirmation |
| Chart initially incomplete | Read loading notice; selected range first, older records in background |
| Wrong file rejected in OTA | Use `.ota.bin` for V2, not `.factory.bin` or firmware for another board |
| Firmware no longer starts | Enter USB programming mode and reinstall the correct factory image, then restore a private backup |

The ESP32 ROM USB bootloader normally remains usable if application firmware fails. It is not a guarantee against hardware, power or USB faults. A factory recovery replaces all data, so export a backup while the device is still working. Command-line recovery and build details are in [BUILD.md](BUILD.md). The current V2 configuration does not burn Secure Boot/encryption eFuses.

Official references: [ESPHome Web flashing](https://esphome.io/guides/faq/), [TTN manual OTAA registration](https://www.thethingsindustries.com/docs/hardware/devices/adding-devices/manual/otaa/), [security model](SECURITY.md).
