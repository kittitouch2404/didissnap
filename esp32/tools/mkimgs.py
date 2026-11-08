# รูปเดิมของหน้า 9-11 (จาก CYD 320x240) ขยายเป็น 480x272 แบบเดียวกับพรีวิว แล้วฝังเป็น JPEG
from PIL import Image
import io
def cover(im):
  im=im.convert('RGB').resize((480,360),Image.LANCZOS)
  return im.crop((0,44,480,316))
def jpg(im,q=86):
  b=io.BytesIO(); im.save(b,'JPEG',quality=q,optimize=True,progressive=False,subsampling=2); return b.getvalue()
out=['// สร้างอัตโนมัติ: fw_tools/mkimgs.py — รูปเดิมหน้า 9 / 10 (20 เฟรม) / 11 + โลโก้ตอนเปิดเครื่อง','#pragma once','#include <stdint.h>']
def arr(name,data): out.append(f'static const uint8_t {name}[] = {{'+','.join(str(x) for x in data)+'};')
tot=0
a=jpg(cover(Image.open('/tmp/img/anime_jpg.jpg'))); arr('BI_PIC9',a); tot+=len(a)
p=jpg(cover(Image.open('/tmp/img/photo2_jpg.jpg'))); arr('BI_PIC11',p); tot+=len(p)
car=Image.open('/tmp/img/car_jpg.jpg').convert('RGB')
names=[]
for i in range(20):
  fr=car.copy(); fr.paste(Image.open(f'/tmp/img/road_f{i}.jpg').convert('RGB'),(0,100))
  d=jpg(cover(fr),84); arr(f'BI_RUN{i}',d); names.append(f'BI_RUN{i}'); tot+=len(d)
lg=Image.open('/tmp/img/logo_jpg.jpg').convert('RGB').resize((363,272),Image.LANCZOS)
canvas=Image.new('RGB',(480,272),(0,0,0)); canvas.paste(lg,((480-363)//2,0)); d=jpg(canvas,88); arr('BI_LOGO',d); tot+=len(d)
out.append('static const uint8_t* const BI_RUN[] = {'+','.join(names)+'};')
out.append('static const uint32_t BI_RUN_LEN[] = {'+','.join(f'sizeof({n})' for n in names)+'};')
out.append('static const int BI_RUN_N = 20;')
open('/home/claude/didissnap/esp32/Didissnap_S3/bimg.h','w').write('\n'.join(out)+'\n')
print(tot//1024,'KB')
