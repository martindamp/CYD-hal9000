// fire_eye.h — platform-independent "eye of fire" engine.
//
// Renders into an 8-bit heat buffer at half the screen resolution
// (FE_W x FE_H). The caller maps heat -> colour with the palette and
// upscales 2x when pushing to the display. No Arduino dependencies, so
// the exact same code runs in the PC simulator (tools/sim).
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>

#define FE_W 160
#define FE_H 120

struct FireInput {
  bool  touching;     // finger currently on the screen
  bool  tapped;       // true on the frame the finger went down
  float tx, ty;       // touch position in engine coords (0..FE_W, 0..FE_H)
};

class FireEye {
public:
  uint8_t  heat[FE_H][FE_W];
  uint16_t palette[256];          // RGB565

  void begin(uint32_t seed) {
    rng = seed ? seed : 0x1234567u;
    memset(heat, 0, sizeof(heat));
    for (int i = 0; i < NOISE_N; i++) { noiseA[i] = rnd() & 255; noiseB[i] = rnd() & 255; }
    buildPalette();
  }

  // Advance the animation by dt seconds.
  void step(float dt, const FireInput& in) {
    t += dt;
    updateState(dt, in);
    propagate();
    drawEye();
    drawEmbers(dt);
  }

  // Produce one full-resolution screen row (2*FE_W pixels, RGB565) by
  // interpolating heat between neighbouring cells, then applying the palette.
  void renderRow(int sy, uint16_t* out) const {
    int y = sy >> 1;
    int y2 = (sy & 1) && y + 1 < FE_H ? y + 1 : y;
    const uint8_t* a = heat[y];
    const uint8_t* b = heat[y2];
    for (int x = 0; x < FE_W; x++) {
      int h0 = (a[x] + b[x]) >> 1;
      int x1 = x + 1 < FE_W ? x + 1 : x;
      int h1 = (h0 + ((a[x1] + b[x1]) >> 1)) >> 1;
      out[2 * x] = palette[h0];
      out[2 * x + 1] = palette[h1];
    }
  }

  // Exposed for debugging / tuning
  float breath = 0, flare = 0, focus = 0;

private:
  static const int NOISE_N = 64;
  uint8_t  noiseA[NOISE_N], noiseB[NOISE_N];
  uint32_t rng = 1;
  float t = 0;
  float spin = 0;                         // iris streak rotation
  float gazeX = 0, gazeY = 0, gazeTX = 0, gazeTY = 0;
  float idleTimer = 0, nextGlance = 3;
  float holdTime = 0;

  // Ember bursts spawned by touches
  struct Burst { float x, y, life; };
  Burst bursts[6] = {};

  // Eye geometry (engine coords)
  static constexpr float CX = FE_W * 0.5f, CY = FE_H * 0.55f;
  static constexpr float HW = 64.0f;      // almond half-width
  static constexpr float HH = 30.0f;      // almond half-height
  static constexpr float IRIS_R = 27.0f;

  inline uint32_t rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

  static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

  static inline uint16_t rgb565(int r, int g, int b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
  }

  void buildPalette() {
    // heat -> colour: black, ember red, red, orange, yellow, white-hot
    const struct { int h, r, g, b; } k[] = {
      {0, 0, 0, 0}, {40, 30, 0, 0}, {90, 140, 10, 0}, {140, 230, 50, 0},
      {190, 255, 140, 10}, {230, 255, 220, 60}, {255, 255, 255, 200}};
    const int n = sizeof(k) / sizeof(k[0]);
    for (int h = 0; h < 256; h++) {
      int i = 0;
      while (i < n - 2 && h > k[i + 1].h) i++;
      float f = (float)(h - k[i].h) / (k[i + 1].h - k[i].h);
      f = clampf(f, 0, 1);
      palette[h] = rgb565(k[i].r + (k[i + 1].r - k[i].r) * f,
                          k[i].g + (k[i + 1].g - k[i].g) * f,
                          k[i].b + (k[i + 1].b - k[i].b) * f);
    }
  }

  // Smooth 1D periodic value noise, x in "cells", returns 0..1
  float noise(const uint8_t* tab, float x) const {
    float fl = floorf(x);
    int i = ((int)fl) & (NOISE_N - 1);
    float f = x - fl;
    f = f * f * (3 - 2 * f);
    float a = tab[i], b = tab[(i + 1) & (NOISE_N - 1)];
    return (a + (b - a) * f) * (1.0f / 255.0f);
  }

  static inline float fastAtan2(float y, float x) {   // ~0.01 rad accuracy
    float ax = fabsf(x), ay = fabsf(y);
    float mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax;
    if (mx < 1e-6f) return 0;
    float a = mn / mx, s = a * a;
    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax) r = 1.57079637f - r;
    if (x < 0) r = 3.14159274f - r;
    if (y < 0) r = -r;
    return r;
  }

  void updateState(float dt, const FireInput& in) {
    breath = 0.5f + 0.5f * sinf(t * 6.2831853f / 4.5f);    // slow ~4.5 s pulse

    if (in.tapped) {
      flare = 1.0f;
      for (auto& b : bursts) if (b.life <= 0) { b = {in.tx, in.ty, 0.45f}; break; }
    }
    if (in.touching) {
      holdTime += dt;
      if (holdTime > 0.6f) focus = fminf(1.0f, focus + dt * 1.2f);
      // look toward the finger
      gazeTX = clampf((in.tx - CX) * 0.35f, -20, 20);
      gazeTY = clampf((in.ty - CY) * 0.25f, -6, 6);
      idleTimer = 0;
    } else {
      holdTime = 0;
      focus = fmaxf(0.0f, focus - dt * 0.8f);
      idleTimer += dt;
      if (idleTimer > nextGlance) {              // idle: an occasional slow glance
        idleTimer = 0;
        nextGlance = 3 + (rnd() % 5);
        if (rnd() & 1) { gazeTX = 0; gazeTY = 0; }
        else { gazeTX = (int)(rnd() % 25) - 12; gazeTY = (int)(rnd() % 7) - 3; }
      }
    }
    flare = fmaxf(0.0f, flare - dt * 1.1f);
    float k = fminf(1.0f, dt * 5.0f);
    gazeX += (gazeTX - gazeX) * k;
    gazeY += (gazeTY - gazeY) * k;
    spin += dt * (0.25f + 1.8f * focus + 1.0f * flare);
  }

  // Classic rising-fire propagation: each pixel takes heat from below,
  // with random sideways drift and random cooling.
  void propagate() {
    // Doom-style: each cell moves up one row, drifts sideways and cools
    // by either 0 or `step`. Lower step => taller flames; breath, taps and
    // holding all fan the fire.
    int step = (int)(17 - 6 * breath - 7 * flare - 6 * focus);
    if (step < 3) step = 3;
    for (int y = 0; y < FE_H - 1; y++) {
      uint8_t* row = heat[y];
      const uint8_t* below = heat[y + 1];
      for (int x = 0; x < FE_W; x++) {
        uint32_t r = rnd();
        int dx = x - (int)(r % 3) + 1;
        if (dx < 0 || dx >= FE_W) continue;
        int xl = x > 0 ? x - 1 : x, xr = x + 1 < FE_W ? x + 1 : x;
        int src = (below[xl] + 2 * below[x] + below[xr]) >> 2;    // soften
        int v = src - (int)((r >> 4) & 1) * step - (int)((r >> 5) & 3);
        row[dx] = v > 0 ? v : 0;
      }
    }
    for (int x = 0; x < FE_W; x++) heat[FE_H - 1][x] = heat[FE_H - 1][x] * 3 / 4;
  }

  // Devil-head silhouette in pupil space (u right, v down, ~[-1,1]).
  static bool inTri(float px, float py, float ax, float ay, float bx, float by, float cx, float cy) {
    float d1 = (px - bx) * (ay - by) - (ax - bx) * (py - by);
    float d2 = (px - cx) * (by - cy) - (bx - cx) * (py - cy);
    float d3 = (px - ax) * (cy - ay) - (cx - ax) * (py - ay);
    bool neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    bool pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(neg && pos);
  }
  static bool inDevil(float u, float v) {
    float au = fabsf(u);                                        // symmetric
    float eu = u / 0.40f, ev = (v + 0.02f) / 0.46f;
    if (eu * eu + ev * ev < 1) return true;                     // head
    if (inTri(au, v, 0.0f, 0.25f, 0.30f, 0.20f, 0.0f, 0.78f)) return true;   // pointed chin / goatee
    if (inTri(au, v, 0.14f, -0.36f, 0.34f, -0.26f, 0.50f, -0.98f)) return true; // horn
    if (inTri(au, v, 0.33f, -0.16f, 0.38f, 0.10f, 0.66f, -0.30f)) return true;  // pointed ear
    return false;
  }
  static bool inDevilEye(float u, float v) {
    float au = fabsf(u);
    // slanted slits
    float du = (au - 0.17f), dv = (v + 0.07f) + (au - 0.17f) * 0.5f;
    return (du * du) / (0.11f * 0.11f) + (dv * dv) / (0.045f * 0.045f) < 1;
  }

  void drawEye() {
    float pulse = breath;
    float irisR = IRIS_R + 1.5f * pulse - 3.0f * flare + 2.0f * focus;
    float pupS  = 13.0f + 1.0f * pulse - 2.5f * flare + 5.0f * focus;   // devil size
    float ix = CX + gazeX, iy = CY + gazeY;
    const float inv2pi = 1.0f / 6.2831853f;

    for (int y = (int)(CY - HH - 1); y <= (int)(CY + HH + 1); y++) {
      if (y < 0 || y >= FE_H) continue;
      float dyc = y - CY;
      for (int x = (int)(CX - HW); x <= (int)(CX + HW); x++) {
        if (x < 0 || x >= FE_W) continue;
        float nx = (x - CX) / HW;
        float lim = HH * (1 - nx * nx);                         // almond lids
        float edge = lim - fabsf(dyc);
        if (edge < 0) continue;

        float h;
        if (edge < 1.6f) {
          // hot lid line; brightness varies along the lid so flames form tongues
          float tongue = noise(noiseA, x * 0.11f + t * 1.7f) * noise(noiseB, x * 0.23f - t * 2.3f);
          h = 150 + 105 * fminf(1.0f, tongue * 2.2f) + (rnd() & 15);
          if (dyc > 0) h = 200 + (rnd() & 31);                  // lower lid: steady glow
        } else {
          float dx = x - ix, dy = y - iy;
          float r = sqrtf(dx * dx + dy * dy);
          float a = fastAtan2(dy, dx) * inv2pi * NOISE_N;       // angle in noise cells
          if (r < irisR) {
            // iris: radial fiery streaks, brightest around the pupil
            float s = 0.6f * noise(noiseA, a + spin * 6) + 0.4f * noise(noiseB, a * 2 - spin * 9);
            float rr = r / irisR;
            float ring = 1.0f - rr * rr;
            h = 120 + 120 * ring * (0.55f + 0.45f * s) + 30 * pulse + 40 * flare;
            if (rr > 0.9f) h -= (rr - 0.9f) * 900;              // dark limbal ring
            // darken around the pupil so the silhouette reads
            float pr = r / (pupS * 1.45f);
            if (pr < 1) h *= 0.15f + 0.85f * pr * pr;
            float u = dx / pupS, v = dy / pupS;
            if (inDevil(u, v)) h = inDevilEye(u, v) ? 90 + 40 * pulse + 60 * flare : 0;
          } else {
            // sclera: dark, slowly churning embers
            float n = noise(noiseB, x * 0.15f + t * 1.5f) * noise(noiseA, y * 0.3f - t * 2.1f + x * 0.05f);
            h = 55 + 70 * n + 25 * pulse + (rnd() & 15);
            h *= 0.55f + 0.45f * clampf(edge / 8, 0, 1) * 1.0f + 0.0f;
            if (edge < 4) h += (4 - edge) * 30;                 // glow near the lids
          }
        }
        heat[y][x] = (uint8_t)clampf(h, 0, 255);
      }
    }
  }

  void drawEmbers(float dt) {
    for (auto& b : bursts) {
      if (b.life <= 0) continue;
      b.life -= dt;
      int rad = 7;
      for (int dy = -rad; dy <= rad; dy++)
        for (int dx = -rad; dx <= rad; dx++) {
          if (dx * dx + dy * dy > rad * rad) continue;
          int x = (int)b.x + dx, y = (int)b.y + dy;
          if (x < 0 || y < 0 || x >= FE_W || y >= FE_H) continue;
          if ((rnd() & 3) == 0) continue;
          int v = 200 + (rnd() & 55);
          if (v > heat[y][x]) heat[y][x] = v;
        }
    }
  }
};
