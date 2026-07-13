# ESP32 Températures — Champagne Belin

Modules de surveillance température/humidité (DHT22 uniquement) pour 4 emplacements du chai : Cave, Habillage, Pressoir, Maison.

## Modules

Un dossier par module, chacun avec son propre `.ino` (template identique, seul `NOM_MODULE` change) :
- `cave/`
- `habillage/`
- `pressoir/`
- `maison/`

## Configuration

1. Copier `src/secrets.h.example` en `secrets.h` dans le dossier de CHAQUE module
2. Renseigner tes réseaux WiFi et l'URL de ton Apps Script
3. `secrets.h` n'est jamais commité (voir `.gitignore`)

## Architecture

- Fonctionnement en continu (pas de deep sleep) pour LED statut temps réel
- Données envoyées vers Google Sheets via Apps Script, 3 fois par jour (4h, 12h, 20h)
- Seuils d'alerte configurables dans l'onglet "Config" du Sheet
- Mise à jour OTA du firmware via GitHub (dossier `firmware/`)
