#!/usr/bin/env python3
"""สร้าง animebox.h (หน้า 17 ของจอ 4.3") จากไฟล์ GIF ของคุณ

ใช้:  pip install pillow
      python make_animebox.py ภาพ.gif ..      (ไฟล์ animebox.h จะไปอยู่โฟลเดอร์สเก็ตช์ Didissnap_S3)
ตัวเลือก: --q 82 (คุณภาพ JPEG)  --max-frames 40
ภาพถูกครอปกลางและย่อเป็น 282x150 · เฟรมแรก = รอบต่ำ เฟรมสุดท้าย = รอบสูง (ANIME_RPM_HI ใน page_anime.h)
"""
import argparse, io, os, sys
from PIL import Image, ImageSequence

W, H = 282, 150
ap = argparse.ArgumentParser()
ap.add_argument('gif'); ap.add_argument('outdir', nargs='?', default='.')
ap.add_argument('--q', type=int, default=82); ap.add_argument('--max-frames', type=int, default=40)
a = ap.parse_args()
im = Image.open(a.gif)
frames = []
for f in ImageSequence.Iterator(im):
    fr = f.convert('RGB'); w, h = fr.size; tw = int(h * W / H)
    box = ((w - tw) // 2, 0, (w - tw) // 2 + tw, h) if tw <= w else (0, (h - int(w * H / W)) // 2, w, (h - int(w * H / W)) // 2 + int(w * H / W))
    frames.append(fr.crop(box).resize((W, H), Image.LANCZOS))
if len(frames) > a.max_frames:
    step = len(frames) / a.max_frames
    frames = [frames[int(i * step)] for i in range(a.max_frames)]
out = ['// animebox.h — หน้า 17: GIF ในกรอบ %dx%d  %d เฟรม (สร้างด้วย tools/make_animebox.py จาก %s)' % (W, H, len(frames), os.path.basename(a.gif)),
       '#pragma once', '#include <stdint.h>', '#define ABOX_W %d' % W, '#define ABOX_H %d' % H, '#define ABOX_N %d' % len(frames)]
total = 0
for i, fr in enumerate(frames):
    b = io.BytesIO(); fr.save(b, 'JPEG', quality=a.q, optimize=True); d = b.getvalue(); total += len(d)
    out.append('static const uint8_t ABOX_%d[] = {%s};' % (i, ','.join(str(x) for x in d)))
out.append('static const uint8_t* const ABOX_JPG[ABOX_N] = {%s};' % ','.join('ABOX_%d' % i for i in range(len(frames))))
out.append('static const uint32_t ABOX_LEN[ABOX_N] = {%s};' % ','.join('sizeof(ABOX_%d)' % i for i in range(len(frames))))
dst = os.path.join(a.outdir, 'animebox.h')
open(dst, 'w').write('\n'.join(out) + '\n')
print('%s: %d frames, %d KB' % (dst, len(frames), total // 1024))
