#!/usr/bin/env bash
# Renders the simulator to docs/preview.gif and docs/*.png (needs g++ and python3-pil).
set -e
cd "$(dirname "$0")"
mkdir -p out
g++ -O2 -std=c++17 -o out/sim sim.cpp
rm -f out/*.ppm
./out/sim out 250
python3 - <<'PY'
from PIL import Image
import glob
fr = sorted(glob.glob("out/f*.ppm"))
imgs = [Image.open(p).convert("RGB") for p in fr]
imgs[60].save("../../docs/idle.png"); imgs[80].save("../../docs/tap.png"); imgs[205].save("../../docs/hold.png")
pal = [im.resize((320, 240)).quantize(colors=64, method=Image.Quantize.MEDIANCUT) for im in imgs[::3]]
pal[0].save("../../docs/preview.gif", save_all=True, append_images=pal[1:], duration=120, loop=0, optimize=True)
print("frames:", len(fr))
PY
