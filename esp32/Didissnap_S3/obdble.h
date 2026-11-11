// obdble.h — บอร์ดต่อตัวอ่าน OBD2 แบบ BLE (ELM327 BLE เช่น Vgate iCar Pro BLE, Veepeak BLE/BLE+, vLinker, IOS-Vlink)
//
//  ทำงานเป็นงานแยกบนคอร์ 0: ค้นหา → ต่อ → หา characteristic อ่าน/เขียนเอง → ตั้งค่า ELM327 → ถามค่าวนไป
//  ค่าที่ได้ใส่ลง RAW (data.h) แล้วจอหน่วงให้นุ่มเหมือนเดิม
//  - จำตัวที่ต่อสำเร็จล่าสุดไว้ (Preferences "obdMac") ครั้งต่อไปต่อตัวนั้นก่อน
//  - รอบเครื่อง + ความเร็ว ถามทุกรอบ ค่าอื่นสลับกันถามทีละตัว (K-line ของ Jazz GD ช้า ~5-10 คำถาม/วินาที)
//  ต้องลงไลบรารี "NimBLE-Arduino" (by h2zero, เวอร์ชัน 2.x หรือ 3.x) จาก Library Manager
//   (เล็กกว่า BLE ที่มากับบอร์ดมาก — แบบเดิมโปรแกรมใหญ่เกินพื้นที่)
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include "data.h"

#if defined(NIMBLE_CPP_VERSION_MAJOR) && NIMBLE_CPP_VERSION_MAJOR >= 3
// NimBLE-Arduino 3.x ใช้ std::atomic_flag แต่คอมไพเลอร์ ESP32-S3 ไม่มีฟังก์ชันนี้ให้ → ใส่ให้เอง
static portMUX_TYPE obdAtomicMux = portMUX_INITIALIZER_UNLOCKED;
extern "C" bool __atomic_test_and_set(volatile void* p, int) {
  portENTER_CRITICAL_SAFE(&obdAtomicMux);
  bool old = *(volatile bool*)p;
  *(volatile bool*)p = true;
  portEXIT_CRITICAL_SAFE(&obdAtomicMux);
  return old;
}
#endif

namespace obdble {

enum State { OFF = 0, SCANNING, CONNECTING, INIT, RUNNING };
static volatile int state = OFF;
static char devName[24] = "";        // ชื่อตัวอ่านที่ต่ออยู่ / กำลังต่อ
static char lastMsg[32] = "";        // ข้อความสถานะล่าสุด (เช่น ไม่พบอุปกรณ์)
static volatile uint32_t lastData = 0;   // millis() ที่ได้ค่าจากรถล่าสุด
static volatile uint32_t answers = 0;    // นับคำตอบที่ใช้ได้
static Preferences* prefs = nullptr;

static NimBLEClient* client = nullptr;
static NimBLERemoteCharacteristic* rxC = nullptr;
static NimBLERemoteCharacteristic* txC = nullptr;
static volatile bool linkUp = false;

// ---------------------------------------------------------------- รับข้อมูล
static portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;
static char rxBuf[512];
static volatile int rxLen = 0;
static void onNotify(NimBLERemoteCharacteristic*, uint8_t* d, size_t n, bool) {
  portENTER_CRITICAL(&rxMux);
  for (size_t i = 0; i < n; i++) {
    if (rxLen >= (int)sizeof(rxBuf) - 1) break;
    rxBuf[rxLen++] = (char)d[i];
  }
  rxBuf[rxLen] = 0;
  portEXIT_CRITICAL(&rxMux);
}
class Cb : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient*, int) override { linkUp = false; }
};
static Cb cb;

// ส่งคำสั่ง แล้วรอ '>' (ELM พร้อมรับคำสั่งถัดไป) — คืนคำตอบ (ตัดช่องว่าง/ขึ้นบรรทัดออกแล้ว ตัวพิมพ์ใหญ่)
static String cmd(const char* c, uint32_t timeoutMs = 1500) {
  if (!txC || !linkUp) return "";
  portENTER_CRITICAL(&rxMux);
  rxLen = 0; rxBuf[0] = 0;
  portEXIT_CRITICAL(&rxMux);
  String s = String(c) + "\r";
  txC->writeValue((const uint8_t*)s.c_str(), s.length(), !txC->canWriteNoResponse());
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs && linkUp) {
    bool done;
    portENTER_CRITICAL(&rxMux);
    done = strchr(rxBuf, '>') != nullptr;
    portEXIT_CRITICAL(&rxMux);
    if (done) break;
    delay(5);
  }
  char tmp[512];
  portENTER_CRITICAL(&rxMux);
  memcpy(tmp, rxBuf, rxLen + 1);
  portEXIT_CRITICAL(&rxMux);
  String r;
  for (char* p = tmp; *p; p++) {
    char ch = *p;
    if (ch == '>' || ch == ' ' || ch == '\r' || ch == '\n') continue;
    r += (char)toupper(ch);
  }
  return r;
}

// หา "41xx" แล้วคืนไบต์ข้อมูล (คืนจำนวนไบต์ที่ได้)
static int parse(const String& r, uint8_t pid, uint8_t* out, int maxN) {
  char key[5];
  snprintf(key, sizeof(key), "41%02X", pid);
  int i = r.indexOf(key);
  if (i < 0) return 0;
  i += 4;
  int n = 0;
  while (n < maxN && i + 1 < (int)r.length() && isxdigit(r[i]) && isxdigit(r[i + 1])) {
    out[n++] = (uint8_t)strtol(r.substring(i, i + 2).c_str(), nullptr, 16);
    i += 2;
  }
  return n;
}

static bool oneShot = true;          // ตัวอ่านบางตัวไม่รองรับเลขต่อท้าย → ปิดเองถ้าตอบ '?'
static uint32_t rateT = 0, rateN = 0;
static volatile int rate = 0;        // คำตอบต่อวินาที (ดูในเมนู)
static bool pid(uint8_t p) {
  char c[8];
  // ต่อท้าย "1" = บอก ELM ว่ารอคำตอบแค่ 1 ชุด ตอบกลับทันที (ไม่ต้องรอจนหมดเวลาเผื่อ ECU อื่น)
  snprintf(c, sizeof(c), oneShot ? "01%02X1" : "01%02X", p);
  String r = cmd(c, 1200);
  if (oneShot && r.indexOf('?') >= 0 && r.indexOf("41") < 0) { oneShot = false; return false; }
  uint8_t b[4];
  int n = parse(r, p, b, 4);
  static int fails = 0;
  if (n <= 0) {
    // ตัวอ่านบางตัว (โดยเฉพาะ K-line) ตอบ NO DATA เมื่อมีเลขต่อท้าย → ไม่สำเร็จติดกัน 4 ครั้ง = กลับไปถามแบบปกติ
    if (oneShot && ++fails >= 4) { oneShot = false; fails = 0; Serial.println("[obd] 1-response mode off"); }
    return false;
  }
  fails = 0;
  switch (p) {
    case 0x0C:
      if (n >= 2) {
        float v = ((b[0] << 8) | b[1]) / 4.f;
        uint32_t now = millis();
        float dt = (now - RAW.rpmT) / 1000.f;
        // อัตราเปลี่ยนรอบ (ให้จอเดาค่าระหว่างรอคำตอบถัดไป)
        RAW.rpmRate = (RAW.rpmT && dt > 0.02f && dt < 1.f) ? constrain((v - RAW.rpm) / dt, -9000.f, 9000.f) : 0;
        RAW.rpm = v;
        RAW.rpmT = now;
      }
      break;
    case 0x0D: RAW.spd = b[0]; break;
    case 0x05: RAW.ect = b[0] - 40.f; break;
    case 0x0F: RAW.iat = b[0] - 40.f; break;
    case 0x04: RAW.load = b[0] * 100.f / 255; break;
    case 0x11: RAW.tps = b[0] * 100.f / 255; break;
    case 0x0B: RAW.map = b[0]; break;
    case 0x10: if (n >= 2) RAW.maf = ((b[0] << 8) | b[1]) / 100.f; break;
    case 0x2F: RAW.fuel = b[0] * 100.f / 255; break;
    case 0x06: RAW.stft = (b[0] - 128) * 100.f / 128; break;
    case 0x42: if (n >= 2) RAW.volt = ((b[0] << 8) | b[1]) / 1000.f; break;
  }
  lastData = millis();
  answers++;
  rateN++;
  if (millis() - rateT >= 1000) { rate = rateN; rateN = 0; rateT = millis(); }
  return true;
}
static bool readVolt() {             // ATRV = แรงดันที่ขาตัวอ่าน (ใช้ได้แม้ ECU ไม่รองรับ PID 42)
  String r = cmd("ATRV", 800);
  int v = r.indexOf('V');
  if (v <= 0) return false;
  float x = r.substring(0, v).toFloat();
  if (x < 5 || x > 20) return false;
  RAW.volt = x;
  return true;
}

// ---------------------------------------------------------------- ค้นหา + ต่อ
static bool looksLikeObd(const NimBLEAdvertisedDevice* d) {
  String n = d->getName().c_str();
  n.toUpperCase();
  static const char* const K[] = {"OBD", "VGATE", "VLINK", "V-LINK", "VEEPEAK", "ELM", "IOS-", "ANDROID-VLINK", "KONNWEI", "VIECAR", "CAR SCANNER", "LELINK"};
  for (auto k : K) if (n.indexOf(k) >= 0) return true;
  static const char* const S[] = {"fff0", "ffe0", "18f0", "e7810a71-73ae-499d-8c15-faa9aef0c3f2"};
  for (auto s : S) if (d->haveServiceUUID() && d->isAdvertisingService(NimBLEUUID(s))) return true;
  return false;
}

static bool findChars() {
  rxC = txC = nullptr;
  for (auto* sv : client->getServices(true)) {
    NimBLEUUID u = sv->getUUID();
    if (u == NimBLEUUID((uint16_t)0x1800) || u == NimBLEUUID((uint16_t)0x1801) || u == NimBLEUUID((uint16_t)0x180A)) continue;
    NimBLERemoteCharacteristic *r = nullptr, *w = nullptr;
    for (auto* c : sv->getCharacteristics(true)) {
      if (!r && (c->canNotify() || c->canIndicate())) r = c;
      if (!w && (c->canWrite() || c->canWriteNoResponse())) w = c;
    }
    if (r && w) { rxC = r; txC = w; return true; }   // อ่าน+เขียนอยู่ service เดียวกัน = ตัว ELM
  }
  return false;
}

static bool connectTo(const NimBLEAddress& addr, const String& name) {
  strlcpy(devName, name.length() ? name.c_str() : addr.toString().c_str(), sizeof(devName));
  state = CONNECTING;
  if (!client) {
    client = NimBLEDevice::createClient();
    client->setConnectionParams(6, 12, 0, 300);   // ช่วงคุย 7.5–15 ms (ค่าเริ่ม 30–50 ms → ทุกคำถามช้าลง) 
#if defined(NIMBLE_CPP_VERSION_MAJOR) && NIMBLE_CPP_VERSION_MAJOR >= 3
    client->setCallbacks(cb);                 // NimBLE-Arduino 3.x
#else
    client->setClientCallbacks(&cb, false);   // NimBLE-Arduino 2.x
#endif
    client->setConnectTimeout(8000); }
  Serial.printf("[obd] connect %s (%s)\n", addr.toString().c_str(), devName);
  if (!client->connect(addr)) { strlcpy(lastMsg, "connect failed", sizeof(lastMsg)); Serial.println("[obd] connect failed"); return false; }
  linkUp = true;
  client->updateConnParams(6, 12, 0, 300);        // บางตัวอ่านไม่รับตอนต่อ → ขออีกครั้งหลังต่อ
  Serial.printf("[obd] chars %s\n", findChars() ? "ok" : "NOT FOUND");
  if (!rxC || !txC) { strlcpy(lastMsg, "no ELM service", sizeof(lastMsg)); client->disconnect(); linkUp = false; return false; }
  if (!rxC->subscribe(rxC->canNotify(), onNotify)) { strlcpy(lastMsg, "subscribe failed", sizeof(lastMsg)); client->disconnect(); linkUp = false; return false; }
  return true;
}

static bool scanAndConnect() {
  state = SCANNING;
  // ตัวที่จำไว้ก่อน
  if (prefs) {
    String mac = prefs->getString("obdMac", "");
    int t = prefs->getInt("obdType", 0);
    if (mac.length() && connectTo(NimBLEAddress(std::string(mac.c_str()), (uint8_t)t), prefs->getString("obdName", ""))) return true;
    state = SCANNING;
  }
  NimBLEScan* s = NimBLEDevice::getScan();
  s->setActiveScan(true);
  s->setInterval(100);
  s->setWindow(90);
  NimBLEScanResults res = s->getResults(5000, false);
  const NimBLEAdvertisedDevice* best = nullptr;
  int bestRssi = -999;
  for (int i = 0; i < res.getCount(); i++) {
    const NimBLEAdvertisedDevice* d = res.getDevice(i);
    if (d && looksLikeObd(d) && d->getRSSI() > bestRssi) { best = d; bestRssi = d->getRSSI(); }
  }
  Serial.printf("[obd] scan: %d devices\n", res.getCount());
  for (int i = 0; i < res.getCount(); i++) { const NimBLEAdvertisedDevice* d = res.getDevice(i); if (d) Serial.printf("[obd]   %s  %s  %d dBm\n", d->getAddress().toString().c_str(), d->getName().c_str(), d->getRSSI()); }
  if (!best) { s->clearResults(); strlcpy(lastMsg, "no OBD found", sizeof(lastMsg)); return false; }
  NimBLEAddress addr = best->getAddress();
  String name = best->getName().c_str();
  s->clearResults();
  if (!connectTo(addr, name)) return false;
  if (prefs) {
    prefs->putString("obdMac", addr.toString().c_str());
    prefs->putInt("obdType", (int)addr.getType());
    prefs->putString("obdName", name);
  }
  return true;
}

static bool initElm() {
  state = INIT;
  cmd("ATZ", 2500);
  delay(300);
  cmd("ATE0");
  cmd("ATL0");
  cmd("ATS0");
  cmd("ATH0");
  cmd("ATAT2");          // ปรับเวลารอคำตอบอัตโนมัติแบบเร็วสุด
  cmd("ATSP0");          // หาโปรโตคอลเอง (Jazz GD = ISO 9141-2 K-line)
  // คำถามแรกให้เวลาหาโปรโตคอล (K-line ใช้หลายวินาที)
  for (int i = 0; i < 3; i++) {
    String r = cmd("0100", 8000);
    Serial.printf("[obd] 0100 -> %s\n", r.c_str());
    if (r.indexOf("4100") >= 0) return true;
    if (!linkUp) return false;
  }
  strlcpy(lastMsg, "ECU no answer", sizeof(lastMsg));
  return false;
}

// ---------------------------------------------------------------- DTC (ให้แอปสั่งผ่าน WiFi)
static volatile int dtcReq = 0;      // 1 = อ่าน, 2 = ลบ
static String dtcJson = "[]";
static volatile bool dtcDone = false, dtcOk = false;
static void doDtc() {
  if (dtcReq == 1) {
    String r = cmd("03", 4000);
    String js = "[";
    int i = r.indexOf("43");
    int nOut = 0;
    while (i >= 0) {
      int j = i + 2;
      while (j + 3 < (int)r.length() && isxdigit(r[j]) && isxdigit(r[j + 1]) && isxdigit(r[j + 2]) && isxdigit(r[j + 3])) {
        uint16_t v = (uint16_t)strtol(r.substring(j, j + 4).c_str(), nullptr, 16);
        j += 4;
        if (!v) continue;
        static const char T[] = {'P', 'C', 'B', 'U'};
        char c[8];
        snprintf(c, sizeof(c), "%c%04X", T[v >> 14], v & 0x3FFF);
        js += (nOut++ ? ",\"" : "\"") + String(c) + "\"";
      }
      i = r.indexOf("43", j);
    }
    js += "]";
    dtcJson = js;
    dtcOk = true;
  } else if (dtcReq == 2) {
    String r = cmd("04", 4000);
    dtcOk = r.indexOf("44") >= 0;
  }
  dtcReq = 0;
  dtcDone = true;
}

// ---------------------------------------------------------------- งานหลัก
static void task(void*) {
  NimBLEDevice::init("Didissnap");
  static const uint8_t SLOW[] = {0x05, 0x0F, 0x04, 0x11, 0x0B, 0x10, 0x2F, 0x06};
  int slow = 0;
  for (;;) {
    if (!linkUp) {
      state = SCANNING;
      if (!scanAndConnect()) { delay(3000); continue; }
      if (!initElm()) { if (client && linkUp) client->disconnect(); linkUp = false; delay(2000); continue; }
      lastMsg[0] = 0;
      state = RUNNING;
    }
    if (dtcReq) { doDtc(); continue; }
    // ลำดับ 6 คำถาม: รอบ ความเร็ว รอบ รอบ ค่าอื่น(วนทีละตัว) รอบ → รอบเครื่องได้ 4 ใน 6
    static int step = 0;
    int k = step++ % 6;
    if (k == 1) pid(0x0D);
    else if (k == 4) { if (slow == (int)sizeof(SLOW)) { readVolt(); slow = 0; } else pid(SLOW[slow++]); }
    else pid(0x0C);
    if (!linkUp) state = SCANNING;
  }
}

inline void begin(Preferences* p) {
  prefs = p;
  xTaskCreatePinnedToCore(task, "obd", 8192, nullptr, 2, nullptr, 0);
}
// ได้ค่าจากรถใน 3 วินาทีล่าสุด
inline bool live() { return state == RUNNING && millis() - lastData < 3000; }
inline const char* status() {
  switch (state) {
    case SCANNING: return lastMsg[0] ? lastMsg : "searching";
    case CONNECTING: return "connecting";
    case INIT: return "ECU init";
    case RUNNING: {
      if (!live()) return "no data";
      static char b[40];
      snprintf(b, sizeof(b), "%s %d/s", devName, rate);
      return b;
    }
    default: return "off";
  }
}
// ลืมตัวที่จำไว้ (ให้หาใหม่)
inline void forget() {
  if (prefs) { prefs->remove("obdMac"); prefs->remove("obdType"); prefs->remove("obdName"); }
  if (client && linkUp) client->disconnect();
}

// แอปขอ DTC: เรียกจาก loop (บล็อกรอสูงสุด ~5 วินาที)
inline String readDtc() {
  if (state != RUNNING) return "[]";
  dtcDone = false; dtcReq = 1;
  uint32_t t0 = millis();
  while (!dtcDone && millis() - t0 < 6000) delay(10);
  return dtcDone ? dtcJson : String("[]");
}
inline bool clearDtc() {
  if (state != RUNNING) return false;
  dtcDone = false; dtcOk = false; dtcReq = 2;
  uint32_t t0 = millis();
  while (!dtcDone && millis() - t0 < 6000) delay(10);
  return dtcOk;
}

}  // namespace obdble
