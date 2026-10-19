// Pocket Ecosystem - artificial life on the Waveshare ESP32-S3-Touch-LCD-1.28
// Library: LovyanGFX (install from Arduino Library Manager)
//
// Controls:
//   Tap               -> drop food
//   Hold ~1s, release -> introduce predators
//   Hold >2.5s        -> restart with a new random seed
//   Swipe up / down   -> raise / lower temperature (metabolism cost)
//   Swipe right / left-> raise / lower mutation rate

#include <Arduino.h>
#include <Wire.h>
#include <LovyanGFX.hpp>

// ---------- Tuning knobs (experiment here, one at a time!) ----------
const int   FOOD_PER_TICK   = 6;      // food patches that regrow each tick
const int   FOOD_AMOUNT     = 25;     // energy in one regrown food patch
const float BASE_COST       = 0.04f;  // energy burned per tick just to stay alive
const float SPEED_COST      = 0.06f;  // extra cost per tick at maximum speed
const float SENSE_COST      = 0.008f; // extra cost per tick per cell of vision
const float HAZARD_DRAIN    = 0.6f;   // extra cost per tick while standing in a hazard
const float PRED_COST_MULT  = 1.6f;   // predators burn energy this much faster
const int   CATCH_CHANCE    = 35;     // % chance a predator's attack succeeds
const float PRED_GAIN       = 0.4f;   // fraction of the prey's energy a predator gains
const int   FLEE_BOOST      = 80;     // extra move chance (0-255) when fleeing
const int   PREDATORS_ADDED = 3;      // predators added per long-press

// ---------- Board pins ----------
#define LCD_SCLK 10
#define LCD_MOSI 11
#define LCD_CS    9
#define LCD_DC    8
#define LCD_RST  14
#define LCD_BL    2
#define TP_SDA    6
#define TP_SCL    7
#define TP_RST   13
#define TP_ADDR  0x15   // CST816S

// ---------- Display driver config ----------
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;
public:
  LGFX() {
    { auto c = _bus.config();
      c.spi_host = SPI2_HOST; c.spi_mode = 0;
      c.freq_write = 40000000; c.freq_read = 16000000;
      c.spi_3wire = false; c.use_lock = true; c.dma_channel = SPI_DMA_CH_AUTO;
      c.pin_sclk = LCD_SCLK; c.pin_mosi = LCD_MOSI; c.pin_miso = -1; c.pin_dc = LCD_DC;
      _bus.config(c); _panel.setBus(&_bus); }
    { auto c = _panel.config();
      c.pin_cs = LCD_CS; c.pin_rst = LCD_RST; c.pin_busy = -1;
      c.panel_width = 240; c.panel_height = 240;
      c.memory_width = 240; c.memory_height = 240;
      c.offset_x = 0; c.offset_y = 0; c.offset_rotation = 0;
      c.readable = false; c.invert = true; c.rgb_order = false;
      c.dlen_16bit = false; c.bus_shared = false;
      _panel.config(c); }
    { auto c = _light.config();
      c.pin_bl = LCD_BL; c.invert = false; c.freq = 44100; c.pwm_channel = 7;
      _light.config(c); _panel.setLight(&_light); }
    setPanel(&_panel);
  }
};

LGFX tft;
LGFX_Sprite canvas(&tft);

// ---------- World ----------
const int G = 40;          // 40x40 grid
const int CELL = 6;        // 6px per cell -> 240px
const int MAXORG = 300;
const int HIST = 100;

uint8_t food[G][G];
uint8_t hazard[G][G];
int16_t occ[G][G];         // organism index or -1
bool inside[G][G];         // inside the round screen

struct Org {
  bool alive, predator;
  uint8_t x, y;
  int8_t hx, hy;           // current heading
  float energy;
  // genes
  uint8_t speed;           // chance (0-255) to move each tick
  uint8_t sense;           // vision radius in cells (1-7)
  uint8_t repro;           // energy needed to reproduce
  uint8_t caution;         // chance (0-255) to refuse entering a hazard
};
Org orgs[MAXORG];

// Adjustable rules (changed by swipes)
float temperature = 1.0f;
float mutationRate = 0.10f;

uint32_t ticks = 0;
uint16_t herbHist[HIST], predHist[HIST];
int nHerb = 0, nPred = 0;
char msg[32] = "";
uint32_t msgUntil = 0;

// ---------- Helpers ----------
inline bool valid(int x, int y) { return x >= 0 && y >= 0 && x < G && y < G && inside[x][y]; }
inline int sgn(int v) { return (v > 0) - (v < 0); }
float frand() { return random(100000) / 100000.0f; }

uint8_t mutate(uint8_t v, int lo, int hi, int step) {
  if (frand() < mutationRate) v = constrain((int)v + (int)random(-step, step + 1), lo, hi);
  return v;
}

int spawn(int x, int y, bool pred, uint8_t sp, uint8_t se, uint8_t re, uint8_t ca, float e) {
  if (!valid(x, y) || occ[x][y] >= 0) return -1;
  for (int i = 0; i < MAXORG; i++) {
    if (orgs[i].alive) continue;
    Org &o = orgs[i];
    o.alive = true; o.predator = pred; o.x = x; o.y = y;
    o.hx = random(-1, 2); o.hy = random(-1, 2);
    o.energy = e; o.speed = sp; o.sense = se; o.repro = re; o.caution = ca;
    occ[x][y] = i;
    return i;
  }
  return -1;
}

void kill(int i, bool leaveFood) {
  Org &o = orgs[i];
  o.alive = false;
  occ[o.x][o.y] = -1;
  if (leaveFood) food[o.x][o.y] = min(255, food[o.x][o.y] + 15);  // corpses feed the soil
}

void showMsg(const char *m) { strncpy(msg, m, sizeof(msg) - 1); msgUntil = millis() + 1500; }

// ---------- Setup world ----------
void resetWorld(uint32_t seed) {
  randomSeed(seed);
  memset(food, 0, sizeof(food));
  memset(hazard, 0, sizeof(hazard));

  // Round world mask, computed from G so it works at any grid size
  float c = (G - 1) / 2.0f, r = G / 2.0f - 1.0f;
  for (int x = 0; x < G; x++)
    for (int y = 0; y < G; y++) {
      occ[x][y] = -1;
      float dx = x - c, dy = y - c;
      inside[x][y] = dx * dx + dy * dy < r * r;
    }
  for (int i = 0; i < MAXORG; i++) orgs[i].alive = false;

  // A few hazard zones: dangerous, but food grows faster there
  for (int h = 0; h < 3; h++) {
    int cx = random(G / 4, 3 * G / 4), cy = random(G / 4, 3 * G / 4), hr = random(2, 4);
    for (int x = 0; x < G; x++)
      for (int y = 0; y < G; y++)
        if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= hr * hr && valid(x, y)) hazard[x][y] = 1;
  }
  for (int k = 0; k < 300; k++) { int x = random(G), y = random(G); if (valid(x, y)) food[x][y] = FOOD_AMOUNT; }

  int placed = 0;
  for (int tries = 0; placed < 40 && tries < 2000; tries++)
    if (spawn(random(G), random(G), false, random(60, 200), random(1, 5), random(40, 90), random(50, 200), 40) >= 0)
      placed++;

  memset(herbHist, 0, sizeof(herbHist));
  memset(predHist, 0, sizeof(predHist));
  ticks = 0;
}

// ---------- Simulation step ----------
void step() {
  // Food regrowth (richer inside hazards)
  for (int k = 0; k < FOOD_PER_TICK; k++) {
    int x = random(G), y = random(G);
    if (valid(x, y) && food[x][y] < 200) food[x][y] += hazard[x][y] ? FOOD_AMOUNT * 2 : FOOD_AMOUNT;
  }

  for (int i = 0; i < MAXORG; i++) {
    Org &o = orgs[i];
    if (!o.alive) continue;

    // Metabolism: faster and sharper-eyed organisms burn more energy
    float cost = (BASE_COST + o.speed / 255.0f * SPEED_COST + o.sense * SENSE_COST) * temperature;
    if (o.predator) cost *= PRED_COST_MULT;
    o.energy -= cost;
    if (hazard[o.x][o.y]) o.energy -= HAZARD_DRAIN;
    if (o.energy <= 0) { kill(i, true); continue; }

    // Look around: nearest target (food or prey), and for herbivores, nearest predator
    int tx = 0, ty = 0, bestT = 9999;
    int px = 0, py = 0, bestP = 9999;
    int s = o.sense;
    for (int dy = -s; dy <= s; dy++)
      for (int dx = -s; dx <= s; dx++) {
        if (!dx && !dy) continue;
        int x = o.x + dx, y = o.y + dy;
        if (!valid(x, y)) continue;
        int d = dx * dx + dy * dy;
        int j = occ[x][y];
        if (o.predator) {
          if (j >= 0 && !orgs[j].predator && d < bestT) { bestT = d; tx = dx; ty = dy; }
        } else {
          if (j >= 0 && orgs[j].predator && d < bestP) { bestP = d; px = dx; py = dy; }
          if (food[x][y] && d < bestT) { bestT = d; tx = dx; ty = dy; }
        }
      }

    // Fleeing herbivores get a burst of speed
    bool fleeing = bestP < 9999;
    int moveChance = fleeing ? min(255, o.speed + FLEE_BOOST) : o.speed;

    if ((int)random(256) < moveChance) {
      int sx, sy;
      if (fleeing) { sx = -sgn(px); sy = -sgn(py); }                  // run away
      else if (bestT < 9999) { sx = sgn(tx); sy = sgn(ty); }          // go to target
      else {                                                          // wander
        if (random(100) < 20 || (o.hx == 0 && o.hy == 0)) { o.hx = random(-1, 2); o.hy = random(-1, 2); }
        sx = o.hx; sy = o.hy;
      }

      int nx = o.x + sx, ny = o.y + sy;
      bool blocked = (sx == 0 && sy == 0) || !valid(nx, ny);
      if (!blocked && hazard[nx][ny] && !hazard[o.x][o.y] && (int)random(256) < o.caution) blocked = true;
      if (!blocked && occ[nx][ny] >= 0) {
        int j = occ[nx][ny];
        if (o.predator && !orgs[j].predator) {
          if ((int)random(100) < CATCH_CHANCE) { o.energy += orgs[j].energy * PRED_GAIN + 5; kill(j, false); }
          else { o.energy -= 1.0f; blocked = true; }                  // missed: wasted effort
        } else blocked = true;
      }
      if (blocked) { o.hx = random(-1, 2); o.hy = random(-1, 2); }
      else { occ[o.x][o.y] = -1; o.x = nx; o.y = ny; occ[nx][ny] = i; o.hx = sx; o.hy = sy; }
    }

    // Eat
    if (!o.predator && food[o.x][o.y]) { o.energy += food[o.x][o.y]; food[o.x][o.y] = 0; }

    // Reproduce with mutation
    if (o.energy > o.repro) {
      for (int t = 0; t < 8; t++) {
        int cx = o.x + random(-1, 2), cy = o.y + random(-1, 2);
        if (valid(cx, cy) && occ[cx][cy] < 0) {
          int c = spawn(cx, cy, o.predator,
                        mutate(o.speed, 10, 255, 20), mutate(o.sense, 1, 7, 1),
                        mutate(o.repro, 25, 220, 10), mutate(o.caution, 0, 255, 25),
                        o.energy / 2);
          if (c >= 0) o.energy /= 2;
          break;
        }
      }
    }
  }
  ticks++;
}

// ---------- Stats ----------
void recordStats() {
  nHerb = nPred = 0;
  long sSpeed = 0, sSense = 0, sCaution = 0;
  for (int i = 0; i < MAXORG; i++) {
    if (!orgs[i].alive) continue;
    if (orgs[i].predator) nPred++;
    else { nHerb++; sSpeed += orgs[i].speed; sSense += orgs[i].sense; sCaution += orgs[i].caution; }
  }
  memmove(herbHist, herbHist + 1, (HIST - 1) * sizeof(uint16_t));
  memmove(predHist, predHist + 1, (HIST - 1) * sizeof(uint16_t));
  herbHist[HIST - 1] = nHerb;
  predHist[HIST - 1] = nPred;

  // Open Tools > Serial Plotter to watch evolution over time
  int d = max(nHerb, 1);
  Serial.printf("herbivores:%d,predators:%d,avgSpeed:%ld,avgSenseX20:%ld,avgCaution:%ld\n",
                nHerb, nPred, sSpeed / d, sSense * 20 / d, sCaution / d);
}

// ---------- Rendering ----------
void render(bool touching, int tx, int ty, uint32_t held) {
  canvas.fillScreen(TFT_BLACK);
  canvas.fillCircle(120, 120, 118, lgfx::color565(8, 8, 16));

  for (int x = 0; x < G; x++)
    for (int y = 0; y < G; y++) {
      if (!inside[x][y]) continue;
      uint16_t c = 0;
      if (hazard[x][y]) c = food[x][y] ? lgfx::color565(160, 90, 0) : lgfx::color565(70, 0, 40);
      else if (food[x][y]) c = lgfx::color565(0, min(60 + food[x][y] / 2, 200), 0);
      if (c) canvas.fillRect(x * CELL, y * CELL, CELL, CELL, c);
    }

  for (int i = 0; i < MAXORG; i++) {
    Org &o = orgs[i];
    if (!o.alive) continue;
    int px = o.x * CELL, py = o.y * CELL;
    if (o.predator) {
      canvas.fillCircle(px + CELL / 2, py + CELL / 2, CELL / 2 + 1, lgfx::color565(255, 40, 40));
      canvas.fillRect(px + CELL / 2 - 1, py + CELL / 2 - 1, 2, 2, TFT_WHITE);
    } else {
      // Color reflects genes: red = speed, green = vision, blue = caution
      canvas.fillRoundRect(px, py, CELL, CELL, 2, lgfx::color565(o.speed, 90 + o.sense * 23, o.caution));
    }
  }

  // HUD
  char buf[32];
  canvas.setTextColor(TFT_WHITE);
  snprintf(buf, sizeof(buf), "H:%d  P:%d", nHerb, nPred);
  canvas.drawCenterString(buf, 120, 16);
  snprintf(buf, sizeof(buf), "T:%.2f M:%.2f", temperature, mutationRate);
  canvas.drawCenterString(buf, 120, 27);

  // Population graph
  int maxv = 10;
  for (int i = 0; i < HIST; i++) maxv = max(maxv, (int)max(herbHist[i], predHist[i]));
  for (int i = 1; i < HIST; i++) {
    int x = 70 + i;
    canvas.drawLine(x - 1, 215 - herbHist[i - 1] * 30 / maxv, x, 215 - herbHist[i] * 30 / maxv, TFT_GREEN);
    canvas.drawLine(x - 1, 215 - predHist[i - 1] * 30 / maxv, x, 215 - predHist[i] * 30 / maxv, TFT_RED);
  }

  // Hold indicator
  if (touching) {
    uint16_t c = held > 2500 ? TFT_RED : (held > 600 ? TFT_ORANGE : TFT_WHITE);
    canvas.drawCircle(tx, ty, min((int)(held / 60) + 4, 45), c);
  }

  if (millis() < msgUntil) {
    canvas.fillRoundRect(40, 105, 160, 26, 6, TFT_BLACK);
    canvas.setTextSize(2);
    canvas.drawCenterString(msg, 120, 110);
    canvas.setTextSize(1);
  }

  canvas.pushSprite(0, 0);
}

// ---------- Touch ----------
bool readTouch(int &x, int &y) {
  Wire.beginTransmission(TP_ADDR);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)TP_ADDR, (uint8_t)5) != 5) return false;
  uint8_t n = Wire.read(), xh = Wire.read(), xl = Wire.read(), yh = Wire.read(), yl = Wire.read();
  if (n == 0) return false;
  x = ((xh & 0x0F) << 8) | xl;
  y = ((yh & 0x0F) << 8) | yl;
  return true;
}

void handleGesture(int x0, int y0, int x1, int y1, uint32_t dur) {
  int dx = x1 - x0, dy = y1 - y0;
  char b[32];
  if (abs(dx) > 40 || abs(dy) > 40) {
    if (abs(dy) > abs(dx)) {
      temperature = constrain(temperature + (dy < 0 ? 0.25f : -0.25f), 0.25f, 3.0f);
      snprintf(b, sizeof(b), "Temp %.2f", temperature);
    } else {
      mutationRate = constrain(mutationRate + (dx > 0 ? 0.05f : -0.05f), 0.0f, 0.5f);
      snprintf(b, sizeof(b), "Mutate %.2f", mutationRate);
    }
    showMsg(b);
  } else if (dur < 600) {
    int cx = x0 / CELL, cy = y0 / CELL;
    for (int x = cx - 2; x <= cx + 2; x++)
      for (int y = cy - 2; y <= cy + 2; y++)
        if (valid(x, y) && (x - cx) * (x - cx) + (y - cy) * (y - cy) <= 4) food[x][y] = FOOD_AMOUNT * 2;
  } else if (dur < 2500) {
    int cx = x0 / CELL, cy = y0 / CELL;
    for (int k = 0; k < PREDATORS_ADDED; k++)
      spawn(cx + random(-2, 3), cy + random(-2, 3), true, 140, 4, 120, 200, 50);
    showMsg("Predators!");
  } else {
    resetWorld(esp_random());
    showMsg("New world");
  }
}

// ---------- Main ----------
bool touchDown = false;
uint32_t touchStart = 0;
int tx0, ty0, txl, tyl;

void setup() {
  Serial.begin(115200);

  pinMode(TP_RST, OUTPUT);
  digitalWrite(TP_RST, LOW);  delay(10);
  digitalWrite(TP_RST, HIGH); delay(50);
  Wire.begin(TP_SDA, TP_SCL, 400000);
  Wire.beginTransmission(TP_ADDR);  // disable touch auto-sleep
  Wire.write(0xFE); Wire.write(0x01);
  Wire.endTransmission();

  tft.init();
  tft.setBrightness(200);
  canvas.setColorDepth(16);
  canvas.setPsram(true);
  if (!canvas.createSprite(240, 240)) { canvas.setPsram(false); canvas.createSprite(240, 240); }

  resetWorld(esp_random());
}

void loop() {
  int x, y;
  if (readTouch(x, y)) {
    if (!touchDown) { touchDown = true; touchStart = millis(); tx0 = x; ty0 = y; }
    txl = x; tyl = y;
  } else if (touchDown) {
    touchDown = false;
    handleGesture(tx0, ty0, txl, tyl, millis() - touchStart);
  }

  step();
  if (ticks % 10 == 0) recordStats();
  render(touchDown, txl, tyl, touchDown ? millis() - touchStart : 0);
}
