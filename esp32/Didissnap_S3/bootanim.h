#pragma once
// =====================================================================
//  bootanim.h  —  Didissnap boot animation for Panlee 4.3" ESP32-S3 (480x272)
//
//  OYT (big, random colour)  /  OBD2 GAUGE (white)  /  DIDISSNAP. (white)
//  - Random font every boot: 8-bit 60%, 16-bit 20%, 32-bit 20%
//  - Random colour every boot (8 colours, never the same as last boot)
//  - Pop-in + ring, light sweep, typing rows, glitch, sparks, loading bar
//  - All drawn in code (no GIF / no font files)  ~6 s, short button = skip
//
//  Usage (Arduino_GFX example):
//    #include "bootanim.h"
//    static void bootFlush(const uint16_t* buf){ gfx->draw16bitRGBBitmap(0,0,(uint16_t*)buf,480,272); }
//    static bool bootSkip(){ return digitalRead(BTN1_PIN)==LOW; }
//    ...in setup() after gfx->begin():
//    bootanim::play(bootFlush, bootSkip);
// =====================================================================
#include <Arduino.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <string.h>
#include <math.h>
#include "rom/ets_sys.h"

namespace bootanim {

static const int W = 480, H = 272;
typedef void (*FlushFn)(const uint16_t* buf);
typedef bool (*SkipFn)();

// ---------------- colour helpers ----------------
struct RGB { uint8_t r, g, b; };
static inline uint16_t c565(RGB c) { return (uint16_t)(((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3)); }
static inline RGB rgbOf(uint16_t p) {
  RGB c; c.r = (uint8_t)(((p >> 11) & 0x1F) * 255 / 31); c.g = (uint8_t)(((p >> 5) & 0x3F) * 255 / 63); c.b = (uint8_t)((p & 0x1F) * 255 / 31); return c;
}
static inline uint8_t lerp8(int a, int b, float k) { int v = a + (int)((b - a) * k); return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }
static inline RGB mixc(RGB c, float k, uint8_t to) { RGB o; o.r = lerp8(c.r, to, k); o.g = lerp8(c.g, to, k); o.b = lerp8(c.b, to, k); return o; }
static inline RGB lerpc(RGB a, RGB b, float k) { RGB o; o.r = lerp8(a.r, b.r, k); o.g = lerp8(a.g, b.g, k); o.b = lerp8(a.b, b.b, k); return o; }
static inline RGB scalec(RGB c, float k) { return mixc(c, 1.0f - k, 0); }

static const RGB PAL[8] = {
  {0xFF, 0xD2, 0x00},  // yellow
  {0xFF, 0x3D, 0x8B},  // pink
  {0x20, 0xD4, 0xFF},  // cyan
  {0x3D, 0xFF, 0x6E},  // green
  {0xFF, 0x7A, 0x1A},  // orange
  {0xFF, 0x2A, 0x3A},  // red
  {0xB0, 0x5C, 0xFF},  // purple
  {0xF2, 0xF2, 0xF2},  // white
};
static const RGB WHITE = {255, 255, 255};
static const RGB GREY = {0x99, 0x99, 0x99};
static const RGB OUTL = {12, 8, 8};

// ---------------- frame buffer ----------------
static uint16_t* fb = nullptr;
// Didissnap_S3: ล้างเฟรมทีละช่วงเล็ก เว้นจังหวะให้จออ่าน PSRAM (ล้างรวดเดียว 261 KB ทุกเฟรม → ภาพเลื่อน/สั่น)
static void clearFb() {
  const int CH = W * 8;
  for (int o = 0; o < W * H; o += CH) {
    memset(fb + o, 0, (W * H - o < CH ? W * H - o : CH) * 2);
    esp_rom_delay_us(30);
  }
}

static inline void px(int x, int y, uint16_t c) { if ((unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H) fb[y * W + x] = c; }
static void fillRect(int x, int y, int w, int h, uint16_t c) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > W) w = W - x;
  if (y + h > H) h = H - y;
  if (w <= 0 || h <= 0) return;
  for (int j = 0; j < h; j++) { uint16_t* p = fb + (y + j) * W + x; for (int i = 0; i < w; i++) p[i] = c; }
}
static void blendRect(int x, int y, int w, int h, RGB c, float a) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > W) w = W - x;
  if (y + h > H) h = H - y;
  if (w <= 0 || h <= 0) return;
  for (int j = 0; j < h; j++) { uint16_t* p = fb + (y + j) * W + x; for (int i = 0; i < w; i++) p[i] = c565(lerpc(rgbOf(p[i]), c, a)); }
}

// ---------------- 5x7 pixel font (only the letters we need) ----------------
struct Glyph { char ch; uint8_t row[7]; };
static const Glyph FONT[] = {
  {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
  {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
  {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
  {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
  {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
  {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}},
  {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
  {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
  {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
  {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
  {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
  {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
  {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
  {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
  {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
  {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
  {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
  {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
};
static const uint8_t* glyph(char c) {
  for (unsigned i = 0; i < sizeof(FONT) / sizeof(FONT[0]); i++) if (FONT[i].ch == c) return FONT[i].row;
  return FONT[sizeof(FONT) / sizeof(FONT[0]) - 1].row;
}

// ---------------- text mask (binary grid) ----------------
struct Mask { int w = 0, h = 0; uint8_t* m = nullptr; };
static inline uint8_t mget(const Mask& k, int x, int y) { return (x < 0 || y < 0 || x >= k.w || y >= k.h) ? 0 : k.m[y * k.w + x]; }
static void mfree(Mask& k) { if (k.m) free(k.m); k.m = nullptr; k.w = k.h = 0; }

static Mask buildL0(const char* s, bool bold) {
  int n = (int)strlen(s), cw = bold ? 6 : 5, adv = cw + 1;
  Mask k; k.w = n * adv - 1; k.h = 7; k.m = (uint8_t*)calloc(k.w * k.h, 1);
  for (int i = 0; i < n; i++) {
    const uint8_t* g = glyph(s[i]);
    for (int y = 0; y < 7; y++) for (int x = 0; x < 5; x++) if (g[y] & (0x10 >> x)) {
      k.m[y * k.w + i * adv + x] = 1; if (bold) k.m[y * k.w + i * adv + x + 1] = 1;
    }
  }
  return k;
}
// Scale2x-style: doubles resolution and smooths the diagonals (16-bit & 32-bit look)
static Mask epx(const Mask& s) {
  Mask d; d.w = s.w * 2; d.h = s.h * 2; d.m = (uint8_t*)calloc(d.w * d.h, 1);
  for (int y = 0; y < s.h; y++) for (int x = 0; x < s.w; x++) {
    uint8_t P = mget(s, x, y), A = mget(s, x, y - 1), B = mget(s, x + 1, y), C = mget(s, x - 1, y), D = mget(s, x, y + 1);
    // fill-only variant: smooths diagonal steps but never shaves square corners (keeps D, B, E crisp)
    uint8_t p1 = P, p2 = P, p3 = P, p4 = P;
    if (!P) {
      if (C && A && !D && !B) p1 = 1;
      if (A && B && !C && !D) p2 = 1;
      if (D && C && !B && !A) p3 = 1;
      if (B && D && !A && !C) p4 = 1;
    }
    d.m[(2 * y) * d.w + 2 * x] = p1; d.m[(2 * y) * d.w + 2 * x + 1] = p2;
    d.m[(2 * y + 1) * d.w + 2 * x] = p3; d.m[(2 * y + 1) * d.w + 2 * x + 1] = p4;
  }
  return d;
}
static Mask buildMask(const char* s, bool bold, int level) {
  Mask k = buildL0(s, bold);
  for (int i = 0; i < level; i++) { Mask n = epx(k); mfree(k); k = n; }
  return k;
}
// outline mask: every cell within e cells of a filled cell (size w+2e x h+2e)
static Mask dilate(const Mask& s, int e) {
  Mask d; d.w = s.w + 2 * e; d.h = s.h + 2 * e; d.m = (uint8_t*)calloc(d.w * d.h, 1);
  for (int y = 0; y < s.h; y++) for (int x = 0; x < s.w; x++) if (s.m[y * s.w + x])
    for (int dy = 0; dy <= 2 * e; dy++) for (int dx = 0; dx <= 2 * e; dx++) d.m[(y + dy) * d.w + x + dx] = 1;
  return d;
}

// ---------------- styled text ----------------
enum Style { ST8 = 8, ST16 = 16, ST32 = 32, PLAIN = 0 };
struct Text { Mask m, out; float cell; int style; RGB col; int nChars; int outE; };

static Text makeText(const char* s, bool bold, int style, float targetH, RGB col) {
  Text t; t.style = style; t.col = col; t.nChars = (int)strlen(s);
  int level = (style == ST8 || style == PLAIN) ? 0 : (style == ST16 ? 1 : 2);
  t.m = buildMask(s, bold, level);
  t.cell = targetH / (float)t.m.h;
  t.outE = (style == ST16) ? 1 : (style == ST32 ? 2 : 0);
  if (t.outE) t.out = dilate(t.m, t.outE);
  return t;
}
static void freeText(Text& t) { mfree(t.m); mfree(t.out); }

// draws text centred at (cx,cy) scaled by s; reveal = 0..1 of chars; sweep = x of light band (<-999 = none)
// returns right edge x of the revealed part (for the cursor)
static int drawText(const Text& t, float cx, float cy, float s, float reveal, float sweep, float fade) {
  float cs = t.cell * s; if (cs <= 0.05f) return (int)cx;
  float tw = t.m.w * cs, th = t.m.h * cs, x0 = cx - tw / 2, y0 = cy - th / 2;
  int cols = (int)(t.m.w * reveal + 0.5f);
  if (reveal < 1.0f) {  // cut at a whole character
    int adv = (t.m.w + 1) / t.nChars; int nc = (int)(reveal * t.nChars + 0.0001f); cols = nc * adv;
    if (cols > t.m.w) cols = t.m.w;
  }
  auto X = [&](int i) { return (int)floorf(x0 + i * cs); };
  auto Y = [&](int j) { return (int)floorf(y0 + j * cs); };
  RGB base = scalec(t.col, fade);

  // pass 1: shadow (8-bit) / outline (16/32-bit)
  if (t.style == ST8) {
    uint16_t sh = c565(scalec(mixc(t.col, 0.65f, 0), fade));
    for (int j = 0; j < t.m.h; j++) for (int i = 0; i < cols; i++) if (t.m.m[j * t.m.w + i])
      fillRect(X(i + 1), Y(j + 1), X(i + 2) - X(i + 1), Y(j + 2) - Y(j + 1), sh);
  } else if (t.outE) {
    uint16_t oc = c565(OUTL);
    int e = t.outE, ocols = cols + 2 * e;
    for (int j = 0; j < t.out.h; j++) for (int i = 0; i < ocols && i < t.out.w; i++) if (t.out.m[j * t.out.w + i])
      fillRect(X(i - e), Y(j - e), X(i - e + 1) - X(i - e), Y(j - e + 1) - Y(j - e), oc);
  }
  // pass 2: fill
  RGB hi16 = mixc(base, 0.35f, 255), lo16 = mixc(base, 0.30f, 0);
  RGB g0 = mixc(base, 0.6f, 255), g2 = mixc(base, 0.25f, 0), g3 = mixc(base, 0.10f, 255);
  for (int j = 0; j < t.m.h; j++) {
    float rf = (j + 0.5f) / t.m.h; RGB c = base;
    if (t.style == ST16) c = rf < 0.42f ? hi16 : (rf > 0.60f ? lo16 : base);
    else if (t.style == ST32) {
      if (rf < 0.45f) c = lerpc(g0, base, rf / 0.45f);
      else if (rf < 0.55f) c = g2;
      else c = lerpc(g2, g3, (rf - 0.55f) / 0.45f);
      if (rf > 0.04f && rf < 0.16f) c = lerpc(c, WHITE, 0.35f);
    }
    uint16_t c16 = c565(c);
    for (int i = 0; i < cols; i++) if (t.m.m[j * t.m.w + i]) {
      int xa = X(i), ya = Y(j), w = X(i + 1) - xa, h = Y(j + 1) - ya;
      uint16_t cc = c16;
      if (sweep > -999) {
        float bx = sweep - ya * 0.4f, mx = xa + w * 0.5f;
        if (mx >= bx && mx <= bx + 34) cc = c565(lerpc(c, WHITE, 0.85f));
      }
      fillRect(xa, ya, w, h, cc);
    }
  }
  return X(cols);
}

// ---------------- effects ----------------
static void ring(int cx, int cy, float r, RGB c, float a, int blk) {
  uint16_t col = c565(scalec(c, a));
  int steps = (int)(r * 6.3f) + 8;
  for (int th = 0; th < 3; th++) {
    float rr = r + th;
    for (int k = 0; k < steps; k++) {
      float an = k * 6.2831853f / steps; int x = cx + (int)(cosf(an) * rr), y = cy + (int)(sinf(an) * rr);
      if (blk > 1) { x = x / blk * blk; y = y / blk * blk; fillRect(x, y, blk, blk, col); } else px(x, y, col);
    }
    if (blk > 1) break;
  }
}
struct Spark { float x, y, vx, vy, life; };
static Spark sparks[96]; static int nSparks = 0;
static void addSpark(float x, float y, float vx, float vy) { if (nSparks < 96) sparks[nSparks++] = {x, y, vx, vy, 1.0f}; }
static inline float frand() { return (esp_random() & 0xFFFF) / 65535.0f; }

static void glitch(float t, int q) {
  static uint16_t row[W];
  for (int y = 0; y < H;) {
    int h = 4 + ((y * 7) % 10), dx = (int)((frand() - 0.5f) * 30); if (q > 1) dx = dx / q * q;
    for (int j = y; j < y + h && j < H; j++) {
      uint16_t* p = fb + j * W; memcpy(row, p, sizeof(row));
      for (int x = 0; x < W; x++) { int sx = x - dx; p[x] = (sx >= 0 && sx < W) ? row[sx] : 0; }
      // red/blue split on the slice
      if (dx > 6) for (int x = 0; x < W - 4; x++) p[x] |= (row[x + 4 < W ? x + 4 : x] & 0xF800);
    }
    y += h + 8 + ((y * 13) % 11);
  }
  int ly = ((int)(t * 900)) % H; blendRect(0, ly, W, 2, {0xFF, 0x00, 0x4C}, 0.5f);
}

static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static inline float easeBack(float t) { const float s = 1.70158f; t -= 1; return t * t * ((s + 1) * t + s) + 1; }
static inline float easeOut(float t) { return 1 - powf(1 - t, 3); }

// ---------------- main ----------------
// forceFont: -1 random, or 8/16/32   forceColor: -1 random, or 0..7
static void play(FlushFn flush, SkipFn skip = nullptr, int forceFont = -1, int forceColor = -1) {
  bool own = false;
  if (!fb) { fb = (uint16_t*)heap_caps_malloc(W * H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); own = true; }
  if (!fb) return;

  // pick font (8-bit 60% / 16-bit 20% / 32-bit 20%) and colour (no repeat)
  int F = forceFont;
  if (F < 0) { uint32_t r = esp_random() % 10; F = r < 6 ? ST8 : (r < 8 ? ST16 : ST32); }
  int ci = forceColor;
  if (ci < 0) {
    Preferences pr; pr.begin("boot", false);
    int last = pr.getInt("lastc", -1);
    do { ci = (int)(esp_random() % 8); } while (ci == last);
    pr.putInt("lastc", ci); pr.end();
  }
  RGB Y = PAL[ci];
  int q = F == ST8 ? 4 : (F == ST16 ? 2 : 1);  // pixel grid for effects

  Text logo = makeText("OYT", true, F, 84, Y);
  float rowH = F == ST8 ? 21 : 28;
  Text r1 = makeText("OBD2 GAUGE", false, F, rowH, WHITE);
  Text r2 = makeText("DIDISSNAP.", false, F, rowH, WHITE);
  int lst = F == ST32 ? ST16 : PLAIN;
  Text l1 = makeText("CONNECTING OBD2...", false, lst, 14, GREY);
  Text l2 = makeText("READY", false, lst, 14, GREY);

  nSparks = 0;
  const float LOGO_Y = 90, R1_Y = 162, R2_Y = 200, END = 6.0f;
  uint32_t t0 = millis();
  while (true) {
    float t = (millis() - t0) / 1000.0f;
    if (t > END) break;
    if (skip && skip()) break;
    float fade = t > 5.4f ? 1 - clamp01((t - 5.4f) / 0.6f) : 1;

    clearFb();

    // ring
    float r = clamp01((t - 0.45f) / 0.9f);
    if (r > 0 && r < 1) ring(240, (int)LOGO_Y, 46 + r * 200, Y, fade * (1 - r) * 0.7f, q > 1 ? 4 : 1);

    // logo pop + light sweep
    float p = clamp01(t / 0.8f), s = p > 0 ? easeBack(p) : 0;
    float sw = clamp01((t - 1.0f) / 0.7f);
    float sweepX = (sw > 0 && sw < 1) ? (-80 + sw * 640) : -10000;
    if (q > 1 && sweepX > -999) sweepX = floorf(sweepX / q) * q;
    if (s > 0.01f) drawText(logo, 240, LOGO_Y, s, 1, sweepX, fade);

    // typing rows + blinking cursor
    const float rs[2] = {1.3f, 2.1f}; Text* rt[2] = {&r1, &r2}; const float ry[2] = {R1_Y, R2_Y};
    for (int k = 0; k < 2; k++) {
      if (t < rs[k]) continue;
      float fr = clamp01((t - rs[k]) / 0.8f);
      int ex = drawText(*rt[k], 240, ry[k], 1, fr, -10000, fade);
      if (fr < 1 && ((int)(t * 8)) % 2 == 0) {
        int cx = q > 1 ? ex / q * q : ex;
        fillRect(cx, (int)ry[k] - 8, F == ST8 ? 8 : 10, 16, c565(scalec(Y, fade)));
      }
    }

    // loading bar + label
    float bb = easeOut(clamp01((t - 3.0f) / 1.6f));
    if (t > 2.9f) {
      fillRect(150, 236, 180, 4, c565(scalec({0x2A, 0x2A, 0x2A}, fade)));
      int bw = (int)(180 * bb); if (F == ST8) bw = bw / 12 * 12; else if (F == ST16) bw = bw / 6 * 6;
      fillRect(150, 236, bw, 4, c565(scalec(Y, fade)));
      drawText(bb < 1 ? l1 : l2, 240, 254, 1, 1, -10000, fade);
    }

    // sparks
    if (t > 0.55f && t < 0.75f && nSparks < 40)
      for (int i = 0; i < 8; i++) { float a = frand() * 6.2831853f, v = 2 + frand() * 4; addSpark(240, LOGO_Y, cosf(a) * v, sinf(a) * v - 1); }
    if (sw > 0 && sw < 1 && frand() < 0.6f) addSpark(140 + sw * 200, 44 + frand() * 90, (frand() - 0.5f) * 2, -frand() * 2);
    RGB hiC = mixc(Y, 0.7f, 255);
    for (int i = 0; i < nSparks; i++) {
      Spark& k = sparks[i]; k.x += k.vx; k.y += k.vy; k.vy += 0.12f; k.life -= 0.025f;
      if (k.life <= 0) continue;
      int z = F == ST8 ? 4 : 2;
      uint16_t c = c565(scalec(k.life > 0.5f ? hiC : Y, fade * k.life));
      fillRect((int)(k.x / z) * z, (int)(k.y / z) * z, z, z, c);
    }

    // glitch moments
    if (t < 0.35f || (t > 2.05f && t < 2.2f) || (t > 3.4f && t < 3.5f)) glitch(t, q);

    flush(fb);
  }
  clearFb(); flush(fb);
  freeText(logo); freeText(r1); freeText(r2); freeText(l1); freeText(l2);
  if (own) { heap_caps_free(fb); fb = nullptr; }
}

}  // namespace bootanim
