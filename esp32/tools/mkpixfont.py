# สร้าง pixfont.h: ตัวอักษร Press Start 2P (OFL 1.1) แบบ 1 บิต 8x8 ต่อช่อง ASCII 32-126
# ใช้กับหน้ารูป 13-16 (ขยายเป็นบล็อก k×k พิกเซล + ขอบดำ)
#   python3 mkpixfont.py /path/PressStart2P-Regular.ttf ../Didissnap_S3/pixfont.h
import sys
from PIL import ImageFont, Image, ImageDraw
f = ImageFont.truetype(sys.argv[1], 8)
rows = []
for c in range(32, 127):
    im = Image.new('L', (8, 8), 0)
    ImageDraw.Draw(im).text((0, 0), chr(c), font=f, fill=255)
    px = im.load()
    rows.append([sum(0x80 >> x for x in range(8) if px[x, y] > 127) for y in range(8)])
out = ['// สร้างอัตโนมัติ: tools/mkpixfont.py — Press Start 2P, Copyright 2012 The Press Start 2P Project Authors, SIL Open Font License 1.1',
       '#pragma once', '#include <stdint.h>',
       '// 8 แถวต่อตัว บิตซ้ายสุด = 0x80, ตัวแรก = ช่องว่าง (32)',
       'static const uint8_t PIX8[95][8] = {']
for c, g in zip(range(32, 127), rows):
    out.append('  {' + ','.join('0x%02x' % b for b in g) + '},' + ('  // ' + chr(c) if chr(c) not in '\\' else '  // backslash'))
out.append('};')
open(sys.argv[2], 'w').write('\n'.join(out) + '\n')
print('ok')
