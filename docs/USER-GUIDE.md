# MySafeFob — User guide

MySafeFob turns the Xteink X4 Pro e-paper reader into an offline password
and two-factor (TOTP) vault: up to 250 accounts, encrypted, protected by a
6-digit PIN, with encrypted backups on a microSD card.

This guide follows the order in which you will use it: first start, setting
the time, using accounts, filling them from a PC, backups, settings,
security, troubleshooting.

---

## Contents

1. [Controls and screen](#1-controls-and-screen)
2. [First start](#2-first-start)
3. [Setting the time](#3-setting-the-time)
4. [Using your accounts](#4-using-your-accounts)
5. [Filling your accounts from a PC](#5-filling-your-accounts-from-a-pc)
6. [Backup, import and export](#6-backup-import-and-export)
7. [Settings reference](#7-settings-reference)
8. [Security: what protects you, good habits, current limits](#8-security)
9. [Troubleshooting and recovery](#9-troubleshooting-and-recovery)
10. [Quick reference](#10-quick-reference)

---

## 1. Controls and screen

| Control | What it does |
|---|---|
| **Left / Right buttons** | Move the focus (the black outline) to the previous / next item |
| **Home key** (touch key next to the screen) | Confirm the focused item |
| **Power, short press** | Confirm too (setting *Short press*, on by default) |
| **Power, held ~1.5 s** | Sleep (also the ⏻ icon on the home screen) |
| **Power, held 10 s** | Recovery mode (*factory*), see §9 |
| **Touch** | Tap any button directly |

**The touch dead band.** A horizontal band across the middle of the screen
(about 2/3 of the way down) does not register taps reliably on this
hardware. The interface never puts a button there; the band is used for
status messages.

**E-paper.** The screen keeps its image without power. A full black/white
flash happens when you change screen and every ~30 small updates (it cleans
the ghosting); in between, only the changed area is redrawn.

**Sleep and wake.** The device wakes with Power. Waking is a full restart:
the vault is always **locked** after sleep, you type your PIN again.

---

## 2. First start

1. **Charge** the device (USB-C). The battery level is shown top right.
2. **Choose your PIN**: 6 digits, typed twice. The keypad is in order here;
   when you unlock later it is **shuffled** at each attempt (smudge marks
   reveal nothing).

   > ⚠️ **A forgotten PIN cannot be recovered.** There is no reset that keeps
   > the data. Your only safety net is a backup (§6) and its passphrase.

3. **Set the time** (§3). TOTP codes are wrong if the clock is wrong.
4. **Add your accounts**, on the device (§4) or, much faster, from a PC (§5).
5. **Make a first backup** (§6) and store the passphrase somewhere safe.

---

## 3. Setting the time

TOTP codes depend on the exact time. The device keeps it in a battery-backed
clock, including in sleep, but it drifts slowly: resync from time to time.
After 90 days without a sync, the home screen shows an **alert** (⚠ icon,
"Time sync is old"); its screen has a **Set the time** button.

Open **Settings › Time**. Three tabs:

### Wi-Fi (most accurate, easiest at home)
1. Tab **Wi-Fi**, then the 🔍 scan button.
2. Pick your network, type its password (full keyboard), confirm.
3. The device connects, reads the time from the internet (NTP), then turns
   Wi-Fi off again. Wi-Fi is never on outside this step.

### Bluetooth (from an Android phone)
The phone must *serve* the time over Bluetooth. Install **nRF Connect for
Mobile** and set it up once as described in
`docs/ble-time-sync-nrf-connect.md`. Then: tab **Bluetooth**, 🔍 scan, pick
the phone. No pairing, nothing is sent to the phone. iPhone: not supported.

### Manual
Tab with the keyboard icon: type the local date and time, adjust the time
zone with the − / + buttons, confirm. Accurate to the second you press OK
("Press OK at :00" helps you hit the minute).

### From a PC over USB (most precise)
With the device connected by USB:
```
python tools/settime.py            # measures, sets, measures again
python tools/settime.py --check    # only tells the current offset
```
Make sure the PC clock itself is synced first.

---

## 4. Using your accounts

### Home screen
- A **grid of letters** (A-Z and # for names not starting with a letter);
  a letter without accounts has no frame and does nothing. "N entries" at
  the top.
- **Profiles**: when the accounts belong to two people or more (e.g. *Luc*,
  *Pat*, *Min*), the top line becomes a button **"Luc (12)"**: tap it to
  switch to the next profile. Letters, counts and names then only show
  that profile's accounts. The device remembers the last profile used.
- **⚙** Settings (top left), **⏻** sleep (top right), **⚠** alerts (when
  there is one), **+ Add** (new account).

Tap a letter: the names under it, 6 per page (◀ ▶ for the next page). Tap a
name to open the account.

### An account
```
< Back            GitHub              🔋
🕒 123 456   (20 s)                 [⟳]     ← only if the account has a TOTP key
[ⓘ Info][🛡 Security][🛟 Recovery]
...
          [✎ Edit]      [🗑 Remove]
```
- **TOTP code** at the top (when the account has one), with the seconds
  left. It changes by itself at the end of each period. ⟳ recomputes it.
- **Security** tab (open by default): login, password and PIN, masked;
  **👁** shows / hides both.
- **Info** tab: ID (customer or account number, e-mail…), note, extra.
- **Recovery** tab: how many recovery codes are still available, and
  **Open the codes**: tap a code to mark it **used** once you have used it
  (irreversible, asks first). Used codes stay visible, struck through.
- **Remove** deletes the whole account (asks first).

A page showing secrets goes back to the home screen after **5 minutes**
without any input.

### Adding or editing an account
**+ Add** (home or letter page) or **✎ Edit** opens the editor, with the
same three tabs. Nothing is saved until **💾 Save**; **Back** with changes
asks "Discard changes?".

| Tab | Fields (tap a row to type) |
|---|---|
| **Info** | Name (required, unique in its profile), Profile, ID, Note, Extra |
| **Security** | Login, Password, PIN (digits only), TOTP |
| **Recovery** | The codes: tap one to remove it, **+ Add**, **Clear all** |

- Three keyboards appear depending on the field: full (with a symbols
  layer), digits only (PIN), Base32 A-Z / 2-7 (TOTP key).
- **🎲** next to the password generates a strong 20-character password;
  Enter keeps it.
- **Profile**: the person the account belongs to; empty (or "Default") = *Default*. A new
  account starts in the profile shown on the home screen. Type a new name
  to create a profile, or **⟳** to step through the existing ones. A
  profile disappears with its last account. After Save, the home screen
  shows the account's profile.
- **Settings › Profiles** opens straight the profile's page when there is
  only one (or no account yet): **+ New profile**, **Rename**, **Delete
  with its accounts**. Deleting the only profile deletes every account; the
  device starts again with an empty *Default* profile.
- With two profiles or more, it lists them with their number of accounts
  (✓ = the one shown on the home screen). **+ New** creates one: type its
  name, then its first account opens (a profile exists through its
  accounts, so it is created when that account is saved). Tap a profile:
  - **Rename**: type the new name (empty = *Default*). Giving it the name
    of another profile merges the two, unless an account name would then
    appear twice (the screen says which one).
  - **Delete with its accounts**: deletes the profile **and all its
    accounts**, after a confirmation screen. It cannot be undone: export a
    backup first if in doubt.
- **TOTP**: type the secret key given by the service ("can't scan the QR
  code? enter this key"), choose 6 / 8 digits and 30 / 60 s (6 and 30 s
  almost everywhere).
- **Limits**: name 48 characters; profile 32; ID, login, note, extra 64; password 128;
  PIN 12 digits; TOTP key 103; 32 recovery codes of 16 characters; 250
  accounts. Accented letters and symbols outside basic ASCII are not shown
  on this screen: avoid them in anything you need to read.

---

## 5. Filling your accounts from a PC

Typing 50+ accounts on the device is slow. The PC tools let you prepare
them with a real keyboard, then import them in one go.

**You need**: Python 3 with `pip install argon2-cffi cryptography` (once),
a web browser, a microSD card (FAT32).

### 5.1 The account editor (web page, works offline)
Open `tools/msf_editor.html` in your browser (double-click). It never
connects to the internet.

1. **New** (start empty) or **Open JSON…** (a file you saved before, or one
   decrypted from a backup, §6.4).
2. The list on the left is grouped by letter; click a letter to fold /
   unfold it, **▾▸** folds / unfolds all, the search box filters.
3. **+ Add**, then fill the sections **Info**, **Security**, **Security —
   TOTP**, **Recovery codes**:
   - each field shows its size (e.g. `42/64`): orange near the limit, red
     over it;
   - paste an **`otpauth://` link** in the TOTP key (the export format of
     most authenticator apps, or the text behind a QR code): key, digits and
     period are filled in, and the current code is shown so you can check it
     against your phone;
   - **Paste several…** adds a whole list of recovery codes at once;
     **Remove all codes** clears them (e.g. when the service issues new ones).
4. Watch the colours in the list:
   - **red ⚠**: an error (too long, missing name…) — the file cannot be
     encrypted until it is fixed; the red "N with errors" link jumps to them;
   - **violet ↻**: the same password is used by other accounts (stronger
     warning when the login is the same too): if one site leaks, the others
     are exposed — change them when you can;
   - **orange**: added / edited since the last save.
5. **Save JSON** — the file goes to your Downloads folder.

> ⚠️ **The JSON file contains all your secrets in clear.** Keep it in a
> private folder while you work, never on a shared or synced drive, and
> delete it (and empty the recycle bin) as soon as it is encrypted.

### 5.2 Encrypt it
```
python tools/msf_backup.py encrypt accounts.json
```
Type a **backup passphrase** twice (8 to 64 characters; see §6.1 to choose
it). You get `MSF-YYYYMMDD-HHMM.msfx`. Any problem (too many accounts, a
field too long) is listed and nothing is written. Then **delete the JSON**.

### 5.3 Import it on the device
Copy the `.msfx` into a folder **`MSF`** at the root of the microSD card,
insert the card, then follow §6.3.

---

## 6. Backup, import and export

All backups are **encrypted files** (`.msfx`) protected by a **passphrase of
your choice**, never by the device PIN. The device never writes your
secrets in clear.

### 6.1 Choosing the backup passphrase
- 8 characters minimum, but **longer is much better**: a short sentence of
  4-5 unrelated words is both strong and memorable.
- It is **not** your PIN, and should not be a password you use elsewhere.
- **Without it, a backup cannot be read**, by anyone. Write it down and keep
  it apart from the card (e.g. in another password manager, or on paper in
  a safe place).

### 6.2 Export (device → card)
1. Insert a microSD card (FAT32).
2. **Settings › Security › Backup** — your **PIN is asked** first.
3. **Export to the SD card**, type the passphrase twice. With two profiles
   or more, the **Export:** button below the field chooses what goes in the
   file: *all profiles* (default, the full backup) or *Luc only* (the
   profile shown on the home screen), e.g. to give Pat a backup without
   Luc's secrets. The file name never shows the profile.
4. "Encrypting and writing…" (a few seconds), then ✓ and the file name, e.g.
   `MSF/MSF-20261016-1430.msfx`. Files are never overwritten.

Do it after any important change, and keep a copy of the file somewhere
else than the card.

### 6.3 Import (card → device)
1. **Settings › Security › Backup** (PIN), **Import from the SD card**.
2. Pick the file (newest first, with its date and number of accounts).
3. Type its passphrase. "Decrypting…"
4. Read the **summary**: the profiles in the file, new, changed, unchanged
   accounts, and how many *Replace* would remove.
5. Choose:
   - **Merge** — adds the new accounts and updates the ones that differ;
     accounts that are not in the file stay untouched. Use it after editing
     on the PC.
   - **Replace** — **the profiles in the file** become exactly the file:
     their accounts not in it are **deleted** (asks first). Profiles that
     are not in the file are never touched: restoring Pat's backup leaves
     Luc's accounts alone. A full backup restores everything.
   - **Into: Default / Luc** — only for a file made without profiles (older
     firmware, a PC JSON without "profile") while another profile is shown
     on the home screen: tap it to import the file into that profile
     instead of *Default*, then Merge or Replace.

   If the summary shows more "changed" accounts than you expected, the file
   may be **older** than the device: cancel and export first.

### 6.3b SD card as a USB disk (no need to remove the card)
1. Connect the device to the PC with the USB cable.
2. **Settings › Security › Backup** (PIN), **SD card as a USB disk**, **Start**.
3. The store is **locked** first; the screen shows "Waiting for the PC",
   then "Connected to the PC": the card appears as a disk on the PC.
4. Copy `.msfx` files to or from its `MSF` folder.
5. **Eject the disk on the PC**, then press **Done: restart**. The device
   restarts and asks for the PIN; the import (6.3) then sees the new files.

While the disk is on the PC the device does not sleep, and the USB serial
console is off (it comes back after the restart). Only the SD card is
shared, never the device's own memory, and it only holds encrypted files.

### 6.4 Editing your existing accounts on the PC
1. Export on the device (§6.2), copy the `.msfx` to the PC.
2. `python tools/msf_backup.py decrypt MSF-20261016-1430.msfx` (passphrase)
   → `MSF-20261016-1430.json`.
3. Open it in the editor (§5.1), change what you need, **Save JSON**.
4. Encrypt (§5.2), copy to the card, **Import › Merge** (§6.3).
5. Delete the JSON.

### 6.5 Restoring on a new device (or after a reset)
1. Create the new PIN at first start.
2. Insert the card with your latest `.msfx`, **Import › Replace**.

### 6.6 Rescue export (when the app no longer starts)
If the device starts but the normal screens fail, the recovery mode can
still copy your vault:
1. Hold **Power 10 s** → recovery menu.
2. Left/Right to **Export accounts**, confirm with the Home key.
3. Type your PIN digit by digit: Left/Right change the digit, Home confirms
   it, Power cancels.
4. The vault is copied to `MSF/RESCUE01.MSR`.
5. On the PC: `python tools/msf_backup.py decrypt RESCUE01.MSR` (asks the
   **PIN**), then encrypt the JSON into a normal `.msfx` (§5.2).

> ⚠️ A rescue file is protected **by the PIN only** (much weaker than a
> passphrase). Delete it from the card as soon as you have your JSON.

### 6.7 SD card check
With **Check SD at start** on (Backup menu, on by default), after you unlock
the device (at most once an hour) it looks on the card for leftovers in
clear: MySafeFob JSON files and rescue copies. If it finds some, it offers
to **delete** them (overwritten, then deleted). Other files on the card are
never touched. For certainty, format the card on a PC.

---

## 7. Settings reference

| Menu | Item | Default | What it does |
|---|---|---|---|
| **Device › Controls** | Short press | On | Power short press = confirm |
| | Auto-sleep | 1 min | Sleep after Off / 30 s / 1 / 2 min without input |
| | Stay awake (USB) | Off | Never sleeps while on USB power (does **not** stop the auto-lock) |
| **Device › Display** | Frontlight | Off | Front light on / off |
| | Color | Warm | Warm / Neutral / Cool |
| | Intensity | 60 % | 🌙 / ☀ steps of 10 % |
| **Device › Touch Calibration** | | | Tap the targets if taps land in the wrong place |
| **Time** | Wi-Fi / Bluetooth / Manual | | §3 |
| **Security** | Change PIN | | Current PIN, then the new one twice |
| | Lock now | | Locks immediately (PIN screen) |
| | Backup | | §6 (asks the PIN) |
| | Auto-lock | 5 min | Locks after Off / 1 / 2 / 5 / 10 / 30 min without input |
| | Symbol keypad | Off | PIN keys show pictures (star, eye, smile…) instead of digits — cosmetic, the PIN stays the same digits |
| **Backup** | Check SD at start | On | §6.7 |
| | SD card as a USB disk | | §6.3b |
| **Profiles** | Rename / Delete | | §4 (Delete removes the profile's accounts too, asks first) |
| **About** | | | Firmware version and build, free memory, UTC time, last time sync (with the measured drift) and when the next one is due, **Owner info** |
| **About › Owner info** | Show on sleep screen | Off | A phone number or e-mail shown on the sleep screen, so whoever finds the device can return it |

---

## 8. Security

### What protects your data
- The vault is encrypted (**AES-256-GCM**). Its key is derived from your PIN
  with **Argon2id**, a deliberately slow, memory-hungry function (about 1 s
  per try on the device).
- **Wrong PINs** are counted, even across restarts: from the 3rd one, you
  wait 10 s, 30 s, 2 min, 10 min, then 1 hour between tries.
- **Auto-lock**, and the PIN is asked again for backups.
- Wi-Fi and Bluetooth are **off** except while you sync the time.
- Backups are encrypted with their own passphrase.

### Good habits
- Never write the PIN on the device or its case.
- Keep at least one recent `.msfx` backup off the card, and its passphrase
  somewhere safe.
- Delete JSON files as soon as they are encrypted.
- Use the 🎲 generator and fix the **reused passwords** the editor shows.
- Check that nobody watches the screen when you reveal a password.

### Current limits (until the release hardening)
The firmware is not yet signed and the flash memory not yet encrypted
(`docs/SECURE-BOOT-PLAN.md`). Until then, someone who takes the device
**and** connects it to a PC could copy the encrypted vault and try PINs on
the PC, or install a modified firmware. **Do not leave the device where
someone could take it for a while**, and keep backups. Details:
`docs/SECURITY-THREAT-MODEL.md`.

---

## 9. Troubleshooting and recovery

| Problem | What to do |
|---|---|
| **TOTP codes rejected** | The clock is probably off: sync the time (§3). Check 6/8 digits and 30/60 s match the service |
| **Forgot the PIN** | The data cannot be recovered. Erase and start again (console `store erase ERASE`), then **Import › Replace** from your backup |
| **"Too many attempts: wait…"** | Wait the time shown; the delay grows with each wrong PIN |
| **Taps land in the wrong place** | Settings › Device › Touch Calibration |
| **Battery shows 0 % after being fully drained** | Charge it; the gauge reloads its profile by itself and shows a real value again |
| **"No SD card"** | The card must be FAT32; insert it fully; try another card |
| **The app does not start / a firmware update failed** | Hold Power 10 s → recovery menu: **Boot app0**, or **SD → app0** with a firmware file `msf-fw.bin` at the root of the card, or **Export accounts** (§6.6). Power leaves the menu |

### Recovery menu (Power held 10 s)
Left/Right choose, the Home key confirms, Power leaves:
- **Boot app0** — start the normal app.
- **SD → app0** — install the firmware file `msf-fw.bin` from the card root
  (renamed `msf-fw.ok` or `msf-fw.bad` afterwards). Your accounts are kept.
- **Export accounts** — rescue copy of the vault (§6.6).

### USB console (advanced)
Connected to a PC, a serial terminal gives commands (`help` lists them),
for example: `backup list`, `backup export <PIN> "<passphrase>"`,
`backup import <file> <PIN> "<passphrase>" merge|replace`, `settime`,
`battery`, `setting list`. Commands touching your secrets ask the PIN.

---

## 10. Quick reference

| I want to… | Do |
|---|---|
| Unlock | Power, type the PIN |
| Lock | Settings › Security › Lock now (or let it sleep) |
| Read a TOTP code | Letter › name — the code is at the top |
| See a password | Account › Security tab › 👁 |
| Add an account | Home › + Add |
| Sync the time | Settings › Time › Wi-Fi / Bluetooth / Manual |
| Back up | Settings › Security › Backup › Export |
| Restore | Settings › Security › Backup › Import › Replace |
| Edit everything on a PC | Export → `decrypt` → editor → `encrypt` → Import › Merge |
| Recovery mode | Hold Power 10 s |
