# Plan d'optimisations - openZoningPannel

*Document créé le 11 février 2026*

## Vue d'ensemble

Ce document recense les optimisations identifiées pour améliorer la fiabilité, la maintenabilité et la flexibilité du système de zonage HVAC. Le système est actuellement stable et fonctionnel.

La refactorisation majeure (#0) consiste à migrer le lambda monolithique de 614 lignes vers un composant C++ ESPHome. Cette migration est découpée en phases et facilitera toutes les optimisations subséquentes.

---

## 🔴 Refactorisation majeure

### 0. Migration vers un composant C++ ESPHome

- **État global** : ⬜ En cours
- **Approche** : Référencer les entités YAML existantes via `cv.use_id()` (approche la plus simple — pas de création d'entités côté C++)
- **Bénéfice global** : Élimine la duplication de macros, rend le nombre de zones configurable (1-6), permet l'OOP, et rend toutes les optimisations #1-#10 triviales à implémenter.

#### Structure cible

```
components/
└── open_zoning/
    ├── __init__.py          (~150 lignes)  Schema YAML + codegen Python
    ├── open_zoning.h        (~100 lignes)  Classe OpenZoningController (PollingComponent)
    ├── open_zoning.cpp      (~350 lignes)  Logique 5 passes (setup, update)
    └── zone.h               (~80 lignes)   Struct Zone + enum ZoneState
```

#### Impact sur les fichiers existants

| Fichier actuel | Impact |
|---|---|
| `packages/base.yml` | ✅ Inchangé |
| `packages/binary_sensors.yml` | ✅ Inchangé — mapping GPIO matériel |
| `packages/switches.yml` | ✅ Inchangé — mapping GPIO matériel |
| `packages/configurations.yml` | ⚠️ Simplifié — les `number` et `switch` template absorbés par le composant |
| `packages/select.yml` | ⚠️ Simplifié — le `on_value` lambda (~120 lignes) migre en C++ |
| `packages/globals.yml` | ❌ Supprimé — les 30+ globals deviennent des membres de classe |
| `packages/automation.yml` | ❌ Supprimé — le lambda 614 lignes + 12 scripts migrent en C++ |
| `packages/text_sensors.yml` | ❌ Supprimé — créés par le composant |
| **`packages/component.yml`** | ➕ Nouveau — déclaration `external_components` + config `open_zoning:` |

#### Risques identifiés

| Risque | Sévérité | Mitigation |
|---|---|---|
| Délai damper 250ms (`delay()` interdit dans un Component) | 🔴 | Utiliser `set_timeout("damper_zN", 250, callback)` |
| Mémoire ESP8266 (vtable + instances ~200-400 bytes) | 🟡 | Profiler avec capteur `debug` heap avant/après |
| Courbe d'apprentissage Python codegen (`__init__.py`) | 🟡 | Suivre le pattern de `esphome-tesla-ble` comme référence |
| Système HVAC critique — erreur = dommage équipement | 🔴 | Déployer en parallèle, comparer logs |

---

#### Phase 0A : Scaffolding du composant ✅

- **Description** : Créer la structure de fichiers `components/open_zoning/` avec un composant C++ minimal qui compile et se connecte sans rien faire.
- **Fichiers à créer** :
  - `components/open_zoning/__init__.py` — `CONFIG_SCHEMA` minimal avec `cv.polling_component_schema("10s")`
  - `components/open_zoning/open_zoning.h` — classe `OpenZoningController` héritant de `PollingComponent` avec `setup()`, `update()`, `dump_config()` vides
  - `components/open_zoning/open_zoning.cpp` — implémentation vide, log "OpenZoning initialized" dans `setup()`
  - `components/open_zoning/zone.h` — `enum class ZoneState` + struct `Zone` avec membres de base
- **Fichiers à créer/modifier côté YAML** :
  - Créer `packages/component.yml` — déclare `external_components` (source locale) et bloc `open_zoning:` minimal
  - Modifier `esphome/geothermie_example.yaml` — ajouter le package `component`
- **Critère de réussite** : Le firmware compile, boote, et affiche "OpenZoning initialized" dans les logs.

#### Phase 0B : Référencement des entités existantes ✅

- **Description** : Ajouter au `CONFIG_SCHEMA` les références vers les binary sensors (entrées thermostats), switches (dampers, LEDs, sorties) et le select (mode). Le composant les reçoit via `cv.use_id()` et les stocke sans encore les utiliser.
- **Fichiers à modifier** :
  - `components/open_zoning/__init__.py` — ajouter `ZONE_SCHEMA` (y1, y2, g, ob, damper_open, damper_close) + références outputs/LEDs/select
  - `components/open_zoning/open_zoning.h` — ajouter les setters et pointeurs membres
  - `components/open_zoning/open_zoning.cpp` — `dump_config()` affiche les entités liées
  - `packages/component.yml` — configuration complète avec les `id` de toutes les entités
- **Critère de réussite** : `dump_config()` affiche correctement les 6 zones avec leurs entités associées.

#### Phase 0C : Migration de la logique PASS 1-3 ✅

- **Description** : Porter les passes 1 (calcul d'état), 1.5 (cycle court), 2 (purge) et 3 (priorités) du lambda vers les méthodes C++ de `OpenZoningController` et `Zone`. Les macros C deviennent des méthodes appelées dans une boucle `for`.
- **Fichiers à modifier** :
  - `components/open_zoning/zone.h` — ajouter `calc_state()`, `apply_short_cycle_protection()`, `get_priority()` à la struct `Zone`
  - `components/open_zoning/open_zoning.cpp` — implémenter `pass1_calc_zone_states_()`, `pass1_5_short_cycle_protection_()`, `pass2_purge_management_()`, `pass3_priority_analysis_()` dans `update()`
- **L'ancien `automation.yml` reste actif en parallèle** pour comparaison de logs.
- **Critère de réussite** : Les logs du composant C++ montrent les mêmes calculs d'état que le lambda existant.

#### Phase 0D : Migration PASS 4-5 + scripts damper ⬜

- **Description** : Porter le contrôle des clapets (PASS 4) et le contrôle de l'unité centrale (PASS 5) en C++. Remplacer les 12 scripts ESPHome de damper par des méthodes utilisant `set_timeout()` pour le délai de 250ms.
- **Fichiers à modifier** :
  - `components/open_zoning/open_zoning.cpp` — implémenter `pass4_damper_control_()`, `pass5_output_control_()`, `open_damper_(int zone)`, `close_damper_(int zone)`, `apply_mode_(int mode)`
  - `components/open_zoning/open_zoning.h` — déclarer les nouvelles méthodes
- **Critère de réussite** : Le composant C++ contrôle correctement les clapets et les sorties de l'unité centrale. Comportement identique au lambda.

#### Phase 0E : Nettoyage et suppression de l'ancien code ⬜

- **Description** : Supprimer les fichiers rendus obsolètes et finaliser la migration.
- **Fichiers à supprimer** :
  - `packages/globals.yml`
  - `packages/automation.yml`
  - `packages/text_sensors.yml`
- **Fichiers à simplifier** :
  - `packages/select.yml` — retirer le `on_value` lambda (géré par le composant)
  - `packages/configurations.yml` — retirer les `number` et `switch` template (gérés par le composant)
- **Fichiers à mettre à jour** :
  - `esphome/geothermie_example.yaml` — retirer les packages supprimés
  - `LOGIQUE_CONTROLE.md` — mettre à jour pour refléter l'architecture C++
  - `README.md` — documenter l'utilisation du composant
- **Critère de réussite** : Le firmware fonctionne exclusivement avec le composant C++. Aucun lambda, global ou script damper résiduel.

---

## 🟢 Priorité Haute

### 1. Durée de purge configurable
- **Fichier(s)** : `packages/automation.yml`, `packages/configurations.yml`, `packages/globals.yml`
- **État** : ⬜ À faire
- **Description** : La durée de purge est hardcodée à 5 minutes (`PURGE_DURATION_MS = 300000`) dans le lambda de la PASS 2. Ajouter un `number` template configurable depuis Home Assistant, comme `min_cycle_time` et `stage2_escalation_delay`.
- **Bénéfice** : Permettre à l'utilisateur d'ajuster la durée de purge selon son installation sans recompiler.

### 2. Zones enable/disable
- **Fichier(s)** : `packages/configurations.yml`, `packages/automation.yml`, `packages/globals.yml`
- **État** : ⬜ À faire
- **Description** : Ajouter un switch par zone (`Zone X Enabled`) permettant de désactiver une zone inutilisée. Une zone désactivée serait ignorée dans tous les calculs (PASS 1-5) et son clapet resterait ouvert.
- **Bénéfice** : Adaptabilité à différentes installations (3, 4, 5 ou 6 zones).

### 3. Capteurs de diagnostic
- **Fichier(s)** : `packages/text_sensors.yml` ou nouveau fichier `packages/sensors.yml`
- **État** : ⬜ À faire
- **Description** : Ajouter des capteurs de monitoring :
  - Nombre de zones actives (sensor)
  - Temps écoulé en Stage 1 avant escalation (sensor)
  - Uptime de l'ESP (sensor, plateforme `uptime`)
  - Free Heap memory (sensor, plateforme `debug`)
  - Protection cycle court active (binary_sensor global)
  - Compteur de changements de mode (sensor)
- **Bénéfice** : Meilleure observabilité et détection proactive des problèmes.

---

## 🟡 Priorité Moyenne

### 4. Macros PRIORITY_CALC et APPLY_WAIT
- **Fichier(s)** : `packages/automation.yml`
- **État** : ⬜ À faire (rendu obsolète par #0 — les macros deviennent des boucles `for` en C++)
- **Description** : Le calcul de priorité (PASS 3) est copié-collé 6 fois. Créer des macros :
  ```cpp
  #define CALC_PRIORITY(N) \
    if (z##N##_state_new == ZONE_PURGE) z##N##_priority = 6; \
    else if (z##N##_state_new == ZONE_HEATING_STAGE1 || z##N##_state_new == ZONE_HEATING_STAGE2) z##N##_priority = 4; \
    else if (z##N##_state_new == ZONE_COOLING_STAGE1 || z##N##_state_new == ZONE_COOLING_STAGE2) z##N##_priority = 2; \
    else if (z##N##_state_new == ZONE_FAN_ONLY) z##N##_priority = 1;

  #define APPLY_WAIT(N) \
    if (z##N##_priority > 0 && z##N##_priority < global_max_priority && z##N##_state_new != ZONE_ERROR) \
      z##N##_state_new = ZONE_WAIT;
  ```
- **Bénéfice** : Réduction de la duplication, meilleure maintenabilité.

### 5. Persistance de `last_active_mode` au redémarrage
- **Fichier(s)** : `packages/globals.yml`, `packages/automation.yml`
- **État** : ⬜ À faire
- **Description** : Sauvegarder `last_active_mode` en flash via `ESPPreferenceObject` pour que la direction de purge survive un redémarrage. Actuellement, après un reboot pendant une purge, le système ne sait plus si c'était chauffage ou climatisation.
- **Bénéfice** : Fiabilité de la purge post-reboot.

### 6. Clarification des sorties W2/W3/W1e
- **Fichier(s)** : `packages/switches.yml`, `packages/select.yml`
- **État** : ⬜ À faire
- **Description** : Les sorties `Out_W2`, `Out_W3`, `Out_W1e` sont définies dans `switches.yml` mais toujours `turn_off()` dans tous les modes du `select.yml`. Soit :
  - Les intégrer à la logique (chauffage auxiliaire, chauffage d'urgence)
  - Les documenter clairement comme réservées pour usage futur
  - Les retirer si non nécessaires
- **Bénéfice** : Clarté du code, possibilité de chauffage auxiliaire.

---

## 🔵 Priorité Faible

### 7. Réduction de la consommation mémoire (types)
- **Fichier(s)** : `packages/globals.yml`
- **État** : ⬜ À faire (intégré automatiquement dans #0 — `uint8_t` natif dans la struct `Zone`)
- **Description** : Remplacer les `int` par des `uint8_t` pour les variables dont les valeurs ne dépassent jamais 255 :
  - `z*_state` (max 99)
  - `z*_error_count` (max ~10)
  - `z*_damper_state` (0 ou 1)
  - `geo_current_mode` (0-7)
  - `last_active_mode` (0-2)
- **Bénéfice** : Économie ~60 octets de RAM (marginal mais utile sur ESP8266).

### 8. Watchdog I2C pour MCP23017
- **Fichier(s)** : `packages/automation.yml` ou nouveau fichier `packages/i2c_watchdog.yml`
- **État** : ⬜ À faire
- **Description** : Les MCP23017 via I2C peuvent parfois se bloquer (bus stuck). Ajouter :
  - Un compteur d'erreurs I2C
  - Un redémarrage automatique du bus I2C après X échecs consécutifs
  - Un `binary_sensor` de santé I2C visible dans HA
- **Bénéfice** : Robustesse face aux problèmes de bus I2C.

### 9. Dé-escalation Stage 2
- **Fichier(s)** : `packages/automation.yml`
- **État** : ⬜ À faire
- **Description** : L'escalation Stage 2 est un simple timer one-way. Ajouter une logique de dé-escalation : si le nombre de zones actives diminue significativement pendant le Stage 2 (ex: de 4 zones à 1 zone), revenir à Stage 1 après un délai configurable.
- **Bénéfice** : Efficacité énergétique, éviter la surconsommation.

### 10. Validation anti-conflit dans le select
- **Fichier(s)** : `packages/select.yml`
- **État** : ⬜ À faire
- **Description** : Le `on_value` du select logue un warning quand on change manuellement le mode en auto. On pourrait soit :
  - Bloquer le changement manuel quand `geo_auto_mode` est actif
  - Désactiver temporairement l'auto mode lors d'un changement manuel
  - Ajouter un timer de retour en auto après X minutes
- **Bénéfice** : Prévention des conflits de commande.

---

## Suivi des modifications

| Date | Optimisation | Statut |
|------|-------------|--------|
| 2026-02-11 | Plan créé | ✅ |
| 2026-02-11 | #0 Migration C++ ajoutée (5 phases) | ⬜ En cours |
| 2026-02-11 | #0 Phase 0A — Scaffolding composant | ✅ En attente de validation compilation |
| 2026-02-12 | #0 Phase 0B — Référencement entités (binary sensors) | ✅ |
| 2026-02-12 | #0 Phase 0C — Migration PASS 1-3 en C++ | ✅ |

---

*Ce document sera mis à jour au fur et à mesure de l'implémentation des optimisations.*
