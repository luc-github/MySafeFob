# TOTP — Time-based One-Time Password
## Principle, RFC 6238, and reference implementation

---

## 1. What is TOTP?

**TOTP** = **T**ime-based **O**ne-**T**ime **P**assword

It's a one-time password that changes every **30 seconds** (by default). You see it in:
- Google Authenticator
- Authy
- Aegis
- Microsoft Authenticator
- Your future X4 Pro firmware

**The key principle**: the server (Google, GitHub, etc.) and your device share the **same secret**. With this secret and the current time, both independently compute the same 6-digit code. No network needed at the moment of generation.

---

## 2. The algorithm in 5 steps (RFC 6238)

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│  Shared secret   │     │  UNIX timestamp  │     │   30-second     │
│   (Base32)       │     │   (seconds)      │     │   time window   │
└────────┬────────┘     └────────┬────────┘     └────────┬────────┘
         │                       │                       │
         │    ┌─────────────────────────────────────┐   │
         └───►│  1. Decode the secret (Base32 → raw) │   │
              │  2. Counter = Timestamp // 30        │◄──┘
              │  3. HMAC-SHA1(secret, counter)       │
              │  4. Dynamic truncation               │
              │  5. Code = number % 1_000_000        │
              └─────────────────────────────────────┘
                             │
                             ▼
                    ┌─────────────────┐
                    │  6-digit code    │
                    │  (e.g.: 123456)  │
                    └─────────────────┘
```

---

## 3. The 5 steps in detail

### Step 1: Decode the secret (Base32 → bytes)

The secret is often provided as a QR code, which contains a URL:
```
otpauth://totp/GitHub:your-email?secret=JBSWY3DPEHPK3PXP&issuer=GitHub
```

The `secret=` parameter is in **Base32** (alphabet: A-Z, 2-7).

**Why Base32?** Because it's easy to type by hand (no confusion between 0/O, 1/I).

**Example**:
```
Base32 : "JBSWY3DPEHPK3PXP"
Hex    : 48 65 6c 6c 6f 21 de ad be ef  
Text   : "Hello!\xde\xad\xbe\xef"
```

In the Python code:
```python
def base32_decode(secret_b32: str) -> bytes:
    padding = 8 - (len(secret_b32) % 8)
    if padding != 8:
        secret_b32 += "=" * padding
    return base64.b32decode(secret_b32.upper(), casefold=True)
```

---

### Step 2: Compute the counter

The counter is the **number of 30-second windows** elapsed since January 1, 1970 (UNIX epoch).

```
Counter = floor(Timestamp_UNIX / 30)
```

**Example**:
```
Timestamp = 1 787 820 509 (August 27, 2026, ~16:48 UTC)
Counter   = 1 787 820 509 // 30 = 59 594 016
```

> 💡 *Everyone using the same secret generates the **same code** during that 30-second window.*

In the Python code:
```python
counter = timestamp // 30
```

---

### Step 3: HMAC-SHA1 (the cryptography)

**HMAC** = Hash-based Message Authentication Code

This is a cryptographic operation that "signs" the counter with the secret:

```
MAC = HMAC-SHA1(secret_key, counter_bytes)
```

The counter (a 64-bit integer) must be encoded as **big-endian** over 8 bytes:

```
Counter = 59 594 016
Big-endian hex: 00 00 00 00 03 8F 6D 60
```

**Why SHA-1?** The TOTP standard (RFC 6238) specifies SHA-1 by default. Some services use SHA-256, but the vast majority still use SHA-1.

The HMAC result is a **20-byte** hash (160 bits):

```
MAC = a3 7b 9c f2 11 4e 8d ... (20 bytes total)
```

In the Python code:
```python
counter_bytes = struct.pack(">Q", counter)  # Big-endian, 8 bytes
mac = hmac.new(secret, counter_bytes, hashlib.sha1).digest()
```

---

### Step 4: Dynamic Truncation

We have 20 bytes of HMAC, but we want a short code. **Dynamic truncation** (RFC 4226) pseudo-randomly selects 4 bytes out of the 20.

**Method**:
1. Take the **last byte** of the MAC: `mac[19]`
2. Extract its 4 low-order bits (mask `0x0F`) → gives an offset between 0 and 15
3. Read 4 consecutive bytes starting at this offset: `mac[offset:offset+4]`
4. Interpret these 4 bytes as a **big-endian** 32-bit integer
5. Clear the high-order bit (mask `0x7FFFFFFF`) to guarantee a positive number

**Example**:
```
MAC (20 bytes)    : [a3] [7b] [9c] ... [f2] [4e] [8d] [ab] [cd] [3f]
                    ^0   ^1   ^2        ^13  ^14  ^15  ^16  ^17  ^18  ^19

Last byte         : mac[19] = 0x3F
Offset            : 0x3F & 0x0F = 0x0F = 15
4 bytes at offset : mac[15:19] = [cd] [3f] ? ?  → careful not to exceed 16!

In practice, the max offset is 15, so we read mac[15:19] (4 bytes)
```

> 💡 *The "truncation" is "dynamic" because the offset changes with every calculation (it depends on the HMAC).*

In the Python code:
```python
offset = mac[-1] & 0x0F                              # Last byte, low 4 bits
code = struct.unpack(">I", mac[offset:offset + 4])[0]  # 4 bytes → 32-bit integer
code = code & 0x7FFFFFFF                              # Sign bit to 0
```

---

### Step 5: Reduction to 6 digits

We have a large number (31 bits, so up to 2 billion). We reduce it to **6 digits** with a modulo:

```
Code = number % 1_000_000
```

Then we pad with leading zeros if needed:

```
If Code = 660314 → "660314"
If Code = 42     → "000042"
```

In the Python code:
```python
code = code % (10 ** digits)          # digits = 6 by default
return f"{code:0{digits}d}"           # Zero-padding
```

---

## 4. Visual summary

```
Base32 secret ──────┐
                    ▼
            ┌───────────────┐
            │ Base32 Decode │
            └───────┬───────┘
                    │ Raw bytes (20 bytes)
                    ▼
Timestamp ────┐    ┌─────────────┐
              └───►│ Counter =   │
                   │ timestamp//30│
                   └──────┬──────┘
                          │ 8 bytes big-endian
                          ▼
                   ┌─────────────┐
                   │ HMAC-SHA1   │
                   └──────┬──────┘
                          │ 20 bytes
                          ▼
                   ┌─────────────┐
                   │ Dynamic     │
                   │ Truncation  │
                   └──────┬──────┘
                          │ 4 bytes → 31-bit integer
                          ▼
                   ┌─────────────┐
                   │ % 1_000_000 │
                   └──────┬──────┘
                          │ 0-999999
                          ▼
                   ┌─────────────┐
                   │ "000000"    │
                   └─────────────┘
```

---

## 5. Time window and tolerance

The server (Google, GitHub) doesn't check just **one** code — it checks **3**:

```
Previous code (T-30s)  → accepted (slow synchronization)
Current code  (T)      → accepted (normal)
Next code     (T+30s)  → accepted (client clock ahead)
```

This is why a **drift of a few seconds** between your device and the server isn't a problem. But a drift of several minutes is.

> ⏱️ *The clock is therefore critical. A hardware RTC (BM8563 on the X4 Pro) is essential.*

---

## 6. Difference between TOTP and HOTP

| | **HOTP** | **TOTP** |
|---|---|---|
| **Counter** | Incremental (0, 1, 2, 3...) | Time-based (timestamp//30) |
| **Synchronization** | Server and client must be synchronized on the counter | Server and client must be synchronized on time |
| **Usage** | YubiKey keys (physical press) | 2FA apps (Authy, Google Authenticator) |
| **RFC** | RFC 4226 | RFC 6238 (extends HOTP with time) |

**TOTP = HOTP(secret, counter=timestamp//30)**

---

## 7. Official test vectors (RFC 6238)

To validate an implementation, the RFC provides test vectors with the secret `12345678901234567890` (20 bytes):

| Timestamp | Counter | Code (8 digits) |
|-----------|----------|-------------------|
| 59 | 1 | `94287082` |
| 1 111 111 109 | 37 037 036 | `07081804` |
| 1 111 111 111 | 37 037 037 | `14050471` |
| 1 234 567 890 | 41 152 263 | `89005924` |
| 2 000 000 000 | 66 666 666 | `69279037` |
| 20 000 000 000 | 666 666 666 | `65353130` |

The Python script `totp_reference.py` validates all these vectors at startup.

---

## 8. Python ↔ C (ESP-IDF) mapping

| Step | Python (reference script) | C (ESP-IDF / mbedtls) |
|-------|---------------------------|----------------------|
| Base32 decode | `base64.b32decode()` | Custom function (~30 lines) |
| Counter → bytes | `struct.pack(">Q", c)` | `uint8_t buf[8];` manual shift |
| HMAC-SHA1 | `hmac.new(key, msg, sha1)` | `mbedtls_md_hmac(sha1, ...)` |
| Truncation | `mac[-1] & 0x0F` | Same bit-by-bit logic |
| Big-endian read | `struct.unpack(">I", ...)` | `(buf[0]<<24) \| (buf[1]<<16) ...` |
| Modulo | `% 1_000_000` | `% 1000000` |
| Format | `f"{code:06d}"` | `snprintf(buf, 7, "%06lu", code)` |

---

## 9. Security: the secret is everything

**The shared secret is the only thing protecting your account.**

- If someone obtains your Base32 secret → they can generate the same codes as you
- The secret must **never** transit in cleartext over the network (the QR code is displayed only once)
- On your X4 Pro, the secret will be encrypted in flash with AES-256 — inaccessible without the master PIN

> 🔒 *Authy stores your secrets encrypted in Twilio's cloud. Your X4 Pro will only store them locally, encrypted, without ever sending them over the network.*

---

## 10. Project files

| File | Role |
|---------|------|
| `totp_reference.py` | Reference Python implementation (RFC 6238 validated) |
| `docs/totp-principle.md` | This document — explanation of the principle |
| `components/totp_engine/` | (Future) C implementation for ESP32 |

---

*Document written on 2026-08-27 — to be used as a reference during firmware development.*
