# Post-mortem : bouton Power / bascule factory (2026-09-16)

> Session unique, ~24h de travail effectif. Objectif final atteint et validé
> sur hardware avec logs à l'appui. Ce document capitalise le *pourquoi* —
> sept bugs distincts, indépendants les uns des autres, empilés sur un
> mécanisme qui semblait trivial sur le papier. Les détails techniques
> exacts (code, valeurs, diffs) vivent dans `docs/ROADMAP.md` (ADR-009 et
> ses amendements) et dans les commentaires des fichiers cités ; ce document
> raconte l'enchaînement et ce qu'il faut en retenir.

## 1. L'objectif, et pourquoi il semblait simple

Design cible, décidé en cours de route (amendement ADR-009) :

- **Power tenu < 10 s** : veille ↔ réveil (comportement « bouton power »
  classique).
- **Power tenu ≥ 10 s** : bascule vers la partition `factory`, **quel que
  soit l'état de départ** — app éveillée, device endormi, ou même app
  plantée/gelée.

Sur le papier : un GPIO, un chronomètre, deux seuils. Rien qui justifie une
journée entière. En pratique, ce mécanisme traverse quatre couches
radicalement différentes du firmware (RTC/deep-sleep hardware, bootloader
ROM, driver flash applicatif, FreeRTOS scheduling), et **chacune** a caché
un bug indépendant. Aucun des sept n'était visible sans avoir déjà corrigé
les précédents — ils se masquaient les uns les autres.

## 2. Le point de départ : un design abandonné

Le mécanisme initial (issu du portage PiBot, ADR-007) utilisait un combo
**Power + Right** : les deux boutons tenus ensemble déclenchaient la
bascule factory via le hook bootloader (`hooks.c`, alors câblé sur
GPIO7/Right).

Testé empiriquement sur hardware (Test A : Power seul → réveil
systématique ; Test B : Power+Right ensemble, tenus 10 s, relâchés →
**aucune réaction, jamais, quelle que soit la durée**) : le combo empêche
le réveil matériel lui-même de se déclencher. Root cause jamais identifiée
avec certitude (hypothèse : interférence électrique entre deux pads RTC_IO
tenus bas simultanément pendant la latch de réveil) — mais peu importe
la cause exacte, le fait est irréfutable et non contournable côté
logiciel. **Décision** : abandon total du combo, redesign vers Power seul
à seuils de durée. C'est ce redesign qui a ouvert la boîte de Pandore.

## 3. Les sept bugs, dans l'ordre où ils ont été découverts

Chaque bug a produit un symptôme sur hardware, chaque symptôme a d'abord
été mal expliqué au moins une fois avant que la vraie cause n'émerge —
souvent parce que le bug suivant masquait l'effet du fix précédent.

### Bug 1 — réveil immédiat après mise en veille (résolu avant le redesign)

**Symptôme** : « je passe en veille et je me réveille aussitôt ».
**Cause** : `esp_deep_sleep_start()` appelé pendant que Power est encore
physiquement enfoncé (c'est le geste qui a déclenché la mise en veille) —
la condition EXT1 (`ANY_LOW`) est donc déjà vraie au moment même où le
sommeil commence, provoquant un rebond quasi instantané.
**Fix** : attendre explicitement le relâchement du bouton avant d'armer
EXT1 et de dormir (`power_mgr_shutdown()`), avec un garde symétrique côté
réveil (`seen_release` dans `power_button_task()`) pour éviter qu'un appui
résiduel au réveil ne réarme immédiatement un nouveau long-press.
**Fichiers** : `components/power_mgr/power_mgr.c`, `main/main.c`.

### Bug 2 — lecture de GPIO3 non fiable juste après un réveil EXT1

**Symptôme** (après passage du hook sur GPIO3/Power à la place de
GPIO7/Right) : « impossible de sortir de veille ».
**Cause** : `esp_sleep_enable_ext1_wakeup()` route le pad GPIO3 via le
domaine RTC_IO pour la durée du sommeil ; cette configuration persiste au
travers du réveil. Lire GPIO3 en digital brut (`gpio_ll_get_level`, la
méthode utilisée sans souci quand le hook lisait GPIO7 — jamais un pin de
réveil) à ce stade renvoie une valeur figée, indépendante de l'état réel du
bouton.
**Deux tentatives** :
  1. *Rejetée* : ne plus jamais lire GPIO3 sur un réveil deep sleep,
     déplacer la détection côté app. Fonctionnellement correct mais
     rejeté sur retour utilisateur — voir §4.
  2. *Retenue* : `rtcio_ll_function_select(GPIO3, RTCIO_LL_FUNC_DIGITAL)`
     (header HAL bas niveau, sans dépendance driver/FreeRTOS, donc
     utilisable en contexte bootloader) — l'équivalent registre de
     `rtc_gpio_deinit()` — rend explicitement la main au digital avant
     toute lecture.
**Fichier** : `boards/x4pro/factory/bootloader_components/custom_bootloader/hooks.c`.

### Bug 3 — chien de garde du bootloader trop court pour le seuil choisi

**Symptôme** : après le fix du bug 2, symptôme identique en apparence
(« idem, une fois en veille on ne se réveille pas »), puis découverte que
même 12 s de maintien ne suffisaient pas non plus à atteindre factory.
**Cause** : `bootloader_init()` arme le RTC watchdog avec un timeout fixe
de 9000 ms (`CONFIG_BOOTLOADER_WDT_TIME_MS`) **avant** que le hook ne
s'exécute. Notre boucle de confirmation bloquait délibérément jusqu'à
10 000 ms sans jamais nourrir ce chien de garde — il se déclenchait à 9 s,
resettait le chip *avant* d'atteindre le seuil, ce qui relançait le
bootloader (qui réarmait le même watchdog pour un nouveau cycle de 9 s
max). Tant que l'utilisateur maintenait le bouton, le seuil de 10 s était
**structurellement inatteignable** — boucle de reset silencieuse
indiscernable, de l'extérieur, d'un blocage total (l'écran n'est jamais
rafraîchi à ce stade).
**Fix** : nourrir explicitement le RTC watchdog (`wdt_hal_feed()`) à
chaque itération de la boucle de confirmation — même pattern que
`bootloader_support/src/flash_encryption/flash_encrypt.c` pour ses propres
opérations longues au même stade du boot.
**Fichier** : `hooks.c`.

### Bug 4 — comptage de durée erroné (sous-estimation ~3.5×)

**Découverte en marge du bug 3**, jamais isolément responsable d'un
symptôme rapporté mais un vrai bug : `is_button_pressed()` bloque ~25 ms en
interne (5 lectures × 5 ms), mais la boucle de confirmation ajoutait
**par-dessus** un délai artificiel de 10 ms tout en ne comptant que ces
10 ms dans le total — le temps réel écoulé par itération (~35 ms) était
sous-estimé d'un facteur ~3.5. Concrètement : pour que le compteur
atteigne 10 s « nominales », il fallait en réalité maintenir le bouton
~35 s.
**Fix** : compter le temps réellement écoulé (durée interne mesurée de
`is_button_pressed()`), sans délai supplémentaire.
**Fichier** : `hooks.c`.

### Bug 5 — mauvais bootloader testé (bug de tooling, pas de code)

**Le plus coûteux des sept.** Après trois corrections de code successives
(bugs 2, 3, 4), toutes confirmées « build clean », toutes signalées cassées
sur hardware **à l'identique**. La cause était en amont de tout le code :
`./msf_build.bat build` lancé depuis la racine du repo — ce que *chaque*
vérification de cette session utilisait — compile un bootloader qui **ne
contient pas `hooks.c`**. ESP-IDF ne détecte `bootloader_components/` que
s'il est un enfant direct du `PROJECT_SOURCE_DIR` du projet compilé ; ce
dossier vit sous `boards/x4pro/factory/`, pas à la racine. Le bootloader
réellement flashé (`installer/x4pro_app/bootloader_16MB.bin`) est **copié**
depuis `boards/x4pro/factory/build/bootloader/bootloader.bin` par le
postbuild de l'app, sans jamais être reconstruit ni vérifié à jour par ce
chemin.

Autrement dit : **aucun des trois bugs précédents n'avait encore été
testé sur le hardware réel** au moment où ils semblaient tous avoir
« échoué ». Le firmware réellement flashé était une version bien plus
ancienne, figée depuis le tout début de la session. Chaque « toujours
cassé, à l'identique » n'était donc pas un signal d'échec du fix — c'était
la preuve, mal interprétée, que rien de neuf n'avait jamais tourné.

**Comment ça a été détecté** : en désespoir de cause, inspection directe du
binaire compilé (`nm bootloader.elf | grep bootloader_after_init` →
pointait vers le stub faible d'ESP-IDF, pas vers notre hook).
**Fix** : procédure de build documentée explicitement (`docs/FACTORY.md`
§3.4) — toujours reconstruire `boards/x4pro/factory` en premier, séparément,
puis la racine.

### Bug 6 — stack overflow dans `power_mgr_switch_to_factory()`

**Symptôme** : depuis App0 éveillée, un maintien ≥10 s ne faisait « rien »,
ou laissait un écran dégradé nécessitant un reset série pour débloquer (le
bouton « reset » physique n'avait aucun effet — cohérent avec une tâche
figée qui ne lit plus jamais le GPIO).
**Cause**, enfin visible une fois le bon binaire testé *avec logs* :
`power_mgr_switch_to_factory()` déclare `uint8_t buf[FLASH_SECTOR_SIZE]` —
un buffer local de **4096 octets** — dans une tâche (`power_button_task`)
créée avec `xTaskCreate(..., 4096, ...)`. Le buffer, à lui seul, occupait
déjà toute la pile allouée, sans compter le reste des locales ni l'usage de
pile propre aux appels `esp_flash_read/erase_region/write`. Dépassement
garanti dès que la fonction était atteinte —
`vApplicationStackOverflowHook` → `panic_abort` → reboot.
Explique rétroactivement le « splash un peu grisé » observé plus tôt dans
la session : pas du ghosting e-ink, un crash-reboot en plein refresh.
**Fix** : pile portée à 12 288 octets (marge large, PSRAM abondante).
**Fichier** : `main/main.c`.

### Bug 7 — `esp3d_log()` compilé en no-op silencieux dans toute l'app

**Découvert en ajoutant les logs de diagnostic** qui ont permis de trouver
le bug 6 : aucun appel `esp3d_log()` (macro sans suffixe) de `main.c`
n'a jamais réellement produit de sortie, y compris la bannière de boot,
depuis (vraisemblablement) l'écriture initiale de ce fichier — bien avant
cette session. `esp3d_log()` ne se compile que si `ESP3D_LOG >=
ESP3D_LOG_LEVEL_ALL` (4), mais `ESP3D_LOG` n'était **jamais défini du
tout** pour aucun composant : `CMakeLists.txt` racine appelait
`add_compile_options(-DESP3D_LOG=${MSF_LOG_LEVEL})` **après** `project()`,
qui ne se propage pas aux composants IDF en 5.5.5 — exactement le même
piège que celui déjà documenté (et corrigé) pour `MSF_BOARD_NAME` deux
lignes au-dessus dans ce même fichier, jamais appliqué à cette ligne.
**Fix** : `idf_build_set_property(COMPILE_DEFINITIONS "ESP3D_LOG=..."
APPEND)`, même mécanisme que pour `MSF_BOARD_NAME`. Les appels
`esp3d_log()` de `main.c` ont aussi été repassés en `esp3d_log_d()`
(niveau debug, cohérent avec `MSF_LOG_LEVEL=3`).
**Fichiers** : `CMakeLists.txt` (racine), `main/main.c`.

## 4. Le détour rejeté : app-level plutôt que bootloader-level

Entre les bugs 2 et 3, une première tentative de fix (déplacer la mesure
« Power tenu ≥ 10 s depuis le réveil » côté app plutôt que dans le hook)
a été **explicitement rejetée sur retour utilisateur** : *« le passage en
factory devrait être géré côté bootloader je pense »*. Raison : si l'app
plante ou se bloque, une mesure uniquement côté app ne peut jamais
garantir l'accès à factory — alors qu'un mécanisme bootloader s'exécute
avant tout code applicatif, donc reste accessible même app morte.

Ce choix s'est révélé juste et a été validé empiriquement bien plus tard
dans la session (voir §6) : un crash applicatif (bug 6 inclus) reboote via
`esp_restart_noos()`, qui produit `RESET_REASON_CPU0_SW`/`CPU1_SW` — un
type de reset que le hook **continue de vérifier** normalement (seul
`RESET_REASON_CORE_SW`, réservé aux resets déjà pilotés par du code qui a
lui-même positionné otadata, est court-circuité). Le hook reste donc la
voie de secours même en cas de crash-loop, sans action supplémentaire.

## 5. Bilan chiffré

| # | Bug | Couche | Symptôme rapporté | Détecté via |
|---|---|---|---|---|
| — | Combo Power+Right | Hardware/RTC | Réveil ne se déclenche jamais avec les 2 boutons | Test A/B ciblé |
| 1 | Réveil immédiat post-sleep | `power_mgr.c` | Rebond veille↔réveil instantané | Observation directe |
| 2 | Pad RTC_IO non rendu au digital | `hooks.c` | Plus aucun réveil après passage à GPIO3 | Lecture doc ESP-IDF + code source HAL |
| 3 | RTC WDT bootloader (9s) < seuil (10s) | `hooks.c` | Même symptôme après fix #2, ni réveil ni factory à 12s | Lecture code source `bootloader_init.c` |
| 4 | Comptage de durée ×3.5 sous-estimé | `hooks.c` | (masqué par #3, jamais isolément symptomatique) | Relecture attentive de la boucle |
| 5 | Mauvais bootloader testé (tooling) | build system | 3 fixes de code consécutifs « sans effet » | `nm`/`grep` sur le binaire compilé |
| 6 | Stack overflow (buffer 4KB / pile 4KB) | `main.c` | Rien, ou écran figé, reset série requis | Logs de diagnostic ajoutés sur demande |
| 7 | `esp3d_log()` jamais compilé | `CMakeLists.txt` racine | Silence total côté logs applicatifs | Recherche de strings absentes du binaire |

**Sept bugs, cinq couches différentes** (hardware/RTC, bootloader ROM,
build system/tooling, driver flash applicatif, FreeRTOS/build config),
**aucun visible sans avoir déjà corrigé au moins un des autres**.

## 6. Validation finale (hardware, avec logs)

Séquence confirmée fonctionnelle de bout en bout, logs à l'appui :

- **Veille** (app éveillée, maintien ≥1,5s puis relâché) : `Power long:
  mise en veille` → écran veille → deep sleep. ✅
- **Réveil court** (endormi, tap bref) : retour app normal, splash → prêt.
  ✅
- **Annulation propre** (endormi, maintien < 10s puis relâché, côté hook
  ou côté app) : `relache trop tot` → boot normal. ✅
- **Factory depuis le réveil** (endormi, maintien ≥10s continu) : hook
  bootloader `seuil atteint` → `otadata efface` → boot factory. ✅
- **Factory depuis l'app éveillée** (App0, maintien ≥10s continu) :
  `power_mgr_switch_to_factory()` termine sans crash → `esp_restart()` →
  otadata déjà vide → boot factory. ✅ (validé après le fix du bug 6)
- **Résilience crash-loop** : confirmée par le comportement observé du
  hook sur un reset `RTC_SW_CPU_RST` (celui du bug 6 lui-même, avant son
  fix) — le hook a bien re-vérifié le bouton sur ce reset, prouvant que le
  chemin de secours reste actif même après un crash applicatif.

## 7. Trade-off restant, assumé et documenté (non « bug »)

Après une bascule factory déclenchée côté app, le hook bootloader
re-vérifie *quand même* le bouton sur le reboot qui suit (puisque
`esp_restart()` produit `CPU0_SW`/`CPU1_SW`, différent du `CORE_SW` que le
hook court-circuite) — l'utilisateur peut donc se retrouver à tenir Power
pendant un délai de confirmation redondant après que l'app ait déjà réussi
sa propre bascule. **Délibérément non corrigé** : le fix évident (ignorer
aussi `CPU0_SW`/`CPU1_SW`) réouvrirait le point aveugle crash-loop du §4,
puisqu'un crash produit exactement le même type de reset qu'un
`esp_restart()` intentionnel — impossible de les distinguer par la seule
raison de reset. Une vraie solution demanderait un marqueur explicite
(ex. un octet écrit par l'app juste avant son propre restart, lu et
effacé par le hook) — non implémentée à ce stade, laissée en décision
ouverte.

## 8. Évitable ou inévitable ? Analyse honnête de l'approche

Question posée en fin de session : est-ce que ces sept bugs (huit avec le
combo) étaient inévitables — seulement solubles par des cycles
expérimentation/correction sur hardware réel — ou est-ce qu'une meilleure
discipline en amont en aurait évité une partie ? Réponse honnête : **un
mélange des deux, mais la majorité était évitable**. Classement bug par
bug :

| # | Bug | Catégorie | Justification |
|---|---|---|---|
| — | Combo Power+Right ne réveille jamais | **Inévitable** | Comportement électrique/RTC empirique, non documenté, non dérivable par lecture de code ou de datasheet — seul un test hardware isolé (A/B) pouvait le révéler. |
| 1 | Réveil immédiat post-sleep | **Partiellement évitable** | Le bug EXT1-ANY_LOW-déjà-vrai-à-l'entrée-en-sommeil est un piège connu et documenté d'ESP-IDF (recherché *a priori*, pas découvert par hasard) — mais sa manifestation précise ne s'observe qu'à l'usage réel du bouton, pas en lecture de code seule. |
| 2 | Pad RTC_IO non rendu au digital | **Évitable avec plus de recherche amont** | `rtc_gpio_deinit()` existe précisément pour ce cas d'usage documenté d'ESP-IDF (réutiliser un pin de réveil EXT1 comme GPIO classique après coup). Une lecture de la doc `esp_sleep`/`rtc_io` *avant* d'écrire le hook (plutôt qu'après l'échec) l'aurait signalé. |
| 3 | RTC WDT bootloader (9s) < seuil (10s) | **Évitable** | `CONFIG_BOOTLOADER_WDT_TIME_MS` est un `Kconfig` visible, et `bootloader_init.c` (~200 lignes) est court. Vérifier "qu'est-ce qui pourrait interrompre une boucle bloquante de 10s à ce stade du boot" *avant* d'écrire la boucle — plutôt qu'après l'avoir vue échouer deux fois — l'aurait évité. |
| 4 | Comptage de durée ×3.5 sous-estimé | **Évitable** | Erreur arithmétique locale dans ~10 lignes de code, détectable par une relecture ligne à ligne ou un calcul à la main du temps réel écoulé par itération. Pas besoin de hardware pour la trouver. |
| 5 | Mauvais bootloader testé (tooling) | **Évitable — et le plus coûteux des sept** | Le commentaire exact expliquant le piège (`postbuild.cmake` : *« le bootloader du build app n'a PAS de hook »*) existait déjà dans la codebase, écrit par une session antérieure. Il n'a pas été relu/appliqué avant de lancer trois cycles de correction. La règle qui aurait évité ce coût : **dès qu'un résultat hardware contredit un fix censément appliqué, vérifier par inspection directe du binaire (`nm`, `grep`) que le fix a bien atteint le hardware — avant de reformuler une nouvelle hypothèse de bug.** |
| 6 | Stack overflow (buffer 4Ko / pile 4Ko) | **Évitable** | Discipline embarquée de base : tout ajout d'un buffer local de taille notable (ici, exactement `FLASH_SECTOR_SIZE`) dans une fonction appelée depuis une tâche existante appelle une vérification immédiate de la pile de cette tâche. Aurait dû être fait à l'écriture de `power_mgr_switch_to_factory()`, pas après un crash observé. |
| 7 | `esp3d_log()` jamais compilé | **Évitable — et la codebase le savait déjà** | Le commentaire documentant exactement ce piège CMake (`add_compile_options()` après `project()`) était déjà présent dans le même fichier, appliqué à `MSF_BOARD_NAME` juste au-dessus. Le motif n'a simplement pas été généralisé à la ligne suivante au moment où elle a été écrite. |

**Bilan** : sur huit problèmes, **un seul** (le combo Power+Right) était
véritablement irréductible à autre chose qu'un cycle d'expérimentation
hardware — sa cause reste d'ailleurs non confirmée avec certitude à ce
jour. Les sept autres avaient chacun un signal disponible *avant* le
premier test raté : une doc ESP-IDF à lire, un fichier de config à
grepper, un calcul à vérifier à la main, un commentaire déjà écrit dans
la codebase elle-même. Le bug #5 (mauvais binaire testé) est le cas le
plus net : il a à lui seul multiplié par ~3 le nombre de cycles
nécessaires, en faisant passer pour "hardware têtu" ce qui était un
problème de tooling déjà documenté.

**Ce qui aurait le plus raccourci la session**, par ordre d'impact
probable :
1. Vérifier l'artefact réellement testé (bug #5) dès le premier résultat
   hardware inattendu, pas après le troisième.
2. Ajouter le logging de diagnostic (ce qui a débloqué les bugs #5, #6 et
   révélé #7) *avant* la première tentative de correction sur hardware,
   pas après plusieurs échecs — chaque round sans logs a coûté un cycle
   complet de rebuild/reflash/retest pour un résultat ambigu.
3. Relire systématiquement les commentaires déjà présents dans les
   fichiers touchés avant de les modifier (bugs #5 et #7 avaient tous
   deux leur solution déjà écrite, ailleurs dans le même fichier ou un
   fichier adjacent, avant même de commencer).

## 9. Leçons à retenir (pour cette codebase, et au-delà)

1. **« Build clean » ne prouve rien sur le contenu du binaire flashé.**
   Un multi-projet avec artefacts partagés/copiés (ici : bootloader
   commun entre app et factory) peut faire tourner un binaire ancien
   pendant qu'on croit tester du code neuf, sans aucune erreur visible.
   Vérifier par inspection directe (`nm`, `grep` de strings connues) dès
   qu'un symptôme persiste identique après plusieurs corrections
   censément indépendantes.
2. **`add_compile_options()`/`add_compile_definitions()` après `project()`
   ne se propagent pas aux composants ESP-IDF (5.5.5).** Piège rencontré
   deux fois dans ce seul fichier (`MSF_BOARD_NAME`, puis `ESP3D_LOG`) —
   `idf_build_set_property(COMPILE_DEFINITIONS ...)` est la seule méthode
   fiable après `project()`.
3. **Un pin utilisé pour le réveil EXT1 ne peut pas être relu en digital
   brut sans rendre explicitement la main au domaine digital**
   (`rtcio_ll_function_select(..., RTCIO_LL_FUNC_DIGITAL)`), y compris en
   contexte bootloader.
4. **Toute boucle de blocage volontaire au niveau bootloader doit nourrir
   le RTC watchdog** si sa durée peut dépasser
   `CONFIG_BOOTLOADER_WDT_TIME_MS` — sinon le watchdog protège le
   bootloader contre... le bootloader lui-même.
5. **Dimensionner la pile d'une tâche en fonction de ses buffers locaux
   les plus gros**, pas d'une estimation générique — un buffer de la
   taille d'un secteur flash (4 Ko) est un cas fréquent dès qu'on
   manipule `esp_flash_*` directement.
6. **La raison de reset seule ne suffit pas à distinguer un crash d'un
   redémarrage intentionnel** si les deux passent par le même chemin
   (`esp_restart_noos()`) — un état explicite est nécessaire pour aller
   plus loin que « toujours vérifier par sécurité ».
7. **Obtenir des logs réels bat toute hypothèse, même bien argumentée.**
   Plusieurs rounds de cette session (bugs 2, 3, 6) ont vu une hypothèse
   plausible mais fausse ou incomplète être proposée puis corrigée « à
   l'aveugle », avant qu'un vrai log (bug 5 débloqué, logging bug 7
   corrigé) ne révèle la cause exacte en une seule lecture. Quand une
   correction censément solide ne change rien sur hardware, la priorité
   devient d'obtenir de la visibilité (logs), pas de formuler une
   nouvelle hypothèse.

## 10. Références

- `docs/ROADMAP.md` — ADR-009 et ses amendements (détail technique complet
  de chaque fix, dans l'ordre chronologique).
- `docs/FACTORY.md` §3.4 — procédure de build correcte (bug #5).
- `boards/x4pro/factory/bootloader_components/custom_bootloader/hooks.c` —
  hook bootloader (bugs #2, #3, #4).
- `components/power_mgr/power_mgr.c`, `main/main.c` — logique app-level
  (bugs #1, #6, #7).
- `CMakeLists.txt` (racine) — bug #7.
