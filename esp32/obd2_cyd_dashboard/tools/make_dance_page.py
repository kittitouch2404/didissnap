#!/usr/bin/env python3
# หน้า 2 (GIF เต็มจอ): แปลง GIF 320x240 เป็น dance.h  (เฟรมละ 4 แถบ JPEG: 72/72/48/48 แถว แถบแรกมีตัวเลขทับ)
# ใช้: python make_dance_page.py ภาพ.gif [โฟลเดอร์ปลายทาง] [--q 80] [--frames N] [--sub 2]
#   --q Q       คุณภาพ JPEG (ค่าเริ่มต้น 80) ลดลงเพื่อประหยัดแฟลช
#   --frames N  ลดจำนวนเฟรมเหลือ N (สุ่มตามเวลา วนลูปต่อเนื่อง) fps ปรับตาม
#   --sub S     chroma subsampling: 2 = 4:2:0 (เล็กสุด, ค่าเริ่มต้น) 0 = 4:4:4
import io, sys, os
from PIL import Image, ImageSequence

args = sys.argv[1:]
def opt(name, dflt):
    if name in args: i = args.index(name); v = int(args[i + 1]); del args[i:i + 2]; return v
    return dflt
Q = opt("--q", 80); NFR = opt("--frames", 0); SUB = opt("--sub", 2)
src, outdir = args[0], (args[1] if len(args) > 1 else ".")
g = Image.open(src)
frames, durs = [], []
for f in ImageSequence.Iterator(g):
    durs.append(f.info.get("duration", 100) or 100)
    im = f.convert("RGB")
    w, h = im.size
    if w * 3 != h * 4:                                        # ไม่ใช่ 4:3 (เช่น 16:9) -> ตัดกลางภาพให้ได้ 4:3 ก่อนย่อ (ไม่ยืดภาพ)
        nw, nh = (h * 4 // 3, h) if w * 3 > h * 4 else (w, w * 3 // 4)
        x0, y0 = (w - nw) // 2, (h - nh) // 2
        im = im.crop((x0, y0, x0 + nw, y0 + nh))
    frames.append(im.resize((320, 240), Image.LANCZOS))
fps = 1000.0 / (sum(durs) / len(durs))
if NFR and NFR < len(frames):
    n0 = len(frames); frames = [frames[int(i * n0 / NFR)] for i in range(NFR)]; fps = fps * NFR / n0
def jpeg(im):
    b = io.BytesIO(); im.save(b, "JPEG", quality=Q, subsampling=SUB, progressive=False, optimize=True); return b.getvalue()
carr = lambda n, d: "const uint8_t %s[] PROGMEM = {%s};\n" % (n, ", ".join("0x%02X" % x for x in d))
SY = [0, 72, 144, 192, 240]                              # แถบ: 72/72/48/48 แถว (สไปรท์ในแรมสูงสุด 72 แถว)
strips = [[jpeg(f.crop((0, SY[k], 320, SY[k + 1]))) for k in range(4)] for f in frames]
n = len(frames)
os.makedirs(outdir, exist_ok=True)
with open(os.path.join(outdir, "dance.h"), "w") as o:
    o.write("// สร้างโดย tools/make_dance_page.py  %d เฟรม %.1f fps  4 แถบต่อเฟรม\n#pragma once\n#include <Arduino.h>\n" % (n, fps))
    o.write("const int DANCE_N = %d, DANCE_FPS = %d, DANCE_STRIPS = 4;\n" % (n, int(round(fps))))
    o.write("const int DANCE_STRIP_Y[5] = {0, 72, 144, 192, 240};\n")
    for i in range(n):
        for k in range(4): o.write(carr("dance_%d_%d" % (i, k), strips[i][k]))
    o.write("const uint8_t *const DANCE_PTR[%d] = {%s};\n" % (n * 4, ", ".join("dance_%d_%d" % (i, k) for i in range(n) for k in range(4))))
    o.write("const unsigned int DANCE_LEN[%d] = {%s};\n" % (n * 4, ", ".join(str(len(strips[i][k])) for i in range(n) for k in range(4))))
print("frames %d  fps %.1f  total JPEG %.0f KB" % (n, fps, sum(len(s) for fr in strips for s in fr) / 1024))
