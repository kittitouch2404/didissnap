// didi_c3_gateway — ESP32-C3 Super Mini เสียบช่อง OBD2: อ่านค่าจากรถ (CAN หรือ K-line) แล้วส่งทาง ESP-NOW
//  ให้จอ ESP32-S3 4.3" และจอ CYD 2.8" (กี่จอก็ได้) — ไม่ต้องใช้ตัวอ่าน ELM327/Bluetooth
//
//  บอร์ด: "ESP32C3 Dev Module" · USB CDC On Boot: Enabled (ดูข้อความผ่าน USB ได้) · Partition: Default 4MB
//
//  วงจร (ดูรูปใน README):
//    CAN  : SN65HVD230  CTX → GPIO4 · CRX → GPIO5 · 3V3 · GND · CANH = OBD ขา 6 · CANL = OBD ขา 14 (ไม่ใส่ 120 Ω)
//    K-line: โมดูล K-line ขาออก (สัญญาณจากรถ) → GPIO20 · ขาเข้า (ส่งไปรถ) ← GPIO21 · K = OBD ขา 7
//            ⚠ ถ้าขาออกของโมดูลเป็น 5V ต้องแบ่งแรงดันก่อนเข้า GPIO20 (ขา C3 รับได้ 3.3V)
//    ไฟ   : OBD ขา 16 (12V) → ฟิวส์ → ตัวแปลง 5V → ขา 5V ของ Super Mini · GND = OBD ขา 4 + 5
//
//  ปุ่ม BOOT (GPIO9) กดค้าง 2 วิ = สลับโหมดจำลอง (DEMO) ↔ อ่านจากรถ (จำไว้ข้ามการปิดเปิด)
//  ไฟ LED บนบอร์ด (GPIO8): กระพริบเร็ว = กำลังหารถ · ติดค้าง กระพริบดับสั้น ๆ = ได้ค่าจากรถ · กระพริบช้า = DEMO
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include "didinow.h"
#include "canobd.h"
#include "kline.h"

// ---------------------------------------------------------------- ขา (แก้ตรงนี้ถ้าต่อต่างจากรูป)
#define PIN_CAN_TX 4
#define PIN_CAN_RX 5
#define PIN_K_RX 20     // สัญญาณจากรถ (ขาออกของโมดูล K-line)
#define PIN_K_TX 21     // ส่งไปรถ (ขาเข้าของโมดูล K-line)
#define PIN_LED 8       // LED บนบอร์ด (ติดเมื่อ LOW)
#define PIN_BOOT 9
#define USE_CAN 1
#define USE_KLINE 1

static Preferences prefs;
static bool demo = false;

// ---------------------------------------------------------------- ค่าจากรถ
struct Raw {
  float rpm = 0, spd = 0, ect = 0, iat = 0, volt = 0, load = 0, tps = 0, map = 0, maf = 0, fuel = 0, stft = 0, afr = 0;
  uint32_t rpmT = 0;
  uint8_t afrMode = 0;
} R;
static uint8_t proto = DN_P_NONE;
static uint8_t carLink = DN_LINK_WAIT;
static uint32_t lastOk = 0, answers = 0, rateT = 0;
static uint16_t rate = 0;
static int fails = 0;

// ---------------------------------------------------------------- ส่งคำถาม OBD ผ่านโปรโตคอลที่ต่อได้
static int ask(const uint8_t* req, int rl, uint8_t* resp, int maxLen, uint32_t tmo = 0, bool multi = false) {
  if (proto == DN_P_CAN500 || proto == DN_P_CAN250) return canobd::request(req, rl, resp, maxLen, tmo ? tmo : 120);
  if (proto >= DN_P_ISO9141) return kline::request(req, rl, resp, maxLen, tmo ? tmo : 150, multi);
  return -1;
}

// ---------------------------------------------------------------- PID ที่รถรองรับ + แหล่ง AFR
static uint32_t pidMask[3] = {0, 0, 0};
static uint8_t afrPid = 0;
static const float STOICH = 14.7f;
static bool pidOn(uint8_t p) {
  if (p < 1 || p > 0x60) return false;
  return (pidMask[(p - 1) / 32] >> (31 - (p - 1) % 32)) & 1;
}
static bool readMasks() {
  memset(pidMask, 0, sizeof(pidMask));
  for (int g = 0; g < 3; g++) {
    if (g && !(pidMask[g - 1] & 1)) break;
    uint8_t q[2] = {0x01, (uint8_t)(g * 0x20)}, r[8];
    int n = ask(q, 2, r, sizeof(r), 400);
    if (n < 6) { if (g == 0) return false; break; }
    pidMask[g] = ((uint32_t)r[2] << 24) | ((uint32_t)r[3] << 16) | ((uint32_t)r[4] << 8) | r[5];
  }
  afrPid = 0;
  for (uint8_t p = 0x24; p <= 0x2B && !afrPid; p++) if (pidOn(p)) afrPid = p;
  for (uint8_t p = 0x34; p <= 0x3B && !afrPid; p++) if (pidOn(p)) afrPid = p;
  if (!afrPid && pidOn(0x44)) afrPid = 0x44;
  if (!afrPid && pidOn(0x14)) afrPid = 0x14;
  R.afrMode = !afrPid ? 0 : afrPid == 0x14 ? 2 : afrPid == 0x44 ? 3 : 1;
  Serial.printf("[obd] PIDs %08X %08X %08X  AFR PID %02X (mode %d)\n", (unsigned)pidMask[0], (unsigned)pidMask[1],
                (unsigned)pidMask[2], afrPid, R.afrMode);
  return true;
}

static bool pid(uint8_t p) {
  uint8_t q[2] = {0x01, p}, r[8];
  int n = ask(q, 2, r, sizeof(r));
  if (n < 3) { fails++; return false; }
  const uint8_t* b = r + 2;
  int m = n - 2;
  switch (p) {
    case 0x0C: if (m >= 2) { R.rpm = ((b[0] << 8) | b[1]) / 4.f; R.rpmT = millis(); } break;
    case 0x0D: R.spd = b[0]; break;
    case 0x05: R.ect = b[0] - 40.f; break;
    case 0x0F: R.iat = b[0] - 40.f; break;
    case 0x04: R.load = b[0] * 100.f / 255; break;
    case 0x11: R.tps = b[0] * 100.f / 255; break;
    case 0x0B: R.map = b[0]; break;
    case 0x10: if (m >= 2) R.maf = ((b[0] << 8) | b[1]) / 100.f; break;
    case 0x2F: R.fuel = b[0] * 100.f / 255; break;
    case 0x06: R.stft = (b[0] - 128) * 100.f / 128; break;
    case 0x42: if (m >= 2) R.volt = ((b[0] << 8) | b[1]) / 1000.f; break;
    case 0x14: R.afr = b[0] / 200.f; break;
    default:
      if (((p >= 0x24 && p <= 0x2B) || (p >= 0x34 && p <= 0x3B) || p == 0x44) && m >= 2)
        R.afr = ((b[0] << 8) | b[1]) * 2.f / 65536.f * STOICH;
      break;
  }
  fails = 0;
  lastOk = millis();
  answers++;
  return true;
}

// ---------------------------------------------------------------- DTC
static int dtcRead(char codes[16][6]) {
  uint8_t q[1] = {0x03}, r[96];
  int n = ask(q, 1, r, sizeof(r), 800, true);
  if (n < 1) return -1;
  int i = 1, k = 0;
  // CAN มีจำนวนรหัสนำหน้า (ไบต์หลัง 0x43) — K-line ไม่มี
  if (proto == DN_P_CAN500 || proto == DN_P_CAN250) i = 2;
  static const char T[] = {'P', 'C', 'B', 'U'};
  for (; i + 1 < n && k < 16; i += 2) {
    uint16_t v = (r[i] << 8) | r[i + 1];
    if (!v) continue;
    snprintf(codes[k++], 6, "%c%04X", T[v >> 14], v & 0x3FFF);
  }
  return k;
}
static bool dtcClear() {
  uint8_t q[1] = {0x04}, r[8];
  return ask(q, 1, r, sizeof(r), 1500) >= 1 && r[0] == 0x44;
}

// ---------------------------------------------------------------- หารถ: CAN 500k → CAN 250k → K-line เร็ว → K-line ช้า
static bool tryCan(uint32_t br, uint8_t p) {
#if USE_CAN
  if (!canobd::start(PIN_CAN_TX, PIN_CAN_RX, br, true)) return false;
  bool traffic = canobd::sniff(300);                    // ฟังก่อน ไม่ส่งอะไรออกบัส (กันรบกวนรถถ้าความเร็วไม่ตรง)
  canobd::stop();
  // บัสเงียบ (บางรถช่อง OBD ผ่าน gateway) → ลองถามที่ 500k ครั้งเดียว
  if (!traffic && br != 500000) return false;
  if (!canobd::start(PIN_CAN_TX, PIN_CAN_RX, br, false)) return false;
  proto = p;
  if (readMasks()) { canobd::physical = true; Serial.printf("[obd] CAN %u ok, ECU 0x%03X\n", (unsigned)br, (unsigned)canobd::ecuRx); return true; }
  canobd::stop();
  proto = DN_P_NONE;
#endif
  return false;
}
static char diag[59] = "starting";
static void setDiag(const char* s) { strlcpy(diag, s, sizeof(diag)); Serial.printf("[diag] %s\n", s); }
static bool tryK(bool fast) {
#if USE_KLINE
  if (fast) {                                           // ตรวจสายก่อนทุกรอบ (กลับขั้วอัตโนมัติ)
    bool pr = kline::probe();
    setDiag(kline::probeMsg);
    if (!pr) return false;
  }
  bool ok = fast ? kline::fastInit() : kline::slowInit();
  Serial.printf("[obd] K-line %s init: %s\n", fast ? "fast" : "slow", kline::info);
  char m[59];
  snprintf(m, sizeof(m), "%s | %s: %s", kline::probeMsg, fast ? "fast" : "slow", kline::info);
  setDiag(m);
  if (!ok) return false;
  proto = fast ? DN_P_KWPFAST : (kline::proto == kline::KWP ? DN_P_KWPSLOW : DN_P_ISO9141);
  if (readMasks()) return true;
  kline::stop();
  proto = DN_P_NONE;
#endif
  return false;
}
static void disconnect() {
  canobd::stop();
  kline::stop();
  proto = DN_P_NONE;
  carLink = DN_LINK_WAIT;
}
static bool connect() {
  carLink = DN_LINK_WAIT;
  setDiag("trying CAN 500k");
  if (tryCan(500000, DN_P_CAN500)) return true;
  if (tryCan(250000, DN_P_CAN250)) return true;
  if (tryK(true)) return true;
  if (tryK(false)) return true;
  return false;
}

// ---------------------------------------------------------------- โหมดจำลอง (ไม่ต่อรถก็ทดสอบจอได้)
static void sim(float dt) {
  static float t = 0, rpm = 900, idle = 2;
  static int gear = 1;
  static bool acc = false;
  t += dt;
  const float kmh[6] = {0, 7.6f, 13.4f, 19.6f, 25.9f, 32.4f};   // km/h ต่อ 1000 rpm
  if (idle > 0) { idle -= dt; rpm = 850 + sinf(t * 7) * 20; R.tps = 12; if (idle <= 0) { acc = true; gear = 1; } }
  else if (acc) {
    R.tps = 85;
    rpm += (gear <= 2 ? 2600 : 1600) * dt;
    if (rpm > 7300) { if (gear < 5) { rpm = rpm * kmh[gear] / kmh[gear + 1]; gear++; } else acc = false; }
  } else {
    R.tps = 12;
    rpm -= 800 * dt;
    if (rpm < 1500) { if (gear > 1) { rpm = rpm * kmh[gear] / kmh[gear - 1]; gear--; } else idle = 2.5f; }
  }
  R.rpm = rpm; R.rpmT = millis();
  R.spd = idle > 0 ? 0 : rpm / 1000 * kmh[gear];
  R.ect = fminf(88, R.ect < 40 ? 40 : R.ect + dt * 2);
  R.iat = 36 + sinf(t / 7) * 2;
  R.volt = 14.1f + sinf(t * 1.3f) * .1f;
  R.load = R.tps > 20 ? 72 : 18;
  R.map = R.tps > 20 ? 95 : 32;
  R.maf = rpm / 1000 * (R.tps > 20 ? 9 : 2.2f);
  R.fuel = 67;
  R.stft = sinf(t / 1.7f) * 3.4f;
  R.afrMode = 1;
  R.afr = R.tps > 20 ? 12.6f + sinf(t * 2) * .3f : 14.7f + sinf(t * 3) * .4f;
}

// ---------------------------------------------------------------- ESP-NOW
static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static volatile uint8_t pendCmd = 0;
static volatile uint16_t pendId = 0;
static uint16_t seq = 0;
static void onRecv(const esp_now_recv_info_t*, const uint8_t* d, int n) {
  if (n < (int)sizeof(DidiNowCmd)) return;
  const DidiNowCmd* c = (const DidiNowCmd*)d;
  if (c->magic != DIDINOW_MAGIC || c->type != DN_GWCMD) return;
  pendId = c->id;
  pendCmd = c->cmd;
}
static bool nowBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(DIDINOW_CHANNEL, WIFI_SECOND_CHAN_NONE);   // ช่องเดียวกับจอ S3 (SoftAP ช่อง 1)
  esp_wifi_set_promiscuous(false);
  if (esp_now_init() != ESP_OK) return false;
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, BCAST, 6);
  p.channel = DIDINOW_CHANNEL;
  p.ifidx = WIFI_IF_STA;
  if (esp_now_add_peer(&p) != ESP_OK) return false;
  esp_now_register_recv_cb(onRecv);
  return true;
}
static void sendData() {
  DidiNowGw m = {};
  m.magic = DIDINOW_MAGIC; m.type = DN_GW; m.seq = seq++;
  m.link = demo ? DN_LINK_DEMO : carLink;
  m.proto = demo ? DN_P_DEMO : proto;
  m.afrMode = R.afrMode; m.rate = rate; m.rpmT = R.rpmT;
  m.rpm = R.rpm; m.spd = R.spd; m.ect = R.ect; m.iat = R.iat; m.volt = R.volt; m.load = R.load; m.tps = R.tps;
  m.map = R.map; m.maf = R.maf; m.fuel = R.fuel; m.stft = R.stft; m.afr = R.afr;
  esp_now_send(BCAST, (const uint8_t*)&m, sizeof(m));
}
static void sendInfo() {
  DidiNowInfo m = {};
  m.magic = DIDINOW_MAGIC; m.type = DN_GWINFO;
  strlcpy(m.text, diag, sizeof(m.text));
  esp_now_send(BCAST, (const uint8_t*)&m, sizeof(m));
}
static void sendDtc(uint8_t cmd, uint16_t id, bool ok, char codes[16][6], int n) {
  DidiNowDtc r = {};
  r.magic = DIDINOW_MAGIC; r.type = DN_DTC; r.ok = ok; r.cmd = cmd; r.id = id;
  for (int i = 0; i < n && i < 16; i++) { strlcpy(r.codes[i], codes[i], 6); r.n++; }
  for (int k = 0; k < 3; k++) { esp_now_send(BCAST, (const uint8_t*)&r, sizeof(r)); delay(20); }
}
static void handleCmd() {
  uint8_t c = pendCmd;
  if (!c) return;
  uint16_t id = pendId;
  pendCmd = 0;
  if (c == DN_CMD_MODE_DEMO || c == DN_CMD_MODE_AUTO) {
    bool d = c == DN_CMD_MODE_DEMO;
    if (d != demo) { demo = d; prefs.putBool("demo", demo); if (demo) disconnect(); Serial.printf("[mode] %s (from screen)\n", demo ? "DEMO" : "CAR"); }
    return;
  }
  char codes[16][6];
  bool run = !demo && carLink == DN_LINK_OK;
  if (c == DN_CMD_DTC_READ) {
    int n = run ? dtcRead(codes) : -1;
    if (demo) { strcpy(codes[0], "P0420"); n = 1; run = true; }   // โหมดจำลอง: ตัวอย่าง 1 รหัส
    sendDtc(c, id, n >= 0, codes, max(0, n));
    Serial.printf("[dtc] read -> %d\n", n);
  } else if (c == DN_CMD_DTC_CLEAR) {
    bool ok = demo || (run && dtcClear());
    sendDtc(c, id, ok, codes, 0);
    Serial.printf("[dtc] clear -> %d\n", ok);
  }
}

// ---------------------------------------------------------------- LED + ปุ่ม BOOT
static void led() {
  uint32_t t = millis();
  bool on;
  if (demo) on = (t / 600) % 2;
  else if (carLink == DN_LINK_OK) on = (t % 1000) > 60;
  else on = (t / 120) % 2;
  digitalWrite(PIN_LED, on ? LOW : HIGH);
}
static void bootBtn() {
  static uint32_t down = 0;
  static bool done = false;
  if (digitalRead(PIN_BOOT) == LOW) {
    if (!down) down = millis();
    if (!done && millis() - down > 2000) {
      done = true;
      demo = !demo;
      prefs.putBool("demo", demo);
      if (demo) disconnect();
      Serial.printf("[mode] %s (BOOT button)\n", demo ? "DEMO" : "CAR");
    }
  } else { down = 0; done = false; }
}

// ---------------------------------------------------------------- งานอ่านรถ (แยก task: ตอน init K-line บล็อกหลายวินาที ไม่ให้ ESP-NOW หยุดส่ง)
static void obdTask(void*) {
  static const uint8_t SLOW[] = {0x0D, 0x05, 0x0F, 0x04, 0x0B, 0x10, 0x2F, 0x06, 0x42};
  int slow = 0;
  uint32_t step = 0;
  for (;;) {
    if (demo) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
    if (carLink != DN_LINK_OK) {
      if (connect()) { carLink = DN_LINK_OK; fails = 0; lastOk = millis(); Serial.printf("[obd] connected: %s\n", DN_P_NAME[proto]); }
      else { Serial.println("[obd] no car — retry"); vTaskDelay(pdMS_TO_TICKS(1500)); }
      continue;
    }
    if (pendCmd == DN_CMD_DTC_READ || pendCmd == DN_CMD_DTC_CLEAR) { handleCmd(); continue; }
    // ลำดับเหมือนจอ S3: รอบ → TPS → AFR (ทุกชุดที่ 4 = ค่าช้าแทน) → รอบ
    int k = step % 4, set = step / 4;
    step++;
    if (k == 1) pid(0x11);
    else if (k == 2) {
      if (afrPid && set % 4 != 3) pid(afrPid);
      else {
        for (int g = 0; g < (int)sizeof(SLOW); g++) {   // ข้าม PID ที่รถไม่รองรับ
          uint8_t p = SLOW[slow];
          slow = (slow + 1) % sizeof(SLOW);
          if (pidOn(p)) { pid(p); break; }
        }
      }
    } else pid(0x0C);
    if (fails >= 8) { Serial.println("[obd] lost car"); disconnect(); }
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BOOT, INPUT_PULLUP);
  prefs.begin("didic3", false);
  demo = prefs.getBool("demo", false);
  kline::begin(PIN_K_RX, PIN_K_TX);
  bool ok = nowBegin();
  Serial.printf("\n[didi_c3] ESP-NOW %s · mode %s · MAC %s\n", ok ? "ready" : "FAILED", demo ? "DEMO" : "CAR",
                WiFi.macAddress().c_str());
  xTaskCreate(obdTask, "obd", 6144, nullptr, 2, nullptr);
}

void loop() {
  static uint32_t lastSend = 0, lastSim = 0;
  uint32_t now = millis();
  bootBtn();
  led();
  if (pendCmd == DN_CMD_MODE_DEMO || pendCmd == DN_CMD_MODE_AUTO || (demo && pendCmd)) handleCmd();
  if (demo) { float dt = lastSim ? (now - lastSim) / 1000.f : 0; lastSim = now; sim(fminf(dt, .1f)); }
  if (now - rateT >= 1000) { rate = demo ? 0 : answers; answers = 0; rateT = now; }
  if (now - lastSend >= 33) { lastSend = now; sendData(); }   // ~30 ครั้ง/วินาที
  static uint32_t lastInfo = 0;
  if (!demo && carLink != DN_LINK_OK && now - lastInfo >= 700) { lastInfo = now; sendInfo(); }   // ผลตรวจสายขึ้นจอ
  delay(2);
}
