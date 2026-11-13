#!/usr/bin/env python3
"""
สร้างหน้า 1 แบบ "เปลี่ยนหน้าตัวละครตามรอบเครื่อง" -> face_bg.h (ภาพฉากหลัง+ตัวละคร 320x240 เป็น JPEG เดียว)
และแพตช์ใบหน้า N แบบ (เล็กมาก) วาดทับตามช่วงรอบ

ตัวอย่าง:
  python make_face_page.py --bg beach.jpeg --faces f1.jpeg f2.jpeg ... f7.jpeg
ค่าเริ่มต้น (ตั้งตามที่เลือกไว้): --zoom 0.270 --shiftx 39 --shifty -18
ลำดับ --faces = ใบหน้า 1..7 (ใบหน้าที่ใช้ตามช่วงรอบกำหนดใน .ino: FACE_FOR_RANGE)
ต้องมี: pillow, numpy, scipy
"""
import argparse, io, sys
import numpy as np
from PIL import Image
from scipy import ndimage as ndi

ap = argparse.ArgumentParser()
ap.add_argument("--bg", required=True)
ap.add_argument("--faces", nargs="+", required=True, help="รูปใบหน้า N แบบ เรียงจากรอบต่ำไปสูง (แบ่งช่วงรอบเท่า ๆ กันตาม maxRpm)")
ap.add_argument("--tops", nargs="+", type=int, default=None,
                help="แถวบนสุดที่ใช้ตัดของแต่ละรูป (จัดแนวให้ตัวละครซ้อนกันพอดี) ค่าเริ่มต้น 289 ทุกรูป = สกรีนช็อตขนาด 1170x2532")
ap.add_argument("--x0", type=int, default=23); ap.add_argument("--cw", type=int, default=1137); ap.add_argument("--ch", type=int, default=1563)
ap.add_argument("--zoom", type=float, default=0.270)
ap.add_argument("--shiftx", type=int, default=39)
ap.add_argument("--shifty", type=int, default=-18)
ap.add_argument("--bgshift", type=float, default=0.5, help="ตำแหน่งตัดฉากหลังแนวนอน 0..1")
ap.add_argument("--facebox", nargs=4, type=int, default=[170, 46, 232, 104], help="x0 y0 x1 y1 บนจอ: บริเวณที่ใบหน้าเปลี่ยน (นอกกรอบนี้ใช้ภาพฐานเหมือนกันหมด)")
ap.add_argument("--thr", type=int, default=25)
ap.add_argument("--q", type=int, default=90)
ap.add_argument("--out", default=".")
a = ap.parse_args()
if a.tops is None: a.tops = [289] * len(a.faces)
assert len(a.tops) == len(a.faces)
NF = len(a.faces)
W, H = 320, 240

def cover(path):
    im = Image.open(path).convert("RGB"); iw, ih = im.size
    s = max(W / iw, H / ih); nw, nh = round(iw * s), round(ih * s)
    im = im.resize((nw, nh), Image.LANCZOS)
    l = round((nw - W) * a.bgshift); t = round((nh - H) * 0.5)
    return np.asarray(im.crop((l, t, l + W, t + H)), dtype=np.float32)

def cutout(path, top):
    im = Image.open(path).convert("RGB")
    can = Image.new("RGB", (a.cw, a.ch), (0, 0, 0))
    can.paste(im.crop((a.x0, top, a.x0 + a.cw, top + a.ch)), (0, 0))
    c = np.asarray(can, dtype=np.float32)
    mx = c.max(axis=2)
    low = mx <= 24
    lab, n = ndi.label(low)
    sizes = ndi.sum(low, lab, range(1, n + 1))
    border = set(np.unique(np.concatenate([lab[0], lab[-1], lab[:, 0], lab[:, -1]]))) - {0}
    bgc = np.zeros_like(low)
    for i, sz in enumerate(sizes, 1):
        if i in border or sz > 400:
            bgc |= lab == i
    alpha = np.where(bgc, np.clip((mx - 3) / 20.0, 0, 1), 1.0)
    col = np.where(bgc[..., None], c / np.maximum(alpha, 0.05)[..., None], c)
    col = np.clip(col, 0, 255)
    return col, alpha

def scale_rgba(col, alpha, s):
    nw, nh = round(a.cw * s), round(a.ch * s)
    pm = np.dstack([col * alpha[..., None], alpha * 255.0])        # premultiplied
    ch = [np.asarray(Image.fromarray(pm[..., i].astype(np.float32), mode="F").resize((nw, nh), Image.LANCZOS)) for i in range(4)]
    pm = np.dstack(ch)
    al = np.clip(pm[..., 3] / 255.0, 0, 1)
    return pm[..., :3], al                                          # rgb premultiplied, alpha

beach = cover(a.bg)
ox = round(W - a.cw * a.zoom + a.shiftx); oy = a.shifty
frames = []
for f, t in zip(a.faces, a.tops):
    col, al = cutout(f, t)
    pm, al = scale_rgba(col, al, a.zoom)
    h, w = al.shape
    fr = beach.copy()
    x0, y0 = max(ox, 0), max(oy, 0); x1, y1 = min(ox + w, W), min(oy + h, H)
    sx, sy = x0 - ox, y0 - oy
    reg = fr[y0:y1, x0:x1]
    A = al[sy:sy + (y1 - y0), sx:sx + (x1 - x0)][..., None]
    P = pm[sy:sy + (y1 - y0), sx:sx + (x1 - x0)]
    fr[y0:y1, x0:x1] = P + reg * (1 - A)
    frames.append(np.clip(fr, 0, 255).astype(np.uint8))

diff = np.zeros((H, W), bool)
fx0, fy0, fx1, fy1 = a.facebox
for f in frames[1:]:
    d = np.abs(f.astype(int) - frames[0].astype(int)).max(axis=2) > a.thr
    d[:fy0] = False; d[fy1:] = False; d[:, :fx0] = False; d[:, fx1:] = False
    diff |= d
ys, xs = np.where(diff)
bx0, bx1, by0, by1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
px0, py0 = bx0 // 16 * 16, by0 // 16 * 16                          # ชิดกริด MCU 16 px เพื่อไม่ให้เห็นรอยต่อ
px1, py1 = -(-bx1 // 16) * 16, -(-by1 // 16) * 16
px1, py1 = min(px1, W), min(py1, H)
bx0, bx1, by0, by1 = int(bx0), int(bx1), int(by0), int(by1)
print("face diff bbox", (bx0, by0, bx1, by1), "-> patch rect", (px0, py0, px1 - px0, py1 - py0))

def jpg(arr, sub):
    b = io.BytesIO(); Image.fromarray(arr).save(b, "JPEG", quality=a.q, subsampling=sub, progressive=False); return b.getvalue()
base = jpg(frames[0], 2)
patches = [jpg(f[py0:py1, px0:px1], 2) for f in frames]
print("base", len(base), "bytes; patches", [len(p) for p in patches])

def arr(name, data):
    return "const uint8_t %s[] PROGMEM = {%s};\n" % (name, ", ".join("0x%02X" % b for b in data))
with open(a.out + "/face_bg.h", "w") as f:
    f.write("// ฉากหลัง+ตัวละคร หน้า 1 (สร้างโดย make_face_page.py) zoom=%.3f shift=(%d,%d)\n#pragma once\n#include <Arduino.h>\n" % (a.zoom, a.shiftx, a.shifty))
    f.write(arr("face_bg_jpg", base))
    f.write("const unsigned int face_bg_len = sizeof(face_bg_jpg);\n")
with open(a.out + "/faces.h", "w") as f:
    f.write("// แพตช์ใบหน้า %d แบบ (สร้างโดย make_face_page.py)\n#pragma once" % NF + "\n#include <Arduino.h>\n")
    f.write("const int N_FACES = %d;\nconst int FACE_X = %d, FACE_Y = %d, FACE_W = %d, FACE_H = %d;\n" % (NF, px0, py0, px1 - px0, py1 - py0))
    for i, p in enumerate(patches): f.write(arr("face_jpg%d" % (i + 1), p))
    f.write("const uint8_t *const FACE_JPG[N_FACES] = {%s};\n" % ", ".join("face_jpg%d" % (i + 1) for i in range(NF)))
    f.write("const unsigned int FACE_LEN[N_FACES] = {%s};\n" % ", ".join("sizeof(face_jpg%d)" % (i + 1) for i in range(NF)))
for i, fr in enumerate(frames):
    Image.fromarray(fr).save(a.out + "/face_frame%d.png" % (i + 1))
# เก็บ JPEG เป็นไฟล์ด้วย สำหรับพรีวิว
open(a.out + "/face_bg.jpg", "wb").write(base)
for i, p in enumerate(patches): open(a.out + "/face_patch%d.jpg" % (i + 1), "wb").write(p)
open(a.out + "/face_rect.txt", "w").write("%d %d %d %d\n" % (px0, py0, px1 - px0, py1 - py0))
