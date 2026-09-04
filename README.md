# Œil démoniaque Halloween — ESP32-S2 + écran rond GC9A01 + GIF sur microSD

Projet Arduino pour afficher une animation d’œil démoniaque sur un écran rond GC9A01 de 240 × 240 pixels.  
L’animation est lue depuis une carte microSD sous forme de fichier GIF (`eye.gif`).

## Aperçu

Le sketch :

- initialise un écran TFT rond GC9A01 en SPI matériel ;
- initialise une carte microSD sur le même bus SPI ;
- démarre un point d’accès Wi-Fi local `DemonEye` ;
- sert une interface web sombre sur `http://192.168.4.1/` ;
- expose l’état du montage en JSON sur `/api/status` ;
- lit le fichier `/eye.gif` depuis la carte SD ;
- décode le GIF avec la bibliothèque `AnimatedGIF` ;
- affiche l’animation en boucle infinie sur l’écran.

## Matériel nécessaire

- 1 carte compatible ESP32-S2
- 1 écran rond TFT GC9A01 240 × 240 pixels
- 1 module lecteur de carte microSD SPI
- 1 carte microSD formatée en FAT32
- Fils Dupont
- Alimentation USB ou alimentation 5 V adaptée à la carte ESP32-S2

## Connexions

Le code utilise un bus SPI matériel partagé entre l’écran et la carte SD.

| Signal | Broche ESP32-S2 | Périphérique |
|---|---:|---|
| MOSI | GPIO 11 | TFT + microSD |
| SCLK | GPIO 12 | TFT + microSD |
| MISO | GPIO 13 | microSD |
| TFT DC | GPIO 9 | écran GC9A01 |
| TFT RST | GPIO 8 | écran GC9A01 |
| TFT CS | GPIO 10 | écran GC9A01 |
| SD CS | GPIO 34 | module microSD |

Notes importantes :

- L’écran et la carte SD partagent `MOSI`, `SCLK` et, si présent côté écran, `MISO`.
- Chaque périphérique doit avoir son propre `CS`.
- Vérifier la logique de tension de vos modules. La plupart des modules ESP32-S2 utilisent du 3,3 V.
- Si votre module microSD ne fonctionne pas sur GPIO 34, choisissez une autre broche libre et modifiez `SD_CS` dans `halloween_Globe_eye.ino`.

## Bibliothèques Arduino

Installer ces bibliothèques depuis le gestionnaire de bibliothèques Arduino :

- `Adafruit GFX Library`
- `Adafruit GC9A01A`
- `AnimatedGIF` par Larry Bank

Les bibliothèques suivantes sont généralement incluses avec le support ESP32 Arduino :

- `SPI`
- `SD`
- `WiFi`
- `WebServer`

## Préparation de la carte microSD

1. Formater la carte microSD en `FAT32`.
2. Copier le fichier GIF à la racine de la carte.
3. Renommer le fichier exactement :

```text
eye.gif
```

Le chemin attendu par le programme est :

```text
/eye.gif
```

Recommandations pour le GIF :

- taille idéale : `240 × 240 px` ;
- format : GIF animé ;
- éviter les fichiers trop lourds pour garder une lecture fluide ;
- si le GIF dépasse `240 × 240 px`, il sera coupé par le sketch.

## Installation avec Arduino IDE

1. Installer Arduino IDE.
2. Installer le support ESP32 dans le gestionnaire de cartes.
3. Ouvrir `halloween_Globe_eye.ino`.
4. Sélectionner la carte ESP32-S2 utilisée.
5. Installer les bibliothèques listées plus haut.
6. Brancher l’ESP32-S2 en USB.
7. Sélectionner le bon port série.
8. Téléverser le sketch.
9. Insérer la carte microSD contenant `eye.gif`.
10. Ouvrir le moniteur série à `115200 bauds` pour vérifier le démarrage.

## Configuration principale

Les paramètres importants sont au début du fichier `halloween_Globe_eye.ino`.

```cpp
#define SPI_MOSI 11
#define SPI_SCLK 12
#define SPI_MISO 13

#define TFT_DC   9
#define TFT_RST  8
#define TFT_CS  10

#define SD_CS 34

const uint32_t TFT_FREQUENCY = 40000000;
const uint32_t SD_FREQUENCY  = 10000000;

const char* GIF_PATH = "/eye.gif";
const uint16_t GIF_RESTART_DELAY_MS = 10;

const char WIFI_AP_SSID[] = "DemonEye";
const char WIFI_AP_PASSWORD[] = "DemonEye2026";
```

Si vous utilisez d’autres broches, modifiez ces valeurs avant le téléversement.

`GIF_RESTART_DELAY_MS` contrôle la petite pause entre la dernière image du GIF et son redémarrage. La valeur `10` garde une boucle quasiment continue.

Le mot de passe du point d’accès Wi-Fi doit contenir au moins 8 caractères. Ne mettez pas d’identifiants Wi-Fi personnels dans le dépôt.

## Interface web Wi-Fi

Au démarrage, l’ESP32-S2 crée un point d’accès local :

- SSID : `DemonEye`
- mot de passe par défaut : `DemonEye2026`
- adresse IP habituelle : `192.168.4.1`

L’écran affiche brièvement l’adresse IP au démarrage, puis reprend l’animation GIF.

Depuis un téléphone ou un ordinateur :

1. Se connecter au réseau Wi-Fi `DemonEye`.
2. Ouvrir `http://192.168.4.1/` dans un navigateur.
3. Vérifier les informations affichées : carte SD, GIF actif, dimensions, taille du fichier, mémoire libre, IP et état lecture/pause.

Route JSON disponible :

```text
GET http://192.168.4.1/api/status
```

Exemple de réponse :

```json
{
  "sd": true,
  "gifName": "/eye.gif",
  "gifWidth": 240,
  "gifHeight": 240,
  "gifSize": 5869752,
  "freeHeap": 123456,
  "ip": "192.168.4.1",
  "paused": false,
  "gifActive": true
}
```

La page web utilise aussi `POST /api/toggle` pour basculer entre lecture et pause.

## Lecture GIF et serveur web

Le sketch n’utilise plus `gif.playFrame(true, ...)`, car cette forme peut bloquer avec un `delay()` interne.  
La lecture utilise `gif.playFrame(false, &delaiFrameMs)` et planifie la prochaine image avec `millis()`.

Cela permet d’appeler régulièrement `server.handleClient()` tout en conservant l’animation.

## Architecture du dépôt

```text
halloween_Globe_eye/
├── assets/
│   └── gifs/
│       ├── README.md
│       └── eye.gif
├── docs/
│   └── README.md
├── .gitignore
├── halloween_Globe_eye.ino
└── README.md
```

Rôle des dossiers :

- `halloween_Globe_eye.ino` : sketch Arduino principal. Il reste à la racine car Arduino IDE attend souvent le fichier `.ino` dans le dossier du projet.
- `assets/gifs/` : sauvegarde des GIFs utilisés par le projet.
- `docs/` : photos de montage, schémas de câblage, notes de boîtier ou documentation complémentaire.
- `.gitignore` : exclut les fichiers locaux inutiles comme la configuration IDE.

Important : le GIF sauvegardé dans `assets/gifs/` sert d’archive pour GitHub. Pour faire fonctionner le montage, copiez le GIF voulu à la racine de la carte microSD et renommez-le exactement `eye.gif`.

## Dépannage

### Message : `Carte SD inaccessible`

- Vérifier le câblage `MOSI`, `MISO`, `SCLK` et `SD_CS`.
- Vérifier que la carte est formatée en FAT32.
- Tester une autre carte microSD.
- Réduire `SD_FREQUENCY` à `4000000` si le module SD est instable.

### Message : `eye.gif introuvable`

- Vérifier que le fichier est à la racine de la carte microSD.
- Vérifier que le nom est exactement `eye.gif`.
- Éviter `eye.gif.gif`, fréquent sous Windows si les extensions sont masquées.

### Écran noir

- Vérifier `TFT_CS`, `TFT_DC`, `TFT_RST`, `MOSI` et `SCLK`.
- Vérifier que l’écran est bien un modèle GC9A01.
- Tester une fréquence plus basse pour l’écran, par exemple `27000000`.

### Animation lente ou saccadée

- Utiliser un GIF plus léger.
- Réduire le nombre d’images ou la taille du fichier.
- Utiliser une carte microSD plus rapide.
- Garder le GIF à `240 × 240 px` maximum.

### Interface web inaccessible

- Vérifier que le téléphone est connecté au Wi-Fi `DemonEye`.
- Ouvrir directement `http://192.168.4.1/`.
- Vérifier le moniteur série à `115200 bauds`.
- Garder l’ESP32-S2 alimenté même si aucun téléphone n’est connecté : l’œil continue à fonctionner seul.

## Test manuel recommandé

1. Copier `assets/gifs/eye.gif` à la racine de la carte microSD sous le nom `eye.gif`.
2. Téléverser le sketch sur l’ESP32-S2.
3. Vérifier sur le GC9A01 que l’adresse IP s’affiche brièvement.
4. Vérifier que l’animation reprend ensuite en boucle.
5. Se connecter au Wi-Fi `DemonEye`.
6. Ouvrir `http://192.168.4.1/`.
7. Vérifier que `/api/status` retourne un JSON cohérent.
8. Tester le bouton lecture/pause sans redémarrer l’ESP32-S2.

## Notes techniques

- L’écran est initialisé à `40 MHz`.
- La carte SD est initialisée à `10 MHz`.
- Le GIF est relancé automatiquement quand la dernière image est atteinte.
- Le serveur web utilise uniquement `WiFi.h` et `WebServer.h` du core Arduino ESP32.
- Le rendu utilise `drawRGBBitmap()` ligne par ligne pour limiter la mémoire utilisée.

## Licence

Ajoutez la licence de votre choix avant de publier le dépôt.  
Pour un projet personnel open source simple, une licence `MIT` est souvent suffisante.
