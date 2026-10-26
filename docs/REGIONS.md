# LoRaWAN regions · 0.28.0

GNSS Live Tracker offers the regional plans below in the LoRaWAN tab. Existing
EU868 and US915 settings migrate automatically. EU868 remains the initial
setting, and WLAN, OTAA credentials, tracks and satellite history are retained.

**Every selectable region other than EU868 is experimental. Only EU868 has been
used on the development device. No other region has undergone a physical join,
uplink or downlink test.** Host tests simulate the radio, not propagation,
receiver timing, measured output or gateway/network interoperability. This
implementation does not claim full conformance to a newer Regional Parameters
revision merely because that revision is used to register an experimental plan.

| Selection | Default join channels | Initial RX2 | Options |
| --- | --- | --- | --- |
| EU868 | 868.1 / 868.3 / 868.5 MHz | 869.525 MHz, DR0 | SF7–12 / 125 kHz |
| US915 · experimental | Selected FSB or full 64+8 plan | 923.3 MHz, DR8 | SF7–10 / 125 kHz; SF8 / 500 kHz |
| AU915 · experimental | Selected FSB or full 64+8 plan | 923.3 MHz, DR8 | SF7–12 / 125 kHz; SF8 / 500 kHz; uplink dwell setting |
| IN865 · experimental | 865.0625 / 865.4025 / 865.985 MHz | 866.55 MHz, DR2 | SF7–12 / 125 kHz |
| KR920 · experimental | 922.1 / 922.3 / 922.5 MHz | 921.9 MHz, DR0 | SF7–12 / 125 kHz; Listen Before Talk |
| AS923-1 · experimental | 923.2 / 923.4 MHz | 923.2 MHz, DR2 | Uplink dwell, LBT, conservative 1% duty cycle |
| AS923-2 · experimental | 921.4 / 921.6 MHz | 921.4 MHz, DR2 | Same, using group 2 absolute frequencies |
| AS923-3 · experimental | 916.6 / 916.8 MHz | 916.6 MHz, DR2 | Same, using group 3 absolute frequencies |
| AS923-4 · experimental | 917.3 / 917.5 MHz | 917.3 MHz, DR2 | Same, using group 4 absolute frequencies |
| RU864 · experimental | 868.9 / 869.1 MHz | 869.1 MHz, DR0 | SF7–12 / 125 kHz; regional duty cycle |

The application's selected rates use LoRa. No FSK selection is offered. Reported
network rates are translated using the actual region, including AU915's
DR6 = SF8 / 500 kHz and the 500-kHz downlink rates. A higher SF has a different
regional DR number in US915 than in the other plans.

This V2 build targets the 863–928 MHz hardware variant. EU433, CN470 and the
deprecated CN779 plan are not enabled in this build. Their presence in the
upstream library does not establish support by this tracker variant. They were
not part of the previously disabled high-band region choices.

## Setup and network compatibility

Select the matching region in the tracker, network device and gateway. For TTN
US915/AU915 the UI suggests FSB2: channels 8–15 plus 65. FSB1–8 and the full
64+8-channel plan remain available for other deployments. AU915 FSB2 uses
916.8–918.2 MHz and its 917.5-MHz / 500-kHz channel. AS923 group numbers are
frequency offsets, not eight-channel FSB numbers; there is no FSB selector for
AS923, IN865, KR920 or RU864.

The MAC implementation remains LoRaWAN 1.0.2. The existing EU868/US915 setup uses
Regional Parameters 1.0.2 revision B. The TTN band definitions require newer
Regional Parameters for the later AS923 groups: RP002-1.0.1 for groups 2/3 and
RP002-1.0.3 for group 4. Select the corresponding plan and revision rather than
using the old EU868 instructions for every region. These AS group backports need
real network validation; selecting a revision in TTN is not a conformance claim.
Use the shared decoder downloaded from the tracker for every packet profile.

Changing region, FSB or the configured dwell flag requires a restart and fresh
OTAA join. The request first persists a rejoin marker; the main task finishes an
active radio transaction before restarting. On boot, only the old MAC session,
frame counters, network-plan binding and regional session context are cleared.
OTAA credentials and DevNonce state remain intact. WLAN and recordings are
independent of this operation.

AS923 defaults are shifted before MAC initialization. Network CFList,
NewChannelReq, DlChannelReq and RxParamSetupReq frequencies remain absolute;
the shift is not applied to them again. Session binding distinguishes all four
AS groups, FSB choice and configured dwell flag, preventing cross-plan reuse.

## Dwell time, packet sizes and channel checks

Selecting AU915 or AS923 starts with the uplink 400-ms dwell limit enabled in
the form. The checkbox can be disabled when the selected frequency plan calls
for that. The setting controls the initial MAC dwell state. TxParamSetupReq can
subsequently change the negotiated value, which is retained with the session.
The browser's saved configuration and actual next-packet estimate are separate.

With this limit enabled, SF11 and SF12 / 125 kHz are unavailable as starting
rates. DR2 (SF10 / 125 kHz) has an 11-byte application budget before pending MAC
options. The existing compact 11/9-byte formats therefore apply to AU915 and
AS923 as well as US915 DR0. Both preserve full encoded coordinates; small
packets omit the GNSS measurement time, sequence, altitude, speed, HDOP and
system details. The 9-byte form also omits satellite totals. The shared decoder
leaves omitted fields absent. See [the compact-format details](US915.md).

The MAC size query accounts for the regional minimum and next ADR step without
mutating the live MAC. A configured fixed rate is clamped to the current regional
minimum if a network enables dwell time. A full position is never forced into an
undersized packet by silently raising the data rate. Pending MAC-only traffic is
handled before the next position if fewer than nine application bytes remain.
The small regional MAC answer queues are bounded to 11 bytes so an ADR reduction
cannot make the reply queue unsendable; full queues reject additional replies
and require the network to repeat requests.

KR920 and all AS923 variants perform a channel RSSI check before transmission.
A failed RSSI read is treated as busy, not free. The AS923 presets additionally
enforce a conservative 1% duty cycle while retaining LBT, including group 1. This
can be stricter than some particular network plans. No country-specific AS923
certification or Japan-specific extended channel plan is implied.

Requested output remains capped at 14 dBm before the V2 FEM gain compensation.
Lower regional/network targets still apply. KR920 also has channel-dependent
output limits. Displayed values are targets, not measured RF output.

## Session persistence and migrations

The new 12-byte radio configuration is stored as config_v5. Versions 1–4 remain
readable in both boot migration and backups; an existing v4 record retains its
EU868/US915, rate, ADR, interval, packet profile and FSB. The additional flags are
initialized without changing the original network choice.

A separate CRC-protected regional session context stores dynamic channels and
alternative RX1 frequencies, dwell flags, RX1 offset, receive delays, repetitions,
MaxEIRP and antenna-gain parameters. It is committed alongside the existing
session blob and network-plan binding. Legacy EU868/US915 sessions without this
context remain readable. New regions require valid matching context to restore;
a missing, corrupted or mismatched record triggers a new join instead. Session
keys/counters/context remain excluded from user backups.

## Tests and field-test checklist

From the repository root:

```sh
python3 tests/run_us915.py
```

The expanded host suite includes the production regional modules, all ten
selectable plans, 81 valid rate/dwell combinations, initial channels and RX2,
RX1, join-rate selection, all AU FSBs, ADR minimum/channel recovery, AS frequency
bounds and LBT free/busy decisions. The production size-query/validation routines
are exercised with US915, AU915 and AS923 compact budgets. Production session
capture/restore routines run against simulated NVS and the real regional channel
handlers, including CRC/plan rejection and legacy EU868/US915 compatibility.
HTTP/NVS tests cover configuration migration, invalid combinations, deferred
restarts, nonce retention and all new region selections. Browser/decoder and
backup regression checks are also included.

Outstanding for every experimental plan: real join request/accept, uplink and
downlink decoding, RX1/RX2 timing, ADR changes, FSB/channel-mask interoperability,
TxParamSetupReq changes, real LBT behavior and restart/session recovery. No test
run by the host suite transmits radio traffic.

References:

- [TTN frequency-plan definitions](https://www.thethingsindustries.com/docs/concepts/features/lorawan/frequency-plans/)
- [TTN regional-parameter overview](https://www.thethingsnetwork.org/docs/lorawan/regional-parameters/other/)
- [TTN band/revision definitions](https://pkg.go.dev/go.thethings.network/lorawan-stack/v3/pkg/band)
- [Semtech AS923 reference](https://github.com/Lora-net/LoRaMac-node/blob/master/src/mac/region/RegionAS923.c)
- [Semtech AU915 reference](https://github.com/Lora-net/LoRaMac-node/blob/master/src/mac/region/RegionAU915.c)
