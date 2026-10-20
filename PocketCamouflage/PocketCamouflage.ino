// Pocket Camouflage - Game 3 of ESP32-Darwinism
// Board: Waveshare ESP32-S3-Touch-LCD-1.28    Library: LovyanGFX
//
// Predators hunt by sight. Creatures whose colours blend into the
// background are harder to spot, so over generations camouflage evolves.
//
// Controls:
//   Tap water            -> drop food
//   Tap a creature       -> inspect it (shows its camouflage score)
//   Swipe left / right   -> change biome (the creatures stay - can they adapt?)
//   Swipe up / down      -> add / remove a predator (0-3)
//   Hold 1-4s, release   -> status card
//   Hold >4s, release    -> start a new world
//
// Sections:
//   STAGE 1  biomes: a patchy background, pre-drawn into its own sprite
//   STAGE 2  creatures: Terrarium bodies and genes, plus a new brightness gene
//   STAGE 3  predators that hunt by sight (contrast-based detection)
//   STAGE 4  life cycle: food, eggs, mutation, death

#include <Arduino.h>
#include <Wire.h>
#include <LovyanGFX.hpp>

// ======================= Tuning knobs =======================
const float SIM_SPEED       = 1.0f;   // 2 = everything runs twice as fast
const int   START_CREATURES = 14;
const float FOOD_PER_SEC    = 1.0f;   // algae pellets that appear each second
const float PELLET_ENERGY   = 20.0f;
const float BURN_PER_SEC    = 0.45f;  // energy an average adult burns per second
const float EGG_TIME        = 8;      // seconds until an egg hatches
const float ADULT_AGE       = 40;     // seconds until full size and able to breed
const float LIFESPAN_MIN    = 240, LIFESPAN_MAX = 360;
const float EGG_COOLDOWN    = 25;     // minimum seconds between eggs
const float EGG_ENERGY      = 0.75f;  // must be this full (fraction) to lay an egg
const float EGG_COST        = 30.0f;
const float STARVING        = 0.25f;
const int   MUTATION_CHANCE = 35;     // % chance each gene changes in a child
const int   BIG_MUTATION_CHANCE = 5;

// Predator knobs
const int   NUM_PREDATORS      = 1;     // at the start (swipe up/down to change)
const float PRED_VISION        = 80;    // pixels
const float SEE_MIN            = 0.05f; // contrast below this is invisible to predators
const float SEE_MAX            = 0.40f; // contrast above this is impossible to miss
const float SPOT_RATE          = 1.5f;  // spots per second for a fully visible creature
const float MOVE_VISIBILITY    = 1.6f;  // moving creatures are this much easier to see
const float LOSE_RATE          = 0.5f;  // how quickly a camouflaged target is lost mid-chase
const float PRED_PATROL_SPEED  = 16;
const float PRED_CHASE_SPEED   = 38;
const float CHASE_TIME         = 8;     // gives up after this many seconds
const float REST_TIME          = 7;     // digesting after a meal

// ======================= Board pins =======================
#define LCD_SCLK 10
#define LCD_MOSI 11
#define LCD_CS    9
#define LCD_DC    8
#define LCD_RST  14
#define LCD_BL    2
#define TP_SDA    6
#define TP_SCL    7
#define TP_RST   13
#define TP_ADDR  0x15   // CST816S touch chip

// ======================= Display driver =======================
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
LGFX_Sprite canvas(&tft);   // the frame being drawn
LGFX_Sprite bg(&tft);       // the background, drawn once per biome

// ======================= The world =======================
const int MAX_CREATURES = 24;
const int MAX_PELLETS   = 40;
const int MAX_PREDATORS = 3;
const float CX = 120, CY = 120, TANK_R = 112;
const int PATCH = 20, PG = 12;              // background is a 12x12 grid of 20px patches

struct Creature {
  bool alive, moving;
  float x, y, angle, energy;
  float age, lifespan, lastEgg;             // seconds
  uint16_t gen;
  // ---- genes ----
  uint8_t size, stretch, legs, tail, eyes;  // body (same as Terrarium)
  uint8_t hue, hue2, tone, pattern;         // colour (same as Terrarium)
  uint8_t light;                            // NEW: 0 = dark ... 255 = bright
};

struct Pellet { bool active; float x, y; };

enum { PATROL, CHASE, REST };
struct Predator {
  float x, y, angle;
  uint8_t state;
  int target;          // creature being chased
  float timer;         // chase time, or rest time left
};

struct Biome { const char *name; uint8_t col[3][3]; };
const Biome BIOMES[] = {
  {"Kelp Forest", {{20, 70, 35}, {80, 100, 40}, {50, 140, 65}}},
  {"Coral Reef",  {{205, 175, 115}, {215, 105, 100}, {125, 85, 155}}},
  {"Deep Water",  {{12, 22, 60}, {20, 65, 85}, {6, 10, 28}}},
};
const int NUM_BIOMES = sizeof(BIOMES) / sizeof(BIOMES[0]);

Creature cr[MAX_CREATURES];
Pellet pel[MAX_PELLETS];
Predator pred[MAX_PREDATORS];
int numPredators = NUM_PREDATORS;
uint8_t patch[PG][PG];      // which of the biome's 3 colours each patch uses
int biome = 0;
uint32_t eatenCount = 0;
uint16_t maxGen = 1;
float foodAccum = 0;

int selected = -1;
uint32_t inspectUntil = 0, statusUntil = 0, msgUntil = 0;
char msg[32] = "";
void showMsg(const char *m) { strncpy(msg, m, sizeof(msg) - 1); msgUntil = millis() + 2000; }

float frand() { return random(100000) / 100000.0f; }                 // 0..1
float frand(float a, float b) { return a + (b - a) * frand(); }
float wrapAngle(float a) {
  while (a > PI) a -= 2 * PI;
  while (a < -PI) a += 2 * PI;
  return a;
}

// ======================= STAGE 1: biomes =======================
// Random patches, then "majority vote" smoothing so they clump into blobs
void makePatches() {
  for (int x = 0; x < PG; x++)
    for (int y = 0; y < PG; y++) patch[x][y] = random(3);
  for (int pass = 0; pass < 3; pass++) {
    uint8_t next[PG][PG];
    for (int x = 0; x < PG; x++)
      for (int y = 0; y < PG; y++) {
        int count[3] = {0, 0, 0};
        for (int dx = -1; dx <= 1; dx++)
          for (int dy = -1; dy <= 1; dy++) {
            int nx = x + dx, ny = y + dy;
            if (nx >= 0 && ny >= 0 && nx < PG && ny < PG) count[patch[nx][ny]]++;
          }
        int best = patch[x][y];
        for (int k = 0; k < 3; k++) if (count[k] > count[best]) best = k;
        next[x][y] = best;
      }
    memcpy(patch, next, sizeof(patch));
  }
}

// Draw the background ONCE into its own sprite; each frame just copies it
void renderBackground() {
  const Biome &b = BIOMES[biome];
  for (int x = 0; x < PG; x++)
    for (int y = 0; y < PG; y++) {
      const uint8_t *c = b.col[patch[x][y]];
      bg.fillRect(x * PATCH, y * PATCH, PATCH, PATCH, lgfx::color565(c[0], c[1], c[2]));
    }
  // Speckles so it looks like texture instead of flat squares
  for (int k = 0; k < 900; k++) {
    int x = random(240), y = random(240);
    const uint8_t *c = b.col[patch[x / PATCH][y / PATCH]];
    int s = random(-28, 29);
    bg.drawPixel(x, y, lgfx::color565(constrain(c[0] + s, 0, 255), constrain(c[1] + s, 0, 255),
                                      constrain(c[2] + s, 0, 255)));
  }
  // Black out everything outside the round tank, row by row
  for (int y = 0; y < 240; y++) {
    float dy = y - CY;
    if (fabsf(dy) >= 119) { bg.drawFastHLine(0, y, 240, TFT_BLACK); continue; }
    int half = (int)sqrtf(119 * 119 - dy * dy);
    bg.drawFastHLine(0, y, (int)CX - half, TFT_BLACK);
    bg.drawFastHLine((int)CX + half, y, 240 - ((int)CX + half), TFT_BLACK);
  }
}

void setBiome(int b) {
  biome = (b + NUM_BIOMES) % NUM_BIOMES;
  makePatches();
  renderBackground();
}

// The background colour under a point
void bgColorAt(float x, float y, uint8_t &r, uint8_t &g, uint8_t &b) {
  int px = constrain((int)x / PATCH, 0, PG - 1), py = constrain((int)y / PATCH, 0, PG - 1);
  const uint8_t *c = BIOMES[biome].col[patch[px][py]];
  r = c[0]; g = c[1]; b = c[2];
}

// ======================= STAGE 2: creatures =======================
bool  isEgg(const Creature &c)     { return c.age < EGG_TIME; }
int   segments(const Creature &c)  { return 1 + c.stretch / 64; }
float maxEnergy(const Creature &c) { return 50 + c.size * 5; }
float visionOf(const Creature &c)  { return 35 + 70 * c.eyes / 255.0f; }

float growth(const Creature &c) {
  if (c.age <= EGG_TIME) return 0.45f;
  if (c.age >= ADULT_AGE) return 1.0f;
  return 0.45f + 0.55f * (c.age - EGG_TIME) / (ADULT_AGE - EGG_TIME);
}

float speedOf(const Creature &c) {
  return (8 + 5 * c.legs + 10 * c.tail / 255.0f) * (1.2f - c.size / 30.0f);
}

float burnPerSec(const Creature &c) {
  float activity = 0.7f + 0.15f * c.legs + 0.3f * c.tail / 255.0f + 0.1f * c.eyes / 255.0f;
  float b = BURN_PER_SEC * (c.size / 9.0f) * activity;
  if (c.age < ADULT_AGE) b *= 0.5f;
  if (c.energy < STARVING * maxEnergy(c)) b *= 0.4f;
  return b;
}

void hsvToRgb(uint8_t h, float s, float v, uint8_t &r, uint8_t &g, uint8_t &b) {
  float hh = h / 256.0f * 6.0f, f = hh - floorf(hh);
  float p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
  float rr, gg, bb;
  switch ((int)hh) {
    case 0:  rr = v; gg = t; bb = p; break;
    case 1:  rr = q; gg = v; bb = p; break;
    case 2:  rr = p; gg = v; bb = t; break;
    case 3:  rr = p; gg = q; bb = v; break;
    case 4:  rr = t; gg = p; bb = v; break;
    default: rr = v; gg = p; bb = q; break;
  }
  r = rr * 255; g = gg * 255; b = bb * 255;
}

// A creature's true colour: hue + tone (saturation) + light (brightness)
void creatureRGB(const Creature &c, uint8_t hue, uint8_t &r, uint8_t &g, uint8_t &b) {
  hsvToRgb(hue, 0.25f + 0.7f * c.tone / 255.0f, 0.2f + 0.8f * c.light / 255.0f, r, g, b);
}

// Screen colour: true colour, faded toward grey when starving, optionally darkened
uint16_t colorFor(const Creature &c, uint8_t hue, float shade) {
  uint8_t r, g, b;
  creatureRGB(c, hue, r, g, b);
  float frac = c.energy / maxEnergy(c);
  if (frac < STARVING) {
    float k = frac / STARVING, grey = (r + g + b) / 3.0f;
    r = grey + (r - grey) * k; g = grey + (g - grey) * k; b = grey + (b - grey) * k;
  }
  return lgfx::color565(r * shade, g * shade, b * shade);
}
uint16_t creatureColor(const Creature &c, float shade = 1.0f) { return colorFor(c, c.hue, shade); }

void randomGenes(Creature &c) {
  c.size = random(6, 12);
  c.stretch = random(0, 200);
  c.legs = random(0, 3);
  c.tail = random(0, 256);
  c.eyes = random(0, 256);
  c.hue = random(0, 256);
  c.hue2 = random(0, 256);
  c.tone = random(0, 256);
  c.pattern = random(0, 4);
  c.light = random(0, 256);
}

// ======================= STAGE 3: seeing and being seen =======================
// How different two colours look, 0 (identical) to 1 (black vs white).
// Green is weighted most because eyes are most sensitive to it.
float colorDistance(uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2, uint8_t b2) {
  float dr = r1 - r2, dg = g1 - g2, db = b1 - b2;
  return sqrtf(0.30f * dr * dr + 0.59f * dg * dg + 0.11f * db * db) / 255.0f;
}

// How much a creature stands out from the patch it's on (0 = invisible)
float contrastOf(const Creature &c) {
  uint8_t br, bgc, bb, r, g, b;
  bgColorAt(c.x, c.y, br, bgc, bb);
  creatureRGB(c, c.hue, r, g, b);
  float c1 = colorDistance(r, g, b, br, bgc, bb);
  if (c.pattern == 0) return c1;
  creatureRGB(c, c.hue2, r, g, b);                 // patterns mix in the second colour...
  float c2 = colorDistance(r, g, b, br, bgc, bb);
  return (0.7f * c1 + 0.3f * c2) * 0.9f;           // ...and break up the outline a little
}

// Contrast -> 0..1 "how noticeable", using a threshold like real vision:
// below SEE_MIN it can't be seen at all, above SEE_MAX it can't be missed
float noticeable(const Creature &c) {
  return constrain((contrastOf(c) - SEE_MIN) / (SEE_MAX - SEE_MIN), 0.0f, 1.0f);
}

// How easy a creature is to spot right now
float visibility(const Creature &c) {
  float n = noticeable(c);
  float v = n * n * (0.6f + 0.4f * c.size / 13.0f);   // squared: small contrast differences matter a lot
  if (c.moving) v *= MOVE_VISIBILITY;                  // big and moving creatures are easier to see
  return v;
}

// Camouflage score shown to the player: 100% = invisible to predators
int camoPercent(const Creature &c) { return (int)(100 * (1 - noticeable(c))); }

// A short expanding ring where a creature died, so deaths are visible
struct Poof { bool active; float x, y; uint32_t start; uint16_t color; };
const int MAX_POOFS = 8;
const uint32_t POOF_MS = 700;
Poof poofs[MAX_POOFS];

void addPoof(float x, float y, uint16_t color) {
  int slot = 0;
  for (int k = 0; k < MAX_POOFS; k++) {
    if (!poofs[k].active) { slot = k; break; }
    if (poofs[k].start < poofs[slot].start) slot = k;   // all busy: reuse the oldest
  }
  poofs[slot] = {true, x, y, millis(), color};
}

const uint16_t POOF_EATEN = 0xF800;   // red
const uint16_t POOF_OLD   = 0xBDF7;   // light grey
const uint16_t POOF_STARVED = 0x8A22; // brown

void die(int i, uint16_t poofColor) {
  cr[i].alive = false;
  addPoof(cr[i].x, cr[i].y, poofColor);
  if (selected == i) selected = -1;
}

void predatorStep(float dt) {
  for (int p = 0; p < numPredators; p++) {
    Predator &P = pred[p];
    float sp = 0, turn = 1.5f;
    bool steer = false;
    float desired = 0;

    if (P.state == REST) {                             // digesting: drift slowly
      sp = 4;
      P.timer -= dt;
      if (P.timer <= 0) P.state = PATROL;
    } else if (P.state == PATROL) {                    // cruise and look around
      sp = PRED_PATROL_SPEED;
      for (int i = 0; i < MAX_CREATURES; i++) {
        Creature &c = cr[i];
        if (!c.alive || isEgg(c)) continue;
        if (sq(c.x - P.x) + sq(c.y - P.y) > sq(PRED_VISION)) continue;
        // The key line: the chance of noticing a creature depends on how visible it is
        if (frand() < SPOT_RATE * visibility(c) * dt) {
          P.state = CHASE; P.target = i; P.timer = 0;
          break;
        }
      }
    } else {                                           // CHASE
      Creature &t = cr[P.target];
      P.timer += dt;
      float d2 = sq(t.x - P.x) + sq(t.y - P.y);
      bool lost = !t.alive || isEgg(t) || P.timer > CHASE_TIME || d2 > sq(PRED_VISION * 1.3f)
                  || frand() < LOSE_RATE * (1 - min(1.0f, visibility(t))) * dt;   // camouflaged prey can vanish
      if (lost) {
        P.state = REST; P.timer = 1.5f;                // short pause before hunting again
      } else {
        sp = PRED_CHASE_SPEED; turn = 3.0f;
        desired = atan2f(t.y - P.y, t.x - P.x); steer = true;
        if (d2 < sq(t.size * growth(t) + 8)) {         // caught it
          die(P.target, POOF_EATEN);
          eatenCount++;
          P.state = REST; P.timer = REST_TIME;
        }
      }
    }

    // Steering: chase target, or turn back from the glass, or meander
    float dx = P.x - CX, dy = P.y - CY;
    if (dx * dx + dy * dy > sq(TANK_R - 15)) { desired = atan2f(-dy, -dx); steer = true; }
    if (steer) {
      float maxTurn = turn * dt;
      P.angle += constrain(wrapAngle(desired - P.angle), -maxTurn, maxTurn);
    } else {
      P.angle += sinf(millis() / 1000.0f * 0.5f + p * 2.0f) * 0.8f * dt;
    }
    P.angle = wrapAngle(P.angle);
    P.x += cosf(P.angle) * sp * dt;
    P.y += sinf(P.angle) * sp * dt;
  }
}

// ======================= Creature movement =======================
void moveStep(float dt) {
  float t = millis() / 1000.0f;
  for (int i = 0; i < MAX_CREATURES; i++) {
    Creature &c = cr[i];
    if (!c.alive || isEgg(c)) { c.moving = false; continue; }
    float g = growth(c), r = c.size * g;
    bool steer = false;
    float desired = 0, sp = 0;

    // Danger first: flee a nearby predator, or one that is chasing me
    int threat = -1;
    float bestD = sq(visionOf(c));
    for (int p = 0; p < numPredators; p++) {
      float d2 = sq(pred[p].x - c.x) + sq(pred[p].y - c.y);
      bool chasingMe = pred[p].state == CHASE && pred[p].target == i;
      if (d2 < bestD && (chasingMe || d2 < sq(35))) { bestD = d2; threat = p; }
    }

    int food = -1;
    float dx = c.x - CX, dy = c.y - CY, rc = sqrtf(dx * dx + dy * dy);
    if (threat >= 0) {
      // Run away from the predator...
      float ax = c.x - pred[threat].x, ay = c.y - pred[threat].y, ad = sqrtf(ax * ax + ay * ay) + 0.01f;
      float vx = ax / ad, vy = ay / ad;
      // ...but bend away from the wall as it gets close, instead of running into it.
      // Without this, "flee" and "turn back from the wall" fight, and prey get trapped circling the edge.
      const float WALL_ZONE = 40;
      if (rc > TANK_R - WALL_ZONE && rc > 0.01f) {
        float w = (rc - (TANK_R - WALL_ZONE)) / WALL_ZONE * 1.5f;   // 0 at the zone's edge, 1.5 at the wall
        vx -= dx / rc * w;
        vy -= dy / rc * w;
      }
      desired = atan2f(vy, vx);
      steer = true;
      sp = speedOf(c);
    } else if (c.energy < 0.9f * maxEnergy(c)) {
      float best = sq(visionOf(c));
      for (int j = 0; j < MAX_PELLETS; j++) {
        if (!pel[j].active) continue;
        float d2 = sq(pel[j].x - c.x) + sq(pel[j].y - c.y);
        if (d2 < best) { best = d2; food = j; }
      }
      if (food >= 0) { desired = atan2f(pel[food].y - c.y, pel[food].x - c.x); steer = true; sp = speedOf(c); }
      else sp = speedOf(c) * 0.4f;                               // hungry: wander looking for food
    }
    // Full and safe: sit still (still creatures are harder to spot)

    // Not fleeing and too close to the glass? Turn back toward the middle
    if (threat < 0 && rc > TANK_R - r - 6) { desired = atan2f(-dy, -dx); steer = true; }

    if (steer) {
      float maxTurn = (threat >= 0 ? 4.0f : 2.5f) * dt;   // panicking creatures turn faster
      c.angle += constrain(wrapAngle(desired - c.angle), -maxTurn, maxTurn);
    } else {
      c.angle += sinf(t * 0.4f + i * 1.7f) * 0.9f * dt;
    }
    c.angle = wrapAngle(c.angle);

    sp *= 0.6f + 0.4f * g;
    if (c.energy < STARVING * maxEnergy(c)) sp *= 0.5f;
    c.moving = sp > 1.0f;
    c.x += cosf(c.angle) * sp * dt;
    c.y += sinf(c.angle) * sp * dt;

    if (food >= 0 && sq(pel[food].x - c.x) + sq(pel[food].y - c.y) < sq(r + 3)) {
      c.energy = min(maxEnergy(c), c.energy + PELLET_ENERGY);
      pel[food].active = false;
    }
  }

  // Push overlapping creatures apart
  for (int i = 0; i < MAX_CREATURES; i++) {
    Creature &a = cr[i];
    if (!a.alive || isEgg(a)) continue;
    for (int j = i + 1; j < MAX_CREATURES; j++) {
      Creature &b = cr[j];
      if (!b.alive || isEgg(b)) continue;
      float dx = b.x - a.x, dy = b.y - a.y, d = sqrtf(dx * dx + dy * dy);
      float minD = (a.size * growth(a) + b.size * growth(b)) * 0.9f;
      if (d < minD && d > 0.01f) {
        float push = (minD - d) / 2, ux = dx / d, uy = dy / d;
        a.x -= ux * push; a.y -= uy * push;
        b.x += ux * push; b.y += uy * push;
      }
    }
  }

  // Hard wall for everyone, predators included
  for (int i = 0; i < MAX_CREATURES; i++) {
    Creature &c = cr[i];
    if (!c.alive) continue;
    float dx = c.x - CX, dy = c.y - CY, d = sqrtf(dx * dx + dy * dy);
    if (d > TANK_R - 3) { c.x = CX + dx / d * (TANK_R - 3); c.y = CY + dy / d * (TANK_R - 3); }
  }
  for (int p = 0; p < numPredators; p++) {
    float dx = pred[p].x - CX, dy = pred[p].y - CY, d = sqrtf(dx * dx + dy * dy);
    if (d > TANK_R - 8) { pred[p].x = CX + dx / d * (TANK_R - 8); pred[p].y = CY + dy / d * (TANK_R - 8); }
  }
}

// ======================= STAGE 4: life cycle =======================
uint8_t mutateGene(uint8_t v, int lo, int hi, int step) {
  if (random(100) < MUTATION_CHANCE) v = constrain((int)v + (int)random(-step, step + 1), lo, hi);
  return v;
}

int freeSlot() {
  for (int i = 0; i < MAX_CREATURES; i++) if (!cr[i].alive) return i;
  return -1;
}

void layEgg(Creature &parent) {
  int i = freeSlot();
  if (i < 0) return;
  Creature &k = cr[i];
  k = parent;
  k.size    = mutateGene(parent.size, 5, 13, 2);
  k.stretch = mutateGene(parent.stretch, 0, 255, 50);
  k.legs    = mutateGene(parent.legs, 0, 4, 1);
  k.tail    = mutateGene(parent.tail, 0, 255, 60);
  k.eyes    = mutateGene(parent.eyes, 0, 255, 50);
  k.tone    = mutateGene(parent.tone, 0, 255, 40);
  k.light   = mutateGene(parent.light, 0, 255, 40);
  if (random(100) < MUTATION_CHANCE) k.hue = parent.hue + random(-20, 21);    // wraps around
  if (random(100) < MUTATION_CHANCE) k.hue2 = parent.hue2 + random(-20, 21);
  if (random(100) < BIG_MUTATION_CHANCE) {
    switch (random(7)) {
      case 0: k.size = random(5, 14); break;
      case 1: k.stretch = random(0, 256); break;
      case 2: k.legs = random(0, 5); break;
      case 3: k.tail = random(0, 256); break;
      case 4: k.eyes = random(0, 256); break;
      case 5: k.pattern = random(0, 4); break;
      case 6: k.hue = random(0, 256); break;
    }
  }
  k.x = parent.x - cosf(parent.angle) * parent.size * 2;
  k.y = parent.y - sinf(parent.angle) * parent.size * 2;
  k.age = 0;
  k.lastEgg = 0;
  k.energy = 30;
  k.lifespan = frand(LIFESPAN_MIN, LIFESPAN_MAX);
  k.gen = parent.gen + 1;
  k.moving = false;
  maxGen = max(maxGen, k.gen);
  parent.energy -= EGG_COST;
  parent.lastEgg = parent.age;
}

void spawnPellet(float x, float y) {
  for (int j = 0; j < MAX_PELLETS; j++)
    if (!pel[j].active) { pel[j].active = true; pel[j].x = x; pel[j].y = y; return; }
}

void randomSpotInTank(float &x, float &y, float maxR) {
  float a = frand(0, 2 * PI), d = maxR * sqrtf(frand());   // sqrt spreads points evenly over the disc
  x = CX + cosf(a) * d;
  y = CY + sinf(a) * d;
}

void bioStep(float dt) {
  // Algae grows by itself
  foodAccum += FOOD_PER_SEC * dt;
  while (foodAccum >= 1) {
    float x, y;
    randomSpotInTank(x, y, TANK_R - 10);
    spawnPellet(x, y);
    foodAccum -= 1;
  }

  for (int i = 0; i < MAX_CREATURES; i++) {
    Creature &c = cr[i];
    if (!c.alive) continue;
    c.age += dt;
    if (isEgg(c)) continue;
    c.energy -= burnPerSec(c) * dt;
    if (c.energy <= 0)       { die(i, POOF_STARVED); continue; }
    if (c.age > c.lifespan)  { die(i, POOF_OLD); continue; }
    if (c.age >= ADULT_AGE && c.energy > EGG_ENERGY * maxEnergy(c) && c.age - c.lastEgg > EGG_COOLDOWN)
      layEgg(c);
  }
}

void placePredator(Predator &P) {
  float a = frand(0, 2 * PI);
  P.x = CX + cosf(a) * (TANK_R - 15);
  P.y = CY + sinf(a) * (TANK_R - 15);
  P.angle = a + PI;              // facing inward
  P.state = REST;
  P.timer = 5;                   // grace period before it starts hunting
  P.target = -1;
}

void newWorld() {
  memset(cr, 0, sizeof(cr));
  memset(pel, 0, sizeof(pel));
  memset(poofs, 0, sizeof(poofs));
  eatenCount = 0;
  maxGen = 1;
  foodAccum = 0;
  setBiome(random(NUM_BIOMES));
  for (int k = 0; k < START_CREATURES; k++) {
    Creature &c = cr[k];
    c.alive = true;
    randomGenes(c);
    randomSpotInTank(c.x, c.y, 70);
    c.angle = frand(0, 2 * PI);
    c.age = ADULT_AGE + frand(0, 60);
    c.lifespan = frand(LIFESPAN_MIN, LIFESPAN_MAX);
    c.lastEgg = c.age - frand(0, EGG_COOLDOWN);
    c.energy = maxEnergy(c) * 0.7f;
    c.gen = 1;
  }
  for (int k = 0; k < 20; k++) { float x, y; randomSpotInTank(x, y, TANK_R - 10); spawnPellet(x, y); }
  for (int p = 0; p < MAX_PREDATORS; p++) placePredator(pred[p]);
}

// ======================= Drawing =======================
void drawPattern(const Creature &c, float px, float py, float rr,
                 float fx, float fy, float sx, float sy) {
  uint16_t col2 = colorFor(c, c.hue2, 1.0f);
  if (c.pattern == 1) {
    for (int k = -1; k <= 1; k += 2) {
      float o = rr * 0.35f * k;
      float half = sqrtf(rr * rr - o * o) * 0.9f;
      float cx = px + fx * o, cy = py + fy * o;
      for (int w = 0; w < 2; w++)
        canvas.drawLine(cx + sx * half + fx * w, cy + sy * half + fy * w,
                        cx - sx * half + fx * w, cy - sy * half + fy * w, col2);
    }
  } else if (c.pattern == 2) {
    float sr = max(1.0f, rr * 0.2f);
    canvas.fillCircle(px - fx * rr * 0.3f + sx * rr * 0.4f, py - fy * rr * 0.3f + sy * rr * 0.4f, sr, col2);
    canvas.fillCircle(px - fx * rr * 0.3f - sx * rr * 0.4f, py - fy * rr * 0.3f - sy * rr * 0.4f, sr, col2);
    canvas.fillCircle(px + fx * rr * 0.25f, py + fy * rr * 0.25f, sr, col2);
  } else if (c.pattern == 3) {
    canvas.fillCircle(px - fx * rr * 0.15f, py - fy * rr * 0.15f, rr * 0.55f, col2);
  }
}

void drawCreature(const Creature &c, int idx, float t) {
  float g = growth(c), r = c.size * g;
  int segs = segments(c);
  float fx = cosf(c.angle), fy = sinf(c.angle);
  float sx = -fy, sy = fx;
  float wiggle = c.moving ? sinf(t * 6.0f + idx) : 0;   // still creatures stop wiggling
  float spacing = r * 0.9f;
  float L = spacing * (segs - 1);
  uint16_t col = creatureColor(c), dark = creatureColor(c, 0.6f);

  float tl = c.tail / 255.0f * 18 * g;
  if (tl > 1.5f) {
    float bx = c.x - fx * (L + r * 0.7f), by = c.y - fy * (L + r * 0.7f);
    float ex = bx - fx * tl + sx * wiggle * tl * 0.35f;
    float ey = by - fy * tl + sy * wiggle * tl * 0.35f;
    canvas.fillTriangle(bx + sx * r * 0.4f, by + sy * r * 0.4f,
                        bx - sx * r * 0.4f, by - sy * r * 0.4f, ex, ey, dark);
  }
  for (int i = 0; i < c.legs; i++) {
    float along = (c.legs == 1) ? L * 0.5f : L * i / (c.legs - 1);
    float px = c.x - fx * along, py = c.y - fy * along;
    float len = r + 5 * g, k = ((i % 2) ? wiggle : -wiggle) * 3 * g;
    canvas.drawLine(px, py, px + sx * len + fx * k, py + sy * len + fy * k, dark);
    canvas.drawLine(px, py, px - sx * len - fx * k, py - sy * len - fy * k, dark);
  }
  for (int i = segs - 1; i >= 0; i--) {
    float px = c.x - fx * spacing * i, py = c.y - fy * spacing * i;
    float rr = r * (1.0f - i * 0.12f);
    canvas.fillCircle(px, py, rr, col);
    drawPattern(c, px, py, rr, fx, fy, sx, sy);
    canvas.drawCircle(px, py, rr, dark);
  }
  float e = (1.0f + c.eyes / 85.0f) * g + 0.5f;
  for (int side = -1; side <= 1; side += 2) {
    float ex = c.x + fx * r * 0.45f + sx * r * 0.45f * side;
    float ey = c.y + fy * r * 0.45f + sy * r * 0.45f * side;
    canvas.fillCircle(ex, ey, e, TFT_WHITE);
    canvas.fillCircle(ex + fx * e * 0.4f, ey + fy * e * 0.4f, max(1.0f, e * 0.5f), TFT_BLACK);
  }
}

void drawEgg(const Creature &c, float t) {
  float wob = (EGG_TIME - c.age < EGG_TIME / 4) ? sinf(t * 15) * 1.5f : 0;
  canvas.fillEllipse(c.x + wob, c.y, 3, 4, creatureColor(c, 0.8f));   // eggs are tinted, so they camouflage too
}

// A long, dark hunter. Eye colour shows its mood: orange = patrolling, red = chasing, dim = resting
void drawPredator(const Predator &P, float t) {
  float fx = cosf(P.angle), fy = sinf(P.angle), sx = -fy, sy = fx;
  float wig = sinf(t * (P.state == CHASE ? 14 : 5));
  uint16_t body = lgfx::color565(45, 35, 55), edge = lgfx::color565(150, 55, 65);
  float bx = P.x - fx * 20, by = P.y - fy * 20;
  canvas.fillTriangle(bx + sx * 5, by + sy * 5, bx - sx * 5, by - sy * 5,
                      bx - fx * 13 + sx * wig * 6, by - fy * 13 + sy * wig * 6, body);
  for (int i = 2; i >= 0; i--) {
    float px = P.x - fx * 8 * i, py = P.y - fy * 8 * i;
    float r = 10 - i * 2;
    canvas.fillCircle(px, py, r, body);
    canvas.drawCircle(px, py, r, edge);
  }
  uint16_t eye = P.state == CHASE ? TFT_RED
               : P.state == REST ? lgfx::color565(90, 40, 40) : lgfx::color565(240, 140, 50);
  for (int side = -1; side <= 1; side += 2)
    canvas.fillCircle(P.x + fx * 5 + sx * 4 * side, P.y + fy * 5 + sy * 4 * side, 2, eye);
  if (P.state == CHASE && P.timer < 0.8f) {          // "!" when it first spots something
    canvas.setTextColor(TFT_RED);
    canvas.drawCenterString("!", P.x, P.y - 22);
  }
}

void card(int y, int h) { canvas.fillRoundRect(40, y, 160, h, 8, TFT_BLACK); }

int averageCamo(int &alive, int &eggs) {
  alive = eggs = 0;
  long sum = 0;
  for (int i = 0; i < MAX_CREATURES; i++) {
    if (!cr[i].alive) continue;
    if (isEgg(cr[i])) { eggs++; continue; }
    alive++;
    sum += camoPercent(cr[i]);
  }
  return alive ? sum / alive : 0;
}

void render(float t, bool touching, int tx, int ty, uint32_t held) {
  bg.pushSprite(&canvas, 0, 0);       // copy the pre-drawn background (fast!)

  for (int j = 0; j < MAX_PELLETS; j++)
    if (pel[j].active) canvas.fillCircle(pel[j].x, pel[j].y, 2, lgfx::color565(150, 220, 110));

  for (int i = 0; i < MAX_CREATURES; i++)
    if (cr[i].alive && isEgg(cr[i])) drawEgg(cr[i], t);
  for (int i = 0; i < MAX_CREATURES; i++)
    if (cr[i].alive && !isEgg(cr[i])) drawCreature(cr[i], i, t);
  for (int p = 0; p < numPredators; p++) drawPredator(pred[p], t);

  for (int k = 0; k < MAX_POOFS; k++) {           // death rings grow and fade out
    if (!poofs[k].active) continue;
    uint32_t age = millis() - poofs[k].start;
    if (age > POOF_MS) { poofs[k].active = false; continue; }
    float f = age / (float)POOF_MS;
    canvas.drawCircle(poofs[k].x, poofs[k].y, 3 + f * 12, poofs[k].color);
    if (f < 0.5f) canvas.drawCircle(poofs[k].x, poofs[k].y, 2 + f * 6, poofs[k].color);
  }

  // HUD
  char buf[40];
  int alive, eggs;
  int camo = averageCamo(alive, eggs);
  canvas.fillRoundRect(62, 8, 116, 28, 6, TFT_BLACK);
  canvas.setTextSize(1);
  canvas.setTextColor(TFT_WHITE);
  canvas.drawCenterString(BIOMES[biome].name, 120, 11);
  snprintf(buf, sizeof(buf), "Pop %d  Camo %d%%", alive, camo);
  canvas.drawCenterString(buf, 120, 23);

  if (alive == 0 && eggs == 0) {
    card(100, 34);
    canvas.drawCenterString("Extinct!", 120, 106);
    canvas.drawCenterString("Hold 4s for a new world", 120, 120);
  }

  if (selected >= 0 && millis() < inspectUntil && cr[selected].alive && !isEgg(cr[selected])) {
    Creature &c = cr[selected];
    canvas.drawCircle(c.x, c.y, c.size * growth(c) + 6, TFT_WHITE);
    card(160, 46);
    snprintf(buf, sizeof(buf), "Gen %u   Camo %d%%", c.gen, camoPercent(c));
    canvas.drawCenterString(buf, 120, 166);
    snprintf(buf, sizeof(buf), "legs %u  tail %u%%  eyes %u%%", c.legs, c.tail * 100 / 255, c.eyes * 100 / 255);
    canvas.drawCenterString(buf, 120, 178);
    snprintf(buf, sizeof(buf), "%s", c.moving ? "moving (easier to see)" : "still (harder to see)");
    canvas.drawCenterString(buf, 120, 190);
  }

  if (millis() < statusUntil) {
    card(80, 80);
    canvas.drawCenterString(BIOMES[biome].name, 120, 88);
    snprintf(buf, sizeof(buf), "Creatures %d  Eggs %d", alive, eggs);
    canvas.drawCenterString(buf, 120, 104);
    snprintf(buf, sizeof(buf), "Eaten %lu  Predators %d", (unsigned long)eatenCount, numPredators);
    canvas.drawCenterString(buf, 120, 120);
    snprintf(buf, sizeof(buf), "Highest gen %u", maxGen);
    canvas.drawCenterString(buf, 120, 136);
  }

  if (touching) {
    uint16_t c = held > 4000 ? TFT_RED : (held > 600 ? TFT_ORANGE : TFT_WHITE);
    canvas.drawCircle(tx, ty, min((int)(held / 80) + 4, 55), c);
  }

  if (millis() < msgUntil) {
    canvas.fillRoundRect(45, 42, 150, 18, 6, TFT_BLACK);
    canvas.setTextColor(TFT_WHITE);
    canvas.drawCenterString(msg, 120, 47);
  }

  canvas.pushSprite(0, 0);
}

// ======================= Touch =======================
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

int creatureAt(int x, int y) {
  for (int i = 0; i < MAX_CREATURES; i++) {
    Creature &c = cr[i];
    if (!c.alive || isEgg(c)) continue;
    float r = c.size * growth(c);
    for (int s = 0; s < segments(c); s++) {
      float px = c.x - cosf(c.angle) * r * 0.9f * s, py = c.y - sinf(c.angle) * r * 0.9f * s;
      if (sq(x - px) + sq(y - py) < sq(r + 6)) return i;
    }
  }
  return -1;
}

void handleGesture(int x0, int y0, int x1, int y1, uint32_t dur) {
  int dx = x1 - x0, dy = y1 - y0;
  char b[32];
  if (abs(dx) > 40 || abs(dy) > 40) {
    if (abs(dx) > abs(dy)) {                              // left / right: change biome
      setBiome(biome + (dx > 0 ? 1 : -1));
      showMsg(BIOMES[biome].name);
    } else {                                              // up / down: predators
      int old = numPredators;
      numPredators = constrain(numPredators + (dy < 0 ? 1 : -1), 0, MAX_PREDATORS);
      if (numPredators > old) placePredator(pred[numPredators - 1]);
      snprintf(b, sizeof(b), "Predators: %d", numPredators);
      showMsg(b);
    }
  } else if (dur < 600) {
    int i = creatureAt(x0, y0);
    if (i >= 0) { selected = i; inspectUntil = millis() + 4000; }
    else for (int k = 0; k < 3; k++) spawnPellet(x0 + random(-8, 9), y0 + random(-8, 9));
  } else if (dur < 4000) {
    statusUntil = millis() + 4000;
  } else {
    newWorld();
    selected = -1;
    showMsg("New world!");
  }
}

// ======================= Main =======================
bool touchDown = false;
uint32_t touchStart = 0, lastFrame = 0, lastLog = 0;
int tx0, ty0, txl, tyl;

void setup() {
  Serial.begin(115200);

  pinMode(TP_RST, OUTPUT);
  digitalWrite(TP_RST, LOW);  delay(10);
  digitalWrite(TP_RST, HIGH); delay(50);
  Wire.begin(TP_SDA, TP_SCL, 400000);
  Wire.beginTransmission(TP_ADDR);
  Wire.write(0xFE); Wire.write(0x01);
  Wire.endTransmission();

  tft.init();
  tft.setBrightness(200);
  canvas.setColorDepth(16);
  canvas.setPsram(true);
  if (!canvas.createSprite(240, 240)) { canvas.setPsram(false); canvas.createSprite(240, 240); }
  bg.setColorDepth(16);
  bg.setPsram(true);
  bg.createSprite(240, 240);

  randomSeed(esp_random());
  newWorld();
  showMsg(BIOMES[biome].name);
  lastFrame = millis();
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

  uint32_t now = millis();
  float dt = (now - lastFrame) / 1000.0f;
  lastFrame = now;
  if (dt > 0.1f) dt = 0.1f;
  dt *= SIM_SPEED;

  bioStep(dt);
  predatorStep(dt);
  moveStep(dt);
  render(now / 1000.0f, touchDown, txl, tyl, touchDown ? now - touchStart : 0);

  // Serial Plotter: watch camouflage climb over time
  if (now - lastLog > 5000) {
    lastLog = now;
    int alive, eggs, camo = averageCamo(alive, eggs);
    Serial.printf("camo:%d,population:%d,eaten:%lu\n", camo, alive, (unsigned long)eatenCount);
  }
}
