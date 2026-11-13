import sys, io
from PIL import Image
src, out = sys.argv[1], sys.argv[2]
im = Image.open(src).convert("RGB")
if im.size != (320, 240):
    im = im.resize((320, 240), Image.LANCZOS)
b = io.BytesIO()
im.save(b, "JPEG", quality=92, subsampling=0, progressive=False, optimize=True)
d = b.getvalue()
lines = ["// ภาพโลโก้หลังหน้าเปิดเครื่อง 320x240 (JPEG baseline 4:4:4) สร้างโดย tools/make_logo.py",
         "#pragma once", "#include <Arduino.h>", "const uint8_t logo_jpg[] PROGMEM = {"]
for i in range(0, len(d), 16):
    lines.append("  " + ", ".join("0x%02X" % x for x in d[i:i+16]) + ",")
lines += ["};", "const uint32_t logo_jpg_len = %d;" % len(d), ""]
open(out, "w").write("\n".join(lines))
print(len(d), "bytes")
