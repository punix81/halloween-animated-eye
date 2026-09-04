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
- permet de selectionner, televerser et supprimer des GIFs dans `/gifs` ;
- ajoute des commandes persistantes : lecture/pause, redemarrage, vitesse, aleatoire, precedent/suivant et repetition ;
- restaure au demarrage le dernier GIF valide et les parametres avec `Preferences` ;
- conserve `/eye.gif` comme animation de secours.

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
- `Preferences`

## Carte microSD

Structure recommandee :

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
- Les chemins contenant `..`, `\`, guillemet, sous-dossier ou caracteres de controle sont rejetes.
- Les uploads sont limites par `MAX_UPLOAD_SIZE_BYTES`, par defaut `10 Mo`.

## Installation

1. Installer Arduino IDE.
2. Installer le support ESP32 dans le gestionnaire de cartes.
3. Ouvrir `halloween_Globe_eye.ino`.
4. Selectionner la carte ESP32-S2 utilisee.
5. Installer les bibliotheques listees plus haut.
6. Preparer la microSD avec `/eye.gif` et le dossier `/gifs`.
7. Brancher l'ESP32-S2 en USB.
8. Televerser le sketch.
9. Ouvrir le moniteur serie a `115200 bauds`.

## Configuration principale

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
const uint32_t MAX_UPLOAD_SIZE_BYTES = 10UL * 1024UL * 1024UL;
const uint16_t VITESSES_LECTURE[] = {50, 75, 100, 125, 150, 200};

const char WIFI_AP_SSID[] = "DemonEye";
const char WIFI_AP_PASSWORD[] = "DemonEye2026";
```

Le mot de passe du point d'acces Wi-Fi doit contenir au moins 8 caracteres. Ne mettez pas d'identifiants Wi-Fi personnels dans le depot.

## Interface web Wi-Fi

Au demarrage, l'ESP32-S2 cree un point d'acces local :

- SSID : `DemonEye`
- mot de passe par defaut : `DemonEye2026`
- adresse IP habituelle : `192.168.4.1`

Depuis un telephone :

1. Se connecter au reseau Wi-Fi `DemonEye`.
2. Ouvrir `http://192.168.4.1/`.
3. Utiliser les boutons `Lecture / pause`, `Redemarrer`, `Precedent`, `Suivant`.
4. Choisir la vitesse : `0,5x`, `0,75x`, `1x`, `1,25x`, `1,5x` ou `2x`.
5. Activer ou desactiver le mode aleatoire et la repetition.
6. Televerser ou supprimer des GIFs depuis la page.

## API locale

Routes existantes conservees :

```text
GET  /api/status
GET  /api/gifs
POST /api/play
POST /api/upload
POST /api/delete
POST /api/toggle
```

Routes de controle de lecture :

```text
GET  /api/playback
POST /api/playback
```

`GET /api/status` et `GET /api/playback` retournent l'etat courant :

```json
{
  "sd": true,
  "gifName": "/gifs/monster.gif",
  "gifWidth": 240,
  "gifHeight": 240,
  "gifSize": 123456,
  "freeHeap": 123456,
  "ip": "192.168.4.1",
  "paused": false,
  "gifActive": true,
  "sdBusy": false,
  "speedPercent": 100,
  "random": false,
  "repeat": true,
  "mode": "normal",
  "message": "Lecture GIF active"
}
```

`POST /api/playback` accepte des parametres formulaire :

- `action=play`, `pause`, `toggle`, `restart`, `next` ou `previous` ;
- `speed=50`, `75`, `100`, `125`, `150` ou `200` ;
- `random=0` ou `1` ;
- `repeat=0` ou `1`.

Parametres sauvegardes avec `Preferences` :

- dernier GIF selectionne ;
- vitesse ;
- mode aleatoire ;
- repetition.

Au demarrage, le chemin memorise est revalide. Si le fichier n'existe plus ou si le chemin est dangereux, le sketch revient a `/eye.gif`.

## Upload et suppression

`POST /api/upload` utilise `multipart/form-data` avec un champ fichier `gif`. Le sketch suspend et ferme l'animation, ecrit les chunks recus dans `/gifs/.upload.tmp`, verifie le fichier, puis renomme le temporaire vers le nom definitif.

Validations upload :

- taille superieure a zero ;
- taille inferieure ou egale a `MAX_UPLOAD_SIZE_BYTES` ;
- signature `GIF87a` ou `GIF89a` ;
- nom de fichier simple, sans sous-dossier, sans `..`, sans `\`, sans guillemet et sans caractere de controle ;
- destination dans `/gifs` uniquement ;
- pas d'ecrasement d'un GIF existant.

`POST /api/delete` attend `path=/gifs/nom.gif`. La suppression est interdite pour le GIF actif et pour `/eye.gif`.

## Gestion temporelle

Le sketch n'utilise pas `gif.playFrame(true, ...)`, car cette forme peut bloquer avec un `delay()` interne. La lecture utilise `gif.playFrame(false, &delaiFrameMs)` et planifie la prochaine image avec `millis()`.

La comparaison temporelle utilise une difference signee afin de rester correcte lors du debordement de `millis()`. La boucle principale ne contient pas de longue attente bloquante.

## Scenarios d'echec documentes

- `SD inaccessible` : les operations SD sont refusees, mais le serveur web reste disponible.
- `Nom de fichier GIF invalide` : nom contenant chemin, `..`, `\`, guillemet, controle, ou mauvaise extension.
- `Fichier trop grand` : taille recue superieure a `MAX_UPLOAD_SIZE_BYTES`.
- `Fichier vide` : aucun octet utile recu.
- `Signature GIF non compatible` : les six premiers octets ne sont ni `GIF87a` ni `GIF89a`.
- `Manque d'espace pendant l'ecriture` : un chunk n'a pas ete ecrit completement.
- `Un GIF porte deja ce nom` : l'upload refuse d'ecraser un fichier existant.
- `Suppression du GIF actif ou de secours interdite` : securite contre une suppression dangereuse.

En cas d'echec d'upload, `/gifs/.upload.tmp` est supprime et l'animation precedente reprend si elle est encore lisible.

## Test manuel recommande

1. Copier `assets/gifs/eye.gif` a la racine de la microSD sous le nom `eye.gif`.
2. Creer `/gifs` sur la microSD et y copier plusieurs GIFs.
3. Televerser le sketch sur l'ESP32-S2.
4. Verifier que l'adresse IP s'affiche brievement sur le GC9A01.
5. Verifier que `/eye.gif` demarre.
6. Se connecter au Wi-Fi `DemonEye` avec un telephone.
7. Ouvrir `http://192.168.4.1/`.
8. Tester lecture/pause, redemarrage, precedent/suivant.
9. Tester toutes les vitesses : `0,5x`, `0,75x`, `1x`, `1,25x`, `1,5x`, `2x`.
10. Activer le mode aleatoire, puis utiliser `Suivant` plusieurs fois.
11. Desactiver la repetition et verifier que le GIF s'arrete en fin de lecture.
12. Selectionner un GIF de `/gifs`, regler vitesse/aleatoire/repetition, puis couper electriquement l'ESP32-S2.
13. Rallumer l'ESP32-S2 et verifier que les parametres sont restaures.
14. Retirer de la microSD le GIF memorise, rallumer, et verifier le retour automatique a `/eye.gif`.
15. Televerser un GIF valide de moins de 10 Mo et verifier la progression.
16. Supprimer un GIF non actif et verifier qu'il disparait.
17. Verifier que la suppression du GIF actif est refusee.
18. Tester un GIF invalide ou trop grand : erreur lisible puis reprise du dernier GIF valide.

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

## Licence

Ajoutez la licence de votre choix avant de publier le depot. Pour un projet personnel open source simple, une licence `MIT` est souvent suffisante.
