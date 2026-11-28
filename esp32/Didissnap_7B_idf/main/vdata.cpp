// vdata.cpp — เก็บค่าจาก DN_GW / DN_CUST / DN_GWINFO + โหมดจำลอง
#include "vdata.h"
#include <math.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>
#include "didinow.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"

namespace vd {

const char* const KEYS[NKEY] = {"rpm", "spd", "ect", "iat", "volt", "load", "tps", "map", "maf", "fuel", "stft", "afr",
                                "timing", "oil", "cvt", "c1", "c2", "c3", "c4", "boost", "gear", "trip", "link"};
const char* const NAMES_TH[NKEY] = {"รอบ", "ความเร็ว", "น้ำ", "อากาศเข้า", "แบต", "โหลด", "คันเร่ง", "MAP", "MAF", "น้ำมัน",
                                    "STFT", "AFR", "จุดระเบิด", "น้ำมันเครื่อง", "เกียร์ CVT", "C1", "C2", "C3", "C4", "บูสต์", "เกียร์", "ระยะทาง", "เชื่อมต่อ"};

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static Snap cur;
static int64_t lastUs = 0;
static bool demoOn = false;
static char infoTxt[64] = "";

static inline int64_t nowUs() { return esp_timer_get_time(); }

int keyOf(const char* name) {
  for (int i = 0; i < NKEY; i++) if (!strcasecmp(name, KEYS[i])) return i;
  return -1;
}

static float ratios[NGEAR];
static float pkMax[NKEY], pkMin[NKEY];
static bool pkHave[NKEY];
static float lastRatio = 0;

void resetPeaks() { memset(pkHave, 0, sizeof(pkHave)); }

static void gearsLoad() {
  memset(ratios, 0, sizeof(ratios));
  nvs_handle_t h;
  if (nvs_open("gear", NVS_READONLY, &h) != ESP_OK) return;
  for (int i = 0; i < NGEAR; i++) {
    char k[4] = {'r', (char)('1' + i), 0};
    int32_t v;
    if (nvs_get_i32(h, k, &v) == ESP_OK) ratios[i] = v / 100.f;
  }
  nvs_close(h);
}
static void gearsSave() {
  nvs_handle_t h;
  if (nvs_open("gear", NVS_READWRITE, &h) != ESP_OK) return;
  for (int i = 0; i < NGEAR; i++) {
    char k[4] = {'r', (char)('1' + i), 0};
    nvs_set_i32(h, k, (int32_t)lroundf(ratios[i] * 100.f));
  }
  nvs_commit(h);
  nvs_close(h);
}
float gearRatio(int i) { return i >= 0 && i < NGEAR ? ratios[i] : 0; }
float liveRatio() { return lastRatio; }
bool learnGear(int i) {
  if (i < 0 || i >= NGEAR || lastRatio <= 0) return false;
  ratios[i] = lastRatio;
  gearsSave();
  return true;
}
void clearGears() {
  memset(ratios, 0, sizeof(ratios));
  gearsSave();
}

void begin() {
  memset(&cur, 0, sizeof(cur));
  cur.link = 1;
  gearsLoad();
  resetPeaks();
}

void setDemo(bool on) { demoOn = on; }
bool demo() { return demoOn; }
const char* info() { return infoTxt; }

static inline void put(int k, float v) {
  if (v <= -998.f) { cur.have[k] = false; return; }   // -999 = รถไม่รองรับ
  cur.v[k] = v;
  cur.have[k] = true;
}

void feed(const uint8_t* d, int n) {
  if (n < 6) return;
  uint32_t magic;
  memcpy(&magic, d, 4);
  if (magic != DIDINOW_MAGIC) return;
  uint8_t t = d[4];
  if (t == DN_GW && n >= (int)offsetof(DidiNowGw, timing)) {
    DidiNowGw g;
    memset(&g, 0, sizeof(g));
    memcpy(&g, d, (size_t)n < sizeof(g) ? (size_t)n : sizeof(g));
    bool hasTO = n >= (int)sizeof(DidiNowGw);
    portENTER_CRITICAL(&mux);
    cur.link = g.link;
    cur.proto = g.proto;
    cur.rate = g.rate;
    put(RPM, g.rpm); put(SPD, g.spd); put(ECT, g.ect); put(IAT, g.iat); put(VOLT, g.volt); put(LOAD, g.load);
    put(TPS, g.tps); put(MAP, g.map); put(MAF, g.maf); put(FUEL, g.fuel); put(STFT, g.stft);
    if (g.afrMode) put(AFR, g.afr); else cur.have[AFR] = false;
    if (hasTO) { put(TIMING, g.timing); put(OIL, g.oil); }
    if (cur.have[MAP]) { cur.v[BOOST] = (g.map - 101.3f) / 100.f; cur.have[BOOST] = true; }   // bar (ประมาณ)
    lastUs = nowUs();
    portEXIT_CRITICAL(&mux);
    return;
  }
  if (t == DN_CUST && n >= (int)sizeof(DidiNowCust)) {
    const DidiNowCust* m = (const DidiNowCust*)d;
    static const int map[DN_NCUST] = {C1, C2, C3, C4, CVT};
    portENTER_CRITICAL(&mux);
    for (int i = 0; i < DN_NCUST; i++) {
      if ((m->have >> i) & 1) { cur.v[map[i]] = m->v[i]; cur.have[map[i]] = true; }
      else if (!((m->on >> i) & 1)) cur.have[map[i]] = false;
    }
    portEXIT_CRITICAL(&mux);
    return;
  }
  if (t == DN_GWINFO && n >= (int)sizeof(DidiNowInfo)) {
    const DidiNowInfo* m = (const DidiNowInfo*)d;
    portENTER_CRITICAL(&mux);
    strncpy(infoTxt, m->text, sizeof(infoTxt) - 1);
    infoTxt[sizeof(infoTxt) - 1] = 0;
    portEXIT_CRITICAL(&mux);
  }
}

// โหมดจำลอง: เร่ง-เปลี่ยนเกียร์วนไป
static void demoFill(Snap& s) {
  static float rpm = 900, spd = 0;
  static int gear = 1;
  static int64_t last = 0;
  int64_t now = nowUs();
  float dt = last ? fminf(0.1f, (now - last) / 1e6f) : 0.016f;
  last = now;
  static float tp = 0;
  tp += dt;
  bool acc = fmodf(tp, 14.f) < 10.f;
  if (acc) {
    rpm += (2600.f / gear) * dt * 1.6f;
    if (rpm > 7200 && gear < 6) { gear++; rpm = 4600; }
    if (rpm > 7400) rpm = 7400;
  } else {
    rpm -= 1800 * dt;
    if (rpm < 1500 && gear > 1) { gear--; rpm = 3600; }
    if (rpm < 850) rpm = 850;
  }
  static const float ratio[7] = {0, 120, 72, 50, 38, 31, 26};
  spd = rpm / ratio[gear];
  memset(&s, 0, sizeof(s));
  s.v[RPM] = rpm; s.v[SPD] = spd; s.v[ECT] = 88; s.v[IAT] = 36 + sinf(tp * .3f) * 3; s.v[VOLT] = 14.1f + sinf(tp) * .1f;
  s.v[LOAD] = acc ? 75 : 18; s.v[TPS] = acc ? 82 : 0; s.v[MAP] = acc ? 160 : 35; s.v[MAF] = rpm / 120.f;
  s.v[FUEL] = 67; s.v[STFT] = 1.6f; s.v[AFR] = acc ? 12.4f : 14.7f; s.v[TIMING] = acc ? 18 : 8; s.v[OIL] = 96;
  s.v[CVT] = 74; s.v[BOOST] = (s.v[MAP] - 101.3f) / 100.f; s.v[GEAR] = gear;
  for (int i = 0; i < NKEY; i++) s.have[i] = true;
  s.have[C1] = s.have[C2] = s.have[C3] = s.have[C4] = false;
  s.link = 0;
  s.rate = 40;
  s.live = true;
}

// เกียร์: หาเกียร์ที่อัตราส่วน รอบ/ความเร็ว ใกล้สุด (ห่างไม่เกิน 7%) · จอด = N · ไม่ตรงเกียร์ไหน (เหยียบคลัตช์/CVT) = "-"
static void calcGear(Snap& s) {
  lastRatio = 0;
  if (!s.have[RPM] || !s.have[SPD]) return;
  float rpm = s.v[RPM], spd = s.v[SPD];
  if (spd >= 5 && rpm >= 500) lastRatio = rpm / spd;
  if (s.have[GEAR]) return;            // โหมดจำลองมีเกียร์ให้อยู่แล้ว
  int n = 0;
  for (int i = 0; i < NGEAR; i++) if (ratios[i] > 0) n++;
  if (!n) return;                      // ยังไม่ได้สอนเกียร์
  if (spd < 3) { s.v[GEAR] = 0; s.have[GEAR] = true; return; }
  if (lastRatio <= 0) return;
  int best = -1;
  float bestErr = 0.07f;
  for (int i = 0; i < NGEAR; i++) {
    if (ratios[i] <= 0) continue;
    float e = fabsf(lastRatio / ratios[i] - 1.f);
    if (e < bestErr) { bestErr = e; best = i; }
  }
  if (best >= 0) { s.v[GEAR] = best + 1; s.have[GEAR] = true; }
}
static void peaks(Snap& s) {
  if (s.live) {
    for (int k = 0; k < NKEY; k++) {
      if (!s.have[k] || k == GEAR) continue;
      float v = s.v[k];
      if (!pkHave[k]) { pkMax[k] = pkMin[k] = v; pkHave[k] = true; }
      else { if (v > pkMax[k]) pkMax[k] = v; if (v < pkMin[k]) pkMin[k] = v; }
    }
  }
  memcpy(s.pmax, pkMax, sizeof(pkMax));
  memcpy(s.pmin, pkMin, sizeof(pkMin));
  memcpy(s.phave, pkHave, sizeof(pkHave));
}

void get(Snap& s) {
  if (demoOn) demoFill(s);
  else {
    portENTER_CRITICAL(&mux);
    s = cur;
    portEXIT_CRITICAL(&mux);
    s.live = lastUs && nowUs() - lastUs < 1500000 && s.link == 2;
    s.have[GEAR] = false;
  }
  calcGear(s);
  // ระยะทางตั้งแต่เปิดจอ (km) จากความเร็ว · สถานะเชื่อมต่อรถ (1 = ได้ค่าจากรถ/จำลอง)
  static double tripKm = 0;
  static int64_t tLast = 0;
  int64_t t = nowUs();
  if (tLast && s.live && s.have[SPD]) tripKm += s.v[SPD] * ((t - tLast) / 3.6e9);
  tLast = t;
  s.v[TRIP] = (float)tripKm;
  s.have[TRIP] = true;
  s.v[LINK] = s.live ? 1 : 0;
  s.have[LINK] = true;
  peaks(s);
}

}  // namespace vd
