// didi_c3_gateway — ESP32-C3 Super Mini เสียบช่อง OBD2: อ่านค่าจากรถ (CAN หรือ K-line) แล้วส่งทาง Bluetooth (BLE)
//  ให้จอ ESP32-S3 4.3", จอ CYD 2.8" และแอปบนจอแอนดรอยด์ ต่อพร้อมกันได้ 3 จอ — ไม่ต้องใช้ ELM327 / WiFi
//  ต้องลงไลบรารี "NimBLE-Arduino" (by h2zero, 2.x) · ESP-NOW แบบเดิมยังเปิดได้ (USE_ESPNOW 1)
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
#include <Preferences.h>
#define USE_BLE 1       // ส่งทาง Bluetooth (BLE) ให้ S3 / CYD / แอป
#define USE_ESPNOW 0    // ส่งทาง ESP-NOW แบบเดิม (ต้องใช้ WiFi) — ปิดไว้
#if USE_ESPNOW
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#endif
#include "didinow.h"
#if USE_BLE
#include "c3ble.h"
#endif
#include "canobd.h"
#include "kline.h"
#include "esp_sleep.h"
#include "driver/gpio.h"

// ---------------------------------------------------------------- ขา (แก้ตรงนี้ถ้าต่อต่างจากรูป)
#define PIN_CAN_TX 4
#define PIN_CAN_RX 5
#define PIN_K_RX 20     // สัญญาณจากรถ (ขาออกของโมดูล K-line)
#define PIN_K_TX 21     // ส่งไปรถ (ขาเข้าของโมดูล K-line)
#define PIN_LED 8       // LED บนบอร์ด (ติดเมื่อ LOW)
#define PIN_BOOT 9
#define USE_VBAT 0      // 1 = ต่อสายวัดแบตที่ GPIO3 แล้ว (ดู README) · 0 = ไม่ต่อ
#define PIN_VBAT 3      // วัดแบต: 12V ──[100k]──┬── GPIO3 ──[18k]── GND  (ไม่ต่อ = ใช้ค่าแบตจาก ECU ถ้ามี)
#define VBAT_RATIO ((100.0f + 18.0f) / 18.0f)
// ประหยัดแบตตอนดับรถ
#define RUN_V 13.0f         // แบต ≥ นี้ = เครื่องติด (ไดชาร์จทำงาน) → ตื่นทำงาน
#define LOWBAT_V 12.0f      // แบต < นี้ ตอนดับรถ = หลับยาว (กันแบตหมด)
#define IDLE_SLEEP_S 180    // ไม่ได้ยินรถ + เครื่องไม่ติด นานเท่านี้ → หลับ
#define WAKE_S 20           // หลับแล้วตื่นมาเช็กแบตทุก ๆ (วินาที)
#define WAKE_LOWBAT_S 300   // แบตต่ำ: ตื่นเช็กห่างขึ้น
#define WAKE_NOVBAT_S 60    // ไม่ได้ต่อสายวัดแบต: ตื่นมาลองเรียกรถทุก ๆ (วินาที)
#define USE_CAN 1
#define USE_KLINE 1

static Preferences prefs;
static bool demo = false;

// ---------------------------------------------------------------- แบต (ADC) + หลับ
static float vbat = 0;                 // แรงดันแบตที่วัดได้ (0 = ไม่ได้ต่อสายวัด)
RTC_DATA_ATTR static uint8_t rtcProto = 0;   // โปรโตคอลที่ต่อรถได้ล่าสุด (จำข้ามการหลับ → ตื่นมาลองตัวนั้นตัวเดียว เร็วกว่า)
static float readVbat() {
  if (!USE_VBAT) return 0;
  uint32_t mv = 0;
  for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(PIN_VBAT);
  float v = mv / 8.f / 1000.f * VBAT_RATIO;
  return v > 5.f ? v : 0;              // ต่ำกว่า 5V = ไม่ได้ต่อสาย
}
static void deepSleep(uint32_t sec) {
  Serial.printf("[sleep] %u s (vbat %.2f)\n", (unsigned)sec, vbat);
  Serial.flush();
#if USE_ESPNOW
  esp_now_deinit();
  esp_wifi_stop();
#endif
#if USE_BLE
  NimBLEDevice::deinit(true);
#endif
  kline::stop();
  canobd::stop();
  pinMode(PIN_K_TX, OUTPUT);
  kline::txLevel(true);                // สาย K ว่างไว้ระหว่างหลับ
  gpio_hold_en((gpio_num_t)PIN_K_TX);
  digitalWrite(PIN_LED, HIGH);         // ไฟ LED ดับ
  gpio_hold_en((gpio_num_t)PIN_LED);
  gpio_deep_sleep_hold_en();
  esp_sleep_enable_timer_wakeup((uint64_t)sec * 1000000ULL);
  esp_deep_sleep_start();
}
// ตื่นจากหลับ: เช็กแบตก่อน (ไม่เปิด WiFi) ถ้าเครื่องยังไม่ติด → หลับต่อเลย (กินไฟน้อยมาก)
static void wakeCheck() {
  gpio_hold_dis((gpio_num_t)PIN_K_TX);
  gpio_hold_dis((gpio_num_t)PIN_LED);
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) return;
  vbat = readVbat();
  if (!vbat) return;                   // ไม่ได้ต่อสายวัดแบต → ตื่นมาลองเรียกรถ
  if (vbat < RUN_V) {
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, HIGH);
    esp_sleep_enable_timer_wakeup((uint64_t)(vbat < LOWBAT_V ? WAKE_LOWBAT_S : WAKE_S) * 1000000ULL);
    gpio_hold_en((gpio_num_t)PIN_LED);
    gpio_deep_sleep_hold_en();
    esp_deep_sleep_start();
  }
}

// ---------------------------------------------------------------- ค่าจากรถ
struct Raw {
  float rpm = 0, spd = 0, ect = 0, iat = 0, volt = 0, load = 0, tps = 0, map = 0, maf = 0, fuel = 0, stft = 0, afr = 0;
  float timing = -999, oil = -999;
  uint32_t rpmT = 0;
  uint8_t afrMode = 0;
} R;
static uint8_t proto = DN_P_NONE;
static uint8_t carLink = DN_LINK_WAIT;
static uint32_t lastOk = 0, answers = 0, rateT = 0;
static uint16_t rate = 0;
static int fails = 0;

// ---------------------------------------------------------------- ส่งคำถาม OBD ผ่านโปรโตคอลที่ต่อได้
static int ask(const uint8_t* req, int rl, uint8_t* resp, int maxLen, uint32_t tmo = 0, bool multi = false, int expect = 0) {
  if (proto == DN_P_CAN500 || proto == DN_P_CAN250) return canobd::request(req, rl, resp, maxLen, tmo ? tmo : 120);
  if (proto >= DN_P_ISO9141) return kline::request(req, rl, resp, maxLen, tmo ? tmo : 150, multi, expect);
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

// จำนวนไบต์ข้อมูลของแต่ละ PID (ให้ K-line รู้ว่าคำตอบจบเมื่อไหร่)
static int pidBytes(uint8_t p) {
  if (p == 0x0C || p == 0x10 || p == 0x42 || p == 0x44 || p == 0x14) return 2;
  if ((p >= 0x24 && p <= 0x2B) || (p >= 0x34 && p <= 0x3B)) return 4;
  return 1;
}
static bool pid(uint8_t p) {
  uint8_t q[2] = {0x01, p}, r[8];
  int n = ask(q, 2, r, sizeof(r), 0, false, 2 + pidBytes(p));
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
    case 0x42: if (m >= 2 && !vbat) R.volt = ((b[0] << 8) | b[1]) / 1000.f; break;   // มีสายวัดแบตเอง = ใช้ค่านั้นแทน
    case 0x14: R.afr = b[0] / 200.f; break;
    case 0x0E: R.timing = b[0] / 2.f - 64; break;
    case 0x5C: R.oil = b[0] - 40.f; break;
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
static bool tryK(bool fast, bool lineIdle = false) {
#if USE_KLINE
  if (fast || lineIdle) {                                           // ตรวจสายก่อนทุกรอบ (กลับขั้วอัตโนมัติ)
    bool pr = kline::probe();
    setDiag(kline::probeMsg);
    if (!pr) return false;
  }
  bool ok = fast ? kline::fastInit() : kline::slowInit(lineIdle);
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

// ---------------------------------------------------------------- ส่ง/รับ: BLE (+ ESP-NOW ถ้าเปิด)
static volatile uint8_t pendCmd = 0;
static volatile uint16_t pendId = 0;
static uint16_t seq = 0;
static DidiNowRaw pendRaw = {};
static volatile bool rawPending = false;
// คำสั่งจากจอ/แอป (มาทาง BLE หรือ ESP-NOW)
static void gotCmd(const uint8_t* d, int n) {
  if (n < (int)sizeof(DidiNowCmd)) return;
  const DidiNowCmd* c = (const DidiNowCmd*)d;
  if (c->magic != DIDINOW_MAGIC) return;
  if (c->type == DN_GWCMD) { pendId = c->id; pendCmd = c->cmd; return; }
  if (c->type == DN_RAWREQ && n >= (int)sizeof(DidiNowRaw) && !rawPending) {
    memcpy(&pendRaw, d, sizeof(pendRaw));
    rawPending = true;
  }
}
static void tx(const void* p, size_t n) {
#if USE_BLE
  c3ble::send(p, n);
#endif
#if USE_ESPNOW
  static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  esp_now_send(BCAST, (const uint8_t*)p, n);
#endif
}
#if USE_ESPNOW
static void onRecv(const esp_now_recv_info_t*, const uint8_t* d, int n) { gotCmd(d, n); }
static bool nowBegin() {
  static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
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
#endif
static void sendData() {
  DidiNowGw m = {};
  m.magic = DIDINOW_MAGIC; m.type = DN_GW; m.seq = seq++;
  m.link = demo ? DN_LINK_DEMO : carLink;
  m.proto = demo ? DN_P_DEMO : proto;
  m.afrMode = R.afrMode; m.rate = rate; m.rpmT = R.rpmT;
  m.rpm = R.rpm; m.spd = R.spd; m.ect = R.ect; m.iat = R.iat; m.volt = R.volt; m.load = R.load; m.tps = R.tps;
  m.map = R.map; m.maf = R.maf; m.fuel = R.fuel; m.stft = R.stft; m.afr = R.afr;
  m.timing = R.timing; m.oil = R.oil;
  tx(&m, sizeof(m));
}
static void sendInfo() {
  DidiNowInfo m = {};
  m.magic = DIDINOW_MAGIC; m.type = DN_GWINFO;
  strlcpy(m.text, diag, sizeof(m.text));
  tx(&m, sizeof(m));
}
static void sendDtc(uint8_t cmd, uint16_t id, bool ok, char codes[16][6], int n) {
  DidiNowDtc r = {};
  r.magic = DIDINOW_MAGIC; r.type = DN_DTC; r.ok = ok; r.cmd = cmd; r.id = id;
  for (int i = 0; i < n && i < 16; i++) { strlcpy(r.codes[i], codes[i], 6); r.n++; }
  for (int k = 0; k < 3; k++) { tx(&r, sizeof(r)); delay(20); }
}
// Custom PID: ถามรถด้วยคำสั่ง/ID ที่จอขอมา แล้วตอบคำตอบดิบกลับไป
static void handleRaw() {
  if (!rawPending) return;
  DidiNowRaw q = pendRaw;
  rawPending = false;
  DidiNowRawRes r = {};
  r.magic = DIDINOW_MAGIC; r.type = DN_RAWRES; r.id = q.id;
  int len = constrain(q.len, 1, 7);
  if (demo) {
    r.n = 3; r.data[0] = q.data[0] + 0x40; r.data[1] = len > 1 ? q.data[1] : 0; r.data[2] = 120;   // จำลอง: A = 120
  } else if (carLink != DN_LINK_OK) {
    r.n = -1;
  } else if (proto == DN_P_CAN500 || proto == DN_P_CAN250) {
    uint32_t to = q.hdr ? q.hdr : (canobd::physical ? canobd::ecuRx - 8 : 0x7DF);
    r.n = canobd::requestTo(to, q.data, len, r.data, sizeof(r.data), 300);
  } else if (q.hdr && q.hdr != 0x7DF && q.hdr != 0x7E0) {
    r.n = -2;                                            // K-line: ถามกล่องอื่นไม่ได้
  } else {
    r.n = ask(q.data, len, r.data, sizeof(r.data), 400, true);
  }
  if (r.n < -2) r.n = -1;
  tx(&r, sizeof(r));
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
  static const uint8_t SLOW[] = {0x0D, 0x05, 0x0F, 0x04, 0x0B, 0x10, 0x2F, 0x06, 0x42, 0x0E, 0x5C};
  int slow = 0;
  uint32_t step = 0;
  for (;;) {
    if (demo) { handleRaw(); vTaskDelay(pdMS_TO_TICKS(100)); continue; }
    if (carLink != DN_LINK_OK) {
      if (connect()) { carLink = DN_LINK_OK; fails = 0; lastOk = millis(); rtcProto = proto; Serial.printf("[obd] connected: %s\n", DN_P_NAME[proto]); }
      else { Serial.println("[obd] no car — retry"); vTaskDelay(pdMS_TO_TICKS(1500)); }
      continue;
    }
    if (pendCmd == DN_CMD_DTC_READ || pendCmd == DN_CMD_DTC_CLEAR) { handleCmd(); continue; }
    if (rawPending) { handleRaw(); continue; }
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
  wakeCheck();
  Serial.begin(115200);
  delay(300);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BOOT, INPUT_PULLUP);
  prefs.begin("didic3", false);
  demo = prefs.getBool("demo", false);
  kline::begin(PIN_K_RX, PIN_K_TX);
  analogSetPinAttenuation(PIN_VBAT, ADC_11db);
  vbat = readVbat();
  // ตื่นจากหลับ (ไม่มีสายวัดแบต): ยังไม่เปิด WiFi — ลองเรียกรถรอบเดียวก่อน ไม่ตอบ = หลับต่อทันที
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER && !vbat && !demo) {
    bool ok = false;
    if (rtcProto == DN_P_ISO9141 || rtcProto == DN_P_KWPSLOW) ok = tryK(false, true);
    else if (rtcProto == DN_P_KWPFAST) ok = tryK(true);
    else if (rtcProto == DN_P_CAN500) ok = tryCan(500000, DN_P_CAN500);
    else if (rtcProto == DN_P_CAN250) ok = tryCan(250000, DN_P_CAN250);
    else ok = connect();
    Serial.printf("[wake] car %s\n", ok ? "found" : "not found -> sleep");
    if (!ok) deepSleep(WAKE_NOVBAT_S);
    carLink = DN_LINK_OK; fails = 0; lastOk = millis();
  }
#if USE_BLE
  c3ble::onCmd = gotCmd;
  bool ok = c3ble::begin();
  Serial.printf("\n[didi_c3] BLE %s · mode %s\n", ok ? "ready" : "FAILED", demo ? "DEMO" : "CAR");
#endif
#if USE_ESPNOW
  bool okn = nowBegin();
  Serial.printf("[didi_c3] ESP-NOW %s · MAC %s\n", okn ? "ready" : "FAILED", WiFi.macAddress().c_str());
#endif
  xTaskCreate(obdTask, "obd", 6144, nullptr, 2, nullptr);
}

void loop() {
  static uint32_t lastSend = 0, lastSim = 0;
  uint32_t now = millis();
  bootBtn();
  led();
  if (pendCmd == DN_CMD_MODE_DEMO || pendCmd == DN_CMD_MODE_AUTO || (demo && pendCmd)) handleCmd();
  if (demo) { float dt = lastSim ? (now - lastSim) / 1000.f : 0; lastSim = now; sim(fminf(dt, .1f)); }
  if (now - rateT >= 1000) {
    rate = demo ? 0 : answers; answers = 0; rateT = now;
    if (carLink == DN_LINK_OK && proto >= DN_P_ISO9141) Serial.printf("[obd] %u/s  P3 %u ms\n", rate, kline::p3);
  }
  // แบต + หลับเมื่อดับรถ
  static uint32_t lastV = 0, lastCar = 0;
  if (now - lastV >= 250) {
    lastV = now;
    float v = readVbat();
    vbat = v ? (vbat ? vbat + (v - vbat) * .3f : v) : 0;
    if (vbat && !demo) R.volt = vbat;
  }
  if (!lastCar || carLink == DN_LINK_OK || demo) lastCar = now;
  bool engineOn = vbat >= RUN_V;
  if (!demo && !engineOn && now - lastCar > (vbat && vbat < LOWBAT_V ? 30000UL : IDLE_SLEEP_S * 1000UL)) {
    char m[59];
    if (vbat) snprintf(m, sizeof(m), "sleep: engine off, batt %.1fV", vbat);
    else snprintf(m, sizeof(m), "sleep: no car (check every %ds)", WAKE_NOVBAT_S);
    setDiag(m);
    for (int i = 0; i < 3; i++) { sendInfo(); delay(30); }
    deepSleep(!vbat ? WAKE_NOVBAT_S : vbat < LOWBAT_V ? WAKE_LOWBAT_S : WAKE_S);
  }
  if (now - lastSend >= 33) { lastSend = now; sendData(); }   // ~30 ครั้ง/วินาที
  static uint32_t lastInfo = 0;
  if (!demo && carLink != DN_LINK_OK && now - lastInfo >= 700) { lastInfo = now; sendInfo(); }   // ผลตรวจสายขึ้นจอ
  delay(2);
}
