# Logique de Contrôle - Système de Zonage HVAC

## Vue d'ensemble

Ce document décrit la logique de contrôle pour un système de chauffage, ventilation et climatisation (HVAC) à 6 zones avec gestion intelligente des priorités, protection contre les cycles courts et contrôle automatique des clapets.

L'ensemble de la logique est implémenté dans le composant ESPHome C++ `open_zoning`, sous forme de 5 passes d'analyse exécutées toutes les 10 secondes par la méthode `update()` de `OpenZoningController`.

## Architecture C++

```
components/
└── open_zoning/
    ├── __init__.py          Schema YAML + codegen Python
    ├── open_zoning.h        Classe OpenZoningController (PollingComponent)
    ├── open_zoning.cpp      Logique 5 passes (setup, update, apply_mode)
    └── zone.h               Struct Zone + enum ZoneState + méthodes par zone
```

### Fichiers YAML (packages)

| Fichier | Rôle |
|---------|------|
| `packages/base.yml` | Config ESPHome de base (board, logger, etc.) |
| `packages/configurations.yml` | I2C, WiFi, API, OTA, MCP23017 |
| `packages/binary_sensors.yml` | Mapping GPIO des entrées thermostat (Y1, Y2, G, OB × 6 zones) |
| `packages/switches.yml` | Mapping GPIO des sorties (dampers, LEDs, Out_Y1/Y2/G/OB/W) |
| `packages/select.yml` | Entité `select` pour affichage du mode dans Home Assistant |
| `packages/component.yml` | Déclaration `external_components` + configuration `open_zoning:` |

## États des zones

Chaque zone peut être dans l'un des états suivants (définis dans `zone.h` : `enum class ZoneState`) :

| État | Code | Priorité | Description |
|------|------|----------|-------------|
| **ERROR** | 99 | - | Erreur détectée (Y1/Y2 sans G) |
| **PURGE** | 6 | 6 | Purge du système (durée configurable) |
| **HEATING_STAGE2** | 5 | 4 | Chauffage 2e étage |
| **HEATING_STAGE1** | 4 | 4 | Chauffage 1er étage |
| **COOLING_STAGE2** | 3 | 2 | Climatisation 2e étage |
| **COOLING_STAGE1** | 2 | 2 | Climatisation 1er étage |
| **FAN_ONLY** | 1 | 1 | Ventilation seulement |
| **WAIT** | 7 | - | En attente (priorité inférieure) |
| **OFF** | 0 | 0 | Éteint |

## Passes d'analyse

Le système utilise 5 passes exécutées toutes les 10 secondes dans `OpenZoningController::update()` :

### PASS 1 : Calcul d'état des zones (`pass1_calc_zone_states_()`)

**Méthode par zone** : `Zone::calc_state()`

**Logique** :
- Lecture des entrées : `Y1`, `Y2`, `G`, `OB` via les pointeurs `binary_sensor::BinarySensor*`
- Détection d'erreurs : Si `Y1` ou `Y2` actif sans `G` (ventilateur)
  - 2 cycles consécutifs requis pour confirmer l'erreur (`error_count`)
  - État = `ERROR` si confirmé
- Détermination du mode (par priorité décroissante) :
  - `Y2 + G + OB` → `HEATING_STAGE2`
  - `Y1 + G + OB` → `HEATING_STAGE1`
  - `Y2 + G + !OB` → `COOLING_STAGE2`
  - `Y1 + G + !OB` → `COOLING_STAGE1`
  - `G` seulement → `FAN_ONLY`
  - Rien → `OFF`

### PASS 1.5 : Protection contre les cycles courts (`pass1_5_short_cycle_protection_()`)

**Méthode par zone** : `Zone::apply_short_cycle_protection(current_time, min_cycle_time_ms)`

**Logique** :
- Enregistrement du temps de démarrage (`active_start_ms`) quand une zone passe à un état actif
- Temps minimum de cycle configuré via le paramètre `min_cycle_time` (défaut : 480s)
- Si une zone tente de s'arrêter avant la fin du temps minimum :
  - La zone reste dans son état actuel (`state_new = state`)
  - Flag `short_cycle_protection` activé
- Les erreurs annulent immédiatement la protection

### PASS 2 : Gestion intelligente des purges multi-zones (`pass2_purge_management_()`)

**Principe** : Seule la dernière zone à s'arrêter purge. Durée configurable via `purge_duration` (défaut : 300s).

**Logique** :
1. Comptage des zones actives (chauffage/climatisation) via `is_heating()` / `is_cooling()`
2. Pour chaque zone transitionnant d'actif à arrêt :
   - Vérification du temps minimum de cycle
   - Si d'autres zones du même type sont encore actives → `OFF` immédiat
   - Si c'est la dernière zone → démarrer purge (`purge_end_ms = now + purge_duration`)
3. Gestion du timer de purge actif

### PASS 3 : Analyse de priorité et états d'attente (`pass3_priority_analysis_()`)

**Hiérarchie** (via `state_to_priority()` dans `zone.h`) :
```
PURGE (6) > HEATING (4) > COOLING (2) > FAN (1) > OFF (0)
```

**Logique** :
1. Calcul de la priorité de chaque zone via `Zone::get_priority()`
2. Détermination de `global_max_priority_`
3. Zones avec priorité > 0 mais < max → `WAIT`
4. Les zones `OFF` et `ERROR` restent inchangées

### PASS 4 : Contrôle des clapets (`pass4_damper_control_()`)

**Logique** :

| Condition | Position du clapet |
|-----------|-------------------|
| État = `WAIT` ou `ERROR` | Fermé |
| Toutes les zones `OFF` | Ouvert (sécurité) |
| État = `OFF` (d'autres actives) | Fermé |
| État actif (HEATING/COOLING/FAN/PURGE) | Ouvert |

**Contrôle moteur** (`open_damper_()` / `close_damper_()`) :
- Éteindre les deux sorties (open + close) immédiatement
- Après 250ms via `set_timeout()`, activer la direction voulue
- Remplace les 12 scripts ESPHome de l'ancien code

### PASS 5 : Contrôle de l'unité centrale (`pass5_output_control_()`)

**Actif uniquement si `auto_mode_ = true`.**

**Logique** :
1. **Erreur** → mode Arrêt + LED erreur ON
2. **`global_max_priority_` = 0** → Arrêt
3. **= 1** → Fan1
4. **= 2** → Clim Stage 1 ou 2 (si une zone demande Stage 2)
5. **= 4** → Chauffage Stage 1 ou 2 (idem)
6. **= 6** → Purge Chauffage ou Clim (selon `last_active_mode_`)

**Escalation Stage 2** : Timer `stage1_start_ms_`. Si en Stage 1 depuis plus de `stage2_escalation_delay` (défaut : 3600s) → auto-escalation vers Stage 2.

**Application du mode** (`apply_mode_(mode)`) :
- Drive les 7 sorties (Y1, Y2, G, OB, W1e, W2, W3) et 4 LEDs
- Synchronise l'entité `select` dans Home Assistant via `make_call().set_index()`

## Initialisation au démarrage (`setup()`)

1. Initialisation de toutes les zones : état `OFF`, damper ouvert, compteurs à 0
2. Ouverture de tous les clapets via `open_damper_(i)` (position sécuritaire)
3. Mode initial : Arrêt

## Détection et gestion des erreurs

### Type d'erreur détecté
- `Y1` ou `Y2` actif sans `G` (ventilateur) → problème de câblage ou thermostat

### Processus de confirmation (dans `Zone::calc_state()`)
1. **1er cycle** : `error_count++`, log WARN
2. **2e cycle** : état → `ERROR`, log ERROR, `zone_error_flag_` = true
3. **Récupération** : dès que la condition disparaît, `error_count` → 0

### Impact
- Clapet fermé, zone exclue des calculs de priorité
- PASS 5 force l'unité en Arrêt si `zone_error_flag_` est actif

## Paramètres de configuration

Configurés dans `component.yml` et passés au composant via `__init__.py` :

| Paramètre | Clé YAML | Défaut | Description |
|-----------|----------|--------|-------------|
| Intervalle de mise à jour | `update_interval` | 10s | Fréquence d'exécution des 5 passes |
| Temps minimum de cycle | `min_cycle_time` | 480s (8 min) | Protection équipement |
| Durée de purge | `purge_duration` | 300s (5 min) | Temps de purge après arrêt |
| Délai escalation Stage 2 | `stage2_escalation_delay` | 3600s (1h) | Timer avant auto-escalation |
| Mode automatique | `auto_mode` | true | PASS 5 active ou non |

## Logs et débogage

### Tag unique : `open_zoning`

Tous les logs utilisent le tag `open_zoning` avec les niveaux suivants :

| Niveau | Utilisation |
|--------|-------------|
| **ERROR** | Erreurs de zones confirmées, forçage arrêt |
| **WARN** | Protection cycle court active, escalation Stage 2 |
| **INFO** | Changements d'état, démarrage/fin de purge, changements de mode |
| **DEBUG** | Heartbeat cycle, détails de mode appliqué |
| **CONFIG** | `dump_config()` — toutes les entités liées |

## Flux de décision

```
┌─────────────────────────┐
│   update() toutes les   │
│   10 secondes           │
└───────────┬─────────────┘
            │
            ▼
┌─────────────────────────┐
│  PASS 1: calc_state()   │
│  par zone (erreurs)     │
└───────────┬─────────────┘
            │
            ▼
┌─────────────────────────┐
│  PASS 1.5: Protection   │
│  cycles courts          │
└───────────┬─────────────┘
            │
            ▼
┌─────────────────────────┐
│  PASS 2: Gestion purge  │
│  (dernière zone only)   │
└───────────┬─────────────┘
            │
            ▼
┌─────────────────────────┐
│  PASS 3: Priorités      │
│  (WAIT si conflit)      │
└───────────┬─────────────┘
            │
            ▼
┌─────────────────────────┐
│  PASS 4: Contrôle       │
│  clapets (set_timeout)  │
└───────────┬─────────────┘
            │
            ▼
┌─────────────────────────┐
│  PASS 5: Contrôle       │
│  unité centrale         │
│  (LEDs + sorties)       │
└───────────┬─────────────┘
            │
            ▼
┌─────────────────────────┐
│  Commit des états       │
│  + log des transitions  │
└─────────────────────────┘
```

## Cas d'usage typiques

### Cas 1 : Démarrage simple d'une zone

1. Thermostat zone 1 active `Y1 + G` (chauffage stage 1)
2. PASS 1 : `calc_state()` → `HEATING_STAGE1`
3. PASS 1.5 : Enregistrement `active_start_ms`
4. PASS 2 : Pas de purge (démarrage)
5. PASS 3 : Priorité 4 (max) → reste `HEATING_STAGE1`
6. PASS 4 : Clapet zone 1 s'ouvre via `open_damper_(0)`
7. PASS 5 : Mode → Chauffage Stage 1, `apply_mode_(4)`

### Cas 2 : Conflit chauffage/climatisation

- Zone 1 demande chauffage (priorité 4), Zone 2 demande climatisation (priorité 2)
- PASS 3 : max = 4 → Zone 2 passe en `WAIT`
- PASS 4 : Clapet zone 1 ouvert, clapet zone 2 fermé
- PASS 5 : Mode chauffage appliqué

### Cas 3 : Purge intelligente multi-zones

- Zones 1, 2, 3 chauffent → zone 1 s'éteint en premier → `OFF` (pas de purge)
- Zone 2 s'éteint → `OFF` (zone 3 encore active)
- Zone 3 s'éteint en dernier → `PURGE` pendant `purge_duration`

### Cas 4 : Protection cycle court

- Zone 1 chauffe depuis 2 min, temps minimum = 8 min, thermostat demande arrêt
- `apply_short_cycle_protection()` maintient zone 1 en `HEATING`
- Après 8 min totales → autorisation d'arrêt ou purge

---

*Document mis à jour le 12 février 2026 — architecture C++ (composant open_zoning)*
