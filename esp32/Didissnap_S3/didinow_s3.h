// didinow_s3.h — ฝั่ง S3: กระจายค่ารถทาง ESP-NOW ให้จอ CYD (กี่จอก็ได้) + รับคำขอ DTC
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "didinow.h"
#include "data.h"
#include "c3link.h"

namespace didinow {

static bool ok = false;
static uint16_t seq = 0;
static uint32_t lastSend = 0;
static volatile uint8_t pendCmd = 0;
static volatile uint16_t pendId = 0;
static volatile uint32_t rxCount = 0;
static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
// ---- รับค่าจาก C3 (Super Mini ที่ช่อง OBD)
static bool gwOn = false, gwDemo = false;
static DidiNowGw gwPkt = {};
static volatile uint32_t gwLast = 0, gwSeq = 0;
static DidiNowDtc gwDtc = {};
static volatile uint16_t gwDtcId = 0;     // id ของคำตอบ DTC ล่าสุดที่ได้
static char gwInfo[59] = "";              // ผลตรวจสายจาก C3 (ตอนยังหารถไม่เจอ)
static_assert(DN_NCUST == N_CUST, "N_CUST (data.h) must match DN_NCUST (didinow.h)");
static auto& gwCust = CUST;
// ค่าปรับ (gain/offset) ที่เก็บใน C3 — เมนู CALIBRATE ของจอนี้ = แก้ค่าใน C3
static DidiNowCal gwCal = {};
static volatile bool gwCalHave = false, gwCalNew = false;

// แพ็กเก็ตจาก C3 (มาทาง BLE หรือ ESP-NOW)
static void gwFeed(const uint8_t* d, int n) {
  if (n < 5 || ((const DidiNowCmd*)d)->magic != DIDINOW_MAGIC) return;
  uint8_t t = ((const DidiNowCmd*)d)->type;
  // C3 รุ่นเก่า (ไม่มี timing/oil) ส่งสั้นกว่า: รับได้ ช่องที่ขาดเป็น 0
  if (t == DN_GW && n >= (int)offsetof(DidiNowGw, timing)) {
    DidiNowGw g = {};
    memcpy(&g, d, min((size_t)n, sizeof(g)));
    memcpy(&gwPkt, &g, sizeof(gwPkt));
    gwLast = millis(); gwSeq++;
    return;
  }
  if (t == DN_DTC && n >= (int)sizeof(DidiNowDtc)) { memcpy(&gwDtc, d, sizeof(gwDtc)); gwDtcId = gwDtc.id; return; }
  if (t == DN_GWINFO && n >= (int)sizeof(DidiNowInfo)) { memcpy(gwInfo, ((const DidiNowInfo*)d)->text, sizeof(gwInfo) - 1); gwInfo[sizeof(gwInfo) - 1] = 0; return; }
  if (t == DN_CAL && n >= (int)sizeof(DidiNowCal)) {
    if (((const DidiNowCal*)d)->set) return;
    memcpy(&gwCal, d, sizeof(gwCal));
    gwCalHave = true; gwCalNew = true;
    return;
  }
  if (t == DN_CUST && n >= (int)sizeof(DidiNowCust)) {
    const DidiNowCust* m = (const DidiNowCust*)d;
    for (int i = 0; i < DN_NCUST; i++) {
      gwCust[i].on = (m->on >> i) & 1;
      if ((m->have >> i) & 1) { gwCust[i].v = m->v[i]; gwCust[i].t = millis(); }
    }
    return;
  }
  if (t == DN_CUSTINFO && n >= (int)sizeof(DidiNowCustInfo)) {
    const DidiNowCustInfo* m = (const DidiNowCustInfo*)d;
    if (m->slot >= DN_NCUST) return;
    CustVal& c = gwCust[m->slot];
    c.on = m->on; c.dec = m->dec;
    memcpy(c.name, m->name, 11); c.name[11] = 0;
    memcpy(c.unit, m->unit, 5); c.unit[5] = 0;
    for (char* p = c.name; *p; p++) *p = toupper(*p);
    return;
  }
}

#if ESP_ARDUINO_VERSION_MAJOR >= 3
static void onRecv(const esp_now_recv_info_t*, const uint8_t* d, int n) {
#else
static void onRecv(const uint8_t*, const uint8_t* d, int n) {
#endif
  if (n < (int)sizeof(DidiNowCmd)) return;
  if (((const DidiNowCmd*)d)->magic == DIDINOW_MAGIC && gwOn) {
    uint8_t t = ((const DidiNowCmd*)d)->type;
    if (t == DN_GW || t == DN_DTC || t == DN_GWINFO) { gwFeed(d, n); return; }
  }
  const DidiNowCmd* c = (const DidiNowCmd*)d;
  if (c->magic != DIDINOW_MAGIC || c->type != DN_CMD) return;
  pendId = c->id;
  pendCmd = c->cmd;
  rxCount++;
}

// เรียกหลังเปิด WiFi (SoftAP) แล้ว
inline bool begin() {
  if (esp_now_init() != ESP_OK) { Serial.println("[now] init FAILED"); return false; }
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, BCAST, 6);
  p.channel = 0;                 // ช่องปัจจุบันของ SoftAP
  p.ifidx = WIFI_IF_AP;
  p.encrypt = false;
  if (esp_now_add_peer(&p) != ESP_OK) { Serial.println("[now] add peer FAILED"); return false; }
  esp_now_register_recv_cb(onRecv);
  ok = true;
  Serial.println("[now] ESP-NOW broadcast ready");
  return true;
}

// เรียกทุกเฟรมจาก loop: ส่งค่ารถ ~30 ครั้ง/วินาที
inline void loop(uint8_t link) {
  if (!ok) return;
  uint32_t now = millis();
  if (now - lastSend >= 33) {
    lastSend = now;
    DidiNowData m;
    m.magic = DIDINOW_MAGIC; m.type = DN_DATA; m.link = link; m.seq = seq++;
    m.rpmT = RAW.rpmT;
    const auto& c = data::calByC3 ? data::CAL_NONE : didicfg::cfg;   // ส่งค่าที่ปรับแล้ว (CALIBRATE) ให้จอ CYD ตรงกับจอนี้
    float tp = c.tps1 > c.tps0 ? (RAW.tps - c.tps0) * 100.f / (c.tps1 - c.tps0) : RAW.tps;
    m.rpm = RAW.rpm * c.rpmG / 100.f; m.spd = RAW.spd * c.spdG / 100.f; m.ect = RAW.ect + c.ectO; m.iat = RAW.iat + c.iatO;
    m.volt = RAW.volt + c.vOfs / 10.f;
    m.load = RAW.load; m.tps = fminf(100.f, fmaxf(0.f, tp)); m.map = RAW.map; m.maf = RAW.maf; m.fuel = RAW.fuel; m.stft = RAW.stft;
    esp_now_send(BCAST, (const uint8_t*)&m, sizeof(m));
  }
}

// คำขอ DTC ที่ค้างอยู่ (ให้ main ทำแล้วตอบด้วย reply())
inline uint8_t takeCmd(uint16_t* id) {
  uint8_t c = pendCmd;
  if (c) { *id = pendId; pendCmd = 0; }
  return c;
}
// json = แบบ ["P0133","P0420"]
inline void reply(uint8_t cmd, uint16_t id, bool good, const String& json) {
  if (!ok) return;
  DidiNowDtc r = {};
  r.magic = DIDINOW_MAGIC; r.type = DN_DTC; r.ok = good; r.cmd = cmd; r.id = id;
  int i = 0;
  while (r.n < 16 && (i = json.indexOf('"', i)) >= 0) {
    int j = json.indexOf('"', i + 1);
    if (j < 0) break;
    strlcpy(r.codes[r.n++], json.substring(i + 1, j).c_str(), sizeof(r.codes[0]));
    i = j + 1;
  }
  for (int k = 0; k < 3; k++) { esp_now_send(BCAST, (const uint8_t*)&r, sizeof(r)); delay(20); }   // ส่งซ้ำกันหลุด
}

// ================================================================ C3 (OBD ผ่าน Super Mini)
static uint32_t gwApplied = 0, gwRpmT = 0, gwModeT = 0;
static uint16_t gwMyId = 0x8000;
static void gwCmd(uint8_t cmd, uint16_t id) {
  DidiNowCmd c = {DIDINOW_MAGIC, DN_GWCMD, cmd, id};
  if (c3link::send(&c, sizeof(c))) return;      // ต่อ C3 ทาง BLE อยู่
  if (!ok) return;
  esp_now_send(BCAST, (const uint8_t*)&c, sizeof(c));
}
// ble = ต่อ C3 ทาง Bluetooth (C3 รุ่นใหม่) · ESP-NOW แบบเดิมยังรับได้ถ้าเปิด WiFi
// ---- ซิงก์ CALIBRATE ของจอนี้ ↔ ค่าใน C3
static int gwCalSeen[8];                 // ค่าเมนู (int) ที่ได้จาก C3 ล่าสุด — ช่องที่ผู้ใช้ไม่ได้แก้ส่งค่าเดิมของ C3 กลับไป (ไม่ปัดเศษทิ้ง)
static bool gwCalSynced = false;
static uint16_t gwCalVer = 0xFFFF;
static void gwCalInts(int* a) {
  const auto& c = didicfg::cfg;
  a[0] = c.spdG; a[1] = c.rpmG; a[2] = c.afrG; a[3] = c.ectO; a[4] = c.iatO; a[5] = c.vOfs; a[6] = c.tps0; a[7] = c.tps1;
}
static void gwCalAdopt() {               // C3 -> เมนูจอนี้
  auto& c = didicfg::cfg;
  const DidiNowCal& k = gwCal;
  auto gi = [](float g, int mn, int mx) { return constrain((int)lroundf(g * 100), mn, mx); };
  c.spdG = gi(k.g[DC_SPD], 80, 130); c.rpmG = gi(k.g[DC_RPM], 80, 120); c.afrG = gi(k.g[DC_AFR], 80, 120);
  c.ectO = constrain((int)lroundf(k.o[DC_ECT]), -15, 15); c.iatO = constrain((int)lroundf(k.o[DC_IAT]), -15, 15);
  c.vOfs = constrain((int)lroundf(k.o[DC_VOLT] * 10), -20, 20);
  float g = k.g[DC_TPS] > .05f ? k.g[DC_TPS] : 1;
  int t0 = (int)lroundf(-k.o[DC_TPS] / g), t1 = (int)lroundf(-k.o[DC_TPS] / g + 100 / g);
  c.tps0 = constrain(t0, 0, 60); c.tps1 = constrain(t1, 30, 100);
  gwCalInts(gwCalSeen);
  didicfg::saveLater();
}
static void gwCalPush() {                // เมนูจอนี้ -> C3 (เฉพาะช่องที่แก้)
  int a[8];
  gwCalInts(a);
  DidiNowCal m = gwCal;
  m.magic = DIDINOW_MAGIC; m.type = DN_CAL; m.set = 1;
  if (a[0] != gwCalSeen[0]) m.g[DC_SPD] = a[0] / 100.f;
  if (a[1] != gwCalSeen[1]) m.g[DC_RPM] = a[1] / 100.f;
  if (a[2] != gwCalSeen[2]) m.g[DC_AFR] = a[2] / 100.f;
  if (a[3] != gwCalSeen[3]) m.o[DC_ECT] = a[3];
  if (a[4] != gwCalSeen[4]) m.o[DC_IAT] = a[4];
  if (a[5] != gwCalSeen[5]) m.o[DC_VOLT] = a[5] / 10.f;
  if (a[6] != gwCalSeen[6] || a[7] != gwCalSeen[7]) {
    float g = a[7] > a[6] ? 100.f / (a[7] - a[6]) : 1;
    m.g[DC_TPS] = g; m.o[DC_TPS] = -a[6] * g;
  }
  if (c3link::send(&m, sizeof(m))) { memcpy(gwCalSeen, a, sizeof(a)); gwCal = m; gwCal.set = 0; }
}
static void gwCalSync() {
  if (!gwCalHave) return;
  if (gwCalNew) {
    gwCalNew = false;
    if (!gwCalSynced) {
      gwCalSynced = true;
      int a[8], d0[8] = {100, 100, 100, 0, 0, 0, 0, 100};
      gwCalInts(a);
      // C3 ยังไม่เคยตั้งค่า แต่จอนี้เคยปรับไว้ (ก่อนใช้ C3) → ย้ายค่าของจอนี้ไปเก็บที่ C3
      if (gwCal.ver == 0 && memcmp(a, d0, sizeof(a))) { memcpy(gwCalSeen, d0, sizeof(d0)); gwCalVer = 0; gwCalPush(); return; }
    }
    if (gwCal.ver != gwCalVer) { gwCalVer = gwCal.ver; gwCalAdopt(); }
    return;
  }
  int a[8];
  gwCalInts(a);
  if (memcmp(a, gwCalSeen, sizeof(a))) gwCalPush();
}
// TPS ดิบ (ก่อน C3 ปรับ) — ใช้กับ "ตั้ง TPS ตอนนี้เป็น 0% / 100%"
inline float gwRawTps() {
  if (!gwCalHave) return RAW.tps;
  float g = gwCal.g[DC_TPS] > .05f ? gwCal.g[DC_TPS] : 1;
  return constrain((RAW.tps - gwCal.o[DC_TPS]) / g, 0.f, 100.f);
}
inline void gwBegin(bool demo, bool ble = true) {
  gwOn = true; gwDemo = demo;
  data::calByC3 = true;
  if (ble) c3link::begin(gwFeed);
}
inline bool gwHeard() { return gwLast && millis() - gwLast < 1500; }
inline bool gwLive() { return gwHeard() && (gwPkt.link == DN_LINK_OK || gwPkt.link == DN_LINK_DEMO); }
inline bool gwIsDemo() { return gwHeard() && gwPkt.link == DN_LINK_DEMO; }
// เรียกทุกเฟรม: เอาค่าล่าสุดจาก C3 ใส่ RAW + บอกโหมด (จริง/จำลอง) ให้ C3 ทุก 3 วิ
inline void gwLoop() {
  if (!gwOn) return;
  uint32_t now = millis();
  if (now - gwModeT > 3000) { gwModeT = now; gwCmd(gwDemo ? DN_CMD_MODE_DEMO : DN_CMD_MODE_AUTO, 0); }
  static uint32_t calT = 0;
  if (now - calT > 200) { calT = now; gwCalSync(); }
  if (gwSeq == gwApplied) return;
  gwApplied = gwSeq;
  DidiNowGw m;
  memcpy(&m, &gwPkt, sizeof(m));
  if (m.rpmT != gwRpmT) {                       // ค่ารอบใหม่ → อัตราเปลี่ยน (ให้จอเดาค่าระหว่างรอ)
    float dt = RAW.rpmT ? (now - RAW.rpmT) / 1000.f : 0;
    RAW.rpmRate = (dt > .02f && dt < 1.f) ? constrain((m.rpm - RAW.rpm) / dt, -9000.f, 9000.f) : 0;
    RAW.rpmT = now;
    gwRpmT = m.rpmT;
  }
  RAW.rpm = m.rpm; RAW.spd = m.spd; RAW.ect = m.ect; RAW.iat = m.iat; RAW.volt = m.volt; RAW.load = m.load;
  RAW.tps = m.tps; RAW.map = m.map; RAW.maf = m.maf; RAW.fuel = m.fuel; RAW.stft = m.stft;
  RAW.afr = m.afr; RAW.afrMode = m.afrMode;
}
inline const char* gwStatus() {
  static char b[72];
  static const char* const AM[] = {"", "  AFR wide", "  O2 narrow", "  AFR cmd"};
  if (!gwHeard()) return strncmp(gwInfo, "sleep", 5) ? (c3link::connected() ? "C3 BLE: waiting data" : "C3 BLE: searching...") : "C3 asleep - start engine to wake";
  uint8_t p = gwPkt.proto < DN_P_COUNT ? gwPkt.proto : 0;
  if (gwPkt.link == DN_LINK_DEMO) return "C3 DEMO";
  if (gwPkt.link != DN_LINK_OK) {
    if (!gwInfo[0]) return "C3: searching car...";
    snprintf(b, sizeof(b), "C3: %s", gwInfo);
    return b;
  }
  snprintf(b, sizeof(b), "C3 %s %u/s%s", DN_P_NAME[p], (unsigned)gwPkt.rate, AM[gwPkt.afrMode & 3]);
  return b;
}
// ส่งคำขอ DTC ของจอ CYD ต่อให้ C3 (C3 ตอบทุกจอด้วยเลข id เดิม)
inline void gwForward(uint8_t cmd, uint16_t id) { gwCmd(cmd, id); }
// DTC จากเมนูบนจอ (ไม่บล็อก)
static int gwUi = 0, gwUiCmd = 0;
static uint16_t gwUiId = 0;
static uint32_t gwUiT = 0;
inline bool gwStartDtc(int c) {
  if (!gwLive() || gwUi == 1) return false;
  gwUiCmd = c; gwUi = 1; gwUiT = millis(); gwUiId = ++gwMyId;
  gwCmd(c == 2 ? DN_CMD_DTC_CLEAR : DN_CMD_DTC_READ, gwUiId);
  return true;
}
static String gwJson(const DidiNowDtc& r) {
  String js = "[";
  for (int i = 0; i < r.n && i < 16; i++) { if (i) js += ","; js += "\""; js += r.codes[i]; js += "\""; }
  return js + "]";
}
inline int gwPollDtc(char* buf, size_t n) {
  if (gwUi == 1 && gwDtcId == gwUiId) { gwUi = gwDtc.ok ? 2 : 3; gwUiT = millis(); }
  if (gwUi == 1 && millis() - gwUiT > 12000) { gwUi = 3; gwUiT = millis(); }
  if (gwUi >= 2 && millis() - gwUiT > 15000) gwUi = 0;
  if (gwUi == 2) {
    if (gwUiCmd == 2) snprintf(buf, n, "Codes cleared");
    else {
      String s = "";
      for (int i = 0; i < gwDtc.n && i < 16; i++) { if (i) s += "  "; s += gwDtc.codes[i]; }
      snprintf(buf, n, "%s", s.length() ? s.c_str() : "No codes - all good");
    }
  } else if (gwUi == 3) snprintf(buf, n, gwUiCmd == 2 ? "Clear failed (engine must be OFF)" : "Read failed");
  return gwUi;
}
// แอป/เว็บขอ DTC (บล็อกรอสูงสุด ~10 วิ)
static bool gwWait(uint8_t cmd) {
  if (!gwLive()) return false;
  uint16_t id = ++gwMyId;
  gwCmd(cmd, id);
  uint32_t t0 = millis();
  while (gwDtcId != id && millis() - t0 < 10000) delay(10);
  return gwDtcId == id && gwDtc.ok;
}
inline String gwReadDtc() { return gwWait(DN_CMD_DTC_READ) ? gwJson(gwDtc) : String("[]"); }
inline bool gwClearDtc() { return gwWait(DN_CMD_DTC_CLEAR); }

}  // namespace didinow
