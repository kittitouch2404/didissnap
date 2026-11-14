// data.h — ค่ารถที่ใช้วาดจอ + โหมดจำลอง (DEMO) + เดาเกียร์ + ค่าสูงสุด/ทริป
#pragma once
#include <Arduino.h>
#include "DidissnapConfig.h"

struct CarData {
  // ค่าที่แสดง (ผ่านการหน่วงให้นุ่มแล้ว)
  float rpm = 0, spd = 0, ect = 0, iat = 0, volt = 0, load = 0, tps = 0, map = 0, maf = 0, fuel = 0;
  float boost = 0, stft = 0;
  float afr = 0;          // AFR (หรือแรงดัน O2 ถ้ารถมีแค่ narrowband — ดู RAW.afrMode)
  char gear[3] = "N";
  float trip = 0;
  float pRpm = 0, pSpd = 0, pEct = 0, pIat = 0, pVmin = 99;   // ค่าสูงสุด/ต่ำสุด
  int link = 0;   // 0 = DEMO, 1 = กำลังต่อ OBD, 2 = OBD OK
};
static CarData D;

// ค่าดิบล่าสุดจาก OBD (ตัวอ่าน OBD ใส่ค่าที่นี่ แล้ว data::update() หน่วงให้นุ่ม)
struct RawData {
  float rpm = 850, spd = 0, ect = 45, iat = 36, volt = 14.1f, load = 18, tps = 0, map = 32, maf = 2, fuel = 67, stft = 0;
  float afr = 14.7f;      // AFR จาก OBD (afrMode 1/3) หรือแรงดัน O2 narrowband (afrMode 2)
  int afrMode = 1;        // 0 = รถไม่มีให้อ่าน, 1 = wideband (ค่าจริง), 2 = narrowband (แค่หนา/บาง), 3 = ค่าที่ ECU สั่ง
  float rpmRate = 0;      // รอบ/วินาที ที่เปลี่ยน (จากตัวอ่าน OBD) ใช้เดาค่าระหว่างรอคำตอบ
  uint32_t rpmT = 0;      // millis() ตอนได้ค่ารอบล่าสุด (0 = ไม่มี/โหมดจำลอง)
};
static RawData RAW;

namespace data {

// สปริงหน่วงแบบไม่เด้งเกิน (critically damped) เหมือนในแอป
struct Smooth {
  float x = 0, v = 0;
  bool init = false;
  void step(float t, float dt, float w) {
    if (!init) { x = t; init = true; return; }
    float f = 1 + 2 * dt * w, oo = w * w, hoo = dt * oo, hhoo = dt * hoo, inv = 1 / (f + hhoo);
    float nx = (f * x + dt * v + hhoo * t) * inv, nv = (v + hoo * (t - x)) * inv;
    x = nx;
    v = nv;
  }
};
static Smooth sRpm, sSpd, sEct, sIat, sVolt, sLoad, sTps, sMap, sMaf, sFuel;

// ---------------------------------------------------------------- โหมดจำลอง (ขับเร่ง-เปลี่ยนเกียร์-ชะลอ วนไป)
static float simRpm = 850, simT = 0, idle = 2;
static int simGear = 1;
static bool accel = false;
inline void sim(float dt) {
  const auto& c = didicfg::cfg;
  int n = c.gearCount;
  float red = c.maxRpm - 300;
  simT += dt;
  float tps;
  auto kmh = [&](int g) { return 1000.f / c.gears[constrain(g, 1, n) - 1]; };   // km/h ต่อ 1000 rpm
  if (idle > 0) {
    idle -= dt;
    simRpm += (850 - simRpm) * fminf(1.f, dt * 3);
    tps = 0;
    if (idle <= 0) { accel = true; simGear = 1; }
  } else if (accel) {
    tps = 85;
    simRpm += (simGear <= 2 ? 2600 : simGear == 3 ? 1700 : 1100) * dt;
    if (simRpm >= red + 80) {
      if (simGear < n) { simRpm = simRpm * kmh(simGear) / kmh(simGear + 1); simGear++; }
      else accel = false;
    }
  } else {
    tps = 0;
    simRpm -= 700 * dt;
    if (simRpm < 1400) {
      if (simGear > 1) { simRpm = simRpm * kmh(simGear) / kmh(simGear - 1); simGear--; }
      else idle = 2.5f;
    }
  }
  RAW.rpm = simRpm + random(15);
  RAW.spd = idle > 0 ? 0 : simRpm / 1000 * kmh(simGear);
  RAW.ect = fminf(88, RAW.ect + dt * 1.5f);
  RAW.iat = 36 + sinf(simT / 7) * 2;
  RAW.volt = 14.1f + sinf(simT * 1.3f) * .1f;
  RAW.load = tps > 0 ? 70 + random(10) : 18;
  RAW.tps = tps;
  RAW.map = tps > 0 ? 165 : 32;
  RAW.maf = simRpm / 1000 * (tps > 0 ? 9 : 2.2f);
  RAW.fuel = 67;
  RAW.stft = sinf(simT / 1.7f) * 3.4f;
  RAW.afrMode = 1;
  RAW.afr = tps > 0 ? 12.6f + sinf(simT * 2) * .3f : 14.7f + sinf(simT * 3) * .4f;
}

// เดาเกียร์จาก รอบ ÷ ความเร็ว เทียบกับค่าที่ตั้งไว้ (rpm ต่อ 1 km/h)
inline void gearEstimate() {
  const auto& c = didicfg::cfg;
  if (D.spd < 3 || D.rpm < 400) { strcpy(D.gear, "N"); return; }
  float r = D.rpm / D.spd, be = 1e9;
  int b = -1;
  for (int i = 0; i < c.gearCount; i++) {
    float e = fabsf(r - c.gears[i]) / c.gears[i];
    if (e < be) { be = e; b = i; }
  }
  if (b < 0 || be > .15f) strcpy(D.gear, "-");
  else { D.gear[0] = '1' + b; D.gear[1] = 0; }
}

inline void update(float dt, bool demo) {
  if (demo) sim(dt);
  // ค่ารอบจาก OBD มาห่าง ๆ (K-line) → เดาต่อจากอัตราเปลี่ยนล่าสุดไม่เกิน 0.25 วิ ให้เข็ม/ตัวเลขไม่ค้างเป็นขั้น
  float rpmT = RAW.rpm;
  if (!demo && RAW.rpmT) {
    float age = (millis() - RAW.rpmT) / 1000.f;
    if (age < 0.25f) rpmT = fmaxf(0, RAW.rpm + RAW.rpmRate * age);
  }
  sRpm.step(rpmT, dt, 16); sSpd.step(RAW.spd, dt, 9); sEct.step(RAW.ect, dt, 4); sIat.step(RAW.iat, dt, 4);
  sVolt.step(RAW.volt, dt, 5); sLoad.step(RAW.load, dt, 10); sTps.step(RAW.tps, dt, 14); sMap.step(RAW.map, dt, 10);
  sMaf.step(RAW.maf, dt, 10); sFuel.step(RAW.fuel, dt, 3);
  D.rpm = fmaxf(0, sRpm.x); D.spd = fmaxf(0, sSpd.x); D.ect = sEct.x; D.iat = sIat.x; D.volt = sVolt.x;
  D.load = sLoad.x; D.tps = sTps.x; D.map = sMap.x; D.maf = fmaxf(0, sMaf.x); D.fuel = sFuel.x; D.stft = RAW.stft;
  D.afr += (RAW.afr - D.afr) * fminf(1.f, dt * 8);
  D.boost = fmaxf(-1.f, (D.map - 101) / 100);
  gearEstimate();
  D.pRpm = fmaxf(D.pRpm, D.rpm); D.pSpd = fmaxf(D.pSpd, D.spd); D.pEct = fmaxf(D.pEct, D.ect);
  D.pIat = fmaxf(D.pIat, D.iat);
  if (D.volt > 5) D.pVmin = fminf(D.pVmin, D.volt);
  D.trip += D.spd * dt / 3600;
}
inline void resetPeaks() { D.pRpm = D.pSpd = D.pEct = D.pIat = 0; D.pVmin = 99; }

}  // namespace data
