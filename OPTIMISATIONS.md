# Plan d'optimisations - openZoningPannel

*Document créé le 11 février 2026*

## Vue d'ensemble

Ce document recense les optimisations identifiées pour améliorer la fiabilité, la maintenabilité et la flexibilité du système de zonage HVAC. Le système est actuellement stable et fonctionnel.

La refactorisation majeure (#0) consiste à migrer le lambda monolithique de 614 lignes vers un composant C++ ESPHome. Cette migration est découpée en phases et facilitera toutes les optimisations subséquentes.

---

## 🔴 Refactorisation majeure

### 0. Migration vers un composant C++ ESPHome

- **État global** : ✅ Complète
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

#### Phase 0D : Migration PASS 4-5 + scripts damper ✅

- **Description** : Porter le contrôle des clapets (PASS 4) et le contrôle de l'unité centrale (PASS 5) en C++. Remplacer les 12 scripts ESPHome de damper par des méthodes utilisant `set_timeout()` pour le délai de 250ms.
- **Fichiers à modifier** :
  - `components/open_zoning/open_zoning.cpp` — implémenter `pass4_damper_control_()`, `pass5_output_control_()`, `open_damper_(int zone)`, `close_damper_(int zone)`, `apply_mode_(int mode)`
  - `components/open_zoning/open_zoning.h` — déclarer les nouvelles méthodes
- **Critère de réussite** : Le composant C++ contrôle correctement les clapets et les sorties de l'unité centrale. Comportement identique au lambda.

#### Phase 0E : Nettoyage et suppression de l'ancien code ✅

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
- **Fichier(s)** : `packages/configurations.yml`, `components/open_zoning/open_zoning.h`
- **État** : ✅ Fait
- **Description** : La durée de purge est hardcodée à 5 minutes (`PURGE_DURATION_MS = 300000`) dans le lambda de la PASS 2. Ajouter un `number` template configurable depuis Home Assistant, comme `min_cycle_time` et `stage2_escalation_delay`.
- **Bénéfice** : Permettre à l'utilisateur d'ajuster la durée de purge selon son installation sans recompiler.

### 2. Zones enable/disable
- **Fichier(s)** : `packages/configurations.yml`, `components/open_zoning/open_zoning.h`, `components/open_zoning/open_zoning.cpp`
- **État** : ✅ Fait
- **Description** : Ajouter un switch par zone (`Geo_zone_N_enabled`) permettant de désactiver une zone inutilisée. Une zone désactivée est ignorée dans tous les calculs (PASS 1–2.5–3), son `state_new` est forcé à `OFF` en PASS 4, et son clapet est fermé fyzicky. Les switches utilisent `restore_mode: RESTORE_DEFAULT_ON` pour survivre aux reboots. Le setter `set_zone_enabled(index, bool)` est exposé via des lambdas dans `configurations.yml`.
- **Bénéfice** : Adaptabilité à différentes installations (3, 4, 5 ou 6 zones).

### 11. Seuil de démarrage minimum (min_active_zones)
- **Fichier(s)** : `packages/configurations.yml`, `packages/component.yml`, `components/open_zoning/open_zoning.h`, `components/open_zoning/open_zoning.cpp`, `components/open_zoning/__init__.py`
- **État** : ✅ Fait
- **Description** : Nouvelle PASS 2.5 — Le système ne démarre que si N zones sont simultanément en demande (paramètre `min_active_zones`, 1–6, défaut 1 = désactivé). Un timer d'urgence (`min_demand_override_delay`, défaut 30 min) force le démarrage si une zone attend trop longtemps seule. N'interrompt pas un cycle déjà en cours.
- **Bénéfice** : Réduction des cycles courts sur les géothermies, meilleur COP, moins d'usure.

### 3. Capteurs de diagnostic
- **Fichier(s)** : `packages/sensors.yml` (nouveau), `components/open_zoning/open_zoning.h`, `components/open_zoning/open_zoning.cpp`, `components/open_zoning/__init__.py`, `packages/component.yml`
- **État** : ✅ Fait
- **Description** : Capteurs de monitoring ajoutés :
  - `Geo_active_zones` (sensor) — nombre de zones avec clapet ouvert participant au cycle (HEATING/COOLING/FAN/PURGE, exclut OFF/WAIT/ERROR)
  - `Geo_stage1_elapsed` (sensor, secondes) — temps écoulé en Stage 1 depuis l'entrée dans ce mode ; 0 si pas en Stage 1 ; permet de surveiller l'approche du seuil d'escalation
  - `Geo_uptime` (sensor, plateforme `uptime`) — temps de fonctionnement de l'ESP en secondes
  - `Geo_free_heap` (sensor, plateforme `debug`) — mémoire heap libre en octets
  - `Geo_short_cycle_protection` (binary_sensor) — ON si au moins une zone est maintenue active pour respecter `min_cycle_time`
  - `Geo_mode_changes` (sensor) — compteur cumulatif de transitions de mode depuis le boot
- **Tous les capteurs composant sont publiés par `publish_diagnostics_()`** appelé à chaque `update()` ; le compteur de mode est incrémenté dans `pass5_output_control_()`.
- **Bénéfice** : Meilleure observabilité et détection proactive des problèmes.

---

## 🟡 Priorité Moyenne

### 5. Persistance de `last_active_mode` au redémarrage
- **Fichier(s)** : `components/open_zoning/open_zoning.h`, `components/open_zoning/open_zoning.cpp`
- **État** : ✅ Fait
- **Description** : `last_active_mode` (0=inconnu, 1=chauffage, 2=climatisation) est sauvegardé en flash via `ESPPreferenceObject` (clé FNV1a `"open_zoning_last_active_mode"`). Implémentation :
  - Dans `setup()` : initialisation de la préférence et rechargement de la valeur stockée si valide (1 ou 2).
  - Dans `pass5_output_control_()` : sauvegarde **conditionnelle** uniquement quand la valeur change (chauffage ↔ climatisation), évitant l'usure du flash (les cycles de chauffe/refroid durent plusieurs minutes ; sans la condition, le save se déclencherait toutes les 10 secondes).
  - Valeur restaurée au boot, loguée à `INFO` ; absence de valeur valide loguée à `DEBUG`.
- **Bénéfice** : La direction de purge (clapet OB ouvert ou fermé) est correcte après un reboot, même si le démarrage survient pendant ou juste après un cycle actif.

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
- **Fichier(s)** : `packages/configurations.yml`, `packages/component.yml`, `components/open_zoning/open_zoning.h`, `components/open_zoning/open_zoning.cpp`, `components/open_zoning/__init__.py`
- **État** : ✅ Fait
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
- **Fichier(s)** : `packages/select.yml`, `components/open_zoning/open_zoning.h`, `components/open_zoning/open_zoning.cpp`
- **État** : ✅ Fait (option retenue : blocage du changement manuel quand `geo_auto_mode` est actif)
- **Description** : Le `on_value` du select logue un warning quand on change manuellement le mode en auto. Implémenté :
  - Blocage du changement manuel quand `geo_auto_mode` est actif
  - Un flag `component_driving_select_` dans le composant C++ empêche toute boucle infinie (distingue les changements internes vs utilisateur)
  - `reapply_mode()` réapplique immédiatement le mode courant du composant (outputs physiques + select)
  - `on_value` lambda dans `select.yml` : si `geo_auto_mode_switch` est ON et ce n'est pas le composant qui drive, appel `reapply_mode()` et log WARNING
- **Bénéfice** : Prévention des conflits de commande — tout changement manuel accidentel depuis HA est annulé instantanément.

---

## Suivi des modifications

| Date | Optimisation | Statut |
|------|-------------|--------|
| 2026-02-11 | Plan créé | ✅ |
| 2026-02-11 | #0 Migration C++ ajoutée (5 phases) | ✅ |
| 2026-02-11 | #0 Phase 0A — Scaffolding composant | ✅ En attente de validation compilation |
| 2026-02-12 | #0 Phase 0B — Référencement entités (binary sensors) | ✅ |
| 2026-02-12 | #0 Phase 0C — Migration PASS 1-3 en C++ | ✅ |
| 2026-02-12 | #0 Phase 0D — Migration PASS 4-5 + dampers en C++ | ✅ |
| 2026-02-12 | #0 Phase 0E — Nettoyage et suppression ancien code | ✅ |
| 2026-03-02 | #1 Durée de purge configurable | ✅ |
| 2026-03-03 | #8 Watchdog I2C pour MCP23017 | ✅ |
| 2026-03-03 | #11 Seuil de démarrage minimum (min_active_zones) | ✅ |
| 2026-03-05 | #10 Blocage changement manuel select (auto_mode actif) | ✅ |
| 2026-03-05 | #3 Capteurs de diagnostic (6 sensors) | ✅ |
| 2026-03-06 | #5 Persistance last_active_mode (ESPPreferenceObject) | ✅ |

---

*Ce document sera mis à jour au fur et à mesure de l'implémentation des optimisations.*
