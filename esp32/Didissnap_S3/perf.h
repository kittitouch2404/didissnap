// perf.h — จับเวลาออกตัว (หน้า Record) กติกาเดียวกับแอป: 60ft, 0-60, 0-100, 60-100, 100-150, 201m, 402m
#pragma once
#include <Arduino.h>

namespace perf {

enum State { IDLE, WAIT, READY, RUN, DONE };
static const int NI = 7;
static const char* const KEY[NI] = {"60ft", "0-60", "0-100", "60-100", "100-150", "201m", "402m"};
static const char* const LABEL[NI] = {"0 - 60 ft (18 m)", "0 - 60 km/h", "0 - 100 km/h", "60 - 100 km/h", "100 - 150 km/h", "1/8 mile (201 m)", "1/4 mile (402 m)"};

static State state = IDLE;
static float times[NI], traps[NI], best[NI];
static float t150 = -1;                   // 0-150 (ใช้คำนวณ 100-150)
static float dist = 0, el = 0, top = 0, t0 = 0, lt = 0, lv = 0, zt = 0;
static int runs = 0;

inline float nowS() { return millis() / 1000.f; }
inline void clearRun() {
  for (int i = 0; i < NI; i++) { times[i] = -1; traps[i] = -1; }
  t150 = -1; dist = 0; el = 0; top = 0;
}
inline void begin() { clearRun(); for (int i = 0; i < NI; i++) best[i] = -1; }

inline void finish() {
  if (times[1] >= 0 && times[2] >= 0) times[3] = times[2] - times[1];
  if (times[2] >= 0 && t150 >= 0) times[4] = t150 - times[2];
  bool any = false;
  for (int i = 0; i < NI; i++) if (times[i] >= 0) { any = true; if (best[i] < 0 || times[i] < best[i]) best[i] = times[i]; }
  if (any) runs++;
  state = DONE;
}
inline void arm(float speed) { clearRun(); zt = nowS(); state = speed < .5f ? READY : WAIT; }
inline void stop() { if (state == RUN) finish(); else state = IDLE; }
// ปุ่ม 2 ค้างบนหน้า Record
inline void toggle(float speed) { if (state == RUN || state == READY || state == WAIT) stop(); else arm(speed); }

// ป้อนความเร็ว (km/h) ทุกครั้งที่ได้ค่าใหม่
inline void sample(float v) {
  float t = nowS();
  if (state == WAIT) { if (v < .5f) { zt = t; state = READY; } return; }
  if (state == READY) {
    if (v < .5f) { zt = t; return; }
    t0 = zt; lt = t0; lv = 0; dist = 0; state = RUN;
  }
  if (state != RUN) return;
  float dt = t - lt;
  if (dt <= 0) return;
  float d0 = dist;
  dist += (lv + v) / 2 / 3.6f * dt;
  const float sp[3] = {60, 100, 150};
  const int si[3] = {1, 2, -1};
  for (int k = 0; k < 3; k++) {
    float tg = sp[k];
    bool unset = si[k] >= 0 ? times[si[k]] < 0 : t150 < 0;
    if (unset && v >= tg && lv < tg) {
      float tt = lt + dt * (tg - lv) / (v - lv) - t0;
      if (si[k] >= 0) times[si[k]] = tt; else t150 = tt;
    }
  }
  const float dg[3] = {18.288f, 201.168f, 402.336f};
  const int di[3] = {0, 5, 6};
  for (int k = 0; k < 3; k++) {
    if (times[di[k]] < 0 && dist >= dg[k] && d0 < dg[k]) {
      float f = (dg[k] - d0) / (dist - d0);
      times[di[k]] = lt + dt * f - t0;
      traps[di[k]] = lv + (v - lv) * f;
    }
  }
  top = fmaxf(top, v);
  lt = t; lv = v; el = t - t0;
  if (v < .5f || el > 90 || (times[6] >= 0 && times[2] >= 0)) finish();
}

}  // namespace perf
