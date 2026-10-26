# Wi-Fi, hotspot and Internet checks

The ESP32 supports the home Wi-Fi connection and its own hotspot simultaneously
(AP+STA). Both use the same radio; this firmware does not route Internet traffic
between them.

## Hotspot

- **Automatic** is the startup default. With saved profiles, fallback starts after
  60 seconds without a Wi-Fi IP address. With no saved profiles it starts at once.
  It turns off after one minute of stable home Wi-Fi connectivity.
- **On** keeps the hotspot active alongside the home connection.
- **Off** suppresses automatic fallback. Saved Wi-Fi networks are still retried.

The mode applies until restart. Holding USER for three seconds always selects On.
Turning off the hotspot disconnects hotspot clients, while an existing home
connection remains active.

The password is loaded into a masked field when the Wi-Fi tab is opened. Show/hide
controls reveal it temporarily; leaving the tab masks it again. Saving an empty
password configures an open hotspot. The open-hotspot warning remains visible even
when it is off. Changing an active hotspot's SSID or password can disconnect clients.

## Internet reachability

Checks and LoRaWAN reporting are off by default. A manual check is available in the
Wi-Fi tab. Automatic checks normally run every five minutes. After a result changes,
follow-up checks run ten seconds apart: three failures confirm an outage and two
successes confirm recovery. The first successful check establishes the initial state.

The tracker resolves and requests `http://connectivitycheck.gstatic.com/generate_204`,
with `http://cp.cloudflare.com/generate_204` as a fallback. Only HTTP 204 counts as
success; redirects or login pages do not. This is a connectivity heuristic, not a
certificate or a guarantee that every Internet service works. No device identifier,
password or position is included in these requests.

HTTP uses three-second socket timeouts and small buffers, without TLS or a response
body buffer. DNS uses the SDK's bounded retry behavior. A short-lived worker task
performs network operations; the GNSS, recording and radio loop do not wait for it.
The worker releases its stack and HTTP allocations after each check.

## Optional LoRaWAN reports

The two reporting options require automatic checks. Turning off automatic checks
also turns off reporting in the form. Internet-check preferences are saved and
included in the Wi-Fi group of settings backups. Older backups remain compatible.

Normal position packets (formats 3, 4 and 5 on FPort 10) reuse flag bits 6–7:

| Bits | Meaning |
| --- | --- |
| 00 | Internet status not included |
| 01 | Not confirmed / stale |
| 10 | Unavailable |
| 11 | Reachable |

Packet lengths remain 23, 24 or 38 bytes. Compact formats 6 and 7 omit Internet
status. The shared decoder adds `internet_status` and `internet_reachable` only
when included; unknown reachability is `null`.

Optional change reports use FPort 12 and eight bytes: format (1), stable state
(0 unknown / 1 unavailable / 2 reachable), flags (bit 0: Wi-Fi IP available),
report UTC as a big-endian 32-bit value (0 if unknown), and a saturated failure
count. UTC is report time, not the start of an outage. The initial state sets a
baseline without sending a change alert. Pending reports always use the latest
stable state. Recovery before an outage report is sent cancels it. A successful
position transmission carrying the status also satisfies that pending report.

Positions have priority. Extra reports wait for radio availability and regional
limits; separate attempts are at least five minutes apart. They consume additional
airtime beyond the position-only estimate. Local send success is not proof of TTN
reception. Install the current shared TTN decoder to decode FPort 12.

The tracker does not need Internet access to send LoRaWAN. Its gateway does need a
working connection to TTN: if the same Internet outage affects both, another
reachable gateway with a working connection would be required.
