// CYD Fire Eye — an eye of fire with a devilish pupil for the
// ESP32 Cheap Yellow Display (ESP32-2432S028R).
//
//  - Idle:  the fire breathes slowly and the eye glances around now and then
//  - Tap:   the eye looks at your finger, the fire flares, embers burst where you touched
//  - Hold:  the pupil grows, the iris swirls faster and the flames roar
//
// All animation logic lives in fire_eye.h (shared with the PC simulator in tools/sim).

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "fire_eye.h"

// ---- CYD resistive touch (XPT2046) on its own SPI bus ----
#define XPT2046_IRQ   36
#define XPT2046_MOSI  32
#define XPT2046_MISO  39
#define XPT2046_CLK   25
#define XPT2046_CS    33
// Raw ADC range -> screen pixels (landscape). Adjust if taps land off target.
#define TOUCH_MINX    200
#define TOUCH_MAXX    3700
#define TOUCH_MINY    240
#define TOUCH_MAXY    3800
#define TOUCH_FLIP_X  0    // set to 1 if left/right taps are mirrored
#define TOUCH_FLIP_Y  0    // set to 1 if up/down taps are mirrored

#define SCR_W 320
#define SCR_H 240
#define BAND  10           // screen rows per DMA transfer

TFT_eSPI tft;
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);

static FireEye* eye;                      // heap-allocated (~20 KB)
static uint16_t bandBuf[2][SCR_W * BAND]; // double buffer for DMA

// Returns true while the screen is touched, with the position in screen pixels.
// A short release grace period keeps a resistive panel from "flickering".
static bool readTouch(int& tx, int& ty) {
  static bool down = false;
  static uint32_t lastSeen = 0;
  static int lx = 0, ly = 0;
  if (touchscreen.tirqTouched() && touchscreen.touched()) {
    TS_Point p = touchscreen.getPoint();
    if (p.z >= 200) {
      lx = map(p.x, TOUCH_MINX, TOUCH_MAXX, 0, SCR_W - 1);
      ly = map(p.y, TOUCH_MINY, TOUCH_MAXY, 0, SCR_H - 1);
      if (TOUCH_FLIP_X) lx = SCR_W - 1 - lx;
      if (TOUCH_FLIP_Y) ly = SCR_H - 1 - ly;
      lx = constrain(lx, 0, SCR_W - 1);
      ly = constrain(ly, 0, SCR_H - 1);
      down = true;
      lastSeen = millis();
    }
  } else if (down && millis() - lastSeen > 80) {
    down = false;
  }
  tx = lx; ty = ly;
  return down;
}

static void pushFrame() {
  tft.startWrite();
  tft.setAddrWindow(0, 0, SCR_W, SCR_H);
  int b = 0;
  for (int y = 0; y < SCR_H; y += BAND) {
    uint16_t* buf = bandBuf[b];
    for (int r = 0; r < BAND; r++) eye->renderRow(y + r, buf + r * SCR_W);
    tft.dmaWait();                        // previous band done -> safe to queue this one
    tft.pushPixelsDMA(buf, SCR_W * BAND);
    b ^= 1;
  }
  tft.dmaWait();
  tft.endWrite();
}

void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(1);                     // landscape, 320 x 240
  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(true);
  tft.initDMA();

  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touchscreen.begin(touchSPI);
  touchscreen.setRotation(1);             // matches the landscape screen

  eye = new FireEye();
  eye->begin(esp_random());
  Serial.println("CYD Fire Eye started");
}

void loop() {
  static uint32_t last = millis(), fpsT = millis();
  static int frames = 0;
  static bool wasDown = false;

  uint32_t now = millis();
  float dt = (now - last) / 1000.0f;
  last = now;
  if (dt > 0.1f) dt = 0.1f;

  int tx, ty;
  FireInput in{};
  in.touching = readTouch(tx, ty);
  in.tapped = in.touching && !wasDown;
  wasDown = in.touching;
  in.tx = tx * 0.5f;                      // engine runs at half resolution
  in.ty = ty * 0.5f;

  eye->step(dt, in);
  pushFrame();

  frames++;
  if (now - fpsT > 5000) {
    Serial.printf("fps: %.1f\n", frames * 1000.0f / (now - fpsT));
    frames = 0;
    fpsT = now;
  }
}
