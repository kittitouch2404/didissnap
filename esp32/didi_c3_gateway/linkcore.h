// linkcore.h — แกนกลางของตัวปล่อยค่ารถ (ไม่ผูกกับชิป): จอที่ต่ออยู่ · จับคู่ (กุญแจ) · ส่งให้ทุกจอ
//  ทางเชื่อม: BLE (c3ble.h = NimBLE บน C3 · c3bt.h = Bluedroid บน ESP32) · WiFi UDP (c3wifi.h) · Bluetooth ธรรมดา SPP (c3bt.h)
//  จับคู่: จอต้องส่ง DN_AUTH (กุญแจ) หลังต่อ — ส่งค่ารถ/รับคำสั่งเฉพาะจอที่จับคู่แล้ว (ดู didinow.h)
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "didinow.h"

namespace c3ble {

static volatile int clients = 0;        // จอที่ต่อทาง BLE อยู่
static char macStr[18] = "";            // ที่อยู่ BLE ของตัวนี้ (AA:BB:...)
// ตัวต่อ BLE ตั้งให้: ส่งให้จอ BLE ช่องนี้ / ตัดจอ BLE / อัปเดตโฆษณา (โหมดจับคู่)
static void (*bleNotify)(uint16_t h, const uint8_t* d, int n) = nullptr;
static void (*bleDrop)(uint16_t h) = nullptr;
static void (*advUpdate)(bool open) = nullptr;
// Bluetooth ธรรมดา (SPP) ส่งออก (c3bt.h ตั้งให้)
static void (*sppSend)(const uint8_t* d, int n) = nullptr;
static char devName[16] = "DIDI-C3";
static const char* fwVer = "";          // ตั้งจากไฟล์หลักก่อน begin()
static uint8_t pairProto[DN_PAIR_MAX];  // รุ่นโปรโตคอลของจอแต่ละช่อง (ไม่เก็บลงแฟลช)
static uint8_t legacyHits = 0;          // จอรุ่นเก่าพยายามต่อ (ล้างทุก 10 นาที)
static uint32_t legacyAt = 0;
static volatile bool stChanged = false; // แจ้งสถานะใหม่ให้ทุกจอ
// คำสั่งที่เข้ามา (ให้ไฟล์หลักเอาไปทำ) — เฉพาะจอที่จับคู่แล้ว
static void (*onCmd)(const uint8_t* d, int n) = nullptr;

// ---------------------------------------------------------------- จอที่ต่ออยู่
enum : uint8_t { K_BLE = 0, K_UDP = 1, K_SPP = 2 };
struct Conn {
  bool used, auth, kill, reply, gone;
  bool udp;          // จอที่ต่อทาง WiFi (UDP) — ไม่ใช่ BLE
  uint8_t kind;      // K_BLE / K_UDP / K_SPP
  uint16_t h;        // BLE: conn handle · UDP: พอร์ต
  uint32_t ip;       // UDP: IP ของจอ
  uint32_t t0, lastRx;
  uint8_t res;
};
#define C3_NCONN 6
static Conn conns[C3_NCONN];
// ส่งแพ็กเก็ตทาง WiFi (c3wifi.h ตั้งให้)
static void (*udpSend)(uint32_t ip, uint16_t port, const uint8_t* d, int n) = nullptr;
static void sendTo(Conn& c, const uint8_t* d, int n) {
  if (c.kind == K_UDP) { if (udpSend) udpSend(c.ip, c.h, d, n); }
  else if (c.kind == K_SPP) { if (sppSend) sppSend(d, n); }
  else if (bleNotify) bleNotify(c.h, d, n);
}
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
  for (int i = 0; i < DN_PAIR_MAX; i++) {
    const Pair& p = pairs[i];
    if (!p.seq || k >= DN_PAIR_MAX) continue;
    memcpy(m.names[k], p.name, 8); m.names[k][7] = 0;
    m.proto[k] = pairProto[i];
    k++;
  }
  m.n = k;
  m.legacy = legacyHits;
  strlcpy(m.fw, fwVer, sizeof(m.fw));
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
        pairProto[i] = a->proto ? a->proto : 1;
        stChanged = true;
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
        pairProto[slot] = a->proto ? a->proto : 1;
        pairsDirty = true;
        stChanged = true;
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
        stChanged = true;
      }
      c.res = c.auth ? DN_AUTHR_OK : DN_AUTHR_DENIED;
      break;
    case DN_AUTH_FORGET:  // จอนี้ขอเลิกจับคู่
      if (c.auth && i >= 0) { memset(&pairs[i], 0, sizeof(Pair)); pairsDirty = true; nPairs--; stChanged = true; }
      c.auth = false; c.kill = true; c.t0 = millis(); c.res = DN_AUTHR_DENIED;
      break;
  }
  c.reply = true;
}

// แพ็กเก็ตจากจอ (BLE หรือ WiFi): ยืนยันตัว หรือส่งต่อคำสั่ง (เฉพาะจอที่จับคู่แล้ว)
static void onPacket(int k, const uint8_t* d, int n) {
  conns[k].lastRx = millis();
  if (n >= (int)sizeof(DidiNowAuth) && ((const DidiNowAuth*)d)->magic == DIDINOW_MAGIC && d[4] == DN_AUTH) {
    doAuth(k, (const DidiNowAuth*)d);
    return;
  }
  if (conns[k].auth && onCmd) onCmd(d, n);   // ยังไม่จับคู่ = ไม่รับคำสั่ง
}
// แพ็กเก็ตทาง WiFi จาก ip:port → หา/สร้างช่องของจอนั้น (เรียกจาก loop)
inline void udpIn(uint32_t ip, uint16_t port, const uint8_t* d, int n) {
  int k = -1, freeK = -1;
  for (int i = 0; i < C3_NCONN; i++) {
    if (conns[i].used && conns[i].kind == K_UDP && conns[i].ip == ip && conns[i].h == port) k = i;
    if (!conns[i].used && freeK < 0) freeK = i;
  }
  if (k < 0) {
    if (freeK < 0) return;
    portENTER_CRITICAL(&mux);
    conns[freeK] = {};
    conns[freeK].used = true; conns[freeK].udp = true; conns[freeK].kind = K_UDP; conns[freeK].ip = ip; conns[freeK].h = port;
    conns[freeK].t0 = millis();
    portEXIT_CRITICAL(&mux);
    k = freeK;
    Serial.printf("[wifi] screen %u.%u.%u.%u:%u\n", (unsigned)(ip & 255), (unsigned)((ip >> 8) & 255), (unsigned)((ip >> 16) & 255),
                  (unsigned)(ip >> 24), port);
  }
  if (conns[k].gone) return;
  onPacket(k, d, n);
}
inline int udpClients() {
  int n = 0;
  for (auto& c : conns) if (c.used && c.kind == K_UDP && c.auth) n++;
  return n;
}

// ---------------------------------------------------------------- จอ BLE ต่อ / หลุด (ตัวต่อ BLE เรียก)
inline void bleOpen(uint16_t h) {
  portENTER_CRITICAL(&mux);
  for (auto& c : conns) if (!c.used) { c = {}; c.used = true; c.kind = K_BLE; c.h = h; c.t0 = c.lastRx = millis(); break; }
  portEXIT_CRITICAL(&mux);
}
inline void bleClose(uint16_t h) {
  portENTER_CRITICAL(&mux);
  for (auto& c : conns) if (c.used && c.kind == K_BLE && c.h == h) c.used = false;
  portEXIT_CRITICAL(&mux);
}
inline void bleIn(uint16_t h, const uint8_t* d, int n) {
  int k = -1;
  for (int i = 0; i < C3_NCONN; i++) if (conns[i].used && conns[i].kind == K_BLE && conns[i].h == h) k = i;
  if (k >= 0) onPacket(k, d, n);
}
// Bluetooth ธรรมดา (ต่อได้ทีละ 1 เครื่อง): ต่อ / หลุด / แพ็กเก็ตเข้า
inline void sppOpen() {
  portENTER_CRITICAL(&mux);
  for (auto& c : conns) if (c.used && c.kind == K_SPP) c.used = false;
  for (auto& c : conns) if (!c.used) { c = {}; c.used = true; c.kind = K_SPP; c.t0 = c.lastRx = millis(); break; }
  portEXIT_CRITICAL(&mux);
}
inline void sppClose() {
  portENTER_CRITICAL(&mux);
  for (auto& c : conns) if (c.used && c.kind == K_SPP) c.used = false;
  portEXIT_CRITICAL(&mux);
}
inline void sppIn(const uint8_t* d, int n) {
  int k = -1;
  for (int i = 0; i < C3_NCONN; i++) if (conns[i].used && conns[i].kind == K_SPP) k = i;
  if (k < 0) { sppOpen(); for (int i = 0; i < C3_NCONN; i++) if (conns[i].used && conns[i].kind == K_SPP) k = i; }
  if (k >= 0 && !conns[k].gone) onPacket(k, d, n);
}
// ตั้งชื่อ (DIDI-C3-xxxx / DIDI-SL-xxxx) + โหลดจอที่จับคู่ · coldBoot = เพิ่งเสียบ → เปิดโหมดจับคู่ 2 นาที
inline void coreBegin(const char* prefix, bool coldBoot) {
  uint64_t mac = ESP.getEfuseMac();
  snprintf(devName, sizeof(devName), "%s-%02X%02X", prefix, (unsigned)((mac >> 32) & 0xFF), (unsigned)((mac >> 40) & 0xFF));
  pairsLoad();
  if (coldBoot) openPairing(120);
}

// ส่งแพ็กเก็ตให้ทุกจอที่จับคู่แล้ว
inline void send(const void* p, size_t n) {
  for (auto& c : conns)
    if (c.used && c.auth && !c.gone) sendTo(c, (const uint8_t*)p, (int)n);
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
      sendTo(c, (const uint8_t*)&m, sizeof(m));
    }
    // จอทาง WiFi: เงียบเกิน 6 วิ (แอปส่ง ping ทุก 1.5 วิ) = ไปแล้ว
    if (c.kind == K_UDP && now - c.lastRx > 6000) { c.used = false; continue; }
    // ไม่ยืนยันตัวใน 5 วิ / ถูกปฏิเสธ (รอ 0.4 วิ ให้ได้รับคำตอบก่อน) → ตัด
    if (!c.gone && ((!c.auth && now - c.t0 > 5000) || (c.kill && now - c.t0 > 400))) {
      if (!c.kill) {        // ไม่ยืนยันตัวเลย = จอรุ่นเก่า (ไม่มีระบบจับคู่)
        if (legacyHits < 255) legacyHits++;
        legacyAt = now;
        stChanged = true;
        Serial.println("[pair] old screen (no pairing) tried to connect");
      }
      c.gone = true;        // กันสั่งตัดซ้ำ
      if (c.kind == K_UDP) { c.used = false; continue; }
      if (c.kind == K_SPP) { c.used = false; continue; }   // SPP: ไม่ตอบ = ไม่ส่งค่าให้ (ตัวต่อจะตัดเอง)
      if (bleDrop) bleDrop(c.h);
    }
  }
  if (legacyHits && now - legacyAt > 600000) { legacyHits = 0; stChanged = true; }
  bool open = pairOpen();
  if (open != advOpen) { advOpen = open; if (advUpdate) advUpdate(open); stChanged = true; }
  // สถานะเปลี่ยน (จอใหม่ / จอเก่าพยายามต่อ / โหมดจับคู่) → บอกทุกจอที่จับคู่แล้ว
  if (stChanged) {
    stChanged = false;
    for (auto& c : conns) if (c.used && c.auth && !c.reply) { c.res = DN_AUTHR_OK; c.reply = true; }
  }
  pairsSave();
}

}  // namespace c3ble
