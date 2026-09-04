#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ctype.h>
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

const char* FALLBACK_GIF_PATH = "/eye.gif";
const char* GIF_DIRECTORY = "/gifs";
const uint16_t GIF_RESTART_DELAY_MS = 10;
const uint8_t MAX_GIFS = 16;
const uint8_t GIF_NAME_MAX_LENGTH = 40;
const uint8_t GIF_PATH_MAX_LENGTH = 80;

// ============================================================
// Point d'acces Wi-Fi
// ============================================================

const char WIFI_AP_SSID[] = "DemonEye";
const char WIFI_AP_PASSWORD[] = "DemonEye2026";
static_assert(sizeof(WIFI_AP_PASSWORD) >= 9, "Le mot de passe Wi-Fi AP doit contenir au moins 8 caracteres.");

WebServer server(80);

IPAddress adresseIP;

// ============================================================
// Decodeur GIF
// ============================================================

AnimatedGIF gif;
File gifFile;

bool gifActif = false;
bool lectureEnPause = false;
bool carteSDDisponible = false;
bool gifDisponible = false;
int16_t gifOffsetX = 0;
int16_t gifOffsetY = 0;
int16_t gifLargeur = 0;
int16_t gifHauteur = 0;
uint32_t gifTailleOctets = 0;
uint32_t prochaineFrameMs = 0;
uint8_t nombreGIFs = 0;
char gifActuel[GIF_PATH_MAX_LENGTH] = "/eye.gif";
char dernierGIFValide[GIF_PATH_MAX_LENGTH] = "/eye.gif";
char dernierMessage[120] = "Demarrage";

struct EntreeGIF {
  char nom[GIF_NAME_MAX_LENGTH];
  char chemin[GIF_PATH_MAX_LENGTH];
  uint32_t taille;
};

EntreeGIF catalogueGIFs[MAX_GIFS];

bool cheminGIFValide(const char* chemin);
int trouverGIFParChemin(const char* chemin);
bool changerGIF(const char* chemin);
bool lancerGIF(const char* chemin);

// Le GC9A01 fait 240 pixels de large.
uint16_t lignePixels[240];

// ============================================================
// Interface web
// ============================================================

const char PAGE_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="fr">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>DemonEye</title>
  <style>
    :root{color-scheme:dark;font-family:system-ui,-apple-system,Segoe UI,sans-serif;background:#09060d;color:#f5e9ff}
    body{margin:0;min-height:100vh;display:grid;place-items:center;background:radial-gradient(circle at top,#35114b,#09060d 55%)}
    main{width:min(92vw,680px);padding:24px}
    .card{border:1px solid #3b254f;border-radius:22px;background:rgba(11,8,18,.86);box-shadow:0 22px 70px rgba(0,0,0,.45);overflow:hidden}
    header{padding:26px 24px;background:linear-gradient(135deg,#d1224b,#5d1ea6)}
    h1{margin:0;font-size:clamp(2rem,8vw,4rem);line-height:.95;letter-spacing:-.05em}
    p{margin:.7rem 0 0;color:#f1d6ff}
    dl{display:grid;grid-template-columns:1fr 1fr;gap:1px;margin:0;background:#2b1d3a}
    div.row{display:grid;gap:6px;padding:18px;background:#100b18}
    dt{font-size:.78rem;text-transform:uppercase;letter-spacing:.08em;color:#b69bcf}
    dd{margin:0;font-size:1.2rem;font-weight:700;overflow-wrap:anywhere}
    button{border:0;border-radius:14px;padding:12px 16px;background:#ff315f;color:white;font-weight:800;font-size:1rem}
    #toggle{width:100%;margin-top:18px;padding:16px 18px}
    .library{margin-top:18px;padding:18px;border:1px solid #3b254f;border-radius:18px;background:#100b18}
    .library h2{margin:0 0 12px;font-size:1.1rem}
    .gif-row{display:grid;grid-template-columns:1fr auto;gap:10px;align-items:center;padding:12px 0;border-top:1px solid #2b1d3a}
    .gif-row:first-of-type{border-top:0}
    .gif-name{font-weight:800}.gif-meta{font-size:.85rem;color:#b69bcf;overflow-wrap:anywhere}
    .error{margin-top:14px;color:#ffb3c1;min-height:1.2rem}
    .ok{color:#52f29a}.bad{color:#ff6b6b}.muted{color:#b69bcf}
    @media (max-width:560px){dl{grid-template-columns:1fr}main{padding:14px}}
  </style>
</head>
<body>
  <main>
    <section class="card">
      <header>
        <h1>DemonEye</h1>
        <p>Interface Wi-Fi locale de l'œil Halloween</p>
      </header>
      <dl>
        <div class="row"><dt>Carte SD</dt><dd id="sd" class="muted">...</dd></div>
        <div class="row"><dt>GIF actif</dt><dd id="gif" class="muted">...</dd></div>
        <div class="row"><dt>Dimensions</dt><dd id="dim" class="muted">...</dd></div>
        <div class="row"><dt>Taille fichier</dt><dd id="size" class="muted">...</dd></div>
        <div class="row"><dt>Mémoire libre</dt><dd id="heap" class="muted">...</dd></div>
        <div class="row"><dt>Adresse IP</dt><dd id="ip" class="muted">...</dd></div>
        <div class="row"><dt>État</dt><dd id="state" class="muted">...</dd></div>
      </dl>
    </section>
    <button id="toggle" type="button">Lecture / pause</button>
    <section class="library">
      <h2>Animations disponibles</h2>
      <div id="gifs" class="muted">Chargement...</div>
      <div id="error" class="error"></div>
    </section>
  </main>
  <script>
    const fields = {
      sd: document.querySelector('#sd'),
      gif: document.querySelector('#gif'),
      dim: document.querySelector('#dim'),
      size: document.querySelector('#size'),
      heap: document.querySelector('#heap'),
      ip: document.querySelector('#ip'),
      state: document.querySelector('#state'),
      toggle: document.querySelector('#toggle'),
      gifs: document.querySelector('#gifs'),
      error: document.querySelector('#error')
    };
    function bytes(value){return value ? `${value.toLocaleString('fr-CH')} octets` : '-'}
    function esc(value){return String(value).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
    async function refresh(){
      const res = await fetch('/api/status',{cache:'no-store'});
      const s = await res.json();
      fields.sd.textContent = s.sd ? 'Disponible' : 'Indisponible';
      fields.sd.className = s.sd ? 'ok' : 'bad';
      fields.gif.textContent = s.gifName;
      fields.dim.textContent = s.gifWidth && s.gifHeight ? `${s.gifWidth} × ${s.gifHeight}px` : '-';
      fields.size.textContent = bytes(s.gifSize);
      fields.heap.textContent = bytes(s.freeHeap);
      fields.ip.textContent = s.ip;
      fields.state.textContent = s.paused ? 'Pause' : 'Lecture';
      fields.state.className = s.paused ? 'bad' : 'ok';
      fields.error.textContent = s.message || '';
    }
    async function refreshGifs(){
      const res = await fetch('/api/gifs',{cache:'no-store'});
      const list = await res.json();
      if (!list.length) {
        fields.gifs.textContent = 'Aucun GIF trouvé dans /gifs.';
        return;
      }
      fields.gifs.className = '';
      fields.gifs.innerHTML = list.map(g => `
        <div class="gif-row">
          <div>
            <div class="gif-name">${esc(g.name)} ${g.current ? '<span class="ok">● actif</span>' : ''}</div>
            <div class="gif-meta">${esc(g.path)} · ${bytes(g.size)}</div>
          </div>
          <button type="button" data-path="${esc(g.path)}">Lire</button>
        </div>
      `).join('');
      fields.gifs.querySelectorAll('button[data-path]').forEach(button => {
        button.addEventListener('click', async () => {
          const body = new URLSearchParams({path: button.dataset.path});
          const res = await fetch('/api/play', {method:'POST', body});
          const status = await res.json();
          fields.error.textContent = status.message || '';
          await refresh();
          await refreshGifs();
        });
      });
    }
    fields.toggle.addEventListener('click', async () => {
      await fetch('/api/toggle', {method:'POST'});
      refresh();
    });
    refresh();
    refreshGifs();
    setInterval(refresh, 2000);
    setInterval(refreshGifs, 5000);
  </script>
</body>
</html>
)rawliteral";

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

void afficherAdresseIP() {
  tft.fillScreen(GC9A01A_BLACK);
  tft.drawCircle(120, 120, 115, GC9A01A_CYAN);
  tft.drawCircle(120, 120, 114, GC9A01A_CYAN);

  tft.setTextWrap(false);
  tft.setTextColor(GC9A01A_CYAN);
  tft.setTextSize(2);
  tft.setCursor(42, 72);
  tft.println("Wi-Fi AP");

  tft.setTextSize(1);
  tft.setCursor(34, 110);
  tft.println(WIFI_AP_SSID);

  tft.setTextSize(2);
  tft.setCursor(42, 138);
  tft.println(adresseIP);
}

void envoyerPageAccueil() {
  server.send_P(200, "text/html", PAGE_HTML);
}

void envoyerEtatJSON() {
  char reponse[512];
  char ipTexte[16];

  snprintf(
    ipTexte,
    sizeof(ipTexte),
    "%u.%u.%u.%u",
    adresseIP[0],
    adresseIP[1],
    adresseIP[2],
    adresseIP[3]
  );

  snprintf(
    reponse,
    sizeof(reponse),
    "{\"sd\":%s,\"gifName\":\"%s\",\"gifWidth\":%d,\"gifHeight\":%d,"
    "\"gifSize\":%lu,\"freeHeap\":%lu,\"ip\":\"%s\",\"paused\":%s,"
    "\"gifActive\":%s,\"message\":\"%s\"}",
    carteSDDisponible ? "true" : "false",
    gifActuel,
    gifLargeur,
    gifHauteur,
    static_cast<unsigned long>(gifTailleOctets),
    static_cast<unsigned long>(ESP.getFreeHeap()),
    ipTexte,
    lectureEnPause ? "true" : "false",
    gifActif ? "true" : "false",
    dernierMessage
  );

  server.send(200, "application/json", reponse);
}

void envoyerListeGIFsJSON() {
  char morceau[192];

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("[");

  for (uint8_t i = 0; i < nombreGIFs; i++) {
    snprintf(
      morceau,
      sizeof(morceau),
      "%s{\"name\":\"%s\",\"path\":\"%s\",\"size\":%lu,\"current\":%s}",
      i == 0 ? "" : ",",
      catalogueGIFs[i].nom,
      catalogueGIFs[i].chemin,
      static_cast<unsigned long>(catalogueGIFs[i].taille),
      strcmp(catalogueGIFs[i].chemin, gifActuel) == 0 ? "true" : "false"
    );

    server.sendContent(morceau);
  }

  server.sendContent("]");
}

void jouerGIFDepuisWeb() {
  if (!server.hasArg("path")) {
    strncpy(dernierMessage, "Parametre path manquant", sizeof(dernierMessage) - 1);
    dernierMessage[sizeof(dernierMessage) - 1] = '\0';
    Serial.println(dernierMessage);
    server.send(400, "application/json", "{\"ok\":false,\"message\":\"Parametre path manquant\"}");
    return;
  }

  String cheminDemande = server.arg("path");

  if (
    !cheminGIFValide(cheminDemande.c_str()) ||
    trouverGIFParChemin(cheminDemande.c_str()) < 0
  ) {
    strncpy(dernierMessage, "Chemin GIF refuse", sizeof(dernierMessage) - 1);
    dernierMessage[sizeof(dernierMessage) - 1] = '\0';
    Serial.print("Chemin GIF refuse : ");
    Serial.println(cheminDemande);
    server.send(400, "application/json", "{\"ok\":false,\"message\":\"Chemin GIF refuse\"}");
    return;
  }

  if (changerGIF(cheminDemande.c_str())) {
    envoyerEtatJSON();
  } else {
    server.send(500, "application/json", "{\"ok\":false,\"message\":\"GIF invalide, retour automatique\"}");
  }
}

void basculerLecture() {
  lectureEnPause = !lectureEnPause;

  if (!lectureEnPause) {
    prochaineFrameMs = millis();
  }

  envoyerEtatJSON();
}

void envoyerIntrouvable() {
  server.send(404, "text/plain", "Route introuvable");
}

void demarrerWiFi() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
  adresseIP = WiFi.softAPIP();

  server.on("/", HTTP_GET, envoyerPageAccueil);
  server.on("/api/status", HTTP_GET, envoyerEtatJSON);
  server.on("/api/gifs", HTTP_GET, envoyerListeGIFsJSON);
  server.on("/api/play", HTTP_POST, jouerGIFDepuisWeb);
  server.on("/api/toggle", HTTP_POST, basculerLecture);
  server.onNotFound(envoyerIntrouvable);
  server.begin();

  Serial.print("Point d'acces Wi-Fi : ");
  Serial.println(WIFI_AP_SSID);
  Serial.print("Adresse IP : ");
  Serial.println(adresseIP);
}

void attendreAvecServeur(uint32_t dureeMs) {
  uint32_t depart = millis();

  while (millis() - depart < dureeMs) {
    server.handleClient();
    delay(1);
  }
}

bool extensionGIF(const char* nom) {
  size_t longueur = strlen(nom);

  if (longueur < 5) {
    return false;
  }

  const char* extension = nom + longueur - 4;
  return (
    extension[0] == '.' &&
    tolower(extension[1]) == 'g' &&
    tolower(extension[2]) == 'i' &&
    tolower(extension[3]) == 'f'
  );
}

bool cheminGIFValide(const char* chemin) {
  if (chemin == nullptr) {
    return false;
  }

  size_t longueur = strlen(chemin);

  if (
    longueur <= strlen(GIF_DIRECTORY) + 1 ||
    longueur >= GIF_PATH_MAX_LENGTH
  ) {
    return false;
  }

  if (strncmp(chemin, "/gifs/", 6) != 0) {
    return false;
  }

  if (
    strstr(chemin, "..") != nullptr ||
    strchr(chemin, '\\') != nullptr ||
    strchr(chemin, '"') != nullptr
  ) {
    return false;
  }

  for (size_t i = 0; i < longueur; i++) {
    if (static_cast<uint8_t>(chemin[i]) < 32) {
      return false;
    }
  }

  return extensionGIF(chemin);
}

int trouverGIFParChemin(const char* chemin) {
  for (uint8_t i = 0; i < nombreGIFs; i++) {
    if (strcmp(catalogueGIFs[i].chemin, chemin) == 0) {
      return i;
    }
  }

  return -1;
}

void ajouterGIFAuCatalogue(const char* nom, const char* chemin, uint32_t taille) {
  if (nombreGIFs >= MAX_GIFS || !cheminGIFValide(chemin)) {
    return;
  }

  if (trouverGIFParChemin(chemin) >= 0) {
    return;
  }

  strncpy(catalogueGIFs[nombreGIFs].nom, nom, GIF_NAME_MAX_LENGTH - 1);
  catalogueGIFs[nombreGIFs].nom[GIF_NAME_MAX_LENGTH - 1] = '\0';

  strncpy(catalogueGIFs[nombreGIFs].chemin, chemin, GIF_PATH_MAX_LENGTH - 1);
  catalogueGIFs[nombreGIFs].chemin[GIF_PATH_MAX_LENGTH - 1] = '\0';

  catalogueGIFs[nombreGIFs].taille = taille;
  nombreGIFs++;
}

void scannerCatalogueGIFs() {
  nombreGIFs = 0;

  if (!carteSDDisponible) {
    Serial.println("Scan /gifs ignore : carte SD indisponible.");
    return;
  }

  File dossier = SD.open(GIF_DIRECTORY);

  if (!dossier || !dossier.isDirectory()) {
    Serial.println("Dossier /gifs introuvable.");
    if (dossier) {
      dossier.close();
    }
    return;
  }

  while (nombreGIFs < MAX_GIFS) {
    File entree = dossier.openNextFile();

    if (!entree) {
      break;
    }

    if (!entree.isDirectory()) {
      const char* nomComplet = entree.name();
      const char* nomFichier = strrchr(nomComplet, '/');
      nomFichier = nomFichier == nullptr ? nomComplet : nomFichier + 1;

      if (extensionGIF(nomFichier)) {
        char chemin[GIF_PATH_MAX_LENGTH];

        snprintf(
          chemin,
          sizeof(chemin),
          "%s/%s",
          GIF_DIRECTORY,
          nomFichier
        );

        ajouterGIFAuCatalogue(nomFichier, chemin, entree.size());
      }
    }

    entree.close();
  }

  dossier.close();

  Serial.printf("GIFs trouves dans /gifs : %u\n", nombreGIFs);
}

void fermerAnimationGIF() {
  gif.close();

  if (gifFile) {
    gifFile.close();
  }

  gifActif = false;
}

// ============================================================
// Ouverture et demarrage de l'animation
// ============================================================

bool lancerGIF(const char* chemin) {
  if (!carteSDDisponible || chemin == nullptr) {
    gifActif = false;
    return false;
  }

  File test = SD.open(chemin, FILE_READ);

  if (!test) {
    Serial.print("ERREUR : ouverture impossible : ");
    Serial.println(chemin);
    gifActif = false;
    return false;
  }

  gifTailleOctets = test.size();
  test.close();

  if (!gif.open(
        chemin,
        ouvrirFichierGIF,
        fermerFichierGIF,
        lireFichierGIF,
        positionnerFichierGIF,
        dessinerGIF
      )) {
    Serial.print("ERREUR : GIF invalide : ");
    Serial.println(chemin);
    if (gifFile) {
      gifFile.close();
    }
    gifActif = false;
    return false;
  }

  strncpy(gifActuel, chemin, sizeof(gifActuel) - 1);
  gifActuel[sizeof(gifActuel) - 1] = '\0';

  int16_t largeur = gif.getCanvasWidth();
  int16_t hauteur = gif.getCanvasHeight();
  gifLargeur = largeur;
  gifHauteur = hauteur;

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
  gifDisponible = true;
  prochaineFrameMs = millis();
  strncpy(dernierGIFValide, gifActuel, sizeof(dernierGIFValide) - 1);
  dernierGIFValide[sizeof(dernierGIFValide) - 1] = '\0';
  strncpy(dernierMessage, "Lecture GIF active", sizeof(dernierMessage) - 1);
  dernierMessage[sizeof(dernierMessage) - 1] = '\0';

  Serial.print("Animation lancee : ");
  Serial.println(gifActuel);

  return true;
}

bool changerGIF(const char* chemin) {
  char retour[GIF_PATH_MAX_LENGTH];

  strncpy(retour, dernierGIFValide, sizeof(retour) - 1);
  retour[sizeof(retour) - 1] = '\0';

  fermerAnimationGIF();
  tft.fillScreen(GC9A01A_BLACK);

  if (lancerGIF(chemin)) {
    Serial.print("GIF selectionne depuis le web : ");
    Serial.println(gifActuel);
    return true;
  }

  snprintf(
    dernierMessage,
    sizeof(dernierMessage),
    "GIF invalide, retour a %s",
    retour
  );
  Serial.println(dernierMessage);

  bool retourOK = lancerGIF(retour);

  if (!retourOK && strcmp(retour, FALLBACK_GIF_PATH) != 0) {
    retourOK = lancerGIF(FALLBACK_GIF_PATH);
  }

  if (retourOK) {
    snprintf(
      dernierMessage,
      sizeof(dernierMessage),
      "GIF invalide, retour a %s",
      gifActuel
    );
  } else {
    strncpy(dernierMessage, "GIF invalide, aucun secours disponible", sizeof(dernierMessage) - 1);
    dernierMessage[sizeof(dernierMessage) - 1] = '\0';
  }

  return false;
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

  demarrerWiFi();
  afficherAdresseIP();
  attendreAvecServeur(2500);

  Serial.println("Initialisation de la carte SD a 10 MHz...");

  if (!SD.begin(SD_CS, SPI, SD_FREQUENCY)) {
    Serial.println("ERREUR : carte SD inaccessible.");
    afficherErreur("ERREUR", "Carte SD inaccessible");
  } else {
    carteSDDisponible = true;
    Serial.println("Carte SD initialisee.");
  }

  if (carteSDDisponible) {
    scannerCatalogueGIFs();
  }

  if (!carteSDDisponible) {
    Serial.println("Animation GIF inactive, serveur web disponible.");
    return;
  }

  // Adafruit_GFX utilise les valeurs RGB565 natives de l'ESP32.
  gif.begin(LITTLE_ENDIAN_PIXELS);

  if (SD.exists(FALLBACK_GIF_PATH) && lancerGIF(FALLBACK_GIF_PATH)) {
    return;
  }

  Serial.println("GIF de secours /eye.gif indisponible ou invalide.");

  if (nombreGIFs > 0 && lancerGIF(catalogueGIFs[0].chemin)) {
    return;
  }

  gifDisponible = false;
  strncpy(dernierMessage, "Aucun GIF valide disponible", sizeof(dernierMessage) - 1);
  dernierMessage[sizeof(dernierMessage) - 1] = '\0';
  afficherErreur("ERREUR", "Aucun GIF valide");
  Serial.println(dernierMessage);
}

// ============================================================
// Lecture en boucle infinie
// ============================================================

void loop() {
  server.handleClient();

  if (lectureEnPause) {
    delay(1);
    return;
  }

  uint32_t maintenant = millis();

  if (!gifActif) {
    if (
      gifDisponible &&
      static_cast<int32_t>(maintenant - prochaineFrameMs) >= 0
    ) {
      if (!lancerGIF(gifActuel)) {
        afficherErreur("ERREUR", "Redemarrage GIF impossible");
      }
    }

    delay(1);
    return;
  }

  if (static_cast<int32_t>(maintenant - prochaineFrameMs) < 0) {
    delay(1);
    return;
  }

  int delaiFrameMs = 0;

  // false : ne pas bloquer avec delay(); le rythme est gere avec millis().
  if (!gif.playFrame(false, &delaiFrameMs)) {
    gif.close();
    gifActif = false;
    prochaineFrameMs = millis() + GIF_RESTART_DELAY_MS;
  } else {
    if (delaiFrameMs < 1) {
      delaiFrameMs = 1;
    }

    prochaineFrameMs = millis() + delaiFrameMs;
  }

  server.handleClient();
  delay(1);
}
