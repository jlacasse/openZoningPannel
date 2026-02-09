# Logique de Contrôle - Système de Zonage HVAC

## Vue d'ensemble

Ce document décrit la logique de contrôle pour un système de chauffage, ventilation et climatisation (HVAC) à 6 zones avec gestion intelligente des priorités, protection contre les cycles courts et contrôle automatique des clapets.

## États des zones

Chaque zone peut être dans l'un des états suivants (par ordre de priorité) :

| État | Code | Priorité | Description |
|------|------|----------|-------------|
| **ERROR** | 99 | - | Erreur détectée (Y1/Y2 sans G) |
| **PURGE** | 6 | Haute | Purge du système (5 minutes) |
| **HEATING_STAGE2** | 5 | 4 | Chauffage 2e étage |
| **HEATING_STAGE1** | 4 | 4 | Chauffage 1er étage |
| **COOLING_STAGE2** | 3 | 2 | Climatisation 2e étage |
| **COOLING_STAGE1** | 2 | 2 | Climatisation 1er étage |
| **FAN_ONLY** | 1 | 1 | Ventilation seulement |
| **WAIT** | 7 | - | En attente (priorité inférieure) |
| **OFF** | 0 | 0 | Éteint |

## Architecture de contrôle

Le système utilise 4 passes d'analyse exécutées toutes les 10 secondes :

### PASS 1 : Calcul d'état des zones

**Objectif** : Déterminer l'état désiré de chaque zone basé sur les entrées du thermostat.

**Logique** :
- Lecture des entrées : `Y1`, `Y2`, `G`, `OB` pour chaque zone
- Détection d'erreurs : Si `Y1` ou `Y2` actif sans `G` (ventilateur)
  - 2 cycles consécutifs requis pour confirmer l'erreur
  - État = `ERROR` si confirmé
- Détermination du mode :
  - `Y2 + G + OB` → `HEATING_STAGE2`
  - `Y1 + G + OB` → `HEATING_STAGE1`
  - `Y2 + G + !OB` → `COOLING_STAGE2`
  - `Y1 + G + !OB` → `COOLING_STAGE1`
  - `G` seulement → `FAN_ONLY`
  - Rien → `OFF`

**Macro utilisée** : `CALC_ZONE_STATE(N)`

### PASS 1.5 : Protection contre les cycles courts

**Objectif** : Prévenir les cycles rapides qui peuvent endommager l'équipement HVAC.

**Logique** :
- Enregistrement du temps de démarrage quand une zone passe à un état actif (chauffage/climatisation)
- Temps minimum de cycle configuré via `min_cycle_time_ms`
- Si une zone tente de s'arrêter avant la fin du temps minimum :
  - La zone reste dans son état actuel
  - Flag `short_cycle_protection` activé
- La protection se désactive automatiquement une fois le temps minimum écoulé
- Les erreurs annulent immédiatement la protection

**Macro utilisée** : `SHORT_CYCLE_PROTECTION(N)`

### PASS 2 : Gestion intelligente des purges multi-zones

**Objectif** : Assurer qu'une seule zone purge à la fois - la dernière à s'arrêter.

**Principe** :
La purge est une phase de 5 minutes où le ventilateur continue de fonctionner après l'arrêt du chauffage/climatisation pour évacuer l'air résiduel.

**Logique** :
1. Détection des transitions :
   - Zone était en chauffage/climatisation ET maintenant arrêtée
2. Vérification du temps minimum de cycle :
   - Si non respecté → rester dans l'état actuel
3. Comptage des zones actives restantes :
   - Si d'autres zones chauffent/refroidissent encore → `OFF` immédiat
   - Si c'est la dernière zone → démarrer purge de 5 minutes
4. Gestion du timer de purge :
   - `purge_end_ms` = temps actuel + 300000 ms (5 min)
   - État = `PURGE` jusqu'à expiration du timer

**Macro utilisée** : `PURGE_MANAGEMENT(N)`

**Avantages** :
- Évite les purges multiples simultanées
- Optimise l'efficacité énergétique
- Réduit l'usure du système

### PASS 3 : Analyse de priorité et états d'attente

**Objectif** : Résoudre les conflits quand plusieurs zones demandent des modes différents.

**Hiérarchie des priorités** :
```
PURGE (6) > HEATING (4) > COOLING (2) > FAN (1) > OFF (0)
```

**Logique** :
1. Calcul de la priorité de chaque zone
2. Détermination de la priorité globale maximale
3. Application de l'état `WAIT` :
   - Zones avec priorité > 0 mais < priorité globale → `WAIT`
   - Les zones `OFF` et `ERROR` restent inchangées

**Exemple** :
- Zone 1 demande `HEATING` (priorité 4)
- Zone 2 demande `COOLING` (priorité 2)
- Zone 3 demande `FAN` (priorité 1)

**Résultat** :
- Zone 1 : `HEATING` (priorité maximale)
- Zone 2 : `WAIT` (priorité inférieure)
- Zone 3 : `WAIT` (priorité inférieure)

### PASS 4 : Contrôle des clapets

**Objectif** : Contrôler l'ouverture/fermeture des clapets de chaque zone.

**Logique de décision** :

| Condition | Position du clapet |
|-----------|-------------------|
| État = `WAIT` ou `ERROR` | Fermé (0) |
| Toutes les zones `OFF` | Ouvert (1) |
| État = `OFF` | Fermé (0) |
| État actif | Ouvert (1) |

**Implémentation** :
- Détection du changement d'état requis
- Exécution du script approprié :
  - `zN_damper_open` → ouvre le clapet
  - `zN_damper_close` → ferme le clapet
- Scripts avec délai de 250ms pour relâchement du moteur

**Macro utilisée** : `DAMPER_CONTROL(N)`

## Scripts de contrôle des clapets

Chaque zone possède 2 scripts :

### Script d'ouverture (`zN_damper_open`)
```
1. Éteindre les deux sorties (open et close)
2. Attendre 250ms (relâchement moteur)
3. Activer la sortie open
```

### Script de fermeture (`zN_damper_close`)
```
1. Éteindre les deux sorties (open et close)
2. Attendre 250ms (relâchement moteur)
3. Activer la sortie close
```

**Mode** : `single` - ne peut s'exécuter qu'une fois à la fois

## Initialisation au démarrage

Au démarrage du système (priorité -100) :

1. **Initialisation des états** :
   - Toutes les zones → `OFF` (0)
   - Tous les drapeaux d'état → 0
   - Compteurs d'erreur → 0

2. **Position des clapets** :
   - Tous les états de clapets → ouvert (1)
   - Exécution des scripts d'ouverture pour toutes les zones

**Objectif** : Éviter les états "unknown" dans Home Assistant

## Détection et gestion des erreurs

### Types d'erreurs

**Erreur de configuration thermostat** :
- `Y1` ou `Y2` actif sans `G` (ventilateur)
- Indication d'un problème de câblage ou de configuration

### Processus de confirmation

1. **Premier cycle d'erreur** :
   - Incrémenter compteur d'erreur
   - Log avertissement (WARN)

2. **Deuxième cycle d'erreur** :
   - Confirmer l'erreur
   - État → `ERROR`
   - Log erreur (ERROR)
   - Flag global `zone_error_flag` = true

3. **Récupération** :
   - Dès que la condition d'erreur disparaît
   - Compteur d'erreur → 0
   - Log information de récupération
   - Calcul normal d'état reprend

### Impact sur le système

- État `ERROR` ignore la protection contre les cycles courts
- Clapet de la zone en erreur se ferme
- La zone ne participe pas aux calculs de priorité

## Variables globales utilisées

### Par zone (N = 1 à 6)

| Variable | Type | Description |
|----------|------|-------------|
| `zN_state` | int | État actuel de la zone |
| `zN_damper_state` | int | État du clapet (0=fermé, 1=ouvert) |
| `zN_error_count` | int | Compteur d'erreurs consécutives |
| `zN_active_start_ms` | unsigned long | Temps de démarrage du cycle actif |
| `zN_purge_end_ms` | unsigned long | Temps de fin de purge |
| `zN_short_cycle_protection` | bool | Flag de protection active |

### Globales

| Variable | Type | Description |
|----------|------|-------------|
| `zone_error_flag` | bool | Indicateur d'erreur globale |
| `min_cycle_time_ms` | unsigned long | Temps minimum de cycle (ms) |

## Macros C++ utilisées

Le système utilise des macros avec concaténation de tokens (`##`) pour générer du code répétitif :

| Macro | Fonction |
|-------|----------|
| `CALC_ZONE_STATE(N)` | Calcul d'état d'une zone |
| `SHORT_CYCLE_PROTECTION(N)` | Protection cycle court |
| `PURGE_MANAGEMENT(N)` | Gestion de la purge |
| `DAMPER_CONTROL(N)` | Contrôle du clapet |

**Exemple** : `CALC_ZONE_STATE(1)` génère le code pour la zone 1

## Paramètres de configuration

| Paramètre | Valeur par défaut | Description |
|-----------|-------------------|-------------|
| Intervalle de mise à jour | 10 secondes | Fréquence d'exécution de la logique |
| Durée de purge | 300000 ms (5 min) | Temps de purge après arrêt |
| Délai relâchement moteur | 250 ms | Pause avant activation clapet |
| Temps minimum de cycle | Configurable via `min_cycle_time_ms` | Protection équipement |

## Logs et débogage

### Tags de logging

| Tag | Utilisation |
|-----|-------------|
| `ZoneError` | Erreurs de zones et récupération |
| `ShortCycleProtection` | Protection cycles courts |
| `DamperControl` | Contrôle des clapets |

### Niveaux de log

- **ERROR** : Erreurs confirmées
- **WARN** : Avertissements, protection active
- **INFO** : Changements d'état, récupération

## Flux de décision simplifié

```
┌─────────────────────────┐
│   Lecture entrées       │
│   thermostat (10s)      │
└───────────┬─────────────┘
            │
            ▼
┌─────────────────────────┐
│  PASS 1: Calcul état    │
│  (détection erreurs)    │
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
│  clapets                │
└───────────┬─────────────┘
            │
            ▼
┌─────────────────────────┐
│  Mise à jour sorties    │
│  et capteurs texte      │
└─────────────────────────┘
```

## Cas d'usage typiques

### Cas 1 : Démarrage simple d'une zone

1. Thermostat zone 1 active `Y1 + G` (chauffage stage 1)
2. PASS 1 : État calculé = `HEATING_STAGE1`
3. PASS 1.5 : Enregistrement temps démarrage
4. PASS 2 : Pas de purge (démarrage)
5. PASS 3 : Priorité 4 (max) → reste `HEATING_STAGE1`
6. PASS 4 : Clapet zone 1 s'ouvre

### Cas 2 : Conflit chauffage/climatisation

**Situation** :
- Zone 1 demande chauffage (priorité 4)
- Zone 2 demande climatisation (priorité 2)

**Résolution** :
1. PASS 3 détecte priorité max = 4 (chauffage)
2. Zone 1 : reste `HEATING`
3. Zone 2 : passe à `WAIT`
4. Clapet zone 1 ouvert, clapet zone 2 fermé
5. Quand zone 1 s'arrête, zone 2 peut refroidir

### Cas 3 : Purge intelligente multi-zones

**Situation** :
- Zones 1, 2, 3 chauffent ensemble
- Zone 1 s'éteint en premier

**Résolution** :
1. Zone 1 veut passer à `OFF`
2. PASS 2 détecte zones 2 et 3 encore actives
3. Zone 1 → `OFF` immédiat (pas de purge)
4. Zone 2 s'éteint ensuite
5. PASS 2 détecte zone 3 encore active
6. Zone 2 → `OFF` immédiat (pas de purge)
7. Zone 3 s'éteint en dernier
8. PASS 2 détecte qu'elle est la dernière
9. Zone 3 → `PURGE` pour 5 minutes

### Cas 4 : Protection cycle court

**Situation** :
- Zone 1 chauffe depuis 2 minutes
- Temps minimum = 5 minutes
- Thermostat demande arrêt

**Résolution** :
1. PASS 1.5 détecte arrêt prématuré
2. Zone 1 maintenue en `HEATING`
3. Flag `short_cycle_protection` = true
4. Après 5 minutes totales, autorisation d'arrêt
5. Zone 1 peut passer à `OFF` ou `PURGE`

## Points d'attention

### ⚠️ Sécurité
- Les erreurs sont confirmées sur 2 cycles pour éviter les faux positifs
- Protection contre les cycles courts protège l'équipement
- Les clapets s'ouvrent tous au démarrage (position sécuritaire)

### 🔧 Maintenance
- Vérifier régulièrement les logs d'erreurs
- Ajuster `min_cycle_time_ms` selon le type d'équipement
- Tester la logique de purge avec plusieurs zones

### 📊 Performance
- Cycle de 10 secondes est un bon compromis réactivité/charge
- Les macros optimisent la taille du code
- Logging conditionnel évite la surcharge

## Améliorations futures

Le code contient une section commentée pour le contrôle de l'unité centrale :

```cpp
// ============= OUTPUT CONTROL LOGIC (COMMENTED FOR LATER) =============
// TODO: Implement central unit control algorithm
```

Cette section permettra de :
- Contrôler les sorties physiques (G, Y1, Y2, OB)
- Gérer les modes de la géothermie
- Optimiser la gestion multi-zones

---

*Document généré le 8 février 2026*  
*Version du système : openZoningPannel*
