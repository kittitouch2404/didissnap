#!/usr/bin/env python3
# หน้า 2 จากภาพเคลื่อนไหว GIF 320x240: แถบบน (ใต้ตัวเลข) นิ่ง ส่วนล่างเล่นตามเฟรมของ GIF
# ใช้: python make_gif_page.py ภาพ.gif [โฟลเดอร์ปลายทาง] [--panel 100] [--shift 0] [--fade 0] [--suffix 2]   -> car.h + wheels.h + page_car_preview.gif
#   --frames N ลดจำนวนเฟรมเหลือ N (เลือกตามเวลาให้ห่างเท่ากัน เฟรมที่ตกกลางผสม 2 เฟรม) ความยาววนเท่าเดิม fps ลดลงตามสัดส่วน
#   --q Q      คุณภาพ JPEG ของส่วนเคลื่อนไหว (ค่าเริ่มต้น 88) ลดลงเพื่อประหยัดแฟลช
#   --suffix S ต่อท้ายชื่อไฟล์/ตัวแปร เช่น --suffix 2 -> car2.h + wheels2.h (car2_jpg, ROAD2_JPG ...) ใช้กับหน้ารถวิ่งหน้าที่ 2 (หน้า 12)
#   --panel N  ความสูงส่วนบนที่เป็นภาพนิ่ง (ใต้ตัวเลข) ส่วนที่เหลือด้านล่างเล่นตาม GIF (ดูว่าภาพขยับตั้งแต่แถวไหน แล้วตั้งให้ต่ำกว่านั้นนิดหน่อย)
#   --shift N  เลื่อนภาพลง N px (ท้องฟ้าส่วนบนถูกยืดเติมช่องว่าง) ให้รถทั้งคันอยู่ในส่วนที่เคลื่อนไหว
#   --fade N   จำนวนแถวที่กลืนจากแถบนิ่งเข้าภาพเคลื่อนไหว (ค่าเริ่มต้น 22)
import io, sys, os
import numpy as np
from PIL import Image, ImageSequence

PANEL_H = 72          # ความสูงพื้นมืดด้านบน (ใต้รอบ/ความร้อน/แถบรอบ) เป็นภาพนิ่ง
FADE = 22             # แถวที่ค่อย ๆ จางจากพื้นมืดลงไปเป็นภาพ
DARK = False          # True = ด้านบนเป็นพื้นมืด / False = ด้านบนเป็นภาพเฟรมแรก (นิ่ง) แล้วค่อย ๆ กลืนเข้าภาพที่เคลื่อนไหว
TOP_C, BOT_C = np.array([4, 6, 16], np.float32), np.array([10, 13, 32], np.float32)   # สีพื้นมืด บน -> ล่าง (ใช้เมื่อ DARK = True)

args = sys.argv[1:]
def opt(name, dflt):
    if name in args: i = args.index(name); v = int(args[i + 1]); del args[i:i + 2]; return v
    return dflt
SUF = ""
if "--suffix" in args: i = args.index("--suffix"); SUF = args[i + 1]; del args[i:i + 2]
NFR = opt("--frames", 0); BQ = opt("--q", 88)
PANEL_H = opt("--panel", PANEL_H); SHIFT = opt("--shift", 0); FADE = opt("--fade", FADE)
src, outdir = args[0], (args[1] if len(args) > 1 else ".")
def shift_down(img, n):                                   # เลื่อนลง n px: แถว 0..(PANEL_H+n) ใหม่ = ยืดจากแถว 0..PANEL_H เดิม
    if n <= 0: return img
    top = PANEL_H + n
    a = np.asarray(img); out = np.empty_like(a)
    out[top:] = a[PANEL_H:240 - n]
    out[:top] = np.asarray(Image.fromarray(a[:PANEL_H]).resize((320, top), Image.LANCZOS))
    return Image.fromarray(out)
g = Image.open(src)
frames, durs = [], []
for f in ImageSequence.Iterator(g):
    durs.append(f.info.get("duration", 40) or 40)
    frames.append(np.asarray(shift_down(f.convert("RGB").resize((320, 240), Image.LANCZOS), SHIFT)).astype(np.float32))
fps = 1000.0 / (sum(durs) / len(durs))
if NFR and NFR < len(frames):                              # ลดเฟรม: สุ่มตัวอย่างตามเวลา วนลูปต่อเนื่อง
    n0 = len(frames); res = []
    for i in range(NFR):
        t = i * n0 / NFR; k = int(t); w = t - k
        res.append(frames[k] if w < 1e-6 else frames[k] * (1 - w) + frames[(k + 1) % n0] * w)
    frames = res; fps = fps * NFR / n0

H = 240; y = np.arange(H, dtype=np.float32)
panel = TOP_C[None, :] + (BOT_C - TOP_C)[None, :] * np.clip(y / PANEL_H, 0, 1)[:, None]      # H x 3
FADE = max(FADE, 1)
t = np.clip((y - PANEL_H) / FADE, 0, 1); a = 1 - t * t * (3 - 2 * t)                        # 1 = มืดสนิท
if not DARK: panel = frames[0]                                                              # ด้านบน = ภาพเฟรมแรก (ไม่ขยับ ตัวเลขไม่กะพริบ)
def dark(fr):
    pn = panel if panel.ndim == 3 else panel[:, None, :]
    return fr * (1 - a[:, None, None]) + pn * a[:, None, None]
out = [np.clip(dark(f) + 0.5, 0, 255).astype(np.uint8) for f in frames]

def jpeg(arr, q, sub):
    b = io.BytesIO(); Image.fromarray(arr).save(b, "JPEG", quality=q, subsampling=sub, progressive=False, optimize=True); return b.getvalue()
carr = lambda n, d: "const uint8_t %s[] PROGMEM = {%s};\n" % (n, ", ".join("0x%02X" % x for x in d))
d0 = jpeg(out[0], 92, 0)
open(os.path.join(outdir, "car%s.h" % SUF), "w").write(
    "// หน้ารถวิ่ง: ภาพเต็มจอ 320x240 (เฟรมแรก) สร้างโดย tools/make_gif_page.py\n#pragma once\n#include <Arduino.h>\n"
    + carr("car%s_jpg" % SUF, d0) + "const uint32_t car%s_jpg_len = %d;\n" % (SUF, len(d0)))
o = ["// หน้ารถวิ่ง: ส่วนที่เคลื่อนไหว (ใต้พื้นมืด ลงไปถึงขอบล่าง) %d เฟรม จาก GIF สร้างโดย tools/make_gif_page.py\n#pragma once\n#include <Arduino.h>\n" % len(out),
     "#define DRIVE_WHEELS_IN_BAND 1   // ล้ออยู่ในเฟรมแล้ว\n#define BAND%s_FPS %.1ff           // เล่นเท่าความเร็วของ GIF (แก้ได้ที่ CAR_FPS ใน .ino)\n" % (SUF, fps)]
names, total = [], 0
for i, f in enumerate(out):
    d = jpeg(f[PANEL_H:], BQ, 2); total += len(d); names.append("road%s_f%d" % (SUF, i)); o.append(carr(names[-1], d))
o.append("const int ROAD%s_Y = %d;\nconst int N_ROAD%s_FRAMES = %d;\n" % (SUF, PANEL_H, SUF, len(out)) + ("const int ROAD_STEP = 4;\n" if not SUF else ""))
o.append("const uint8_t *const ROAD%s_JPG[%d] = {%s};\n" % (SUF, len(out), ", ".join(names)))
o.append("const unsigned int ROAD%s_LEN[%d] = {%s};\n" % (SUF, len(out), ", ".join("sizeof(%s)" % n for n in names)))
open(os.path.join(outdir, "wheels%s.h" % SUF), "w").write("".join(o))
pv = [Image.fromarray(f).resize((640, 480), Image.LANCZOS) for f in out]
pv[0].save(os.path.join(outdir, "page_car%s_preview.gif" % SUF), save_all=True, append_images=pv[1:], duration=int(round(1000 / fps)), loop=0)
print("frames %d, fps %.1f, car.h %d B, band %d B (avg %d)" % (len(out), fps, len(d0), total, total // len(out)))
