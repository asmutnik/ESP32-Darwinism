// Pocket Terrarium - a tiny creature tank that depends on you to feed it
// Board: Waveshare ESP32-S3-Touch-LCD-1.28    Library: LovyanGFX
//
// Controls:
//   Tap empty water      -> drop food (a few pellets)
//   Tap a creature/egg   -> inspect it
//   Swipe any direction  -> clean the tank
//   Hold 1-5s, release   -> tank status card
//   Hold >5s, release    -> start a brand-new tank (replaces the save!)
//
// The tank saves itself to flash memory. Unplugging pauses time;
// plugging back in resumes exactly where it left off.
//
// Sections are labelled STAGE 1-4 so you can find each feature:
//   STAGE 1  creatures that swim and look different (genes -> body)
//   STAGE 2  hunger, feeding, dirty water
//   STAGE 3  eggs, growing up, aging, mutation
//   STAGE 4  saving and loading

#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <LovyanGFX.hpp>

// ======================= Tuning knobs =======================
// TIME_SCALE speeds up biology (hunger, aging, eggs, rotting) but not swimming.
//   1    = real time (the normal setting)
//   600  = one tank-day passes in 2.4 minutes - great for testing!
const float TIME_SCALE = 150.0f;

const uint32_t HOUR = 3600, DAY = 24 * HOUR;   // tank-time is counted in seconds
const uint32_t EGG_TIME     = 3 * HOUR;    // eggs hatch after this
const uint32_t ADULT_AGE    = 1 * DAY;     // grown up (full size, can lay eggs)
const uint32_t EGG_COOLDOWN = 36 * HOUR;   // minimum time between eggs
const uint32_t ROT_TIME     = 8 * HOUR;    // uneaten food rots after this
const float BURN_PER_HOUR   = 2.2f;        // energy an average adult burns per hour
const float PELLET_ENERGY   = 25.0f;       // energy in one food pellet
const float EGG_ENERGY      = 0.85f;       // must be at least this full (fraction) to lay an egg
const float EGG_COST        = 40.0f;       // energy a parent spends on each egg
const float STARVING        = 0.25f;       // below this fraction full: grey, sluggish, survival mode
const int   PELLETS_PER_TAP = 3;
const float WASTE_PER_MEAL  = 0.4f;        // dirt added every time something eats
const float ROT_DIRT        = 3.0f;        // dirt added when a pellet rots
const float DEATH_DIRT      = 1.5f;        // dirt added when a creature dies
const float DIRT_HARM       = 40.0f;       // dirt above this starts hurting creatures
const float DIRT_DAMAGE_PER_HOUR = 3.0f;   // extra energy lost per hour at 100% dirt
const int   MUTATION_CHANCE = 35;          // % chance each gene changes in a child
const int   BIG_MUTATION_CHANCE = 5;       // % chance a child gets one brand-new gene
const int   START_CREATURES = 6;
const uint32_t SAVE_EVERY_MS = 5UL * 60 * 1000;  // autosave interval (real time)
const uint32_t SAVE_VERSION = 2;           // bump this whenever you change struct World

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
LGFX_Sprite canvas(&tft);

// ======================= The world =======================
const int MAX_CREATURES = 14;   // includes eggs
const int MAX_PELLETS   = 40;
const float CX = 120, CY = 120, TANK_R = 112;   // tank centre and radius in pixels

struct Creature {
  bool alive;
  float x, y, angle;          // position (pixels) and facing direction (radians)
  float energy;               // food in the belly; 0 = starved
  uint32_t age;               // tank-seconds since the egg was laid
  uint32_t lifespan;          // dies of old age after this
  uint32_t lastEgg;           // age when it last laid an egg
  uint16_t gen;               // generation number (starting creatures are 1)
  // ---- genes: these are what get passed on and mutated ----
  uint8_t size;               // body radius 5-13 px
  uint8_t stretch;            // 0 = round blob ... 255 = long worm (1-4 segments)
  uint8_t legs;               // pairs of legs, 0-4
  uint8_t tail;               // tail length, 0-255
  uint8_t eyes;               // eye size, 0-255
  uint8_t hue;                // colour, 0-255 around the colour wheel
  uint8_t hue2;               // second colour, used by patterns
  uint8_t tone;               // 0 = pale pastel ... 255 = deep, rich colour
  uint8_t pattern;            // 0 plain, 1 stripes, 2 spots, 3 saddle
};

struct Pellet {
  bool active;
  float x, y;
  uint32_t age;               // tank-seconds since it was dropped
};

// Everything that needs saving lives in ONE struct, so saving is one line (STAGE 4)
struct World {
  uint32_t version;
  uint32_t simSeconds;        // total tank-time
  float dirt;                 // 0 = clean, 100 = filthy
  uint16_t maxGen;            // highest generation ever reached
  Creature c[MAX_CREATURES];
  Pellet p[MAX_PELLETS];
};
World world;

// Things that do NOT need saving (screen state, touch state, timers)
Preferences prefs;
float simAccum = 0;                 // fractional tank-seconds not yet simulated
uint32_t lastFrame = 0, lastSave = 0, lastChange = 0;
bool unsavedChanges = false;
int selected = -1;                  // creature being inspected
uint32_t inspectUntil = 0, statusUntil = 0, msgUntil = 0;
char msg[32] = "";

void showMsg(const char *m) { strncpy(msg, m, sizeof(msg) - 1); msgUntil = millis() + 2000; }

float wrapAngle(float a) {
  while (a > PI) a -= 2 * PI;
  while (a < -PI) a += 2 * PI;
  return a;
}

// ======================= STAGE 1: genes -> body and behaviour =======================
bool  isEgg(const Creature &c)     { return c.age < EGG_TIME; }
int   segments(const Creature &c)  { return 1 + c.stretch / 64; }
float maxEnergy(const Creature &c) { return 50 + c.size * 5; }        // bigger belly
float visionOf(const Creature &c)  { return 35 + 70 * c.eyes / 255.0f; }

// Juveniles start at 45% size and grow to full size at ADULT_AGE
float growth(const Creature &c) {
  if (c.age <= EGG_TIME) return 0.45f;
  if (c.age >= ADULT_AGE) return 1.0f;
  return 0.45f + 0.55f * (c.age - EGG_TIME) / (float)(ADULT_AGE - EGG_TIME);
}

// Legs and tails make you faster; big bodies make you slower
float speedOf(const Creature &c) {
  return (8 + 5 * c.legs + 10 * c.tail / 255.0f) * (1.2f - c.size / 30.0f);
}

// ...but every body part costs energy. This is what makes evolution choose.
float burnPerHour(const Creature &c) {
  float activity = 0.7f + 0.15f * c.legs + 0.3f * c.tail / 255.0f + 0.1f * c.eyes / 255.0f;
  float b = BURN_PER_HOUR * (c.size / 9.0f) * activity;
  if (c.age < ADULT_AGE) b *= 0.5f;                   // babies need less food
  if (c.energy < STARVING * maxEnergy(c)) b *= 0.4f;  // survival mode: starving creatures slow down
  return b;
}

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
}

void newTank() {
  memset(&world, 0, sizeof(world));
  world.version = SAVE_VERSION;
  world.maxGen = 1;
  for (int k = 0; k < START_CREATURES; k++) {
    Creature &c = world.c[k];
    c.alive = true;
    randomGenes(c);
    float a = random(628) / 100.0f, d = random(0, 70);
    c.x = CX + cosf(a) * d;
    c.y = CY + sinf(a) * d;
    c.angle = random(628) / 100.0f;
    c.age = ADULT_AGE + random(0, DAY);              // start as adults of different ages
    c.lifespan = random(5 * DAY, 8 * DAY);
    c.lastEgg = c.age - random(0, EGG_COOLDOWN);     // staggered so they don't all breed at once
    c.energy = maxEnergy(c) * 0.7f;
    c.gen = 1;
  }
}

// Swimming runs in REAL time (dt in seconds), so it looks smooth at any TIME_SCALE
void moveStep(float dt) {
  float t = millis() / 1000.0f;
  for (int i = 0; i < MAX_CREATURES; i++) {
    Creature &c = world.c[i];
    if (!c.alive || isEgg(c)) continue;
    float g = growth(c), r = c.size * g;

    // Hungry? Look for the nearest pellet within vision range
    int food = -1;
    bool steer = false;
    float desired = 0;
    if (c.energy < 0.9f * maxEnergy(c)) {
      float best = sq(visionOf(c));
      for (int j = 0; j < MAX_PELLETS; j++) {
        Pellet &p = world.p[j];
        if (!p.active) continue;
        float d2 = sq(p.x - c.x) + sq(p.y - c.y);
        if (d2 < best) { best = d2; food = j; }
      }
      if (food >= 0) { desired = atan2f(world.p[food].y - c.y, world.p[food].x - c.x); steer = true; }
    }

    // Too close to the glass? Turn back toward the middle
    float dx = c.x - CX, dy = c.y - CY;
    if (dx * dx + dy * dy > sq(TANK_R - r - 6)) { desired = atan2f(-dy, -dx); steer = true; }

    if (steer) {
      float maxTurn = 2.5f * dt;   // radians per second it can turn
      c.angle += constrain(wrapAngle(desired - c.angle), -maxTurn, maxTurn);
    } else {
      c.angle += sinf(t * 0.4f + i * 1.7f) * 0.9f * dt;   // lazy meandering, different per creature
    }
    c.angle = wrapAngle(c.angle);

    float sp = speedOf(c) * (food >= 0 ? 1.0f : 0.4f) * (0.6f + 0.4f * g);
    if (c.energy < STARVING * maxEnergy(c)) sp *= 0.5f;   // sluggish when starving
    c.x += cosf(c.angle) * sp * dt;
    c.y += sinf(c.angle) * sp * dt;

    // STAGE 2: eat the pellet if we reached it
    if (food >= 0) {
      Pellet &p = world.p[food];
      if (sq(p.x - c.x) + sq(p.y - c.y) < sq(r + 3)) {
        c.energy = min(maxEnergy(c), c.energy + PELLET_ENERGY);
        p.active = false;
        world.dirt = min(100.0f, world.dirt + WASTE_PER_MEAL);
      }
    }
  }

  // Gently push overlapping creatures apart
  for (int i = 0; i < MAX_CREATURES; i++) {
    Creature &a = world.c[i];
    if (!a.alive || isEgg(a)) continue;
    for (int j = i + 1; j < MAX_CREATURES; j++) {
      Creature &b = world.c[j];
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

  // Hard wall: nobody leaves the tank
  for (int i = 0; i < MAX_CREATURES; i++) {
    Creature &c = world.c[i];
    if (!c.alive) continue;
    float dx = c.x - CX, dy = c.y - CY, d = sqrtf(dx * dx + dy * dy);
    if (d > TANK_R - 3) { c.x = CX + dx / d * (TANK_R - 3); c.y = CY + dy / d * (TANK_R - 3); }
  }
}

// ======================= STAGE 3: eggs, mutation, aging =======================
uint8_t mutateGene(uint8_t v, int lo, int hi, int step) {
  if (random(100) < MUTATION_CHANCE) v = constrain((int)v + (int)random(-step, step + 1), lo, hi);
  return v;
}

int freeSlot() {
  for (int i = 0; i < MAX_CREATURES; i++) if (!world.c[i].alive) return i;
  return -1;
}

void layEgg(Creature &parent) {
  int i = freeSlot();
  if (i < 0) return;                       // tank is full
  Creature &k = world.c[i];
  k = parent;                              // copy everything, then change what differs

  // Small mutations: each gene might drift a little
  k.size    = mutateGene(parent.size, 5, 13, 2);
  k.stretch = mutateGene(parent.stretch, 0, 255, 50);
  k.legs    = mutateGene(parent.legs, 0, 4, 1);
  k.tail    = mutateGene(parent.tail, 0, 255, 60);
  k.eyes    = mutateGene(parent.eyes, 0, 255, 50);
  if (random(100) < MUTATION_CHANCE) k.hue = parent.hue + random(-20, 21);  // uint8_t wraps around the colour wheel
  if (random(100) < MUTATION_CHANCE) k.hue2 = parent.hue2 + random(-20, 21);
  k.tone = mutateGene(parent.tone, 0, 255, 40);

  // Rare big mutation: one gene is completely re-rolled
  if (random(100) < BIG_MUTATION_CHANCE) {
    switch (random(6)) {
      case 0: k.size = random(5, 14); break;
      case 1: k.stretch = random(0, 256); break;
      case 2: k.legs = random(0, 5); break;
      case 3: k.tail = random(0, 256); break;
      case 4: k.eyes = random(0, 256); break;
      case 5: k.pattern = random(0, 4); break;
    }
  }

  k.x = parent.x - cosf(parent.angle) * parent.size * 2;   // laid behind the parent
  k.y = parent.y - sinf(parent.angle) * parent.size * 2;
  k.age = 0;
  k.lastEgg = 0;
  k.energy = 35;
  k.lifespan = random(5 * DAY, 8 * DAY);
  k.gen = parent.gen + 1;
  world.maxGen = max(world.maxGen, k.gen);

  parent.energy -= EGG_COST;               // eggs are expensive
  parent.lastEgg = parent.age;
  showMsg("New egg!");
}

void die(Creature &c, const char *why) {
  c.alive = false;
  world.dirt = min(100.0f, world.dirt + DEATH_DIRT);
  if (selected >= 0 && &world.c[selected] == &c) selected = -1;
  showMsg(why);
}

// Biology runs in TANK time: called with whole tank-seconds that have passed
void bioStep(uint32_t secs) {
  world.simSeconds += secs;

  for (int i = 0; i < MAX_CREATURES; i++) {
    Creature &c = world.c[i];
    if (!c.alive) continue;

    bool wasEgg = isEgg(c);
    c.age += secs;
    if (wasEgg) {                           // eggs don't eat or age-die
      if (!isEgg(c)) showMsg("An egg hatched!");
      continue;
    }

    // STAGE 2: hunger, plus extra harm from dirty water
    c.energy -= burnPerHour(c) * secs / 3600.0f;
    if (world.dirt > DIRT_HARM)
      c.energy -= (world.dirt - DIRT_HARM) / (100 - DIRT_HARM) * DIRT_DAMAGE_PER_HOUR * secs / 3600.0f;

    if (c.energy <= 0)       { die(c, "One starved..."); continue; }
    if (c.age > c.lifespan)  { die(c, "Died of old age"); continue; }

    // Well-fed adults lay eggs now and then
    if (c.age >= ADULT_AGE && c.energy > EGG_ENERGY * maxEnergy(c) && c.age - c.lastEgg > EGG_COOLDOWN)
      layEgg(c);
  }

  // STAGE 2: uneaten food rots and dirties the water
  for (int j = 0; j < MAX_PELLETS; j++) {
    Pellet &p = world.p[j];
    if (!p.active) continue;
    p.age += secs;
    if (p.age > ROT_TIME) { p.active = false; world.dirt = min(100.0f, world.dirt + ROT_DIRT); }
  }
}

// ======================= STAGE 4: saving and loading =======================
void markChanged() { unsavedChanges = true; lastChange = millis(); }

void saveTank() {
  prefs.putBytes("world", &world, sizeof(World));
  lastSave = millis();
  unsavedChanges = false;
  Serial.println("Tank saved");
}

bool loadTank() {
  // If the saved data is a different size, the struct changed since it was saved - don't trust it
  if (prefs.getBytesLength("world") != sizeof(World)) return false;
  prefs.getBytes("world", &world, sizeof(World));
  return world.version == SAVE_VERSION;
}

// ======================= Drawing =======================
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

// Creature colour; starving creatures fade toward grey (a visual hunger warning)
uint16_t colorFor(const Creature &c, uint8_t hue, float shade) {
  uint8_t r, g, b;
  float tn = c.tone / 255.0f;
  hsvToRgb(hue, 0.3f + 0.65f * tn, 1.0f - 0.3f * tn, r, g, b);
  float frac = c.energy / maxEnergy(c);
  if (frac < STARVING) {
    float k = frac / STARVING, grey = (r + g + b) / 3.0f;
    r = grey + (r - grey) * k; g = grey + (g - grey) * k; b = grey + (b - grey) * k;
  }
  return lgfx::color565(r * shade, g * shade, b * shade);
}

uint16_t creatureColor(const Creature &c, float shade = 1.0f) { return colorFor(c, c.hue, shade); }

// Draws the pattern gene on one body segment centred at (px, py) with radius rr
void drawPattern(const Creature &c, float px, float py, float rr,
                 float fx, float fy, float sx, float sy) {
  uint16_t col2 = colorFor(c, c.hue2, 1.0f);
  if (c.pattern == 1) {                                   // stripes: bands across the body
    for (int k = -1; k <= 1; k += 2) {
      float o = rr * 0.35f * k;                           // stripe position along the body
      float half = sqrtf(rr * rr - o * o) * 0.9f;         // chord length, so stripes fit the circle
      float cx = px + fx * o, cy = py + fy * o;
      for (int w = 0; w < 2; w++)                         // two 1px lines = a 2px stripe
        canvas.drawLine(cx + sx * half + fx * w, cy + sy * half + fy * w,
                        cx - sx * half + fx * w, cy - sy * half + fy * w, col2);
    }
  } else if (c.pattern == 2) {                            // spots: three dots
    float sr = max(1.0f, rr * 0.2f);
    canvas.fillCircle(px - fx * rr * 0.3f + sx * rr * 0.4f, py - fy * rr * 0.3f + sy * rr * 0.4f, sr, col2);
    canvas.fillCircle(px - fx * rr * 0.3f - sx * rr * 0.4f, py - fy * rr * 0.3f - sy * rr * 0.4f, sr, col2);
    canvas.fillCircle(px + fx * rr * 0.25f, py + fy * rr * 0.25f, sr, col2);
  } else if (c.pattern == 3) {                            // saddle: a patch of the second colour
    canvas.fillCircle(px - fx * rr * 0.15f, py - fy * rr * 0.15f, rr * 0.55f, col2);
  }
}

void drawCreature(const Creature &c, int idx, float t) {
  float g = growth(c), r = c.size * g;
  int segs = segments(c);
  float fx = cosf(c.angle), fy = sinf(c.angle);   // "forward" direction
  float sx = -fy, sy = fx;                         // "sideways" direction (forward rotated 90 degrees)
  float wiggle = sinf(t * 6.0f + idx);             // animation, offset per creature
  float spacing = r * 0.9f;                        // distance between body segments
  float L = spacing * (segs - 1);                  // head-to-last-segment length
  uint16_t col = creatureColor(c), dark = creatureColor(c, 0.6f);

  // Tail: a triangle fin off the back that wags sideways
  float tl = c.tail / 255.0f * 18 * g;
  if (tl > 1.5f) {
    float bx = c.x - fx * (L + r * 0.7f), by = c.y - fy * (L + r * 0.7f);
    float ex = bx - fx * tl + sx * wiggle * tl * 0.35f;
    float ey = by - fy * tl + sy * wiggle * tl * 0.35f;
    canvas.fillTriangle(bx + sx * r * 0.4f, by + sy * r * 0.4f,
                        bx - sx * r * 0.4f, by - sy * r * 0.4f, ex, ey, dark);
  }

  // Legs: spread evenly along the body; neighbouring pairs swing opposite ways
  for (int i = 0; i < c.legs; i++) {
    float along = (c.legs == 1) ? L * 0.5f : L * i / (c.legs - 1);
    float px = c.x - fx * along, py = c.y - fy * along;
    float len = r + 5 * g, k = ((i % 2) ? wiggle : -wiggle) * 3 * g;
    canvas.drawLine(px, py, px + sx * len + fx * k, py + sy * len + fy * k, dark);
    canvas.drawLine(px, py, px - sx * len - fx * k, py - sy * len - fy * k, dark);
  }

  // Body: a chain of circles, drawn back-to-front so the head is on top
  for (int i = segs - 1; i >= 0; i--) {
    float px = c.x - fx * spacing * i, py = c.y - fy * spacing * i;
    float rr = r * (1.0f - i * 0.12f);
    canvas.fillCircle(px, py, rr, col);
    drawPattern(c, px, py, rr, fx, fy, sx, sy);
    canvas.drawCircle(px, py, rr, dark);
  }

  // Eyes: size comes from the eyes gene; pupils look where it's going
  float e = (1.0f + c.eyes / 85.0f) * g + 0.5f;
  for (int side = -1; side <= 1; side += 2) {
    float ex = c.x + fx * r * 0.45f + sx * r * 0.45f * side;
    float ey = c.y + fy * r * 0.45f + sy * r * 0.45f * side;
    canvas.fillCircle(ex, ey, e, TFT_WHITE);
    canvas.fillCircle(ex + fx * e * 0.4f, ey + fy * e * 0.4f, max(1.0f, e * 0.5f), TFT_BLACK);
  }
}

void drawEgg(const Creature &c, float t) {
  float wob = (EGG_TIME - c.age < EGG_TIME / 4) ? sinf(t * 15) * 1.5f : 0;   // wobbles when about to hatch
  canvas.fillEllipse(c.x + wob, c.y, 4, 5, lgfx::color565(235, 230, 210));
  canvas.fillCircle(c.x + wob + 1, c.y - 1, 1, creatureColor(c));            // hint of the baby's colour
}

void card(int y, int h) { canvas.fillRoundRect(40, y, 160, h, 8, lgfx::color565(0, 0, 0)); }

void render(float t, bool touching, int tx, int ty, uint32_t held) {
  canvas.fillScreen(TFT_BLACK);

  // Water colour shifts from clear blue to murky green-brown as it gets dirty
  float d = world.dirt / 100.0f;
  canvas.fillCircle(CX, CY, 119, lgfx::color565(10 + 45 * d, 45 + 15 * d, 85 - 60 * d));

  // Rising bubbles (pure decoration)
  for (int b = 0; b < 5; b++) {
    float ph = fmodf(t * 0.08f + b * 0.21f, 1.0f);
    float bx = 50 + b * 35 + sinf(t * 2 + b) * 3, by = 220 - ph * 200;
    if (sq(bx - CX) + sq(by - CY) < sq(TANK_R)) canvas.drawCircle(bx, by, 2, lgfx::color565(120, 170, 210));
  }

  // Food: fresh pellets are tan, old ones darken before they rot
  for (int j = 0; j < MAX_PELLETS; j++) {
    Pellet &p = world.p[j];
    if (!p.active) continue;
    bool old = p.age > ROT_TIME * 3 / 4;
    canvas.fillCircle(p.x, p.y, 2, old ? lgfx::color565(110, 80, 40) : lgfx::color565(215, 170, 90));
  }

  // Eggs first, then creatures on top
  for (int i = 0; i < MAX_CREATURES; i++)
    if (world.c[i].alive && isEgg(world.c[i])) drawEgg(world.c[i], t);
  for (int i = 0; i < MAX_CREATURES; i++)
    if (world.c[i].alive && !isEgg(world.c[i])) drawCreature(world.c[i], i, t);

  // HUD: day counter and care warnings
  char buf[40];
  int alive = 0, eggs = 0, hungry = 0;
  for (int i = 0; i < MAX_CREATURES; i++) {
    Creature &c = world.c[i];
    if (!c.alive) continue;
    if (isEgg(c)) { eggs++; continue; }
    alive++;
    if (c.energy < 0.35f * maxEnergy(c)) hungry++;
  }
  canvas.setTextSize(1);
  canvas.setTextColor(TFT_WHITE);
  snprintf(buf, sizeof(buf), "Day %lu", (unsigned long)(world.simSeconds / DAY + 1));
  canvas.drawCenterString(buf, 120, 12);
  if (world.dirt > DIRT_HARM) {
    canvas.setTextColor(TFT_YELLOW);
    canvas.drawCenterString("Dirty water - swipe!", 120, 24);
  } else if (alive > 0 && hungry * 2 >= alive) {
    canvas.setTextColor(TFT_ORANGE);
    canvas.drawCenterString("Hungry!", 120, 24);
  }
  canvas.setTextColor(TFT_WHITE);

  if (alive == 0 && eggs == 0) {
    canvas.drawCenterString("The tank is empty.", 120, 105);
    canvas.drawCenterString("Hold 5s for a new tank", 120, 120);
  }

  // Inspect card for a tapped creature
  if (selected >= 0 && millis() < inspectUntil && world.c[selected].alive) {
    Creature &c = world.c[selected];
    canvas.drawCircle(c.x, c.y, c.size * growth(c) + 6, TFT_WHITE);
    card(160, 52);
    if (isEgg(c)) {
      snprintf(buf, sizeof(buf), "Egg - gen %u", c.gen);
      canvas.drawCenterString(buf, 120, 166);
      snprintf(buf, sizeof(buf), "Hatches in %.1f h", (EGG_TIME - c.age) / 3600.0f);
      canvas.drawCenterString(buf, 120, 180);
    } else {
      const char *patName[] = {"plain", "stripes", "spots", "saddle"};
      snprintf(buf, sizeof(buf), "Gen %u  age %.1fd  %s", c.gen, c.age / (float)DAY, patName[c.pattern]);
      canvas.drawCenterString(buf, 120, 165);
      snprintf(buf, sizeof(buf), "legs %u  tail %u%%  eyes %u%%", c.legs, c.tail * 100 / 255, c.eyes * 100 / 255);
      canvas.drawCenterString(buf, 120, 177);
      canvas.drawRect(60, 192, 120, 8, TFT_WHITE);
      canvas.fillRect(61, 193, 118 * c.energy / maxEnergy(c), 6, TFT_GREEN);
    }
  }

  // Tank status card
  if (millis() < statusUntil) {
    card(80, 80);
    snprintf(buf, sizeof(buf), "Day %lu", (unsigned long)(world.simSeconds / DAY + 1));
    canvas.drawCenterString(buf, 120, 88);
    snprintf(buf, sizeof(buf), "Creatures %d  Eggs %d", alive, eggs);
    canvas.drawCenterString(buf, 120, 104);
    snprintf(buf, sizeof(buf), "Highest gen %u", world.maxGen);
    canvas.drawCenterString(buf, 120, 120);
    snprintf(buf, sizeof(buf), "Water %d%% clean", (int)(100 - world.dirt));
    canvas.drawCenterString(buf, 120, 136);
  }

  // Hold indicator ring
  if (touching) {
    uint16_t c = held > 5000 ? TFT_RED : (held > 600 ? TFT_ORANGE : TFT_WHITE);
    canvas.drawCircle(tx, ty, min((int)(held / 100) + 4, 55), c);
  }

  if (millis() < msgUntil) {
    canvas.fillRoundRect(45, 40, 150, 18, 6, TFT_BLACK);
    canvas.drawCenterString(msg, 120, 45);
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

// Returns the creature under (x, y), or -1
int creatureAt(int x, int y) {
  for (int i = 0; i < MAX_CREATURES; i++) {
    Creature &c = world.c[i];
    if (!c.alive) continue;
    float r = isEgg(c) ? 5 : c.size * growth(c);
    int segs = isEgg(c) ? 1 : segments(c);
    for (int s = 0; s < segs; s++) {     // check each body segment
      float px = c.x - cosf(c.angle) * r * 0.9f * s, py = c.y - sinf(c.angle) * r * 0.9f * s;
      if (sq(x - px) + sq(y - py) < sq(r + 6)) return i;
    }
  }
  return -1;
}

void dropFood(int x, int y) {
  if (sq(x - CX) + sq(y - CY) > sq(TANK_R - 10)) return;   // tapped outside the water
  int dropped = 0;
  for (int j = 0; j < MAX_PELLETS && dropped < PELLETS_PER_TAP; j++) {
    Pellet &p = world.p[j];
    if (p.active) continue;
    p.active = true;
    p.x = x + random(-8, 9);
    p.y = y + random(-8, 9);
    p.age = 0;
    dropped++;
  }
  if (dropped == 0) showMsg("Plenty of food!");
  else markChanged();
}

void handleGesture(int x0, int y0, int x1, int y1, uint32_t dur) {
  if (abs(x1 - x0) > 40 || abs(y1 - y0) > 40) {          // swipe: clean
    world.dirt = 0;
    showMsg("Tank cleaned!");
    markChanged();
  } else if (dur < 600) {                                // tap: inspect or feed
    int i = creatureAt(x0, y0);
    if (i >= 0) { selected = i; inspectUntil = millis() + 4000; }
    else dropFood(x0, y0);
  } else if (dur < 5000) {                               // medium hold: status
    statusUntil = millis() + 4000;
  } else {                                               // long hold: new tank
    newTank();
    saveTank();
    selected = -1;
    showMsg("New tank!");
  }
}

// ======================= Main =======================
bool touchDown = false;
uint32_t touchStart = 0;
int tx0, ty0, txl, tyl;

void setup() {
  Serial.begin(115200);

  pinMode(TP_RST, OUTPUT);
  digitalWrite(TP_RST, LOW);  delay(10);
  digitalWrite(TP_RST, HIGH); delay(50);
  Wire.begin(TP_SDA, TP_SCL, 400000);
  Wire.beginTransmission(TP_ADDR);   // disable touch auto-sleep
  Wire.write(0xFE); Wire.write(0x01);
  Wire.endTransmission();

  tft.init();
  tft.setBrightness(200);
  canvas.setColorDepth(16);
  canvas.setPsram(true);
  if (!canvas.createSprite(240, 240)) { canvas.setPsram(false); canvas.createSprite(240, 240); }

  randomSeed(esp_random());

  // STAGE 4: resume the saved tank, or start a new one
  prefs.begin("terrarium", false);
  if (loadTank()) {
    showMsg("Welcome back!");
  } else {
    newTank();
    saveTank();
    showMsg("New tank!");
  }
  lastFrame = millis();
}

void loop() {
  // Touch
  int x, y;
  if (readTouch(x, y)) {
    if (!touchDown) { touchDown = true; touchStart = millis(); tx0 = x; ty0 = y; }
    txl = x; tyl = y;
  } else if (touchDown) {
    touchDown = false;
    handleGesture(tx0, ty0, txl, tyl, millis() - touchStart);
  }

  // Time: real seconds since last frame, and the tank-seconds they represent
  uint32_t now = millis();
  float dt = (now - lastFrame) / 1000.0f;
  lastFrame = now;
  if (dt > 0.1f) dt = 0.1f;
  simAccum += dt * TIME_SCALE;
  uint32_t secs = (uint32_t)simAccum;   // whole tank-seconds to simulate
  simAccum -= secs;                     // keep the leftover fraction for next frame
  if (secs) bioStep(secs);

  moveStep(dt);
  render(now / 1000.0f, touchDown, txl, tyl, touchDown ? now - touchStart : 0);

  // STAGE 4: save 10 s after you interact, and every few minutes regardless
  if ((unsavedChanges && now - lastChange > 10000) || now - lastSave > SAVE_EVERY_MS) saveTank();
}
