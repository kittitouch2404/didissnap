#!/usr/bin/env python3
"""
แปลงรูปถ่ายเป็นภาพพื้นหลังแนว 16-bit (พิกเซลอาร์ตแบบเกมยุค SNES/Mega Drive) ขนาด 320x240
-> ได้ไฟล์ <name>.h (ตัวแปร <name>_jpg, <name>_jpg_len) ใช้แทน anime.h / car.h / page3.h ได้เลย

ขั้นตอน: ครอป 4:3 -> ย่อเหลือ 160x120 (1 พิกเซลเกม = 2x2 พิกเซลจอ) -> เร่งสี/คอนทราสต์
        -> ลดสีเหลือ N สี (ค่าสีปัดเป็น 5 บิตต่อช่องแบบเครื่อง 16-bit) -> ขยายกลับ 320x240 แบบไม่เบลอ
        -> JPEG baseline 4:4:4 คุณภาพสูง (ขอบพิกเซลคม บอร์ดถอดรหัสได้)

ตัวอย่าง:
    pip install pillow
    python make_pixel_bg.py 01_GE.jpg --name anime                    # หน้า 1
    python make_pixel_bg.py 02_GE.jpg --name car                      # หน้า 2
    python make_pixel_bg.py 03_GE.jpg --name page3                    # หน้า 3

ตัวเลือก:
    --colors 48      จำนวนสี (16-bit จริง ๆ ราว 32-64 สีต่อฉาก ยิ่งน้อยยิ่งดูเป็นเกม)
    --pixel 2        ขนาดพิกเซลเกม (1 = ละเอียด, 2 = แนวเกม, 3-4 = หยาบมาก)
    --cx 0.5 --cy 0.5  จุดกึ่งกลางครอป (0-1) ;  --zoom 1.0  ซูมเข้า (>1)
    --left-dim 0.35  ทำฝั่งซ้ายมืดลงเล็กน้อยให้ตัวเลขอ่านง่าย (0 = ไม่ทำ)
    --preview out.png  บันทึกภาพผลลัพธ์ไว้ดู
"""
import argparse, io
from PIL import Image, ImageEnhance, ImageFilter

ap = argparse.ArgumentParser()
ap.add_argument("image")
ap.add_argument("--name", default="anime")
ap.add_argument("--colors", type=int, default=48)
ap.add_argument("--pixel", type=int, default=2)
ap.add_argument("--cx", type=float, default=0.5)
ap.add_argument("--cy", type=float, default=0.5)
ap.add_argument("--zoom", type=float, default=1.0)
ap.add_argument("--sat", type=float, default=1.35)
ap.add_argument("--contrast", type=float, default=1.12)
ap.add_argument("--left-dim", type=float, default=0.0)
ap.add_argument("--quality", type=int, default=95)
ap.add_argument("--out", default=None, help="โฟลเดอร์/ไฟล์ .h ปลายทาง (ค่าเริ่มต้น <name>.h)")
ap.add_argument("--preview", default=None)
a = ap.parse_args()

W, H = 320, 240
im = Image.open(a.image).convert("RGB")
iw, ih = im.size

# ---- ครอป 4:3 ตามจุดกึ่งกลาง/ซูม ----
cw = min(iw, ih * 4 / 3) / a.zoom
ch = cw * 3 / 4
l = min(max(a.cx * iw - cw / 2, 0), iw - cw)
t = min(max(a.cy * ih - ch / 2, 0), ih - ch)
im = im.crop((round(l), round(t), round(l + cw), round(t + ch)))

# ---- ย่อเป็นความละเอียดเกม + แต่งสี ----
gw, gh = W // a.pixel, H // a.pixel
im = im.filter(ImageFilter.UnsharpMask(radius=2, percent=80, threshold=2))
im = im.resize((gw, gh), Image.LANCZOS)
im = ImageEnhance.Color(im).enhance(a.sat)
im = ImageEnhance.Contrast(im).enhance(a.contrast)

# ---- ทำฝั่งซ้ายมืดลงให้ตัวเลขอ่านง่าย (ทำก่อนลดสี: จานสีจะไล่ระดับเอง ไม่เกิดแถบ) ----
if a.left_dim > 0:
    px = im.load()
    span = gw * 0.55
    for x in range(int(span)):
        f = a.left_dim * (1 - x / span)
        for y in range(gh):
            r, g, b = px[x, y]
            px[x, y] = (int(r * (1 - f)), int(g * (1 - f)), int(b * (1 - f)))

# ---- ลดสี: จานสีจำกัด ค่าสีปัด 5 บิต (RGB555 แบบเครื่อง 16-bit) ไม่ดิทเธอร์ = ก้อนสีเรียบแบบพิกเซลอาร์ต ----
q = im.quantize(colors=a.colors, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
pal = q.getpalette()[: a.colors * 3]
pal = [min(255, (v >> 3) * 255 // 31) for v in pal]
q.putpalette(pal + [0] * (768 - len(pal)))
im = q.convert("RGB")

im = im.resize((W, H), Image.NEAREST)

b = io.BytesIO()
im.save(b, "JPEG", quality=a.quality, progressive=False, subsampling=0, optimize=False)
data = b.getvalue()
rows = ["  " + ", ".join("0x%02X" % v for v in data[i:i + 16]) + "," for i in range(0, len(data), 16)]
out = a.out or (a.name + ".h")
with open(out, "w") as f:
    style = "32-bit (ละเอียดเต็ม %d สี)" % a.colors if a.pixel == 1 else "16-bit pixel art (พิกเซล %dx%d, %d สี)" % (a.pixel, a.pixel, a.colors)
    f.write("// พื้นหลังแนว %s 320x240 (JPEG baseline 4:4:4) สร้างโดย make_pixel_bg.py จาก %s\n" % (style, a.image.split("/")[-1]))
    f.write("#pragma once\n#include <Arduino.h>\n")
    f.write("const uint8_t %s_jpg[] PROGMEM = {\n%s\n};\n" % (a.name, "\n".join(rows)))
    f.write("const unsigned int %s_jpg_len = %d;\n" % (a.name, len(data)))
if a.preview:
    Image.open(io.BytesIO(data)).save(a.preview)
print("wrote %s (%d bytes JPEG, %d colors, pixel %d)" % (out, len(data), a.colors, a.pixel))
