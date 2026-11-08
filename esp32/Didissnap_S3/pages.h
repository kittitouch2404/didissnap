// pages.h — หน้าปัด 12 หน้า (แปลงมาจากพรีวิว board.js ให้หน้าตาเหมือนกัน)
#pragma once
#include "gfx.h"
#include "data.h"
#include "bg.h"
#include "pages2.h"

namespace pages {
using namespace gfx;

static const int N = 16;
static const char* const NAMES[N] = {"Bar graph", "Yellow box", "Race bar", "Half gauge", "Round gauge", "Strip",
                                     "Race dash", "Gear big", "Red stripe", "ECU", "Record", "OBD2 table",
                                     "Picture", "Car run", "Photo", "Pic by rpm"};
// หน้ารูป (ใช้ slot 0-2 ของรูปที่ส่งจากแอป) และหน้ารูปตามรอบ
static const int PG_PIC = 12, PG_RUN = 13, PG_PHOTO = 14, PG_RPM = 15, PG_RECORD = 10;

constexpr uint16_t ORG = rgb(0xFF7A00), YEL = rgb(0xFFC400), RD = rgb(0xFF2A2A), GR = rgb(0x00E05A),
                   BL = rgb(0x2E7BFF), WH = 0xFFFF, BK = 0x0000;

// ---------------------------------------------------------------- ตัวช่วย
static char tbs[2][8][16];   // ข้อความชั่วคราว (แยกต่อคอร์)
static int tbis[2] = {0, 0};
inline const char* f0(float v) { char* b = tbs[gfx::core()][tbis[gfx::core()]++ & 7]; snprintf(b, 16, "%d", (int)lroundf(v)); return b; }
inline const char* f1(float v) { char* b = tbs[gfx::core()][tbis[gfx::core()]++ & 7]; snprintf(b, 16, "%.1f", v); return b; }
inline int maxRpm() { return didicfg::cfg.maxRpm; }
inline int kMax() { return max(6, (int)ceilf(maxRpm() / 1000.f) + 1); }
inline float rpmF() { return fminf(1.f, D.rpm / (kMax() * 1000.f)); }
inline float redF() { return maxRpm() / (kMax() * 1000.f); }
inline bool over() { return D.rpm >= maxRpm(); }
inline bool hot() { return D.ect >= didicfg::cfg.tempAlert; }
inline bool blink() { return (gfx::nowMs() / 250) % 2 == 0; }
inline uint16_t ectC(uint16_t c = WH) { return hot() ? (blink() ? RD : rgb(0x550000)) : c; }
inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
inline void obd(int x, int y, Align al = LEFT) {
  if (D.link == 2) text("OBD OK", x, y, 11, GR, al, 700);
  else if (D.link == 1) text(blink() ? "OBD ..." : "OBD", x, y, 11, YEL, al, 700);
  else text("DEMO", x, y, 11, rgb(0x888888), al, 700);
}
inline void dots() {
  int n = N, p = didicfg::cfg.page, s = 11, x0 = W / 2 - (n - 1) * s / 2;
  for (int i = 0; i < n; i++) circle(x0 + i * s, H - 5, i == p ? 3 : 2.2f, i == p ? WH : rgb(0xAAB4C8));
}
inline void shiftFlash() {
  if (over() && blink()) frame(3, 3, W - 6, H - 6, RD, 6);
}
inline void vbox(int x, int y, int w, int h, const char* lab, const char* val, const char* mn, const char* mx, bool yel = false, uint16_t vc = BK) {
  text(mn, x + 2, y + 9, 9, rgb(0xBBBBBB), LEFT, 500);
  text(mx, x + w - 2, y + 9, 9, rgb(0xBBBBBB), RIGHT, 500);
  fill(x, y + 11, w, h - 24, yel ? rgb(0xFFB000) : WH);
  text(val, x + w - 4, y + h - 15, h * .5f, vc, RIGHT, 800);
  fill(x, y + h - 12, w, 12, yel ? rgb(0x9A6A00) : rgb(0x1A50C8));
  text(lab, x + w / 2, y + h - 3, 9.5f, WH, CENTER, 700);
}
inline void gearBox(int x, int y, int w, int h, uint16_t bgc, uint16_t fg) {
  rrect(x, y, w, h, 8, bgc);
  text(D.gear, x + w / 2, y + (int)(h * .83f), h * .78f, fg, CENTER, 900);
}
// หน้ารูป 13-16: ตัวอักษรพิกเซลแบบเกม 16-bit (Press Start 2P + ขอบดำ) + แถบรอบแบบหลอดพลัง
// ขนาด (k = พิกเซลต่อจุด, สูง 8k) — ตั้งจากตัวปรับในพรีวิว
static const int PX_LAB = 2, PX_RPM = 4, PX_SPD = 3, PX_SM = 3, PX_BAR_W = 226, PX_BAR_H = 19, PX_OL = 1;
constexpr uint16_t PXG = rgb(0x39E05A), PXY = rgb(0xFFD21E), PXR = rgb(0xFF3030);
inline void rpmHp(int x, int y, int w, int h) {
  float rf = redF();
  hpBar(x, y, w, h, rpmF(), rf - .2f, rf, PXG, PXY, PXR);
}
inline void pt(const char* s, int x, int y, int k, uint16_t c = WH, Align al = LEFT) { pxText(s, x, y, k, c, al, PX_OL); }
inline int lh() { return 8 * PX_LAB; }
// ค่าตัวเลข + หน่วย "C" ต่อท้าย (ชิดล่างของตัวเลข)
inline void valC(const char* v, int x, int y, int k, uint16_t c) {
  pt(v, x, y, k, c);
  pt("C", x + pxWidth(v, k) + 6, y + 8 * k - lh(), PX_LAB);
}
inline void topBar() {               // หน้า 14-16
  int vy = 8 + lh() + 4;
  int rr = 10 + pxWidth("8888", PX_RPM);
  int by = vy + 8 * PX_RPM + 6;
  fillA(0, 0, W, by + PX_BAR_H + 4, BK, 40);
  pt("RPM", 10, 8, PX_LAB);
  pt(f0(D.rpm), rr, vy, PX_RPM, over() ? PXR : WH, RIGHT);
  int cx = rr + 24;
  pt("COOLANT", cx, 8, PX_LAB);
  valC(f0(D.ect), cx, vy, PX_RPM, ectC());
  obd(474, 14, RIGHT);
  rpmHp(8, by, PX_BAR_W, PX_BAR_H);
}
inline void noPics() {
  clear(BK);
  text("NO PICTURES", W / 2, 140, 26, rgb(0x666666), CENTER, 900);
  text("SEND FROM APP: MENU > ESP32", W / 2, 166, 13, rgb(0x666666), CENTER, 600);
}

// ---------------------------------------------------------------- 1 Bar graph
inline void p1() {
  clear(BK);
  text(f0(D.spd), 8, 58, 58, WH, LEFT, 900, true);
  text("Km/h", 10, 76, 13, WH, LEFT, 500);
  const int x0 = 110, x1 = 470, yb = 96, NB = 44, K = kMax();
  float red = redF(), fr = rpmF();
  for (int i = 0; i < NB; i++) {
    float f = (float)i / NB, bx = x0 + i * (float)(x1 - x0) / NB, bw = (float)(x1 - x0) / NB - 1.5f, bh = 16 + f * 62;
    uint16_t c = f < fr ? (f >= red ? RD : rgb(0xFF9A1A)) : (f >= red ? rgb(0x5A2020) : rgb(0xC9C9D4));
    fill((int)bx, (int)(yb - bh), (int)ceilf(bw), (int)bh, c);
  }
  for (int k = 1; k <= K; k++) {
    float f = (float)k / K, bx = x0 + f * (x1 - x0) - 6, bh = 16 + f * 62;
    text(f0(k), (int)fminf(bx, x1 - 4), (int)(yb - bh - 4), 16, k * 1000 >= maxRpm() ? RD : WH, CENTER, 700);
  }
  text(f0(D.rpm), 360, 138, 40, over() ? RD : WH, RIGHT, 900, true);
  text("RPM", 368, 128, 13, RD, LEFT, 700);
  obd(8, 126);
  const int r1 = 140, r2 = 202, bw = 86, bh = 60;
  vbox(4, r1, bw, bh, "WATER", f0(D.ect), "40", "120", false, hot() ? RD : BK);
  vbox(94, r1, bw, bh, "AIR.T", f0(D.iat), "10", "60", true);
  vbox(4, r2, bw, bh, "CALC.LOAD", f1(D.load), "0", "100");
  vbox(94, r2, bw, bh, "THRO.P", f1(D.tps), "0", "100", true);
  gearBox(186, r1 + 11, 108, bh * 2 - 12, rgb(0x7FB6F5), WH);
  vbox(300, r1, bw, bh, "BOOST.P", f1(D.boost), "0.0", "2.5", true);
  vbox(390, r1, bw, bh, "AIR.FLOW", f0(D.maf), "0", "655");
  vbox(300, r2, bw, bh, "FUEL.L", f0(D.fuel), "0", "100");
  vbox(390, r2, bw, bh, "BAT", f1(D.volt), "10.0", "16.0", true);
  dots();
  shiftFlash();
}

// ---------------------------------------------------------------- 2 Yellow box
inline void yb(int x, int y, int w, int h, const char* l, const char* v, uint16_t c = BK) {
  rrect(x, y, w, h, 4, YEL);
  text(l, x + 5, y + 14, 11.5f, BK, LEFT, 800);
  text(v, x + w - 5, y + h - 6, h * .55f, c, RIGHT, 900);
}
inline void p2() {
  clear(BK);
  const int K = kMax(), x0 = 8, x1 = 472;
  float fr = rpmF(), red = redF();
  for (int k = 1; k <= K; k++) text(f0(k), (int)(x0 + (k - .5f) / K * (x1 - x0)), 17, 17, k * 1000 > maxRpm() ? RD : WH, CENTER, 800);
  fill(x0, 22, x1 - x0, 14, rgb(0x10235A));
  fill((int)(x0 + red * (x1 - x0)), 22, (int)((1 - red) * (x1 - x0)) + 1, 14, rgb(0x4A0D0D));
  fill(x0, 22, (int)(fminf(fr, red) * (x1 - x0)), 14, BL);
  if (fr > red) fill((int)(x0 + red * (x1 - x0)), 22, (int)((fr - red) * (x1 - x0)), 14, RD);
  rrect(160, 42, 150, 38, 4, YEL);
  text(f0(D.spd), 235, 76, 36, BK, CENTER, 900, true);
  text("Km/h", 318, 72, 14, WH, LEFT, 500);
  yb(6, 86, 74, 56, "WATER T", f0(D.ect), hot() ? RD : BK);
  yb(84, 86, 74, 56, "AIR T", f0(D.iat));
  yb(6, 146, 74, 56, "ENG LOAD", f0(D.load));
  yb(84, 146, 74, 56, "AIRFLOW", f1(D.maf));
  yb(6, 206, 152, 44, "FUEL %", f0(D.fuel));
  text("GEAR", 237, 92, 11, WH, CENTER, 600);
  rrect(166, 96, 142, 148, 10, BL, rgb(0x9CC3FF), 2);
  text(D.gear, 237, 222, 130, WH, CENTER, 900);
  yb(314, 86, 160, 56, "r/min", f0(D.rpm), over() ? RD : BK);
  yb(314, 146, 78, 56, "BATT V", f1(D.volt));
  yb(396, 146, 78, 56, "BOOST", f1(D.boost));
  yb(314, 206, 160, 44, "TRIP km", f1(fmodf(D.trip, 1000)));
  obd(166, 258);
  dots();
  shiftFlash();
}

// ---------------------------------------------------------------- 3 Race bar (โค้ง)
inline void cell3(int x, int y, const char* l, const char* u, const char* v, uint16_t c = WH) {
  text(l, x, y, 10.5f, WH, LEFT, 600);
  text(u, x + 64, y, 10.5f, WH, RIGHT, 600);
  seg7(v, x + 64, y + 5, 24, c);
  fill(x, y + 34, 64, 2, rgb(0x2050C0));
}
inline void p3() {
  clear(BK);
  const int K = kMax();
  float fr = rpmF(), red = redF();
  const float cx = 330, cy = 272, rx = 318, ry = 258, split = .62f;
  const int NB = 64;
  for (int i = 0; i < NB; i++) {
    float f = (float)i / NB;
    bool lit = f < fr;
    if (f < split) {
      float a = M_PI + f / split * M_PI / 2, ca = cosf(a), sa = sinf(a), len = .16f + f * .12f;
      line(cx + rx * ca, cy + ry * sa, cx + rx * (1 - len) * ca, cy + ry * (1 - len) * sa, lit ? WH : rgb(0x3A3A3A), 3);
    } else {
      float g = (f - split) / (1 - split);
      int x = (int)(cx + 4 + g * 140);
      fill(x, 6, 2, 76, lit ? (f >= red ? RD : YEL) : (f >= red ? rgb(0x4A1010) : rgb(0x4A4300)));
    }
  }
  for (int k = 0; k <= K; k++) {
    float f = (float)k / K, x, y;
    if (f <= split) { float a = M_PI + f / split * M_PI / 2; x = cx + rx * .66f * cosf(a); y = cy + ry * .66f * sinf(a) + 8; }
    else { x = cx + 4 + (f - split) / (1 - split) * 140; y = 100; }
    text(f0(k), (int)x, (int)y, k >= K - 2 ? 24 : 22, k * 1000 >= maxRpm() ? RD : WH, CENTER, 900, true);
  }
  text(f0(D.spd), 328, 192, 76, WH, RIGHT, 900, true);
  text("KM/H", 300, 210, 13, WH, CENTER, 700);
  text(D.gear, 300, 252, 36, rgb(0x3A8BFF), CENTER, 900);
  text("GEAR", 300, 266, 10, rgb(0x3A8BFF), CENTER, 700);
  cell3(340, 120, "WATER", "C", f0(D.ect), ectC());
  cell3(410, 120, "RPM", "", f0(D.rpm), over() ? RD : WH);
  cell3(340, 168, "BATT", "V", f1(D.volt));
  cell3(410, 168, "TPS", "%", f0(D.tps));
  cell3(340, 216, "BOOST", "bar", f1(D.boost));
  cell3(410, 216, "TRIP", "km", f1(fmodf(D.trip, 100)));
  obd(6, 262);
  dots();
  shiftFlash();
}

// ---------------------------------------------------------------- 4 Half gauge
inline void p4() {
  clear(BK);
  const int K = kMax();
  float fr = rpmF(), red = redF();
  const float cx = 150, cy = 150, R = 112, a0 = M_PI * .42f, a1 = M_PI * 1.82f;
  auto A = [&](float f) { return a0 + (a1 - a0) * f; };
  arc(cx, cy, R - 22, a0, a1, rgb(0x1C2C50), 44);
  arc(cx, cy, R - 22, a0, A(fr), YEL, 44);
  arc(cx, cy, R + 3, A(red), a1, over() && blink() ? rgb(0xFF6060) : RD, 9);
  arc(cx, cy, R + 3, a0, A(red), rgb(0x555555), 9);
  for (int k = 1; k <= K; k++) {
    float a = A((float)k / K);
    bool r = k * 1000 >= maxRpm();
    text(f0(k), (int)(cx + (R - 62) * cosf(a)), (int)(cy + (R - 62) * sinf(a) + 9), r ? 24 : 26, r ? RD : WH, CENTER, 900);
  }
  text(D.gear, (int)cx + 8, (int)cy + 26, 74, WH, CENTER, 900);
  text("GEAR", (int)cx + 8, (int)cy + 42, 10, WH, CENTER, 600);
  text("x1000 RPM", 8, 264, 10, rgb(0xAAAAAA), LEFT, 600);
  obd(8, 14);
  text("BOOST", 300, 20, 12, WH, LEFT, 500);
  text("bar", 474, 20, 12, WH, RIGHT, 500);
  text(f1(D.boost), 390, 58, 36, WH, CENTER, 900);
  rrect(292, 68, 182, 116, 10, rgb(0x05070C), rgb(0x4A6A90), 2);
  text(f0(D.spd), 383, 160, 92, WH, CENTER, 900);
  text("KM/H", 466, 176, 14, WH, RIGHT, 800);
  text("BATT", 300, 204, 12, WH, LEFT, 500);
  text("V", 372, 204, 12, WH, RIGHT, 500);
  text(f1(D.volt), 300, 252, 44, WH, LEFT, 900);
  text("WATER", 392, 204, 12, WH, LEFT, 500);
  text(f0(D.ect), 474, 204, 16, ectC(), RIGHT, 900);
  float bf = clamp01((D.ect - 40) / 90.f);
  fill(392, 214, 82, 6, rgb(0x333333));
  fill(392, 214, (int)(82 * bf), 6, hot() ? RD : rgb(0x3AA0FF));
  text("0", 392, 232, 9, rgb(0xAAAAAA));
  text("60", 415, 232, 9, rgb(0xAAAAAA));
  text(f0(didicfg::cfg.tempAlert), 474, 232, 9, RD, RIGHT);
  dots();
}

// ---------------------------------------------------------------- 5 Round gauge
inline void side5(int x, int y, const char* l, const char* v, float f, uint16_t vc = WH) {
  rrect(x, y, 92, 68, 6, rgb(0x111111), rgb(0x333333));
  for (int i = 0; i < 8; i++) fill(x + 5, y + 58 - i * 7, 8, 5, (float)i / 8 < f ? YEL : rgb(0x333333));
  text(v, x + 86, y + 44, 32, vc, RIGHT, 900);
  text(l, x + 86, y + 62, 10, rgb(0xBBBBBB), RIGHT, 600);
}
inline void p5() {
  clear(BK);
  const int K = kMax();
  float red = redF();
  const float cx = 240, cy = 146, R = 112, a0 = M_PI * .72f, a1 = M_PI * 2.28f;
  auto A = [&](float f) { return a0 + (a1 - a0) * f; };
  circle(cx, cy, R, rgb(0x3A3A3A));
  arc(cx, cy, R - 12, a0, A(red), YEL, 22);
  arc(cx, cy, R - 12, A(red), a1, RD, 22);
  circle(cx, cy, R - 23, rgb(0x0A0A0A));
  for (int i = 0; i <= K * 4; i++) {
    float a = A((float)i / (K * 4)), l = i % 4 ? 6 : 12;
    line(cx + (R - 24) * cosf(a), cy + (R - 24) * sinf(a), cx + (R - 24 - l) * cosf(a), cy + (R - 24 - l) * sinf(a), BK, 2);
  }
  for (int k = 0; k <= K; k++) {
    float a = A((float)k / K);
    text(f0(k), (int)(cx + (R - 12) * cosf(a)), (int)(cy + (R - 12) * sinf(a) + 8), 22, k * 1000 >= maxRpm() ? WH : BK, CENTER, 900);
  }
  text("x1000 RPM", (int)cx, (int)cy - 36, 10, rgb(0xBBBBBB), CENTER, 600);
  float an = A(rpmF());
  line(cx - 14 * cosf(an), cy - 14 * sinf(an), cx + (R - 30) * cosf(an), cy + (R - 30) * sinf(an), RD, 4);
  circle(cx, cy, 9, rgb(0x777777));
  rrect(cx + 12, cy + 38, 30, 34, 4, rgb(0x0B2A12), GR, 1.5f);
  text(D.gear, (int)cx + 27, (int)cy + 67, 30, WH, CENTER, 900);
  rrect(cx + 46, cy + 38, 60, 34, 4, BK, rgb(0x888888), 1.5f);
  text(f0(D.spd), (int)cx + 102, (int)cy + 67, 30, WH, RIGHT, 900);
  rrect(cx + 12, cy + 76, 94, 30, 4, BK, rgb(0x888888), 1.5f);
  text(f0(D.rpm), (int)cx + 102, (int)cy + 101, 28, over() ? RD : YEL, RIGHT, 900);
  side5(4, 30, "BOOST", f1(D.boost), (D.boost + 1) / 2.5f);
  side5(4, 104, "TPS %", f0(D.tps), D.tps / 100);
  side5(4, 178, "LOAD %", f0(D.load), D.load / 100);
  side5(384, 30, "WATER C", f0(D.ect), (D.ect - 40) / 90, ectC());
  side5(384, 104, "AIR C", f0(D.iat), D.iat / 80);
  side5(384, 178, "BATT V", f1(D.volt), (D.volt - 10) / 6);
  obd(6, 18);
  dots();
  shiftFlash();
}

// ---------------------------------------------------------------- 6 Strip
inline void box6(int x, int y, int w, int h) { rrect(x, y, w, h, 6, rgb(0x1B1B1D), rgb(0x333333)); }
inline void sm6(int i, const char* l, const char* v, float f, uint16_t vc = WH) {
  int x = 6 + i * 94;
  box6(x, 196, 88, 60);
  text(l, x + 6, 211, 11, rgb(0xBBBBBB), LEFT, 500);
  text(v, x + 82, 240, 24, vc, RIGHT, 900);
  for (int j = 0; j < 6; j++) fill(x + 6 + j * 8, 246, 6, 4, (float)j / 6 < f ? GR : rgb(0x333333));
}
inline void p6() {
  clear(BK);
  const int K = kMax();
  float fr = rpmF(), red = redF();
  text(f0(D.rpm), 8, 50, 50, over() ? RD : WH, LEFT, 900, true);
  text("RPM", 8, 64, 11, rgb(0xBBBBBB), LEFT, 600);
  const int x0 = 150, x1 = 470, NB = 60;
  for (int i = 0; i < NB; i++) {
    float f = (float)i / NB;
    int x = (int)(x0 + f * (x1 - x0));
    fill(x, 10, 4, i % 5 ? 22 : 30, f < fr ? (f >= red ? RD : ORG) : rgb(0x3A2A10));
  }
  for (int k = 1; k <= K; k++) text(f0(k), (int)(x0 + (float)k / K * (x1 - x0) - 3), 54, 13, k * 1000 >= maxRpm() ? RD : WH, CENTER, 600);
  circle(x0 + fr * (x1 - x0), 46, 3.5f, WH);
  obd(474, 14, RIGHT);
  box6(6, 68, 140, 60);
  text("TRIP km", 12, 84, 12, rgb(0xBBBBBB), LEFT, 500);
  text(f1(fmodf(D.trip, 1000)), 140, 120, 30, WH, RIGHT, 900);
  box6(6, 132, 140, 58);
  text("FUEL %", 12, 148, 12, rgb(0xBBBBBB), LEFT, 500);
  text(f0(D.fuel), 140, 178, 30, WH, RIGHT, 900);
  for (int i = 0; i < 10; i++) fill(12 + i * 9, 180, 7, 5, (float)i / 10 < D.fuel / 100 ? GR : rgb(0x333333));
  box6(152, 68, 82, 122);
  text("GEAR", 160, 84, 12, rgb(0xBBBBBB), LEFT, 500);
  text(D.gear, 193, 176, 84, YEL, CENTER, 900);
  box6(240, 68, 234, 122);
  text("SPEED", 248, 84, 12, rgb(0xBBBBBB), LEFT, 500);
  text(f0(D.spd), 440, 170, 92, WH, RIGHT, 900);
  text("km/h", 466, 182, 12, rgb(0xBBBBBB), RIGHT, 500);
  sm6(0, "COOLANT", f0(D.ect), (D.ect - 40) / 90, ectC());
  sm6(1, "IAT C", f0(D.iat), D.iat / 80);
  sm6(2, "BATT V", f1(D.volt), (D.volt - 10) / 6);
  sm6(3, "TPS %", f0(D.tps), D.tps / 100);
  sm6(4, "LOAD %", f0(D.load), D.load / 100);
  dots();
  shiftFlash();
}

// ---------------------------------------------------------------- 7 Race dash สีส้ม
inline void p7() {
  clear(BK);
  float fr = rpmF(), sl = fmaxf(0.f, (D.rpm - maxRpm() * .55f) / (maxRpm() * .45f));
  for (int i = 0; i < 12; i++) {
    bool on = (float)i / 12 < sl;
    uint16_t c = i < 4 ? GR : (i < 8 ? YEL : RD);
    rrect(8 + i * 39, 6, 33, 16, 2, on ? (over() ? (blink() ? c : rgb(0x222222)) : c) : rgb(0x2A2A2A), rgb(0x555555));
  }
  fill(8, 28, 464, 6, rgb(0x3A1D00));
  fill(8, 28, (int)(464 * fr), 6, ORG);
  const char* L[4][2] = {{"Water TMP", f0(D.ect)}, {"Air TMP", f0(D.iat)}, {"Air Press", f1(D.map / 100)}, {"AIR FLOW", f1(D.maf)}};
  const char* R[4][2] = {{"Km/h", f0(D.spd)}, {"ENGINE L", f1(D.load)}, {"Short FT", f1(D.stft)}, {"Batt Vol", f1(D.volt)}};
  for (int i = 0; i < 4; i++) {
    int y = 58 + i * 52;
    text(L[i][0], 8, y, 17, ORG, LEFT, 700, true);
    text(L[i][1], 60, y + 26, 26, i == 0 ? ectC(ORG) : ORG, CENTER, 900, true);
    text(R[i][0], 472, y, 17, ORG, RIGHT, 700, true);
    text(R[i][1], 420, y + 26, 26, ORG, CENTER, 900, true);
  }
  text(f0(D.rpm), 236, 66, 30, over() ? RD : ORG, RIGHT, 900, true);
  text("r/min", 242, 64, 16, ORG, LEFT, 700, true);
  text("Race", 240, 100, 22, ORG, CENTER, 900);
  text(D.gear, 240, 250, 150, ORG, CENTER, 900, true);
  obd(8, 264);
  dots();
}

// ---------------------------------------------------------------- 8 Gear arc + MAX/MIN
inline void row8(int y, const char* l, const char* v, const char* tag, float f, float mk, uint16_t vc = WH) {
  text(l, 300, y, 14, rgb(0xDDDDDD), LEFT, 600);
  text(tag, 474, y, 10, YEL, RIGHT, 700);
  text(v, 300, y + 32, 30, vc, LEFT, 900);
  fill(300, y + 40, 174, 6, rgb(0x333333));
  fill(300, y + 40, (int)(174 * clamp01(f)), 6, rgb(0x3AA0FF));
  fill(300 + (int)(174 * clamp01(mk)) - 1, y + 36, 3, 14, YEL);
}
inline void p8() {
  clear(BK);
  const int K = kMax();
  float fr = rpmF(), red = redF();
  const float cx = 150, cy = 148, R = 106, a0 = M_PI * .75f, a1 = M_PI * 2.25f;
  auto A = [&](float f) { return a0 + (a1 - a0) * f; };
  arc(cx, cy, R, a0, a1, rgb(0x2C2C30), 16);
  arc(cx, cy, R, a0, A(fminf(fr, red)), rgb(0x3AA0FF), 16);
  if (fr > red) arc(cx, cy, R, A(red), A(fr), RD, 16);
  arc(cx, cy, R + 11, A(red), a1, RD, 4);
  for (int k = 0; k <= K; k++) {
    float a = A((float)k / K);
    text(f0(k), (int)(cx + (R - 28) * cosf(a)), (int)(cy + (R - 28) * sinf(a) + 7), 19, k * 1000 >= maxRpm() ? RD : rgb(0xDDDDDD), CENTER, 600);
  }
  float an = A(fr);
  line(cx + 30 * cosf(an), cy + 30 * sinf(an), cx + (R + 6) * cosf(an), cy + (R + 6) * sinf(an), ORG, 4);
  circle(cx, cy, 28, rgb(0x111111), ORG, 2);
  text("GEAR", (int)cx, (int)cy - 10, 9, ORG, CENTER, 600);
  text(D.gear, (int)cx, (int)cy + 20, 30, WH, CENTER, 900);
  text(f0(D.spd), (int)cx, (int)cy + 82, 40, WH, CENTER, 900);
  text("km/h - x1000 RPM", (int)cx, (int)cy + 100, 11, rgb(0xAAAAAA), CENTER, 500);
  obd(6, 16);
  char v[16], t[24];
  snprintf(v, 16, "%d\xC2\xB0", (int)lroundf(D.ect));
  snprintf(t, 24, "MAX %d", (int)lroundf(D.pEct));
  row8(36, "COOLANT", v, t, (D.ect - 40) / 90, (D.pEct - 40) / 90, ectC());
  snprintf(v, 16, "%d\xC2\xB0", (int)lroundf(D.iat));
  snprintf(t, 24, "MAX %d", (int)lroundf(D.pIat));
  row8(110, "IAT", v, t, D.iat / 80, D.pIat / 80);
  if (D.pVmin < 90) snprintf(t, 24, "MIN %.1f", D.pVmin); else snprintf(t, 24, "MIN --");
  row8(184, "VOLT", f1(D.volt), t, (D.volt - 10) / 6, (D.pVmin - 10) / 6);
  char pk[48];
  snprintf(pk, 48, "PEAK %d rpm   %d km/h", (int)lroundf(D.pRpm), (int)lroundf(D.pSpd));
  text(pk, 240, 256, 13, YEL, CENTER, 700);
  dots();
  shiftFlash();
}

// ---------------------------------------------------------------- 9-12 หน้ารูป
inline void p9() {                   // หน้า 13
  bg::draw();
  pt("RPM", 10, 10, PX_LAB);
  int rr = 10 + pxWidth("RPM", PX_LAB) + 12 + pxWidth("8888", PX_RPM);
  pt(f0(D.rpm), rr, 10, PX_RPM, over() ? PXR : WH, RIGHT);
  int y = 10 + max(8 * PX_RPM, lh()) + 8;
  rpmHp(8, y, PX_BAR_W, PX_BAR_H);
  y += PX_BAR_H + 12;
  pt("SPEED", 10, y, PX_LAB);
  int sr = 10 + pxWidth("SPEED", PX_LAB) + 12 + pxWidth("888", PX_SPD);
  pt(f0(D.spd), sr, y, PX_SPD, WH, RIGHT);
  pt("km/h", sr + 8, y + 8 * PX_SPD - lh(), PX_LAB);
  y += max(8 * PX_SPD, lh()) + 12;
  int vy = y + lh() + 6;
  pt("COOLANT", 10, y, PX_LAB);
  valC(f0(D.ect), 10, vy, PX_SM, ectC());
  int ix = max(10 + pxWidth("COOLANT", PX_LAB), 10 + pxWidth("110", PX_SM) + pxWidth("C", PX_LAB) + 6) + 20;
  pt("IAT", ix, y, PX_LAB);
  valC(f0(D.iat), ix, vy, PX_SM, WH);
  obd(474, 14, RIGHT);
  dots();
  shiftFlash();
}
inline void pTop() {          // 10, 11
  bg::draw();
  topBar();
  dots();
  shiftFlash();
}
inline void p12() {
  if (!bg::draw()) noPics();
  topBar();
  dots();
  shiftFlash();
}

inline void draw(int p) {
  switch (p) {
    case 0: p1(); break;
    case 1: p2(); break;
    case 2: p3(); break;
    case 3: p4(); break;
    case 4: p5(); break;
    case 5: p6(); break;
    case 6: p7(); break;
    case 7: p8(); break;
    case 8: pages2::redstripe(); dots(); break;
    case 9: pages2::ecu(); dots(); shiftFlash(); break;
    case 10: pages2::record(); dots(); break;
    case 11: pages2::pids(); dots(); break;
    case 12: p9(); break;
    case 13: case 14: pTop(); break;
    default: p12(); break;
  }
}

}  // namespace pages
