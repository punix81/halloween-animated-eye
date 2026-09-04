# Oeil demoniaque Halloween - ESP32-S2 + GC9A01 + GIF microSD

Projet Arduino pour afficher une animation d'oeil demoniaque sur un ecran rond GC9A01 240 x 240 px. Les GIFs sont lus depuis une carte microSD, sans etre charges entierement en RAM.

## Apercu

Le sketch :

- initialise un ecran TFT rond GC9A01 en SPI materiel ;
- initialise une carte microSD sur le meme bus SPI ;
- demarre un point d'acces Wi-Fi local `DemonEye` ;
- sert une interface web sombre sur `http://192.168.4.1/` ;
- expose l'etat du montage en JSON sur `/api/status` ;
- expose la liste des GIFs detectes sur `/api/gifs` ;
- lit `/eye.gif` comme animation de secours ;
- scanne `/gifs` pour proposer plusieurs animations ;
- decode le GIF avec la bibliotheque `AnimatedGIF` ;
- affiche l'animation en boucle infinie sur l'ecran.

## Materiel necessaire

- 1 carte compatible ESP32-S2
- 1 ecran rond TFT GC9A01 240 x 240 pixels
- 1 module lecteur de carte microSD SPI
- 1 carte microSD formatee en FAT32
- Fils Dupont
- Alimentation USB ou alimentation 5 V adaptee a la carte ESP32-S2

## Connexions

Le code utilise un bus SPI materiel partage entre l'ecran et la carte SD.

| Signal | Broche ESP32-S2 | Peripherique |
|---|---:|---|
| MOSI | GPIO 11 | TFT + microSD |
| SCLK | GPIO 12 | TFT + microSD |
| MISO | GPIO 13 | microSD |
| TFT DC | GPIO 9 | ecran GC9A01 |
| TFT RST | GPIO 8 | ecran GC9A01 |
| TFT CS | GPIO 10 | ecran GC9A01 |
| SD CS | GPIO 34 | module microSD |

Contraintes preservees :

- GC9A01 : `40 MHz`
- microSD : `10 MHz`
- bus SPI materiel partage : `MOSI 11`, `SCLK 12`, `MISO 13`
- fichier de secours : `/eye.gif`

## Bibliotheques Arduino

Installer depuis le gestionnaire de bibliotheques Arduino :

- `Adafruit GFX Library`
- `Adafruit GC9A01A`
- `AnimatedGIF` par Larry Bank

Fournies par le support Arduino ESP32 :

- `SPI`
- `SD`
- `WiFi`
- `WebServer`

## Preparation de la carte microSD

1. Formater la carte microSD en `FAT32`.
2. Garder un GIF de secours a la racine : `/eye.gif`.
3. Creer un dossier `/gifs`.
4. Copier les animations selectionnables dans `/gifs`.

Structure attendue :

```text
/eye.gif
/gifs/eye-red.gif
/gifs/eye-green.gif
/gifs/monster.gif
/gifs/hypnotic.gif
```

Regles :

- `/eye.gif` reste l'animation de secours.
- Les GIFs selectionnables depuis le web sont ceux presents dans `/gifs`.
- Les extensions `.gif`, `.GIF` et variantes majuscules/minuscules sont acceptees.
- Les chemins contenant `..`, `\`, guillemet ou caracteres de controle sont rejetes.

## Installation avec Arduino IDE

1. Installer Arduino IDE.
2. Installer le support ESP32 dans le gestionnaire de cartes.
3. Ouvrir `halloween_Globe_eye.ino`.
4. Selectionner la carte ESP32-S2 utilisee.
5. Installer les bibliotheques listees plus haut.
6. Brancher l'ESP32-S2 en USB.
7. Selectionner le bon port serie.
8. Televerser le sketch.
9. Inserer la carte microSD preparee.
10. Ouvrir le moniteur serie a `115200 bauds`.

## Configuration principale

Les parametres importants sont au debut de `halloween_Globe_eye.ino`.

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

const char* FALLBACK_GIF_PATH = "/eye.gif";
const char* GIF_DIRECTORY = "/gifs";

const char WIFI_AP_SSID[] = "DemonEye";
const char WIFI_AP_PASSWORD[] = "DemonEye2026";
```

Le mot de passe du point d'acces Wi-Fi doit contenir au moins 8 caracteres. Ne mettez pas d'identifiants Wi-Fi personnels dans le depot.

## Interface web Wi-Fi

Au demarrage, l'ESP32-S2 cree un point d'acces local :

- SSID : `DemonEye`
- mot de passe par defaut : `DemonEye2026`
- adresse IP habituelle : `192.168.4.1`

L'ecran affiche brievement l'adresse IP au demarrage, puis reprend l'animation GIF.

Depuis un telephone ou un ordinateur :

1. Se connecter au reseau Wi-Fi `DemonEye`.
2. Ouvrir `http://192.168.4.1/` dans un navigateur.
3. Verifier l'etat SD, le GIF actif, les dimensions, la taille, la memoire libre, l'IP et lecture/pause.
4. Cliquer sur `Lire` a cote d'un GIF pour changer d'animation.

## API locale

```text
GET  /api/status
GET  /api/gifs
POST /api/play
POST /api/toggle
```

`GET /api/status` retourne l'etat courant :

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
  "gifActive": true,
  "message": "Lecture GIF active"
}
```

`GET /api/gifs` retourne les animations detectees dans `/gifs` :

```json
[
  {
    "name": "eye-red.gif",
    "path": "/gifs/eye-red.gif",
    "size": 123456,
    "current": false
  }
]
```

`POST /api/play` attend un parametre `path`, par exemple `/gifs/monster.gif`. Le chemin doit correspondre a un GIF detecte au demarrage.

Si le nouveau GIF est invalide, le sketch ferme proprement l'animation courante, tente le nouveau fichier, puis revient automatiquement au dernier GIF valide ou a `/eye.gif`.

## Lecture GIF et serveur web

Le sketch n'utilise pas `gif.playFrame(true, ...)`, car cette forme peut bloquer avec un `delay()` interne. La lecture utilise `gif.playFrame(false, &delaiFrameMs)` et planifie la prochaine image avec `millis()`.

Cela permet d'appeler regulierement `server.handleClient()` tout en conservant l'animation.

## Architecture du depot

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

`assets/gifs/` sert a sauvegarder les GIFs dans GitHub. Pour le montage, copiez les fichiers necessaires sur la carte microSD dans la structure decrite plus haut.

## Depannage

### Carte SD inaccessible

- Verifier `MOSI`, `MISO`, `SCLK` et `SD_CS`.
- Verifier que la carte est formatee en FAT32.
- Tester une autre carte microSD.
- Reduire `SD_FREQUENCY` a `4000000` si le module SD est instable.

### Aucun GIF dans la page web

- Verifier que le dossier `/gifs` existe a la racine de la microSD.
- Verifier que les fichiers finissent par `.gif`.
- Redemarrer l'ESP32-S2 apres ajout de fichiers, car le scan est fait au demarrage.

### GIF invalide ou impossible a lire

- Verifier le moniteur serie a `115200 bauds`.
- Tester un GIF plus petit ou reexporte proprement.
- Garder `/eye.gif` valide comme secours.

### Interface web inaccessible

- Verifier que le telephone est connecte au Wi-Fi `DemonEye`.
- Ouvrir directement `http://192.168.4.1/`.
- Garder l'ESP32-S2 alimente meme si aucun telephone n'est connecte : l'oeil continue a fonctionner seul.

## Test manuel recommande

1. Copier `assets/gifs/eye.gif` a la racine de la carte microSD sous le nom `eye.gif`.
2. Creer `/gifs` sur la microSD et y copier `eye-red.gif`, `eye-green.gif`, `monster.gif` et `hypnotic.gif`.
3. Televerser le sketch sur l'ESP32-S2.
4. Verifier sur le GC9A01 que l'adresse IP s'affiche brievement.
5. Verifier que l'animation de secours `/eye.gif` demarre.
6. Se connecter au Wi-Fi `DemonEye`.
7. Ouvrir `http://192.168.4.1/`.
8. Verifier que la liste des GIFs de `/gifs` apparait.
9. Cliquer sur `Lire` pour plusieurs GIFs et verifier que l'ecran change sans redemarrage.
10. Verifier que `/api/status` et `/api/gifs` retournent des JSON coherents.
11. Tester le bouton lecture/pause sans redemarrer l'ESP32-S2.
12. Tester un GIF invalide dans `/gifs` : la page et le moniteur serie doivent afficher une erreur, puis le dernier GIF valide doit reprendre.

## Notes techniques

- Le serveur web utilise uniquement `WiFi.h` et `WebServer.h` du core Arduino ESP32.
- Les GIFs de `/gifs` sont references par nom, chemin et taille ; ils ne sont pas charges entierement en RAM.
- Le rendu utilise `drawRGBBitmap()` ligne par ligne pour limiter la memoire utilisee.
- Le fonctionnement de l'oeil ne depend pas d'un telephone connecte.

## Licence

Ajoutez la licence de votre choix avant de publier le depot. Pour un projet personnel open source simple, une licence `MIT` est souvent suffisante.
