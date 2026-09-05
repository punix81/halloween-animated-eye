# Documentation technique

Cette page complete le `README.md` principal avec les informations utiles pour assembler, tester et recuperer le projet.

## Architecture

```text
Telephone / navigateur
        |
        | Wi-Fi AP DemonEye
        v
ESP32-S2 Mini
  |-- WebServer HTTP local
  |     |-- /              interface de controle
  |     |-- /api/status    etat JSON, version firmware
  |     |-- /api/gifs      catalogue GIF
  |     |-- /api/upload    ajout GIF sur microSD
  |     |-- /api/delete    suppression GIF
  |     |-- /ota           page firmware protegee
  |     `-- /ota/update    upload firmware .bin
  |
  |-- AnimatedGIF          decodage image par image
  |-- Adafruit_GC9A01A     affichage rond 240 x 240
  |-- SD                   fichiers /eye.gif et /gifs/*.gif
  `-- Update.h             ecriture OTA progressive en flash
```

L'animation est suspendue pendant les operations d'ecriture SD et pendant l'OTA pour eviter les conflits SPI et reduire la pression RAM. Les fichiers GIF et firmware sont recus par chunks; ils ne sont pas charges entierement en memoire.

## Cablage

| Signal | ESP32-S2 Mini | Module |
|---|---:|---|
| `MOSI` | GPIO `11` | TFT + microSD |
| `SCLK` | GPIO `12` | TFT + microSD |
| `MISO` | GPIO `13` | microSD |
| `TFT_DC` | GPIO `9` | GC9A01 DC |
| `TFT_RST` | GPIO `8` | GC9A01 RST |
| `TFT_CS` | GPIO `10` | GC9A01 CS |
| `SD_CS` | GPIO `34` | microSD CS |

Frequences configurees :

- TFT GC9A01 : `40 MHz`
- microSD : `10 MHz`

## Captures d'ecran

Les captures reelles dependent du montage et du navigateur utilise. Captures recommandees a ajouter dans ce dossier avant une publication finale :

| Fichier conseille | Contenu attendu |
|---|---|
| `docs/screenshots/home.png` | interface principale sur `http://192.168.4.1/` |
| `docs/screenshots/ota-login.png` | demande d'authentification HTTP Basic pour `/ota` |
| `docs/screenshots/ota-upload.png` | page d'envoi du firmware `.bin` |
| `docs/screenshots/status-json.png` | reponse de `/api/status` avec `firmwareVersion` |

Ne pas capturer ni publier de vrai mot de passe OTA.

## Procedure de recuperation

Si une mise a jour OTA echoue :

1. Garder l'ESP32-S2 alimente.
2. Reconnecter le telephone ou l'ordinateur au Wi-Fi `DemonEye`.
3. Ouvrir `http://192.168.4.1/`.
4. Lire le message d'erreur expose par l'interface ou par `/api/status`.
5. Corriger le fichier `.bin` ou recompiler avec le bon FQBN `esp32:esp32:lolin_s2_mini`.
6. Retourner sur `http://192.168.4.1/ota` et relancer l'upload.

Si l'appareil ne demarre plus apres un firmware invalide :

1. Brancher l'ESP32-S2 Mini en USB.
2. Mettre la carte en mode bootloader si necessaire.
3. Reflasher depuis Arduino IDE avec le sketch connu comme fonctionnel.
4. Conserver `config.h` local et ne pas le commiter.
5. Verifier que `/eye.gif` existe toujours a la racine de la microSD.

## Limitations connues

- Le serveur HTTP est local au point d'acces `DemonEye`; il n'est pas concu pour etre expose a Internet.
- L'authentification OTA utilise HTTP Basic sur le reseau local; choisir un mot de passe fort et ne pas le publier.
- La page OTA accepte uniquement des fichiers `.bin`, mais ne peut pas garantir que le binaire cible exactement le bon montage.
- Les GIFs trop grands ou fortement compresses peuvent depasser les capacites RAM/CPU de l'ESP32-S2.
- Le catalogue web est limite par `MAX_GIFS`.
- Les GIFs plus grands que `240 x 240` sont coupes a l'affichage.
- La licence du depot reste a definir explicitement.
