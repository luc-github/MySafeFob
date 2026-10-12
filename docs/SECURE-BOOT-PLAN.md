# Secure Boot v2 + Flash Encryption — step-by-step plan

**Status**: plan only (decided 2026-10-11: not critical for now, to be done
before the final v1 release). Nothing in this document has been enabled on
a device yet. Every eFuse step is **irreversible**: read the whole document
before starting, and follow the order.

Target: ESP32-S3 (X4 Pro), ESP-IDF 5.5.5. References: ESP-IDF docs
`security/secure-boot-v2.rst`, `security/flash-encryption.rst`,
`security/security-features-enablement-workflows.rst`,
`api-guides/bootloader.rst` (section "Bootloader Size"). Related: ADR-003
(firmware only via SD or serial, "residual threat accepted until the final
prerelease"), ADR-007 (factory partition + bootloader hook), ADR-011
(single app slot), `INTERFACES.md` §1-§3.

---

## 1. Why

What an attacker with the device in hand (but not the PIN) can do today:

| Attack | Today | With Flash Encryption (release) | + Secure Boot v2 |
|---|---|---|---|
| Dump the flash over USB (`esptool read_flash`) and brute-force the 6-digit PIN offline on a PC (no back-off, Argon2id m=2 MiB t=1) | **Possible**: hours at most | Blocked: the flash reads back as ciphertext, download mode cannot decrypt | Blocked |
| Reset the PIN failure counter (plaintext control sector) by rewriting the flash | **Possible** | Blocked if `secrets` is marked `encrypted` (see 4.3) | Blocked |
| Flash a modified firmware (SD `msf-fw.bin` through the factory, or USB) that records the PIN, then give the device back | **Possible** | Still possible through the factory (it encrypts whatever it writes) | **Blocked**: only images signed with our key boot |

Flash Encryption protects the data at rest; Secure Boot protects the
code. Only both together close the "evil maid" case (modified firmware
handed back). The PIN back-off on the device is only meaningful once the
store cannot be copied out.

What it does NOT protect against: someone who knows the PIN; an unlocked
device left unattended (auto-lock covers that); a weak backup passphrase
(`.msfx` files on the SD card are protected by their passphrase only).

---

## 2. Decisions to take first

1. **Signing scheme**: RSA-3072 (recommended by ESP-IDF on the S3: the
   S3's ECDSA Secure Boot is flagged "not recommended" in the IDF docs for
   this chip revision, and RSA verifies faster at boot).
2. **Flash encryption key**: generated on the device (default, nobody ever
   knows it) **or** generated on the PC and burned (allows flashing
   pre-encrypted images over USB later, but the key file must then be
   guarded like the signing key). Recommended: **on the device** — updates
   go through the factory + SD anyway (ADR-003/011).
3. **Key size**: XTS-AES-256 (512-bit key, two eFuse key blocks). The S3
   has 6 key blocks: 2 for flash encryption, 1 for the Secure Boot key
   digest, 3 left (spare digests for key revocation, or NVS encryption).
4. **Download mode in release**: `DIS_DOWNLOAD_MODE` (USB flashing gone for
   good) **or** Secure Download Mode (limited commands, no flash read).
   Recommended: **Secure Download Mode** — keeps a way to talk to the ROM,
   reveals nothing.
5. **Which partitions are encrypted**: always the bootloader, partition
   table, `otadata` and every app partition (`factory`, `app0`). Plus,
   recommended: `secrets` (marked `encrypted`, see 4.3). `nvs` holds no
   secret (settings, owner info that is shown on the sleep screen anyway):
   NVS encryption optional, not planned.
6. **Who holds the signing key, and its backup**: losing it = no firmware
   update ever again on that device. Two offline copies (e.g. encrypted
   USB stick + paper/QR of the PEM in a safe), never in the repository.

---

## 3. Code and layout changes needed BEFORE touching eFuses

All of this is testable on a device with no eFuse burned.

### 3.1 Bootloader size vs the 0xB000 otadata backup (ADR-007 contract)

Flash layout today: bootloader at 0x0, **otadata backup at 0xB000**
(written by the bootloader hook, `boards/x4pro/factory/bootloader_components/custom_bootloader/hooks.c`),
partition table at 0xC000. The bootloader must therefore end below 0xB000
(44 KiB). Measured 2026-10-11: the factory build's bootloader (the one
flashed, with the hook) = 0x69B0 bytes (27,056), 0x4650 (~17.6 KiB) left
below 0xB000.

Secure Boot v2 appends a **4 KiB signature sector** to the bootloader, and
both features add code. ESP-IDF's limit for the S3 is 64 KiB before the
signature, but **ours is 44 KiB including the signature**. Steps:

1. Enable the options of section 5 in a branch, build, read the printed
   bootloader size.
2. If bootloader + 4 KiB signature > 0xB000: set the bootloader log level
   to Warning/None and optimization to Size; if still too big, **move the
   otadata backup and the partition table** (e.g. backup 0xF000, table
   0x10000, `otadata` 0x14000...) — this changes the ADR-007 contract:
   factory, bootloader hook, `partitions.csv`, `INTERFACES.md` §2 and the
   flash tool must change together.

### 3.2 Bootloader hook and flash encryption

The hook copies `otadata` to 0xB000 and back. With flash encryption the
ciphertext depends on the **address** (XTS tweak): copying encrypted bytes
from 0x10000 to 0xB000 and back gives garbage. The hook must read with
decryption and write with encryption (`bootloader_flash_read(..., true)` /
`bootloader_flash_write(..., true)`), and 0xB000 must be treated as an
encrypted region. To be implemented and tested with flash encryption in
**development** mode (3 re-flash cycles available, see 6.2).

### 3.3 `secrets` partition: 16-byte alignment

Encrypted flash writes must be 16-byte aligned, in multiples of 16 bytes.
`secret_store.c`:

- control sector `control_t` = 16 bytes: OK.
- slot header `slot_header_t` = 46 bytes, and the body is written right
  after it (`kSlotOff + sizeof(h)`): **not aligned**. Pad the header to 64
  bytes (new `FORMAT_VERSION`, the reader accepting both during the
  migration, or a simple export / erase / re-import).
- body length `ct_len + 16` (tag): round the write up to 16.

Same rule for anything the factory or the app writes raw.

### 3.4 Factory

- Must be built with the same Secure Boot / flash encryption options, so
  it is itself signed and its `esp_ota_*` writes into `app0` are encrypted
  and the new image's signature verified (`CONFIG_SECURE_SIGNED_ON_UPDATE`,
  done by `esp_ota_end` / `esp_ota_set_boot_partition`).
- The SD update file `msf-fw.bin` must be the **signed** app binary.
- After release mode the factory and the bootloader can no longer be
  replaced (no USB flashing, the bootloader is never updated by OTA): the
  factory must be final **before** step 7.

### 3.5 Tools

- `tools/flash_scripts` (flash manager): signed images, encrypted-flash
  flags in development mode; refuse plain flashing once release mode is
  detected (`espefuse summary`).
- Keep `msf_backup.py` unchanged (the `.msfx` format does not depend on
  any of this).

---

## 4. Configuration (sdkconfig, app AND factory)

Indicative names, to be checked in `idf.py menuconfig` → Security features:

```
CONFIG_SECURE_BOOT=y
CONFIG_SECURE_BOOT_V2_ENABLED=y
CONFIG_SECURE_BOOT_V2_RSA_ENABLED=y          # RSA-3072
CONFIG_SECURE_BOOT_SIGNING_KEY="..."         # path OUTSIDE the repo
CONFIG_SECURE_FLASH_ENC_ENABLED=y
CONFIG_SECURE_FLASH_ENCRYPTION_AES256=y      # XTS-AES-256
CONFIG_SECURE_FLASH_ENCRYPTION_MODE_DEVELOPMENT=y   # step 6, then RELEASE at step 7
CONFIG_SECURE_UART_ROM_DL_MODE / secure download mode   # decision 2.4
```

`partitions.csv`: add the `encrypted` flag to `secrets` (decision 2.5).
Keep the `.gitignore` covering `*.pem` and any key file.

---

## 5. Step by step

Each step ends with a check; do not go further if it fails. Before any
eFuse step: **export a backup** (Settings > Security > Backup), check it on
the PC (`msf_backup.py decrypt`), keep the PIN and passphrase at hand,
uninterrupted power (USB + charged battery; a power cut while eFuses are
being written can brick the chip).

| # | Step | Reversible? | Check |
|---|------|-------------|-------|
| 1 | Code changes of section 3 (alignment, hook, tools), on a branch. Normal build, normal device. | Yes | Store, backup, factory update all still work |
| 2 | Generate the signing key: `idf.py secure-generate-signing-key --version 2 --scheme rsa3072 msf_signing.pem` (or `espsecure.py generate_signing_key`). Store the two offline copies (decision 2.6). | Yes (just a file) | Key readable from both copies |
| 3 | Build app + factory with section 4 options, flash encryption in **development** mode, Secure Boot **not yet** enabled. Check the bootloader size (3.1). | Yes | Build OK, bootloader + 4 KiB below 0xB000 (or layout moved) |
| 4 | Read the chip state: `espefuse.py summary` (save the output). | Yes | No security eFuse already burned |
| 5 | Flash the plaintext images. First boot: the bootloader generates the key, encrypts flash in place (up to a minute: **do not unplug**), burns `SPI_BOOT_CRYPT_CNT` = 1. | **No** (key burned; dev mode still allows plaintext re-flash, a limited number of times) | Boots; `espefuse summary` shows the key blocks read-protected; app, PIN, store, backup, factory update all work |
| 6 | Test everything under flash encryption: unlock, accounts, backup export/import, factory SD update (encrypted write path), factory entry (Power held 10 s, awake and asleep), otadata backup/restore by the hook, deep sleep and wake. | Dev mode: re-flash possible (each plaintext re-flash spends one `SPI_BOOT_CRYPT_CNT` toggle, 3 bits: keep at least one) | All features OK |
| 7 | Enable Secure Boot v2 (build with `CONFIG_SECURE_BOOT`), flash the signed bootloader + images. First boot burns the key digest and enables verification. | **No** | `espefuse summary`: secure boot enabled, digest burned; an unsigned app is refused (test with a deliberately unsigned `msf-fw.bin` through the factory) |
| 8 | Switch flash encryption to **release** mode (rebuild with `..._MODE_RELEASE`, flash through the dev-mode path one last time, or `espefuse.py` per the IDF workflow), set the download-mode decision (2.4). | **No** | Plaintext USB flashing refused; `esptool read_flash` returns ciphertext/denied; full feature test again |
| 9 | Record the final `espefuse summary`, the firmware version and the signing-key fingerprint in the release notes. | — | — |

From then on, the only update path is: build signed `msf-fw.bin` → SD
card → factory (Power held 10 s) → `app0`.

---

## 6. Recovery and limits

1. **Lost signing key**: no new firmware can ever boot on that device.
   Data stays readable (PIN) and exportable (backup).
2. **Development mode re-flashes**: limited by `SPI_BOOT_CRYPT_CNT` (3
   bits); the last one should be spent switching to release mode.
3. **Broken factory after release**: no way to replace it. Hence step 6
   (exhaustive test) and 3.4 (factory final before step 7).
4. **Forgotten PIN**: unchanged — data lost unless a `.msfx` backup and its
   passphrase exist.
5. **Key revocation**: Secure Boot v2 on the S3 accepts up to 3 key
   digests; burning a spare digest at step 7 allows migrating to a new
   signing key later (aggressive or conservative revocation, IDF docs).

---

## 7. Open points to settle when starting

- Measure the real bootloader size with both features (3.1) — decides
  whether the ADR-007 layout moves.
- Exact Kconfig names and the release-mode switch procedure for IDF 5.5.5
  (`security-features-enablement-workflows.rst`).
- Whether a second X4 Pro is available to rehearse steps 5-8 first (strongly
  recommended: every step from 5 on is irreversible).
