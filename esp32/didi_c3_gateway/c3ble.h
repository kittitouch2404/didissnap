// c3ble.h — C3 เป็นตัวปล่อยค่ารถทาง Bluetooth (BLE) ให้จอ S3 / CYD / แอปแอนดรอยด์ ต่อพร้อมกัน (สูงสุด 3)
//  ต้องลงไลบรารี "NimBLE-Arduino" (by h2zero, 2.x) จาก Library Manager
//  จับคู่: จอต้องส่ง DN_AUTH (กุญแจ) หลังต่อ — ส่งค่ารถ/รับคำสั่งเฉพาะจอที่จับคู่แล้ว (ดู didinow.h)
#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include "didinow.h"

namespace c3ble {

static NimBLEServer* srv = nullptr;
static NimBLECharacteristic* dataC = nullptr;
static NimBLECharacteristic* cmdC = nullptr;
static volatile int clients = 0;
static char devName[16] = "DIDI-C3";
// คำสั่งที่เข้ามา (ให้ไฟล์หลักเอาไปทำ) — เฉพาะจอที่จับคู่แล้ว
static void (*onCmd)(const uint8_t* d, int n) = nullptr;

// ---------------------------------------------------------------- จอที่ต่ออยู่
struct Conn {
  bool used, auth, kill, reply, gone;
  uint16_t h;
  uint32_t t0;
  uint8_t res;
};
static Conn conns[4];
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// ---------------------------------------------------------------- จอที่จับคู่แล้ว (จำในแฟลช)
struct Pair {
  uint8_t tok[8];
  char name[8];
  uint32_t seq;   // ใช้ล่าสุด (เต็มแล้วทับอันเก่าสุด)
};
static Pair pairs[DN_PAIR_MAX];
static int nPairs = 0;
static uint32_t pairSeq = 1;
static volatile bool pairsDirty = false;
static volatile uint32_t openUntil = 0;
static bool advOpen = false;
static Preferences pp;

inline bool pairOpen() { return nPairs == 0 || (int32_t)(openUntil - millis()) > 0; }
inline int pairCount() { return nPairs; }
// เปิดโหมดจับคู่ (เพิ่มจอใหม่) n วินาที
inline void openPairing(uint32_t sec) {
  openUntil = millis() + sec * 1000;
  Serial.printf("[pair] open %u s\n", (unsigned)sec);
}

static void pairsLoad() {
  pp.begin("c3pair", false);
  nPairs = 0;
  if (pp.getBytes("p", pairs, sizeof(pairs)) == sizeof(pairs)) {
    for (int i = 0; i < DN_PAIR_MAX; i++) {
      pairs[i].name[7] = 0;
      if (pairs[i].seq) { nPairs++; if (pairs[i].seq >= pairSeq) pairSeq = pairs[i].seq + 1; }
    }
  } else memset(pairs, 0, sizeof(pairs));
}
static void pairsSave() {
  if (!pairsDirty) return;
  pairsDirty = false;
  pp.putBytes("p", pairs, sizeof(pairs));
  int n = 0;
  for (auto& p : pairs) if (p.seq) n++;
  nPairs = n;
  Serial.printf("[pair] saved %d screens\n", n);
}
static int pairFind(const uint8_t* tok) {
  for (int i = 0; i < DN_PAIR_MAX; i++) if (pairs[i].seq && !memcmp(pairs[i].tok, tok, 8)) return i;
  return -1;
}

// ---------------------------------------------------------------- สถานะตอบกลับ
static void fillSt(DidiNowAuthSt& m, uint8_t res) {
  memset(&m, 0, sizeof(m));
  m.magic = DIDINOW_MAGIC; m.type = DN_AUTHST; m.res = res;
  int32_t left = (int32_t)(openUntil - millis());
  m.open = nPairs == 0 ? 255 : left > 0 ? (uint8_t)min<int32_t>(254, left / 2000 + 1) : 0;
  strlcpy(m.c3, devName, sizeof(m.c3));
  int k = 0;
  for (auto& p : pairs) if (p.seq && k < DN_PAIR_MAX) { memcpy(m.names[k], p.name, 8); m.names[k][7] = 0; k++; }
  m.n = k;
}

// ทำคำสั่งจับคู่ (เรียกจาก callback ของ BLE)
static void doAuth(int ci, const DidiNowAuth* a) {
  Conn& c = conns[ci];
  int i = pairFind(a->token);
  switch (a->op) {
    case DN_AUTH_LOGIN:
      if (i >= 0) {
        c.auth = true; c.res = DN_AUTHR_OK; pairs[i].seq = pairSeq++;
        memcpy(pairs[i].name, a->name, 7); pairs[i].name[7] = 0;
      } else if (pairOpen()) {
        int slot = -1;
        uint32_t old = UINT32_MAX;
        for (int k = 0; k < DN_PAIR_MAX; k++) {
          if (!pairs[k].seq) { slot = k; break; }
          if (pairs[k].seq < old) { old = pairs[k].seq; slot = k; }
        }
        memcpy(pairs[slot].tok, a->token, 8);
        memcpy(pairs[slot].name, a->name, 7); pairs[slot].name[7] = 0;
        pairs[slot].seq = pairSeq++;
        pairsDirty = true;
        int n = 0;
        for (auto& p : pairs) if (p.seq) n++;
        nPairs = n;
        c.auth = true; c.res = DN_AUTHR_NEW;
        Serial.printf("[pair] new screen '%s'\n", pairs[slot].name);
      } else {
        c.res = DN_AUTHR_DENIED; c.kill = true; c.t0 = millis();
        Serial.println("[pair] unknown screen -> denied");
      }
      break;
    case DN_AUTH_OPEN:
      if (c.auth) openPairing(180);
      c.res = c.auth ? DN_AUTHR_OK : DN_AUTHR_DENIED;
      break;
    case DN_AUTH_CLEAR:   // ล้างทุกจอ ยกเว้นจอที่สั่ง
      if (c.auth) {
        for (int k = 0; k < DN_PAIR_MAX; k++) if (k != i) memset(&pairs[k], 0, sizeof(Pair));
        pairsDirty = true;
        nPairs = i >= 0 ? 1 : 0;
      }
      c.res = c.auth ? DN_AUTHR_OK : DN_AUTHR_DENIED;
      break;
    case DN_AUTH_FORGET:  // จอนี้ขอเลิกจับคู่
      if (c.auth && i >= 0) { memset(&pairs[i], 0, sizeof(Pair)); pairsDirty = true; nPairs--; }
      c.auth = false; c.kill = true; c.t0 = millis(); c.res = DN_AUTHR_DENIED;
      break;
  }
  c.reply = true;
}

class SrvCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, NimBLEConnInfo& ci) override {
    clients = s->getConnectedCount();
    portENTER_CRITICAL(&mux);
    for (auto& c : conns) if (!c.used) { c = {}; c.used = true; c.h = ci.getConnHandle(); c.t0 = millis(); break; }
    portEXIT_CRITICAL(&mux);
    Serial.printf("[ble] client connected (%d) %s\n", (int)clients, ci.getAddress().toString().c_str());
    // ช่วงส่งข้อมูลถี่ ๆ (15-30 ms) ค่าวิ่งลื่น
    s->updateConnParams(ci.getConnHandle(), 12, 24, 0, 300);
    if (clients < 3) NimBLEDevice::startAdvertising();   // ให้จออื่นต่อเพิ่มได้
  }
  void onDisconnect(NimBLEServer* s, NimBLEConnInfo& ci, int reason) override {
    clients = s->getConnectedCount();
    portENTER_CRITICAL(&mux);
    for (auto& c : conns) if (c.used && c.h == ci.getConnHandle()) c.used = false;
    portEXIT_CRITICAL(&mux);
    Serial.printf("[ble] client left (%d) reason %d\n", (int)clients, reason);
    NimBLEDevice::startAdvertising();
  }
};
class CmdCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* ch, NimBLEConnInfo& ci) override {
    NimBLEAttValue v = ch->getValue();
    const uint8_t* d = v.data();
    int n = (int)v.length();
    int k = -1;
    for (int i = 0; i < 4; i++) if (conns[i].used && conns[i].h == ci.getConnHandle()) k = i;
    if (k < 0) return;
    if (n >= (int)sizeof(DidiNowAuth) && ((const DidiNowAuth*)d)->magic == DIDINOW_MAGIC && d[4] == DN_AUTH) {
      doAuth(k, (const DidiNowAuth*)d);
      return;
    }
    if (conns[k].auth && onCmd) onCmd(d, n);   // ยังไม่จับคู่ = ไม่รับคำสั่ง
  }
};

// โฆษณา: ชื่อไม่ซ้ำ + บอกว่าเปิดโหมดจับคู่อยู่ไหม
static void advSet(bool open) {
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData ad, sd;
  ad.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  ad.addServiceUUID(DIDIBLE_SVC);
  const uint8_t mfg[5] = {0xFF, 0xFF, 'D', 'N', (uint8_t)(open ? 1 : 0)};
  ad.setManufacturerData(mfg, sizeof(mfg));
  sd.setName(devName);
  bool was = adv->isAdvertising();
  if (was) adv->stop();
  adv->setAdvertisementData(ad);
  adv->setScanResponseData(sd);
  if (was || clients < 3) adv->start();
  advOpen = open;
}

// coldBoot = เพิ่งเสียบ C3 (ไม่ใช่ตื่นจากหลับ) → เปิดโหมดจับคู่ 2 นาที
inline bool begin(bool coldBoot) {
  uint64_t mac = ESP.getEfuseMac();
  snprintf(devName, sizeof(devName), "DIDI-C3-%02X%02X", (unsigned)((mac >> 32) & 0xFF), (unsigned)((mac >> 40) & 0xFF));
  pairsLoad();
  if (coldBoot) openPairing(120);
  NimBLEDevice::init(devName);
  NimBLEDevice::setMTU(DIDIBLE_MTU);
  NimBLEDevice::setPower(9);                       // แรงสุด (dBm) ในรถระยะใกล้ก็พอ
  srv = NimBLEDevice::createServer();
  static SrvCb scb;
  srv->setCallbacks(&scb);
  NimBLEService* svc = srv->createService(DIDIBLE_SVC);
  dataC = svc->createCharacteristic(DIDIBLE_DATA, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  cmdC = svc->createCharacteristic(DIDIBLE_CMD, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  static CmdCb ccb;
  cmdC->setCallbacks(&ccb);
  svc->start();
  advSet(pairOpen());
  bool ok = NimBLEDevice::getAdvertising()->isAdvertising();
  Serial.printf("[ble] %s advertising as %s · paired %d · pairing %s\n", ok ? "OK" : "FAILED", devName, nPairs,
                pairOpen() ? "OPEN" : "closed");
  return ok;
}

// ส่งแพ็กเก็ตให้ทุกจอที่จับคู่แล้ว
inline void send(const void* p, size_t n) {
  if (!dataC || !clients) return;
  for (auto& c : conns)
    if (c.used && c.auth) dataC->notify((const uint8_t*)p, n, c.h);
}

// เรียกใน loop(): ตอบผลจับคู่ · ตัดจอที่ไม่จับคู่ · อัปเดตโฆษณา · บันทึก
inline void tick() {
  uint32_t now = millis();
  for (auto& c : conns) {
    if (!c.used) continue;
    if (c.reply) {
      c.reply = false;
      DidiNowAuthSt m;
      fillSt(m, c.res);
      if (dataC) dataC->notify((const uint8_t*)&m, sizeof(m), c.h);
    }
    // ไม่ยืนยันตัวใน 5 วิ / ถูกปฏิเสธ (รอ 0.4 วิ ให้ได้รับคำตอบก่อน) → ตัด
    if (!c.gone && ((!c.auth && now - c.t0 > 5000) || (c.kill && now - c.t0 > 400))) {
      c.gone = true;        // กันสั่งตัดซ้ำ
      if (srv) srv->disconnect(c.h);
    }
  }
  bool open = pairOpen();
  if (open != advOpen) advSet(open);
  pairsSave();
}

}  // namespace c3ble
