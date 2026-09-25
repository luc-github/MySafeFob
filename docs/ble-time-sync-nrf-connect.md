# BLE time sync: setting up nRF Connect on Android

MySafeFob can set its clock over Bluetooth Low Energy (ADR-006, `ble_time.c`).
The device acts as a GATT **client**: it scans, connects to a phone, reads the
**Current Time Service (CTS)**, sets its clock, and disconnects. No pairing, and
nothing is ever sent to the phone.

## Why the phone needs an app

The device can only read the time from a phone that *serves* CTS.

| Phone | CTS available out of the box? |
|-------|-------------------------------|
| Android | **No.** Android does not expose CTS as a GATT server by itself. An app has to provide it. |
| iPhone / iPad | In theory, but only after pairing/bonding (encrypted link), and iOS does not normally advertise CTS on its own. **Not supported and untested**: the device does not pair yet, and no iOS hardware is available for testing. |

Until a companion app exists (ADR-006, v2), **nRF Connect for Mobile** (Nordic
Semiconductor) is the simplest way to turn an Android phone into a CTS server.
It is also the reference tool for testing the feature.

## 1. Install

Install **nRF Connect for Mobile** from Google Play. When asked, allow
*Nearby devices* (Bluetooth) permissions: without them the phone will not
advertise.

## 2. Configure the GATT server

Open the app's GATT server configuration (menu, *Configure GATT server*), create a
new configuration and give it a name (for example `TestCTS`). **Save it and keep it
selected**: an unnamed or unsaved configuration was not served during our tests.

Add the service and characteristics below (the app offers the standard SIG
services in a list; otherwise enter the UUIDs by hand):

| Item | UUID | Properties | Value |
|------|------|------------|-------|
| Current Time Service | `0x1805` | | |
| Current Time | `0x2A2B` | Read | 10 bytes, see below |
| Local Time Information | `0x2A0F` | Read (optional) | 2 bytes, see below |

### Current Time (0x2A2B), 10 bytes

| Byte(s) | Field | Example |
|---------|-------|---------|
| 0-1 | Year, little-endian | `EA 07` = 2026 |
| 2 | Month (1-12) | `09` |
| 3 | Day (1-31) | `19` = 25 |
| 4 | Hours (0-23) | `0C` = 12 |
| 5 | Minutes | `1E` = 30 |
| 6 | Seconds | `00` |
| 7 | Day of week (1 = Monday ... 7 = Sunday) | `05` |
| 8 | Fractions of a second, 1/256 s | `00` |
| 9 | Adjust reason | `01` |

Example for 2026-09-25 12:30:00: `EA 07 09 19 0C 1E 00 05 00 01`.

This is the phone's **local** time. If the characteristic holds a fixed value,
edit it before every test: the device sets its clock to exactly what it reads.

### Local Time Information (0x2A0F), optional, 2 bytes

| Byte | Field | Example |
|------|-------|---------|
| 0 | Time zone, signed, units of 15 min | `00` = UTC, `08` = UTC+2, `20` = UTC+8 |
| 1 | DST offset, units of 15 min (`00`, `02`, `04`, `08`; `FF` = unknown) | `00` |

The device computes `UTC = local time - time zone - DST`. If the phone does not
provide this characteristic, the device falls back to the time zone configured in
**Settings > Time > Manual**.

## 3. Configure the Advertiser

In the *Advertiser* tab, create an advertising set with:

- **Connectable**: on (the device ignores non-connectable advertisers).
- **Legacy advertising**: on, i.e. **extended advertising / "adv extension" off**.
  The device scans for legacy advertising packets only; a phone that advertises
  with Bluetooth 5 extended advertising is invisible to it. This was the cause
  of the initial "the phone never shows up" problem.
- A **Complete Local Name** record (for example `Pixel 9 Pro XL`, or any name you
  will recognise). The device lists the name it receives.
- A **Complete List of 16-bit Service UUIDs** record containing `0x1805`.

Turn the advertiser on and keep nRF Connect in the foreground with the screen on
while scanning: Android may stop advertising when the app goes to the background.

## 4. Sync from the device

1. On the device: *Settings > Time*, BLE tab (`LV_SYMBOL_BLUETOOTH`).
2. Tap the scan button. The scan lasts about 5 seconds.
3. Devices advertising CTS are listed first, marked with `*`, then the others by signal
   strength. The percentage is a rough quality derived from the RSSI.
4. Tap your phone. The device connects, reads the time, disconnects and shows
   *Time synchronized*.
5. *Settings > About* shows the sync (date, source `BLE`, offset).

Anyone can advertise CTS with any name. Only tap a device you recognise.

## Troubleshooting

The serial log (`ble_time` tag) shows every step.

| Symptom | Likely cause |
|---------|--------------|
| Phone not in the list at all | Advertiser off, app in the background, *Legacy advertising* off, or Nearby devices permission missing. |
| Listed without `*` | The `0x1805` UUID is missing from the advertising data. |
| `(not connectable)` in the log | *Connectable* is off. |
| `No time service on this device` | The phone is connectable but does not serve service `0x1805`: the GATT server configuration is not saved/active. |
| `No Current Time characteristic` | The service exists but `0x2A2B` is missing, or is not readable without pairing (some phones, iPhone). |
| `Invalid time from device` | The 10-byte value is malformed (year, month, day or time out of range). |
| Time is off by the phone's UTC offset | `0x2A0F` is missing and the time zone in *Settings > Time > Manual* does not match the phone. |

Useful log lines, in order: `host synced` -> `connecting to ...` -> `connect event, status 0`
-> `time service found` -> `characteristic 0x2A2B` -> `read Current Time` ->
`sync result: OK` -> `sync recorded: source 2`.
