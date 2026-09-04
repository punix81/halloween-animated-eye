#include <SPI.h>
#include <SD.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include <AnimatedGIF.h>

// ============================================================
// Bus SPI materiel partage
// ESP32-S2 : MOSI=11, SCLK=12, MISO=13
// ============================================================

#define SPI_MOSI 11
#define SPI_SCLK 12
#define SPI_MISO 13

// ============================================================
// Ecran GC9A01
// ============================================================

#define TFT_DC   9
#define TFT_RST  8
#define TFT_CS  10

// Constructeur SPI materiel : SPI, DC, CS, RST
Adafruit_GC9A01A tft(&SPI, TFT_DC, TFT_CS, TFT_RST);

// ============================================================
// Carte microSD
// ============================================================

#define SD_CS 34

const uint32_t TFT_FREQUENCY = 40000000;  // 40 MHz
const uint32_t SD_FREQUENCY  = 10000000;  // 10 MHz

const char* GIF_PATH = "/eye.gif";

// ============================================================
// Decodeur GIF
// ============================================================

AnimatedGIF gif;
File gifFile;

bool gifActif = false;
int16_t gifOffsetX = 0;
int16_t gifOffsetY = 0;

// Le GC9A01 fait 240 pixels de large.
uint16_t lignePixels[240];

// ============================================================
// Acces au fichier GIF sur la carte SD
// ============================================================

void* ouvrirFichierGIF(const char* nom, int32_t* taille) {
  gifFile = SD.open(nom, FILE_READ);

  if (!gifFile) {
    return nullptr;
  }

  *taille = gifFile.size();
  return &gifFile;
}

void fermerFichierGIF(void* handle) {
  File* fichier = static_cast<File*>(handle);

  if (fichier != nullptr) {
    fichier->close();
  }
}

int32_t lireFichierGIF(
  GIFFILE* fichierGIF,
  uint8_t* buffer,
  int32_t longueur
) {
  File* fichier = static_cast<File*>(fichierGIF->fHandle);

  int32_t restant = fichierGIF->iSize - fichierGIF->iPos;

  if (longueur > restant) {
    longueur = restant;
  }

  if (longueur <= 0) {
    return 0;
  }

  int32_t octetsLus = fichier->read(buffer, longueur);
  fichierGIF->iPos = fichier->position();

  return octetsLus;
}

int32_t positionnerFichierGIF(
  GIFFILE* fichierGIF,
  int32_t position
) {
  File* fichier = static_cast<File*>(fichierGIF->fHandle);

  if (!fichier->seek(position)) {
    return -1;
  }

  fichierGIF->iPos = fichier->position();
  return fichierGIF->iPos;
}

// ============================================================
// Affichage d'une ligne decodee du GIF
// ============================================================

void dessinerGIF(GIFDRAW* dessin) {
  uint8_t* source = dessin->pPixels;
  uint16_t* palette = dessin->pPalette;

  int16_t positionX = dessin->iX + gifOffsetX;
  int16_t positionY = dessin->iY + dessin->y + gifOffsetY;
  int16_t largeur = dessin->iWidth;

  if (positionY < 0 || positionY >= tft.height()) {
    return;
  }

  if (positionX < 0 || positionX >= tft.width()) {
    return;
  }

  if (positionX + largeur > tft.width()) {
    largeur = tft.width() - positionX;
  }

  // Disposal method 2 : restaurer la couleur de fond.
  if (dessin->ucDisposalMethod == 2) {
    for (int16_t x = 0; x < largeur; x++) {
      if (source[x] == dessin->ucTransparent) {
        source[x] = dessin->ucBackground;
      }
    }

    dessin->ucHasTransparency = 0;
  }

  if (dessin->ucHasTransparency) {
    int16_t x = 0;

    while (x < largeur) {
      // Sauter les pixels transparents.
      while (
        x < largeur &&
        source[x] == dessin->ucTransparent
      ) {
        x++;
      }

      int16_t debut = x;
      int16_t nombrePixels = 0;

      // Copier une serie de pixels visibles.
      while (
        x < largeur &&
        source[x] != dessin->ucTransparent
      ) {
        lignePixels[nombrePixels++] = palette[source[x]];
        x++;
      }

      if (nombrePixels > 0) {
        tft.drawRGBBitmap(
          positionX + debut,
          positionY,
          lignePixels,
          nombrePixels,
          1
        );
      }
    }
  } else {
    for (int16_t x = 0; x < largeur; x++) {
      lignePixels[x] = palette[source[x]];
    }

    tft.drawRGBBitmap(
      positionX,
      positionY,
      lignePixels,
      largeur,
      1
    );
  }
}

// ============================================================
// Messages sur l'ecran
// ============================================================

void afficherErreur(const char* ligne1, const char* ligne2) {
  tft.fillScreen(GC9A01A_BLACK);
  tft.drawCircle(120, 120, 115, GC9A01A_RED);
  tft.drawCircle(120, 120, 114, GC9A01A_RED);

  tft.setTextWrap(false);
  tft.setTextColor(GC9A01A_RED);
  tft.setTextSize(2);
  tft.setCursor(70, 90);
  tft.println(ligne1);

  tft.setTextSize(1);
  tft.setCursor(45, 125);
  tft.println(ligne2);
}

// ============================================================
// Ouverture et demarrage de l'animation
// ============================================================

bool lancerGIF() {
  if (!gif.open(
        GIF_PATH,
        ouvrirFichierGIF,
        fermerFichierGIF,
        lireFichierGIF,
        positionnerFichierGIF,
        dessinerGIF
      )) {
    Serial.println("ERREUR : impossible d'ouvrir /eye.gif.");
    gifActif = false;
    return false;
  }

  int16_t largeur = gif.getCanvasWidth();
  int16_t hauteur = gif.getCanvasHeight();

  Serial.printf("Dimensions du GIF : %d x %d\n", largeur, hauteur);

  if (largeur > tft.width() || hauteur > tft.height()) {
    Serial.println("ATTENTION : le GIF depasse 240 x 240 et sera coupe.");
  }

  gifOffsetX = (tft.width() - largeur) / 2;
  gifOffsetY = (tft.height() - hauteur) / 2;

  if (gifOffsetX < 0) {
    gifOffsetX = 0;
  }

  if (gifOffsetY < 0) {
    gifOffsetY = 0;
  }

  tft.fillScreen(GC9A01A_BLACK);

  gifActif = true;
  Serial.println("Animation lancee en SPI materiel.");

  return true;
}

// ============================================================
// Initialisation
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(3000);

  Serial.println();
  Serial.println("====================================");
  Serial.println(" GC9A01 + SD + GIF - SPI MATERIEL");
  Serial.println("====================================");

  pinMode(TFT_CS, OUTPUT);
  pinMode(SD_CS, OUTPUT);

  // Aucun peripherique selectionne au demarrage.
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(SD_CS, HIGH);

  // Bus SPI materiel partage par l'ecran et la carte SD.
  SPI.begin(
    SPI_SCLK,
    SPI_MISO,
    SPI_MOSI
  );

  Serial.println("Initialisation du GC9A01 a 40 MHz...");

  tft.begin(TFT_FREQUENCY);
  tft.setRotation(0);
  tft.fillScreen(GC9A01A_BLACK);

  tft.setTextWrap(false);
  tft.setTextColor(GC9A01A_CYAN);
  tft.setTextSize(2);
  tft.setCursor(42, 110);
  tft.println("Chargement...");

  Serial.println("Initialisation de la carte SD a 10 MHz...");

  if (!SD.begin(SD_CS, SPI, SD_FREQUENCY)) {
    Serial.println("ERREUR : carte SD inaccessible.");
    afficherErreur("ERREUR", "Carte SD inaccessible");
    return;
  }

  Serial.println("Carte SD initialisee.");

  if (!SD.exists(GIF_PATH)) {
    Serial.println("ERREUR : /eye.gif introuvable.");
    afficherErreur("ERREUR", "eye.gif introuvable");
    return;
  }

  File test = SD.open(GIF_PATH, FILE_READ);

  if (!test) {
    Serial.println("ERREUR : ouverture de /eye.gif impossible.");
    afficherErreur("ERREUR", "Ouverture GIF impossible");
    return;
  }

  Serial.printf(
    "Taille du GIF : %u octets\n",
    static_cast<unsigned int>(test.size())
  );

  test.close();

  // Adafruit_GFX utilise les valeurs RGB565 natives de l'ESP32.
  gif.begin(LITTLE_ENDIAN_PIXELS);

  if (!lancerGIF()) {
    afficherErreur("ERREUR", "GIF incompatible");
  }
}

// ============================================================
// Lecture en boucle
// ============================================================

void loop() {
  if (!gifActif) {
    delay(1000);
    return;
  }

  // true : respecter les delais contenus dans le GIF.
  if (!gif.playFrame(true, nullptr)) {
    gif.close();
    gifActif = false;

    delay(10);

    if (!lancerGIF()) {
      afficherErreur("ERREUR", "Redemarrage GIF impossible");
    }
  }
}
