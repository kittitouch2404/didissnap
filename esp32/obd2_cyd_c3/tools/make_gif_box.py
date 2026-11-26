#!/usr/bin/env python3
# หน้า 9: GIF ในกรอบสี่เหลี่ยม (188x100) -> gifbox.h (แต่ละเฟรมเป็น JPEG เล็ก ๆ)
# ใช้: python make_gif_box.py ภาพ.gif [โฟลเดอร์ปลายทาง] [ตัวเลือก]
#   --zoom Z    ขยายภาพ (1.0 = ย่อให้ความกว้างพอดีกรอบ, 1.3 = ซูมเข้า 30%)
#   --x X       ตำแหน่งแนวนอนของจุดกลางที่จะครอป 0.0 (ซ้ายสุด) - 1.0 (ขวาสุด) ค่าเริ่มต้น 0.5
#   --y Y       ตำแหน่งแนวตั้ง 0.0 (บนสุด) - 1.0 (ล่างสุด) ค่าเริ่มต้น 0.5
#   --frames N  ลดจำนวนเฟรมเหลือ N (ความยาววนเท่าเดิม)
#   --q Q       คุณภาพ JPEG (ค่าเริ่มต้น 88)
#   --tween T   เพิ่มเฟรมคั่นกลาง T เฟรมระหว่างทุกคู่เฟรม (ผสมภาพ) ให้ภาพที่ขยับตามรอบดูถี่/ลื่นขึ้น เช่น --tween 1 = เฟรมเกือบ 2 เท่า
#   --even N    จัดเฟรมใหม่ N เฟรม ให้ภาพเปลี่ยน "เท่า ๆ กัน" ทุกเฟรม (วัดจากความต่างของภาพ ตัดช่วงที่ภาพค้าง/กระโดด)
#               เหมาะกับโหมดภาพค้างตามรอบ: กวาดรอบแล้วภาพขยับสม่ำเสมอ ไม่หน่วงแล้วกระโดด เช่น --even 50
#   --mix M     วิธีทำเฟรมคั่นกลางของ --even / --tween:
#                 flow  (ค่าเริ่มต้น) ขยับภาพตามทิศการเคลื่อนไหว ไม่ซ้อน (ต้อง pip install opencv-python ถ้าไม่มีจะใช้ pick)
#                 pick  ใช้เฟรมจริงที่ใกล้ที่สุด ไม่ซ้อนแน่นอน แต่ช่วงที่ภาพกระโดดจะยังกระโดด
#                 blend ผสมสองเฟรม (ภาพซ้อนตอนขยับเยอะ)
#   --size WxH  ขนาดภาพ (ค่าเริ่มต้น 188x100 = แบบ A)  แบบ B (ภาพใหญ่) ใช้ --size 308x164
#   --suffix S  สำหรับหน้า GIF หน้าถัดไป: --suffix 2 -> gifbox2.h (หน้า 10), --suffix 3 -> gifbox3.h (หน้า 11)
# ตัวอย่าง: python make_gif_box.py car.gif . --zoom 1.1 --y 0.6
import io, sys, os
import numpy as np
from PIL import Image, ImageSequence, ImageEnhance

W, H = 188, 100
args = sys.argv[1:]
if "--size" in args:
    i = args.index("--size"); W, H = (int(v) for v in args[i + 1].lower().split("x")); del args[i:i + 2]
def opt(name, dflt, typ=float):
    if name in args:
        i = args.index(name); v = typ(args[i + 1]); del args[i:i + 2]; return v
    return dflt
ZOOM = opt("--zoom", 1.0); CX = opt("--x", 0.5); CY = opt("--y", 0.5)
NFR = opt("--frames", 0, int); Q = opt("--q", 88, int); TW = opt("--tween", 0, int); EV = opt("--even", 0, int)
SUF = ""
if "--suffix" in args: i = args.index("--suffix"); SUF = args[i + 1]; del args[i:i + 2]
MIX = "flow"
if "--mix" in args: i = args.index("--mix"); MIX = args[i + 1]; del args[i:i + 2]
try:
    import cv2
except ImportError:
    cv2 = None
    if MIX == "flow": print("ไม่มี opencv-python -> ใช้ --mix pick (ติดตั้ง: pip install opencv-python)"); MIX = "pick"
_flow = {}
def inter(a, b, w, key):                              # เฟรมระหว่าง a กับ b ที่สัดส่วน w (0 = a, 1 = b)
    if w < 1e-3: return a
    if w > 1 - 1e-3: return b
    if MIX == "pick": return a if w < 0.5 else b
    if MIX == "blend": return a * (1 - w) + b * w
    if key not in _flow:                              # optical flow สองทาง (คำนวณครั้งเดียวต่อคู่เฟรม)
        ga = cv2.cvtColor(np.clip(a, 0, 255).astype(np.uint8), cv2.COLOR_RGB2GRAY)
        gb = cv2.cvtColor(np.clip(b, 0, 255).astype(np.uint8), cv2.COLOR_RGB2GRAY)
        prm = (None, 0.5, 4, 21, 5, 7, 1.5, 0)
        _flow[key] = (cv2.calcOpticalFlowFarneback(ga, gb, *prm), cv2.calcOpticalFlowFarneback(gb, ga, *prm))
    fab, fba = _flow[key]
    h, wd = a.shape[:2]
    gx, gy = np.meshgrid(np.arange(wd, dtype=np.float32), np.arange(h, dtype=np.float32))
    wa = cv2.remap(a.astype(np.float32), gx + fba[..., 0] * w, gy + fba[..., 1] * w, cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)
    wb = cv2.remap(b.astype(np.float32), gx + fab[..., 0] * (1 - w), gy + fab[..., 1] * (1 - w), cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)
    return wa * (1 - w) + wb * w
src, outdir = args[0], (args[1] if len(args) > 1 else ".")

g = Image.open(src)
frames, durs = [], []
for f in ImageSequence.Iterator(g):
    durs.append(f.info.get("duration", 50) or 50)
    frames.append(f.convert("RGB"))
sw, sh = frames[0].size
sc = max(W / sw, H / sh) * ZOOM                      # ย่อ/ขยายให้เต็มกรอบ แล้วครอปส่วนเกิน
nw, nh = max(W, round(sw * sc)), max(H, round(sh * sc))
x0 = round((nw - W) * min(max(CX, 0), 1)); y0 = round((nh - H) * min(max(CY, 0), 1))
def box(im):
    im = im.resize((nw, nh), Image.LANCZOS).crop((x0, y0, x0 + W, y0 + H))
    return ImageEnhance.Sharpness(im).enhance(1.15)
out = [np.asarray(box(f)).astype(np.float32) for f in frames]
ms = sum(durs) / len(durs)
dms = list(durs)                                      # เวลาของแต่ละเฟรมตามไฟล์ (เช่น เฟรมสุดท้ายค้างนานกว่า)
if NFR and NFR < len(out):                            # ลดเฟรมตามเวลา (เฟรมที่ตกกลางผสม 2 เฟรม) วนลูปต่อเนื่อง
    n0 = len(out); res = []
    for i in range(NFR):
        t = i * n0 / NFR; k = int(t); w = t - k
        res.append(out[k] if w < 1e-6 else out[k] * (1 - w) + out[(k + 1) % n0] * w)
    out = res; ms = ms * n0 / NFR; dms = [ms] * NFR
if EV > 1 and len(out) > 1:                           # จัดเฟรมตามระยะการเปลี่ยนของภาพ (motion เท่ากันทุกเฟรม)
    d = [float(np.mean(np.abs(out[i + 1] - out[i]))) + 1e-3 for i in range(len(out) - 1)]
    cum = [0.0]
    for x in d: cum.append(cum[-1] + x)
    res = []
    for k in range(EV):
        t = cum[-1] * k / (EV - 1); i = 0
        while i < len(d) - 1 and cum[i + 1] < t: i += 1
        w = min(max((t - cum[i]) / d[i], 0.0), 1.0)
        res.append(inter(out[i], out[i + 1], w, i))
    out = res; ms = sum(dms) / EV; dms = [ms] * EV; TW = 0
if TW > 0:                                            # เฟรมคั่นกลาง: ผสม 2 เฟรมติดกันตามสัดส่วน
    res, rd = [], []
    for k in range(len(out)):
        res.append(out[k]); rd.append(dms[k] / (TW + 1))
        if k + 1 < len(out):
            for j in range(1, TW + 1):
                w = j / (TW + 1); res.append(inter(out[k], out[k + 1], w, k)); rd.append(dms[k] / (TW + 1))
    out, dms = res, rd; ms = ms / (TW + 1)
ms = max(20, int(round(ms)))                          # เร็วสุด ~25 fps (ถอด JPEG ในกรอบได้ทัน)
imgs = [Image.fromarray(np.clip(a + 0.5, 0, 255).astype(np.uint8)) for a in out]

def jpeg(im):
    b = io.BytesIO(); im.save(b, "JPEG", quality=Q, subsampling=0, progressive=False, optimize=True); return b.getvalue()
carr = lambda n, d: "const uint8_t %s[] PROGMEM = {%s};\n" % (n, ", ".join("0x%02X" % x for x in d))
data = [jpeg(im) for im in imgs]
U, L = "GBOX" + SUF, "gbox" + SUF
with open(os.path.join(outdir, "gifbox%s.h" % SUF), "w") as f:
    f.write("// หน้า GIF (gifbox%s.h): กรอบ %dx%d  %d เฟรม  %d ms/เฟรม  (สร้างด้วย tools/make_gif_box.py จาก %s)\n#pragma once\n"
            % (SUF, W, H, len(data), ms, os.path.basename(src)))
    f.write("#define %s_W %d\n#define %s_H %d\n#define %s_N %d\n#define %s_MS %d\n" % (U, W, U, H, U, len(data), U, ms))
    for i, d in enumerate(data): f.write(carr("%s_%d" % (L, i), d))
    f.write("const uint8_t *const %s_JPG[%s_N] = {%s};\n" % (U, U, ", ".join("%s_%d" % (L, i) for i in range(len(data)))))
    f.write("const uint32_t %s_LEN[%s_N] = {%s};\n" % (U, U, ", ".join(str(len(d)) for d in data)))
    f.write("#define %s_HAS_DMS 1\nconst uint16_t %s_DMS[%s_N] = {%s};   // ms ของแต่ละเฟรม\n" % (U, U, U, ", ".join(str(max(20, int(round(d)))) for d in dms)))
imgs[0].save(os.path.join(outdir, "gifbox%s_preview.gif" % SUF), save_all=True, append_images=imgs[1:], duration=ms, loop=0)
print("gifbox%s.h: %d frames, %d ms/frame, %.1f KB" % (SUF, len(data), ms, sum(len(d) for d in data) / 1024))
