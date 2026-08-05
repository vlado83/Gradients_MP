/*
 * Gradients v1.17
 * Procedural IMU-controlled single-player and ESP-NOW multiplayer game
 * for 320 x 240 M5Stack devices, with M5Stack Core2 as the primary target.
 *
 * Author: Vladimir Divić
 * Repository: https://github.com/vlado83/Gradients_MP
 * License: GPL-3.0-only
 */

// Compile-time fallback only. Actual role and peer MAC are loaded from NVS.
// Serial commands can change them without recompiling.
//   1 = default HOST role
//   0 = default CLIENT role
#define DEFAULT_IS_HOST_DEVICE 0

#include <M5Unified.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_system.h>  // esp_random, esp_restart
#include <esp_wifi.h>    // esp_wifi_get_mac
#include <esp_idf_version.h>

// ------------------------------------------------------------
// Display & game params
// ------------------------------------------------------------
#define scrX 320
#define scrY 240
#define topBar 20

// control points
int NcpX = 33;
int NcpY = 25;

float cpA = 1000.0;
uint32_t rngNo;
int smoothPass = 5;
float field[33][25];
float Fx[33][25];
float Fy[33][25];
float minF = 99999;
float maxF = -999999;
float dF;

// IMU
float accX = 0.0F;
float accY = 0.0F;
float accZ = 0.0F;
float tx = 0.0F;
float ty = 0.0F;
float imuNeutralX = 0.0F;
float imuNeutralY = 0.0F;
bool imuRecalibrationLatched = false;

const uint16_t IMU_CALIBRATION_MS = 1000;
const uint16_t IMU_RECALIBRATION_HOLD_MS = 1200;
const float IMU_DEAD_ZONE = 0.04F;

// Player 1 (HOST / single) state
float xs1 = scrX / 2.0F;
float ys1 = (scrY + topBar) / 2.0F;
float xv1 = 0.0F;
float yv1 = 0.0F;
int   xp1 = 0;
int   yp1 = 0;

// Player 2 (CLIENT) state
float xs2 = scrX * 3.0F / 4.0F;
float ys2 = (scrY + topBar) / 2.0F;
float xv2 = 0.0F;
float yv2 = 0.0F;
int   xp2 = 0;
int   yp2 = 0;

// For cleanup of target
int prevTargX = 0;
int prevTargY = 0;

// timing
uint32_t lastR = 0;   // last update time (ms)
uint32_t gameStartMs = 0;

// power management
const uint32_t MENU_AUTO_POWER_OFF_MS     = 60000UL;  // auto power-off after 60s in menu
const uint32_t POSTGAME_AUTO_POWER_OFF_MS = 60000UL;  // auto power-off after 60s on game-over screen
const uint32_t MENU_BATTERY_REFRESH_MS    = 10000UL;  // redraw menu battery text every 10s
uint32_t postGameScreenStartMs = 0;

// sound FX
// M5Unified speaker volume is set on a 0..255 scale, but this project keeps
// the user-facing value as percent. Default is intentionally quiet.
const uint8_t SFX_VOLUME_DEFAULT_PERCENT = 10;
uint8_t sfxVolumePercent = SFX_VOLUME_DEFAULT_PERCENT;
bool sfxEnabled = true;

// Put this enum before any function definitions.
// Arduino's .ino preprocessor auto-generates function prototypes near the top;
// if SfxId is declared later, the generated prototype `void playSfx(SfxId id);`
// cannot compile.
enum SfxId {
  SFX_BOOT,
  SFX_START,
  SFX_PAIR_SUCCESS,
  SFX_SCORE,
  SFX_GAME_OVER,
  SFX_POWER_OFF
};

enum PostGameAction {
  POSTGAME_PLAY_AGAIN,
  POSTGAME_MAIN_MENU
};

// physics
float sm = 0.0F;    // smoothing (0 => raw accel)
float drag = 0.99;
float conR = 1.0;
float topoR = 2.0;
float CoR = 0.9;     // coefficient of restitution
int ballR = 5;
int halfWind = 14;   // sprite cleanup radius
int vectL = 6;
int precision = 8;

// Sprites => M5GFX canvases
M5Canvas bck(&M5.Display);
M5Canvas scene(&M5.Display);
M5Canvas menuScreen(&M5.Display);
bool menuScreenReady = false;
uint32_t startupTerrainSeed = 0;

// Scores
int score1 = 0;  // HOST
int score2 = 0;  // CLIENT

// Target
int targX;
int targY;
long lastCapt;
int remoteTargetBonusPoints = 1000;

// game end time = millis() when game ends
long gameEndTimeMs = 0;

// options
bool teleport = false;
bool grippy   = false;

// non-volatile
Preferences preferences;
long counter;
long HS;

// Intro vars
float xmIntro = scrX / 2.0f;
float ymIntro = scrY / 2.0f;
int   ncolor  = 1000;
float nxIntro, nyIntro;
float scr_, scg, scb;
float ncr, ncg, ncb;
int   kIntro = 0;

// terrain sync flag (for client)
bool terrainReady = false;

// client-side game over / sync tracking
bool   remoteGameOver      = false;
int8_t remoteWinner        = 0;
bool   firstStateReceived  = false;
uint32_t lastStateMs       = 0;
bool   waitingShown        = false;
bool   clientLinkLost      = false;

// ---- Color map block ----
int Ncols = 100;
uint16_t cols[100];
uint8_t reds[100] = {49,50,51,52,54,55,57,60,62,66,69,73,78,82,86,91,95,100,106,111,117,123,129,135,141,146,152,157,163,168,172,178,184,190,196,202,208,213,218,222,225,229,232,236,240,243,247,250,252,254,255,255,255,255,255,255,255,254,254,254,254,254,254,254,254,254,254,254,254,253,253,252,252,251,251,250,249,248,246,244,242,240,238,235,233,230,227,224,220,215,211,206,202,197,192,187,182,176,171,165};
uint8_t greens[100] = {54,61,68,74,81,87,94,100,106,112,118,123,129,135,141,147,153,158,164,169,174,179,184,188,193,198,202,207,211,214,218,221,225,228,231,233,236,238,240,242,244,245,247,248,250,251,252,253,254,255,254,251,249,246,243,240,236,233,229,226,222,217,213,208,203,198,192,187,181,176,170,164,158,151,144,137,130,123,117,110,104,98,92,85,79,72,66,60,54,48,44,39,33,28,23,17,11,6,2,0};
uint8_t blues[100]  = {149,152,155,159,162,165,168,171,174,177,180,183,186,189,192,195,198,201,204,207,210,212,215,217,220,223,225,227,230,232,234,236,238,240,242,245,246,248,248,248,248,246,243,238,233,227,221,213,205,196,189,184,179,174,170,165,160,155,150,146,141,136,131,126,121,116,111,106,102,98,95,91,88,85,82,79,76,73,71,68,65,62,59,56,53,50,47,44,42,39,37,36,35,34,34,35,35,36,37,38};

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static inline uint16_t scale565(uint16_t c, uint8_t scale) {
  uint8_t r = ((c >> 11) & 0x1F) << 3;
  uint8_t g = ((c >> 5)  & 0x3F) << 2;
  uint8_t b = ( c        & 0x1F) << 3;
  return rgb565((uint8_t)((uint16_t)r * scale / 255),
                (uint8_t)((uint16_t)g * scale / 255),
                (uint8_t)((uint16_t)b * scale / 255));
}

void isolines() {
  for (int i = 0; i < Ncols; i += (Ncols/10)) {
    reds[i] = 0;
    greens[i] = 0;
    blues[i] = 0;
  }
}

void convertTo565() {
  for (int i = 0; i < Ncols; i++) cols[i] = rgb565(reds[i], greens[i], blues[i]);
}

// ------------------------------------------------------------
// Multiplayer control
// ------------------------------------------------------------
bool multiplayer = false;        // false = singleplayer, true = multiplayer
bool isHost      = false;        // runtime: this device acts as host or client

// Multiplayer supports one host plus up to three clients.
// Player IDs are stable and color-coded:
//   0 = HOST   = red
//   1 = C1     = blue
//   2 = C2     = cyan
//   3 = C3     = magenta
#define MAX_PLAYERS 4
#define MAX_CLIENTS 3
#define HOST_PLAYER_ID 0
const uint16_t PLAYER_COLORS[MAX_PLAYERS] = { TFT_RED, TFT_BLUE, TFT_CYAN, TFT_MAGENTA };
const char* PLAYER_LABELS[MAX_PLAYERS] = { "H", "C1", "C2", "C3" };

uint8_t localPlayerId = HOST_PLAYER_ID; // host=0; client receives assigned id in GameState
uint8_t playerActiveMask = 0x01;

float pX[MAX_PLAYERS];
float pY[MAX_PLAYERS];
float pVX[MAX_PLAYERS];
float pVY[MAX_PLAYERS];
float pInputX[MAX_PLAYERS];
float pInputY[MAX_PLAYERS];
int   pPrevX[MAX_PLAYERS];
int   pPrevY[MAX_PLAYERS];
int32_t pScore[MAX_PLAYERS];
uint32_t pLastInputMs[MAX_PLAYERS];
uint8_t hostDisconnectedMask = 0;

// Post-game score history, sampled once per second.
const uint16_t MAX_SCORE_SAMPLES = 600;
uint16_t scoreHistoryTimeSec[MAX_SCORE_SAMPLES];
int32_t scoreHistory[MAX_PLAYERS][MAX_SCORE_SAMPLES];
uint16_t scoreHistoryCount = 0;
uint32_t lastScoreHistorySampleMs = 0;
uint8_t playerParticipantMask = 0x01;

uint8_t clientMacs[MAX_CLIENTS][6];
bool clientMacValid[MAX_CLIENTS];

// peerMac is used by clients as the HOST MAC. On the host, clientMacs[] are used.
// peerMac is retained for backward compatibility with older serial commands.
// These are fallback defaults only; NVS/Serial config can override them.
const uint8_t DEFAULT_PEER_IF_HOST[6]   = {0x84, 0x1F, 0xE8, 0x85, 0x30, 0x48}; // HOST talks to CLIENT
const uint8_t DEFAULT_PEER_IF_CLIENT[6] = {0x84, 0x1F, 0xE8, 0x85, 0x5F, 0x0C}; // CLIENT talks to HOST

bool cfgIsHostDevice = (DEFAULT_IS_HOST_DEVICE != 0);
uint8_t peerMac[6];
String serialLine;
bool configDirty = false;

// Boot role override: hold A during boot => HOST, hold B during boot => CLIENT.
bool bootRoleButtonWasUsed = false;
String bootRoleMessage;

// Host client clearing: hold C during HOST boot to clear stored client slots.
bool bootClearClientsButtonWasUsed = false;
String bootClearClientsMessage;

// C-button pairing state. Pairing uses ESP-NOW broadcast and writes peerMac/client slots
// to the same NVS keys already used by Serial pairing. Both devices must press C.
const uint8_t ESPNOW_BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
const uint32_t PAIR_MAGIC = 0xB00FCAFE;
const uint16_t PAIR_TX_WINDOW_MS = 2500;     // broadcast duration after C is pressed
const uint16_t PAIR_TX_PERIOD_MS = 100;      // repeated broadcast interval
const uint16_t PAIR_VIBRATION_MS = 200;      // haptic feedback only after successful pairing

bool espNowReady = false;
uint32_t localPairNonce = 0;
uint32_t pairSendUntilMs = 0;
uint32_t lastPairTxMs = 0;
uint32_t pairVibrationUntilMs = 0;
bool pairWindowAcceptedOneClient = false;    // host accepts only one new client per C-press burst
uint32_t lastPairSuccessNonce = 0;
uint8_t lastPairSuccessMac[6] = {0, 0, 0, 0, 0, 0};
String pairingStatus;

// Throttle network updates a bit (ms)
const uint16_t NET_DT_MS = 25;  // ~40 Hz
const uint16_t CONNECTION_TIMEOUT_MS = 1500;

// ESP-NOW structs
struct GameState {
  uint32_t magic = 0xDEADBEF1;
  uint32_t frame;
  float px[MAX_PLAYERS];
  float py[MAX_PLAYERS];
  float vx[MAX_PLAYERS];
  float vy[MAX_PLAYERS];
  float inputTx[MAX_PLAYERS];
  float inputTy[MAX_PLAYERS];
  int16_t targX, targY;
  int16_t targetBonusPoints;
  int32_t score[MAX_PLAYERS];
  int32_t timeLeftMs;
  uint32_t rngSeed;        // used to sync terrain
  uint8_t  activeMask;     // bit 0=host, bit 1=C1, bit 2=C2, bit 3=C3
  uint8_t  recipientPlayerId; // unicast target; clients use this to know their color/score
  uint8_t  gameOver;       // 0=no, 1=yes
  int8_t   winner;         // 0=tie, 1=H, 2=C1, 3=C2, 4=C3
};

struct InputPacket {
  uint32_t magic = 0xC1E17AA1;
  uint8_t  playerId;       // advisory only; host assigns by sender MAC
  float tx;
  float ty;
};

struct __attribute__((packed)) PairPacket {
  uint32_t magic;
  uint8_t  version;
  uint8_t  role;       // 1 = HOST, 2 = CLIENT
  uint8_t  mac[6];     // sender STA MAC
  uint32_t nonce;      // helps distinguish fresh pairing bursts
  uint32_t uptimeMs;
};

GameState  gState;
InputPacket lastInputFromClient[MAX_PLAYERS];
PairPacket lastPairPacket;
volatile bool haveNewInputFor[MAX_PLAYERS] = { false, false, false, false };
volatile bool haveNewState = false;
volatile bool haveNewPairPacket = false;

// ------------------------------------------------------------

void generateGradients() {
  for (int i = 0; i < (NcpX - 1); i++) {
    for (int j = 0; j < (NcpY - 1); j++) {
      Fx[i][j] = (field[i + 1][j] - field[i][j]) / 1.0;
      Fy[i][j] = (field[i][j + 1] - field[i][j]) / 1.0;
    }
  }
}

void cleanUp(int x0, int y0) {
  for (int x = x0 - halfWind; x <= (x0 + halfWind); x++) {
    for (int y = y0 - halfWind; y <= (y0 + halfWind); y++) {
      if (x >= 0 && x < scrX && y >= 0 && y < scrY) {
        scene.drawPixel(x, y, bck.readPixel(x, y));
      }
    }
  }
}

void generateTerrain() {
  // rough terrain
  for (int i = 0; i < NcpX; i++) {
    for (int j = 0; j < NcpY; j++) {
      field[i][j] = float(random(-cpA, cpA + 1)) / 100.0;
      if ((i == 0) || (i == (NcpX - 1)) || (j == 0) || (j == (NcpY - 1))) field[i][j] = 0.0;
    }
  }

  for (int kpass = 0; kpass < (smoothPass - 1); kpass++) {
    minF =  999999.0F;
    maxF = -999999.0F;

    for (int i = 1; i < (NcpX - 1); i++) {
      for (int j = 1; j < (NcpY - 1); j++) {
        field[i][j] =  (field[i - 1][j - 1] + field[i][j - 1] + field[i + 1][j - 1] +
                        field[i - 1][j]     + field[i][j]     + field[i + 1][j] +
                        field[i - 1][j + 1] + field[i][j + 1] + field[i + 1][j + 1]) / 9.0F;
      }
    }

    for (int i = 0; i < NcpX; i++) {
      for (int j = 0; j < NcpY; j++) {
        if (field[i][j] < minF) minF = field[i][j];
        if (field[i][j] > maxF) maxF = field[i][j];
      }
    }

    float dFmax = fabsf(maxF) / (Ncols / 2);
    float dFmin = fabsf(minF) / (Ncols / 2);
    dF = max(dFmax, dFmin);

    isolines();
    convertTo565();

    for (int x = 0; x < scrX; x++) {
      for (int y = 0; y < scrY; y++) {
        int inx  = floorf(float(x) / (scrX / (NcpX - 1)));
        int iny  = floorf(float(y) / (scrY / (NcpY - 1)));
        float xm = x - float(scrX / (NcpX - 1)) * inx;
        float ym = y - float(scrY / (NcpY - 1)) * iny;
        float clX = float(scrX / (NcpX - 1));
        float clY = float(scrY / (NcpY - 1));

        float z = 0.01f * ( field[inx][iny]         * (clX - xm) * (clY - ym)
                          + field[inx + 1][iny]     * xm         * (clY - ym)
                          + field[inx][iny + 1]     * (clX - xm) * ym
                          + field[inx + 1][iny + 1] * xm         * ym );

        int ncol = (Ncols / 2) + lroundf(z / dF);
        ncol = max(0, min(Ncols - 1, ncol));
        bck.drawPixel(x, y, cols[ncol]);
      }
    }
    bck.pushSprite(0, 0);
  }
}

// ------------------------------------------------------------
// Runtime configuration: role + peer MAC in NVS, controlled by Serial
// ------------------------------------------------------------

String macToString(const uint8_t mac[6]) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

bool parseHexByte(const String &s, int pos, uint8_t &out) {
  if (pos + 1 >= s.length()) return false;

  auto hexVal = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
  };

  int hi = hexVal(s[pos]);
  int lo = hexVal(s[pos + 1]);
  if (hi < 0 || lo < 0) return false;
  out = (uint8_t)((hi << 4) | lo);
  return true;
}

bool parseMac(String text, uint8_t out[6]) {
  text.trim();
  text.toUpperCase();
  text.replace("-", ":");

  // Accept compact 12-character form: 841FE8853048
  if (text.length() == 12) {
    for (int i = 0; i < 6; i++) {
      if (!parseHexByte(text, 2 * i, out[i])) return false;
    }
    return true;
  }

  // Accept colon-separated form: 84:1F:E8:85:30:48
  if (text.length() != 17) return false;
  for (int i = 0; i < 6; i++) {
    int pos = 3 * i;
    if (!parseHexByte(text, pos, out[i])) return false;
    if (i < 5 && text[pos + 2] != ':') return false;
  }
  return true;
}

bool sameMac(const uint8_t a[6], const uint8_t b[6]) {
  for (int i = 0; i < 6; i++) if (a[i] != b[i]) return false;
  return true;
}

void getLocalStaMac(uint8_t mac[6]) {
  esp_wifi_get_mac(WIFI_IF_STA, mac);
}

bool isZeroMac(const uint8_t mac[6]) {
  for (int i = 0; i < 6; i++) if (mac[i] != 0) return false;
  return true;
}

int findClientSlotByMac(const uint8_t mac[6]) {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (clientMacValid[i] && sameMac(clientMacs[i], mac)) return i;
  }
  return -1;
}

int firstEmptyClientSlot() {
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (!clientMacValid[i]) return i;
  }
  return -1;
}

int addOrUpdateClientMac(const uint8_t mac[6]) {
  if (isZeroMac(mac)) return -1;
  int slot = findClientSlotByMac(mac);
  if (slot < 0) slot = firstEmptyClientSlot();
  if (slot < 0) return -1;
  memcpy(clientMacs[slot], mac, 6);
  clientMacValid[slot] = true;
  return slot;
}

void clearClientMacSlots() {
  memset(clientMacs, 0, sizeof(clientMacs));
  for (int i = 0; i < MAX_CLIENTS; i++) clientMacValid[i] = false;
}

void saveClientMacToNvs(int slot, const uint8_t mac[6]) {
  if (slot < 0 || slot >= MAX_CLIENTS) return;
  Preferences cfg;
  cfg.begin("gradcfg", false);
  String key = String("cli") + String(slot + 1);
  cfg.putBytes(key.c_str(), mac, 6);
  cfg.end();
}

void saveRoleToNvs(bool hostRole) {
  Preferences cfg;
  cfg.begin("gradcfg", false);
  cfg.putBool("isHost", hostRole);
  cfg.end();
}

void savePeerToNvs(const uint8_t mac[6]) {
  Preferences cfg;
  cfg.begin("gradcfg", false);
  cfg.putBytes("peer", mac, 6);
  cfg.end();
}

void clearAllHostClients() {
  clearClientMacSlots();

  uint8_t zero[6] = {0, 0, 0, 0, 0, 0};
  memcpy(peerMac, zero, 6);

  Preferences cfg;
  cfg.begin("gradcfg", false);
  for (int i = 0; i < MAX_CLIENTS; i++) {
    String key = String("cli") + String(i + 1);
    cfg.remove(key.c_str());
  }
  // Keep a zero peer key so loadRuntimeConfig() does not revive the legacy default C1.
  cfg.putBytes("peer", zero, 6);
  cfg.end();
}

void loadRuntimeConfig() {
  Preferences cfg;
  cfg.begin("gradcfg", true);

  cfgIsHostDevice = cfg.getBool("isHost", DEFAULT_IS_HOST_DEVICE != 0);

  clearClientMacSlots();

  size_t n = cfg.getBytesLength("peer");
  if (n == 6) {
    cfg.getBytes("peer", peerMac, 6);
  } else {
    memcpy(peerMac, cfgIsHostDevice ? DEFAULT_PEER_IF_HOST : DEFAULT_PEER_IF_CLIENT, 6);
  }

  // Host stores up to three clients. If no client slots exist yet, keep the
  // old two-player default as Client 1 so existing setups do not break.
  bool anyClientSlot = false;
  for (int i = 0; i < MAX_CLIENTS; i++) {
    String key = String("cli") + String(i + 1);
    if (cfg.getBytesLength(key.c_str()) == 6) {
      cfg.getBytes(key.c_str(), clientMacs[i], 6);
      clientMacValid[i] = !isZeroMac(clientMacs[i]);
      if (clientMacValid[i]) anyClientSlot = true;
    }
  }

  if (!anyClientSlot && cfgIsHostDevice && !isZeroMac(peerMac)) {
    memcpy(clientMacs[0], peerMac, 6);
    clientMacValid[0] = true;
  }

  cfg.end();
}

void resetRuntimeConfig() {
  Preferences cfg;
  cfg.begin("gradcfg", false);
  cfg.clear();
  cfg.end();
  loadRuntimeConfig();
}

uint8_t sfxSpeakerVolume255() {
  uint8_t pct = constrain(sfxVolumePercent, 0, 100);
  return (uint8_t)(((uint16_t)pct * 255U) / 100U);
}

void applySfxVolume() {
  M5.Speaker.setVolume(sfxSpeakerVolume255());
}

void playSfxTone(uint16_t freqHz, uint16_t durationMs) {
  if (!sfxEnabled || sfxVolumePercent == 0) return;
  applySfxVolume();
  M5.Speaker.tone(freqHz, durationMs);
}

void playSfx(SfxId id) {
  switch (id) {
    case SFX_BOOT:         playSfxTone(880, 70);  break;
    case SFX_START:        playSfxTone(1175, 90); break;
    case SFX_PAIR_SUCCESS: playSfxTone(1568, 120); break;
    case SFX_SCORE:        playSfxTone(2093, 55); break;
    case SFX_GAME_OVER:    playSfxTone(330, 180); break;
    case SFX_POWER_OFF:    playSfxTone(220, 120); break;
  }
}

float applyImuDeadZone(float value) {
  float magnitude = fabsf(value);
  if (magnitude <= IMU_DEAD_ZONE) return 0.0f;

  // Remove the dead-zone step and rescale the remaining range smoothly to 1.
  float adjusted = (magnitude - IMU_DEAD_ZONE) / (1.0f - IMU_DEAD_ZONE);
  adjusted = constrain(adjusted, 0.0f, 1.0f);
  return value < 0.0f ? -adjusted : adjusted;
}

void calibrateImuNeutral(bool pauseHostGameClock) {
  uint32_t calibrationStartMs = millis();

  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextDatum(MC_DATUM);
  M5.Display.setTextSize(2);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.drawString("Hold comfortably", scrX / 2, 76);
  M5.Display.drawString("Keep device still", scrX / 2, 104);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  M5.Display.drawString("Calibrating IMU...", scrX / 2, 134);
  M5.Display.drawString("Hold A in game to recalibrate", scrX / 2, 184);

  double sumX = 0.0;
  double sumY = 0.0;
  uint32_t samples = 0;
  uint32_t lastProgressWidth = 0;

  while ((uint32_t)(millis() - calibrationStartMs) < IMU_CALIBRATION_MS) {
    M5.update();
    float sampleX, sampleY, sampleZ;
    if (M5.Imu.getAccel(&sampleX, &sampleY, &sampleZ)) {
      sumX += sampleX;
      sumY += sampleY;
      samples++;
    }

    uint32_t elapsed = millis() - calibrationStartMs;
    uint32_t progressWidth = min((uint32_t)220, (elapsed * 220) / IMU_CALIBRATION_MS);
    if (progressWidth != lastProgressWidth) {
      M5.Display.fillRect(50, 154, progressWidth, 8, TFT_GREEN);
      lastProgressWidth = progressWidth;
    }
    delay(10);
  }

  if (samples > 0) {
    imuNeutralX = (float)(sumX / samples);
    imuNeutralY = (float)(sumY / samples);
  }
  tx = 0.0f;
  ty = 0.0f;

  uint32_t pausedMs = millis() - calibrationStartMs;
  if (pauseHostGameClock && gameEndTimeMs != 0) {
    gameEndTimeMs += pausedMs;
    gameStartMs += pausedMs;
    lastCapt += pausedMs;
    lastR = millis();
  }

  Serial.printf("IMU neutral calibrated: X=%.4f Y=%.4f samples=%lu\n",
                imuNeutralX, imuNeutralY, (unsigned long)samples);
}

void processImuRecalibrationRequest() {
  if (!M5.BtnA.isPressed()) {
    imuRecalibrationLatched = false;
    return;
  }

  if (!imuRecalibrationLatched && M5.BtnA.pressedFor(IMU_RECALIBRATION_HOLD_MS)) {
    imuRecalibrationLatched = true;
    calibrateImuNeutral(isHost);
  }
}

void printConfig() {
  Serial.println();
  Serial.println("--- Gradients config ---");
  Serial.print("Local STA MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Role: ");
  Serial.println(cfgIsHostDevice ? "HOST" : "CLIENT");
  if (cfgIsHostDevice) {
    Serial.println("Client MAC slots:");
    for (int i = 0; i < MAX_CLIENTS; i++) {
      Serial.print("  C"); Serial.print(i + 1); Serial.print(": ");
      Serial.println(clientMacValid[i] ? macToString(clientMacs[i]) : String("<empty>"));
    }
  } else {
    Serial.print("Host/Peer MAC: ");
    Serial.println(macToString(peerMac));
  }
  Serial.println("Boot buttons:");
  Serial.println("  Hold A during boot   force/save HOST role");
  Serial.println("  Hold B during boot   force/save CLIENT role");
  Serial.println("C-button pairing:");
  Serial.println("  In the startup menu, press C on HOST and one CLIENT to exchange MACs");
  Serial.print("SFX: ");
  Serial.print(sfxEnabled ? "ON" : "OFF");
  Serial.print("  Volume: ");
  Serial.print(sfxVolumePercent);
  Serial.println("%");
  Serial.println("Commands:");
  Serial.println("  CFG?                 show config");
  Serial.println("  MAC?                 show local + peer MAC");
  Serial.println("  ROLE HOST            set this device as host");
  Serial.println("  ROLE CLIENT          set this device as client");
  Serial.println("  PEER 84:1F:E8:85:30:48");
  Serial.println("  PEER 841FE8853048    set peer only, compact form also works");
  Serial.println("  PAIR HOST <peer MAC> set role + peer in one command");
  Serial.println("  PAIR CLIENT <peer MAC>");
  Serial.println("  PAIR?                show pairing instructions");
  Serial.println("  RESETCFG             clear NVS config and reload defaults");
  Serial.println("  VOL? / VOL 10        show/set SFX volume percent, 0..100");
  Serial.println("  SFX ON / SFX OFF     enable/disable sound FX");
  Serial.println("  Hold C during HOST boot to clear stored client slots");
  Serial.println("  REBOOT               restart device");
  Serial.println("------------------------");
}

void printPairHelp() {
  Serial.println();
  Serial.println("--- Pairing help ---");
  Serial.print("This device local MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.println("Read the LOCAL MAC from the other device screen, then type one of:");
  Serial.println("  PAIR HOST   <other device LOCAL MAC>");
  Serial.println("  PAIR CLIENT <other device LOCAL MAC>");
  Serial.println();
  Serial.println("Examples:");
  Serial.println("  PAIR HOST 84:1F:E8:85:30:48");
  Serial.println("  PAIR CLIENT 841FE8855F0C");
  Serial.println();
  Serial.println("Use HOST on the device that starts the game/terrain.");
  Serial.println("Use CLIENT on each client device.");
  Serial.println("No-PC option: hold A during boot on the host, hold B during boot on the client,");
  Serial.println("then press C on HOST and one CLIENT at roughly the same time.");
  Serial.println("After pairing, start multiplayer from the menu.");
  Serial.println("--------------------");
}

void handleSerialCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  String upper = cmd;
  upper.toUpperCase();

  if (upper == "HELP" || upper == "CFG?" || upper == "CONFIG?") {
    printConfig();
    return;
  }

  if (upper == "PAIR?" || upper == "PAIR HELP") {
    printPairHelp();
    return;
  }

  if (upper == "MAC?") {
    Serial.print("Local STA MAC: ");
    Serial.println(WiFi.macAddress());
    if (cfgIsHostDevice) {
      for (int i = 0; i < MAX_CLIENTS; i++) {
        Serial.print("C"); Serial.print(i + 1); Serial.print(": ");
        Serial.println(clientMacValid[i] ? macToString(clientMacs[i]) : String("<empty>"));
      }
    } else {
      Serial.print("Host/Peer MAC: ");
      Serial.println(macToString(peerMac));
    }
    return;
  }

  if (upper == "ROLE?") {
    Serial.print("Role: ");
    Serial.println(cfgIsHostDevice ? "HOST" : "CLIENT");
    return;
  }

  if (upper == "VOL?" || upper == "VOLUME?") {
    Serial.print("SFX volume: ");
    Serial.print(sfxVolumePercent);
    Serial.println("%");
    return;
  }

  if (upper.startsWith("VOL ") || upper.startsWith("VOLUME ")) {
    int sp = cmd.indexOf(' ');
    String arg = cmd.substring(sp + 1);
    arg.trim();
    int pct = arg.toInt();
    if (pct < 0 || pct > 100 || (pct == 0 && arg != "0")) {
      Serial.println("Invalid volume. Use VOL 0 .. VOL 100. Default is VOL 10.");
      return;
    }
    sfxVolumePercent = (uint8_t)pct;
    applySfxVolume();
    Serial.print("SFX volume set to ");
    Serial.print(sfxVolumePercent);
    Serial.println("%");
    playSfxTone(1200, 50);
    return;
  }

  if (upper == "SFX?" || upper == "SOUND?") {
    Serial.print("SFX: ");
    Serial.println(sfxEnabled ? "ON" : "OFF");
    return;
  }

  if (upper == "SFX ON" || upper == "SOUND ON") {
    sfxEnabled = true;
    applySfxVolume();
    Serial.println("SFX enabled.");
    playSfxTone(1200, 50);
    return;
  }

  if (upper == "SFX OFF" || upper == "SOUND OFF") {
    sfxEnabled = false;
    Serial.println("SFX disabled.");
    return;
  }

  if (upper == "ROLE HOST") {
    cfgIsHostDevice = true;
    saveRoleToNvs(cfgIsHostDevice);
    configDirty = true;
    Serial.println("Role saved as HOST. If you are in the startup menu, you can start a game now.");
    return;
  }

  if (upper == "ROLE CLIENT") {
    cfgIsHostDevice = false;
    saveRoleToNvs(cfgIsHostDevice);
    configDirty = true;
    Serial.println("Role saved as CLIENT. If you are in the startup menu, you can start a game now.");
    return;
  }

  if (upper.startsWith("PEER ")) {
    String arg = cmd.substring(5);
    uint8_t m[6];
    if (!parseMac(arg, m)) {
      Serial.println("Invalid MAC. Use PEER 84:1F:E8:85:30:48 or PEER 841FE8853048");
      return;
    }
    memcpy(peerMac, m, 6);
    savePeerToNvs(peerMac);
    if (cfgIsHostDevice) {
      int slot = addOrUpdateClientMac(m);
      if (slot >= 0) saveClientMacToNvs(slot, m);
    }
    configDirty = true;
    Serial.print(cfgIsHostDevice ? "Client MAC saved: " : "Host/Peer MAC saved: ");
    Serial.println(macToString(peerMac));
    return;
  }

  if (upper.startsWith("PAIR ")) {
    String rest = cmd.substring(5);
    rest.trim();

    int sp = rest.indexOf(' ');
    if (sp < 0) {
      Serial.println("Invalid PAIR command. Use: PAIR HOST <peer MAC> or PAIR CLIENT <peer MAC>");
      printPairHelp();
      return;
    }

    String roleText = rest.substring(0, sp);
    String macText  = rest.substring(sp + 1);
    roleText.trim();
    macText.trim();
    roleText.toUpperCase();

    bool newRole;
    if (roleText == "HOST") {
      newRole = true;
    } else if (roleText == "CLIENT") {
      newRole = false;
    } else {
      Serial.println("Invalid role. Use HOST or CLIENT.");
      printPairHelp();
      return;
    }

    uint8_t m[6];
    if (!parseMac(macText, m)) {
      Serial.println("Invalid MAC. Use 84:1F:E8:85:30:48 or 841FE8853048");
      return;
    }

    cfgIsHostDevice = newRole;
    memcpy(peerMac, m, 6);
    saveRoleToNvs(cfgIsHostDevice);
    savePeerToNvs(peerMac);
    if (cfgIsHostDevice) {
      int slot = addOrUpdateClientMac(m);
      if (slot >= 0) saveClientMacToNvs(slot, m);
    }
    configDirty = true;

    Serial.println("Pairing config saved.");
    Serial.print("Role: ");
    Serial.println(cfgIsHostDevice ? "HOST" : "CLIENT");
    if (cfgIsHostDevice) {
      int slot = findClientSlotByMac(m);
      Serial.print("Client slot: C");
      Serial.println(slot >= 0 ? slot + 1 : 0);
    } else {
      Serial.print("Host/Peer MAC: ");
      Serial.println(macToString(peerMac));
    }
    Serial.println("If you are still in the startup menu, you can start multiplayer now.");
    return;
  }

  if (upper == "RESETCFG") {
    resetRuntimeConfig();
    configDirty = true;
    Serial.println("Config cleared. Defaults reloaded.");
    printConfig();
    return;
  }

  if (upper == "REBOOT" || upper == "RESTART") {
    Serial.println("Restarting...");
    delay(100);
    esp_restart();
  }

  Serial.print("Unknown command: ");
  Serial.println(cmd);
  Serial.println("Type HELP or CFG?");
}

void processSerialConfig() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      handleSerialCommand(serialLine);
      serialLine = "";
    } else {
      serialLine += c;
      if (serialLine.length() > 96) serialLine = "";
    }
  }
}

String batteryStatusString() {
  int level = M5.Power.getBatteryLevel();
  if (level >= 0 && level <= 100) {
    return String("Bat: ") + String(level) + "%";
  }
  return String("Bat: --");
}

bool isUsbPowered() {
  // Core2's AXP192 reports VBUS even when the battery is already full, unlike
  // isCharging(). Treat a normal USB supply voltage as external power.
  return M5.Power.getVBUSVoltage() >= 4000;
}

void autoPowerOffNow(const char* reason) {
  M5.Power.setVibration(0);
  playSfx(SFX_POWER_OFF);

  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextSize(1);
  M5.Display.setTextDatum(MC_DATUM);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.drawString("Auto power off", scrX / 2, scrY / 2 - 18, 2);
  M5.Display.drawString(reason,           scrX / 2, scrY / 2 + 8,  2);
  M5.Display.drawString("Press power to wake", scrX / 2, scrY / 2 + 34, 2);
  delay(800);

  M5.Power.powerOff();

  // If USB power or a board-specific PMIC state prevents full shutdown, stop here.
  while (true) {
    delay(1000);
  }
}

// ------------------------------------------------------------
// ESP-NOW callbacks + radio setup
// ------------------------------------------------------------
void triggerPairSuccessVibrationOnce(const uint8_t mac[6], uint32_t nonce) {
  if (nonce == lastPairSuccessNonce && sameMac(mac, lastPairSuccessMac)) return;

  lastPairSuccessNonce = nonce;
  memcpy(lastPairSuccessMac, mac, 6);
  pairVibrationUntilMs = millis() + PAIR_VIBRATION_MS;
  playSfx(SFX_PAIR_SUCCESS);
}

void handleReceivedPairPacket() {
  PairPacket pp;
  noInterrupts();
  pp = lastPairPacket;
  haveNewPairPacket = false;
  interrupts();

  uint8_t localMac[6];
  getLocalStaMac(localMac);

  if (pp.magic != PAIR_MAGIC || pp.version != 1) return;
  if (sameMac(pp.mac, localMac)) return; // ignore our own broadcast echo

  uint8_t localRole = cfgIsHostDevice ? 1 : 2;
  if (pp.role == localRole) {
    pairingStatus = "Pair rejected: same role";
    configDirty = true;
    return;
  }

  // Deterministic C-pairing: accept pair packets only while this device is
  // also broadcasting because C was pressed locally. A device that did not
  // press C ignores all pairing traffic from nearby Gradients devices.
  uint32_t now = millis();
  bool localIsBroadcasting = (pairSendUntilMs != 0) && (now < pairSendUntilMs);
  if (!localIsBroadcasting) return;

  // Ignore repeated packets from the same peer C-press burst.
  if (pp.nonce == lastPairSuccessNonce && sameMac(pp.mac, lastPairSuccessMac)) return;

  if (cfgIsHostDevice && pp.role == 2) {
    int existingSlot = findClientSlotByMac(pp.mac);

    // One C press on the HOST should pair with only one new CLIENT. This prevents
    // several powered clients from being added during a single host pairing burst.
    if (existingSlot < 0 && pairWindowAcceptedOneClient) {
      pairingStatus = "Pair ignored: press C again for next client";
      configDirty = true;
      return;
    }

    int slot = addOrUpdateClientMac(pp.mac);
    if (slot < 0) {
      pairingStatus = "Pair rejected: client slots full";
      configDirty = true;
      return;
    }

    saveClientMacToNvs(slot, pp.mac);
    memcpy(peerMac, pp.mac, 6);   // backward compatibility/status only
    savePeerToNvs(peerMac);
    configDirty = true;
    pairWindowAcceptedOneClient = true;

    pairingStatus = String("PAIRED C") + String(slot + 1) + " " + macToString(pp.mac);
    triggerPairSuccessVibrationOnce(pp.mac, pp.nonce);

    Serial.println();
    Serial.println("C-button pairing saved on host.");
    Serial.print("Client slot: C");
    Serial.println(slot + 1);
    Serial.print("Client MAC: ");
    Serial.println(macToString(pp.mac));
    return;
  }

  if (!cfgIsHostDevice && pp.role == 1) {
    memcpy(peerMac, pp.mac, 6);
    savePeerToNvs(peerMac);
    configDirty = true;

    pairingStatus = "PAIRED HOST ";
    pairingStatus += macToString(peerMac);
    triggerPairSuccessVibrationOnce(pp.mac, pp.nonce);

    Serial.println();
    Serial.println("C-button pairing saved on client.");
    Serial.print("Host MAC: ");
    Serial.println(macToString(peerMac));
  }
}

void onDataRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
  // Pair packets are accepted in the startup menu, before multiplayer starts.
  if (!multiplayer && len == sizeof(PairPacket)) {
    PairPacket pp;
    memcpy(&pp, incomingData, sizeof(PairPacket));
    if (pp.magic == PAIR_MAGIC) {
      lastPairPacket = pp;
      haveNewPairPacket = true;
      return;
    }
  }

  if (!multiplayer) return;  // ignore gameplay packets if in singleplayer/menu

  if (isHost) {
    // HOST: receive InputPacket from any paired client. The host assigns the
    // player slot by sender MAC, not by what the client claims in the packet.
    if (len == sizeof(InputPacket)) {
      InputPacket ip;
      memcpy(&ip, incomingData, sizeof(InputPacket));
      if (ip.magic == 0xC1E17AA1 && info != nullptr) {
        int slot = findClientSlotByMac(info->src_addr);
        if (slot >= 0) {
          uint8_t pid = slot + 1;
          lastInputFromClient[pid] = ip;
          pInputX[pid] = ip.tx;
          pInputY[pid] = ip.ty;
          pLastInputMs[pid] = millis();
          playerActiveMask |= (1 << pid);
          haveNewInputFor[pid] = true;
        }
      }
    }
  } else {
    // CLIENT: receive GameState from host.
    if (len == sizeof(GameState)) {
      GameState gs;
      memcpy(&gs, incomingData, sizeof(GameState));
      if (gs.magic == 0xDEADBEF1) {
        gState = gs;
        haveNewState = true;
      }
    }
  }
}

#if ESP_IDF_VERSION_MAJOR >= 5
void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  (void)tx_info;
  (void)status;
}
#else
void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {
  (void)mac;
  (void)status;
}
#endif

void ensureEspNowCore() {
  WiFi.mode(WIFI_STA);

  if (!espNowReady) {
    esp_err_t err = esp_now_init();
    if (err == ESP_OK) {
      esp_now_register_recv_cb(onDataRecv);
      esp_now_register_send_cb(onDataSent);
      espNowReady = true;
    } else {
      Serial.print("ESP-NOW init failed: ");
      Serial.println((int)err);
    }
  }
}

bool addEspNowPeerIfNeeded(const uint8_t mac[6]) {
  if (!espNowReady) return false;
  if (esp_now_is_peer_exist(mac)) return true;

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, mac, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  esp_err_t err = esp_now_add_peer(&peerInfo);
  return (err == ESP_OK || err == ESP_ERR_ESPNOW_EXIST);
}

void initPairingRadio() {
  ensureEspNowCore();
  addEspNowPeerIfNeeded(ESPNOW_BROADCAST_MAC);
}

void initEspNow() {
  ensureEspNowCore();
  if (cfgIsHostDevice) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
      if (clientMacValid[i]) addEspNowPeerIfNeeded(clientMacs[i]);
    }
  } else {
    addEspNowPeerIfNeeded(peerMac);
  }
}

// ------------------------------------------------------------
// Boot role selection + C-button pairing menu support
// ------------------------------------------------------------
void warmupInputsAfterM5Begin(uint16_t warmupMs = 650) {
  // Core2/CoreS3 touch can need a short time after M5.begin() before
  // M5.BtnA/B and M5.Touch.getDetail() become reliable.
  uint32_t t0 = millis();
  while ((uint32_t)(millis() - t0) < warmupMs) {
    M5.update();
    (void)M5.Touch.getDetail();  // harmless on non-touch boards; primes touch state on Core2/CoreS3
    delay(10);
  }
}

void applyBootRoleSelection() {
  M5.update();
  bool holdA = M5.BtnA.isPressed();
  bool holdB = M5.BtnB.isPressed();

  if (holdA && !holdB) {
    cfgIsHostDevice = true;
    saveRoleToNvs(true);
    bootRoleButtonWasUsed = true;
    bootRoleMessage = "Boot role: HOST saved";
  } else if (holdB && !holdA) {
    cfgIsHostDevice = false;
    saveRoleToNvs(false);
    bootRoleButtonWasUsed = true;
    bootRoleMessage = "Boot role: CLIENT saved";
  } else if (holdA && holdB) {
    bootRoleButtonWasUsed = true;
    bootRoleMessage = "Boot role unchanged: A+B held";
  } else {
    bootRoleMessage = "Boot role: saved role used";
  }
}

void applyBootClientClearSelection() {
  M5.update();
  bool holdC = M5.BtnC.isPressed();

  if (!holdC) {
    bootClearClientsMessage = "HOST clients kept";
    return;
  }

  bootClearClientsButtonWasUsed = true;

  if (cfgIsHostDevice) {
    clearAllHostClients();
    configDirty = true;
    pairingStatus = "HOST clients cleared";
    bootClearClientsMessage = "Boot C: HOST clients cleared";
    Serial.println("HOST boot C: stored client slots cleared.");
  } else {
    bootClearClientsMessage = "Boot C ignored: CLIENT role";
    Serial.println("Boot C ignored because this device is CLIENT.");
  }
}

void waitForBootButtonsReleaseIfNeeded() {
  if (!bootRoleButtonWasUsed && !bootClearClientsButtonWasUsed) return;

  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
  M5.Display.setCursor(10, 218);
  M5.Display.println("Release boot button(s) to continue...");

  while (true) {
    M5.update();
    if (!M5.BtnA.isPressed() && !M5.BtnB.isPressed() && !M5.BtnC.isPressed()) break;
    delay(20);
  }
}

void sendPairPacket() {
  PairPacket pp = {};
  pp.magic = PAIR_MAGIC;
  pp.version = 1;
  pp.role = cfgIsHostDevice ? 1 : 2;
  getLocalStaMac(pp.mac);
  pp.nonce = localPairNonce;
  pp.uptimeMs = millis();

  esp_now_send(ESPNOW_BROADCAST_MAC, (uint8_t*)&pp, sizeof(pp));
}

void startPairBroadcast() {
  initPairingRadio();
  localPairNonce = esp_random();
  pairSendUntilMs = millis() + PAIR_TX_WINDOW_MS;
  lastPairTxMs = 0;
  pairWindowAcceptedOneClient = false;
  pairingStatus = cfgIsHostDevice ? "Pairing: HOST C pressed" : "Pairing: CLIENT C pressed";
  configDirty = true;
  Serial.println("C pressed. Broadcasting pairing packet...");
}

void drawPairingStatusLine() {
  // Compact link/status block for the startup screen.
  M5.Display.fillRect(0, 148, scrX, 72, TFT_BLACK);
  M5.Display.setTextDatum(TL_DATUM);
  M5.Display.setTextSize(1);
  M5.Display.setFont(&fonts::FreeSansOblique9pt7b);

  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  M5.Display.drawString(String("Role: ") + (cfgIsHostDevice ? "HOST" : "CLIENT"), 10, 150);

  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  if (cfgIsHostDevice) {
    String slots = "Clients: ";
    for (int i = 0; i < MAX_CLIENTS; i++) {
      slots += clientMacValid[i] ? String("C") + String(i + 1) : String("-");
      if (i < MAX_CLIENTS - 1) slots += " ";
    }
    M5.Display.drawString(slots, 10, 170);
  } else {
    M5.Display.drawString(String("Host: ") + macToString(peerMac), 10, 170);
  }

  M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
  String linkLine = pairingStatus.length() > 0 ? pairingStatus : String("Link: press C on HOST + CLIENT");
  if (linkLine.length() > 34) linkLine = linkLine.substring(0, 34);
  M5.Display.drawString(linkLine, 10, 190);
}

bool processCButtonPairingMenu() {
  bool activity = false;

  if (haveNewPairPacket) {
    handleReceivedPairPacket();
    activity = true;
  }

  if (M5.BtnC.wasPressed()) {
    startPairBroadcast();
    activity = true;
  }

  uint32_t now = millis();
  if (pairSendUntilMs != 0 && now < pairSendUntilMs) {
    if (lastPairTxMs == 0 || (now - lastPairTxMs) >= PAIR_TX_PERIOD_MS) {
      lastPairTxMs = now;
      sendPairPacket();
    }
  } else if (pairSendUntilMs != 0 && now >= pairSendUntilMs) {
    pairSendUntilMs = 0;
    pairWindowAcceptedOneClient = false;
    if (!pairingStatus.startsWith("PAIRED")) {
      pairingStatus = "Pairing ended. Press C on both.";
      configDirty = true;
    }
  }

  if (pairVibrationUntilMs != 0 && now < pairVibrationUntilMs) {
    M5.Power.setVibration(180);
  } else if (pairVibrationUntilMs != 0 && now >= pairVibrationUntilMs) {
    M5.Power.setVibration(0);
    pairVibrationUntilMs = 0;
  }

  return activity;
}

// ------------------------------------------------------------

void simulatePlayer(float &xs, float &ys, float &xv, float &yv,
                    float txIn, float tyIn, float dt) {
  if (grippy) { drag = 0.92; topoR = 0.7; }
  else        { drag = 0.99; topoR = 2.0; }

  txIn = constrain(txIn, -1.0f, 1.0f);
  tyIn = constrain(tyIn, -1.0f, 1.0f);

  int xi = floorf(xs / (scrX / (NcpX - 1)));
  int yi = floorf(ys / (scrY / (NcpY - 1)));
  xi = constrain(xi, 0, NcpX - 2);
  yi = constrain(yi, 0, NcpY - 2);

  // dt here is scaled like your original: ms * 10
  float dtSec = dt / 1000.0f;

  xv += conR * txIn * dtSec - topoR * Fx[xi][yi] * dtSec;
  yv += conR * tyIn * dtSec - topoR * Fy[xi][yi] * dtSec;
  xv *= drag;
  yv *= drag;

  xs += xv * dtSec;
  ys += yv * dtSec;

  if (!teleport) {
    if (xs < ballR)               { xv = -CoR * xv; xs = ballR; }
    if (xs > (scrX - ballR))      { xv = -CoR * xv; xs = scrX - ballR; }
    if (ys < topBar + ballR)      { yv = -CoR * yv; ys = topBar + ballR; }
    if (ys > (scrY - ballR))      { yv = -CoR * yv; ys = scrY - ballR; }
  } else {
    if (xs < ballR)               { xs = scrX - ballR; }
    if (xs > (scrX - ballR))      { xs = ballR; }
    if (ys < topBar + ballR)      { ys = scrY - ballR; }
    if (ys > (scrY - ballR))      { ys = topBar + ballR; }
  }
}

// ------------------------------------------------------------
// True multiplayer helpers
// ------------------------------------------------------------
bool isPlayerActive(uint8_t pid) {
  return (pid < MAX_PLAYERS) && ((playerActiveMask & (1 << pid)) != 0);
}

void initPlayersForMode() {
  localPlayerId = isHost ? HOST_PLAYER_ID : 1;
  playerActiveMask = 0x01;
  hostDisconnectedMask = 0;
  clientLinkLost = false;
  if (multiplayer && isHost) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
      if (clientMacValid[i]) playerActiveMask |= (1 << (i + 1));
    }
  } else if (multiplayer && !isHost) {
    // Client will receive the real active mask and assigned ID from host.
    playerActiveMask = 0x03;
  }

  float startX[MAX_PLAYERS] = { scrX * 0.25f, scrX * 0.75f, scrX * 0.25f, scrX * 0.75f };
  float startY[MAX_PLAYERS] = { (scrY + topBar) * 0.5f, (scrY + topBar) * 0.5f, topBar + 42.0f, scrY - 42.0f };

  for (int i = 0; i < MAX_PLAYERS; i++) {
    pX[i] = startX[i];
    pY[i] = startY[i];
    pVX[i] = 0.0f;
    pVY[i] = 0.0f;
    pInputX[i] = 0.0f;
    pInputY[i] = 0.0f;
    pPrevX[i] = -1000;
    pPrevY[i] = -1000;
    pScore[i] = 0;
    pLastInputMs[i] = 0;
    haveNewInputFor[i] = false;
  }

  // Keep legacy globals roughly synced for any old code left in place.
  xs1 = pX[0]; ys1 = pY[0]; xv1 = pVX[0]; yv1 = pVY[0]; score1 = pScore[0];
  xs2 = pX[1]; ys2 = pY[1]; xv2 = pVX[1]; yv2 = pVY[1]; score2 = pScore[1];
}

int32_t sumScores() {
  int32_t s = 0;
  for (int i = 0; i < MAX_PLAYERS; i++) if (isPlayerActive(i)) s += pScore[i];
  return s;
}

int bestPlayerExcluding(int excludePid) {
  int best = -1;
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (!isPlayerActive(i) || i == excludePid) continue;
    if (best < 0 || pScore[i] > pScore[best]) best = i;
  }
  return best;
}

int bestPlayerOverall() {
  return bestPlayerExcluding(-1);
}

int8_t winnerCodeOrTie() {
  int best = -1;
  bool tie = false;
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (!isPlayerActive(i)) continue;
    if (best < 0 || pScore[i] > pScore[best]) {
      best = i;
      tie = false;
    } else if (pScore[i] == pScore[best]) {
      tie = true;
    }
  }
  if (best < 0 || tie) return 0;
  return (int8_t)(best + 1); // 1=H, 2=C1, 3=C2, 4=C3
}

void fillGameStateForRecipient(uint8_t recipientPlayerId, uint8_t gameOver, int8_t winner) {
  gState.frame++;
  for (int i = 0; i < MAX_PLAYERS; i++) {
    gState.px[i] = pX[i];
    gState.py[i] = pY[i];
    gState.vx[i] = pVX[i];
    gState.vy[i] = pVY[i];
    gState.inputTx[i] = pInputX[i];
    gState.inputTy[i] = pInputY[i];
    gState.score[i] = pScore[i];
  }
  gState.targX = targX;
  gState.targY = targY;
  gState.targetBonusPoints = currentTargetBonusPoints();
  int32_t remainingMs = (int32_t)(gameEndTimeMs - millis());
  if (remainingMs < 0) remainingMs = 0;
  gState.timeLeftMs = gameOver ? 0 : remainingMs;
  gState.rngSeed = rngNo;
  gState.activeMask = playerActiveMask;
  gState.recipientPlayerId = recipientPlayerId;
  gState.gameOver = gameOver;
  gState.winner = winner;
}

void sendGameStateToClients(uint8_t gameOver, int8_t winner) {
  if (!multiplayer || !isHost) return;
  for (int slot = 0; slot < MAX_CLIENTS; slot++) {
    if (!clientMacValid[slot]) continue;
    uint8_t pid = slot + 1;
    fillGameStateForRecipient(pid, gameOver, winner);
    esp_now_send(clientMacs[slot], (uint8_t*)&gState, sizeof(gState));
  }
}

void syncLocalLegacyFromPlayers() {
  xs1 = pX[0]; ys1 = pY[0]; xv1 = pVX[0]; yv1 = pVY[0]; score1 = pScore[0];
  xs2 = pX[1]; ys2 = pY[1]; xv2 = pVX[1]; yv2 = pVY[1]; score2 = pScore[1];
}

int currentTargetBonusPoints() {
  if (multiplayer && !isHost && firstStateReceived) {
    return constrain(remoteTargetBonusPoints, 100, 1000);
  }
  int points = 1000 - (int)((millis() - lastCapt) / 10);
  return max(points, 100);
}

int currentTargetVisualRadius() {
  // Bonus 1000 -> radius 8 (about 130% of the old radius 6).
  // Bonus  100 -> radius 5 (about  80% of the old radius 6).
  int bonus = currentTargetBonusPoints();
  return 5 + ((bonus - 100) * 3 + 450) / 900;
}

// ------------------------------------------------------------
// Drawing helpers
// ------------------------------------------------------------
void drawFrame(int dTargX, int dTargY, int32_t timeLeftMs) {
  // cleanup previous locations
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (isPlayerActive(i) || pPrevX[i] > -999) cleanUp(pPrevX[i], pPrevY[i]);
  }
  cleanUp(prevTargX, prevTargY);

  // Players
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (!isPlayerActive(i)) continue;

    float ix = constrain(pInputX[i], -1.0f, 1.0f);
    float iy = constrain(pInputY[i], -1.0f, 1.0f);

    scene.fillTriangle(int(pX[i] + ix * (ballR + vectL)), int(pY[i] + iy * (ballR + vectL)),
                       int(pX[i] + iy * ballR),           int(pY[i] - ix * ballR),
                       int(pX[i] - iy * ballR),           int(pY[i] + ix * ballR),
                       TFT_BLACK);

    scene.fillEllipse(pX[i], pY[i], ballR - 1, ballR - 1, PLAYER_COLORS[i]);
    scene.drawEllipse(pX[i], pY[i], ballR + 1, ballR + 1, PLAYER_COLORS[i]);
  }

  // Green diamond: its size visualizes the currently available bonus points.
  int targetR = currentTargetVisualRadius();
  scene.fillTriangle(dTargX, dTargY - targetR,
                     dTargX + targetR, dTargY,
                     dTargX, dTargY + targetR, TFT_GREEN);
  scene.fillTriangle(dTargX, dTargY - targetR,
                     dTargX, dTargY + targetR,
                     dTargX - targetR, dTargY, TFT_GREEN);
  scene.drawLine(dTargX, dTargY - targetR, dTargX + targetR, dTargY, TFT_BLACK);
  scene.drawLine(dTargX + targetR, dTargY, dTargX, dTargY + targetR, TFT_BLACK);
  scene.drawLine(dTargX, dTargY + targetR, dTargX - targetR, dTargY, TFT_BLACK);
  scene.drawLine(dTargX - targetR, dTargY, dTargX, dTargY - targetR, TFT_BLACK);

  // HUD
  scene.fillRect(0, 0, scrX, topBar, TFT_BLACK);
  scene.setTextSize(2);
  scene.setTextDatum(TL_DATUM);

  uint8_t me = localPlayerId;
  if (me >= MAX_PLAYERS || !isPlayerActive(me)) me = HOST_PLAYER_ID;

  // First score in the top bar is always THIS device/player, in that player's color.
  scene.setTextColor(PLAYER_COLORS[me], TFT_TRANSPARENT);
  scene.drawString(String(PLAYER_LABELS[me]) + ":", 2, 1);
  scene.drawNumber(pScore[me], 34, 1);

  if (multiplayer) {
    // On the host, replace the secondary score with a clear link warning while
    // one or more previously joined clients are disconnected.
    if (isHost && hostDisconnectedMask != 0) {
      String lost = "LOST";
      for (int pid = 1; pid < MAX_PLAYERS; pid++) {
        if (hostDisconnectedMask & (1 << pid)) {
          lost += " ";
          lost += PLAYER_LABELS[pid];
        }
      }
      scene.setTextColor(TFT_YELLOW, TFT_TRANSPARENT);
      scene.drawString(lost, 116, 1);
    } else {
    int best = bestPlayerOverall();
    int showPid = best;
    if (showPid == me) showPid = bestPlayerExcluding(me); // if you are top, show second top
    if (showPid >= 0) {
      scene.setTextColor(PLAYER_COLORS[showPid], TFT_TRANSPARENT);
      scene.drawString(String(PLAYER_LABELS[showPid]) + ":", 116, 1);
      scene.drawNumber(pScore[showPid], 154, 1);
    }
    }
  }

  // timer (right) – white
  float t  = timeLeftMs / 1000.0f;
  scene.setTextDatum(TR_DATUM);
  scene.setTextColor(TFT_WHITE, TFT_TRANSPARENT);
  scene.drawFloat(t, 1, scrX, 1);

  // highscore (center) – only in singleplayer & host
  if (!multiplayer && isHost) {
    scene.setTextDatum(TC_DATUM);
    scene.setTextColor(TFT_MAGENTA, TFT_TRANSPARENT);
    scene.drawNumber(HS, scrX / 2, 1);
  }

  scene.setTextDatum(TL_DATUM);
  scene.pushSprite(0, 0);

  // store for next cleanup
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (isPlayerActive(i)) {
      pPrevX[i] = int(pX[i]);
      pPrevY[i] = int(pY[i]);
    }
  }
  prevTargX = dTargX;
  prevTargY = dTargY;
}

void resetScoreHistory() {
  scoreHistoryCount = 0;
  lastScoreHistorySampleMs = 0;
  playerParticipantMask = isHost ? 0x01 : 0x00;
}

void recordScoreHistory(bool forceSample) {
  uint32_t now = millis();
  if (!forceSample && lastScoreHistorySampleMs != 0 &&
      (uint32_t)(now - lastScoreHistorySampleMs) < 1000) return;
  if (scoreHistoryCount >= MAX_SCORE_SAMPLES) return;

  uint16_t index = scoreHistoryCount++;
  scoreHistoryTimeSec[index] = (uint16_t)min((uint32_t)65535, (now - gameStartMs) / 1000);
  for (int pid = 0; pid < MAX_PLAYERS; pid++) scoreHistory[pid][index] = pScore[pid];
  playerParticipantMask |= playerActiveMask;
  lastScoreHistorySampleMs = now;
}

PostGameAction showPostGameResults(int8_t winner) {
  recordScoreHistory(true);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextDatum(MC_DATUM);
  M5.Display.fillScreen(TFT_BLACK);

  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_RED, TFT_BLACK);
  M5.Display.drawString("GAME OVER", scrX / 2, 10);

  String result;
  if (winner == 0) result = "Tie";
  else {
    int winPid = winner - 1;
    result = (winPid == localPlayerId) ? "You win" : String("Winner: ") + PLAYER_LABELS[winPid];
  }
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.drawString(result, scrX / 2, 29);

  const int gx = 28, gy = 43, gw = 282, gh = 124;
  M5.Display.drawLine(gx, gy, gx, gy + gh, TFT_WHITE);
  M5.Display.drawLine(gx, gy + gh, gx + gw, gy + gh, TFT_WHITE);

  int32_t maxScore = 1;
  uint16_t maxTime = 1;
  for (uint16_t s = 0; s < scoreHistoryCount; s++) {
    maxTime = max(maxTime, scoreHistoryTimeSec[s]);
    for (int pid = 0; pid < MAX_PLAYERS; pid++) {
      if (playerParticipantMask & (1 << pid)) maxScore = max(maxScore, scoreHistory[pid][s]);
    }
  }

  M5.Display.setTextDatum(TR_DATUM);
  M5.Display.drawNumber(maxScore, gx - 2, gy);
  M5.Display.setTextDatum(TC_DATUM);
  M5.Display.drawString("0", gx, gy + gh + 2);
  M5.Display.drawString(String(maxTime) + "s", gx + gw, gy + gh + 2);

  for (int pid = 0; pid < MAX_PLAYERS; pid++) {
    if (!(playerParticipantMask & (1 << pid))) continue;
    for (uint16_t s = 1; s < scoreHistoryCount; s++) {
      int x0 = gx + ((uint32_t)scoreHistoryTimeSec[s - 1] * gw) / maxTime;
      int x1 = gx + ((uint32_t)scoreHistoryTimeSec[s] * gw) / maxTime;
      int y0 = gy + gh - ((int64_t)scoreHistory[pid][s - 1] * gh) / maxScore;
      int y1 = gy + gh - ((int64_t)scoreHistory[pid][s] * gh) / maxScore;
      M5.Display.drawLine(x0, y0, x1, y1, PLAYER_COLORS[pid]);
    }
  }

  M5.Display.setTextDatum(TL_DATUM);
  int legendX = 32;
  for (int pid = 0; pid < MAX_PLAYERS; pid++) {
    if (!(playerParticipantMask & (1 << pid))) continue;
    M5.Display.setTextColor(PLAYER_COLORS[pid], TFT_BLACK);
    M5.Display.drawString(PLAYER_LABELS[pid], legendX, 179);
    legendX += 38;
  }

  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
  M5.Display.drawString("A / LEFT: Play Again", 12, 201);
  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  M5.Display.drawString("B / RIGHT: Main Menu", 12, 221);

  while (M5.BtnA.isPressed() || M5.BtnB.isPressed()) {
    M5.update();
    delay(10);
  }

  postGameScreenStartMs = millis();
  while (true) {
    M5.update();
    bool playAgain = M5.BtnA.wasPressed();
    bool mainMenu = M5.BtnB.wasPressed();
    if (hasTouchscreen()) {
      auto td = M5.Touch.getDetail();
      if (td.wasPressed()) {
        if (td.x < scrX / 2) playAgain = true;
        else mainMenu = true;
      }
    }
    if (playAgain) return POSTGAME_PLAY_AGAIN;
    if (mainMenu) return POSTGAME_MAIN_MENU;

    if (isUsbPowered()) postGameScreenStartMs = millis();
    else if (millis() - postGameScreenStartMs >= POSTGAME_AUTO_POWER_OFF_MS) {
      autoPowerOffNow("Post-game timeout");
    }
    delay(20);
  }
}


void drawStartupTerrainBackground(M5Canvas &dst) {
  if (startupTerrainSeed == 0) startupTerrainSeed = esp_random();

  randomSeed(startupTerrainSeed);

  float localMin =  999999.0F;
  float localMax = -999999.0F;

  // Use the same terrain idea as the game: random control points + smoothing + bilinear interpolation.
  for (int i = 0; i < NcpX; i++) {
    for (int j = 0; j < NcpY; j++) {
      field[i][j] = float(random(-cpA, cpA + 1)) / 100.0f;
      if ((i == 0) || (i == (NcpX - 1)) || (j == 0) || (j == (NcpY - 1))) field[i][j] = 0.0f;
    }
  }

  for (int kpass = 0; kpass < smoothPass; kpass++) {
    for (int i = 1; i < (NcpX - 1); i++) {
      for (int j = 1; j < (NcpY - 1); j++) {
        field[i][j] =  (field[i - 1][j - 1] + field[i][j - 1] + field[i + 1][j - 1] +
                        field[i - 1][j]     + field[i][j]     + field[i + 1][j] +
                        field[i - 1][j + 1] + field[i][j + 1] + field[i + 1][j + 1]) / 9.0F;
      }
    }
  }

  for (int i = 0; i < NcpX; i++) {
    for (int j = 0; j < NcpY; j++) {
      if (field[i][j] < localMin) localMin = field[i][j];
      if (field[i][j] > localMax) localMax = field[i][j];
    }
  }

  float dFmax = fabsf(localMax) / (Ncols / 2);
  float dFmin = fabsf(localMin) / (Ncols / 2);
  float localDF = max(dFmax, dFmin);
  if (localDF < 0.0001f) localDF = 0.0001f;

  isolines();
  convertTo565();

  float cellW = float(scrX) / float(NcpX - 1);
  float cellH = float(scrY) / float(NcpY - 1);

  for (int y = 0; y < scrY; y++) {
    for (int x = 0; x < scrX; x++) {
      int inx = floorf(float(x) / cellW);
      int iny = floorf(float(y) / cellH);
      inx = constrain(inx, 0, NcpX - 2);
      iny = constrain(iny, 0, NcpY - 2);

      float xm = float(x) - cellW * inx;
      float ym = float(y) - cellH * iny;

      float z = 0.01f * ( field[inx][iny]         * (cellW - xm) * (cellH - ym)
                        + field[inx + 1][iny]     * xm          * (cellH - ym)
                        + field[inx][iny + 1]     * (cellW - xm) * ym
                        + field[inx + 1][iny + 1] * xm          * ym );

      int ncol = (Ncols / 2) + lroundf(z / localDF);
      ncol = max(0, min(Ncols - 1, ncol));

      // Keep the terrain visible on the startup screen.
      uint16_t c = scale565(cols[ncol], 245);
      dst.drawPixel(x, y, c);
    }
  }

}

void drawBlackTextOnly(M5Canvas &dst, const String &txt, int32_t x, int32_t y) {
  // IMPORTANT: use the one-argument setTextColor().
  // The two-argument form can fill the FreeFont bounding box on M5Canvas,
  // which was causing the dark rectangles on the startup screen.
  dst.setTextColor(TFT_WHITE);
  for(int i=-2;i<=2;i+=2)
    for(int j=-2;j<=2;j+=2)
      dst.drawString(txt, x-i, y+j);
  
  
  dst.setTextColor(TFT_BLACK);
  dst.drawString(txt, x, y);
}

// Helper: does this board have a touchscreen?
bool hasTouchscreen() {
#if defined(ARDUINO_M5STACK_CORES3) || defined(ARDUINO_M5STACK_Core2)
  return true;
#else
  return false;
#endif
}

void drawModeMenu(bool touch) {
  (void)touch;  // menu uses A/B/C labels on all boards; touchscreen left/right still works.

  if (!menuScreenReady) {
    M5.Display.fillScreen(TFT_BLACK);
    return;
  }

  drawStartupTerrainBackground(menuScreen);
  menuScreen.setTextDatum(TL_DATUM);
  menuScreen.setTextSize(1);
  menuScreen.setTextPadding(0);

  // No rectangles, no halo, no background color: draw only black glyphs.
  menuScreen.setFont(&fonts::FreeSansBoldOblique12pt7b);
  drawBlackTextOnly(menuScreen, "Gradients 1.17", 10, 10);

  menuScreen.setFont(&fonts::FreeSansBoldOblique9pt7b);
  drawBlackTextOnly(menuScreen, "github.com/vlado83/Gradients_MP", 10, 43);

  String batLine = batteryStatusString();
  int batX = scrX - menuScreen.textWidth(batLine) - 10;
  if (batX < 10) batX = 10;
  drawBlackTextOnly(menuScreen, batLine, batX, 73);

  drawBlackTextOnly(menuScreen, "A: Singleplayer", 26, 108);
  drawBlackTextOnly(menuScreen, "B: Multi", 26, 133);

  String roleLine = cfgIsHostDevice ? "Role: HOST" : "Role: CLIENT";
  drawBlackTextOnly(menuScreen, roleLine, 26, 178);

  if (cfgIsHostDevice) {
    String slots = "Clients: ";
    for (int i = 0; i < MAX_CLIENTS; i++) {
      slots += clientMacValid[i] ? String("C") + String(i + 1) : String("-");
      if (i < MAX_CLIENTS - 1) slots += " ";
    }
    drawBlackTextOnly(menuScreen, slots, 26, 200);
  } else {
    drawBlackTextOnly(menuScreen, "Host saved: " + String(!isZeroMac(peerMac) ? "yes" : "no"), 26, 200);
  }

  String linkLine = pairingStatus.length() > 0 ? pairingStatus : String("C: pair HOST + CLIENT");
  if (linkLine.length() > 30) linkLine = linkLine.substring(0, 30);
  drawBlackTextOnly(menuScreen, linkLine, 26, 222);

  menuScreen.pushSprite(0, 0);
}

// ------------------------------------------------------------

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Speaker.begin();
  applySfxVolume();
  playSfx(SFX_BOOT);

  Serial.begin(115200);
  delay(100);

  warmupInputsAfterM5Begin();

  WiFi.mode(WIFI_STA);       // needed to know local STA MAC and improves RNG entropy later
  loadRuntimeConfig();
  applyBootRoleSelection();
  applyBootClientClearSelection();

  printConfig();

  M5.Display.setRotation(1);
  M5.Display.setBrightness(200);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(1);

  bool touch = hasTouchscreen();

  // Startup menu is rendered off-screen and pushed as one sprite, so it does not flicker.
  startupTerrainSeed = esp_random();
  menuScreen.setColorDepth(16);
  menuScreen.createSprite(scrX, scrY);
  menuScreenReady = true;

  initPairingRadio();
  drawModeMenu(touch);
  waitForBootButtonsReleaseIfNeeded();
  if (bootRoleButtonWasUsed || bootClearClientsButtonWasUsed) {
    drawModeMenu(touch);
  }

  uint32_t menuIdleStartMs = millis();
  uint32_t lastMenuBatteryRefreshMs = millis();

  // Static terrain start screen + mode selection. No Lissajous intro drawing.
  while (true) {
    M5.update();

    processSerialConfig();
    bool menuActivity = processCButtonPairingMenu();
    if (configDirty) {
      drawModeMenu(touch);
      configDirty = false;
      menuActivity = true;
    }

    uint32_t nowMenuMs = millis();
    if (nowMenuMs - lastMenuBatteryRefreshMs >= MENU_BATTERY_REFRESH_MS) {
      lastMenuBatteryRefreshMs = nowMenuMs;
      drawModeMenu(touch);
    }

    // --- Menu selection ---
    // Accept BOTH input styles. On Core2, BtnA/BtnB are derived from the
    // touch controller, so this also gives us a fallback when the full-screen
    // touch zones are not behaving as expected.
    bool chooseSingle = false;
    bool chooseMulti  = false;

    if (touch) {
      auto td = M5.Touch.getDetail();
      if (td.wasPressed()) {
        Serial.printf("Menu touch: x=%d y=%d\n", td.x, td.y);
        menuActivity = true;

        // Bottom-right virtual C area is reserved for C-button pairing.
        bool cButtonZone = (td.y > scrY - 55) && (td.x > (2 * scrX) / 3);
        if (!cButtonZone) {
          if (td.x < scrX / 2) chooseSingle = true;
          else                 chooseMulti  = true;
        }
      }
    }

    if (M5.BtnA.wasPressed()) { chooseSingle = true; menuActivity = true; }
    if (M5.BtnB.wasPressed()) { chooseMulti  = true; menuActivity = true; }

    if (menuActivity) menuIdleStartMs = nowMenuMs;

    // Do not auto-shutdown while connected to USB. Reset the idle baseline so
    // unplugging starts a fresh 60-second timeout instead of powering off at once.
    if (isUsbPowered()) {
      menuIdleStartMs = nowMenuMs;
    } else if (nowMenuMs - menuIdleStartMs >= MENU_AUTO_POWER_OFF_MS) {
      autoPowerOffNow("Menu timeout");
    }

    if (chooseSingle) {
      multiplayer = false;
      // Single-player is always simulated locally. Keep cfgIsHostDevice unchanged
      // because it is the device's saved role for future multiplayer sessions.
      isHost = true;
      goto mode_selected;
    }
    if (chooseMulti) {
      multiplayer = true;
      isHost = cfgIsHostDevice;
      goto mode_selected;
    }

    delay(10);
  }

mode_selected:

  // Startup screen uses GFX free fonts. Return to the default bitmap font
  // before creating the game canvases/HUD.
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextSize(1);
  M5.Power.setVibration(0);
  playSfx(SFX_START);
  if (menuScreenReady) {
    menuScreen.deleteSprite();
    menuScreenReady = false;
  }

  calibrateImuNeutral(false);
  // If A selected the mode and is still held, require a release before the
  // in-game long-press gesture can trigger another calibration.
  imuRecalibrationLatched = M5.BtnA.isPressed();

  Serial.print("Starting mode: " );
  Serial.print(multiplayer ? "MULTIPLAYER " : "SINGLEPLAYER " );
  Serial.println(isHost ? "HOST" : "CLIENT");

  initPlayersForMode();
  postGameScreenStartMs = 0;

  // Prepare canvases
  bck.createSprite(scrX, scrY);
  scene.createSprite(scrX, scrY);
  bck.fillScreen(TFT_BLACK);
  scene.fillScreen(TFT_BLACK);
  scene.pushSprite(0, 0);

  // RANDOM terrain:
  // - Singleplayer or Host: choose seed now and generate.
  // - Client: wait until first GameState (rngSeed) to generate.
  if (!multiplayer || isHost) {
    rngNo = esp_random();
    randomSeed(rngNo);
    generateTerrain();
    generateGradients();

    // copy background into scene
    for (int x = 0; x < scrX; x++) {
      for (int y = 0; y < scrY; y++) {
        scene.drawPixel(x, y, bck.readPixel(x, y));
      }
    }
    scene.pushSprite(0, 0);
    terrainReady = true;
  } else {
    terrainReady = false;
  }

  targX = random(ballR, scrX - ballR);
  targY = random(topBar + ballR, scrY - ballR);
  lastCapt = millis();
  prevTargX = targX;
  prevTargY = targY;

  // highscore only in singleplayer & host
  if (!multiplayer && isHost) {
    preferences.begin("VGame", false);
    counter = preferences.getLong("counter", -1);
    if (counter == -1) { preferences.putLong("counter", 1); counter = 1; }
    HS = preferences.getLong("HS", -1);
    if (HS == -1) { preferences.putLong("HS", 0); HS = 0; }
  } else {
    HS = 0;
  }

  // 90s from now
  gameEndTimeMs = millis() + 90000;
  gameStartMs = millis();
  resetScoreHistory();
  if (!multiplayer || isHost) recordScoreHistory(true);

  // reset timing baseline AFTER intro
  lastR = millis();

  // Init ESP-NOW only if multiplayer
  if (multiplayer) {
    initEspNow();
  }
}

// ------------------------------------------------------------
// Singleplayer + Host gameplay (physics + scoring)
// ------------------------------------------------------------
void returnToMainMenuAndStart() {
  multiplayer = false; // menu/pairing packets are handled outside gameplay
  M5.Power.setVibration(0);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextSize(1);
  startupTerrainSeed = esp_random();

  if (!menuScreenReady) {
    menuScreen.setColorDepth(16);
    menuScreen.createSprite(scrX, scrY);
    menuScreenReady = true;
  }

  bool touch = hasTouchscreen();
  drawModeMenu(touch);
  while (M5.BtnA.isPressed() || M5.BtnB.isPressed()) {
    M5.update();
    delay(10);
  }

  uint32_t menuIdleStartMs = millis();
  uint32_t lastMenuBatteryRefreshMs = millis();
  while (true) {
    M5.update();
    processSerialConfig();
    bool menuActivity = processCButtonPairingMenu();
    if (configDirty) {
      drawModeMenu(touch);
      configDirty = false;
      menuActivity = true;
    }

    uint32_t nowMenuMs = millis();
    if (nowMenuMs - lastMenuBatteryRefreshMs >= MENU_BATTERY_REFRESH_MS) {
      lastMenuBatteryRefreshMs = nowMenuMs;
      drawModeMenu(touch);
    }

    bool chooseSingle = false;
    bool chooseMulti = false;
    if (touch) {
      auto td = M5.Touch.getDetail();
      if (td.wasPressed()) {
        menuActivity = true;
        bool cButtonZone = (td.y > scrY - 55) && (td.x > (2 * scrX) / 3);
        if (!cButtonZone) {
          if (td.x < scrX / 2) chooseSingle = true;
          else chooseMulti = true;
        }
      }
    }
    if (M5.BtnA.wasPressed()) { chooseSingle = true; menuActivity = true; }
    if (M5.BtnB.wasPressed()) { chooseMulti = true; menuActivity = true; }
    if (menuActivity) menuIdleStartMs = nowMenuMs;

    if (isUsbPowered()) menuIdleStartMs = nowMenuMs;
    else if (nowMenuMs - menuIdleStartMs >= MENU_AUTO_POWER_OFF_MS) autoPowerOffNow("Menu timeout");

    if (chooseSingle) {
      multiplayer = false;
      isHost = true;
      break;
    }
    if (chooseMulti) {
      multiplayer = true;
      isHost = cfgIsHostDevice;
      break;
    }
    delay(10);
  }

  menuScreen.deleteSprite();
  menuScreenReady = false;
  calibrateImuNeutral(false);
  imuRecalibrationLatched = M5.BtnA.isPressed();
  if (multiplayer) initEspNow();
  restartCurrentGame();
}

void restartCurrentGame() {
  remoteGameOver = false;
  remoteWinner = 0;
  remoteTargetBonusPoints = 1000;
  firstStateReceived = isHost;
  lastStateMs = 0;
  waitingShown = false;
  clientLinkLost = false;
  haveNewState = false;
  gState.frame = 0;
  initPlayersForMode();

  bck.fillScreen(TFT_BLACK);
  scene.fillScreen(TFT_BLACK);

  if (!multiplayer || isHost) {
    rngNo = esp_random();
    randomSeed(rngNo);
    generateTerrain();
    generateGradients();
    for (int x = 0; x < scrX; x++) {
      for (int y = 0; y < scrY; y++) scene.drawPixel(x, y, bck.readPixel(x, y));
    }
    scene.pushSprite(0, 0);
    terrainReady = true;
  } else {
    terrainReady = false;
    M5.Display.fillScreen(TFT_BLACK);
  }

  targX = random(ballR, scrX - ballR);
  targY = random(topBar + ballR, scrY - ballR);
  lastCapt = millis();
  prevTargX = targX;
  prevTargY = targY;

  if (!multiplayer && isHost) {
    preferences.begin("VGame", false);
    counter = preferences.getLong("counter", 1);
    HS = preferences.getLong("HS", 0);
  } else {
    HS = 0;
  }

  gameStartMs = millis();
  gameEndTimeMs = gameStartMs + 90000;
  lastR = gameStartMs;
  postGameScreenStartMs = 0;
  resetScoreHistory();
  if (!multiplayer || isHost) recordScoreHistory(true);
  playSfx(SFX_START);
}

void loopHostLike(bool asMultiplayer) {
  processImuRecalibrationRequest();

  // IMU for host/local player
  if (M5.Imu.getAccel(&accX, &accY, &accZ)) {
    // ok
  }

  float calibratedX = applyImuDeadZone(accX - imuNeutralX);
  float calibratedY = applyImuDeadZone(accY - imuNeutralY);
  tx = tx * sm - calibratedX * (1.0f - sm);
  ty = ty * sm + calibratedY * (1.0f - sm);
  tx = constrain(tx, -1.0f, 1.0f);
  ty = constrain(ty, -1.0f, 1.0f);
  pInputX[HOST_PLAYER_ID] = tx;
  pInputY[HOST_PLAYER_ID] = ty;

  uint32_t now = millis();
  int32_t dt_ms = (int32_t)(now - lastR);
  if (dt_ms < 1)   dt_ms = 1;
  if (dt_ms > 40)  dt_ms = 40;
  lastR = now;
  float dt = (float)dt_ms * 10.0f;

  // In multiplayer, paired clients are active only while input packets are
  // arriving. A timed-out player gets neutral input and automatically rejoins
  // as soon as a fresh packet updates pLastInputMs.
  if (asMultiplayer) {
    playerActiveMask = 0x01;
    hostDisconnectedMask = 0;
    for (int slot = 0; slot < MAX_CLIENTS; slot++) {
      uint8_t pid = slot + 1;
      if (!clientMacValid[slot]) continue;

      bool hasJoined = pLastInputMs[pid] != 0;
      bool connected = hasJoined && ((uint32_t)(now - pLastInputMs[pid]) < CONNECTION_TIMEOUT_MS);
      if (connected) {
        playerActiveMask |= (1 << pid);
      } else {
        pInputX[pid] = 0.0f;
        pInputY[pid] = 0.0f;
        if (hasJoined) hostDisconnectedMask |= (1 << pid);
      }
    }
  } else {
    playerActiveMask = 0x01;
    hostDisconnectedMask = 0;
  }

  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (!isPlayerActive(i)) continue;
    simulatePlayer(pX[i], pY[i], pVX[i], pVY[i], pInputX[i], pInputY[i], dt);
  }

  // Pairwise bumping between all active players.
  if (asMultiplayer) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
      if (!isPlayerActive(i)) continue;
      for (int j = i + 1; j < MAX_PLAYERS; j++) {
        if (!isPlayerActive(j)) continue;
        float dx = pX[i] - pX[j];
        float dy = pY[i] - pY[j];
        float dist2 = dx * dx + dy * dy;
        float minDist = 2 * ballR;
        if (dist2 < (minDist * minDist)) {
          float tmpx = pVX[i]; pVX[i] = pVX[j]; pVX[j] = tmpx;
          float tmpy = pVY[i]; pVY[i] = pVY[j]; pVY[j] = tmpy;
        }
      }
    }
  }

  // Capture logic: every active player touching the target gets the same points.
  bool anyGot = false;
  bool got[MAX_PLAYERS] = { false, false, false, false };
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (!isPlayerActive(i)) continue;
    got[i] = (fabsf(pX[i] - targX) < precision) && (fabsf(pY[i] - targY) < precision);
    anyGot = anyGot || got[i];
  }

  if (anyGot) {
    playSfx(SFX_SCORE);
    int s = currentTargetBonusPoints();

    for (int i = 0; i < MAX_PLAYERS; i++) {
      if (got[i]) pScore[i] += s;
    }

    targX = random(ballR, scrX - ballR);
    targY = random(topBar + ballR, scrY - ballR);
    gameEndTimeMs += 2000;
    lastCapt = millis();
  }

  int32_t timeLeftMs = (int32_t)(gameEndTimeMs - millis());
  if (timeLeftMs < 0) timeLeftMs = 0;
  recordScoreHistory(false);

  static uint32_t lastSendMs = 0;
  if (asMultiplayer && (now - lastSendMs >= NET_DT_MS)) {
    lastSendMs = now;
    sendGameStateToClients(0, 0);
  }

  syncLocalLegacyFromPlayers();
  drawFrame(targX, targY, timeLeftMs);

  if ((millis() - lastCapt) < 120) M5.Power.setVibration(210);
  else M5.Power.setVibration(0);

  // game over
  if (millis() > gameEndTimeMs) {
    int8_t winner = asMultiplayer ? winnerCodeOrTie() : 1;

    if (!multiplayer && isHost) {
      int totalScore = pScore[HOST_PLAYER_ID];
      if (totalScore > HS) {
        HS = totalScore;
        preferences.putLong("HS", HS);
        M5.Display.setTextColor(TFT_MAGENTA, TFT_BLACK);
        M5.Display.setTextDatum(MC_DATUM);
        M5.Display.drawString("New high score!", scrX / 2, 40, 2);
      }
      counter++;
      preferences.putLong("counter", counter);
      preferences.end();
    }

    if (asMultiplayer) {
      // Repeat final state a few times to reduce the chance that one client misses it.
      for (int k = 0; k < 5; k++) {
        sendGameStateToClients(1, winner);
        delay(20);
      }
    }

    playSfx(SFX_GAME_OVER);
    PostGameAction action = showPostGameResults(winner);
    if (action == POSTGAME_PLAY_AGAIN) {
      restartCurrentGame();
      return;
    }
    returnToMainMenuAndStart();
    return;
  }

  M5.update();
}

// ------------------------------------------------------------
// Client loop (multiplayer only)
// ------------------------------------------------------------
void drawClientConnectionLostOverlay() {
  M5.Display.fillRect(34, 82, scrX - 68, 76, TFT_BLACK);
  M5.Display.drawRect(34, 82, scrX - 68, 76, TFT_YELLOW);
  M5.Display.setTextDatum(MC_DATUM);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
  M5.Display.drawString("CONNECTION LOST", scrX / 2, 104);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.drawString("RECONNECTING...", scrX / 2, 136);
}

void loopClient() {
  processImuRecalibrationRequest();

  // IMU for local client
  if (M5.Imu.getAccel(&accX, &accY, &accZ)) {
    // ok
  }

  float calibratedX = applyImuDeadZone(accX - imuNeutralX);
  float calibratedY = applyImuDeadZone(accY - imuNeutralY);
  tx = tx * sm - calibratedX * (1.0f - sm);
  ty = ty * sm + calibratedY * (1.0f - sm);
  tx = constrain(tx, -1.0f, 1.0f);
  ty = constrain(ty, -1.0f, 1.0f);

  if (localPlayerId < MAX_PLAYERS) {
    pInputX[localPlayerId] = tx;
    pInputY[localPlayerId] = ty;
  }

  if (!firstStateReceived && !remoteGameOver && !waitingShown) {
    waitingShown = true;
    M5.Display.setTextSize(2);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.drawString("Waiting for host...", scrX / 2, scrY / 2);
  }

  // send input to host (throttled)
  static uint32_t lastSendMs = 0;
  uint32_t nowMs = millis();
  if (nowMs - lastSendMs >= NET_DT_MS) {
    lastSendMs = nowMs;
    InputPacket ip;
    ip.magic = 0xC1E17AA1;
    ip.playerId = localPlayerId;
    ip.tx = tx;
    ip.ty = ty;
    esp_now_send(peerMac, (uint8_t*)&ip, sizeof(ip));
  }

  static GameState localGS;
  static int32_t lastScoreSum = 0;
  static uint32_t vibStartMs = 0;

  if (haveNewState) {
    noInterrupts();
    localGS = gState;
    haveNewState = false;
    interrupts();

    firstStateReceived = true;
    lastStateMs = millis();
    waitingShown = false;
    clientLinkLost = false;

    if (localGS.recipientPlayerId < MAX_PLAYERS) {
      localPlayerId = localGS.recipientPlayerId;
    }

    if (!terrainReady) {
      rngNo = localGS.rngSeed;
      randomSeed(rngNo);
      generateTerrain();
      generateGradients();

      for (int x = 0; x < scrX; x++) {
        for (int y = 0; y < scrY; y++) {
          scene.drawPixel(x, y, bck.readPixel(x, y));
        }
      }
      scene.pushSprite(0, 0);
      terrainReady = true;
    }

    playerActiveMask = localGS.activeMask;
    for (int i = 0; i < MAX_PLAYERS; i++) {
      pX[i] = localGS.px[i];
      pY[i] = localGS.py[i];
      pVX[i] = localGS.vx[i];
      pVY[i] = localGS.vy[i];
      pInputX[i] = localGS.inputTx[i];
      pInputY[i] = localGS.inputTy[i];
      pScore[i] = localGS.score[i];
    }
    targX = localGS.targX;
    targY = localGS.targY;
    remoteTargetBonusPoints = localGS.targetBonusPoints;
    int32_t timeLeftMs = localGS.timeLeftMs;
    playerParticipantMask |= playerActiveMask;
    recordScoreHistory(false);

    int32_t scoreSumNow = sumScores();
    if (scoreSumNow > lastScoreSum) {
      lastScoreSum = scoreSumNow;
      vibStartMs = millis();
      playSfx(SFX_SCORE);
    }

    syncLocalLegacyFromPlayers();
    drawFrame(targX, targY, timeLeftMs);

    if (localGS.gameOver) {
      remoteGameOver = true;
      remoteWinner   = localGS.winner;
      playSfx(SFX_GAME_OVER);
      PostGameAction action = showPostGameResults(remoteWinner);
      if (action == POSTGAME_PLAY_AGAIN) {
        restartCurrentGame();
        return;
      }
      returnToMainMenuAndStart();
      return;
    }
  }

  if (!remoteGameOver && (millis() - vibStartMs < 120)) M5.Power.setVibration(210);
  else M5.Power.setVibration(0);

  if (firstStateReceived && !remoteGameOver &&
      (uint32_t)(millis() - lastStateMs) >= CONNECTION_TIMEOUT_MS) {
    if (!clientLinkLost) {
      clientLinkLost = true;
      // Input transmission continues above, allowing the host and client to
      // rejoin automatically as soon as packets can flow again.
      drawClientConnectionLostOverlay();
      Serial.println("Host connection lost; reconnecting...");
    }
  }

  M5.update();
}

// ------------------------------------------------------------

void loop() {
  processSerialConfig();

  if (!multiplayer) {
    // singleplayer on this device (host-like logic, no ESP-NOW)
    loopHostLike(false);
  } else {
    if (isHost) {
      // host in multiplayer
      loopHostLike(true);
    } else {
      // client in multiplayer
      loopClient();
    }
  }
}
