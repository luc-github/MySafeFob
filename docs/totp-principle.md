# TOTP — Time-based One-Time Password
## Principe, RFC 6238, et implémentation de référence

---

## 1. Qu'est-ce que TOTP ?

**TOTP** = **T**ime-based **O**ne-**T**ime **P**assword

C'est un mot de passe à usage unique qui change toutes les **30 secondes** (par défaut). Tu le vois dans :
- Google Authenticator
- Authy
- Aegis
- Microsoft Authenticator
- Ton future firmware X4 Pro

**Le principe clé** : le serveur (Google, GitHub, etc.) et ton device partagent le **même secret**. Avec ce secret et l'heure actuelle, les deux calculent indépendamment le même code à 6 chiffres. Pas besoin de réseau au moment de la génération.

---

## 2. L'algorithme en 5 étapes (RFC 6238)

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│   Secret partagé │     │  Timestamp UNIX  │     │   Fenêtre temps │
│   (Base32)       │     │  (secondes)      │     │   30 secondes   │
└────────┬────────┘     └────────┬────────┘     └────────┬────────┘
         │                       │                       │
         │    ┌─────────────────────────────────────┐   │
         └───►│  1. Décoder le secret (Base32 → raw) │   │
              │  2. Counter = Timestamp // 30        │◄──┘
              │  3. HMAC-SHA1(secret, counter)       │
              │  4. Troncature dynamique             │
              │  5. Code = nombre % 1_000_000        │
              └─────────────────────────────────────┘
                             │
                             ▼
                    ┌─────────────────┐
                    │  Code à 6 chiffres │
                    │  (ex: 123456)     │
                    └─────────────────┘
```

---

## 3. Les 5 étapes détaillées

### Étape 1 : Décoder le secret (Base32 → bytes)

Le secret est souvent fourni sous forme de QR code, qui contient une URL :
```
otpauth://totp/GitHub:ton-email?secret=JBSWY3DPEHPK3PXP&issuer=GitHub
```

Le paramètre `secret=` est en **Base32** (alphabet : A-Z, 2-7).

**Pourquoi Base32 ?** Parce que c'est facile à taper à la main (pas de 0/O, 1/I confondus).

**Exemple** :
```
Base32 : "JBSWY3DPEHPK3PXP"
Hex    : 48 65 6c 6c 6f 21 de ad be ef  
Texte  : "Hello!\xde\xad\xbe\xef"
```

Dans le code Python :
```python
def base32_decode(secret_b32: str) -> bytes:
    padding = 8 - (len(secret_b32) % 8)
    if padding != 8:
        secret_b32 += "=" * padding
    return base64.b32decode(secret_b32.upper(), casefold=True)
```

---

### Étape 2 : Calculer le compteur (Counter)

Le compteur est le **nombre de fenêtres de 30 secondes** écoulées depuis le 1er janvier 1970 (epoch UNIX).

```
Counter = floor(Timestamp_UNIX / 30)
```

**Exemple** :
```
Timestamp = 1 787 820 509 (27 août 2026, ~16:48 UTC)
Counter   = 1 787 820 509 // 30 = 59 594 016
```

> 💡 *Toutes les personnes utilisant le même secret génèrent le **même code** pendant ces 30 secondes.*

Dans le code Python :
```python
counter = timestamp // 30
```

---

### Étape 3 : HMAC-SHA1 (la cryptographie)

**HMAC** = Hash-based Message Authentication Code

C'est une opération cryptographique qui "signe" le compteur avec le secret :

```
MAC = HMAC-SHA1(secret_key, counter_bytes)
```

Le compteur (un entier 64 bits) doit être encodé en **big-endian** sur 8 octets :

```
Counter = 59 594 016
En hex big-endian : 00 00 00 00 03 8F 6D 60
```

**Pourquoi SHA-1 ?** Le standard TOTP (RFC 6238) spécifie SHA-1 par défaut. Certains services utilisent SHA-256, mais la grande majorité reste en SHA-1.

Le résultat du HMAC est un hash de **20 octets** (160 bits) :

```
MAC = a3 7b 9c f2 11 4e 8d ... (20 octets au total)
```

Dans le code Python :
```python
counter_bytes = struct.pack(">Q", counter)  # Big-endian, 8 bytes
mac = hmac.new(secret, counter_bytes, hashlib.sha1).digest()
```

---

### Étape 4 : Troncature dynamique (Dynamic Truncation)

On a 20 octets de HMAC, mais on veut un code court. La **troncature dynamique** (RFC 4226) sélectionne 4 octets parmi les 20 de manière pseudo-aléatoire.

**Méthode** :
1. Prendre le **dernier octet** du MAC : `mac[19]`
2. En extraire les 4 bits de poids faible (masque `0x0F`) → ça donne un offset entre 0 et 15
3. Lire 4 octets consécutifs à partir de cet offset : `mac[offset:offset+4]`
4. Interpréter ces 4 octets comme un entier 32-bit **big-endian**
5. Effacer le bit de poids fort (masque `0x7FFFFFFF`) pour garantir un nombre positif

**Exemple** :
```
MAC (20 octets)   : [a3] [7b] [9c] ... [f2] [4e] [8d] [ab] [cd] [3f]
                    ^0   ^1   ^2        ^13  ^14  ^15  ^16  ^17  ^18  ^19

Dernier octet     : mac[19] = 0x3F
Offset            : 0x3F & 0x0F = 0x0F = 15
4 octets à offset : mac[15:19] = [cd] [3f] ? ?  → attention à ne pas dépasser 16 !

En pratique, l'offset max est 15, donc on lit mac[15:19] (4 octets)
```

> 💡 *La "troncature" est "dynamique" car l'offset change à chaque calcul (dépend du HMAC).*

Dans le code Python :
```python
offset = mac[-1] & 0x0F                              # Dernier octet, 4 bits faibles
code = struct.unpack(">I", mac[offset:offset + 4])[0]  # 4 octets → entier 32-bit
code = code & 0x7FFFFFFF                              # Bit de signe à 0
```

---

### Étape 5 : Réduction à 6 chiffres

On a un grand nombre (31 bits, donc jusqu'à 2 milliards). On le réduit à **6 chiffres** avec un modulo :

```
Code = nombre % 1_000_000
```

Puis on formate avec des zéros devant si nécessaire :

```
Si Code = 660314 → "660314"
Si Code = 42     → "000042"
```

Dans le code Python :
```python
code = code % (10 ** digits)          # digits = 6 par défaut
return f"{code:0{digits}d}"           # Zero-padding
```

---

## 4. Récapitulatif visuel

```
Secret Base32 ──────┐
                    ▼
            ┌───────────────┐
            │ Base32 Decode │
            └───────┬───────┘
                    │ Raw bytes (20 octets)
                    ▼
Timestamp ────┐    ┌─────────────┐
              └───►│ Counter =   │
                   │ timestamp//30│
                   └──────┬──────┘
                          │ 8 octets big-endian
                          ▼
                   ┌─────────────┐
                   │ HMAC-SHA1   │
                   └──────┬──────┘
                          │ 20 octets
                          ▼
                   ┌─────────────┐
                   │ Dynamic     │
                   │ Truncation  │
                   └──────┬──────┘
                          │ 4 octets → entier 31 bits
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

## 5. Fenêtre temporelle et tolérance

Le serveur (Google, GitHub) ne vérifie pas **un seul** code — il en vérifie **3** :

```
Code précédent (T-30s)  → accepté (synchronisation lente)
Code actuel    (T)      → accepté (normal)
Code suivant   (T+30s)  → accepté (horloge client en avance)
```

C'est pourquoi un **décalage de quelques secondes** entre ton device et le serveur ne pose pas problème. Mais un décalage de plusieurs minutes oui.

> ⏱️ *L'horloge est donc critique. Un RTC hardware (BM8563 sur le X4 Pro) est essentiel.*

---

## 6. Différence TOTP vs HOTP

| | **HOTP** | **TOTP** |
|---|---|---|
| **Compteur** | Incrémental (0, 1, 2, 3...) | Basé sur le temps (timestamp//30) |
| **Synchronisation** | Le serveur et le client doivent être synchronisés en compteur | Le serveur et le client doivent être synchronisés en heure |
| **Usage** | Clés YubiKey (appui physique) | Apps 2FA (Authy, Google Authenticator) |
| **RFC** | RFC 4226 | RFC 6238 (étend HOTP avec le temps) |

**TOTP = HOTP(secret, counter=timestamp//30)**

---

## 7. Vecteurs de test officiels (RFC 6238)

Pour valider une implémentation, la RFC fournit des vecteurs de test avec le secret `12345678901234567890` (20 octets) :

| Timestamp | Compteur | Code (8 chiffres) |
|-----------|----------|-------------------|
| 59 | 1 | `94287082` |
| 1 111 111 109 | 37 037 036 | `07081804` |
| 1 111 111 111 | 37 037 037 | `14050471` |
| 1 234 567 890 | 41 152 263 | `89005924` |
| 2 000 000 000 | 66 666 666 | `69279037` |
| 20 000 000 000 | 666 666 666 | `65353130` |

Le script Python `totp_reference.py` valide tous ces vecteurs au démarrage.

---

## 8. Correspondance Python ↔ C (ESP-IDF)

| Étape | Python (script référence) | C (ESP-IDF / mbedtls) |
|-------|---------------------------|----------------------|
| Base32 decode | `base64.b32decode()` | Fonction maison (~30 lignes) |
| Counter → bytes | `struct.pack(">Q", c)` | `uint8_t buf[8];` manuel shift |
| HMAC-SHA1 | `hmac.new(key, msg, sha1)` | `mbedtls_md_hmac(sha1, ...)` |
| Troncature | `mac[-1] & 0x0F` | Même logique bit à bit |
| Big-endian read | `struct.unpack(">I", ...)` | `(buf[0]<<24) \| (buf[1]<<16) ...` |
| Modulo | `% 1_000_000` | `% 1000000` |
| Format | `f"{code:06d}"` | `snprintf(buf, 7, "%06lu", code)` |

---

## 9. Sécurité : le secret est tout

**Le secret partagé est la seule chose qui protège ton compte.**

- Si quelqu'un obtient ton secret Base32 → il peut générer les mêmes codes que toi
- Le secret ne doit **jamais** transiter en clair sur le réseau (le QR code est affiché une seule fois)
- Sur ton X4 Pro, le secret sera chiffré en flash avec AES-256 — inaccessible sans le PIN maître

> 🔒 *Authy stocke tes secrets chiffrés dans le cloud de Twilio. Ton X4 Pro ne les stockera que localement, chiffrés, sans jamais les envoyer sur le réseau.*

---

## 10. Fichiers du projet

| Fichier | Rôle |
|---------|------|
| `totp_reference.py` | Implémentation Python de référence (validée RFC 6238) |
| `docs/totp-principle.md` | Ce document — explication du principe |
| `components/totp_engine/` | (Futur) Implémentation C pour ESP32 |

---

*Document rédigé le 2026-08-27 — à utiliser comme référence pendant le développement du firmware.*
