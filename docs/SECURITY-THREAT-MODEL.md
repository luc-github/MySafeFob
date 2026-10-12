# Security threat model — MySafeFob v1 (as built)

**Date**: 2026-10-12 (ROADMAP 6.3). Describes the firmware as built and
hardware-validated at the end of Phase 8, not an intention. Formats:
`INTERFACES.md` §1-§3; decisions: ADR-002/004/005/019 in `ROADMAP.md`;
release hardening plan: `SECURE-BOOT-PLAN.md`.

---

## 1. Assets

| Asset | Where it lives |
|---|---|
| Account secrets: TOTP keys, passwords, account PINs, recovery codes, notes, logins | `secrets` flash partition, encrypted (§3.1); decrypted in PSRAM only while unlocked; on screen only on the account / edit pages |
| Device PIN (6 digits) | Never stored; only the key derived from it, in RAM while unlocked |
| Backup passphrase (8-64 characters) | Never stored; typed for each export / import |
| Backups | `.msfx` files on the SD card (passphrase); factory rescue copies `RESCUEnn.MSR` (PIN only); plaintext JSON on the PC while editing |
| Integrity of the firmware | `app0` / `factory` partitions, unsigned today |
| Owner contact info | NVS, deliberately public: shown on the sleep screen when enabled |

## 2. Attackers considered

| # | Attacker | Capabilities |
|---|---|---|
| A1 | Finder / thief of a **locked or sleeping** device | Buttons, touch, USB cable, SD slot; unlimited time |
| A2 | Same, with a **PC** and the device | USB serial console, `esptool` flash read/write, offline computing |
| A3 | Someone who gets a **copy of the SD card** only | Files on the card, offline computing |
| A4 | **Shoulder surfer** / someone briefly holding an **unlocked** device | Sees the screen, can tap for a short while |
| A5 | **Evil maid**: has the device for a while, gives it back | A2 + can flash firmware, then wait for the owner to type the PIN |
| A6 | **Radio** attacker near the device | Wi-Fi / BLE, only while a time sync runs |

Out of scope for v1: invasive hardware attacks (decapping, fault
injection, side channels on the ESP32-S3), a malicious PC used by the
owner to run the tools, coercion.

## 3. Protections in place

### 3.1 Data at rest (flash)

- **AES-256-GCM** over the whole serialized store (header authenticated
  as additional data): confidentiality + integrity; any bit flipped =
  refused, never half-read.
- **Key = Argon2id(PIN, 16-byte random salt, m = 2 MiB, t = 1)** (ADR-004,
  ~0.6-1 s on the S3): every PIN guess costs a full derivation.
- **Two alternating slots**, header written last: a power cut during a
  save leaves the previous version usable.
- Nothing secret in NVS (settings, owner info, time history only).

### 3.2 PIN guessing on the device (A1)

- **Failure counter persisted before the check** (cutting power during a
  try gains nothing), in the plaintext control sector of `secrets`.
- **Back-off** from the 3rd consecutive failure: 10 s, 30 s, 2 min, 10 min,
  then 1 h per try; counted from boot after a restart. 10^6 PINs at 1 h each
  after the first few = out of reach on the device.
- The same counter and back-off apply to every path that checks the PIN:
  unlock screen, change PIN, Backup menu confirmation, console `backup`
  export / import, factory rescue export.
- **Shuffled keypad** at every attempt (smudge marks reveal nothing),
  optional symbol keypad (cosmetic).
- No auto-erase after N failures (decision: a forgotten-PIN wipe would
  also let anyone destroy the data).

### 3.3 While unlocked (A4)

- **Auto-lock** after 5 min without input by default (Off / 1-30 min),
  independent of deep sleep and **not suspended by "Stay awake on USB"**;
  an open confirmation is dropped unanswered.
- Deep sleep = reboot on wake = locked (PIN screen).
- **Auto-clear** (F-05): a screen showing a secret returns HOME after 5 min
  without input; passwords and PINs masked until the eye is pressed.
- Lock wipes the decrypted records and the key from RAM (`crypto_wipe`).
- Secrets are never written to the serial log (only account names, counts
  and results: names are treated as metadata, not secrets).

### 3.4 Backups (A3)

- `.msfx`: **dedicated passphrase** (never the PIN), Argon2id **t = 3**,
  fresh salt per file, AES-256-GCM with the header as AAD; an unknown
  format, an altered byte or a wrong passphrase are refused before the
  store is touched.
- Export and import ask the **device PIN again** (menu entry, console):
  an unlocked device alone cannot copy everything out or slip modified
  accounts in.
- **SD sanity check** after unlock (at most hourly): offers to overwrite
  and delete plaintext leftovers (MySafeFob JSON, rescue copies).
- The device never writes plaintext; JSON exists only on the PC
  (`msf_backup.py`, offline `msf_editor.html` with a no-network CSP).

### 3.5 Radios (A6)

- Wi-Fi and BLE are **off** except during a manual time sync started from
  Settings > Time; no secret is ever sent. A forged time only produces
  wrong TOTP codes until the next sync (availability, not confidentiality);
  "time sync is old" alert after 90 days.

### 3.6 Serial console (A2)

- Commands that expose or change secrets ask the PIN (`store unlock`,
  `backup export|import <pin>`); `setting` cannot touch the store or its
  counter. Kept in the release on purpose (export path if the screens
  fail).

## 4. Residual risks (accepted for v1 or planned)

| # | Risk | Attacker | Status |
|---|---|---|---|
| R1 | **Flash readout** over USB (`esptool read_flash`) then offline PIN brute force: 10^6 Argon2id(m=2 MiB, t=1) on a PC is hours, with no back-off | A2 | **Open until Flash Encryption** (`SECURE-BOOT-PLAN.md`); the back-off only protects the on-device path |
| R2 | **Counter reset** by rewriting the plaintext control sector over USB | A2 | Same: closed by encrypting `secrets` (plan §3.3) |
| R3 | **Evil maid**: unsigned firmware flashed over USB or through the factory's SD update (`msf-fw.bin`, Power held 10 s) records the PIN | A5 | **Open until Secure Boot v2** (plan); the factory's SD path needs no PC at all |
| R4 | **Rescue copy** `RESCUEnn.MSR` protected by the PIN only: brute-forceable offline in hours if the card leaks | A3 | Accepted (user decision): rare rescue file, warning on screen, SD sanity check offers to wipe it |
| R5 | **Weak backup passphrase** chosen by the user | A3 | Accepted: 8-character minimum, Argon2id t=3; user guide to recommend a long passphrase |
| R6 | **SD wear levelling** keeps blocks of a wiped file | A3 | Accepted, documented: best-effort overwrite; format the card on a PC for certainty |
| R7 | **Shoulder surfing** of the PIN (an observer sees the keys pressed even shuffled) or of a revealed secret | A4 | Accepted: inherent to any PIN pad |
| R8 | **Destructive console commands** (`store erase ERASE`) without the PIN: loss of data, not disclosure | A2 | Accepted: denial of service only; backups are the answer |
| R9 | **Forgotten PIN** = data lost (no recovery by design) | Owner | Accepted: backups + passphrase |
| R10 | **e-paper persistence**: the last image stays visible without power | A1 | Mitigated: lock / sleep replace the screen (PIN screen / sleep image); the sleep image never shows a secret |

## 5. Security tests for Phase 9 (ROADMAP 9.2)

1. Wrong PINs: back-off sequence 10 s / 30 s / 2 min / 10 min / 1 h, counter
   kept across power cuts and reboots, shared by every PIN path (unlock,
   change, backup menu, console, factory).
2. Power cut during a store save (unplug + battery at the critical
   threshold): the previous version still unlocks.
3. Altered `.msfx` (one byte flipped in the header, in the body, truncated
   file): refused, store untouched.
4. Auto-lock and auto-clear with and without USB power.
5. Console: every secret-revealing command refuses without the PIN.
6. After the release hardening (plan step 8): `esptool read_flash` returns
   no plaintext; an unsigned `msf-fw.bin` is refused by the factory.
