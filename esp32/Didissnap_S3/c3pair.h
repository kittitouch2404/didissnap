// c3pair.h — ฝั่งจอ (S3 / CYD): จับคู่กับ C3 ตัวเดียว ไม่ไปต่อผิดคันตอนจอดรวมกัน
//  - จำ "กุญแจ" ของจอนี้ (สุ่มครั้งแรก) + ที่อยู่ของ C3 ที่จับคู่แล้ว (Preferences "c3pair")
//  - ยังไม่เคยจับคู่: เลือก C3 ที่ "เปิดโหมดจับคู่" อยู่ และสัญญาณแรงสุด (= อยู่ในรถเรา)
//  - ต่อแล้วส่ง DN_AUTH → C3 ตอบ DN_AUTHST (ผ่าน / จับคู่ใหม่ / ปฏิเสธ)
//  ไฟล์นี้ต้องเหมือนกันใน Didissnap_S3 และ obd2_cyd_dashboard
#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <esp_random.h>
#include "didinow.h"

namespace c3pair {

static Preferences pref;
static uint8_t tok[8];
static char myName[8] = "SCREEN";
static String saved;                       // ที่อยู่ C3 ที่จับคู่ ("" = ยังไม่จับคู่)
static String c3Name;                      // ชื่อ C3 เช่น DIDI-C3-A1B2
static volatile int8_t res = -1;           // ผลล่าสุด: -1 รอ · DN_AUTHR_*
static DidiNowAuthSt st = {};              // สถานะล่าสุดจาก C3 (จำนวนจอ / โหมดจับคู่)
static volatile uint32_t stAt = 0;
static String rejAddr[4];
static uint32_t rejT[4];
static const char* msg = "";               // ข้อความสถานะ (โชว์ในเมนู)

inline void begin(const char* name) {
  strlcpy(myName, name, sizeof(myName));
  pref.begin("c3pair", false);
  if (pref.getBytes("tok", tok, 8) != 8) {
    esp_fill_random(tok, 8);
    pref.putBytes("tok", tok, 8);
  }
  saved = pref.getString("addr", "");
  c3Name = pref.getString("name", "");
}

inline bool paired() { return saved.length() > 0; }

static bool rejected(const String& a) {
  for (int i = 0; i < 4; i++) if (rejAddr[i] == a && millis() - rejT[i] < 60000) return true;
  return false;
}
static void reject(const String& a) {
  int k = 0;
  for (int i = 1; i < 4; i++) if (rejT[i] < rejT[k]) k = i;
  rejAddr[k] = a;
  rejT[k] = millis();
}

// ดูว่า C3 ตัวนี้เปิดโหมดจับคู่อยู่ไหม: 1 เปิด · 0 ปิด · -1 เฟิร์มแวร์เก่า (ไม่บอก)
static int openFlag(const NimBLEAdvertisedDevice* d) {
  for (uint8_t i = 0; i < d->getManufacturerDataCount(); i++) {
    std::string m = d->getManufacturerData(i);
    if (m.size() >= 5 && (uint8_t)m[0] == 0xFF && (uint8_t)m[1] == 0xFF && m[2] == 'D' && m[3] == 'N') return m[4] & 1;
  }
  return -1;
}

static bool isC3(const NimBLEAdvertisedDevice* d) {
  return d->isAdvertisingService(NimBLEUUID(DIDIBLE_SVC)) || d->getName() == DIDIBLE_NAME ||
         d->getName().rfind("DIDI-C3", 0) == 0;
}

// เลือก C3 จากผลสแกน: จับคู่แล้ว = ตัวเดิมเท่านั้น · ยังไม่จับคู่ = ตัวที่เปิดโหมดจับคู่ สัญญาณแรงสุด
inline const NimBLEAdvertisedDevice* pick(NimBLEScanResults& r) {
  const NimBLEAdvertisedDevice* best = nullptr;
  const NimBLEAdvertisedDevice* legacy = nullptr;
  int bestRssi = -999;
  for (int i = 0; i < r.getCount(); i++) {
    const NimBLEAdvertisedDevice* d = r.getDevice(i);
    if (!isC3(d)) continue;
    String a = d->getAddress().toString().c_str();
    if (paired()) {
      if (a.equalsIgnoreCase(saved)) return d;
      continue;
    }
    if (rejected(a)) continue;
    int f = openFlag(d);
    if (f == 1 && d->getRSSI() > bestRssi) { best = d; bestRssi = d->getRSSI(); }
    if (f < 0 && !legacy) legacy = d;
  }
  if (best) return best;
  msg = paired() ? "C3 not found" : "no C3 in pairing mode";
  return legacy;   // C3 เฟิร์มแวร์เก่า (ไม่มีระบบจับคู่) ใช้ได้ตามเดิม
}

// แพ็กเก็ตจาก C3: ถ้าเป็นผลจับคู่ เก็บไว้แล้วคืน true (ไม่ต้องส่งต่อ)
inline bool onPacket(const uint8_t* d, int n) {
  if (n < 6 || d[4] != DN_AUTHST || n < (int)sizeof(DidiNowAuthSt)) return false;
  memcpy(&st, d, sizeof(st));
  st.c3[15] = 0;
  res = st.res;
  stAt = millis();
  return true;
}

static bool sendAuth(NimBLERemoteCharacteristic* c, uint8_t op) {
  if (!c) return false;
  DidiNowAuth a = {};
  a.magic = DIDINOW_MAGIC; a.type = DN_AUTH; a.op = op;
  memcpy(a.token, tok, 8);
  strlcpy(a.name, myName, sizeof(a.name));
  return c->writeValue((const uint8_t*)&a, sizeof(a), true);
}

// หลังต่อ + เปิดแจ้งเตือนแล้ว: ยืนยันตัวกับ C3 · คืน false = ต้องตัดสาย (ไม่ใช่ C3 ของเรา)
inline bool login(NimBLERemoteCharacteristic* cmd, const NimBLEAddress& addr) {
  res = -1;
  if (!sendAuth(cmd, DN_AUTH_LOGIN)) return false;
  uint32_t t0 = millis();
  while (res < 0 && millis() - t0 < 2500) delay(20);
  String a = addr.toString().c_str();
  if (res < 0) { msg = "old C3 firmware (no pairing)"; return true; }   // C3 รุ่นเก่า: ใช้ได้ตามเดิม
  if (res == DN_AUTHR_DENIED) {
    reject(a);
    msg = "C3 belongs to another car";
    Serial.printf("[c3] %s denied (paired with other screens)\n", a.c_str());
    return false;
  }
  if (!a.equalsIgnoreCase(saved) || c3Name != st.c3) {
    saved = a;
    c3Name = st.c3;
    pref.putString("addr", saved);
    pref.putString("name", c3Name);
  }
  msg = res == DN_AUTHR_NEW ? "paired (new)" : "paired";
  Serial.printf("[c3] %s %s (%d screens)\n", st.c3, res == DN_AUTHR_NEW ? "PAIRED NEW" : "ok", st.n);
  return true;
}

// ลืม C3 ตัวนี้ (บอก C3 ด้วยถ้าต่ออยู่) → ครั้งหน้าหา C3 ที่เปิดโหมดจับคู่ใหม่
inline void forget(NimBLERemoteCharacteristic* cmd) {
  if (cmd) sendAuth(cmd, DN_AUTH_FORGET);
  saved = "";
  c3Name = "";
  pref.remove("addr");
  pref.remove("name");
  msg = "forgot C3";
}

// ให้ C3 เปิดโหมดจับคู่ 3 นาที (เพิ่มจอใหม่)
inline bool openPairing(NimBLERemoteCharacteristic* cmd) { return sendAuth(cmd, DN_AUTH_OPEN); }

}  // namespace c3pair
