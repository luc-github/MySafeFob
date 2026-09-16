# FEATURES.md — MySafeFob (MSF) : firmware X4 Pro — TOTP + Password Manager

> **Statut** : v1.0 — ✅ **VALIDÉ 2026-09-13 (Phase 6)**
> **Règle** : une fois ce document validé, plus de nouvelles features jusqu'à la v1.0 (gate Phase 6).
> Référence : `docs/ROADMAP.md` (ADR-001 : sync temps par Wi-Fi).

---

## 1. Vision

Un device **air-gapped** de poche (XTEINK X4 Pro Developer Edition) qui remplace Authy pour
le TOTP et stocke les mots de passe, **sans jamais être connecté en usage normal**.
La surface de confiance est minimale : pas de credentials réseau persistés, pas d'OTA,
mises à jour uniquement par SD.

## 2. Cas d'usage principaux

1. **UC-1** : Ouvrir le device → déverrouiller par PIN → choisir un compte dans la liste
   TOTP → lire le code à 6 chiffres (+ décompte de validité) → le taper sur la machine cible.
2. **UC-2** : Chercher / afficher un mot de passe stocké pour le taper à la main.
3. **UC-2b** : En cas de problème de synchro TOTP, afficher un code de recovery
   du service concerné et le marquer utilisé.
4. **UC-3** : Provisionner un nouveau compte TOTP (saisie du secret Base32 via UI).
5. **UC-4** : Synchroniser l'heure (Wi-Fi ponctuel, ou saisie manuelle).
6. **UC-5** : Mettre à jour le firmware par carte SD.

## 3. Must Have (v1.0)

### F-01 — TOTP multi-comptes
- Liste de comptes (label + secret Base32 + digits [6/8] + période [30/60 s]).
- Ajout / modification / suppression d'une entrée via UI.
- Affichage « une entrée à la fois » : menu → sélection → écran code.
- Compte à rebours visuel de validité du code ; le code reste affiché après expiration
  (grisé/marqué) jusqu'au prochain affichage — usage réel = on lit puis on tape.
- Moteur déjà validé (Phase 3/4) : RFC 6238, HMAC-SHA1, référence Authy + vecteurs RFC.

### F-02 — Gestion du temps
- **Sync Wi-Fi sur demande uniquement** (ADR-001) : saisie des credentials à chaque session,
  jamais stockés ; radio coupée hors sync ; SNTP → `settimeofday`.
- **Saisie manuelle UI** (fallback permanent) : date + HH:MM lisibles, astuce de la
  frontière de minute (confirmation à :00 → précision ~1 s), décalage UTC en setting.
- **Indicateur de santé de l'heure** : âge de la dernière sync + drift RTC estimé affichés
  dans les réglages ; **alerte visuelle si l'âge dépasse 45 jours** (drift RTC
  ±20 ppm ≈ 1,7 s/jour, budget TOTP ±15 s).

### F-03 — Verrouillage PIN
- **PIN à 6 chiffres, longueur fixe (v1.0)** — arbitrage : le back-off protège du brute-force
  « online », la longueur protège du brute-force « offline » (extraction flash + KDF sur PC :
  10⁴ = trivial, 10⁶ = jours avec KDF lent).
- PIN à l'allumage ; verrouillage automatique après inactivité (délai réglable).
- N tentatives avec back-off exponentiel (anti brute-force online).
- Le contenu chiffré n'est jamais lisible sans déverrouillage.
- **Pavé PIN anti-traces** : layout non séquentiel (ou effacement de l'écran
  après saisie) pour contrer l'analyse des marques de doigt sur l'écran.

### F-04 — Stockage chiffré des secrets
- **Blob applicatif chiffré** dans une partition raw (pas de filesystem) — voir ADR-002.
- AES-256-GCM (mbedtls) ; clé dérivée du PIN par **Argon2id** (ADR-004 :
  memory-hard, m=8 MiB PSRAM, ~0,5-1 s/essai — PBKDF2-SHA256 écarté car
  parallelisable GPU/ASIC ; **confirmé Phase 5 : pas de secure element**).
- Sel aléatoire unique par device, stocké en clair (le sel n'est pas secret).
- Format du blob : `magic | version | sel | itérations | nonce | ciphertext | tag`,
  **versionné dès le départ** (règle 3 du projet) ; spec détaillée en Phase 7.
- TOTP secrets + mots de passe + recovery codes dans le même conteneur chiffré.

### F-05 — Gestion des mots de passe
- Entrées : label, identifiant, mot de passe, notes optionnelles.
- Affichage d'une entrée à la fois (écran sobre, mono, pas d'animation).
- Ajout / modification / suppression via UI.
- Pas de copier-coller possible (air-gapped) : l'affichage est conçu pour être tapé
  (font lisible, possibilité de révéler caractère par caractère).
- **Auto-clear** : retour automatique à un écran neutre après un délai sans
  interaction quand un secret est affiché (l'image e-paper persiste hors
  tension — un mot de passe affiché resterait visible sinon).

### F-05b — Gestion des codes de recovery
- Entrées : label (service) + liste de codes one-time fournis à l'activation 2FA.
- Stockés dans le même conteneur chiffré ; section UI dédiée.
- **Usage** : afficher un code à la fois, le marquer « utilisé » après usage réel
  (le code est alors barré mais reste visible en historique — traçabilité).
- Cas d'usage cible : problème de synchro TOTP, perte temporaire du device, ou
  service qui exige un recovery code pour reconfigurer le 2FA.
- Le TEST-PLAN Phase 2 a déjà établi la procédure GitHub de recovery avec ces codes —
  le device en devient le stockage de référence.

### F-06 — Mise à jour firmware par SD
- Pas d'OTA (décision projet). Binaire sur SD → vérification (checksum via
  `esp_ota_end`, cf. `INTERFACES.md` §3) → flash.
- **Tranché (ADR-007, amendé ADR-011)** : un seul slot app (`app0`) + la
  partition `factory` comme filet de sécurité — pas de rollback A/B esp_ota
  (pas d'OTA réseau, pas de scénario qui le justifie). La mise à jour passe
  systématiquement par la factory, pas par l'app en cours d'exécution.

### F-06b — Import/export des données via SD
- **Passphrase d'export dédiée** (ADR-005, mode transfer défaut) : la SD exportée
  est inutilisable sans elle. Option backup « même clé que le device ».
- **Format versionné** `x4pro-export-v1.bin` : magic | version | timestamp |
  entry_count | salt | nonce | AES-256-GCM(clear interne) | tag — écriture
  atomique (temp + rename), jamais d'écrasement, versioning par date.
- **Import** : parse + vérification tag AVANT de toucher au keystore courant,
  confirmation explicite et avertissement d'écrasement.
- Le blob reste chiffré pendant tout le transfert : la SD ne voit jamais les données
  en clair.
- La SD sert aussi pour la mise à jour firmware (F-06) ; elle n'est jamais montée
  en permanence.

### F-07 — Boot sécurisé et sobre
- Boot direct sur l'écran de déverrouillage ; zéro tâche réseau active au démarrage.
- UI monochrome sobre, un écran à la fois : l'écran est créé pour son contexte puis
  détruit pour libérer la mémoire (pas de pile d'écrans persistants).

### F-19 — Écran de veille (deep sleep)
- **Principe** : l'e-ink conserve la dernière image à consommation nulle → l'écran
  affiché avant l'entrée en deep sleep reste visible indéfiniment (comme le X4 Pro
  d'origine). C'est une surface d'UI gratuite, pas un luxe.
- **Contenu** :
  - indicateur d'état (device en veille / batterie),
  - carte des boutons physiques et leur fonction (left / right / power / home),
  - **option** contact propriétaire (téléphone ou email) en cas de perte —
    configurable dans Settings, **désactivé par défaut**.
- **Sécurité** : rien de secret à l'écran (évidemment), et le geste d'entrée
  factory (Power tenu ≥ 10 s, ADR-009 amendé 2026-09-16) **n'apparaît pas**
  sur cet écran — ce qui est peint dessus finit entre les mains de qui trouve
  le device. Seul le réveil (power court) est documenté visuellement ; le
  geste factory reste dans la doc.
- L'écran de veille est aussi l'écran d'accueil après un boot en mode principal.

### F-20 — Interpréteur de commandes série (permanent, pas un outil jetable)
- **Décision (2026-09-16)** : la console REPL (`esp_console`, USB-Serial/JTAG)
  démarrée dans `main.c` n'est **pas** un outil de debug temporaire voué à
  disparaître une fois l'UI tactile (F-06/tâche 8.4) livrée — elle reste en
  place en continu, pour lancer des commandes et vérifier des statuts sans
  passer par l'écran (diagnostic, support, tests). Un interpréteur de
  commandes reste utile même une fois le produit fini.
- **Menace** : accessible uniquement par connexion USB physique — même
  niveau de confiance que la mise à jour firmware par SD (F-06) ou le
  recovery factory (ADR-007/009) : nécessite déjà un accès physique au
  device. N'expose aucun secret par défaut (pas de commande de dump des
  credentials tant que F-03/F-04 ne sont pas en place).
- **Portée actuelle** (squelette Phase 8) : `help`, `about`,
  `totpselftest`, `sleep`. À étoffer au fil des phases (ex. statut
  batterie/RTC, diagnostic stockage) plutôt que remplacé.
- **⚠️ Piège rencontré (2026-09-16), point de contrôle pour la suite** :
  `esp_console_new_repl_usb_serial_jtag()`/`_uart()` créent bien la tâche
  REPL, mais celle-ci reste parquée à l'état `CONSOLE_REPL_STATE_INIT`
  (aucune commande traitée, bannière/logs quand même visibles car
  indépendants de cette tâche) tant que **`esp_console_start_repl(repl)`**
  n'est pas appelé explicitement pour la faire passer à
  `CONSOLE_REPL_STATE_START` (`esp_console_common.c`, IDF 5.5.5). Piège
  facile à rater précisément parce que le symptôme est trompeur : tout
  semble fonctionner (prompt affiché, logs qui défilent), seule la saisie
  ne fait jamais rien. Repéré par comparaison avec
  `references/test_apps/x4pro-probe/main/main.c`, qui l'appelle bien. **À
  chaque nouvel usage d'`esp_console_new_repl_*()` dans ce projet,
  vérifier que `esp_console_start_repl()` suit bien l'appel.**

## 4. Should Have (si le temps/budget mémoire le permet)

| # | Feature | Notes |
|---|---------|-------|
| F-08 | Générateur de mots de passe | Accessoire (confirmé utilisateur) : longueur, jeux de caractères |
| F-10 | HOTP (compteur) | Support RFC 4226 pour les comptes rares |
| F-11 | Recherche/filtrage dans les entrées | Utile au-delà de ~15 comptes |
| F-12 | Catégories ou tags | TOTP / passwords / perso / pro |
| F-16 | Indicateur batterie (CW2017) + frontlight dans la barre d'état | Drivers déjà validés Phase 5. Frontlight **éteint par défaut**, allumage manuel uniquement (trigger exact TBD Phase 8), auto-off réglable (défaut 30 s) |

## 5. Nice to Have (post-v1.0)

| # | Feature | Notes |
|---|---------|-------|
| F-13 | PIN de duress (contenu factice) | Threat model avancé |
| F-14 | Passphrase utilisateur (vs PIN numérique) | Clé plus forte, saisie lente sur e-ink |
| F-15 | Support SHA-256/SHA-512 TOTP | Rarement requis par les services |
| F-17 | **Bridge USB-MSC read-only** : la SD exposée en lecteur USB (TinyUSB, write-protect) → vault Cryptamator officiel lu/déchiffré par le PC/téléphone hôte (Android OTG ; iOS limité) | Lecture seule ; zéro crypto à réimplémenter ; le device devient un coffre transporteur |
| F-18 | Viewer e-ink maison pour documents chiffrés | Optionnel — seulement si le besoin « lire sans PC » émerge ; vault maison non compatible Cryptamator, images seulement |

## 6. Non-goals explicites (hors scope v1.0)

- **OTA / mises à jour réseau** — SD uniquement.
- **Connectivité en usage normal** — Wi-Fi radio éteinte hors sync temps.
- **Stockage des credentials Wi-Fi** — ADR-001.
- **Copier-coller / pont USB vers la machine cible** — l'usage est « lire et taper ».
- **Lecture de QR code / import par caméra** — pas de caméra sur le X4 Pro (confirmé presse) ;
  saisie manuelle Base32 uniquement pour le provisionning.
- **Authy-style cloud backup** — backup = SD chiffrée (F-06b).
- **Multi-utilisateur** — device personnel à un utilisateur.
- **Animations/transparence UI** — monochrome sobre, pas d'animation (décision utilisateur).

## 7. Contraintes techniques (confirmées Phase 5 — 2026-09-13)

| Contrainte | Impact |
|------------|--------|
| Flash = seul stockage persistant, SD montée uniquement à la demande | La SD sert de : (1) zone de mise à jour firmware, (2) export/import backup, (3) vault documents Cryptomator (F-17, bridge USB-MSC filaire read-only). Jamais montée en permanence |
| ESP-IDF 5.5.x (pas 6.x : stabilité + empreinte) | Cible de build fixée ; attention renommage composants (`console` vs futur `esp_console`) |
| **FreeInkUI** (porté ESP-IDF natif depuis `freeink-sdk`, MIT — ADR-010, remplace LVGL) | UI à concevoir sobre dès le départ ; immediate-mode, dessine directement dans notre framebuffer `eink.c` via `DisplayTarget` ; navigation par `Frame`/`InteractionBuffer` (focus + confirm), validée sur nos `buttons.c`/`touch.c` (POC `references/test_apps/freeinkui-poc/`) |
| **Typographie e-ink 3.7"** (confirmé utilisateur 2026-09-14) | 8×16 = illisible ; **minimum 16×32 px** (factory + splash validés à cette taille). L'UI doit partir sur des polices ≥ 20-24 px équivalent, à adapter par densité de panneau |
| **Pas de secure element** (confirmé Phase 5) | La barrière offline = KDF Argon2id memory-hard (F-03/F-04, ADR-004) ; ne jamais stocker de clé en clair |
| **RTC BM8563 avec backup validé** (survit aux reboots) | F-02 reste requis pour sync initiale/resync, mais l'heure persiste entre les usages |
| **E-Ink UC8279 : fb natif 800×480 paysage, rotation 90° CW matérielle** | fb à dessiner en paysage natif, stream brut ; jamais de PSR REG=1 au DRF (freeze) — cf. `hardware-specs.md` |
| **Wi-Fi/NVS** : esp_wifi stocke les credentials en NVS par défaut | `CONFIG_ESP_WIFI_NVS_ENABLED=n` + `WIFI_STORAGE_RAM` + wipe buffers (critère d'acceptation 2) |
| **KDF Argon2id** (ADR-004) | Portage libsodium ou monocypher dans `src/components/` ; paramètres calibrés ~0,5-1 s sur S3 |
| Hardware X4 Pro : **bring-up terminé** (E-Ink, GT911, RTC, CW2017, SD, frontlight, boutons) | Référence définitive : `hardware-specs.md` — drivers à porter tels quels |

## 8. Critères d'acceptation globaux (v1.0)

1. Les codes TOTP du device valident sur 3 services réels distincts (GitHub + 2 autres).
2. Aucune trace de credentials Wi-Fi en flash après une sync (audit binaire,
   incluant la NVS — `CONFIG_ESP_WIFI_NVS_ENABLED=n`).
3. La DB chiffrée résiste à une extraction flash brute (pas de clé en clair).
4. Mise à jour par SD validée avec récupération via factory en cas d'échec (ADR-011, Phase 9).
5. Autonomie ≥ 2 semaines d'usage réel (test Phase 9).
6. L'écran de veille (F-19) n'expose aucune donnée sensible et ne documente
   pas le geste factory.

---

*Document à valider : toute modification après validation = revue formelle.*
