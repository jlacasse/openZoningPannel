# openZoningPannel

Système de contrôle de zonage HVAC intelligent pour ESPHome, conçu pour gérer jusqu'à 6 zones indépendantes avec une unité géothermique centralisée.

## Fonctionnalités

- **6 zones indépendantes** avec entrées thermostat (Y1, Y2, G, OB) par zone
- **Contrôle automatique des clapets** motorisés avec délai de 250ms pour protection moteur
- **5 passes d'analyse** exécutées toutes les 10 secondes :
  - PASS 1 : Calcul d'état des zones (chauffage/climatisation/ventilation/erreur)
  - PASS 1.5 : Protection contre les cycles courts (configurable)
  - PASS 2 : Gestion intelligente des purges multi-zones (seule la dernière zone purge)
  - PASS 3 : Analyse de priorité (PURGE > HEATING > COOLING > FAN > OFF)
  - PASS 4 : Contrôle des clapets
  - PASS 5 : Contrôle automatique de l'unité centrale avec escalation Stage 2
- **Détection d'erreurs** avec confirmation sur 2 cycles
- **Composant C++ ESPHome natif** — pas de lambda, pas de globals, pas de macros

## Architecture

```
components/open_zoning/
├── __init__.py          # Schema YAML + codegen Python
├── open_zoning.h        # Classe OpenZoningController (PollingComponent)
├── open_zoning.cpp      # Logique 5 passes
└── zone.h               # Struct Zone + enum ZoneState

packages/
├── base.yml             # Config ESPHome de base
├── configurations.yml   # I2C, WiFi, API, OTA, MCP23017
├── binary_sensors.yml   # Entrées thermostat GPIO (Y1, Y2, G, OB × 6)
├── switches.yml         # Sorties GPIO (dampers, LEDs, Out_Y1/Y2/G/OB/W)
├── select.yml           # Entité select pour affichage du mode dans HA
└── component.yml        # Déclaration external_components + config open_zoning
```

## Installation

### 1. Ajouter les packages dans votre configuration ESPHome

```yaml
packages:
  base: github://jlacasse/openZoningPannel/packages/base.yml
  configurations: github://jlacasse/openZoningPannel/packages/configurations.yml
  binary_sensors: github://jlacasse/openZoningPannel/packages/binary_sensors.yml
  switches: github://jlacasse/openZoningPannel/packages/switches.yml
  select: github://jlacasse/openZoningPannel/packages/select.yml
  component: github://jlacasse/openZoningPannel/packages/component.yml
```

### 2. Créer le fichier `secrets.yaml`

Voir [esphome/secrets.yaml.example](esphome/secrets.yaml.example) pour le format requis.

### 3. Personnaliser

Voir [esphome/geothermie_example.yaml](esphome/geothermie_example.yaml) pour un exemple complet.

## Configuration du composant

Le composant `open_zoning` est configuré dans `packages/component.yml` :

```yaml
open_zoning:
  update_interval: 10s
  min_cycle_time: 480s              # Protection cycle court (8 min)
  purge_duration: 300s              # Durée de purge (5 min)
  stage2_escalation_delay: 3600s    # Escalation Stage 2 (1h)
  auto_mode: true                   # PASS 5 contrôle l'unité

  # Sorties unité centrale
  out_y1: Out_Y1
  out_y2: Out_Y2
  out_g:  Out_G
  out_ob: Out_OB

  # LEDs indicatrices
  led_heat:  Led_Heat
  led_cool:  Led_Cool
  led_fan:   Led_Fan
  led_error: Led_error

  # Entité mode
  mode_select: Geothermie_mode_select

  # Zones (1 à 6)
  zones:
    - y1: Z1_Y1
      y2: Z1_Y2
      g:  Z1_G
      ob: Z1_OB
      damper_open:  Z1_damper_open
      damper_close: Z1_damper_close
    # ... (jusqu'à 6 zones)
```

## Matériel requis

- ESP8266 (ex. Wemos D1 Mini)
- 3× MCP23017 (I2C GPIO expanders)
- Clapets motorisés (open/close par zone)
- LEDs indicatrices (Heat, Cool, Fan, Error)
- Connexions thermostat 24V → 3.3V par zone

## Documentation

- [LOGIQUE_CONTROLE.md](LOGIQUE_CONTROLE.md) — Description détaillée de la logique de contrôle
- [OPTIMISATIONS.md](OPTIMISATIONS.md) — Plan d'optimisations et suivi

## Licence

Voir [LICENSE](LICENSE).