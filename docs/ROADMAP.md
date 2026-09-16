# ROADMAP — X4 Pro TOTP + Password Manager Firmware

> **Version** : 1.0  
> **Date** : 2026-08-30  
> **Statut** : Phase 1 — Documentation et préparation

---

## Philosophie du projet

**Pas de code sans document. Pas de validation sans critère. Pas de feature sans test.**

Chaque phase produit un livrable documenté et validé avant passage à la suivante.

---

## Phases

### Phase 1 : Génération des documents (EN COURS)

| # | Livrable | Statut | Description |
|---|----------|--------|-------------|
| 1.1 | `ROADMAP.md` | ✅ | Ce document — plan macro |
| 1.2 | `TEST-PLAN.md` | 🔄 | Stratégie de test, service cible, critères de validation |
| 1.3 | `SPECS.md` | ✅ | Spécifications hardware (X4 Pro, pins, périphériques) |
| 1.4 | `TOTP-ALGO.md` | ✅ | Principe TOTP, RFC 6238, implémentation de référence |
| 1.5 | `ARCHITECTURE.md` | ⏳ | Architecture logicielle (composants, interfaces, flux de données) |

**Gates de sortie** : Tous les documents rédigés et revus.

---

### Phase 2 : Service de test TOTP

| # | Livrable | Description |
|---|----------|-------------|
| 2.1 | Compte de test créé | Service avec 2FA TOTP activé, codes de backup sauvegardés |
| 2.2 | Procédure de recovery | Documentée — comment récupérer si lockout |
| 2.3 | Procédure de reset | Comment supprimer/recréer le compte en cas de problème |

**Gates de sortie** : On peut générer un code TOTP, le valider sur le service, et récupérer le compte si échec.

---

### Phase 3 : Validation TOTP Python

| # | Livrable | Description |
|---|----------|-------------|
| 3.1 | `totp_reference.py` validé | Script Python génère codes identiques au service de test |
| 3.2 | Rapport de validation | N codes consécutifs validés, drift mesuré, edge cases testés |
| 3.3 | Secret exporté en C | Tableau `uint8_t[]` prêt pour le firmware |

**Gates de sortie** : 10 codes consécutifs sur 5 minutes validés avec succès contre le service.

---

### Phase 4 : Validation TOTP ESP-IDF (C)

| # | Livrable | Description |
|---|----------|-------------|
| 4.1 | `test_totp/` compilé et flashé | Projet ESP-IDF minimal, interaction serial uniquement |
| 4.2 | RFC 6238 vectors passent | Self-test au boot valide les 6 vecteurs officiels |
| 4.3 | Validation contre service | Mêmes 10 codes que Phase 3, générés sur ESP32 |

**Gates de sortie** : Le code généré par l'ESP32 match le service de test et le script Python.

---

### Phase 5 : Bring-up hardware X4 Pro

| # | Livrable | Description |
|---|----------|-------------|
| 5.1 | `hw_probe` flashé et exécuté | Identifie SoC, RAM, flash, I2C devices, E-Ink controller |
| 5.2 | Pinout validé | Document mis à jour avec les pins réellement mesurées |
| 5.3 | Drivers stubs remplacés | Chaque driver testé individuellement (E-Ink, touch, RTC, buttons) |

**Gates de sortie** : Tous les périphériques reconnus, drivers basiques fonctionnels.

---

### Phase 6 : Revue des specs et features

| # | Livrable | Description |
|---|----------|-------------|
| 6.1 | `FEATURES.md` | Liste finalisée des features (must have / should have / nice to have) |
| 6.2 | `UI-SPECS.md` | Maquettes textuelles de chaque écran |
| 6.3 | `SECURITY-THREAT-MODEL.md` | Scénarios d'attaque et mitigations |

**Gates de sortie** : Le scope du projet est figé. Plus de nouvelles features jusqu'à la v1.0.

---

### Phase 7 : Définition des interfaces

| # | Livrable | Description |
|---|----------|-------------|
| 7.1 | API internes documentées | Headers + contrats de chaque composant |
| 7.2 | Protocole de stockage | Format binaire/JSON de la DB chiffrée, versionné |
| 7.3 | Protocole de mise à jour | Procédure flash via SD (format fichier, vérification) |

**Gates de sortie** : On peut écrire le code de chaque composant sans se poser de questions d'interface.

---

### Phase 8 : Développement itératif

| # | Livrable | Description |
|---|----------|-------------|
| 8.1 | TOTP Engine complet | Génération de codes, menu liste services, affichage timer |
| 8.2 | Sécurité (PIN, chiffrement) | Dérivation clé, stockage chiffré, verrouillage |
| 8.3 | Password Manager v1 | CRUD basique, génération de mots de passe |
| 8.4 | UI/UX | Navigation tactile, clavier virtuel, thèmes |

**Gates de sortie** : Chaque sous-phase testée et validée individuellement.

---

### Phase 9 : Intégration et tests

| # | Livrable | Description |
|---|----------|-------------|
| 9.1 | Tests de endurance | Batterie, drift RTC sur 1 semaine, stress TOTP |
| 9.2 | Tests de sécurité | Tentatives de brute-force PIN, corruption flash, recovery |
| 9.3 | Tests d'usage réel | Utilisation quotidienne pendant 1 semaine minimum |

**Gates de sortie** : Tous les tests passent. Le device est utilisable en production.

---

### Phase 10 : Release

| # | Livrable | Description |
|---|----------|-------------|
| 10.1 | Firmware v1.0 signé/taggué | Release GitHub avec binaire et checksum |
| 10.2 | Documentation utilisateur | Guide d'utilisation, procédure de backup/recovery |
| 10.3 | Documentation développeur | Comment flasher, comment contribuer, comment porter |

---

## Définitions

- **Gate de sortie** : Condition qui doit être remplie avant de passer à la phase suivante. Pas de contournement.
- **Livrable** : Document ou artefact produit, versionné, et revu.
- **Validation** : Preuve concrète que le livrable fonctionne comme attendu (logs, captures, comparaisons).

---

## Décisions de design

### ADR-001 : Sync de l'heure par Wi-Fi (NTP/SNTP), radio coupée en usage normal

**Contexte** : Le TOTP exige une horloge précise (tolérance typique ±30 s). Le X4 Pro n'a pas de RTC sauvegardée par batterie, et l'appareil est air-gapped par design (règle 5). La saisie manuelle du timestamp (testée en Phase 4) introduit un offset de plusieurs secondes et est source d'erreurs.

**Décision** : Synchronisation de l'heure via SNTP sur Wi-Fi, **uniquement pendant le provisioning ou sur demande explicite de l'utilisateur**. En dehors des sessions de sync, la radio Wi-Fi est désactivée (`esp_wifi_deinit` + netif détruit, ou équivalent).

**Justification** :
- L'usage quotidien reste air-gapped ; la radio est un point d'attaque qui n'existe que quelques secondes par sync.
- Le NTP ne transporte aucun secret : le pire scénario (serveur NTP hostile/spoofé) décale l'heure → les codes deviennent temporairement invalides (DoS), mais aucune clé ne peut être extraite ni de code forgé. Toléré par le threat model.
- La dérive RTC entre syncs (~1 s/jour) reste largement dans la fenêtre de validité.

**Alternatives écartées** :
- Saisie manuelle de l'heure : imprécise (offset copier-coller), validée comme solution de secours uniquement.
- Module GPS : hardware supplémentaire, coût, encombrement.
- Wi-Fi permanent : viole la règle 5 et multiplie la surface d'attaque.

**Questions ouvertes** (à trancher en Phase 6/7) :
- ~~Stocker les credentials Wi-Fi pour resync rapide, ou les redemander à chaque sync ?~~ **Tranché** : jamais stockés — saisis à chaque session de sync, oubliés ensuite. Aucune auto-connexion ; la radio est coupée en dehors des sessions de sync.
- ~~Fréquence de resync~~ : sur action utilisateur uniquement (compte à rebours de dérive RTC affiché pour l'informer).
- ~~Fallback sans réseau ?~~ **Tranché** : saisie manuelle toujours disponible, via **UI avec heure/date lisible par un humain (HH:MM + date), pas un timestamp Unix**. Détails UX en Phase 6 (`UI-SPECS.md`) : entrée HH:MM + date, avec astuce de sync sur frontière de minute (l'utilisateur confirme quand son horloge de référence passe à :00 → précision ~1 s) ; la conversion en epoch UTC et la gestion du fuseau sont internes.

---

### ADR-002 : Base chiffrée (blob applicatif), pas de partition chiffrée

**Contexte** : F-04 exige un stockage chiffré avec clé dérivée du PIN. Deux options : partition chiffrée (flash encryption matériel eFuse, ou couche bloc logicielle) vs base chiffrée applicative.

**Décision** : **Blob applicatif chiffré** dans une partition raw : AES-256-GCM (mbedtls), clé = PBKDF2-HMAC-SHA256(PIN, sel). Chiffrement/déchiffrement du blob entier en RAM à chaque mutation. Pas de filesystem.

**Justification** :
- Le flash encryption matériel a une clé fixe par device (eFuse) : ne dépend pas du PIN → incompatible avec le modèle « verrouillé par PIN ». De plus eFuse/secure boot = état inconnu sur le X4 Pro Developer Edition, dépendance à éviter.
- Une couche bloc chiffrée logicielle revient à réimplémenter filesystem + wear-leveling : effort disproportionné.
- Données au format : quelques Ko → blob entier en RAM, aucun cas limite. Changement de PIN = re-chiffrage du blob.

**Hardening post-v1.0 possible** : ajouter le flash encryption matériel en couche complémentaire (protection cold-boot à l'arrêt), sans remplacer la couche PIN.

---

### ADR-003 : Mise à jour SD ou serial uniquement ; pas de secure boot avant la prerelease finale ; pas d'ATECC608A

**Contexte** : le design doc externe (§8, §10) proposait OTA réseau
(`esp_https_ota`), Secure Boot v2 + Flash Encryption « planned » dès le début,
et un secure element ATECC608A (I2C) en option.

**Décision** (validée utilisateur 2026-09-13) :
1. **Mise à jour firmware : SD ou serial (UART) uniquement — jamais par le
   réseau.** L'OTA Wi-Fi est exclu du périmètre (alpha → v1.0).
2. **Secure Boot v2 / Flash Encryption : pas en alpha, pas en beta — envisageable
   uniquement pour la prerelease finale** (eFuse = irréversible, casserait le
   workflow de flash UART pendant le dev ; le blob applicatif chiffré couvre
   déjà la protection des données au repos).
3. **Pas d'ATECC608A prévu** — pas de secure element ; la barrière offline
   reste le KDF lent (voir ADR-004).

**Justification** : le modèle de menace v1 suppose un attaquant sans accès
physique prolongé ; le PIN protège les données, pas le firmware. La surface
d'attaque réseau est nulle par construction (aucun listener, aucune mise à
jour réseau).

---

### ADR-004 (proposé) : KDF = Argon2id au lieu de PBKDF2-HMAC-SHA256

**Contexte** : PBKDF2-HMAC-SHA256 se parallelise très bien sur GPU/ASIC
(SHA256 non memory-hard) : l'espace d'un PIN 6 chiffres (~10⁶) tombe en
~1-2 jours sur GPU grand public avec des itérations « 250 ms ». Le design doc
externe recommande Argon2id, faisable grâce aux 8 Mo de PSRAM.

**Décision proposée** : clé = `Argon2id(PIN, sel, m=8 Mo, t=4, p=1)`
(paramètres à calibrer pour ~0,5-1 s/essai sur le S3), via portage
libsodium/monocypher. Mise à jour d'INTERFACES.md §1.1 en conséquence.

**Statut** : ✅ VALIDÉ 2026-09-13 (remplace PBKDF2 dans INTERFACES.md §1.1
et FEATURES.md F-04).

---

### ADR-005 (proposé) : Export SD avec passphrase dédiée + format versionné

**Contexte** : F-06b exporte le blob avec la même clé PIN → la SD volée seule
(compromission sans le device) ne sert à rien, mais SD + device volés = tout
perdu. Le design doc externe propose une passphrase d'export séparée et un
format de fichier structuré.

**Décision proposée** :
- Fichier `/x4pro-export-v1.bin` : `magic(8B) | version(2B) | timestamp(8B) |
  entry_count(4B) | salt(16B) | nonce(12B) | Argon2id(passphrase_export) →
  AES-256-GCM(clear interne) | tag(16B)`.
- Écriture atomique (temp + rename), jamais d'écrasement
  (`export-YYYY-MM-DD.enc`).
- Import : parse + vérif tag **avant** de toucher au keystore courant,
  tailles bornées. Distinct de l'update firmware (ADR-003).
- Le backup « mode PIN » (même clé que le device) reste possible mais n'est
  pas le défaut.

**Statut** : ✅ VALIDÉ 2026-09-13 (INTERFACES.md §3 et FEATURES.md F-06b
mis à jour en conséquence).

---

### ADR-006 (proposé) : Sync temps par « terminal de confiance », hiérarchie BLE → serial → Wi-Fi

**Contexte** : ADR-001 fixe la sync Wi-Fi SNTP manuelle. Le design doc externe
(§4.3, §5, §6) propose de faire du téléphone un *terminal de confiance* :
une balise d'heure remplaçable, jamais un pair de données.

**Décision proposée** :
1. **Le terminal de confiance n'est trusted que pour l'heure.** Aucune donnée
   sensible ne transite par aucun canal radio/serial — jamais chiffrée, jamais
   en clair. Le keystore ne bouge que par SD (ADR-005).
2. **Trois canaux, tous déclenchés manuellement** (jamais de sync automatique) :
   - **BLE CTS** (service 0x1805 / char 0x2A2B) : le X4 Pro est GATT *client*
     (NimBLE), scan → connect → read → disconnect, ~3-5 s, sans pairing.
     Précision ~1 s, suffisante pour des fenêtres TOTP de 30 s.
   - **Serial/USB** : le PC envoie l'heure par le port série (mécanisme déjà
     validé dans le probe, commande `st`). Précision exacte, zéro infra.
   - **Wi-Fi SNTP** (ADR-001) : fallback quand aucun terminal n'est disponible.
3. **Validation précoce Phase 8** : test BLE CTS end-to-end en utilisant
   l'Advertiser de nRF Connect pour émettre le service CTS — aucune app
   compagnon à écrire pour valider. La question « Android expose-t-il CTS
   nativement » est tranchée par ce test ; sinon app compagnon minimal en v2.

**Justification** : réduit la surface d'exposition (pas de credentials réseau
dans le chemin nominal), précision compatible TOTP, cohérent avec le modèle
air-gapped. Le drift RTC (±20 ppm ≈ 1,7 s/jour) impose une sync tous les
30-45 jours ; l'UI affiche l'âge de la dernière sync et alerte à 45 jours.

**Statut** : en attente de validation utilisateur.

---

### ADR-007 : Partition factory de recovery + bootloader hook (portage PiBot)

**Contexte** : avec le dual-slot esp_ota seul, un device dont les deux slots
sont corrompus exige un PC + câble pour reflash. L'utilisateur dispose d'un
bootloader hook éprouvé (projet PiBot) : sur appui bouton au boot,
`bootloader_after_init` sauvegarde l'otadata (offset libre 0xB000, magic
0xAA55AA55), l'efface et resette → le bootloader standard boote la partition
`factory`. Trigger logicique identique depuis l'app
(`esp_ota_set_boot_partition(factory)`).

**Décision (validée 2026-09-13)** :
1. Table des partitions mise à jour (INTERFACES.md §2) : ajout `factory` 1 Mo,
   `CONFIG_PARTITION_TABLE_OFFSET=0xC000`, backup otadata @0xB000.
2. Le recovery courant reste le dual-slot (update in-app vers slot inactif +
   rollback) ; la factory est le **secours sans PC** (flash depuis SD ou serial).
3. La factory app réutilise la logique PiBot (SD → `esp_ota_write`, restore
   otadata) ; son UI est portée sur le driver e-ink UC8279 (sobre, texte).
4. **Portage à valider sur IDF 5.5.5** (le code d'origine tourne en 5.4.3) :
   nom exact de la config hooks, signatures `esp_rom_spiflash_*` S3, GPIO de
   trigger non-strapping (éviter GPIO0/3/45/46), taille bootloader S3.

**Documentation technique** : `FACTORY.md` (portage détaillé PiBot → MySafeFob,
table des différences, mécanisme otadata, points de validation 5.5.5).

---

### ADR-008 : Squelette board-agnostic, board réel `x4pro` + stub `m5paper_mono`

**Contexte** : le projet cible en premier le Xteink X4 Pro (déjà entre les
mains, bring-up complet Phase 5), mais le M5Stack M5PaperMono (ESP32-S3R8,
e-paper SSD1677 480x800 natif portrait, touch FT6336G) est une board portuaire
probable à terme (hors stock au moment de la décision). Les deux convergent
vers une UI 480x800 portrait.

**Décision (validée 2026-09-13)** :
1. Le squelette `src/` est board-agnostic : le board se sélectionne par
   `-DMSF_BOARD=x4pro` (défaut), qui inclut `boards/${MSF_BOARD}/board_config.cmake`.
   Pattern inspiré du PiBot, simplifié (pas de matrice de variants).
2. `boards/x4pro/` : configuration réelle et validée sur hardware (pins,
   drivers UC8279/GT911/BM8563/CW2017, e-ink en rotation 90° CW mode 0).
3. `boards/m5paper_mono/` : **stub documentation uniquement** (README specs) —
   aucun code avant d'avoir le hardware en main. Le probe `x4pro-probe`
   contient déjà un chemin driver SSD1677 (busy=HIGH) qui servira de base.
4. Ce qui est board-specific (pins, init écran/touch, batterie) vit sous
   `boards/<name>/` ; le code applicatif (TOTP, store, UI) reste indépendant
   du board derrière l'interface `board_config.h`.

**Statut** : ✅ VALIDÉ 2026-09-13.

---

### ADR-009 : On/Off = deep sleep + wake-up GPIO, le device vit "éteint" entre deux usages

> 📋 **Post-mortem complet de la session 2026-09-16** (7 bugs distincts,
> chronologie, leçons apprises) : `docs/postmortem-power-factory-2026-09-16.md`.
> Les amendements ci-dessous restent la référence technique détaillée ;
> le post-mortem raconte l'enchaînement et le *pourquoi*.

**Contexte** : le bouton Power du X4 Pro (GPIO3, actif-LOW) est une simple
entrée GPIO — il ne coupe pas l'alimentation. Il faut donc un mécanisme
dédié pour offrir la fonction on/off attendue, en minimisant la consommation
(la batterie fait ~2300 mAh et l'usage cible est un porte-clés laissé
éteint la plupart du temps).

**Décision proposée** :
1. **Mise en veille logicielle** (`power_mgr_shutdown()`) : `eink_power_off()`
   (l'image reste affichée — e-ink bistable), frontlight off, rails
   optionnels coupés (touch GPIO2 HIGH, SD GPIO5 HIGH, hold RTC), puis
   `esp_deep_sleep_start()` avec **wake-up sur GPIO3 LOW**
   (`esp_deep_sleep_enable_gpio_wakeup`). GPIO3 est un RTC GPIO (0-21) :
   wake-up valide en deep sleep, pull-up RTC maintenu pendant le sommeil.
2. **Réveil** : appui Power → reboot S3 → `esp_sleep_get_wakeup_cause()`
   → wake-up GPIO ⇒ saut direct à l'écran UNLOCK (pas de splash).
   L'heure TOTP survit via le RTC BM8563 (backup batterie).
3. **Conséquence d'architecture** : le device est *presque toujours éteint*
   entre deux utilisations, comme la liseuse d'origine. Les codes TOTP sont
   calculés **à la demande au réveil** — aucune tâche en fond, aucun tick.
   La barre des 30 s ne tourne qu'écran allumé.
4. **Mesure à planifier (tâche 8c)** : conso réelle en deep sleep avec rails
   tenus par `rtc_gpio_hold_en` — valide (ou non) la stratégie de coupure
   des rails GPIO1/2/5 pendant le sommeil.
5. **Factory** : inchangée pour l'instant (Power y reste le select de
   secours ; la mise en veille y est optionnelle, l'écran bistable tient
   le menu affiché sans consommer).

**Alternatives écartées** :
- Light sleep : conso plus élevée, aucun bénéfice ici (rien ne tourne en
  fond par design).
- Power latch hardware : n'existe pas sur cette board (pas de PMIC).

**Sémantique des boutons (validée en discussion 2026-09-13, amendée 2026-09-13)** :

| État | Geste | Effet |
|---|---|---|
| Éteint (deep sleep) | **Power court** (Right relâché) | Réveil → app, écran UNLOCK |
| Éteint | **Power + Right maintenus** | Réveil → **factory** (détection combo : GPIO7 LOW dans les 1res ms du boot) |
| Éteint | Right seul | Rien — Right n'est pas une source de wake |
| Éteint | Right (GPIO7) maintenu au boot **USB** (cold boot) | Factory via hook bootloader (cas dev/reflash) |
| Factory active | Power long | Retour deep sleep (otadata restauré pointe vers app0 → prochain réveil court = app) |
| App active | Power long | Deep sleep |

**Mécanisme du combo** : la source de wake est Power (GPIO3) uniquement ;
au boot, le hook bootloader (`hooks.c`) échantillonne GPIO7 ~125 ms après
le début du boot (100 ms stabilisation pull-up + debounce) — maintenu LOW =
combo délibéré, non-strapping donc sans danger au reset.
**Symétrie** : Right est le bouton recovery partout — seul au boot USB
(hook bootloader), en combo avec Power depuis le deep sleep.

**Seuil de confirmation (amendé 2026-09-16)** : une fois Right détecté
pressé, il doit rester tenu en continu **3 s** pour que la bascule vers
factory se déclenche — automatiquement au franchissement du seuil, sans
avoir besoin de relâcher à un instant précis. Relâché avant 3 s = annulé,
boot normal. *Corrige un bug UX* : la version précédente exigeait au
contraire de **relâcher** Right dans les 5 s suivant sa détection, sous
peine d'annulation (« maintenu trop longtemps ») — mais rien pendant le
boot ne permet de savoir quand cette fenêtre commence, rendant un
relâchement précis impossible à viser en usage réel. Le nouveau mécanisme
(tenir jusqu'au seuil, pas de fenêtre de relâchement à deviner) est plus
robuste et reprend le pattern déjà utilisé pour le long-press Power de
l'app (`MSF_POWER_LONG_MS`, `main.c`).

**Contraintes matérielles documentées** :
- **Left+Power IMPOSSIBLE** : GPIO0 est un strapping pin — maintenu LOW au
  reset (sortie de deep sleep = reset complet), le S3 démarre en mode
  download UART. D'où le combo sur Right (GPIO7 = RTC_GPIO7, non-strapping).
- Le réveil deep sleep n'accepte que des RTC GPIO (0-21) : Power (GPIO3)
  est le seul bouton pouvant réveiller seul ; Right (RTC_GPIO7) peut en
  faire partie mais seul il ne réveille pas (hors masque de wake).
- **Point de validation 8c** : GPIO3 LOW au reset modifie le strapping
  « JTAG source » — vérifier que la console USB reste utilisable après un
  réveil par bouton (applicable à tout réveil, combo ou simple).

**Feedback appui Power (pattern à reprendre en 8c, validé factory 2026-09-15)** :
pas de buzzer (ADR-007) ni de retour audio — sur e-ink, l'utilisateur ne sait
pas quand un appui est « pris en compte » et peut relâcher, surtout avec des
gestes qui dépendent d'une durée (court/long/combo). Solution appliquée dans
la factory : dès la détection brute du GPIO Power (avant même le debounce),
déclencher le refresh du splash — le fait que l'écran **commence** à flasher
sert de signal « relâche, c'est bon », sans attendre la fin du refresh
(`boards/x4pro/factory/main/main.c`, boucle principale). Ce pattern devra
être repris pour les gestes app (court=wake/cancel, long=deep sleep,
Power+Right=combo factory), avec un signal supplémentaire à envisager au
franchissement du seuil long-press (distinguer « relâche pour court » de
« continue pour long »).

**Amendement 2026-09-16 — abandon du combo Right, bascule Power seul (durée)** :

Le combo Power+Right décrit ci-dessus (sections précédentes de cet ADR) est
**abandonné**, remplacé par une mesure de durée sur Power seul :

| État de départ | Geste | Effet |
|---|---|---|
| App éveillée | Power tenu **< 10 s** puis relâché | Deep sleep (comportement existant, inchangé) |
| App éveillée | Power tenu **≥ 10 s** | Bascule factory — `power_mgr_switch_to_factory()` (logiciel : backup otadata @0xB000, erase otadata, `esp_restart()`) |
| Éteint (deep sleep) | Power tenu **< 10 s** puis relâché | Réveil → app, écran UNLOCK (inchangé) |
| Éteint (deep sleep) | Power tenu **≥ 10 s** en continu depuis le réveil | Bascule factory — géré **côté hook bootloader** (`hooks.c`), indépendamment de l'état de l'app (voir amendement suivant, fix 2026-09-16) |
| Factory active | Power long | Retour deep sleep (inchangé) |

**Root cause de l'abandon** : isolé empiriquement sur hardware (2026-09-16) via
deux tests dédiés — Test A (Power seul, deep sleep → tenir Power) réveille
systématiquement le device ; Test B (Power+Right tenus ensemble dès le départ,
10 s, puis relâchés) **ne réveille JAMAIS** le device, aucun log, aucune
réaction, quelle que soit la durée de maintien. Le combo empêche le réveil
matériel lui-même de se déclencher — en amont de tout hook bootloader ou
timing logiciel, donc non réparable côté firmware sans changer le geste.

**Conséquences** :
- `hooks.c` : `RECOVERY_BUTTON_PIN` passe de `GPIO_NUM_7` (Right) à
  `GPIO_NUM_3` (Power) ; `CONFIRM_HOLD_US` passe de 3 s à **10 s** (aligné
  sur le seuil logiciel côté app, même geste quel que soit l'état de départ).
- `power_mgr.h`/`power_mgr.c` : nouvelle fonction `power_mgr_switch_to_factory()`
  côté app éveillée (contrat de backup identique à `hooks.c`, mais via
  `esp_flash_*` — contexte app, pas bootloader ROM).
- `main.c` (`power_button_task`) : threshold unique remplacé par deux seuils
  sur le même bouton (1,5 s veille / 10 s factory) ; toute référence à
  `MSF_BTN_RIGHT_PIN`/GPIO7 supprimée de ce chemin.
- Le hook bootloader n'a qu'un seul trigger (`RECOVERY_BUTTON_PIN`) : le
  changer pour Power migre aussi le cas **cold-boot USB** (dev/reflash) —
  il se déclenche désormais en tenant Power (10 s) au branchement USB, plus
  Right. Cohérent avec « un seul geste, Power, quel que soit le contexte ».
- Left+Power reste impossible (GPIO0 strapping, inchangé).

**Amendement 2026-09-16 (suite) — 2e bug matériel : lire Power en brut au
bootloader casse le réveil lui-même** :

Premier essai de l'implémentation ci-dessus : `hooks.c` lisait
`RECOVERY_BUTTON_PIN` (désormais GPIO3) exactement comme il le faisait pour
Right — sans tenir compte du fait que GPIO3 est *aussi* le pin de réveil
EXT1. Résultat observé sur hardware : **plus aucun réveil ni bascule
factory possible** depuis la veille (« une fois en veille pas de réveil et
pas de factory »).

**Root cause** : `esp_sleep_enable_ext1_wakeup()` configure GPIO3 via le
domaine RTC_IO pour agir comme source de réveil ; cette configuration
persiste au travers du sommeil et du réveil (le domaine RTC reste
alimenté). Juste après un réveil EXT1, au stade `bootloader_after_init()`
(bien avant tout code applicatif), le pad est donc encore routé par le
domaine RTC — `esp_rom_gpio_pad_select_gpio()` + `gpio_ll_get_level()`
(lecture GPIO digitale classique, déjà utilisée sans souci pour Right qui
n'est jamais un pin de réveil) ne reflètent alors plus l'état réel du
bouton. La boucle de confirmation du hook se retrouve à lire une valeur
figée, indépendante de ce que fait réellement l'utilisateur — d'où
l'absence totale de réaction observée.

**1er correctif (abandonné)** : renvoyer immédiatement sur
`RESET_REASON_CORE_DEEP_SLEEP`, en déplaçant le seuil ≥ 10 s côté app
(`power_button_task`, `main.c`). Fonctionnellement correct mais **rejeté**
sur retour utilisateur (2026-09-16) : *« le passage en factory devrait être
géré côté bootloader »* — si l'app plante ou se bloque avant que
`power_button_task` ne tourne (ou si la tâche elle-même se bloque), plus
aucun chemin de secours n'existe pour rejoindre la factory depuis la veille.
Le hook bootloader doit rester la source de vérité, précisément parce qu'il
tourne AVANT tout code applicatif et reste donc accessible même app morte.

**Fix retenu** : traiter la vraie root cause plutôt que la contourner.
`bootloader_after_init()` appelle désormais
`rtcio_ll_function_select(RECOVERY_BUTTON_PIN, RTCIO_LL_FUNC_DIGITAL)`
(`hal/rtc_io_ll.h`) — l'équivalent bas niveau, à base de registres, de
`rtc_gpio_deinit()` (composant driver, non disponible/nécessaire au stade
bootloader) — qui rend explicitement la main au domaine digital sur GPIO3
AVANT toute lecture. Sur S3, l'indice `rtcio_num` correspond directement au
numéro de GPIO (offset 0) ; sur un reset « froid » où le pad n'a jamais été
routé par RTC_IO, cet appel est un no-op sans effet de bord. Le hook peut
donc à nouveau mesurer directement le maintien de GPIO3 sur **tous** les
resets, reveil deep sleep inclus — la bascule factory redevient
entièrement pilotée par le bootloader, indépendamment de l'état de l'app.

Seule exception conservée : `RESET_REASON_CORE_SW` (reset logiciel —
`esp_restart()`, déclenché par `power_mgr_switch_to_factory()` côté app ou
`action_cancel()` côté factory) court-circuite la lecture du bouton, car
otadata est déjà positionné correctement par l'appelant dans ce cas ; relire
le bouton ici ne ferait qu'ajouter un 2e délai de confirmation redondant
sans rien changer à la destination du boot.

**Conséquence sur le tableau ci-dessus** : la ligne « Éteint (deep sleep) →
Power tenu ≥ 10 s en continu depuis le réveil » est bien gérée **côté hook
bootloader** (`hooks.c`), comme indiqué initialement — `main.c` ne gère que
les appuis longs effectués une fois l'app déjà démarrée et tournante.

**Amendement 2026-09-16 (suite) — 3e bug : le chien de garde du bootloader
lui-même empêchait d'atteindre le seuil de 10 s** :

Après le fix ci-dessus, le symptôme persistait à l'identique (« idem, une
fois en veille on ne se réveille pas »), et un maintien de 12 s ne
déclenchait pas non plus la bascule factory — signe que le blocage se
produit **avant même** que la question du bouton ne soit tranchée.

**Root cause** : `bootloader_init()` (component `bootloader_support`,
`bootloader_init.c`) arme le RTC WDT (RWDT) avec un timeout **unique de
`CONFIG_BOOTLOADER_WDT_TIME_MS` = 9000 ms** (action
`WDT_STAGE_ACTION_RESET_RTC`) **avant** d'appeler `bootloader_after_init()`
— donc avant que notre hook ne s'exécute. Notre boucle de confirmation
bloque délibérément jusqu'à `CONFIRM_HOLD_US` = **10 000 ms**, sans jamais
nourrir ce chien de garde : il se déclenche à 9 s pile, **reset le chip
avant d'atteindre le seuil**. Ce reset relance tout le bootloader depuis
zéro, qui réarme le même RWDT pour un nouveau cycle de 9 s max — tant que
l'utilisateur maintient le bouton, ce cycle se répète indéfiniment et le
seuil de 10 s n'est **jamais atteignable**, quelle que soit la durée réelle
de maintien. Vu de l'extérieur (écran jamais rafraîchi à ce stade, aucun
code appli encore exécuté), cette boucle de reset silencieuse est
indiscernable d'un blocage total — expliquant à la fois l'absence de
réveil perçu et l'absence de bascule factory après 12 s.

**Fix** : `hooks.c` nourrit désormais explicitement le RWDT
(`wdt_hal_feed()`, avec `wdt_hal_write_protect_disable/enable()` — même
pattern que `bootloader_support/src/flash_encryption/flash_encrypt.c` pour
ses propres opérations longues au même stade) à chaque itération de la
boucle de confirmation (et une fois avant, au moment de la stabilisation du
pull-up). `CONFIRM_HOLD_US` peut ainsi dépasser
`CONFIG_BOOTLOADER_WDT_TIME_MS` sans risque, sans qu'il soit nécessaire de
garder les deux valeurs synchronisées.

**Amendement 2026-09-16 (suite) — 4e bug, un bug de tooling, pas de code :
`hooks.c` n'a jamais été compilé dans ce qui était testé** :

Après TROIS corrections de code successives (combo, pad RTC_IO, chien de
garde) toutes confirmées « build clean » mais toujours signalées cassées
sur hardware à l'identique, la cause s'est révélée être en amont de tout le
code : `./msf_build.bat build` lancé depuis la **racine** du repo (ce que
chaque vérification de cette session utilisait) compile un bootloader qui
**ne contient PAS `hooks.c`** — ESP-IDF ne détecte
`bootloader_components/` que s'il est un enfant direct du
`PROJECT_SOURCE_DIR` du projet compilé, et ce dossier vit sous
`boards/x4pro/factory/`, pas à la racine. Le bootloader réellement flashé
(`installer/x4pro_app/bootloader_16MB.bin`) est **copié** depuis
`boards/x4pro/factory/build/bootloader/bootloader.bin` par le postbuild de
l'app, sans jamais être reconstruit ni vérifié à jour par ce chemin — un
`rm -rf build` + rebuild à la racine ne rafraîchit donc rien. Autrement
dit : chaque « toujours cassé, à l'identique » de ce fil n'était pas un
signal que les fixes précédents avaient échoué — c'était `hooks.c` d'avant
la toute première modification de cette session qui tournait sur le
hardware à chaque test, en boucle. Procédure corrigée documentée dans
`docs/FACTORY.md` §3.4. Les 3 bugs de code ci-dessus restent chacun des
corrections légitimes (confirmées par lecture directe du binaire compilé
après coup) mais **aucun n'avait encore été testé sur hardware** au moment
de cet amendement.

**Amendement 2026-09-16 (suite) — 5e bug, confirmé avec logs réels : stack
overflow dans `power_mgr_switch_to_factory()`** :

Avec le bootloader correctement testé (amendement précédent) et le logging
applicatif enfin fonctionnel (`esp3d_log()` ne se compilait jamais non
plus — `add_compile_options()` après `project()` dans le CMakeLists racine,
même piège que `MSF_BOARD_NAME` déjà documenté juste au-dessus dans ce même
fichier mais pas appliqué à cette ligne ; fixé via `idf_build_set_property`),
les logs ont montré : le chemin **hook bootloader** (Power tenu ≥ 10 s
depuis la veille) fonctionne bien de bout en bout (`... maintenu Nms` →
`seuil atteint` → factory). Le chemin **app-level** (Power tenu ≥ 10 s
depuis App0 éveillée, `power_mgr_switch_to_factory()`) plantait
systématiquement, immédiatement après `erase secteur backup @0xb000` :

```
***ERROR*** A stack overflow in task pwr_btn has been detected.
```

**Root cause** : `power_mgr_switch_to_factory()` déclare
`uint8_t buf[FLASH_SECTOR_SIZE]` — un buffer local de **4096 octets** — dans
une tâche (`power_button_task`) créée avec `xTaskCreate(..., 4096, ...)`,
soit une pile totale égale à la seule taille de ce buffer, sans compter
`entry1`/`entry2`, les autres locales de la boucle, ni l'usage de pile
propre aux appels `esp_flash_read/erase_region/write`. Dépassement garanti
dès que la fonction est atteinte. Explique aussi rétroactivement le splash
« un peu grisé » observé après un test à 14 s (§ plus haut) : ce n'était
pas du ghosting e-ink, mais un crash-reboot en plein milieu d'un refresh.

**Fix** : pile de `power_button_task` portée à 12288 octets (marge large,
PSRAM abondante sur cette board).

**Statut** : ✅ VALIDÉ 2026-09-13 (sémantique boutons amendée 2026-09-13,
feedback Power ajouté 2026-09-15, **combo Right abandonné et remplacé par
Power seul (durée) le 2026-09-16** — 5 bugs corrigés successivement : combo
Power+Right empêchant le réveil, lecture GPIO3 non fiable après réveil
EXT1, chien de garde bootloader trop court, mauvais bootloader testé
(tooling), stack overflow app-level. Chemin hook bootloader validé sur
hardware avec logs ; chemin app-level (`power_mgr_switch_to_factory()`)
corrigé mais reste à revalider sur hardware avec le fix de pile).

---

### ADR-010 : LVGL invalidé pour l'app — UI portée depuis `freeink-sdk` (FreeInkUI), ESP-IDF natif

**Contexte** : le choix initial de LVGL 9.2.2 pour l'UI de l'app (posé en Phase
1, avant tout bring-up e-ink réel) partait d'un a priori générique
« framework GUI embarqué classique », sans expérience e-paper préalable sur ce
projet. Le bring-up et le debug de la factory (session 2026-09-14/15 —
`docs/hardware-specs.md`, `docs/FACTORY.md`) ont révélé que l'e-paper a des
contraintes fondamentalement différentes d'un TFT : bistabilité des pixels,
distinction physique full refresh (GC, plusieurs balayages noir/blanc pour
repurger les charges résiduelles) vs refresh rapide (DU, différentiel, sans
flash mais accumulant du ghosting), gestion d'un framebuffer 1 bpp. LVGL est
agnostique de la techno d'affichage (`flush_cb` générique pensé TFT) : tout ce
travail (bug `s_prev` périmé après un DU, calibrage du budget de refresh
avant un GC forcé, etc. — corrigé cette session dans `boards/x4pro/factory/
main/eink.c`) aurait dû être réimplémenté à la main **dans** un driver LVGL
custom, sans aucun gain vs l'implémentation directe.

**Découverte** : `freeink-sdk` (MIT, `references/freeink-sdk-main/`) a déjà
ce travail fait, nativement pour ce couple contrôleur/board (X4 Pro, UC8279) —
recensé dans `docs/display-driver-references.md` du SDK, et son driver
`Uc8279X4Driver` gère déjà correctement la resynchronisation OLD-plane après
CHAQUE refresh (bug qu'on a dû corriger nous-même dans notre propre driver).
Son firmware jumeau open-source (`crosspoint-reader`, `references/
crosspoint-reader-develop/`) tourne en production sur du X4 Pro réel, validant
le driver et sa couche UI (`FreeInkUI`) en usage réel, pas seulement en
théorie.

**Obstacle initial** : `freeink-sdk`/`crosspoint-reader` sont bâtis sur
PlatformIO + framework Arduino — deux couches au-dessus de l'ESP-IDF natif
qu'utilise tout le reste de MySafeFob (factory comprise). Adopter tel quel
aurait dupliqué l'outillage (`build_mgr.py`/`flash_mgr.py`, patterns
board-agnostic) sur un framework différent.

**Étude de portage** (agent de recherche, 2026-09-15, résultats détaillés
dans la conversation — non dupliqués ici) : lecture exhaustive de `EpdBus`,
`Uc8279X4Driver`, la portion GT911 d'`InputManager`, et `libs/ui/FreeInkUI/`.
Verdict par composant :
- **FreeInkUI** (~3000 lignes, tous les composants d'UI) : **porte tel
  quel** — freestanding C++17, une seule ligne dépendante d'Arduino dans tout
  le module (`src/FreeInkUI.cpp`, un symbole faible `getArduinoLoopTaskStackSize`
  gaté `#if defined(ARDUINO_ARCH_ESP32)`, jamais défini sous ESP-IDF natif —
  compile en no-op sans même avoir besoin d'être retiré).
- **Uc8279X4Driver** (~1000 lignes, le driver e-ink X4 Pro) : **porte
  quasi tel quel** — seulement 2 lignes Arduino directes (`millis`/
  `digitalRead`/`delay`), le reste passe par l'abstraction `EpdBus`.
- **EpdBus** (~450 lignes, SPI/GPIO/ISR bas niveau) : **à réécrire** en
  ESP-IDF natif (`gpio_*`/`spi_device_*`/`esp_timer_get_time`) — c'est le
  seul vrai morceau de traduction mécanique.
- **InputManager (GT911)** : **ignoré** — notre `touch.c` (déjà validé
  matériellement, y compris le fallback self-load/upload host et la zone
  Home recalibrée) fait déjà le travail ; porter celui du SDK aurait été un
  doublon pur.
- **BoardConfig** : **profil X4 Pro extrait** (pins/calibration), pas le
  fichier monolithique 18-devices du SDK.

**Validation par prototype** (`references/test_apps/freeinkui-poc/`,
2026-09-15) : preuve concrète, pas seulement théorique — `FreeInkUI::
DisplayTarget` posé sur notre `eink.c` affiche correctement (splash texte
lisible), et `Frame`/`InteractionBuffer` (mécanisme d'interaction natif de
FreeInkUI) piloté par nos `buttons.c`/`touch.c` déjà validés résout
correctement focus et confirmation (navigation L/R, confirmation Home,
wrap circulaire, action routée vers le bon composant — vérifié log à
l'appui). Aucune ligne d'Arduino ni de PlatformIO nécessaire.

**Décision** :
1. **LVGL est abandonné pour l'app.** Le contrat « LVGL 9.2.2 figé »
   (`FEATURES.md` §7, `README.md` « Contrats critiques ») est invalidé et
   remplacé par **FreeInkUI porté en ESP-IDF natif**.
2. **Portage réel** (tâche 8.4) dans cet ordre (repris de l'étude) :
   `FreeInkUI` (quasi acquis) → profil `BoardConfig` X4 Pro extrait →
   `EpdBus` réécrit natif → `Uc8279X4Driver` porté → intégration sur notre
   `eink.c`/`touch.c`/`buttons.c` existants (déjà prouvée par le POC).
3. **Amendé 2026-09-15** : la factory adopte elle aussi FreeInkUI (au moins
   `DisplayTarget`, pour afficher un splash statique au boot — cf.
   `docs/FACTORY.md`), mais via **une copie figée et
   indépendante** sous `boards/x4pro/factory/components/`, jamais partagée
   avec la copie que l'app vendorisera pour elle-même. La duplication est
   assumée délibérément : c'est le prix pour garder la propriété centrale
   d'ADR-007 (« la factory doit rester minimale, autonome, testée telle
   quelle » — `README.md` §Structure) — un changement côté app (mise à jour
   de freeink-sdk, refonte de composants) ne doit jamais pouvoir casser la
   factory. Règle : la copie factory n'est mise à jour que par une action
   explicite et testée sur hardware, jamais automatiquement en même temps
   que celle de l'app.
4. **Attribution** : `freeink-sdk` est MIT, dérivé de `open-x4-epaper/
   community-sdk` (MIT) — licence compatible, attribution à porter dans
   `NOTICE`/en-tête des fichiers copiés au moment de l'intégration réelle
   (au-delà du POC).

**Justification du choix par rapport aux alternatives étudiées** (tableau
utilisateur 2026-09-15) : GxEPD2 (léger, bon contrôle refresh, mais pas
pensé pour ce contrôleur précis — portage from scratch) et LVGL (excellent
pour UI complexe générique, mais contrôle du refresh e-paper à bricoler
soi-même et support X4 Pro à construire de zéro) perdent face à FreeInkUI
sur les deux critères qui comptent pour MySafeFob : **support natif du
X4 Pro/UC8279** (déjà fait, déjà validé en prod par crosspoint-reader) et
**UI simple suffisante** (le besoin réel : menu TOTP + password manager,
pas une UI complexe façon LVGL).

**Statut** : ✅ VALIDÉ 2026-09-15 (portage à réaliser en tâche 8.4).

---

### ADR-011 : app1 retiré — un seul slot app, factory comme filet de sécurité

**Contexte** : ADR-007 avait posé un dual-slot esp_ota (`app0`/`app1`) pour
permettre un rollback automatique A/B en cas de mise à jour ratée, en plus
de la factory (secours sans PC). Après le bring-up complet de la factory
(cette session) — recovery e-ink/touch validé, action `SD -> app0` déjà
fonctionnelle pour (re)flasher l'app depuis la factory — la question s'est
posée : le 2ᵉ slot apporte-t-il encore quelque chose ?

**Analyse** :
1. **Pas d'OTA réseau** (ADR-003, tranché) : aucun scénario de coupure/
   corruption en vol pendant un transfert qui justifierait un rollback
   automatique — les mises à jour SD sont un fichier complet, écrit d'un
   coup.
2. **`esp_ota_end()` valide déjà le header/checksum** de l'image avant de la
   rendre bootable — un fichier tronqué ou corrompu est rejeté à l'écriture,
   pas après un boot raté.
3. **La factory sait déjà reflasher `app0` directement** (`SD -> app0`,
   validé cette session) : même un firmware "valide mais logiquement cassé"
   se répare en repassant par la factory (combo Power+Right, ADR-009), sans
   avoir besoin d'un second slot pour y revenir automatiquement.

**Décision (validée utilisateur 2026-09-15)** :
1. **`app1` retiré de `partitions.csv`.** Un seul slot app (`app0`).
   `secrets` (F-05b, ADR-002 : conteneur TOTP + passwords + recovery codes)
   **agrandi de 0x40000 à 0x240000 (2,25 Mo)** avec l'espace libéré —
   marge confortable pour le coffre chiffré.
2. **F-06 (mise à jour firmware) devient une fonctionnalité factory, pas
   app** : la séquence `esp_ota_begin/write/end` + `set_boot_partition`
   tourne dans la factory (déjà implémenté), jamais dans l'app en cours
   d'exécution — pas de risque d'auto-écraser la partition active pendant
   qu'elle s'exécute, pas besoin du dérivé applicatif self-check/mark-valid/
   rollback documenté dans une version antérieure d'`INTERFACES.md` §3.
3. **Contre-partie assumée** : pas de rollback automatique si le nouveau
   firmware boote mais est logiquement cassé — récupération manuelle via
   factory au lieu d'un retour automatique sur l'ancien slot. Acceptable :
   la factory est déjà le filet de sécurité de référence du projet, robuste
   et testée indépendamment de l'app.
4. **Menu factory simplifié** : `Boot app1`/`SD -> app1` retirés
   (`boards/x4pro/factory/main/main.c`), plus de sondage
   `esp_partition_find_first(..., "app1")`.

**Documents mis à jour en conséquence** : `partitions.csv`,
`flash_params.json`, `docs/INTERFACES.md` §2/§3, `docs/FACTORY.md` §5.3/§6,
`docs/FEATURES.md` F-06 et critère d'acceptation §8.4, `README.md`.

**Statut** : ✅ VALIDÉ 2026-09-15.

**Amendement 2026-09-15 (même jour) — `SD -> factory` retiré** : en
implémentant ce qui précède, un second risque a été identifié (utilisateur) :
l'action `SD -> factory` héritée du PiBot faisait
`esp_partition_erase_range` + `esp_partition_write` **sur la partition
`factory` pendant qu'elle s'exécute depuis cette même partition (XIP)** —
contrairement à `SD -> app0` qui écrit une partition inactive. Toute
interruption en plein vol (coupure batterie, bug, watchdog) entre l'erase et
la fin du write laisse le code en cours d'exécution lui-même invalide,
**sans aucun filet de recovery sans PC** — exactement le scénario que toute
cette architecture cherche à éviter. Retiré : `MENU_ACTION_SD_FACTORY` et la
branche `to_factory` d'`action_sd_flash()` (`boards/x4pro/factory/main/
main.c`). La factory est désormais traitée comme le bootloader : mise à jour
**uniquement par USB/serial** (`flash_mgr.py --variant x4pro_factory`),
jamais en self-service sur le terrain — cohérent avec « testée telle quelle »
(ADR-007). Menu factory final : `Boot app0`, `SD -> app0`.

---

## Règles du projet

1. **Pas de code sans spec** — Chaque composant a un header documenté avant implémentation.
2. **Pas de merge sans test** — Chaque PR/feature testée sur hardware réel.
3. **Pas de breaking change sans version** — Le format de stockage est versionné dès le départ.
4. **Python = vérité terrain** — Le script `totp_reference.py` est la référence absolue.
5. **Air-gapped by design** — Aucune feature ne nécessite de connectivité réseau en usage normal.

---

*Document maintenu par les développeurs — toute modification doit être revue et approuvée.*
