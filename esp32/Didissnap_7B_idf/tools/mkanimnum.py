#!/usr/bin/env python3
"""ตัวเลขแบบไตเติลอนิเมะ (เหมือนหน้า Anime ของจอ 4.3): ไล่สีชมพู→ขาว + ขอบขาว + ขอบม่วง + เงาเยื้อง
   ทำเป็นรูป ARGB8888 ทีละตัวอักษร (LVGL 9 lv_image_dsc_t)
   python3 mkanimnum.py ChakraPetch-BoldItalic.ttf out.c"""
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage

PK, WH, OUT, SHD = (0xFF, 0x5C, 0xC4), (0xFF, 0xF4, 0xFC), (0x4A, 0x16, 0x70), (0x1C, 0x06, 0x2C)
CHARS = "0123456789.-N"
SETS = [("L", 112), ("M", 76)]

def mix(a, b, t):
    t = np.clip(t, 0, 1)[..., None]
    return np.array(a) * (1 - t) + np.array(b) * t

def glyph(font, ch, size):
    w1, w2, so = max(2, size // 28), max(5, size // 10), max(3, size // 14)
    pad = w2 + so + 2
    asc, desc = font.getmetrics()
    H = asc + desc + pad * 2
    x0, y0, x1, y1 = font.getbbox(ch, anchor='ls')
    adv = font.getlength(ch)
    W = (x1 - x0) + pad * 2
    im = Image.new('L', (W, H), 0)
    ImageDraw.Draw(im).text((pad - x0, pad + asc), ch, font=font, fill=255, anchor='ls')
    cov = np.asarray(im, dtype=np.float32) / 255.0
    ink = cov > 0.5
    dist = ndimage.distance_transform_edt(~ink)
    rows = np.where(ink.any(axis=1))[0]
    top, bot = (rows[0], rows[-1]) if len(rows) else (0, H - 1)
    jj = np.arange(H, dtype=np.float32)[:, None] * np.ones((1, W))
    fill = mix(PK, WH, (jj - top) / max(1, bot - top) * 1.15 - 0.05)
    rgb = np.zeros((H, W, 3)); a = np.zeros((H, W))
    sh = np.zeros_like(ink)
    sh[so:, so:] = (dist <= w2)[:-so, :-so]
    m = sh & ~(dist <= w2); rgb[m] = SHD; a[m] = 255
    m = (dist <= w2); rgb[m] = OUT; a[m] = 255
    m = (dist <= w1); rgb[m] = WH; a[m] = 255
    m = cov > 0.05
    rgb[m] = mix(WH, 0, 0)[0] * 0 + (np.array(WH) * (1 - cov[m][:, None]) + fill[m] * cov[m][:, None]); a[m] = 255
    ofs = x0 - pad                       # ภาพเริ่มที่ pen + ofs
    return W, H, rgb.astype(np.uint8), a.astype(np.uint8), int(round(adv)), int(ofs), w2

def main():
    ttf, outp = sys.argv[1], sys.argv[2]
    o = ['// สร้างด้วย tools/mkanimnum.py — ตัวเลขแบบไตเติลอนิเมะ (ARGB8888) · ฟอนต์ Chakra Petch (OFL)',
         '#include "lvgl.h"', '#include "animnum.h"', '']
    for name, size in SETS:
        font = ImageFont.truetype(ttf, size)
        entries = []
        for ci, ch in enumerate(CHARS):
            W, H, rgb, a, adv, ofs, w2 = glyph(font, ch, size)
            buf = np.dstack([rgb[..., 2], rgb[..., 1], rgb[..., 0], a]).astype(np.uint8).tobytes()
            sym = f'an_{name}_{ci}'
            o.append(f'static const uint8_t {sym}_px[] = {{' + ','.join(str(b) for b in buf) + '};')
            o.append(f'static const lv_image_dsc_t {sym} = {{.header = {{.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_ARGB8888, '
                     f'.flags = 0, .w = {W}, .h = {H}, .stride = {W * 4}}}, .data_size = {len(buf)}, .data = {sym}_px}};')
            entries.append((ch, sym, adv, ofs))
        o.append(f'static const an_glyph_t an_{name}_g[] = {{' + ', '.join(f"{{'{c}', &{s}, {adv}, {ofs}}}" for c, s, adv, ofs in entries) + '};')
        o.append(f'const an_set_t an_{name} = {{an_{name}_g, {len(entries)}, {H}, {w2}}};')
        o.append('')
    open(outp, 'w').write('\n'.join(o))

main()
