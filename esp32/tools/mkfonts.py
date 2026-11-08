# สร้าง fonts.h (4-bit anti-aliased) จาก Roboto Condensed ตามขนาดที่หน้าปัดใช้
from PIL import ImageFont, Image
import math
REG='/tmp/gf/ofl/robotocondensed/RobotoCondensed[wght].ttf'
ITA='/tmp/gf/ofl/robotocondensed/RobotoCondensed-Italic[wght].ttf'
def wclass(w): return 600 if w<=600 else (800 if w<=800 else 900)
combos=[(9,500,0),(9,600,0),(9,700,0),(9.5,700,0),(10,600,0),(10,700,0),(10.5,600,0),(11,500,0),(11,600,0),(11,700,0),(11.5,800,0),
(12,500,0),(12,600,0),(12,700,0),(13,500,0),(13,600,0),(13,700,0),(14,500,0),(14,600,0),(14,700,0),(14,800,0),(16,700,0),(16,700,1),
(16,800,0),(16,900,0),(17,700,1),(17,800,0),(19,600,0),(22,900,0),(24,900,0),(26,900,0),(26,900,1),(28,900,0),(30,900,0),(30,900,1),
(32,900,0),(36,900,0),(36,900,1),(40,900,0),(40,900,1),(44,900,0),(50,900,1),(58,900,1),(74,900,0),(76,900,1),(84,900,0),(92,900,0),
(130,900,0),(150,900,1),
# ไม่ได้ parse: vbox h*.5=30/800→900, gearBox 84, yb 31/24, P3 numbers 22/24 italic, menu 16/14/13/11
(24,900,1),(22,900,1),(31,900,0),(20,900,0)]
fonts={}
for sz,w,it in combos:
  k=(int(round(sz)),wclass(w),it); fonts[k]=1
fonts=sorted(fonts)
out=['// สร้างอัตโนมัติจาก Roboto Condensed (Apache 2.0 / OFL) — fw_tools/mkfonts.py','#pragma once','#include <stdint.h>',
'struct DGlyph { uint32_t off; uint8_t w, h; int8_t xo, yo; uint8_t adv; };',
'struct DFont { uint8_t size; uint16_t weight; uint8_t italic, first, last, ascent; const DGlyph* g; const uint8_t* bmp; };']
tbl=[];total=0
for (sz,w,it) in fonts:
  f=ImageFont.truetype(ITA if it else REG, sz)
  try: f.set_variation_by_axes([w])
  except Exception as e: print('var fail',e)
  first,last=(32,127) if sz<=36 else (37,78)
  asc,desc=f.getmetrics()
  bmp=bytearray();gl=[]
  for c in range(first,last+1):
    ch='°' if c==127 else chr(c)
    adv=int(round(f.getlength(ch)))
    bb=f.getbbox(ch)  # relative to (0,0) top-left at ascent line? PIL: origin top-left, baseline at asc
    x0,y0,x1,y1=bb
    w_,h_=max(0,x1-x0),max(0,y1-y0)
    off=len(bmp)
    if w_ and h_:
      im=Image.new('L',(w_,h_),0)
      from PIL import ImageDraw
      ImageDraw.Draw(im).text((-x0,-y0),ch,font=f,fill=255)
      px=im.load()
      for y in range(h_):
        row=[px[x,y]>>4 for x in range(w_)]
        if len(row)%2: row.append(0)
        for i in range(0,len(row),2): bmp.append(row[i]<<4|row[i+1])
    gl.append((off,min(w_,255),min(h_,255),max(-128,min(127,x0)),max(-128,min(127,y0-asc)),min(255,adv)))
  nm=f'F{sz}_{w}{"i" if it else ""}'
  out.append(f'static const uint8_t {nm}_bmp[] = {{'+','.join(str(b) for b in bmp)+'};')
  out.append(f'static const DGlyph {nm}_g[] = {{'+','.join('{%d,%d,%d,%d,%d,%d}'%g for g in gl)+'};')
  tbl.append(f'{{{sz},{w},{it},{first},{last},{asc},{nm}_g,{nm}_bmp}}')
  total+=len(bmp)+len(gl)*12
out.append('static const DFont FONTS[] = {'+',\n'.join(tbl)+'};')
out.append(f'static const int N_FONTS = {len(fonts)};')
open('/home/claude/didissnap/esp32/Didissnap_S3/fonts.h','w').write('\n'.join(out)+'\n')
print(len(fonts),'fonts',total//1024,'KB')
