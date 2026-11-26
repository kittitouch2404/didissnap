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
#include <Update.h>
#define FW_VER "c3 2026.11.26e"   // เวอร์ชันเฟิร์มแวร์ (โชว์ในแอปตอนอัปเดต)
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
#include "c3wifi.h"     // ส่งค่าทาง WiFi (ฮอตสปอตมือถือ) ให้จอแอนดรอยด์ที่ไม่มี BLE
#endif
#include "custeval.h"
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
// เสียบสาย USB กับคอมอยู่ (มีโฮสต์ USB) → ห้ามหลับ: อัปโหลด/ดู Serial ได้ ไม่หลุดวนเสียงเด้ง
// ในรถใช้ไฟ 5V จาก step-down (ไม่มีโฮสต์ USB) → หลับประหยัดแบตตามปกติ
static bool usbHost() {
#if ARDUINO_USB_MODE && ARDUINO_USB_CDC_ON_BOOT
  return HWCDC::isPlugged();
#else
  return false;
#endif
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
  delay(250);                          // ให้ USB ต่อกับคอมทันก่อนเช็ก
  if (vbat < RUN_V && !usbHost()) {
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
  if (dnIsCan(proto)) return canobd::request(req, rl, resp, maxLen, tmo ? tmo : 120);
  if (dnIsK(proto)) return kline::request(req, rl, resp, maxLen, tmo ? tmo : 150, multi, expect);
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
  if (dnIsCan(proto)) i = 2;
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
static char diag[59] = "starting";
static String canDiag;   // ผลลอง CAN ล่าสุด (โชว์ต่อท้ายตอนลอง K-line ด้วย)
static void setDiag(const char* s) { strlcpy(diag, s, sizeof(diag)); Serial.printf("[diag] %s\n", s); }
static bool canHeard = false;   // ได้ยินเฟรม CAN วิ่งในบัส = รถคันนี้ใช้ CAN (ไม่ต้องลอง K-line)
// ลอง ID 11 บิตก่อน (รถส่วนใหญ่) ไม่ตอบ → ลอง ID 29 บิตที่ความเร็วเดียวกัน
// ขา CRX ของโมดูล CAN: ปกติ GPIO5 · ถ้าไม่ได้ยินอะไรเลยลองฟังที่ GPIO3 ด้วย (ต่อสายไว้ขานั้น) แล้วจำขาที่ใช้ได้
static int canRx = PIN_CAN_RX;
static bool canSniff(uint32_t br) {
  if (!canobd::start(PIN_CAN_TX, canRx, br, true)) return false;
  bool t = canobd::sniff(300);
  canobd::stop();
  if (t || USE_VBAT) return t;                          // GPIO3 ใช้วัดแบตอยู่ → ไม่ลอง
  int alt = canRx == PIN_CAN_RX ? 3 : PIN_CAN_RX;
  int r0 = canobd::sniffRx, e0 = canobd::sniffErr;
  if (!canobd::start(PIN_CAN_TX, alt, br, true)) return false;
  bool ta = canobd::sniff(300);
  canobd::stop();
  if (ta) {
    Serial.printf("[obd] CAN RX found on GPIO%d (was GPIO%d)\n", alt, canRx);
    canRx = alt;
    prefs.putInt("canrx", canRx);
    return true;
  }
  canobd::sniffRx = max(r0, canobd::sniffRx);
  canobd::sniffErr = max(e0, canobd::sniffErr);
  return false;
}
// รถใหม่ (D-Max / Navara / รถยุโรป ฯลฯ) มีหลายกล่องตอบ OBD — เลือกกล่องเครื่อง (รองรับรอบเครื่อง PID 0C · ID ต่ำสุด)
// แล้วลองถามตรงกล่องนั้น ไม่ตอบ = ถามทุกกล่อง (7DF) แทน
static void pickEcu() {
#if USE_CAN
  uint32_t ids[8], mk[8];
  int n = canobd::discover(ids, mk, 8);
  uint32_t best = 0, any = 0;
  for (int i = 0; i < n; i++) {
    if (!any || ids[i] < any) any = ids[i];
    if ((mk[i] & (1u << 20)) && (!best || ids[i] < best)) best = ids[i];   // bit 20 = PID 0C (รอบ)
  }
  if (!best) best = any;
  char m[59];
  snprintf(m, sizeof(m), "CAN: %d modules answer, engine 0x%X", n, (unsigned)best);
  setDiag(m);
  if (best) canobd::ecuRx = best;
  canobd::physical = true;
  uint8_t q[2] = {0x01, 0x0C}, r[8];
  if (ask(q, 2, r, sizeof(r), 250) < 4) {
    canobd::physical = false;
    Serial.println("[obd] direct ECU request no reply -> broadcast 7DF");
  }
  readMasks();   // PID ที่รองรับ: ของกล่องเครื่องจริง
#endif
}
static bool tryCan(uint32_t br, uint8_t p, uint8_t pExt) {
#if USE_CAN
  bool traffic = canSniff(br);                          // ฟังก่อน ไม่ส่งอะไรออกบัส (กันรบกวนรถถ้าความเร็วไม่ตรง)
  char dm[59];
  snprintf(dm, sizeof(dm), "CAN %uk: heard %d frames, rx err %d", (unsigned)(br / 1000), canobd::sniffRx, canobd::sniffErr);
  setDiag(dm);
  // บัสเงียบ (บางรถช่อง OBD ผ่าน gateway) → ลองถามที่ 500k ครั้งเดียว
  if (traffic || canobd::sniffErr >= 32) canHeard = true;   // มีเฟรมวิ่ง หรือ error เยอะ (สายสลับ/ความเร็วผิด) = มีบัส CAN
  if (!traffic && br != 500000) return false;
  int txErr = 0, busOff = 0;
  for (int e = 0; e < 2; e++) {
    if (!canobd::start(PIN_CAN_TX, canRx, br, false, e == 1)) return false;
    proto = e ? pExt : p;
    if (readMasks()) {
      pickEcu();
      Serial.printf("[obd] CAN %u %s ok, ECU 0x%X %s\n", (unsigned)br, e ? "29-bit" : "11-bit", (unsigned)canobd::ecuRx,
                    canobd::physical ? "(direct)" : "(broadcast)");
      return true;
    }
    if (!e) canobd::txStatus(&txErr, &busOff);
    canobd::stop();
  }
  proto = DN_P_NONE;
  // บอกสาเหตุ (ขึ้นในแอป/จอ S3): ไม่มีใครรับเฟรม = สาย CANH/CANL ขาด/สลับ หรือโมดูลไม่ได้ไฟ 3.3V
  if (busOff || txErr >= 96) {
    snprintf(dm, sizeof(dm), "CAN %uk no ACK (tx %d): check CANH/L, 3V3, CTX/CRX", (unsigned)(br / 1000), txErr);
  } else if (canobd::sniffRx == 0) {
    snprintf(dm, sizeof(dm), "CAN %uk: bus silent, ECU no reply - key ON? CANH/L?", (unsigned)(br / 1000));
  } else if (canobd::sniffErr >= 16) {
    snprintf(dm, sizeof(dm), "CAN %uk: rx errors %d - swap CANH/CANL / remove 120R", (unsigned)(br / 1000), canobd::sniffErr);
  } else {
    snprintf(dm, sizeof(dm), "CAN %uk: bus ok (%d frames) but no OBD reply", (unsigned)(br / 1000), canobd::sniffRx);
  }
  setDiag(dm);
  canDiag = dm;
#endif
  return false;
}
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
  canDiag = "";
  canHeard = false;
  setDiag("trying CAN 500k");
  if (tryCan(500000, DN_P_CAN500, DN_P_CAN500X)) return true;
  if (tryCan(250000, DN_P_CAN250, DN_P_CAN250X)) return true;
  // มีเฟรม CAN วิ่งแต่ ECU ไม่ตอบ → เป็นรถ CAN แน่ ๆ ไม่ลอง K-line (ข้อความสาเหตุฝั่ง CAN จะได้ค้างให้เห็น)
  if (canHeard) { setDiag(canDiag.c_str()); return false; }
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
// ---- Custom PID ที่ C3 ถามเอง (ตั้งจากแอป จำไว้ในตัว)
static DidiNowCustSet cust[DN_NCUST] = {};
static volatile float custV[DN_NCUST];
static volatile uint32_t custT[DN_NCUST];
static DidiNowCustRaw custRaw[DN_NCUST];
static uint32_t custNext[DN_NCUST];   // เวลาที่จะถามช่องนี้ครั้งถัดไป
static uint8_t custBad[DN_NCUST];     // ไม่ได้คำตอบติดกันกี่ครั้ง
static volatile uint8_t custDirty = 0, custRawNew = 0;   // บิต = ช่องที่ต้องบันทึก / มีคำตอบดิบใหม่
static portMUX_TYPE custMux = portMUX_INITIALIZER_UNLOCKED;
static void custLoad() {
  for (int i = 0; i < DN_NCUST; i++) {
    char k[6] = {'c', 'u', 's', (char)('0' + i), 0};
    if (prefs.getBytes(k, &cust[i], sizeof(cust[i])) != sizeof(cust[i]) || cust[i].magic != DIDINOW_MAGIC) memset(&cust[i], 0, sizeof(cust[i]));
    cust[i].fml[sizeof(cust[i].fml) - 1] = 0; cust[i].name[11] = 0; cust[i].unit[5] = 0;
  }
}
static void custSave() {
  uint8_t d = custDirty;
  if (!d) return;
  custDirty = 0;
  for (int i = 0; i < DN_NCUST; i++) if (d & (1 << i)) {
    char k[6] = {'c', 'u', 's', (char)('0' + i), 0};
    DidiNowCustSet c;
    portENTER_CRITICAL(&custMux); c = cust[i]; portEXIT_CRITICAL(&custMux);
    prefs.putBytes(k, &c, sizeof(c));
    Serial.printf("[cust] slot %d %s '%s' hdr %03X fml '%s'\n", i, c.on ? "ON" : "off", c.name, c.hdr, c.fml);
  }
}
// ---- ปรับค่า gain / offset (จำไว้ในตัว)
static DidiNowCal cal = {};
static volatile bool calDirty = false;
static void calLoad() {
  if (prefs.getBytes("cal", &cal, sizeof(cal)) != sizeof(cal) || cal.magic != DIDINOW_MAGIC) {
    memset(&cal, 0, sizeof(cal)); cal.magic = DIDINOW_MAGIC; dnCalDefault(cal);
  }
  for (int i = 0; i < DN_NCAL; i++) {   // กันค่าเพี้ยน
    if (!(cal.g[i] > 0.2f && cal.g[i] < 5.f)) cal.g[i] = 1;
    if (!(fabsf(cal.o[i]) < 500.f)) cal.o[i] = 0;
  }
  cal.type = DN_CAL; cal.set = 0;
}
static inline float calc(int i, float v) {
  if (v <= -998.f) return v;
  return v * cal.g[i] + cal.o[i];
}
// ---- โหมดวัดแรงม้า: ถามแค่รอบ/ความเร็ว/TPS ให้ถี่ที่สุด (แอปต่ออายุทุก 1 วิ)
static volatile uint32_t dynoUntil = 0;
static inline bool dynoOn() { return dynoUntil && (int32_t)(dynoUntil - millis()) > 0; }
// ---- อัปเดตเฟิร์มแวร์ทาง BLE (แอปส่งไฟล์ .bin มาทีละ 160 ไบต์ แบบรอตอบรับ)
static volatile bool otaOn = false, otaStNew = false, otaReboot = false;
static volatile uint32_t otaLast = 0, otaGot = 0;   // otaGot = ไบต์ที่รับแล้ว (Update.progress() นับเฉพาะที่เขียนลงแฟลชครบ 4 KB)
static DidiNowOtaSt otaSt = {};
static void otaStatus(uint8_t st, uint8_t err, const char* msg) {
  otaSt.magic = DIDINOW_MAGIC; otaSt.type = DN_OTAST; otaSt.state = st; otaSt.err = err;
  otaSt.off = Update.progress();
  otaSt.size = st == DN_OTAS_IDLE ? ESP.getFreeSketchSpace() : Update.size();
  strlcpy(otaSt.msg, msg, sizeof(otaSt.msg));
  otaStNew = true;   // ส่งจาก loop (ไม่ส่งจากใน callback ของ BLE)
}
static void otaFail(const char* why) {
  char m[40];
  snprintf(m, sizeof(m), "%s (%u)", why, (unsigned)Update.getError());
  uint8_t e = Update.getError();
  Update.abort();
  otaOn = false;
  otaStatus(DN_OTAS_ERR, e ? e : 99, m);
  Serial.printf("[ota] FAILED: %s\n", m);
}
static void otaCmd(const DidiNowOta* o, int n) {
  otaLast = millis();
  switch (o->op) {
    case DN_OTA_INFO:   // ระหว่างรับไฟล์: บอกว่ารับไปแล้วกี่ไบต์ (แอปส่งต่อจากตรงนั้นได้ ถ้าสัญญาณสะดุด)
      if (!otaOn) otaStatus(DN_OTAS_IDLE, 0, FW_VER);
      else { otaStatus(DN_OTAS_RECV, 0, "receiving"); otaSt.off = otaGot; }
      break;
    case DN_OTA_BEGIN: {
      if (otaOn) Update.abort();
      uint32_t size = o->off;
      if (!size || size > ESP.getFreeSketchSpace()) { otaOn = false; otaStatus(DN_OTAS_ERR, 98, "file too big"); break; }
      if (!Update.begin(size, U_FLASH)) { otaFail("begin"); break; }
      if (o->len == 32) { char md5[33]; memcpy(md5, o->data, 32); md5[32] = 0; Update.setMD5(md5); }
      otaOn = true;
      otaGot = 0;
      Serial.printf("[ota] begin %u bytes\n", (unsigned)size);
      otaStatus(DN_OTAS_RECV, 0, "receiving");
      break;
    }
    case DN_OTA_DATA: {
      if (!otaOn) break;
      // ตำแหน่งต้องต่อจากที่รับแล้ว (แอปรุ่นแรกส่งค่าแบบ Update.progress() — รับได้ทั้งสองแบบ)
      if (o->off != otaGot && o->off != Update.progress()) { otaFail("offset"); break; }
      if (o->len > DN_OTA_CHUNK || n < (int)offsetof(DidiNowOta, data) + o->len) { otaFail("short"); break; }
      if (Update.write((uint8_t*)o->data, o->len) != o->len) { otaFail("write"); break; }
      otaGot += o->len;
      if ((Update.progress() & 0x3FFF) < o->len) otaStatus(DN_OTAS_RECV, 0, "receiving");   // ทุก 16 KB
      break;
    }
    case DN_OTA_DATA2: {   // แอปรุ่นใหม่: ตำแหน่งเป็นไบต์จริง · ส่งซ้ำ (ตอบรับหาย) = รับไว้เฉย ๆ ไม่เขียนซ้ำ
      if (!otaOn) break;
      if (o->off + o->len <= otaGot) break;
      if (o->off != otaGot) { otaFail("offset"); break; }
      if (o->len > DN_OTA_CHUNK || n < (int)offsetof(DidiNowOta, data) + o->len) { otaFail("short"); break; }
      if (Update.write((uint8_t*)o->data, o->len) != o->len) { otaFail("write"); break; }
      otaGot += o->len;
      if ((otaGot & 0x3FFF) < o->len) { otaStatus(DN_OTAS_RECV, 0, "receiving"); otaSt.off = otaGot; }
      break;
    }
    case DN_OTA_END:
      if (!otaOn) break;
      if (!Update.end(false)) { otaFail(Update.getError() == UPDATE_ERROR_MD5 ? "MD5 mismatch" : "verify"); break; }
      otaOn = false;
      otaStatus(DN_OTAS_DONE, 0, "OK - rebooting");
      Serial.println("[ota] done -> reboot");
      otaReboot = true;
      break;
    case DN_OTA_ABORT:
      if (otaOn) { Update.abort(); otaOn = false; }
      otaStatus(DN_OTAS_IDLE, 0, FW_VER);
      break;
  }
}
// คำสั่งจากจอ/แอป (มาทาง BLE หรือ ESP-NOW)
static void gotCmd(const uint8_t* d, int n) {
  if (n < (int)sizeof(DidiNowCmd)) return;
  const DidiNowCmd* c = (const DidiNowCmd*)d;
  if (c->magic != DIDINOW_MAGIC) return;
  if (c->type == DN_WIFI) { c3wifi::command(d, n); return; }
  if (c->type == DN_GWCMD && c->cmd == DN_CMD_DYNO) { dynoUntil = millis() + 2500; return; }
  if (c->type == DN_GWCMD) { pendId = c->id; pendCmd = c->cmd; return; }
  if (c->type == DN_OTA && n >= (int)offsetof(DidiNowOta, data)) { otaCmd((const DidiNowOta*)d, n); return; }
  if (c->type == DN_RAWREQ && n >= (int)sizeof(DidiNowRaw) && !rawPending) {
    memcpy(&pendRaw, d, sizeof(pendRaw));
    rawPending = true;
  }
  if (c->type == DN_CAL && n >= (int)sizeof(DidiNowCal) && ((const DidiNowCal*)d)->set == 1) {
    const DidiNowCal* k = (const DidiNowCal*)d;
    bool ch = false;
    for (int i = 0; i < DN_NCAL; i++) {
      float g = (k->g[i] > 0.2f && k->g[i] < 5.f) ? k->g[i] : 1, o = fabsf(k->o[i]) < 500.f ? k->o[i] : 0;
      if (g != cal.g[i] || o != cal.o[i]) { cal.g[i] = g; cal.o[i] = o; ch = true; }
    }
    if (ch) { cal.ver++; calDirty = true; }
    return;
  }
  if (c->type == DN_CUSTSET && n >= (int)sizeof(DidiNowCustSet)) {
    const DidiNowCustSet* cs = (const DidiNowCustSet*)d;
    if (cs->slot >= DN_NCUST) return;
    portENTER_CRITICAL(&custMux);
    bool same = !memcmp(&cust[cs->slot], cs, sizeof(DidiNowCustSet));
    if (!same) {
      memcpy(&cust[cs->slot], cs, sizeof(DidiNowCustSet));
      cust[cs->slot].fml[sizeof(cs->fml) - 1] = 0; cust[cs->slot].name[11] = 0; cust[cs->slot].unit[5] = 0;
      custT[cs->slot] = 0;
      custBad[cs->slot] = 0;
      custNext[cs->slot] = 0;
      custDirty |= 1 << cs->slot;
    }
    portEXIT_CRITICAL(&custMux);
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
  // ค่าที่ปรับแล้ว (gain / offset ตั้งจากแอปหรือจอ S3)
  m.rpm = fmaxf(0, calc(DC_RPM, R.rpm)); m.spd = fmaxf(0, calc(DC_SPD, R.spd));
  m.ect = calc(DC_ECT, R.ect); m.iat = calc(DC_IAT, R.iat); m.volt = calc(DC_VOLT, R.volt);
  m.load = calc(DC_LOAD, R.load); m.tps = fminf(100, fmaxf(0, calc(DC_TPS, R.tps)));
  m.map = calc(DC_MAP, R.map); m.maf = fmaxf(0, calc(DC_MAF, R.maf)); m.fuel = R.fuel; m.stft = R.stft;
  m.afr = R.afrMode == 2 ? R.afr : calc(DC_AFR, R.afr);
  m.timing = calc(DC_TIMING, R.timing); m.oil = calc(DC_OIL, R.oil);
  tx(&m, sizeof(m));
}
// ค่า Custom ทุกช่อง (4 ครั้ง/วินาที) + ชื่อ/หน่วยทีละช่อง + คำตอบดิบที่ได้ใหม่
static void sendCust(uint32_t now) {
  static uint32_t tv = 0, ti = 0;
  static int si = 0;
  if (now - tv >= 250) {
    tv = now;
    DidiNowCust m = {};
    m.magic = DIDINOW_MAGIC; m.type = DN_CUST;
    for (int i = 0; i < DN_NCUST; i++) {
      if (!cust[i].on) continue;
      m.on |= 1 << i;
      if (demo) { m.v[i] = (i == 4 ? 72 : 40 + i * 15) + sinf(now / 2000.f + i) * 3; m.have |= 1 << i; continue; }
      if (custT[i] && now - custT[i] < 5000) { m.v[i] = i == 4 ? calc(DC_CVT, custV[i]) : custV[i]; m.have |= 1 << i; }
    }
    if (m.on) tx(&m, sizeof(m));
  }
  if (now - ti >= 600) {
    ti = now;
    DidiNowCustInfo m = {};
    m.magic = DIDINOW_MAGIC; m.type = DN_CUSTINFO; m.slot = si;
    portENTER_CRITICAL(&custMux);
    m.on = cust[si].on; m.dec = cust[si].dec; m.lo = cust[si].lo; m.hi = cust[si].hi;
    memcpy(m.name, cust[si].name, sizeof(m.name)); memcpy(m.unit, cust[si].unit, sizeof(m.unit));
    portEXIT_CRITICAL(&custMux);
    tx(&m, sizeof(m));
    si = (si + 1) % DN_NCUST;
  }
  static uint32_t tc = 0;
  if (now - tc >= 2000 || calDirty) {   // ค่าปรับปัจจุบัน → ทุกจอ (เมนูของจอ/แอปโชว์ค่าตรงกัน)
    tc = now;
    if (calDirty) { calDirty = false; prefs.putBytes("cal", &cal, sizeof(cal)); Serial.printf("[cal] saved v%u\n", cal.ver); }
    DidiNowCal m = cal;
    m.magic = DIDINOW_MAGIC; m.type = DN_CAL; m.set = 0;
    tx(&m, sizeof(m));
  }
  uint8_t nr = custRawNew;
  for (int i = 0; i < DN_NCUST && nr; i++) if (nr & (1 << i)) { custRawNew &= ~(1 << i); tx(&custRaw[i], sizeof(custRaw[i])); break; }
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
// header จากแอป (16 บิต): 0x000-0x7FF = CAN 11 บิต · 0xF0xx = Honda/ISO 29 บิต 18DA xx F1 (xx = กล่อง เช่น 1E เกียร์, 10 เครื่อง)
static uint32_t hdrOf(uint16_t h) { return (h & 0xFF00) == 0xF000 ? (0x18DA00F1UL | ((uint32_t)(h & 0xFF) << 8)) : h; }
// ถาม Custom PID ช่องถัดไปที่ถึงคิว แล้วคิดสูตร
// ช่องที่ไม่มีใครตอบ / ตอบปฏิเสธ (7F) → เว้นระยะถามนานขึ้นเรื่อย ๆ (สูงสุด 10 วิ) ไม่ให้ค่าหลัก (รอบ/ความเร็ว) ช้าลง
static void pollCust() {
  static int ci = 0;
  uint32_t now = millis();
  for (int g = 0; g < DN_NCUST; g++) {
    int i = ci;
    ci = (ci + 1) % DN_NCUST;
    DidiNowCustSet c;
    portENTER_CRITICAL(&custMux); c = cust[i]; portEXIT_CRITICAL(&custMux);
    if (!c.on || c.len < 1 || c.len > 7) { custBad[i] = 0; custNext[i] = 0; continue; }
    if (custT[i] == 0 && custBad[i] == 0) custNext[i] = 0;          // เพิ่งตั้งค่าใหม่ → ถามทันที
    if ((int32_t)(now - custNext[i]) < 0) continue;
    DidiNowCustRaw r = {};
    r.magic = DIDINOW_MAGIC; r.type = DN_CUSTRAW; r.slot = i;
    if (dnIsCan(proto)) {
      uint32_t to = c.hdr ? hdrOf(c.hdr) : canobd::defTx();
      r.n = canobd::requestTo(to, c.data, c.len, r.data, sizeof(r.data), 150);
    } else if (c.hdr && c.hdr != 0x7DF && c.hdr != 0x7E0) {
      r.n = -2;
    } else {
      r.n = ask(c.data, c.len, r.data, sizeof(r.data), 250, true);
    }
    if (r.n < -2) r.n = -1;
    bool ok = false;
    // ข้อมูล = หลัง SID+0x40 และ echo ของคำสั่ง (เหมือน parseUds ในแอป)
    if (r.n > c.len && r.data[0] == (uint8_t)(c.data[0] + 0x40) && !memcmp(r.data + 1, c.data + 1, c.len - 1)) {
      float v;
      if (custeval::eval(c.fml, r.data + c.len, r.n - c.len, &v)) { custV[i] = v; custT[i] = millis(); ok = true; }
    }
    if (ok) { custBad[i] = 0; custNext[i] = millis() + (i == 4 ? 900 : 400); }
    else {
      if (custBad[i] < 20) custBad[i]++;
      uint32_t wait = r.n == -2 ? 10000 : min<uint32_t>(10000, 600UL * custBad[i] * custBad[i]);
      custNext[i] = millis() + wait;
    }
    custRaw[i] = r;
    custRawNew |= 1 << i;
    return;
  }
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
  } else if (dnIsCan(proto)) {
    uint32_t to = q.hdr ? hdrOf(q.hdr) : canobd::defTx();
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
  else if (c3ble::pairOpen() && c3ble::pairCount() > 0) { uint32_t k = t % 1000; on = k < 80 || (k > 180 && k < 260); }   // กะพริบคู่ = โหมดจับคู่
  else if (carLink == DN_LINK_OK) on = (t % 1000) > 60;
  else on = (t / 120) % 2;
  digitalWrite(PIN_LED, on ? LOW : HIGH);
}
// ปุ่ม BOOT: กดค้าง 2-5 วิแล้วปล่อย = สลับโหมดจำลอง · ค้าง 6 วิ = เปิดโหมดจับคู่จอใหม่ 3 นาที
static void bootBtn() {
  static uint32_t down = 0;
  static bool done = false;
  if (digitalRead(PIN_BOOT) == LOW) {
    if (!down) down = millis();
    if (!done && millis() - down > 6000) {
      done = true;
      c3ble::openPairing(180);
      Serial.println("[pair] BOOT button -> pairing open 3 min");
    }
  } else {
    if (down && !done && millis() - down > 2000) {
      demo = !demo;
      prefs.putBool("demo", demo);
      if (demo) disconnect();
      Serial.printf("[mode] %s (BOOT button)\n", demo ? "DEMO" : "CAR");
    }
    down = 0; done = false;
  }
}

// ---------------------------------------------------------------- งานอ่านรถ (แยก task: ตอน init K-line บล็อกหลายวินาที ไม่ให้ ESP-NOW หยุดส่ง)
// ถามแล้วไม่ตอบติดกัน: CAN ที่ถามตรงกล่อง → เปลี่ยนเป็นถามทุกกล่องก่อน · ยังไม่ตอบ = หลุดจากรถ
static void lostCheck() {
#if USE_CAN
  if (dnIsCan(proto) && canobd::physical) {
    canobd::physical = false;
    fails = 0;
    Serial.println("[obd] no reply from ECU -> broadcast 7DF");
    setDiag("CAN: ECU stopped answering direct -> broadcast");
    return;
  }
#endif
  Serial.println("[obd] lost car");
  disconnect();
}
static void obdTask(void*) {
  static const uint8_t SLOW[] = {0x0D, 0x05, 0x0F, 0x04, 0x0B, 0x10, 0x2F, 0x06, 0x42, 0x0E, 0x5C};
  int slow = 0;
  uint32_t step = 0;
  for (;;) {
    if (otaOn) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }   // อัปเดตเฟิร์มแวร์อยู่: หยุดถามรถ ให้ CPU/แฟลชว่าง
    if (demo) { handleRaw(); vTaskDelay(pdMS_TO_TICKS(100)); continue; }
    if (carLink != DN_LINK_OK) {
      if (connect()) { carLink = DN_LINK_OK; fails = 0; lastOk = millis(); rtcProto = proto; Serial.printf("[obd] connected: %s\n", DN_P_NAME[proto]); }
      else {
        // ไม่เจอรถ: โชว์ผลฝั่ง CAN ค้างไว้ 3 วิ (ข้อความ K-line วิ่งผ่านเร็ว ไม่บังสาเหตุฝั่ง CAN)
        if (canDiag.length()) setDiag(canDiag.c_str());
        Serial.println("[obd] no car — retry");
        vTaskDelay(pdMS_TO_TICKS(3000));
      }
      continue;
    }
    if (pendCmd == DN_CMD_DTC_READ || pendCmd == DN_CMD_DTC_CLEAR) { handleCmd(); continue; }
    if (rawPending) { handleRaw(); continue; }
    if (dynoOn()) {                                     // วัดแรงม้า: รอบ → ความเร็ว → รอบ → ความเร็ว → TPS
      static uint8_t dk = 0;
      uint8_t p = (dk % 5 == 4) ? 0x11 : (dk % 2 ? 0x0D : 0x0C);
      dk++;
      pid(p);
      if (fails >= 8) lostCheck();
      continue;
    }
    static uint32_t lastCust = 0;
    if (millis() - lastCust > 400) { lastCust = millis(); pollCust(); }   // Custom PID ~2-3 ครั้ง/วินาที รวมทุกช่อง
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
    if (fails >= 8) lostCheck();
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
  custLoad();
  calLoad();
  canRx = prefs.getInt("canrx", PIN_CAN_RX);
  if (canRx != PIN_CAN_RX && canRx != 3) canRx = PIN_CAN_RX;
  kline::begin(PIN_K_RX, PIN_K_TX);
  if (USE_VBAT) analogSetPinAttenuation(PIN_VBAT, ADC_11db);   // ไม่วัดแบต → GPIO3 ว่าง (ใช้เป็นขา CAN RX ได้)
  vbat = readVbat();
  // ตื่นจากหลับ (ไม่มีสายวัดแบต): ยังไม่เปิด WiFi — ลองเรียกรถรอบเดียวก่อน ไม่ตอบ = หลับต่อทันที
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER && !vbat && !demo) {
    bool ok = false;
    if (rtcProto == DN_P_ISO9141 || rtcProto == DN_P_KWPSLOW) ok = tryK(false, true);
    else if (rtcProto == DN_P_KWPFAST) ok = tryK(true);
    else if (rtcProto == DN_P_CAN500 || rtcProto == DN_P_CAN500X) ok = tryCan(500000, DN_P_CAN500, DN_P_CAN500X);
    else if (rtcProto == DN_P_CAN250 || rtcProto == DN_P_CAN250X) ok = tryCan(250000, DN_P_CAN250, DN_P_CAN250X);
    else ok = connect();
    Serial.printf("[wake] car %s\n", ok ? "found" : "not found -> sleep");
    if (!ok && !usbHost()) deepSleep(WAKE_NOVBAT_S);
    if (ok) { carLink = DN_LINK_OK; fails = 0; lastOk = millis(); }
  }
#if USE_BLE
  c3ble::onCmd = gotCmd;
  c3ble::fwVer = FW_VER;
  bool ok = c3ble::begin(esp_reset_reason() == ESP_RST_POWERON);
  c3wifi::begin();
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
    if (carLink == DN_LINK_OK && dnIsK(proto)) Serial.printf("[obd] %u/s  P3 %u ms\n", rate, kline::p3);
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
  bool wantSleep = !demo && !engineOn && now - lastCar > (vbat && vbat < LOWBAT_V ? 30000UL : IDLE_SLEEP_S * 1000UL);
  if (wantSleep && usbHost()) {          // ต่อคอมอยู่ → ไม่หลับ (บอกใน Serial ทุก 30 วิ)
    static uint32_t tn = 0;
    if (now - tn > 30000) { tn = now; Serial.println("[sleep] skipped: USB connected to computer"); }
    wantSleep = false;
  }
  if (wantSleep) {
    char m[59];
    if (vbat) snprintf(m, sizeof(m), "sleep: engine off, batt %.1fV", vbat);
    else snprintf(m, sizeof(m), "sleep: no car (check every %ds)", WAKE_NOVBAT_S);
    setDiag(m);
    for (int i = 0; i < 3; i++) { sendInfo(); delay(30); }
    deepSleep(!vbat ? WAKE_NOVBAT_S : vbat < LOWBAT_V ? WAKE_LOWBAT_S : WAKE_S);
  }
  c3ble::tick();   // จับคู่จอ: ตอบผล / ตัดจอที่ไม่จับคู่ / โฆษณา
  c3wifi::pause = otaOn;   // อัปเดตเฟิร์มแวร์: ปิด WiFi ชั่วคราว (ให้ Bluetooth ใช้คลื่นเต็มที่ ส่งไฟล์ไม่สะดุด)
  c3wifi::tick();  // จอทาง WiFi: รับคำสั่ง / ประกาศตัว
  // อัปเดตเฟิร์มแวร์อยู่: หยุดส่งค่ารถ (ให้ BLE ว่างรับไฟล์) · ไม่หลับ · ไม่มีไฟล์มา 45 วิ = ยกเลิก
  if (otaStNew) { otaStNew = false; tx(&otaSt, sizeof(otaSt)); }
  if (otaReboot) { delay(400); tx(&otaSt, sizeof(otaSt)); delay(300); ESP.restart(); }
  if (otaOn) {
    lastCar = now;
    if (now - otaLast > 45000) otaFail("timeout");
    digitalWrite(PIN_LED, (now / 80) & 1);
    delay(2);
    return;
  }
  if (now - lastSend >= 33) { lastSend = now; sendData(); }   // ~30 ครั้ง/วินาที
  custSave();
  sendCust(now);
  static uint32_t lastInfo = 0;
  if (!demo && carLink != DN_LINK_OK && now - lastInfo >= 700) { lastInfo = now; sendInfo(); }   // ผลตรวจสายขึ้นจอ
  delay(2);
}
