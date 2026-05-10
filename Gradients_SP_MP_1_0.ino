// --- Gradients 1.0 Multiplayer (M5Unified + M5GFX + ESP-NOW) ---
// Works on 320x240 devices (Core2, CoreS3, Core, Fire)
//  - Core2 / CoreS3: touchscreen LEFT/RIGHT for mode selection
//  - Core / Fire: buttons A/B for mode selection

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

// Scores
int score1 = 0;  // HOST
int score2 = 0;  // CLIENT

// Target
int targX;
int targY;
long lastCapt;

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

// ---- Color map block ----
int Ncols = 100;
uint16_t cols[100];
uint8_t reds[100] = {49,50,51,52,54,55,57,60,62,66,69,73,78,82,86,91,95,100,106,111,117,123,129,135,141,146,152,157,163,168,172,178,184,190,196,202,208,213,218,222,225,229,232,236,240,243,247,250,252,254,255,255,255,255,255,255,255,254,254,254,254,254,254,254,254,254,254,254,254,253,253,252,252,251,251,250,249,248,246,244,242,240,238,235,233,230,227,224,220,215,211,206,202,197,192,187,182,176,171,165};
uint8_t greens[100] = {54,61,68,74,81,87,94,100,106,112,118,123,129,135,141,147,153,158,164,169,174,179,184,188,193,198,202,207,211,214,218,221,225,228,231,233,236,238,240,242,244,245,247,248,250,251,252,253,254,255,254,251,249,246,243,240,236,233,229,226,222,217,213,208,203,198,192,187,181,176,170,164,158,151,144,137,130,123,117,110,104,98,92,85,79,72,66,60,54,48,44,39,33,28,23,17,11,6,2,0};
uint8_t blues[100]  = {149,152,155,159,162,165,168,171,174,177,180,183,186,189,192,195,198,201,204,207,210,212,215,217,220,223,225,227,230,232,234,236,238,240,242,245,246,248,248,248,248,246,243,238,233,227,221,213,205,196,189,184,179,174,170,165,160,155,150,146,141,136,131,126,121,116,111,106,102,98,95,91,88,85,82,79,76,73,71,68,65,62,59,56,53,50,47,44,42,39,37,36,35,34,34,35,35,36,37,38};

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
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

// peerMac must always be "the other device".
// These are fallback defaults only; NVS/Serial config can override them.
const uint8_t DEFAULT_PEER_IF_HOST[6]   = {0x84, 0x1F, 0xE8, 0x85, 0x30, 0x48}; // HOST talks to CLIENT
const uint8_t DEFAULT_PEER_IF_CLIENT[6] = {0x84, 0x1F, 0xE8, 0x85, 0x5F, 0x0C}; // CLIENT talks to HOST

bool cfgIsHostDevice = (DEFAULT_IS_HOST_DEVICE != 0);
uint8_t peerMac[6];
String serialLine;
bool configDirty = false;

// Throttle network updates a bit (ms)
const uint16_t NET_DT_MS = 25;  // ~40 Hz

// ESP-NOW structs
struct GameState {
  uint32_t magic = 0xDEADBEF1;
  uint32_t frame;
  float xs1, ys1;
  float xs2, ys2;
  float vx1, vy1;
  float vx2, vy2;
  int16_t targX, targY;
  int32_t score1, score2;
  int32_t timeLeftMs;
  uint32_t rngSeed;    // used to sync terrain
  uint8_t  gameOver;   // 0=no, 1=yes
  int8_t   winner;     // 0=tie, 1=host, 2=client
};

struct InputPacket {
  uint32_t magic = 0xC1E17AA1;
  float tx;
  float ty;
};

GameState  gState;
InputPacket lastInputFromClient;
volatile bool haveNewInput = false;
volatile bool haveNewState = false;

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

void loadRuntimeConfig() {
  Preferences cfg;
  cfg.begin("gradcfg", true);

  cfgIsHostDevice = cfg.getBool("isHost", DEFAULT_IS_HOST_DEVICE != 0);

  size_t n = cfg.getBytesLength("peer");
  if (n == 6) {
    cfg.getBytes("peer", peerMac, 6);
  } else {
    memcpy(peerMac, cfgIsHostDevice ? DEFAULT_PEER_IF_HOST : DEFAULT_PEER_IF_CLIENT, 6);
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

void printConfig() {
  Serial.println();
  Serial.println("--- Gradients config ---");
  Serial.print("Local STA MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Role: ");
  Serial.println(cfgIsHostDevice ? "HOST" : "CLIENT");
  Serial.print("Peer MAC: ");
  Serial.println(macToString(peerMac));
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
  Serial.println("Use CLIENT on the second device.");
  Serial.println("After pairing, use REBOOT before starting multiplayer.");
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
    Serial.print("Peer MAC: ");
    Serial.println(macToString(peerMac));
    return;
  }

  if (upper == "ROLE?") {
    Serial.print("Role: ");
    Serial.println(cfgIsHostDevice ? "HOST" : "CLIENT");
    return;
  }

  if (upper == "ROLE HOST") {
    cfgIsHostDevice = true;
    saveRoleToNvs(cfgIsHostDevice);
    configDirty = true;
    Serial.println("Role saved as HOST. Reboot or return to the menu before starting a game.");
    return;
  }

  if (upper == "ROLE CLIENT") {
    cfgIsHostDevice = false;
    saveRoleToNvs(cfgIsHostDevice);
    configDirty = true;
    Serial.println("Role saved as CLIENT. Reboot or return to the menu before starting a game.");
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
    configDirty = true;
    Serial.print("Peer MAC saved: ");
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
    configDirty = true;

    Serial.println("Pairing config saved.");
    Serial.print("Role: ");
    Serial.println(cfgIsHostDevice ? "HOST" : "CLIENT");
    Serial.print("Peer MAC: ");
    Serial.println(macToString(peerMac));
    Serial.println("Use REBOOT before starting multiplayer.");
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

// ------------------------------------------------------------
// ESP-NOW callbacks (IDF v5 style recv signature)
// ------------------------------------------------------------
void onDataRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
  (void)info; // unused

  if (!multiplayer) return;  // ignore if in singleplayer

  if (isHost) {
    // HOST: receive InputPacket from client
    if (len == sizeof(InputPacket)) {
      memcpy((void*)&lastInputFromClient, incomingData, sizeof(InputPacket));
      haveNewInput = true;
    }
  } else {
    // CLIENT: receive GameState from host
    if (len == sizeof(GameState)) {
      memcpy((void*)&gState, incomingData, sizeof(GameState));
      haveNewState = true;
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

void initEspNow() {
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    // if this fails, you might be out of NVS / WiFi issues
  }
  esp_now_register_recv_cb(onDataRecv);
  esp_now_register_send_cb(onDataSent);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, peerMac, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
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
// Drawing helpers
// ------------------------------------------------------------
void drawFrame(float dXs1, float dYs1,
               float dXs2, float dYs2,
               int dTargX, int dTargY,
               int dScore1, int dScore2,
               int32_t timeLeftMs) {
  // cleanup previous locations
  cleanUp(xp1, yp1);
  if (multiplayer) {
    cleanUp(xp2, yp2);
  }
  cleanUp(prevTargX, prevTargY);

  // Player 1 vector triangle (using current tx/ty)
  scene.fillTriangle(int(dXs1 + tx * (ballR + vectL)), int(dYs1 + ty * (ballR + vectL)),
                     int(dXs1 + ty * ballR),           int(dYs1 - tx * ballR),
                     int(dXs1 - ty * ballR),           int(dYs1 + tx * ballR),
                     TFT_BLACK);

  // HOST (P1): red filled + red ring
  scene.fillEllipse(dXs1, dYs1, ballR - 1, ballR - 1, TFT_RED);
  scene.drawEllipse(dXs1, dYs1, ballR + 1, ballR + 1, TFT_RED);

  // CLIENT (P2): blue filled + blue ring (only in multiplayer)
  if (multiplayer) {
    scene.fillEllipse(dXs2, dYs2, ballR - 1, ballR - 1, TFT_BLUE);
    scene.drawEllipse(dXs2, dYs2, ballR + 1, ballR + 1, TFT_BLUE);
  }

  // target – chased circle = GREEN
  scene.fillEllipse(dTargX, dTargY, 6, 6, TFT_GREEN);
  scene.drawEllipse(dTargX, dTargY, 6, 6, TFT_BLACK);

  // HUD
  scene.fillRect(0, 0, scrX, topBar, TFT_BLACK);
  scene.setTextSize(2);

  // HOST score (left) – red, with "H:"
  scene.setTextDatum(TL_DATUM);
  scene.setTextColor(TFT_RED, TFT_TRANSPARENT);
  scene.drawString("H:", 2, 1);
  scene.drawNumber(dScore1, 28, 1);

  // CLIENT score (a bit spaced to the right) – blue, with "C:"
  if (multiplayer) {
    scene.setTextDatum(TL_DATUM);
    scene.setTextColor(TFT_BLUE, TFT_TRANSPARENT);
    scene.drawString("C:", 90, 1);
    scene.drawNumber(dScore2, 116, 1);
  }

  // timer (right) – white
  float t  = timeLeftMs / 1000.0f;
  scene.setTextDatum(TR_DATUM);
  scene.setTextColor(TFT_WHITE, TFT_TRANSPARENT);
  scene.drawFloat(t, 1, scrX, 1);

  // highscore (center) – only in singleplayer & host, magenta
  if (!multiplayer && isHost) {
    scene.setTextDatum(TC_DATUM);
    scene.setTextColor(TFT_MAGENTA, TFT_TRANSPARENT);
    scene.drawNumber(HS, scrX / 2, 1);
  }

  scene.setTextDatum(TL_DATUM);
  scene.pushSprite(0, 0);

  // store for next cleanup
  xp1 = int(dXs1);
  yp1 = int(dYs1);
  if (multiplayer) {
    xp2 = int(dXs2);
    yp2 = int(dYs2);
  }
  prevTargX = dTargX;
  prevTargY = dTargY;
}

void drawGameOverOverlay(int8_t winner, bool localIsHost) {
  M5.Display.setTextSize(2);
  M5.Display.setTextDatum(MC_DATUM);

  int cx = scrX / 2;
  int cy = scrY / 2;

  // GAME OVER title
  M5.Display.setTextColor(TFT_RED, TFT_BLACK);
  M5.Display.drawString("GAME OVER", cx, cy - 20);

  // Winner / loser message
  const char* msg;
  if (winner == 0) {
    msg = "It's a tie!";
  } else {
    bool localWon = (localIsHost && winner == 1) || (!localIsHost && winner == 2);
    if (localWon) {
      msg = "Congratulations, you win!";
    } else {
      msg = "Better luck next time :)";
    }
  }
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.drawString(msg, cx, cy + 5);

  // Restart hint
  M5.Display.drawString("Hold A to restart", cx, cy + 30);
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
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(2);

  M5.Display.setCursor(10, 10);
  M5.Display.println("Gradients 1.0");
  M5.Display.setCursor(10, 30);
  M5.Display.println("V.Divic 2025");

  if (touch) {
    M5.Display.setCursor(10, 60);
    if (cfgIsHostDevice) M5.Display.println("Touch LEFT: Singleplayer");
    else                 M5.Display.println("Touch LEFT: disabled");

    M5.Display.setCursor(10, 80);
    M5.Display.println("Touch RIGHT: Multiplayer");
  } else {
    M5.Display.setCursor(10, 60);
    if (cfgIsHostDevice) M5.Display.println("A: Singleplayer");
    else                 M5.Display.println("A: disabled");

    M5.Display.setCursor(10, 80);
    M5.Display.println("B: Multiplayer");
  }

  M5.Display.setCursor(10, 102);
  M5.Display.printf("Role: %s", cfgIsHostDevice ? "HOST" : "CLIENT");

  M5.Display.setTextSize(1);
  M5.Display.setCursor(10, 126);
  M5.Display.printf("Local MAC: %s", WiFi.macAddress().c_str());
  M5.Display.setCursor(10, 140);
  M5.Display.printf("Peer  MAC: %s", macToString(peerMac).c_str());
  M5.Display.setCursor(10, 158);
  M5.Display.println("Serial: PAIR HOST/CLIENT <other MAC>");
  M5.Display.setCursor(10, 171);
  M5.Display.println("Serial: MAC?  CFG?  PAIR?  REBOOT");

  M5.Display.setTextSize(2);
}

// ------------------------------------------------------------

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  Serial.begin(115200);
  delay(100);

  WiFi.mode(WIFI_STA);       // needed to know local STA MAC and improves RNG entropy later
  loadRuntimeConfig();
  printConfig();

  M5.Display.setRotation(1);
  M5.Display.setBrightness(200);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(2);

  // Intro screen text
  scr_ = random(100) / 100.0;
  scg  = random(100) / 100.0;
  scb  = random(100) / 100.0;
  ncr  = random(100) / 100.0;
  ncg  = random(100) / 100.0;
  ncb  = random(100) / 100.0;
  nxIntro = 1 + ((float)random(900))/100.0;
  nyIntro = 1 + ((float)random(900))/100.0;

  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setCursor(10, 10);
  M5.Display.println("Gradients 1.0");
  M5.Display.setCursor(10, 30);
  M5.Display.println("V.Divic 2025");

  bool touch = hasTouchscreen();
  drawModeMenu(touch);

  // Mode selection with full-screen intro drawing
  while (true) {
    float radius = (min(scrX, scrY) / 2.0f) - 8.0f;
    for (float t = 0; t < 4 * PI; t += 0.03f) {
      M5.update();

      processSerialConfig();
      if (configDirty) {
        drawModeMenu(touch);
        configDirty = false;
      }

      if (touch) {
        // --- Touch-based selection (Core2 / CoreS3) ---
        auto td = M5.Touch.getDetail();
        if (td.wasPressed()) {
          if (td.x < scrX / 2) {
            // LEFT = singleplayer, host role only
            if (cfgIsHostDevice) {
              multiplayer = false;
              isHost = true;
              goto mode_selected;
            }
          } else {
            // RIGHT = multiplayer, runtime role decides host/client
            multiplayer = true;
            isHost = cfgIsHostDevice;
            goto mode_selected;
          }
        }
      } else {
        // --- Button-based selection (Core / Fire) ---
        if (cfgIsHostDevice && M5.BtnA.wasPressed()) {
          multiplayer = false;
          isHost = true;
          goto mode_selected;
        }
        if (M5.BtnB.wasPressed()) {
          multiplayer = true;
          isHost = cfgIsHostDevice;
          goto mode_selected;
        }
      }

      // Intro drawing
      kIntro++;
      float ccr = scr_ * (float)kIntro / ncolor + ncr * (1.0f - (float)kIntro / ncolor);
      float ccg = scg  * (float)kIntro / ncolor + ncg * (1.0f - (float)kIntro / ncolor);
      float ccb = scb  * (float)kIntro / ncolor + ncb * (1.0f - (float)kIntro / ncolor);

      if (kIntro == ncolor) {
        scr_ = ncr;  scg = ncg;  scb = ncb;
        ncr = random(100) / 100.0;
        ncg = random(100) / 100.0;
        ncb = random(100) / 100.0;
        kIntro = 0;
      }

      int indX = lroundf(xmIntro + cosf(nxIntro * t) * sinf(t) * radius);
      int indY = lroundf(ymIntro + sinf(nyIntro * t) * sinf(t) * radius);

      uint16_t color = rgb565(ccr * 255, ccg * 255, ccb * 255);
      if (indX >= 0 && indX < scrX && indY >= 0 && indY < scrY) {
        M5.Display.drawPixel(indX, indY, color);
      }
      delay(5);
    }
  }

mode_selected:

  Serial.print("Starting mode: " );
  Serial.print(multiplayer ? "MULTIPLAYER " : "SINGLEPLAYER " );
  Serial.println(isHost ? "HOST" : "CLIENT");

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
void loopHostLike(bool asMultiplayer) {
  // IMU
  if (M5.Imu.getAccel(&accX, &accY, &accZ)) {
    // ok
  }

  // smoothing like original
  tx = tx * sm -  accX * (1.0f - sm);
  ty = ty * sm +  accY * (1.0f - sm);
  tx = constrain(tx, -1.0f, 1.0f);
  ty = constrain(ty, -1.0f, 1.0f);

  // dt: positive, scaled similar to original
  uint32_t now = millis();
  int32_t dt_ms = (int32_t)(now - lastR);
  if (dt_ms < 1)   dt_ms = 1;
  if (dt_ms > 40)  dt_ms = 40; // clamp for stability
  lastR = now;
  float dt = (float)dt_ms * 10.0f; // scale like original

  float tx2 = 0.0f;
  float ty2 = 0.0f;

  if (asMultiplayer) {
    if (haveNewInput) {
      haveNewInput = false;
      tx2 = lastInputFromClient.tx;
      ty2 = lastInputFromClient.ty;
    }
  }

  // simulate player 1
  simulatePlayer(xs1, ys1, xv1, yv1, tx,  ty,  dt);

  // simulate player 2 only in multiplayer
  if (asMultiplayer) {
    simulatePlayer(xs2, ys2, xv2, yv2, tx2, ty2, dt);
  } else {
    // park P2 off-screen in singleplayer
    xs2 = -1000;
    ys2 = -1000;
  }

  // bumping in multiplayer
  if (asMultiplayer) {
    float dx = xs1 - xs2;
    float dy = ys1 - ys2;
    float dist2 = dx*dx + dy*dy;
    float minDist = 2 * ballR;
    if (dist2 < (minDist * minDist)) {
      float tmpx = xv1; xv1 = xv2; xv2 = tmpx;
      float tmpy = yv1; yv1 = yv2; yv2 = tmpy;
    }
  }

  // capture logic
  bool p1Got = (fabsf(xs1 - targX) < precision) && (fabsf(ys1 - targY) < precision);
  bool p2Got = false;
  if (asMultiplayer) {
    p2Got = (fabsf(xs2 - targX) < precision) && (fabsf(ys2 - targY) < precision);
  }

  if (p1Got || p2Got) {
    int s = 1000 - (millis() - lastCapt) / 10;
    if (s < 100) s = 100;

    if (p1Got && !p2Got) score1 += s;
    if (p2Got && !p1Got && asMultiplayer) score2 += s;
    if (p1Got && p2Got && asMultiplayer) {
      score1 += s;
      score2 += s;
    }

    targX = random(ballR, scrX - ballR);
    targY = random(topBar + ballR, scrY - ballR);
    gameEndTimeMs += 2000;
    lastCapt = millis();
  }

  int32_t timeLeftMs = (int32_t)(gameEndTimeMs - millis());
  if (timeLeftMs < 0) timeLeftMs = 0;

  // send to client in multiplayer (including rngSeed), throttled
  static uint32_t lastSendMs = 0;
  if (asMultiplayer) {
    if (now - lastSendMs >= NET_DT_MS) {
      lastSendMs = now;
      gState.frame++;
      gState.xs1 = xs1; gState.ys1 = ys1;
      gState.xs2 = xs2; gState.ys2 = ys2;
      gState.vx1 = xv1; gState.vy1 = yv1;
      gState.vx2 = xv2; gState.vy2 = yv2;
      gState.targX = targX;
      gState.targY = targY;
      gState.score1 = score1;
      gState.score2 = score2;
      gState.timeLeftMs = timeLeftMs;
      gState.rngSeed = rngNo;
      gState.gameOver = 0;
      gState.winner   = 0;

      esp_now_send(peerMac, (uint8_t*)&gState, sizeof(gState));
    }
  }

  // draw normal frame
  drawFrame(xs1, ys1, xs2, ys2, targX, targY, score1, score2, timeLeftMs);

  // vibro when just captured
  if ((millis() - lastCapt) < 120) {
    M5.Power.setVibration(210);
  } else {
    M5.Power.setVibration(0);
  }

  // game over
  if (millis() > gameEndTimeMs) {
    int8_t winner = 0;

    if (!asMultiplayer) {
      // singleplayer: host is the only player
      winner = 1;
    } else {
      if      (score1 > score2) winner = 1;
      else if (score2 > score1) winner = 2;
      else                      winner = 0;
    }

    // singleplayer HS logic (host only)
    if (!multiplayer && isHost) {
      int totalScore = score1; // only HOST score counts for HS
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

    // send final game-over state to client
    if (asMultiplayer) {
      gState.frame++;
      gState.xs1 = xs1; gState.ys1 = ys1;
      gState.xs2 = xs2; gState.ys2 = ys2;
      gState.vx1 = xv1; gState.vy1 = yv1;
      gState.vx2 = xv2; gState.vy2 = yv2;
      gState.targX = targX;
      gState.targY = targY;
      gState.score1 = score1;
      gState.score2 = score2;
      gState.timeLeftMs = 0;
      gState.rngSeed = rngNo;
      gState.gameOver = 1;
      gState.winner   = winner;
      esp_now_send(peerMac, (uint8_t*)&gState, sizeof(gState));
    }

    // draw GAME OVER overlay on host
    drawGameOverOverlay(winner, true);

    // allow restart from button instead of power cycle
    while (true) {
      M5.update();
      if (M5.BtnA.pressedFor(800)) {
        esp_restart();
      }
      delay(50);
    }
  }

  M5.update();
}

// ------------------------------------------------------------
// Client loop (multiplayer only)
// ------------------------------------------------------------
void loopClient() {
  // IMU for player 2 (local tilt)
  if (M5.Imu.getAccel(&accX, &accY, &accZ)) {
    // ok
  }

  tx = tx * sm -  accX * (1.0f - sm);
  ty = ty * sm +  accY * (1.0f - sm);
  tx = constrain(tx, -1.0f, 1.0f);
  ty = constrain(ty, -1.0f, 1.0f);

  // Waiting screen until we see the first state (draw once)
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
    ip.tx = tx;
    ip.ty = ty;
    ip.magic = 0xC1E17AA1;
    esp_now_send(peerMac, (uint8_t*)&ip, sizeof(ip));
  }

  static GameState localGS;
  static int lastScoreSum = 0;
  static uint32_t vibStartMs = 0;

  // if we have new state from host, process it
  if (haveNewState) {
    noInterrupts();
    localGS = gState;
    haveNewState = false;
    interrupts();

    firstStateReceived = true;
    lastStateMs = millis();

    // First time: sync terrain using rngSeed from host
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

    xs1 = localGS.xs1;
    ys1 = localGS.ys1;
    xs2 = localGS.xs2;
    ys2 = localGS.ys2;
    targX = localGS.targX;
    targY = localGS.targY;
    score1 = localGS.score1;
    score2 = localGS.score2;
    int32_t timeLeftMs = localGS.timeLeftMs;

    // detect capture (host or client) by score increase
    int scoreSum = score1 + score2;
    if (scoreSum > lastScoreSum) {
      lastScoreSum = scoreSum;
      vibStartMs = millis();
    }

    // draw normal frame
    drawFrame(xs1, ys1, xs2, ys2, targX, targY, score1, score2, timeLeftMs);

    // handle game over flag from host
    if (localGS.gameOver) {
      remoteGameOver = true;
      remoteWinner   = localGS.winner;
      drawGameOverOverlay(remoteWinner, false);
    }
  }

  // vibrate on client for a short time after capture (only if not already game-over)
  if (!remoteGameOver && (millis() - vibStartMs < 120)) {
    M5.Power.setVibration(210);
  } else {
    M5.Power.setVibration(0);
  }

  // allow restart from client side as well once game over
  if (remoteGameOver) {
    if (M5.BtnA.pressedFor(800)) {
      esp_restart();
    }
  }

  // If we've already received at least one state, but no new state
  // came in for a while, keep re-drawing the last known one.
  if (firstStateReceived && !remoteGameOver) {
    if (millis() - lastStateMs > 150) {   // ~6 FPS safety refresh
      drawFrame(xs1, ys1, xs2, ys2, targX, targY, score1, score2, 0);
      lastStateMs = millis();
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
