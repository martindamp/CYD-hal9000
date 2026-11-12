// PC simulator: runs the exact fire_eye.h engine with a scripted touch
// sequence and writes raw RGB frames (320x240) to stdout-specified dir.
// Build & run: see render.sh
#include <cstdio>
#include <cstdlib>
#include "../../src/fire_eye.h"

static FireEye eye;

int main(int argc, char** argv) {
  const char* outDir = argc > 1 ? argv[1] : "out";
  int frames = argc > 2 ? atoi(argv[2]) : 240;
  eye.begin(42);
  const float dt = 1.0f / 25;
  uint16_t row[FE_W * 2];
  bool wasDown = false;
  for (int f = 0; f < frames; f++) {
    float t = f * dt;
    FireInput in{};
    // Script: idle, tap right, idle, long hold left
    if (t > 3.0f && t < 3.25f) { in.touching = true; in.tx = 140; in.ty = 40; }
    if (t > 6.0f && t < 8.5f)  { in.touching = true; in.tx = 25;  in.ty = 80; }
    in.tapped = in.touching && !wasDown;
    wasDown = in.touching;
    eye.step(dt, in);
    char path[256];
    snprintf(path, sizeof(path), "%s/f%04d.ppm", outDir, f);
    FILE* fp = fopen(path, "wb");
    fprintf(fp, "P6 320 240 255\n");
    for (int sy = 0; sy < 240; sy++) {
      eye.renderRow(sy, row);
      for (int x = 0; x < 320; x++) {
        uint16_t c = row[x];
        unsigned char px[3] = {(unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2),
                               (unsigned char)((c & 31) << 3)};
        fwrite(px, 1, 3, fp);
      }
    }
    fclose(fp);
  }
  return 0;
}
