// pages2.h — หน้าที่ยกมาจากแอป: Red stripe (Civic แดง), ECU (FT450), Record (จับเวลา), ตาราง OBD2
// แอปวาดบนพื้นที่ 1000x460 — บอร์ดแปลงตำแหน่งเป็น 480x272 (ขนาดตัวอักษร/วงกลม คูณ K)
#pragma once
#include "gfx.h"
#include "data.h"
#include "perf.h"

namespace pages2 {
using namespace gfx;

constexpr uint16_t ORG = rgb(0xFF7A00), YEL = rgb(0xFFC400), RD = rgb(0xFF2A2A), GR = rgb(0x00E05A), BL = rgb(0x2E7BFF),
                   GRY = rgb(0x8A8A8A), WH = 0xFFFF, BK = 0;
static const float SX = 0.48f, SY = 272.f / 460.f, K = 0.555f;
inline float X(float v) { return v * SX; }
inline float Y(float v) { return v * SY; }

// เหมือน txt() ของแอป: ax -1 ซ้าย / 0 กลาง / 1 ขวา, ay -1 บน / 0 กลาง / 1 ล่าง (ตำแหน่งเป็นพิกัด 1000x460)
inline void T(const char* s, float px, float py, float size, uint16_t c, int ax = -1, int ay = -1, int w = 700, bool it = false) {
  float sz = size * K, top = Y(py) - (ay + 1) / 2.f * sz;
  text(s, (int)lroundf(X(px)), (int)lroundf(top + sz * .80f), sz, c, ax < 0 ? LEFT : (ax == 0 ? CENTER : RIGHT), w, it);
}

static char tbs[2][8][24];   // ข้อความชั่วคราว (แยกต่อคอร์)
static int tbis[2] = {0, 0};
inline const char* f0(float v) { char* b = tbs[gfx::core()][tbis[gfx::core()]++ & 7]; snprintf(b, 24, "%d", (int)lroundf(v)); return b; }
inline const char* f1(float v) { char* b = tbs[gfx::core()][tbis[gfx::core()]++ & 7]; snprintf(b, 24, "%.1f", v); return b; }
inline const char* f2(float v) { char* b = tbs[gfx::core()][tbis[gfx::core()]++ & 7]; snprintf(b, 24, "%.2f", v); return b; }
inline int redline() { return didicfg::cfg.maxRpm; }
inline int scaleMax() { return (int)ceilf(redline() / 1000.f) * 1000 + 1000; }
inline int shiftStart() { return redline() - 2000; }
inline bool atRed() { return D.rpm >= redline(); }
inline bool hot() { return D.ect >= didicfg::cfg.tempAlert; }
inline bool blink() { return (gfx::nowMs() / 125) % 2 == 0; }
inline float cl01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// ---------------------------------------------------------------- 9 Red stripe (Civic แดง)
inline void redstripe() {
  const int max_ = scaleMax();
  clear(rgb(0x161616));
  {                                                                                      // ลายคาร์บอน (เส้นทแยงทุก 6 px)
    const uint16_t cc = rgb(0x262626);
    for (int y = GY0; y < GY1; y++) { uint16_t* rp = GBUF + y * W; for (int x = ((y % 6) + 6) % 6; x < W; x += 6) rp[x] = cc; }
  }
  const int sx = (int)X(12), sy = (int)Y(12), sw = (int)X(976), sh = (int)Y(410);
  for (int i = 0; i < 9; i++) fill(sx, sy + i * sh / 9, sw, sh / 9 + 1, i % 2 ? rgb(0xB80E18) : rgb(0xD01420));
  // ค่าทางขวา 4 แถว
  const char* rows[4][3] = {{f0(D.iat), "\xC2\xB0" "C", "INTAKE AIR TEMP"}, {f0(D.ect), "\xC2\xB0" "C", "WATER TEMP"},
                            {f1(D.volt), "V", "BATTERY"}, {f0(D.load), "%", "ENGINE LOAD"}};
  float top = 12 + 34, rh = (410 - 34) / 4.f;
  for (int i = 0; i < 4; i++) {
    float y = top + i * rh - 8;
    fillA((int)X(560), (int)Y(y + 4), (int)X(415), (int)Y(rh - 6), BK, i % 2 ? 56 : 20);
    bool h = i == 1 && hot();
    T(rows[i][0], 930, y + 60, 58, h && blink() ? rgb(0xFFE14A) : WH, 1, 1, 900, true);
    T(rows[i][1], 975, y + 30, 20, WH, 1, 0, 800);
    T(rows[i][2], 975, y + 68, 24, WH, 1, -1, 800);
  }
  // เกจ
  const float cx = X(265), cy = Y(232), R = 225 * K, a0 = 120 * M_PI / 180, sw2 = 300 * M_PI / 180;
  auto ang = [&](float v) { return a0 + sw2 * cl01(v / max_); };
  circle(cx, cy, R, rgb(0x0C0C0C));
  for (int i = 0; i < 100; i++) {
    float v = (i + .5f) / 100 * max_, a = ang(v), ca = cosf(a), sa = sinf(a);
    bool lit = v < D.rpm;
    uint16_t c = lit ? (v >= redline() ? rgb(0xFF3A3A) : rgb(0xF2F2F2)) : rgb(0x5A5A5A);
    line(cx + ca * (R * .9f - 7 * K), cy + sa * (R * .9f - 7 * K), cx + ca * (R * .9f + 7 * K), cy + sa * (R * .9f + 7 * K), c, 8 * K);
  }
  for (int k = 0; k * 1000 <= max_; k++) {
    float a = ang(k * 1000), ca = cosf(a), sa = sinf(a);
    line(cx + ca * (R * .97f - 6 * K), cy + sa * (R * .97f - 6 * K), cx + ca * (R * .97f + 6 * K), cy + sa * (R * .97f + 6 * K), RD, 10 * K);
    float tx = cx + ca * R * .76f, ty = cy + sa * R * .76f, sz = 34 * K;
    text(f0(k), (int)tx, (int)(ty - sz / 2 + sz * .8f), sz, k * 1000 >= redline() ? rgb(0xFF6A6A) : rgb(0xE8E8E8), CENTER, 900, true);
  }
  const float ri = R * .6f;
  arc(cx, cy, ri, 0, 6.2832f, rgb(0xB8B8B8), 14 * K);                                   // วงแหวนเงิน
  radial(cx, cy, ri - 7 * K, rgb(0x2A0000), rgb(0x8A0008), rgb(0xE0101C), .55f, 40 * K); // หน้าปัดแดง
  float pa = ang(D.rpm), pc = cosf(pa), ps = sinf(pa), pr = ri + 22 * K;                // เข็มเหลือง
  auto rot = [&](float lx, float ly, float& ox, float& oy) { ox = cx + pc * pr + (lx * pc - ly * ps) * K; oy = cy + ps * pr + (lx * ps + ly * pc) * K; };
  float xs[3], ys[3];
  rot(-14, 0, xs[0], ys[0]); rot(10, -12, xs[1], ys[1]); rot(10, 12, xs[2], ys[2]);
  poly(xs, ys, 3, rgb(0xFFD21E));
  float gy = cy - ri;                                                                    // ช่องเกียร์ทรงคางหมู
  float tx[4] = {cx - 80 * K, cx + 80 * K, cx + 52 * K, cx - 52 * K}, ty[4] = {gy + 4 * K, gy + 4 * K, gy + 70 * K, gy + 70 * K};
  poly(tx, ty, 4, rgb(0xE0141E));
  text(D.gear, (int)cx, (int)(gy + 38 * K + 56 * K * .3f), 56 * K, atRed() && blink() ? rgb(0xFFE14A) : WH, CENTER, 900);
  text(f0(D.rpm), (int)cx, (int)(cy + 12 * K + 86 * K * .3f), 86 * K, WH, CENTER, 900, true);
  fill((int)(cx - 100 * K), (int)(cy + 58 * K), (int)(200 * K), 2, WH);
  text(f0(D.spd), (int)cx, (int)(cy + 92 * K + 50 * K * .3f), 50 * K, WH, CENTER, 900);
  text("KMH", (int)cx, (int)(cy + 124 * K + 20 * K * .3f), 20 * K, WH, CENTER, 800);
  // แถบไฟ shift สีเงินด้านล่าง
  int by = (int)Y(460 - 34);
  fill(sx, by, sw, (int)Y(26), rgb(0xC8C8C8));
  int lit = (int)ceilf(cl01((D.rpm - shiftStart()) / (float)max(1, redline() - shiftStart())) * 10);
  for (int i = 0; i < 10; i++) {
    uint16_t c = i < lit ? (i < 4 ? GR : (i < 7 ? YEL : RD)) : rgb(0x3A3A3A);
    if (atRed()) c = blink() ? BL : rgb(0x3A3A3A);
    rrect(X(330 + i * 36), by + Y(5), X(28), Y(16), 2, c);
  }
}

// ---------------------------------------------------------------- 10 ECU (FT450)
inline void ecu() {
  clear(BK);
  const int max_ = scaleMax();
  const uint16_t Yc = rgb(0xF5C518);
  T(f0(D.rpm), 20, 24, 72, atRed() && blink() ? RD : WH, -1, -1, 900);
  T("RPM", 250, 74, 24, WH, -1, 1, 700);
  const float bx = 20, bw = 960, by = 104, bh = 30;
  fill((int)X(bx), (int)Y(by), (int)X(bw), (int)Y(bh), rgb(0x1A1A1A));
  float t = cl01(D.rpm / max_), s1 = (float)shiftStart() / max_, s2 = (float)redline() / max_;
  int px0 = (int)X(bx), pw = (int)(X(bw) * t);
  for (int i = 0; i < pw; i++) {                         // ไล่สี เหลือง → ส้ม → แดง
    float f = (float)i / X(bw);
    uint16_t c = f < s1 ? lerp565(rgb(0xFFE600), rgb(0xFFB000), f / s1)
               : f < s2 ? lerp565(rgb(0xFFB000), rgb(0xFF5A00), (f - s1) / (s2 - s1)) : lerp565(rgb(0xFF5A00), rgb(0xFF1010), (f - s2) / (1 - s2));
    fill(px0 + i, (int)Y(by), 1, (int)Y(bh), c);
  }
  for (int k = 1; k * 1000 <= max_; k++) {
    float x = bx + bw * k * 1000 / max_;
    fill((int)X(x) - 1, (int)Y(by), 2, (int)Y(bh), BK);
    T(f0(k), x, by + bh + 6, 18, k * 1000 >= redline() ? RD : rgb(0xDDDDDD), 0);
  }
  auto lab = [&](const char* s, float x, float y, int a = -1) { T(s, x, y, 20, Yc, a, -1, 700); };
  lab("TRIP km", 250, 170, 1); T(f1(D.trip), 250, 196, 30, WH, 1, -1, 800);
  lab("FUEL %", 250, 240, 1); T(f0(D.fuel), 250, 266, 30, WH, 1, -1, 800);
  lab("GEAR", 430, 170, 0); T(D.gear, 430, 268, 140, WH, 0, 0, 900);
  lab("SPEED", 920, 170, 1); T(f0(D.spd), 920, 268, 140, WH, 1, 0, 900); T("km/h", 975, 320, 20, rgb(0xCCCCCC), 1, 1);
  const char* cols[5][2] = {{"WATER \xC2\xB0" "C", f0(D.ect)}, {"BATT V", f1(D.volt)}, {"BOOST bar", f2(D.boost)}, {"LOAD %", f0(D.load)}, {"IAT \xC2\xB0" "C", f0(D.iat)}};
  for (int i = 0; i < 5; i++) {
    float x = 100 + i * 200;
    lab(cols[i][0], x, 342, 0);
    T(cols[i][1], x, 368, i == 2 ? 40 : 34, i == 0 && hot() ? (blink() ? RD : rgb(0xFF8A3A)) : WH, 0, -1, 900);
  }
  auto box = [&](float x, float w, const char* title, float frac, uint16_t col) {
    frame((int)X(x), (int)Y(418), (int)X(w), (int)Y(34), rgb(0x666666), 1);
    fill((int)X(x + 2), (int)Y(420), (int)((X(w) - 2) * cl01(frac)), (int)Y(30), col);
    T(title, x + w / 2, 435, 16, BK, 0, 0, 800);
  };
  char tp[16];
  snprintf(tp, 16, "TPS %d%%", (int)lroundf(D.tps));
  box(20, 180, tp, D.tps / 100, rgb(0x2EC84A));
  box(220, 360, "FUEL", D.fuel / 100, rgb(0x2EC84A));
  for (int v = 0; v <= 100; v += 50) T(f0(v), 220 + 360 * v / 100.f, 412, 13, rgb(0xAAAAAA), 0, 1);
  box(600, 180, "ENGINE", 1, D.rpm > 400 ? (hot() ? RD : rgb(0x2EC84A)) : rgb(0x555555));
  box(800, 180, "LINK", 1, D.link == 2 ? rgb(0x2EC84A) : YEL);
}

// ---------------------------------------------------------------- 11 Record (จับเวลาออกตัว) — ปุ่ม 2 ค้าง = ARM / STOP
inline void record() {
  using namespace perf;
  clear(BK);
  const char* msg;
  uint16_t sc;
  switch (state) {
    case WAIT: msg = "STOP THE CAR FIRST..."; sc = YEL; break;
    case READY: msg = "READY - GO!"; sc = blink() ? GR : rgb(0x0E5A26); break;
    case RUN: msg = "TIMING..."; sc = ORG; break;
    case DONE: msg = "DONE - HOLD BTN2 TO ARM"; sc = BL; break;
    default: msg = "HOLD BTN2 TO ARM"; sc = GRY;
  }
  rrect(8, 8, 196, 24, 6, blend(sc, BK, 40), sc, 1.5f);
  text(msg, 16, 25, 13, sc, LEFT, 800);
  bool show = state == RUN || state == DONE;
  char b[24];
  snprintf(b, 24, "%.2f", show ? el : 0.f);
  uint16_t tc = state == RUN ? WH : rgb(0xB3B3B3);
  text(b, 8, 100, 58, tc, LEFT, 900, true);
  text("s", 14 + textWidth(b, 58, 900, true), 100, 22, tc, LEFT, 900, true);
  auto big = [&](const char* v, const char* u, int x) {
    text(v, x, 150, 22, WH, LEFT, 900);
    text(u, x + textWidth(v, 22, 900) + 3, 150, 10, GRY, LEFT, 600);
  };
  big(f0(D.spd), "km/h", 8);
  snprintf(b, 24, "%d", show ? (int)dist : 0);
  big(b, "m", 92);
  if (show) snprintf(b, 24, "%d", (int)top); else strcpy(b, "--");
  big(b, "top", 150);
  text("BTN2 HOLD = ARM / STOP", 8, 186, 11, GRY, LEFT, 600);
  snprintf(b, 24, "runs: %d", runs);
  text(b, 8, 204, 11, GRY, LEFT, 600);
  text("OBD speed: +/-0.1-0.3 s", 8, 238, 10, rgb(0x666666), LEFT, 500);
  text("Track / closed area only", 8, 252, 10, rgb(0x666666), LEFT, 500);
  // ตาราง
  const int x0 = 214, x1 = 474;
  auto row = [&](int y, const char* a, const char* bb, const char* c, bool hd, bool hl) {
    text(a, x0 + 4, y, hd ? 10 : 12, hd ? GRY : WH, LEFT, hd ? 600 : 700);
    text(bb, x0 + 190, y, hd ? 10 : 13, hd ? GRY : (hl ? ORG : WH), RIGHT, 700);
    text(c, x1 - 4, y, hd ? 10 : 13, hd ? GRY : rgb(0x8FD3FF), RIGHT, 700);
    fill(x0, y + 6, x1 - x0, 1, rgb(0x262626));
  };
  row(22, "ITEM", "THIS RUN", "BEST", true, false);
  for (int i = 0; i < NI; i++) {
    char v[24], bs[16];
    if (times[i] < 0) strcpy(v, "--");
    else if (traps[i] >= 0) snprintf(v, 24, "%.2f s @%d", times[i], (int)lroundf(traps[i]));
    else snprintf(v, 24, "%.2f s", times[i]);
    if (best[i] < 0) strcpy(bs, "--"); else snprintf(bs, 16, "%.2f s", best[i]);
    row(50 + i * 30, LABEL[i], v, bs, false, times[i] >= 0 && times[i] == best[i]);
  }
}

// ---------------------------------------------------------------- 12 ตารางค่า OBD2
inline void pids() {
  clear(BK);
  float tp = D.tps, T_ = gfx::nowMs() / 1000.f;
  char v[30][16];
  int n = 0;
  auto S = [&](const char* fmt, float x) { snprintf(v[n++], 16, fmt, x); };
  const char* id[30] = {"01", "03", "04", "05", "06", "07", "0B", "0C", "0D", "0E", "0F", "10", "11", "14", "15",
                        "1C", "1F", "2F", "31", "33", "3C", "42", "43", "44", "45", "46", "49", "4C", "51", "5C"};
  const char* nm[30] = {"MIL / DTC count", "Fuel system", "Engine load", "Coolant temp", "Short fuel trim B1", "Long fuel trim B1",
                        "MAP (intake press)", "Engine RPM", "Vehicle speed", "Timing advance", "Intake air temp", "MAF air flow",
                        "Throttle position", "O2 sensor B1S1", "O2 sensor B1S2", "OBD standard", "Run time since start",
                        "Fuel level", "Distance since clear", "Barometric press", "Catalyst temp B1S1", "ECU voltage", "Absolute load",
                        "Commanded AFR", "Relative throttle", "Ambient air temp", "Accel pedal D", "Commanded throttle", "Fuel type", "Engine oil temp"};
  const char* un[30] = {"", "", "%", "\xC2\xB0" "C", "%", "%", "kPa", "rpm", "km/h", "\xC2\xB0", "\xC2\xB0" "C", "g/s", "%", "V", "V",
                        "", "s", "%", "km", "kPa", "\xC2\xB0" "C", "V", "%", "lam", "%", "\xC2\xB0" "C", "%", "%", "", "\xC2\xB0" "C"};
  strcpy(v[n++], "OFF / 0"); strcpy(v[n++], "Closed loop"); S("%.1f", D.load); S("%.0f", D.ect); S("%.1f", D.stft); S("%.1f", 2.3f);
  S("%.0f", D.map); S("%.0f", D.rpm); S("%.0f", D.spd); S("%.1f", 12 + tp / 8); S("%.0f", D.iat); S("%.2f", D.maf); S("%.1f", tp);
  S("%.3f", .45f + sinf(T_ * 5) * .4f); S("%.3f", .7f); strcpy(v[n++], "6"); S("%.0f", T_); S("%.1f", D.fuel); S("%.0f", 1234);
  S("%.0f", 101); S("%.0f", 620 + tp * 2); S("%.2f", D.volt); S("%.1f", D.load); S("%.3f", tp > 0 ? .88f : 1.f); S("%.1f", tp * .94f);
  S("%.0f", 32); S("%.1f", tp * .86f + 11.8f); S("%.1f", tp * .94f); strcpy(v[n++], "Gasoline"); S("%.0f", fminf(98, D.ect + 4));
  text("OBD2 LIVE DATA", 8, 17, 14, ORG, LEFT, 900);
  text(D.link == 2 ? "30 PIDs - polled in turn" : "DEMO values", 140, 17, 10, rgb(0x888888), LEFT, 500);
  const int half = 15, rh = 16, y0 = 24;
  for (int i = 0; i < 30; i++) {
    int c = i / half, x = 6 + c * 238, y = y0 + (i % half) * rh;
    text(id[i], x, y + 12, 9, rgb(0x6A8FB0), LEFT, 700);
    text(nm[i], x + 18, y + 12, 10, rgb(0xB8B8B8), LEFT, 500);
    text(v[i], x + 206, y + 12, 12, WH, RIGHT, 800);
    text(un[i], x + 209, y + 12, 9, GRY, LEFT, 500);
    fill(x, y + rh - 1, 232, 1, rgb(0x242424));
  }
}

}  // namespace pages2
