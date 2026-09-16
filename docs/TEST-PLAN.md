# TEST-PLAN — Validation TOTP Python & ESP-IDF

> **Version** : 1.0  
> **Date** : 2026-08-30  
> **Statut** : Phase 2 — À valider

---

## 1. Objectif

Établir un **service de référence** avec 2FA TOTP activé, servant de vérité terrain pour valider :
- L'implémentation Python (`totp_reference.py`)
- L'implémentation C/ESP-IDF (`test_totp/`)
- Le firmware final sur X4 Pro

Le service doit être **jetable** : si notre implémentation TOTP échoue et nous lock out, on peut le recréer en quelques minutes sans perdre de données personnelles.

---

## 2. Service cible : GitHub (compte dédié)

### Pourquoi GitHub ?

| Critère | GitHub |
|---------|--------|
| Gratuit | ✅ Free tier |
| 2FA TOTP natif | ✅ Authenticator app (TOTP) |
| Codes de backup | ✅ 16 codes générables |
| Facilité de reset | ✅ Désactivation 2FA possible via email backup |
| Pas de données sensibles | ✅ Compte dédié, jetable |
| Logs d'auth | ✅ Historique des connexions visible |
| API / CLI | ✅ `gh auth` pour tests automatisés |

### Pourquoi PAS un compte Google personnel ?

- Compte Google = trop critique. Si notre TOTP fail et qu'on a pas les codes backup, c'est la panique.
- GitHub dédié = zéro enjeu émotionnel.

---

## 3. Procédure de création du compte de test

### 3.1 Création du compte

1. Aller sur https://github.com/signup
2. Créer un compte avec :
   - Email : adresse jetable (ex: `x4pro-test@yopmail.com` ou alias)
   - Username : `x4pro-totp-test` (ou similaire)
   - Password : généré par password manager
3. **Ne PAS lier de numéro de téléphone** (on veut du TOTP pur, pas SMS)

### 3.2 Activation du 2FA TOTP

1. Settings → Password and authentication → Two-factor authentication
2. Click "Enable two-factor authentication"
3. Choisir **"Authenticator app"** (pas SMS, pas security key)
4. GitHub affiche un **QR code** et une **clé secrète en texte** (format Base32)
5. **Capturer la clé secrète Base32** — c'est le secret partagé pour notre TOTP
6. Compléter l'activation avec le premier code généré

### 3.3 Sauvegarde des codes de recovery

GitHub génère **16 codes de recovery** à usage unique.

1. **Sauvegarder immédiatement** dans un fichier chiffré ou sur papier
2. Nommer le fichier `github-recovery-codes.txt`
3. Ces codes servent à récupérer l'accès si notre TOTP ne fonctionne pas

### 3.4 Sauvegarde du secret TOTP

Le secret Base32 affiché par GitHub lors de l'activation est la clé.

1. Copier le secret dans un fichier `github-totp-secret.txt`
2. Ce fichier sera lu par `totp_reference.py --secret-file`
3. **Ne JAMAIS commiter ce fichier** — ajouter à `.gitignore`

---

## 4. Procédure de recovery (si lockout)

### Scénario 1 : Notre TOTP ne fonctionne pas, mais on a les codes de recovery

1. Aller sur https://github.com/login
2. Entrer email + password
3. Quand demandé pour le code 2FA, cliquer "Use a recovery code"
4. Entrer un des 16 codes de recovery
5. Accès rétabli → désactiver temporairement le 2FA → recommencer les tests

### Scénario 2 : On a perdu les codes de recovery (catastrophe)

1. GitHub permet la récupération via email vérifié
2. Délai de récupération : 24-72h
3. **Mitigation** : conserver les codes de recovery dans 2 endroits physiques distincts

### Scénario 3 : On veut tout recommencer à zéro

1. Se connecter au compte (avec recovery code si besoin)
2. Settings → Password and authentication → Disable 2FA
3. Supprimer le compte si vraiment jetable : Settings → Account → Delete account
4. Recréer un nouveau compte GitHub en 5 minutes

---

## 5. Stratégie de validation

### 5.1 Validation Python (Phase 3)

**Outil** : `totp_reference.py --secret-file github-totp-secret.txt`

| Test # | Description | Critère de succès |
|--------|-------------|-------------------|
| T1 | Code actuel match GitHub login | 1 code valide sur 1 essai |
| T2 | 5 codes consécutifs match | 5/5 codes validés sur 2.5 min |
| T3 | Drift tolerance | Codes valides ±1 fenêtre (±30s) acceptés |
| T4 | Fenêtre de transition | Code précédent accepté 5s après expiration |
| T5 | Export C array | Tableau copié-collé sans erreur |
| T6 | Secret re-saisi | Résultat identique avec secret re-tapé à la main |

**Gate de sortie** : T1-T6 tous passent.

### 5.2 Validation ESP-IDF C (Phase 4)

**Outil** : `test_totp/` flashé sur ESP32 generic

| Test # | Description | Critère de succès |
|--------|-------------|-------------------|
| E1 | Self-test RFC 6238 au boot | 6/6 vecteurs passent |
| E2 | Code actuel match GitHub | 1 code valide sur 1 essai |
| E3 | 5 codes consécutifs match | 5/5 codes validés sur 2.5 min |
| E4 | Identique à Python | Code ESP32 == Code Python pour même timestamp |
| E5 | Stability 24h | Device allumé 24h, codes toujours corrects |

**Gate de sortie** : E1-E5 tous passent.

### 5.3 Validation X4 Pro (Phase 8)

| Test # | Description | Critère de succès |
|--------|-------------|-------------------|
| X1 | Code affiché sur E-Ink match GitHub | 1 code valide |
| X2 | Timer visuel précis | Barre de progression synchro avec expiration réelle |
| X3 | Navigation liste services | Sélection d'un service → code correct |
| X4 | Deep sleep recovery | Après 1h de deep sleep, code toujours correct |

---

## 6. Scénarios de test détaillés

### Test T1 : Match immédiat

```bash
# Générer le code
python totp_reference.py --secret-file github-totp-secret.txt

# Ouvrir https://github.com/login dans un navigateur privé
# Entrer identifiants
# Quand demandé le code 2FA, entrer le code affiché par le script
# Résultat attendu : connexion réussie
```

### Test T2 : Séquence de codes

```bash
# Mode watch pendant 3 minutes
python totp_reference.py --secret-file github-totp-secret.txt --watch

# Tenter de se connecter à GitHub à t=0s, t=30s, t=60s, t=90s, t=120s
# Résultat attendu : 5 connexions réussies sur 5
```

### Test T3 : Drift

```bash
# Si un code ne fonctionne pas :
python totp_reference.py --secret-file github-totp-secret.txt --drift

# Entrer le code affiché par GitHub/Authy
# Le script calcule le décalage
# Résultat attendu : drift < 30s (si > 30s, problème de secret ou d'horloge)
```

### Test T4 : Transition de fenêtre

```bash
# Générer un code à t=28s (2s avant expiration)
# Tenter connexion à t=28s → devrait fonctionner
# Attendre t=32s (2s après expiration)
# Tenter connexion avec le NOUVEAU code → devrait fonctionner
# Tenter connexion avec l'ANCIEN code → devrait échouer
```

---

## 7. Journal de test (à maintenir)

Chaque session de test est loguée dans `test-log.md` :

```markdown
## Session 2026-08-30

### Setup
- Service : GitHub (x4pro-totp-test)
- Secret : `JBSWY3DPEHPK3PXP` (exemple)
- Outil : totp_reference.py v1.2

### Résultats
| Test | Résultat | Notes |
|------|----------|-------|
| T1 | ✅ PASS | Connexion réussie au premier essai |
| T2 | ✅ PASS | 5/5 codes validés |
| T3 | ✅ PASS | Drift = 0s |
| T4 | ✅ PASS | Ancien code rejeté après 32s |

### Problèmes rencontrés
- Aucun

### Actions
- Secret exporté en C array
- Prêt pour Phase 4 (ESP-IDF)
```

---

## 8. Risques et mitigations

| Risque | Probabilité | Impact | Mitigation |
|--------|-------------|--------|------------|
| Lockout du compte GitHub | Moyenne | Moyen | Codes de recovery sauvegardés ×2 |
| Secret Base32 mal copié | Élevée | Moyen | Double-vérification caractère par caractère |
| Drift horaire important | Faible | Élevé | Test T3 systématique, alerte si > 10s |
| Compte GitHub suspendu | Faible | Faible | Compte jetable, recréable en 5 min |

---

*Document à mettre à jour après chaque session de test.*
