#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Update.h>
#include <stdlib.h>
#include <ctype.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include <AnimatedGIF.h>

#if __has_include("config.h")
#include "config.h"
#else
#define OTA_AUTH_USERNAME "admin"
#define OTA_AUTH_PASSWORD "replace-with-at-least-8-characters"
#endif

#ifndef DEMON_EYE_ALLOW_EXAMPLE_OTA_PASSWORD
  static_assert(
    !(
      sizeof(OTA_AUTH_PASSWORD) == sizeof("replace-with-at-least-8-characters") &&
      OTA_AUTH_PASSWORD[0] == 'r' &&
      OTA_AUTH_PASSWORD[8] == 'w' &&
      OTA_AUTH_PASSWORD[18] == 'a' &&
      OTA_AUTH_PASSWORD[33] == 's'
    ),
    "Copiez config.example.h vers config.h et changez OTA_AUTH_PASSWORD avant compilation."
  );
#endif
static_assert(
  sizeof(OTA_AUTH_PASSWORD) >= 9,
  "Le mot de passe OTA doit contenir au moins 8 caracteres."
);

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
const uint32_t MAX_UPLOAD_SIZE_BYTES = 10UL * 1024UL * 1024UL;
const char* UPLOAD_TEMP_PATH = "/gifs/.upload.tmp";
const uint16_t VITESSES_LECTURE[] = {50, 75, 100, 125, 150, 200};
const uint8_t NOMBRE_VITESSES = sizeof(VITESSES_LECTURE) / sizeof(VITESSES_LECTURE[0]);
const char FIRMWARE_VERSION[] = "0.4.0";

// ============================================================
// Point d'acces Wi-Fi
// ============================================================

const char WIFI_AP_SSID[] = "DemonEye";
const char WIFI_AP_PASSWORD[] = "DemonEye2026";
static_assert(sizeof(WIFI_AP_PASSWORD) >= 9, "Le mot de passe Wi-Fi AP doit contenir au moins 8 caracteres.");

WebServer server(80);
Preferences preferences;

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
bool ecritureSDEnCours = false;
bool uploadErreur = false;
bool uploadOK = false;
bool otaErreur = false;
bool otaOK = false;
bool lectureEtaitEnPauseAvantUpload = false;
bool modeAleatoire = false;
bool repetitionActive = true;
int16_t gifOffsetX = 0;
int16_t gifOffsetY = 0;
int16_t gifLargeur = 0;
int16_t gifHauteur = 0;
uint32_t gifTailleOctets = 0;
uint32_t prochaineFrameMs = 0;
uint8_t nombreGIFs = 0;
uint8_t uploadSignatureOctets = 0;
uint16_t vitesseLecturePourcent = 100;
char gifActuel[GIF_PATH_MAX_LENGTH] = "/eye.gif";
char dernierGIFValide[GIF_PATH_MAX_LENGTH] = "/eye.gif";
char dernierMessage[120] = "Demarrage";
char gifAvantEcriture[GIF_PATH_MAX_LENGTH] = "/eye.gif";
char uploadCheminFinal[GIF_PATH_MAX_LENGTH] = "";
char uploadNomFinal[GIF_NAME_MAX_LENGTH] = "";
char uploadMessage[120] = "";
char otaMessage[160] = "";
uint8_t uploadSignature[6];

File uploadFile;

struct EntreeGIF {
  char nom[GIF_NAME_MAX_LENGTH];
  char chemin[GIF_PATH_MAX_LENGTH];
  uint32_t taille;
};

EntreeGIF* catalogueGIFs = nullptr;

bool cheminGIFValide(const char* chemin);
bool nomFichierGIFValide(const char* nom);
int trouverGIFParChemin(const char* chemin);
bool changerGIF(const char* chemin);
bool lancerGIF(const char* chemin);
void sauvegarderPreferencesLecture();
void chargerPreferencesLecture();
void definirMessage(const char* message);
void redemarrerGIFActuel();
void jouerGIFSuivant();
void jouerGIFPrecedent();
void envoyerLectureJSON();
void modifierLectureDepuisWeb();
void traiterUploadGIF();
void terminerUploadGIF();
void supprimerGIFDepuisWeb();
bool authentifierOTA();
void envoyerPageOTA();
void traiterUploadOTA();
void terminerUploadOTA();
bool initialiserCatalogueGIFs();

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
    :root{color-scheme:dark;font-family:system-ui,-apple-system,Segoe UI,sans-serif;background:#08040b;color:#f8edff}
    body{margin:0;min-height:100vh;background:radial-gradient(circle at top,#421150,#08040b 58%);font-size:16px}
    main{width:min(94vw,760px);margin:0 auto;padding:16px 0 28px}
    .card,.panel{border:1px solid #442759;border-radius:22px;background:rgba(13,8,20,.9);box-shadow:0 22px 70px rgba(0,0,0,.45);overflow:hidden}
    header{padding:26px 22px;background:linear-gradient(135deg,#f02d55,#6120a8)}
    h1{margin:0;font-size:clamp(2.2rem,10vw,4.8rem);line-height:.9;letter-spacing:-.06em}h2{margin:0 0 14px;font-size:1.15rem}.panel{margin-top:16px;padding:18px}
    p{margin:.7rem 0 0;color:#f3d8ff}a{color:#ffd166;font-weight:800}dl{display:grid;grid-template-columns:1fr 1fr;gap:1px;margin:0;background:#2b1d3a}
    div.row{display:grid;gap:6px;padding:16px;background:#100b18}dt{font-size:.75rem;text-transform:uppercase;letter-spacing:.08em;color:#bfa5d8}dd{margin:0;font-size:1.08rem;font-weight:800;overflow-wrap:anywhere}
    button,select{border:1px solid #5a3672;border-radius:14px;padding:12px;background:#21122f;color:#fff;font-weight:800;font-size:1rem}button{background:#ff315f;border:0}button.secondary{background:#5d2bc3}button.danger{background:#652034}button:disabled{opacity:.45}
    .buttons{display:grid;grid-template-columns:repeat(4,1fr);gap:10px}.toggles{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-top:12px}label{display:grid;gap:7px;color:#cdb7e2;font-weight:700}
    .gif-row{display:grid;grid-template-columns:1fr auto auto;gap:10px;align-items:center;padding:12px 0;border-top:1px solid #2b1d3a}.gif-row:first-of-type{border-top:0}.gif-name{font-weight:900}.gif-meta{font-size:.86rem;color:#b69bcf;overflow-wrap:anywhere}
    form{display:grid;gap:12px;margin-top:8px}input[type=file]{width:100%;color:#f5e9ff}progress{width:100%;height:18px}.error{margin-top:14px;color:#ffb3c1;min-height:1.2rem}.ok{color:#52f29a}.bad{color:#ff6b6b}.muted{color:#b69bcf}.mode{color:#ffd166}
    @media (max-width:620px){dl,.gif-row,.buttons,.toggles{grid-template-columns:1fr}main{padding:10px 0 20px}.panel{padding:14px}}
  </style>
</head>
<body><main>
  <section class="card"><header><h1>DemonEye</h1><p>Interface Wi-Fi locale de l'oeil Halloween</p></header><dl>
    <div class="row"><dt>Carte SD</dt><dd id="sd" class="muted">...</dd></div><div class="row"><dt>GIF actif</dt><dd id="gif" class="muted">...</dd></div><div class="row"><dt>Dimensions</dt><dd id="dim" class="muted">...</dd></div><div class="row"><dt>Taille fichier</dt><dd id="size" class="muted">...</dd></div><div class="row"><dt>Memoire libre</dt><dd id="heap" class="muted">...</dd></div><div class="row"><dt>Adresse IP</dt><dd id="ip" class="muted">...</dd></div><div class="row"><dt>Lecture</dt><dd id="state" class="muted">...</dd></div><div class="row"><dt>Mode</dt><dd id="mode" class="mode">...</dd></div><div class="row"><dt>Firmware</dt><dd id="fw" class="muted">...</dd></div>
  </dl></section>
  <section class="panel"><h2>Commandes</h2><div class="buttons"><button type="button" data-action="previous">Precedent</button><button id="toggle" type="button" data-action="toggle">Lecture / pause</button><button type="button" data-action="next">Suivant</button><button type="button" data-action="restart" class="secondary">Redemarrer</button></div><div class="toggles"><label>Vitesse<select id="speed"><option value="50">0,5x</option><option value="75">0,75x</option><option value="100">1x</option><option value="125">1,25x</option><option value="150">1,5x</option><option value="200">2x</option></select></label><label>Aleatoire<select id="random"><option value="0">Desactive</option><option value="1">Active</option></select></label><label>Repetition<select id="repeat"><option value="1">Activee</option><option value="0">Desactivee</option></select></label></div></section>
  <section class="panel"><h2>Animations disponibles</h2><div id="gifs" class="muted">Chargement...</div></section>
  <section class="panel"><h2>Ajouter un GIF</h2><form id="upload-form" enctype="multipart/form-data"><input id="gif-file" name="gif" type="file" accept=".gif,image/gif" required><button type="submit">Televerser</button><progress id="progress" value="0" max="100"></progress></form><div id="error" class="error"></div></section>
  <section class="panel"><h2>Maintenance</h2><p>Firmware OTA protege : <a href="/ota">ouvrir la page de mise a jour</a></p></section>
</main><script>
const fields={sd:q('#sd'),gif:q('#gif'),dim:q('#dim'),size:q('#size'),heap:q('#heap'),ip:q('#ip'),state:q('#state'),mode:q('#mode'),fw:q('#fw'),toggle:q('#toggle'),speed:q('#speed'),random:q('#random'),repeat:q('#repeat'),gifs:q('#gifs'),error:q('#error'),uploadForm:q('#upload-form'),gifFile:q('#gif-file'),progress:q('#progress')};
function q(s){return document.querySelector(s)}function bytes(v){return v?`${Number(v).toLocaleString('fr-CH')} octets`:'-'}function esc(v){return String(v).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}function speedLabel(v){return({50:'0,5x',75:'0,75x',100:'1x',125:'1,25x',150:'1,5x',200:'2x'}[v]||`${v/100}x`)}
async function refresh(){const res=await fetch('/api/status',{cache:'no-store'});const s=await res.json();fields.sd.textContent=s.sd?'Disponible':'Indisponible';fields.sd.className=s.sd?'ok':'bad';fields.gif.textContent=s.gifName;fields.dim.textContent=s.gifWidth&&s.gifHeight?`${s.gifWidth} x ${s.gifHeight}px`:'-';fields.size.textContent=bytes(s.gifSize);fields.heap.textContent=bytes(s.freeHeap);fields.ip.textContent=s.ip;fields.state.textContent=s.paused?'Pause':'Lecture';fields.state.className=s.paused?'bad':'ok';fields.mode.textContent=`${s.random?'Aleatoire':'Normal'} ? repetition ${s.repeat?'on':'off'} ? ${speedLabel(s.speedPercent)}`;fields.fw.textContent=s.firmwareVersion||'-';fields.speed.value=String(s.speedPercent||100);fields.random.value=s.random?'1':'0';fields.repeat.value=s.repeat?'1':'0';fields.error.textContent=s.message||''}
async function refreshGifs(){const res=await fetch('/api/gifs',{cache:'no-store'});const list=await res.json();if(!list.length){fields.gifs.textContent='Aucun GIF trouve dans /gifs.';return}fields.gifs.className='';fields.gifs.innerHTML=list.map(g=>`<div class="gif-row"><div><div class="gif-name">${esc(g.name)} ${g.current?'<span class="ok">actif</span>':''}</div><div class="gif-meta">${esc(g.path)} ? ${bytes(g.size)}</div></div><button type="button" data-play="${esc(g.path)}">Lire</button><button class="danger" type="button" data-delete="${esc(g.path)}" ${g.current?'disabled':''}>Supprimer</button></div>`).join('');fields.gifs.querySelectorAll('button[data-play]').forEach(b=>b.onclick=()=>playGif(b.dataset.play));fields.gifs.querySelectorAll('button[data-delete]').forEach(b=>b.onclick=()=>deleteGif(b.dataset.delete))}
async function updatePlayback(params){const res=await fetch('/api/playback',{method:'POST',body:new URLSearchParams(params)});const s=await res.json();fields.error.textContent=s.message||'';await refresh();await refreshGifs()}async function playGif(path){const res=await fetch('/api/play',{method:'POST',body:new URLSearchParams({path})});const s=await res.json();fields.error.textContent=s.message||'';await refresh();await refreshGifs()}async function deleteGif(path){if(!confirm(`Supprimer ${path} ?`))return;const res=await fetch('/api/delete',{method:'POST',body:new URLSearchParams({path})});const s=await res.json();fields.error.textContent=s.message||'';await refresh();await refreshGifs()}
fields.toggle.onclick=()=>updatePlayback({action:'toggle'});document.querySelectorAll('button[data-action]').forEach(b=>{if(b.id!=='toggle')b.onclick=()=>updatePlayback({action:b.dataset.action})});fields.speed.onchange=()=>updatePlayback({speed:fields.speed.value});fields.random.onchange=()=>updatePlayback({random:fields.random.value});fields.repeat.onchange=()=>updatePlayback({repeat:fields.repeat.value});fields.uploadForm.onsubmit=e=>{e.preventDefault();const file=fields.gifFile.files[0];if(!file)return;const data=new FormData();data.append('gif',file,file.name);const xhr=new XMLHttpRequest();fields.progress.value=0;fields.error.textContent='Televersement en cours...';xhr.upload.onprogress=e=>{if(e.lengthComputable)fields.progress.value=Math.round(e.loaded*100/e.total)};xhr.onload=async()=>{let s={};try{s=JSON.parse(xhr.responseText||'{}')}catch(e){}fields.error.textContent=s.message||(xhr.status<400?'Televersement termine':'Erreur televersement');fields.gifFile.value='';fields.progress.value=xhr.status<400?100:0;await refresh();await refreshGifs()};xhr.onerror=()=>{fields.error.textContent='Erreur reseau pendant le televersement';fields.progress.value=0};xhr.open('POST','/api/upload');xhr.send(data)};
refresh();refreshGifs();setInterval(refresh,2000);setInterval(refreshGifs,5000);
</script></body></html>
)rawliteral";

const char OTA_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="fr">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>DemonEye OTA</title>
  <style>
    :root{color-scheme:dark;font-family:system-ui,-apple-system,Segoe UI,sans-serif;background:#08040b;color:#f8edff}
    body{margin:0;min-height:100vh;display:grid;place-items:center;background:radial-gradient(circle at top,#421150,#08040b 58%)}
    main{width:min(92vw,560px);padding:20px}.panel{border:1px solid #442759;border-radius:22px;background:#100b18;padding:22px;box-shadow:0 22px 70px rgba(0,0,0,.45)}
    h1{margin:0 0 8px;font-size:2rem}p{color:#cdb7e2}form{display:grid;gap:14px;margin-top:18px}button{border:0;border-radius:14px;padding:14px;background:#ff315f;color:white;font-weight:900;font-size:1rem}
    input{color:#fff}progress{width:100%;height:18px}.msg{min-height:1.3rem;color:#ffb3c1}a{color:#ffd166}
  </style>
</head>
<body><main><section class="panel">
  <h1>Mise a jour firmware</h1>
  <p>Envoyez un fichier <code>.bin</code> compile pour cette carte ESP32-S2. L'appareil redemarre uniquement si Update confirme la mise a jour.</p>
  <form id="form" enctype="multipart/form-data"><input id="firmware" name="firmware" type="file" accept=".bin,application/octet-stream" required><button type="submit">Mettre a jour</button><progress id="progress" value="0" max="100"></progress></form>
  <p class="msg" id="msg"></p><p><a href="/">Retour interface</a></p>
</section></main><script>
const form=document.querySelector('#form'),file=document.querySelector('#firmware'),progress=document.querySelector('#progress'),msg=document.querySelector('#msg');
form.onsubmit=e=>{e.preventDefault();if(!file.files[0])return;const data=new FormData();data.append('firmware',file.files[0],file.files[0].name);const xhr=new XMLHttpRequest();progress.value=0;msg.textContent='Mise a jour en cours. Ne coupez pas l alimentation.';xhr.upload.onprogress=e=>{if(e.lengthComputable)progress.value=Math.round(e.loaded*100/e.total)};xhr.onload=()=>{let s={};try{s=JSON.parse(xhr.responseText||'{}')}catch(e){}msg.textContent=s.message||(xhr.status<400?'Mise a jour reussie, redemarrage...':'Mise a jour echouee');progress.value=xhr.status<400?100:0};xhr.onerror=()=>{msg.textContent='Erreur reseau pendant OTA';progress.value=0};xhr.open('POST','/ota/update');xhr.send(data)};
</script></body></html>
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

bool authentifierOTA() {
  if (server.authenticate(OTA_AUTH_USERNAME, OTA_AUTH_PASSWORD)) {
    return true;
  }

  server.requestAuthentication(BASIC_AUTH, "DemonEye OTA");
  return false;
}

void envoyerPageOTA() {
  if (!authentifierOTA()) {
    return;
  }

  server.send_P(200, "text/html", OTA_HTML);
}

void envoyerEtatJSON() {
  char reponse[640];
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
    "\"gifActive\":%s,\"sdBusy\":%s,\"speedPercent\":%u,"
    "\"random\":%s,\"repeat\":%s,\"mode\":\"%s\","
    "\"firmwareVersion\":\"%s\",\"message\":\"%s\"}",
    carteSDDisponible ? "true" : "false",
    gifActuel,
    gifLargeur,
    gifHauteur,
    static_cast<unsigned long>(gifTailleOctets),
    static_cast<unsigned long>(ESP.getFreeHeap()),
    ipTexte,
    lectureEnPause ? "true" : "false",
    gifActif ? "true" : "false",
    ecritureSDEnCours ? "true" : "false",
    vitesseLecturePourcent,
    modeAleatoire ? "true" : "false",
    repetitionActive ? "true" : "false",
    modeAleatoire ? "aleatoire" : "normal",
    FIRMWARE_VERSION,
    dernierMessage
  );

  server.send(200, "application/json", reponse);
}

void envoyerLectureJSON() {
  envoyerEtatJSON();
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
  if (ecritureSDEnCours) {
    server.send(409, "application/json", "{\"ok\":false,\"message\":\"Operation SD en cours\"}");
    return;
  }

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
  if (ecritureSDEnCours) {
    server.send(409, "application/json", "{\"ok\":false,\"message\":\"Operation SD en cours\"}");
    return;
  }

  lectureEnPause = !lectureEnPause;

  if (!lectureEnPause) {
    prochaineFrameMs = millis();
  }

  definirMessage(lectureEnPause ? "Lecture en pause" : "Lecture active");
  envoyerEtatJSON();
}

void modifierLectureDepuisWeb() {
  if (ecritureSDEnCours) {
    server.send(409, "application/json", "{\"ok\":false,\"message\":\"Operation SD en cours\"}");
    return;
  }

  bool sauvegardeNecessaire = false;

  if (server.hasArg("speed")) {
    uint16_t vitesse = server.arg("speed").toInt();
    bool valide = false;

    for (uint8_t i = 0; i < NOMBRE_VITESSES; i++) {
      if (VITESSES_LECTURE[i] == vitesse) {
        valide = true;
        break;
      }
    }

    if (!valide) {
      definirMessage("Vitesse refusee");
      server.send(400, "application/json", "{\"ok\":false,\"message\":\"Vitesse refusee\"}");
      return;
    }

    vitesseLecturePourcent = vitesse;
    sauvegardeNecessaire = true;
  }

  if (server.hasArg("random")) {
    modeAleatoire = server.arg("random") == "1" || server.arg("random") == "true";
    sauvegardeNecessaire = true;
  }

  if (server.hasArg("repeat")) {
    repetitionActive = server.arg("repeat") == "1" || server.arg("repeat") == "true";
    sauvegardeNecessaire = true;
  }

  if (server.hasArg("action")) {
    String action = server.arg("action");

    if (action == "play") {
      lectureEnPause = false;
      prochaineFrameMs = millis();
      definirMessage("Lecture active");
    } else if (action == "pause") {
      lectureEnPause = true;
      definirMessage("Lecture en pause");
    } else if (action == "toggle") {
      lectureEnPause = !lectureEnPause;
      prochaineFrameMs = millis();
      definirMessage(lectureEnPause ? "Lecture en pause" : "Lecture active");
    } else if (action == "restart") {
      redemarrerGIFActuel();
    } else if (action == "next") {
      jouerGIFSuivant();
    } else if (action == "previous") {
      jouerGIFPrecedent();
    } else {
      definirMessage("Action lecture refusee");
      server.send(400, "application/json", "{\"ok\":false,\"message\":\"Action lecture refusee\"}");
      return;
    }
  }

  if (sauvegardeNecessaire) {
    sauvegarderPreferencesLecture();
    definirMessage("Parametres sauvegardes");
  }

  envoyerLectureJSON();
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
  server.on("/api/playback", HTTP_GET, envoyerLectureJSON);
  server.on("/api/playback", HTTP_POST, modifierLectureDepuisWeb);
  server.on("/api/gifs", HTTP_GET, envoyerListeGIFsJSON);
  server.on("/api/play", HTTP_POST, jouerGIFDepuisWeb);
  server.on("/api/upload", HTTP_POST, terminerUploadGIF, traiterUploadGIF);
  server.on("/api/delete", HTTP_POST, supprimerGIFDepuisWeb);
  server.on("/api/toggle", HTTP_POST, basculerLecture);
  server.on("/ota", HTTP_GET, envoyerPageOTA);
  server.on("/ota/update", HTTP_POST, terminerUploadOTA, traiterUploadOTA);
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
    strchr(chemin + 6, '/') != nullptr ||
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

bool nomFichierGIFValide(const char* nom) {
  if (nom == nullptr) {
    return false;
  }

  size_t longueur = strlen(nom);

  if (longueur < 5 || longueur >= GIF_NAME_MAX_LENGTH) {
    return false;
  }

  if (
    strstr(nom, "..") != nullptr ||
    strchr(nom, '/') != nullptr ||
    strchr(nom, '\\') != nullptr ||
    strchr(nom, '"') != nullptr ||
    nom[0] == '.'
  ) {
    return false;
  }

  for (size_t i = 0; i < longueur; i++) {
    if (static_cast<uint8_t>(nom[i]) < 32) {
      return false;
    }
  }

  return extensionGIF(nom);
}

int trouverGIFParChemin(const char* chemin) {
  if (catalogueGIFs == nullptr) {
    return -1;
  }

  for (uint8_t i = 0; i < nombreGIFs; i++) {
    if (strcmp(catalogueGIFs[i].chemin, chemin) == 0) {
      return i;
    }
  }

  return -1;
}

void ajouterGIFAuCatalogue(const char* nom, const char* chemin, uint32_t taille) {
  if (catalogueGIFs == nullptr || nombreGIFs >= MAX_GIFS || !cheminGIFValide(chemin)) {
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

bool initialiserCatalogueGIFs() {
  if (catalogueGIFs != nullptr) {
    return true;
  }

  catalogueGIFs = static_cast<EntreeGIF*>(calloc(MAX_GIFS, sizeof(EntreeGIF)));

  if (catalogueGIFs == nullptr) {
    nombreGIFs = 0;
    definirMessage("Memoire insuffisante pour catalogue GIF");
    return false;
  }

  return true;
}

void scannerCatalogueGIFs() {
  nombreGIFs = 0;

  if (!initialiserCatalogueGIFs()) {
    Serial.println("Scan /gifs ignore : catalogue indisponible.");
    return;
  }

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

void definirMessage(const char* message) {
  strncpy(dernierMessage, message, sizeof(dernierMessage) - 1);
  dernierMessage[sizeof(dernierMessage) - 1] = '\0';
  Serial.println(dernierMessage);
}

void suspendreAnimationPourSD() {
  lectureEtaitEnPauseAvantUpload = lectureEnPause;
  strncpy(gifAvantEcriture, gifActuel, sizeof(gifAvantEcriture) - 1);
  gifAvantEcriture[sizeof(gifAvantEcriture) - 1] = '\0';
  lectureEnPause = true;
  ecritureSDEnCours = true;
  fermerAnimationGIF();
}

void reprendreAnimationApresSD() {
  char messageAvantReprise[sizeof(dernierMessage)];

  strncpy(messageAvantReprise, dernierMessage, sizeof(messageAvantReprise) - 1);
  messageAvantReprise[sizeof(messageAvantReprise) - 1] = '\0';

  ecritureSDEnCours = false;
  lectureEnPause = lectureEtaitEnPauseAvantUpload;

  if (!lectureEnPause && carteSDDisponible) {
    if (!lancerGIF(gifAvantEcriture)) {
      lancerGIF(dernierGIFValide);
    }
  }

  if (messageAvantReprise[0] != '\0') {
    strncpy(dernierMessage, messageAvantReprise, sizeof(dernierMessage) - 1);
    dernierMessage[sizeof(dernierMessage) - 1] = '\0';
  }
}

bool signatureUploadGIFValide() {
  return (
    uploadSignatureOctets == 6 &&
    memcmp(uploadSignature, "GIF87a", 6) == 0
  ) || (
    uploadSignatureOctets == 6 &&
    memcmp(uploadSignature, "GIF89a", 6) == 0
  );
}

void abandonnerUpload(const char* message) {
  uploadErreur = true;
  uploadOK = false;
  strncpy(uploadMessage, message, sizeof(uploadMessage) - 1);
  uploadMessage[sizeof(uploadMessage) - 1] = '\0';
  definirMessage(uploadMessage);

  if (uploadFile) {
    uploadFile.close();
  }

  if (carteSDDisponible && SD.exists(UPLOAD_TEMP_PATH)) {
    SD.remove(UPLOAD_TEMP_PATH);
  }
}

void traiterUploadGIF() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    uploadErreur = false;
    uploadOK = false;
    uploadSignatureOctets = 0;
    uploadCheminFinal[0] = '\0';
    uploadNomFinal[0] = '\0';
    uploadMessage[0] = '\0';

    if (!carteSDDisponible) {
      abandonnerUpload("SD inaccessible");
      return;
    }

    const char* nomBrut = upload.filename.c_str();
    const char* nomBase = strrchr(nomBrut, '/');
    nomBase = nomBase == nullptr ? nomBrut : nomBase + 1;
    const char* nomWindows = strrchr(nomBase, '\\');
    nomBase = nomWindows == nullptr ? nomBase : nomWindows + 1;

    if (!nomFichierGIFValide(nomBase)) {
      abandonnerUpload("Nom de fichier GIF invalide");
      return;
    }

    snprintf(uploadCheminFinal, sizeof(uploadCheminFinal), "%s/%s", GIF_DIRECTORY, nomBase);

    if (!cheminGIFValide(uploadCheminFinal)) {
      abandonnerUpload("Chemin final refuse");
      return;
    }

    if (!SD.exists(GIF_DIRECTORY) && !SD.mkdir(GIF_DIRECTORY)) {
      abandonnerUpload("Dossier /gifs inaccessible");
      return;
    }

    if (SD.exists(uploadCheminFinal)) {
      abandonnerUpload("Un GIF porte deja ce nom");
      return;
    }

    suspendreAnimationPourSD();

    if (SD.exists(UPLOAD_TEMP_PATH)) {
      SD.remove(UPLOAD_TEMP_PATH);
    }

    uploadFile = SD.open(UPLOAD_TEMP_PATH, FILE_WRITE);

    if (!uploadFile) {
      abandonnerUpload("Manque d'espace ou fichier temporaire impossible");
      reprendreAnimationApresSD();
      return;
    }

    strncpy(uploadNomFinal, nomBase, sizeof(uploadNomFinal) - 1);
    uploadNomFinal[sizeof(uploadNomFinal) - 1] = '\0';
    definirMessage("Televersement GIF en cours");
    return;
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadErreur) {
      return;
    }

    if (!ecritureSDEnCours || !uploadFile) {
      abandonnerUpload("Ecriture SD non initialisee");
      reprendreAnimationApresSD();
      return;
    }

    if (upload.totalSize > MAX_UPLOAD_SIZE_BYTES) {
      abandonnerUpload("Fichier trop grand");
      reprendreAnimationApresSD();
      return;
    }

    for (
      size_t i = 0;
      i < upload.currentSize && uploadSignatureOctets < sizeof(uploadSignature);
      i++
    ) {
      uploadSignature[uploadSignatureOctets++] = upload.buf[i];
    }

    size_t ecrit = uploadFile.write(upload.buf, upload.currentSize);

    if (ecrit != upload.currentSize) {
      abandonnerUpload("Manque d'espace pendant l'ecriture");
      reprendreAnimationApresSD();
    }

    return;
  }

  if (upload.status == UPLOAD_FILE_END) {
    if (uploadErreur) {
      if (ecritureSDEnCours) {
        reprendreAnimationApresSD();
      }
      return;
    }

    if (uploadFile) {
      uploadFile.close();
    }

    if (upload.totalSize == 0) {
      abandonnerUpload("Fichier vide");
      reprendreAnimationApresSD();
      return;
    }

    if (upload.totalSize > MAX_UPLOAD_SIZE_BYTES) {
      abandonnerUpload("Fichier trop grand");
      reprendreAnimationApresSD();
      return;
    }

    if (!signatureUploadGIFValide()) {
      abandonnerUpload("Signature GIF non compatible");
      reprendreAnimationApresSD();
      return;
    }

    if (!SD.rename(UPLOAD_TEMP_PATH, uploadCheminFinal)) {
      abandonnerUpload("Renommage final impossible");
      reprendreAnimationApresSD();
      return;
    }

    scannerCatalogueGIFs();
    uploadOK = true;
    snprintf(
      uploadMessage,
      sizeof(uploadMessage),
      "GIF ajoute : %s",
      uploadNomFinal
    );
    definirMessage(uploadMessage);
    reprendreAnimationApresSD();
    return;
  }

  if (upload.status == UPLOAD_FILE_ABORTED) {
    abandonnerUpload("Televersement interrompu");
    if (ecritureSDEnCours) {
      reprendreAnimationApresSD();
    }
  }
}

void terminerUploadGIF() {
  if (uploadOK) {
    char reponse[180];
    snprintf(
      reponse,
      sizeof(reponse),
      "{\"ok\":true,\"message\":\"%s\"}",
      uploadMessage
    );
    server.send(200, "application/json", reponse);
    return;
  }

  char reponse[180];
  snprintf(
    reponse,
    sizeof(reponse),
    "{\"ok\":false,\"message\":\"%s\"}",
    uploadMessage[0] == '\0' ? "Televersement echoue" : uploadMessage
  );
  server.send(400, "application/json", reponse);
}

void supprimerGIFDepuisWeb() {
  if (ecritureSDEnCours) {
    server.send(409, "application/json", "{\"ok\":false,\"message\":\"Operation SD en cours\"}");
    return;
  }

  if (!carteSDDisponible) {
    definirMessage("SD inaccessible");
    server.send(503, "application/json", "{\"ok\":false,\"message\":\"SD inaccessible\"}");
    return;
  }

  if (!server.hasArg("path")) {
    definirMessage("Parametre path manquant");
    server.send(400, "application/json", "{\"ok\":false,\"message\":\"Parametre path manquant\"}");
    return;
  }

  String cheminDemande = server.arg("path");

  if (
    !cheminGIFValide(cheminDemande.c_str()) ||
    trouverGIFParChemin(cheminDemande.c_str()) < 0
  ) {
    definirMessage("Chemin GIF refuse");
    server.send(400, "application/json", "{\"ok\":false,\"message\":\"Chemin GIF refuse\"}");
    return;
  }

  if (
    strcmp(cheminDemande.c_str(), gifActuel) == 0 ||
    strcmp(cheminDemande.c_str(), FALLBACK_GIF_PATH) == 0
  ) {
    definirMessage("Suppression du GIF actif ou de secours interdite");
    server.send(409, "application/json", "{\"ok\":false,\"message\":\"Suppression du GIF actif ou de secours interdite\"}");
    return;
  }

  suspendreAnimationPourSD();

  bool supprime = SD.remove(cheminDemande.c_str());
  scannerCatalogueGIFs();

  if (supprime) {
    definirMessage("GIF supprime");
    reprendreAnimationApresSD();
    server.send(200, "application/json", "{\"ok\":true,\"message\":\"GIF supprime\"}");
  } else {
    definirMessage("Suppression impossible");
    reprendreAnimationApresSD();
    server.send(500, "application/json", "{\"ok\":false,\"message\":\"Suppression impossible\"}");
  }
}

void traiterUploadOTA() {
  if (!authentifierOTA()) {
    return;
  }

  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    otaErreur = false;
    otaOK = false;
    otaMessage[0] = '\0';

    if (!upload.filename.endsWith(".bin")) {
      otaErreur = true;
      strncpy(otaMessage, "Firmware refuse : extension .bin requise", sizeof(otaMessage) - 1);
      otaMessage[sizeof(otaMessage) - 1] = '\0';
      definirMessage(otaMessage);
      return;
    }

    suspendreAnimationPourSD();
    definirMessage("Mise a jour OTA en cours");

    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      otaErreur = true;
      Update.printError(Serial);
      snprintf(
        otaMessage,
        sizeof(otaMessage),
        "Update.begin echoue, erreur %u",
        Update.getError()
      );
      definirMessage(otaMessage);
      Update.abort();
      reprendreAnimationApresSD();
      return;
    }

    return;
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    if (otaErreur) {
      return;
    }

    size_t ecrit = Update.write(upload.buf, upload.currentSize);

    if (ecrit != upload.currentSize) {
      otaErreur = true;
      Update.printError(Serial);
      snprintf(
        otaMessage,
        sizeof(otaMessage),
        "Update.write incomplet, erreur %u",
        Update.getError()
      );
      definirMessage(otaMessage);
      Update.abort();
      reprendreAnimationApresSD();
    }

    return;
  }

  if (upload.status == UPLOAD_FILE_END) {
    if (otaErreur) {
      if (ecritureSDEnCours) {
        reprendreAnimationApresSD();
      }
      return;
    }

    if (!Update.end(true)) {
      otaErreur = true;
      Update.printError(Serial);
      snprintf(
        otaMessage,
        sizeof(otaMessage),
        "Update.end echoue, erreur %u",
        Update.getError()
      );
      definirMessage(otaMessage);
      Update.abort();
      reprendreAnimationApresSD();
      return;
    }

    if (!Update.isFinished()) {
      otaErreur = true;
      strncpy(otaMessage, "Update incomplete", sizeof(otaMessage) - 1);
      otaMessage[sizeof(otaMessage) - 1] = '\0';
      definirMessage(otaMessage);
      Update.abort();
      reprendreAnimationApresSD();
      return;
    }

    otaOK = true;
    strncpy(otaMessage, "Mise a jour reussie, redemarrage", sizeof(otaMessage) - 1);
    otaMessage[sizeof(otaMessage) - 1] = '\0';
    definirMessage(otaMessage);
    return;
  }

  if (upload.status == UPLOAD_FILE_ABORTED) {
    otaErreur = true;
    strncpy(otaMessage, "Mise a jour OTA interrompue", sizeof(otaMessage) - 1);
    otaMessage[sizeof(otaMessage) - 1] = '\0';
    definirMessage(otaMessage);
    Update.abort();

    if (ecritureSDEnCours) {
      reprendreAnimationApresSD();
    }
  }
}

void terminerUploadOTA() {
  if (!authentifierOTA()) {
    return;
  }

  if (otaOK) {
    server.send(200, "application/json", "{\"ok\":true,\"message\":\"Mise a jour reussie, redemarrage\"}");
    delay(250);
    ESP.restart();
    return;
  }

  if (ecritureSDEnCours) {
    reprendreAnimationApresSD();
  }

  char reponse[220];
  snprintf(
    reponse,
    sizeof(reponse),
    "{\"ok\":false,\"message\":\"%s\"}",
    otaMessage[0] == '\0' ? "Mise a jour echouee" : otaMessage
  );

  server.send(400, "application/json", reponse);
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
    sauvegarderPreferencesLecture();
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

bool vitesseLectureValide(uint16_t vitesse) {
  for (uint8_t i = 0; i < NOMBRE_VITESSES; i++) {
    if (VITESSES_LECTURE[i] == vitesse) {
      return true;
    }
  }

  return false;
}

uint32_t delaiFrameAjuste(int delaiFrameMs) {
  if (delaiFrameMs < 1) {
    delaiFrameMs = 1;
  }

  uint32_t delai = (
    static_cast<uint32_t>(delaiFrameMs) * 100UL
  ) / vitesseLecturePourcent;

  if (delai < 1) {
    delai = 1;
  }

  return delai;
}

void sauvegarderPreferencesLecture() {
  preferences.putString("gif", gifActuel);
  preferences.putUInt("speed", vitesseLecturePourcent);
  preferences.putBool("random", modeAleatoire);
  preferences.putBool("repeat", repetitionActive);
}

void chargerPreferencesLecture() {
  uint16_t vitesse = preferences.getUInt("speed", 100);

  if (vitesseLectureValide(vitesse)) {
    vitesseLecturePourcent = vitesse;
  } else {
    vitesseLecturePourcent = 100;
  }

  modeAleatoire = preferences.getBool("random", false);
  repetitionActive = preferences.getBool("repeat", true);

  String cheminMemorise = preferences.getString("gif", FALLBACK_GIF_PATH);
  const char* chemin = cheminMemorise.c_str();

  if (strcmp(chemin, FALLBACK_GIF_PATH) == 0 && SD.exists(FALLBACK_GIF_PATH)) {
    strncpy(gifActuel, FALLBACK_GIF_PATH, sizeof(gifActuel) - 1);
  } else if (
    cheminGIFValide(chemin) &&
    trouverGIFParChemin(chemin) >= 0 &&
    SD.exists(chemin)
  ) {
    strncpy(gifActuel, chemin, sizeof(gifActuel) - 1);
  } else {
    strncpy(gifActuel, FALLBACK_GIF_PATH, sizeof(gifActuel) - 1);
    definirMessage("GIF memorise indisponible, retour a /eye.gif");
  }

  gifActuel[sizeof(gifActuel) - 1] = '\0';
  strncpy(dernierGIFValide, gifActuel, sizeof(dernierGIFValide) - 1);
  dernierGIFValide[sizeof(dernierGIFValide) - 1] = '\0';
}

int indexGIFActuel() {
  return trouverGIFParChemin(gifActuel);
}

void redemarrerGIFActuel() {
  char chemin[GIF_PATH_MAX_LENGTH];

  strncpy(chemin, gifActuel, sizeof(chemin) - 1);
  chemin[sizeof(chemin) - 1] = '\0';

  lectureEnPause = false;
  fermerAnimationGIF();
  tft.fillScreen(GC9A01A_BLACK);

  if (!lancerGIF(chemin)) {
    if (!lancerGIF(dernierGIFValide)) {
      lancerGIF(FALLBACK_GIF_PATH);
    }
  }

  definirMessage("GIF redemarre");
}

void jouerGIFSuivant() {
  if (catalogueGIFs == nullptr || nombreGIFs == 0) {
    definirMessage("Aucun GIF dans /gifs");
    return;
  }

  uint8_t index = 0;

  if (modeAleatoire) {
    index = random(nombreGIFs);
  } else {
    int actuel = indexGIFActuel();
    index = actuel < 0 ? 0 : (actuel + 1) % nombreGIFs;
  }

  lectureEnPause = false;
  changerGIF(catalogueGIFs[index].chemin);
}

void jouerGIFPrecedent() {
  if (catalogueGIFs == nullptr || nombreGIFs == 0) {
    definirMessage("Aucun GIF dans /gifs");
    return;
  }

  int actuel = indexGIFActuel();
  uint8_t index = actuel <= 0 ? nombreGIFs - 1 : actuel - 1;

  lectureEnPause = false;
  changerGIF(catalogueGIFs[index].chemin);
}

// ============================================================
// Initialisation
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(3000);
  preferences.begin("demon-eye", false);
  randomSeed(micros());

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
  chargerPreferencesLecture();

  if (SD.exists(gifActuel) && lancerGIF(gifActuel)) {
    return;
  }

  Serial.println("GIF memorise indisponible ou invalide.");

  if (SD.exists(FALLBACK_GIF_PATH) && lancerGIF(FALLBACK_GIF_PATH)) {
    return;
  }

  Serial.println("GIF de secours /eye.gif indisponible ou invalide.");

  if (catalogueGIFs != nullptr && nombreGIFs > 0 && lancerGIF(catalogueGIFs[0].chemin)) {
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

  if (ecritureSDEnCours) {
    delay(1);
    return;
  }

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
    fermerAnimationGIF();

    if (!repetitionActive) {
      lectureEnPause = true;
      definirMessage("Fin du GIF, repetition desactivee");
      delay(1);
      return;
    }

    if (modeAleatoire && nombreGIFs > 0) {
      jouerGIFSuivant();
    } else {
      prochaineFrameMs = millis() + GIF_RESTART_DELAY_MS;
    }
  } else {
    prochaineFrameMs = millis() + delaiFrameAjuste(delaiFrameMs);
  }

  server.handleClient();
  delay(1);
}
