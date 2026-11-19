// page_anime.h — หน้า 17: GIF ในกรอบ (เฟรมตามรอบ) + ตัวเลขแบบไตเติลอนิเมะ (ย้ายมาจากหน้า 9 ของจอ CYD)
//  ซ้าย: กรอบภาพ / แถบรอบช่องเฉียง / ความเร็ว / รอบ    ขวา: COOLANT / IAT / TPS + แถบเฉียง + ขีดค่าสูงสุด
//  ส่วนที่หนัก (ถอด JPEG, ทำตัวเลขขอบ/เงา) ทำใน animePrep() ครั้งเดียวต่อเฟรมก่อนวาด — ตอนวาดทีละแถบแค่คัดลอก
//  (ไฟล์นี้ include จากใน namespace pages ของ pages.h)
#if __has_include("animebox.h")
#include "animebox.h"          // สร้างเองด้วย tools/make_animebox.py จากไฟล์ GIF ของคุณ
#define HAS_ABOX 1
#else
#define HAS_ABOX 0              // ยังไม่มีไฟล์ = กรอบว่าง + ข้อความบอกวิธีใส่ GIF
#define ABOX_W 282
#define ABOX_H 150
#define ABOX_N 1
#endif

#define ANIME_RPM_LO 0       // ต่ำกว่านี้ = เฟรมแรก
#define ANIME_RPM_HI 5500    // ถึงนี้ = เฟรมสุดท้าย (เหยียบ = ภาพเดินหน้า ปล่อย = ถอยกลับ)

constexpr uint16_t Q_PK = rgb(0xFF5CC4), Q_WH = rgb(0xFFF4FC), Q_OUT = rgb(0x4A1670), Q_SHD = rgb(0x1C062C),
                   Q_OFF = rgb(0x221630), Q_LAB = rgb(0xC4B0DC), Q_RD = rgb(0xFF3246), Q_RDOFF = rgb(0x3C0E16),
                   Q_VIO = rgb(0x8C46E6);
static const int A_FX = 6, A_FY = 10, A_FW = 294, A_FH = 162;   // กรอบ (ภาพข้างใน 282x150)
static const int A_IX = 12, A_IY = 16;
static const int A_RX = 312;                                      // คอลัมน์ขวา

// ---- เฟรม GIF: ถอดเมื่อต้องใช้ เก็บไว้ใน PSRAM (ออกจากหน้านี้ = คืน)
static uint16_t* aFrames[ABOX_N] = {};
static const uint16_t* aCur = nullptr;
#ifdef BG_HOST
static uint16_t* hostAnime = nullptr;             // พรีวิวบนคอม: เฟรมดิบ RGB565 ทั้งชุด
#else
struct AJ { const uint8_t* src; uint32_t len, pos; uint16_t* dst; };
static uint8_t aJwork[4096];
static UINT aJin(JDEC* jd, BYTE* b, UINT n) {
  AJ* c = (AJ*)jd->device;
  if (c->pos + n > c->len) n = c->len - c->pos;
  if (b) memcpy(b, c->src + c->pos, n);
  c->pos += n;
  return n;
}
static UINT aJout(JDEC* jd, void* bmp, JRECT* r) {
  AJ* c = (AJ*)jd->device;
  const uint8_t* p = (const uint8_t*)bmp;
  for (int y = r->top; y <= r->bottom; y++)
    for (int x = r->left; x <= r->right; x++, p += 3)
      if (x < ABOX_W && y < ABOX_H) c->dst[y * ABOX_W + x] = ((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3);
  return 1;
}
#endif
static const uint16_t* aFrame(int i) {
  i = constrain(i, 0, ABOX_N - 1);
#ifdef BG_HOST
  return hostAnime ? hostAnime + (size_t)i * ABOX_W * ABOX_H : nullptr;
#else
#if !HAS_ABOX
  return nullptr;
#else
  if (aFrames[i]) return aFrames[i];
  uint16_t* b = (uint16_t*)heap_caps_malloc(ABOX_W * ABOX_H * 2, MALLOC_CAP_SPIRAM);
  if (!b) return aCur;                            // PSRAM เต็ม → ค้างภาพเดิม
  AJ c{ABOX_JPG[i], ABOX_LEN[i], 0, b};
  JDEC jd;
  if (jd_prepare(&jd, aJin, aJwork, sizeof(aJwork), &c) != JDR_OK || jd_decomp(&jd, aJout, 0) != JDR_OK) { free(b); return aCur; }
  aFrames[i] = b;
  return b;
#endif
#endif
}
inline void animeRelease() {
  for (int i = 0; i < ABOX_N; i++) if (aFrames[i]) { free(aFrames[i]); aFrames[i] = nullptr; }
  aCur = nullptr;
}

// ---- ตัวเลขแบบไตเติลอนิเมะ: ไล่สีชมพู→ขาว ขอบขาว + ขอบม่วง + เงาเยื้อง (สร้างเมื่อข้อความเปลี่ยน)
struct AText { char s[16] = ""; int w = 0, h = 0; bool hot = false; uint16_t* c = nullptr; uint8_t* a = nullptr; float size = 0; };
static uint16_t* aScratch = nullptr;              // 480 x 90 ไว้วาดตัวอักษรดิบ
static uint8_t* aDist = nullptr;
static const int A_SH = 90;
static inline uint16_t aMix(uint16_t a, uint16_t b, float t) { return gfx::lerp565(a, b, t); }
static void atMake(AText& t, const char* s, float size, bool hot) {
  if (t.c && t.size == size && t.hot == hot && !strcmp(t.s, s)) return;
#ifdef BG_HOST
  if (!aScratch) { aScratch = (uint16_t*)malloc(W * A_SH * 2); aDist = (uint8_t*)malloc(W * A_SH); }
#else
  if (!aScratch) { aScratch = (uint16_t*)heap_caps_malloc(W * A_SH * 2, MALLOC_CAP_SPIRAM); aDist = (uint8_t*)heap_caps_malloc(W * A_SH, MALLOC_CAP_SPIRAM); }
#endif
  if (!aScratch || !aDist) return;
  strlcpy(t.s, s, sizeof(t.s)); t.size = size; t.hot = hot;
  const int pad = 12;
  int tw = textWidth(s, size, 900, true);
  int w = min(W, tw + pad * 2 + 6), h = min(A_SH, (int)(size * 1.05f) + pad * 2);
  int base = pad + (int)(size * .78f);
  gfx::Ctx save = gfx::C();
  gfx::C().g = aScratch; gfx::C().y0 = 0; gfx::C().y1 = h;
  memset(aScratch, 0, W * h * 2);
  text(s, pad, base, size, 0xFFFF, LEFT, 900, true);
  gfx::C() = save;
  int top = h, bot = 0;
  for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
    bool ink = (aScratch[j * W + i] >> 11) >= 16;
    aDist[j * w + i] = ink ? 0 : 250;
    if (ink) { top = min(top, j); bot = max(bot, j); }
  }
  for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {        // ระยะห่างแบบ chamfer สองรอบ (ตรง 2 ทแยง 3)
    int v = aDist[j * w + i];
    if (i) v = min(v, aDist[j * w + i - 1] + 2);
    if (j) { v = min(v, aDist[(j - 1) * w + i] + 2); if (i) v = min(v, aDist[(j - 1) * w + i - 1] + 3); if (i < w - 1) v = min(v, aDist[(j - 1) * w + i + 1] + 3); }
    aDist[j * w + i] = v;
  }
  for (int j = h - 1; j >= 0; j--) for (int i = w - 1; i >= 0; i--) {
    int v = aDist[j * w + i];
    if (i < w - 1) v = min(v, aDist[j * w + i + 1] + 2);
    if (j < h - 1) { v = min(v, aDist[(j + 1) * w + i] + 2); if (i < w - 1) v = min(v, aDist[(j + 1) * w + i + 1] + 3); if (i) v = min(v, aDist[(j + 1) * w + i - 1] + 3); }
    aDist[j * w + i] = v;
  }
  if (!t.c || t.w * t.h < w * h) {
    free(t.c); free(t.a);
#ifdef BG_HOST
    t.c = (uint16_t*)malloc(w * h * 2); t.a = (uint8_t*)malloc(w * h);
#else
    t.c = (uint16_t*)heap_caps_malloc(w * h * 2, MALLOC_CAP_SPIRAM); t.a = (uint8_t*)heap_caps_malloc(w * h, MALLOC_CAP_SPIRAM);
#endif
  }
  t.w = w; t.h = h;
  uint16_t c0 = hot ? Q_RD : Q_PK, c1 = hot ? rgb(0xFFDCC8) : Q_WH;
  float span = bot > top ? (float)(bot - top) : 1.f;
  const int W1 = 4, W2 = 10, SO = 4;                                // ขอบขาว / ขอบม่วง / เงาเยื้อง (px x2)
  for (int j = 0; j < h; j++) {
    uint16_t fill = aMix(c0, c1, (j - top) / span * 1.15f - .05f);
    for (int i = 0; i < w; i++) {
      int k = j * w + i, d = aDist[k];
      float cov = (aScratch[j * W + i] >> 11) / 31.f;
      uint16_t c = 0; uint8_t a = 255;
      if (cov > .05f) c = aMix(Q_WH, fill, cov);
      else if (d <= W1) c = Q_WH;
      else if (d <= W2) c = Q_OUT;
      else if (i >= SO && j >= SO && aDist[(j - SO) * w + i - SO] <= W2) c = Q_SHD;
      else a = 0;
      t.c[k] = c; t.a[k] = a;
    }
  }
}
// วาด: align 0 ซ้าย 2 ขวา (x = ขอบ) · y = ขอบบนของกล่อง
static void atDraw(const AText& t, int x, int y, int align = 0) {
  if (!t.c) return;
  if (align == 2) x -= t.w;
  for (int j = max(0, GY0 - y); j < t.h && y + j < GY1; j++) {
    uint16_t* row = GBUF + (y + j) * W;
    for (int i = 0; i < t.w; i++) {
      int X = x + i;
      if ((unsigned)X >= (unsigned)W || !t.a[j * t.w + i]) continue;
      row[X] = t.c[j * t.w + i];
    }
  }
}

// ---- แถบช่องเฉียง
static void aSeg(int x, int y, int w, int h, uint16_t c) {
  for (int k = 0; k < h; k++) fill(x + (h - 1 - k) * 4 / max(h - 1, 1), y + k, w, 1, c);
}
static uint16_t aSegCol(float t) { return t < .55f ? aMix(Q_VIO, Q_PK, t / .55f) : aMix(Q_PK, Q_WH, (t - .55f) / .45f); }

// ---- ค่าทางขวา
struct ASlot { const char* lab; const char* unit; int dec; float lo, hi; };
static const ASlot A_SL[3] = {{"COOLANT", "C", 0, 40, 130}, {"IAT", "C", 0, -10, 80}, {"TPS", "%", 0, 0, 100}};
static AText aSpd, aRpm, aVal[3];
static float aVal1(int i) { return i == 0 ? D.ect : i == 1 ? D.iat : D.tps; }
static float aTpsMax = 0;
static float aPk1(int i) { return i == 0 ? D.pEct : i == 1 ? D.pIat : (aTpsMax = fmaxf(aTpsMax, D.tps)); }
static bool aHot(int i) { return i == 0 ? hot() : false; }

// เรียกจาก loop ก่อนวาดเฟรม (คอร์เดียว)
inline void animePrep(int page) {
  if (page != PG_ANIME) { if (aFrames[0] || aCur) animeRelease(); return; }
  float rpm = rpmF() * kMax() * 1000.f;
  int idx = (int)lroundf(clamp01((rpm - ANIME_RPM_LO) / (float)(ANIME_RPM_HI - ANIME_RPM_LO)) * (ABOX_N - 1));
  aCur = aFrame(idx);
  atMake(aSpd, f0(D.spd), 50, false);
  atMake(aRpm, f0(D.rpm), 34, over());
  for (int i = 0; i < 3; i++) {
    char b[12];
    snprintf(b, sizeof(b), A_SL[i].dec ? "%.1f" : "%.0f", aVal1(i));
    atMake(aVal[i], b, 34, aHot(i));
  }
}

inline void pAnime() {
  clear(BK);
  // กรอบภาพ: ชมพูนอก ม่วงใน + มุมขาว
  frame(A_FX, A_FY, A_FW, A_FH, Q_PK, 2);
  frame(A_FX + 3, A_FY + 3, A_FW - 6, A_FH - 6, Q_VIO, 1);
  const int L = 16;
  fill(A_FX - 2, A_FY - 2, L, 3, Q_WH); fill(A_FX - 2, A_FY - 2, 3, L, Q_WH);
  fill(A_FX + A_FW - L + 2, A_FY + A_FH - 1, L, 3, Q_WH); fill(A_FX + A_FW - 1, A_FY + A_FH - L + 2, 3, L, Q_WH);
  if (!aCur) {
    pxText("PUT YOUR GIF HERE", A_IX + ABOX_W / 2, A_IY + 60, 2, Q_LAB, CENTER, 1);
    pxText("tools/make_animebox.py", A_IX + ABOX_W / 2, A_IY + 86, 1, Q_LAB, CENTER, 1);
  } else
    for (int y = max(GY0, A_IY); y < min(GY1, A_IY + ABOX_H); y++) memcpy(GBUF + y * W + A_IX, aCur + (y - A_IY) * ABOX_W, ABOX_W * 2);
  // แถบรอบ 32 ช่องเฉียง (เกิน redline = แดง, ถึง redline กะพริบทั้งแถบ)
  const int N = 32, bx = 6, by = 182, bw = 294, bh = 14;
  float fr = rpmF(), red = redF(), pitch = (bw - 4) / (float)N;
  bool flash = over() && blink();
  for (int s = 0; s < N; s++) {
    float t = (float)s / N;
    bool lit = t < fr, rz = t >= red;
    uint16_t c = flash ? Q_RD : lit ? (rz ? Q_RD : aSegCol(t / max(red, .01f))) : (rz ? Q_RDOFF : Q_OFF);
    aSeg(bx + (int)(s * pitch), by, (int)pitch - 2, bh, c);
  }
  // ความเร็ว + รอบ
  text("km/h", 12, 214, 13, Q_LAB, LEFT, 600);
  atDraw(aSpd, 160, 204, 2);
  text("RPM", 172, 214, 13, Q_LAB, LEFT, 600);
  atDraw(aRpm, 306, 214, 2);
  // คอลัมน์ขวา 3 ช่อง
  for (int i = 0; i < 3; i++) {
    int y0 = 8 + i * 86;
    text(A_SL[i].lab, A_RX + 4, y0 + 14, 13, Q_LAB, LEFT, 700);
    text(A_SL[i].unit, 472, y0 + 14, 13, Q_LAB, RIGHT, 700);
    atDraw(aVal[i], 476, y0 + 8, 2);
    const int MB = 17, mx = A_RX + 2, mw = 160;
    float span = A_SL[i].hi - A_SL[i].lo, f = clamp01((aVal1(i) - A_SL[i].lo) / span), pk = clamp01((aPk1(i) - A_SL[i].lo) / span);
    float p = (mw - 4) / (float)MB;
    int pkSeg = min(MB - 1, (int)(pk * MB));
    for (int s = 0; s < MB; s++) {
      float t = (float)s / MB;
      uint16_t c = s == pkSeg ? Q_WH : t < f ? (aHot(i) ? Q_RD : aSegCol(t)) : Q_OFF;
      aSeg(mx + (int)(s * p), y0 + 66, (int)p - 2, 10, c);
    }
  }
  if (D.link != 2) obd(10, 266);
  dots();
}
