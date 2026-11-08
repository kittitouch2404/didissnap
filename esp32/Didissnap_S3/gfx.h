// gfx.h — ชุดวาดภาพลงเฟรมบัฟเฟอร์ RGB565 (สี่เหลี่ยม วงกลม ส่วนโค้ง เส้น ตัวอักษร 7-segment)
#pragma once
#include <Arduino.h>
#include <math.h>
#include "fonts.h"

namespace gfx {

static const int W = 480, H = 272;
// สถานะการวาด แยกต่อคอร์ (วาดพร้อมกัน 2 คอร์ คนละแถบ)
//  ไม่ใช้ thread_local: เวอร์ชันที่ใช้ thread_local ดับตั้งแต่เปิดเครื่อง (น่าจะเพราะกินสแตกของทุกงานในระบบ)
struct Ctx {
  uint16_t* g;          // บัฟเฟอร์ที่กำลังวาด (ตำแหน่ง g[y*W+x] ใช้ได้เฉพาะแถว y0..y1-1)
  int y0, y1;           // แถวที่วาดได้ตอนนี้
  const DFont* lastPick;
  float lastSize;
  int lastW;
  bool lastIt;
};
static Ctx ctxs[2] = {{nullptr, 0, H, nullptr, -1, -1, false}, {nullptr, 0, H, nullptr, -1, -1, false}};
// เลขคอร์ (อ่านรีจิสเตอร์ PRID — ไม่ใส่ volatile ให้คอมไพเลอร์อ่านครั้งเดียวต่อฟังก์ชัน)
static inline int core() {
#if defined(__XTENSA__)
  int id;
  __asm__("rsr.prid %0\n extui %0, %0, 13, 1" : "=r"(id));
  return id;
#else
  return 0;
#endif
}
static inline Ctx& C() { return ctxs[core()]; }
#define GBUF (::gfx::C().g)
#define GY0 (::gfx::C().y0)
#define GY1 (::gfx::C().y1)
// วาดลงบัฟเฟอร์เต็มจอ
inline void target(uint16_t* full) { GBUF = full; GY0 = 0; GY1 = H; }
// วาดลงแถบ: buf เก็บแถว y0..y1-1
inline void strip(uint16_t* buf, int y0, int y1) { GBUF = buf - y0 * W; GY0 = y0; GY1 = y1; }
// เวลาของเฟรมนี้ (ทุกแถบใช้ค่าเดียวกัน ไฟกะพริบจะไม่ขาดครึ่งจอ)
static volatile uint32_t frameMs = 0;
inline uint32_t nowMs() { return frameMs ? frameMs : millis(); }

constexpr uint16_t rgb(uint32_t hex) {
  return (uint16_t)((((hex >> 16) & 0xF8) << 8) | (((hex >> 8) & 0xFC) << 3) | ((hex & 0xFF) >> 3));
}

static inline uint16_t blend(uint16_t fg, uint16_t bg, uint8_t a) {
  if (a >= 250) return fg;
  if (a < 6) return bg;
  uint32_t f = (fg | ((uint32_t)fg << 16)) & 0x07E0F81F;
  uint32_t b = (bg | ((uint32_t)bg << 16)) & 0x07E0F81F;
  uint32_t r = (b + (((f - b) * (uint32_t)(a >> 3)) >> 5)) & 0x07E0F81F;
  return (uint16_t)(r | (r >> 16));
}
static inline void px(int x, int y, uint16_t c) {
  if ((unsigned)x < (unsigned)W && y >= GY0 && y < GY1) GBUF[y * W + x] = c;
}
static inline void pxa(int x, int y, uint16_t c, uint8_t a) {
  if ((unsigned)x < (unsigned)W && y >= GY0 && y < GY1) {
    uint16_t* p = &GBUF[y * W + x];
    *p = blend(c, *p, a);
  }
}

// ---------------------------------------------------------------- สี่เหลี่ยม
inline void fill(int x, int y, int w, int h, uint16_t c) {
  if (x < 0) { w += x; x = 0; }
  if (y < GY0) { h -= GY0 - y; y = GY0; }
  if (x + w > W) w = W - x;
  if (y + h > GY1) h = GY1 - y;
  if (w <= 0 || h <= 0) return;
  for (int j = 0; j < h; j++) {
    uint16_t* r = GBUF + (y + j) * W + x;
    for (int i = 0; i < w; i++) r[i] = c;
  }
}
inline void clear(uint16_t c) { fill(0, 0, W, H, c); }
inline void fillA(int x, int y, int w, int h, uint16_t c, uint8_t a) {   // ทับแบบโปร่ง
  if (x < 0) { w += x; x = 0; }
  if (y < GY0) { h -= GY0 - y; y = GY0; }
  if (x + w > W) w = W - x;
  if (y + h > GY1) h = GY1 - y;
  for (int j = 0; j < h; j++) {
    uint16_t* r = GBUF + (y + j) * W + x;
    for (int i = 0; i < w; i++) r[i] = blend(c, r[i], a);
  }
}
inline void frame(int x, int y, int w, int h, uint16_t c, int lw = 1) {
  fill(x, y, w, lw, c); fill(x, y + h - lw, w, lw, c); fill(x, y, lw, h, c); fill(x + w - lw, y, lw, h, c);
}

// สี่เหลี่ยมมุมมน: เติมสี และ/หรือ เส้นขอบ (ขอบมนแบบนุ่ม)
static inline float cov(float d) { return d >= 0.5f ? 1.f : (d <= -0.5f ? 0.f : d + 0.5f); }
inline void rrect(float x, float y, float w, float h, float r, int fillC, int strokeC = -1, float lw = 1) {
  if (r < 0.5f && strokeC < 0 && fillC >= 0) { fill((int)lroundf(x), (int)lroundf(y), (int)lroundf(w), (int)lroundf(h), (uint16_t)fillC); return; }
  r = fminf(r, fminf(w, h) / 2);
  const float cxm = x + w / 2, cym = y + h / 2, hx = w / 2 - r, hy = h / 2 - r;
  // ระยะจากขอบ (บวก = ข้างใน) ของจุด (px,py)
  auto inside = [&](float pxf, float py) {
    float qx = fabsf(pxf - cxm) - hx, qy = fabsf(py - cym) - hy;
    if (qx > 0 && qy > 0) return r - sqrtf(qx * qx + qy * qy);
    return r - fmaxf(qx, qy);
  };
  auto shade = [&](int xx, int yy, float in) {
    float a = cov(in);
    if (a <= 0) return;
    uint16_t* p = &GBUF[yy * W + xx];
    if (fillC >= 0) *p = blend((uint16_t)fillC, *p, (uint8_t)(a * 255));
    if (strokeC >= 0) {
      float st = a * (1.f - cov(in - lw));
      if (st > 0) *p = blend((uint16_t)strokeC, *p, (uint8_t)(st * 255));
    }
  };
  int y0 = (int)floorf(y), y1 = (int)ceilf(y + h), x0 = max(0, (int)floorf(x)), x1 = min(W, (int)ceilf(x + w));
  const float edge = (strokeC >= 0 ? lw : 0) + 1.5f;   // ความกว้างขอบที่ต้องคำนวณละเอียด
  for (int yy = max(y0, GY0); yy < min(y1, GY1); yy++) {
    float py = yy + 0.5f;
    bool corner = fabsf(py - cym) > hy - edge;           // แถวใกล้ขอบบน/ล่าง หรือมุม
    if (corner) { for (int xx = x0; xx < x1; xx++) shade(xx, yy, inside(xx + 0.5f, py)); continue; }
    int ea = min(x1, (int)ceilf(x + edge)), eb = max(ea, (int)floorf(x + w - edge));
    for (int xx = x0; xx < ea; xx++) shade(xx, yy, inside(xx + 0.5f, py));
    if (fillC >= 0) { uint16_t* rp = GBUF + yy * W; for (int xx = ea; xx < eb; xx++) rp[xx] = (uint16_t)fillC; }
    for (int xx = eb; xx < x1; xx++) shade(xx, yy, inside(xx + 0.5f, py));
  }
}

// ---------------------------------------------------------------- วงกลม / ส่วนโค้ง
inline void circle(float cx, float cy, float r, uint16_t c, int strokeC = -1, float lw = 1) {
  int y0 = (int)floorf(cy - r - 1), y1 = (int)ceilf(cy + r + 1);
  float rin = strokeC >= 0 ? r - lw - 1 : r - 1;          // ข้างในวงนี้ = สีเต็ม ไม่ต้องคำนวณ
  float rin2 = rin > 0 ? rin * rin : -1;
  for (int yy = max(y0, GY0); yy <= min(y1, GY1 - 1); yy++) {
    float dy = yy + 0.5f - cy;
    if (fabsf(dy) > r + 1) continue;
    float span = sqrtf(fmaxf(0.f, (r + 1) * (r + 1) - dy * dy));
    int xa = max(0, (int)floorf(cx - span)), xb = min(W - 1, (int)ceilf(cx + span));
    uint16_t* rp = GBUF + yy * W;
    for (int xx = xa; xx <= xb; xx++) {
      float dx = xx + 0.5f - cx, d2 = dx * dx + dy * dy;
      if (d2 < rin2) { rp[xx] = c; continue; }
      float d = sqrtf(d2), a = cov(r - d);
      if (a <= 0) continue;
      rp[xx] = blend(c, rp[xx], (uint8_t)(a * 255));
      if (strokeC >= 0) {
        float st = a * (1.f - cov(r - d - lw));
        if (st > 0) rp[xx] = blend((uint16_t)strokeC, rp[xx], (uint8_t)(st * 255));
      }
    }
  }
}

// ส่วนโค้งหนา: รัศมีกลางเส้น r, หนา w, มุม a0→a1 (เรเดียน ตามเข็มนาฬิกา เริ่มจากแกน x)
inline void arc(float cx, float cy, float r, float a0, float a1, uint16_t c, float w) {
  if (a1 <= a0) return;
  const float TP = 6.2831853f;
  float span = a1 - a0;
  bool full = span >= TP - 1e-4f;
  // ทดสอบมุมด้วย cross product (เร็วกว่า atan2)
  float sx = cosf(a0), sy = sinf(a0), ex = cosf(a1), ey = sinf(a1);
  bool big = span > (float)M_PI;
  auto inAngle = [&](float dx, float dy) {
    if (full) return true;
    bool afterStart = sx * dy - sy * dx >= 0, beforeEnd = dx * ey - dy * ex >= 0;
    return big ? (afterStart || beforeEnd) : (afterStart && beforeEnd);
  };
  float ro = r + w / 2, ri = r - w / 2;
  float ro2s = (ro - 1) * (ro - 1), ri2s = (ri + 1) * (ri + 1);   // ระหว่างนี้ = สีเต็ม
  int y0 = (int)floorf(cy - ro - 1), y1 = (int)ceilf(cy + ro + 1);
  for (int yy = max(y0, GY0); yy <= min(y1, GY1 - 1); yy++) {
    float dy = yy + 0.5f - cy;
    float so = sqrtf(fmaxf(0.f, (ro + 1) * (ro + 1) - dy * dy));
    float si = (fabsf(dy) < ri - 1) ? sqrtf((ri - 1) * (ri - 1) - dy * dy) : -1;
    uint16_t* rp = GBUF + yy * W;
    for (int pass = 0; pass < 2; pass++) {
      int xa, xb;
      if (si < 0) { if (pass) break; xa = (int)floorf(cx - so); xb = (int)ceilf(cx + so); }
      else if (pass == 0) { xa = (int)floorf(cx - so); xb = (int)ceilf(cx - si); }
      else { xa = (int)floorf(cx + si); xb = (int)ceilf(cx + so); }
      xa = max(xa, 0);
      xb = min(xb, W - 1);
      for (int xx = xa; xx <= xb; xx++) {
        float dx = xx + 0.5f - cx;
        if (!inAngle(dx, dy)) continue;
        float d2 = dx * dx + dy * dy;
        if (d2 <= ro2s && d2 >= ri2s) { rp[xx] = c; continue; }
        float d = sqrtf(d2), a = fminf(cov(ro - d), cov(d - ri));
        if (a > 0) rp[xx] = blend(c, rp[xx], (uint8_t)(a * 255));
      }
    }
  }
}

// วงกลมไล่สีจากกลางออกขอบ (c0 กลาง → c1 ที่ระยะ mid → c2 ขอบ) จุดศูนย์กลางสีเลื่อนได้ (fy)
static inline uint16_t lerp565(uint16_t a, uint16_t b, float t) { return blend(b, a, (uint8_t)(fminf(1.f, fmaxf(0.f, t)) * 255)); }
inline void radial(float cx, float cy, float r, uint16_t c0, uint16_t c1, uint16_t c2, float mid = .55f, float fy = 0) {
  int y0 = (int)floorf(cy - r - 1), y1 = (int)ceilf(cy + r + 1);
  for (int yy = max(y0, GY0); yy <= min(y1, GY1 - 1); yy++) {
    float dy = yy + 0.5f - cy;
    if (fabsf(dy) > r + 1) continue;
    float span = sqrtf(fmaxf(0.f, (r + 1) * (r + 1) - dy * dy));
    for (int xx = (int)floorf(cx - span); xx <= (int)ceilf(cx + span); xx++) {
      if ((unsigned)xx >= (unsigned)W) continue;
      float dx = xx + 0.5f - cx, d2 = dx * dx + dy * dy;
      float a = d2 < (r - 1) * (r - 1) ? 1.f : cov(r - sqrtf(d2));
      if (a <= 0) continue;
      float ddy = dy - fy, t = sqrtf(dx * dx + ddy * ddy) / r;
      uint16_t c = t < mid ? lerp565(c0, c1, t / mid) : lerp565(c1, c2, (t - mid) / (1 - mid));
      pxa(xx, yy, c, (uint8_t)(a * 255));
    }
  }
}

// ---------------------------------------------------------------- รูปหลายเหลี่ยม / เส้นหนา
inline void poly(const float* xs, const float* ys, int n, uint16_t c, uint8_t alpha = 255) {
  float mny = 1e9, mxy = -1e9;
  for (int i = 0; i < n; i++) { mny = fminf(mny, ys[i]); mxy = fmaxf(mxy, ys[i]); }
  int ya = max(GY0, (int)floorf(mny)), yb = min(GY1 - 1, (int)ceilf(mxy));
  float xi[16];
  for (int yy = ya; yy <= yb; yy++) {
    float py = yy + 0.5f;
    int k = 0;
    for (int i = 0, j = n - 1; i < n; j = i++) {
      if ((ys[i] <= py && ys[j] > py) || (ys[j] <= py && ys[i] > py)) {
        if (k < 16) xi[k++] = xs[i] + (py - ys[i]) / (ys[j] - ys[i]) * (xs[j] - xs[i]);
      }
    }
    for (int a = 1; a < k; a++) { float t = xi[a]; int b = a - 1; while (b >= 0 && xi[b] > t) { xi[b + 1] = xi[b]; b--; } xi[b + 1] = t; }
    for (int a = 0; a + 1 < k; a += 2) {
      int xa = (int)ceilf(xi[a] - 0.5f), xb = (int)floorf(xi[a + 1] - 0.5f);
      if (xa < 0) xa = 0;
      if (xb >= W) xb = W - 1;
      uint16_t* r = GBUF + yy * W;
      if (alpha >= 250) for (int xx = xa; xx <= xb; xx++) r[xx] = c;
      else for (int xx = xa; xx <= xb; xx++) r[xx] = blend(c, r[xx], alpha);
    }
  }
}
inline void line(float x0, float y0, float x1, float y1, uint16_t c, float w = 1) {
  float dx = x1 - x0, dy = y1 - y0, L = sqrtf(dx * dx + dy * dy);
  if (L < 0.01f) return;
  float nx = -dy / L * w / 2, ny = dx / L * w / 2;
  float xs[4] = {x0 + nx, x1 + nx, x1 - nx, x0 - nx}, ys[4] = {y0 + ny, y1 + ny, y1 - ny, y0 - ny};
  poly(xs, ys, 4, c);
}

// ---------------------------------------------------------------- ตัวอักษร
enum Align { LEFT, CENTER, RIGHT };
// ฟอนต์ที่ใช้จริง (ตอนเปิดเครื่อง copyFontsToPsram() ย้ายรูปตัวอักษรจากแฟลชไป PSRAM — เร็วกว่าแฟลช DIO มาก)
static DFont fontsRt[N_FONTS];
static bool fontsInit = false;
inline const DFont* fontTable() {
  if (!fontsInit) { memcpy(fontsRt, FONTS, sizeof(FONTS)); fontsInit = true; }
  return fontsRt;
}
inline void copyFontsToPsram() {
#ifndef BG_HOST
  const DFont* t = fontTable();
  for (int i = 0; i < N_FONTS; i++) {
    const DGlyph& last = t[i].g[t[i].last - t[i].first];
    size_t n = last.off + (size_t)((last.w + 1) / 2) * last.h + 16;
    uint8_t* p = (uint8_t*)heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    DGlyph* g = (DGlyph*)heap_caps_malloc(sizeof(DGlyph) * (t[i].last - t[i].first + 1), MALLOC_CAP_SPIRAM);
    if (!p || !g) break;
    memcpy(p, t[i].bmp, n);
    memcpy(g, t[i].g, sizeof(DGlyph) * (t[i].last - t[i].first + 1));
    fontsRt[i].bmp = p;
    fontsRt[i].g = g;
  }
#endif
}
inline const DFont* pick(float size, int weight, bool italic) {
  Ctx& cx = C();
  if (size == cx.lastSize && weight == cx.lastW && italic == cx.lastIt) return cx.lastPick;
  const DFont* T = fontTable();
  int wc = weight <= 600 ? 600 : (weight <= 800 ? 800 : 900);
  const DFont* best = nullptr;
  float bd = 1e9;
  for (int i = 0; i < N_FONTS; i++) {
    const DFont& f = T[i];
    float d = fabsf(f.size - size) * 10 + (f.weight != wc ? (f.weight > wc ? 3 : 6) * 4 : 0) + ((f.italic != 0) != italic ? 25 : 0);
    if (d < bd) { bd = d; best = &f; }
  }
  cx.lastPick = best; cx.lastSize = size; cx.lastW = weight; cx.lastIt = italic;
  return best;
}
static inline int glyphIndex(const DFont* f, const unsigned char*& s) {
  int ch = *s;
  if (ch == 0xC2 && s[1] == 0xB0) { s++; ch = 127; }   // °
  if (ch < f->first || ch > f->last) ch = (f->first <= '?' && f->last >= '?') ? '?' : -1;
  return ch < 0 ? -1 : ch - f->first;
}
inline int textWidth(const char* str, float size, int weight = 700, bool italic = false) {
  const DFont* f = pick(size, weight, italic);
  int w = 0;
  for (const unsigned char* s = (const unsigned char*)str; *s; s++) {
    int g = glyphIndex(f, s);
    w += g < 0 ? (int)(size * 0.25f) : f->g[g].adv;
  }
  return w;
}
// y = เส้นฐานตัวอักษร (เหมือน canvas textBaseline='alphabetic')
inline void text(const char* str, int x, int y, float size, uint16_t col, Align al = LEFT, int weight = 700, bool italic = false) {
  const DFont* f = pick(size, weight, italic);
  if (al != LEFT) { int tw = textWidth(str, size, weight, italic); x -= al == CENTER ? tw / 2 : tw; }
  for (const unsigned char* s = (const unsigned char*)str; *s; s++) {
    int gi = glyphIndex(f, s);
    if (gi < 0) { x += (int)(size * 0.25f); continue; }
    const DGlyph& g = f->g[gi];
    const uint8_t* b = f->bmp + g.off;
    int gx = x + g.xo, gy = y + g.yo, stride = (g.w + 1) / 2;
    if (gy >= GY1 || gy + g.h <= GY0) { x += g.adv; continue; }
    for (int r = 0; r < g.h; r++) {
      int yy = gy + r;
      if (yy < GY0 || yy >= GY1) continue;
      const uint8_t* row = b + r * stride;
      for (int c = 0; c < g.w; c++) {
        uint8_t v = (c & 1) ? (row[c >> 1] & 15) : (row[c >> 1] >> 4);
        if (v) pxa(gx + c, yy, col, (uint8_t)(v * 17));
      }
    }
    x += g.adv;
  }
}
inline void textf(int x, int y, float size, uint16_t col, Align al, int weight, bool italic, const char* fmt, ...) {
  char b[64];
  va_list a;
  va_start(a, fmt);
  vsnprintf(b, sizeof(b), fmt, a);
  va_end(a);
  text(b, x, y, size, col, al, weight, italic);
}

// ---------------------------------------------------------------- เลข 7-segment (ชิดขวาที่ xr)
static inline void hseg(float x, float y, float l, float t, uint16_t c, uint8_t al) {
  float xs[6] = {x, x + t / 2, x + l - t / 2, x + l, x + l - t / 2, x + t / 2}, ys[6] = {y + t / 2, y, y, y + t / 2, y + t, y + t};
  poly(xs, ys, 6, c, al);
}
static inline void vseg(float x, float y, float l, float t, uint16_t c, uint8_t al) {
  float xs[6] = {x + t / 2, x + t, x + t, x + t / 2, x, x}, ys[6] = {y, y + t / 2, y + l - t / 2, y + l, y + l - t / 2, y + t / 2};
  poly(xs, ys, 6, c, al);
}
// ส่วนที่ดับวาดเป็นสีขาวจาง ๆ (offA = ความเข้ม 0-255)
inline void seg7(const char* str, float xr, float y, float h, uint16_t c, uint8_t offA = 20) {
  static const char* SEG[] = {"abcdef", "bc", "abged", "abgcd", "fgbc", "afgcd", "afgedc", "abc", "abcdefg", "abcdfg"};
  float w = h * .5f, t = fmaxf(2.f, h * .13f), gap = h * .16f, g = 1;
  int n = strlen(str);
  float x = xr;
  for (int i = n - 1; i >= 0; i--) {
    char ch = str[i];
    if (ch == '.') { fill((int)(x - t - 2), (int)(y + h - t), (int)t, (int)t, c); x -= t + 4; continue; }
    x -= w;
    const char* on = (ch >= '0' && ch <= '9') ? SEG[ch - '0'] : (ch == '-' ? "g" : "");
    auto S = [&](char k) { return strchr(on, k) ? c : (uint16_t)0xFFFF; };
    auto Al = [&](char k) { return strchr(on, k) ? (uint8_t)255 : offA; };
    hseg(x + g, y, w - 2 * g, t, S('a'), Al('a'));
    hseg(x + g, y + h / 2 - t / 2, w - 2 * g, t, S('g'), Al('g'));
    hseg(x + g, y + h - t, w - 2 * g, t, S('d'), Al('d'));
    vseg(x, y + g, h / 2 - 2 * g, t, S('f'), Al('f'));
    vseg(x + w - t, y + g, h / 2 - 2 * g, t, S('b'), Al('b'));
    vseg(x, y + h / 2 + g, h / 2 - 2 * g, t, S('e'), Al('e'));
    vseg(x + w - t, y + h / 2 + g, h / 2 - 2 * g, t, S('c'), Al('c'));
    x -= gap;
  }
}

}  // namespace gfx
