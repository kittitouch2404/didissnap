/*
  ESP32 CYD (ESP32-2432S028, จอ 2.8" 320x240) + ELM327 Bluetooth 3.0 (Classic)
  แสดง: รอบเครื่อง / ความเร็ว / อุณหภูมิน้ำ / แรงดันแบต
  - หน้า 1-4 วาดด้วยกราฟิกทั้งหมด (ไม่ใช้รูปพื้นหลัง): 1 กราฟแท่งรอบทรงลาด, 2 ช่องสีเหลือง, 3 แท่งรอบโค้งแบบจอรถแข่ง, 4 เกจครึ่งวง
  - หน้า 5 หน้าปัดกลม, หน้า 6 ไมล์แบบแถบ (ปัดซ้าย/ขวาเพื่อสลับหน้า)
  - หน้า 12 = Jazz #66 วิ่งทางด่วนกลางวัน (แบบหน้า 10), หน้า 13 = ภาพนิ่ง J's Racing (แบบหน้า 9)
  - ตัวเลขวาดผ่านสไปรท์เล็ก ๆ วาดใหม่เฉพาะส่วนที่เปลี่ยน = ไม่กะพริบ
  - ค่าที่แสดง: รอบ / ความเร็ว / เกียร์ / น้ำ / IAT / บูสต์ / แอร์โฟลว์ / โหลด / TPS / น้ำมัน / แบต / ทริป
  - ปัดขึ้นเพื่อเปิดเมนู (รายการ 7 ข้อ): รอบสูงสุด / อุณหภูมิเตือน / จอ / Bluetooth / DTC / เกียร์ / ค่าสูงสุด

  Library : LovyanGFX (ติดตั้งจาก Library Manager)
  Board   : ESP32 Dev Module
  Partition Scheme : "Custom" (partitions.csv ในโฟลเดอร์นี้ ~3.9 MB) หรือ "Huge APP (3MB)" -- 13 หน้า ~2.45 MB เกิน 2MB
*/

#include <Arduino.h>
// ---------- รับค่ารถจากไหน ----------
// 1 = รับจากบอร์ด S3 (Didissnap_S3) ทาง ESP-NOW — S3 ต่อ OBD (BLE) แล้วส่งค่ามาให้ ไม่ใช้ Bluetooth บนจอนี้
// 0 = ต่อ ELM327 Bluetooth 3.0 เองแบบเดิม
#define LINK_ESPNOW 1
#if LINK_ESPNOW
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "didinow.h"
#else
#include <BluetoothSerial.h>
#endif
#include <Preferences.h>
#include "esp_bt.h"
#include "esp_system.h"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>


#if __has_include("face_bg.h") && __has_include("faces.h")
#include "face_bg.h"        // หน้า 1: ฉากหลัง+ตัวละคร face_bg_jpg 320x240 สร้างด้วย make_face_page.py
#include "faces.h"          // แพตช์ใบหน้า N_FACES แบบ (เปลี่ยนตามช่วงรอบ): FACE_X/Y/W/H, FACE_JPG[], FACE_LEN[]
#define HAS_FACE 1
#define HAS_ANIME 1         // หน้า 1 ใช้โค้ดหน้า "ภาพพื้นหลัง + ตัวเลขลอย" (PG_H)
#else
#define HAS_FACE 0
#define HAS_ANIME 0
#warning "face_bg.h / faces.h not found: page 1 (character page) is disabled"
#endif
#define HAS_CARPAGE 0
#define HAS_PHOTO2 0
#define HAS_CARPAGE2 0
#define HAS_CARPAGE3 0
#define HAS_ANIME2 0

#if __has_include("dance.h")
#include "dance.h"          // หน้า 2: GIF นักเต้นเต็มจอ DANCE_N เฟรม (DANCE_TOP[] ส่วนบน + DANCE_BOT[] ส่วนล่าง) สร้างด้วย tools/make_dance_page.py
#define HAS_DANCE 1
#else
#define HAS_DANCE 0         // ไม่มีไฟล์ = ไม่มีหน้า 2
#endif

#if __has_include("logo.h")
#include "logo.h"           // ภาพโลโก้หลังหน้าเปิดเครื่อง: logo_jpg / logo_jpg_len (JPEG 320x240) สร้างด้วย tools/make_logo.py
#define HAS_LOGO 1
#else
#define HAS_LOGO 0
#endif


// ---------- ค่าเริ่มต้น ----------
#define OBD_NAME       "OBDII"   // ใช้เมื่อยังไม่ได้เลือกอุปกรณ์ในเมนู Bluetooth
#define DEF_PIN        "1234"
#define DEF_MAX_RPM    7500
#define DEF_TEMP_ALERT 100
#define MAX_RPM_LIMIT  10000     // ตั้งรอบสูงสุดในเมนูได้ถึงค่านี้
#define MAX_SPEED      300       // ความเร็วสูงสุดที่แสดง (km/h)

// ---------- ทิศทางจอ (ถ้าจอกลับหัว/กลับด้าน แก้ตรงนี้) ----------
// ต้องเป็นแนวนอนเท่านั้น: 1 และ 3 = หมุนปกติ (ต่างกัน 180 องศา)
//                         5 และ 7 = กลับซ้าย-ขวา (mirror) ของ 1 และ 3
// ลำดับที่แนะนำให้ลอง: 3 -> 1 -> 7 -> 5 (เปลี่ยนแล้วบอร์ดจะให้คาลิเบรตทัชใหม่ให้เอง)
#define LCD_ROTATION   3
// CYD รุ่นที่มีพอร์ต USB 2 ช่อง (USB-C + micro-USB) มักใช้จอ ST7789 แทน ILI9341
// ถ้าลองเปลี่ยนทิศแล้วภาพยังกลับด้านหรือสีเพี้ยน ให้ตั้งเป็น 1
#define PANEL_ST7789   1
#define LCD_INVERT     0              // กลับสี: ถ้าสีเหมือนฟิล์มเนกาทีฟ (เช่นท้องฟ้ากลางคืนกลายเป็นสีอ่อน) ให้สลับ 0/1
#define LCD_RGB_ORDER  0              // ถ้าแดง<->น้ำเงินสลับกัน ให้เปลี่ยนเป็น 1
#define TOUCH_FLIP_FIX 0         // ถ้ากด FLIP 180 แล้วแตะจอผิดฝั่ง (ทัชกลับด้าน) ให้เปลี่ยนเป็น 1
#define GAUGE_CARBON   0         // หน้า 5 (หน้าปัดกลม): 0 = พื้นหลังดำสนิท, 1 = ลายคาร์บอน
#define SPLASH_MS      5000      // ความยาวหน้าปัดเข็มกวาดตอนเปิดเครื่อง (4000-6000)
#define LOGO_FADE_MS   1800      // หน้าโลโก้ (logo.h): เวลาค่อย ๆ สว่างขึ้น
#define LOGO_HOLD_MS   2000      // หน้าโลโก้: ค้างไว้ที่ความสว่างเต็ม (แตะจอเพื่อข้าม)
#define LOGO_OUT_MS    500       // หน้าโลโก้: ค่อย ๆ มืดลงก่อนเข้าหน้าหลัก
#define CAR_FPS        0         // หน้า 10 รถวิ่ง: เฟรมต่อวินาทีของภาพเคลื่อนไหว (0 = ตามไฟล์ GIF)
#define FRAME_MS       16        // เวลาต่อเฟรมขั้นต่ำ (16 = ~60 fps, เดิม 25 = 40 fps)
#define LDR_PIN        34        // เซ็นเซอร์แสงบน CYD
#define LDR_DARK       3200      // ค่า ADC ตอนมืด  (ถ้าบอร์ดคุณกลับด้าน ให้สลับสองค่านี้)
#define LDR_BRIGHT     400       // ค่า ADC ตอนสว่าง

// ---------- จอ CYD ----------
class LGFX : public lgfx::LGFX_Device {
#if PANEL_ST7789
  lgfx::Panel_ST7789  _panel;
#else
  lgfx::Panel_ILI9341 _panel;
#endif
  lgfx::Bus_SPI       _bus;
  lgfx::Light_PWM     _light;
  lgfx::Touch_XPT2046 _touch;
public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = HSPI_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read  = 16000000;
      cfg.pin_sclk = 14;
      cfg.pin_mosi = 13;
      cfg.pin_miso = 12;
      cfg.pin_dc   = 2;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = 15;
      cfg.pin_rst = -1;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.readable = false;
      cfg.invert = (LCD_INVERT != 0);
      cfg.rgb_order = (LCD_RGB_ORDER != 0);
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = 21;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    {
      auto cfg = _touch.config();
      cfg.x_min = 300;  cfg.x_max = 3900;
      cfg.y_min = 200;  cfg.y_max = 3700;
      cfg.pin_int = 36;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.spi_host = VSPI_HOST;
      cfg.freq = 1000000;
      cfg.pin_sclk = 25;
      cfg.pin_mosi = 32;
      cfg.pin_miso = 39;
      cfg.pin_cs   = 33;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};

LGFX lcd;
#if LINK_ESPNOW
// ไม่ใช้ Bluetooth: ตัวแทนที่ไม่ทำอะไร ให้โค้ดเดิมที่อ้างถึง SerialBT คอมไพล์ได้
struct BtStub {
  bool begin(const char*, bool) { return false; }
  int available() { return 0; }
  int read() { return -1; }
  size_t print(const char*) { return 0; }
  size_t print(const String&) { return 0; }
  bool connected() { return false; }
  bool connect(uint8_t*) { return false; }
  bool connect(const char*) { return false; }
  void disconnect() {}
  void setPin(const char*, size_t = 0) {}
};
BtStub SerialBT;
#else
BluetoothSerial SerialBT;
#endif
Preferences prefs;

// ---------- กลับจอ 180 องศา (MENU -> DISPLAY -> FLIP 180) ----------
bool flip180 = false;
int  curRotation() { return flip180 ? (LCD_ROTATION ^ 2) : LCD_ROTATION; }   // 3<->1, 7<->5
bool getTouchXY(int32_t *x, int32_t *y) {
  if (!lcd.getTouch(x, y)) return false;
  if (flip180 && TOUCH_FLIP_FIX) { *x = 319 - *x; *y = 239 - *y; }
  return true;
}
void applyFlip() { lcd.setRotation(curRotation()); }

// ---------- ค่าที่ตั้งได้ (เก็บใน NVS) ----------
int    maxRpm    = DEF_MAX_RPM;
int    tempAlert = DEF_TEMP_ALERT;
String btName = "";                 // อุปกรณ์ที่เลือกไว้ (ว่าง = ค้นหาด้วยชื่อ OBD_NAME)
String btMac  = "";
char   btPin[8] = DEF_PIN;
volatile int page = 0;               // หน้าที่แสดงอยู่ (ปัดซ้าย/ขวาเพื่อเปลี่ยน)
const int PG_H = 0, PG_DN = 1;                         // หน้า 1 ตัวละคร / หน้า 2 GIF นักเต้น
const int PG_A = 100;                               // (เลิกใช้: หน้ากราฟิกเดิมหน้า 1 ถูกแทนด้วย PG_H)
const int PG_B = 2, PG_C = 3, PG_D = 4;             // หน้า 3-5
const int PAGE_GAUGE = 5, PAGE_DASH = 6;            // หน้า 6 หน้าปัดกลม, หน้า 7 ไมล์แบบแถบ
const int PG_E = 7;                                 // หน้า 7 จอแข่งสีส้ม
const int PG_F = 8;                                 // หน้า 8 เกจโค้ง + ค่าสูงสุด/ต่ำสุด
const int PG_I = 100;                                 // หน้า 10 รถวิ่ง (ภาพเคลื่อนไหวจาก GIF)
const int PG_J = 101;                                // หน้า 11 ภาพนิ่ง + รอบ/ความร้อนด้านบน (แบบหน้า 2 เดิม)
const int PG_L = 102;                                // หน้า 12 รถวิ่งที่ 2 (car2.h / wheels2.h) เลย์เอาต์เดียวกับหน้า 10
const int PG_K = 103;                                // หน้า 13 รถวิ่งที่ 3 (car3.h / wheels3.h) เลย์เอาต์เดียวกับหน้า 10 / 12
const int N_PAGES = 9;                              // หน้า 1 ตัวละคร + หน้า 2-8 (ลบหน้า 9-13 แล้ว)
inline bool isFPage(int p) { return p == PG_I || p == PG_J || p == PG_L || p == PG_K; }   // หน้าที่ใช้เลย์เอาต์หน้า 10 (รอบ/ความร้อนด้านบน)
inline bool isHPage(int p) { return p == PG_H; }
inline bool isPoolPage(int p) { return p == PG_H || p == PG_DN; }   // หน้าที่ใช้ก้อนแรมกลางทั้งก้อน (ไม่ใช้สไปรท์ตัวเลข)   // หน้าที่ใช้เลย์เอาต์หน้า 9 (ภาพพื้นหลัง + ตัวเลขลอย)
bool pageAvail(int p) { return p >= 0 && p < N_PAGES && !(p == PG_DN && !HAS_DANCE) && !(p == PG_H && !HAS_ANIME) && !(p == PG_I && !HAS_CARPAGE) && !(p == PG_J && !HAS_PHOTO2) && !(p == PG_K && !HAS_CARPAGE3) && !(p == PG_L && !HAS_CARPAGE2); }
int nextPage(int p) { do { p = (p + 1) % N_PAGES; } while (!pageAvail(p)); return p; }

// ---- โหมดเดโม: จำลองค่าขับรถเองทั้งหมด (เริ่ม/หยุดที่ปุ่ม DEMO ในเมนู) ----
volatile bool demoMode = false;
int    briPct  = 80;                // ความสว่าง 10-100 %
bool   briAuto = false;             // true = ปรับอัตโนมัติด้วยเซ็นเซอร์แสง

// ---------- ค่าที่อ่านได้ (OBD task เขียน, UI อ่าน) ----------
volatile int   rpm = 0, speedKmh = 0, coolant = 0, iat = 0;

// ---- ค่าที่แสดงในช่องล่างของหน้าหลัก: แตะที่ช่องเพื่อสลับไปค่าถัดไป ----
enum { M_COOLANT, M_IAT, M_BATT, M_LOAD, M_TPS, M_MAP, M_MAF, M_FUEL, M_STFT, N_METRICS };
const char *const METRIC_LABEL[N_METRICS] = {"COOLANT", "IAT", "BATT", "LOAD", "TPS", "MAP", "MAF", "FUEL", "STFT"};
const char *const METRIC_UNIT[N_METRICS]  = {"deg C", "deg C", "volt", "%", "%", "kPa", "g/s", "%", "%"};
volatile float metricVal[N_METRICS];     // ค่าล่าสุดของแต่ละตัว
volatile bool  metricHave[N_METRICS];    // เคยอ่านได้แล้วหรือยัง (ยังไม่ได้ = แสดง --)
volatile int   urgentMetric = -1;        // สั่งให้ OBD task อ่านค่านี้ทันที (ตอนเพิ่งสลับ)
uint32_t pidMask[3];                     // PID ที่รถรองรับ (จาก 0100 / 0120 / 0140)
bool     pidMaskKnown[3] = {false, false, false};
volatile bool  obdOk = false;
const char * volatile statusMsg = LINK_ESPNOW ? "LINK..." : "BT...";
void setStatus(const char *m) { statusMsg = m; }
const char *curStatus() { return demoMode ? "DEMO" : (const char *)statusMsg; }   // เดโม = แสดง DEMO แทนสถานะ OBD

// ---------- ควบคุม OBD task ตอนเข้าเมนู Bluetooth ----------
volatile bool obdPause = false, obdIdle = false, btReconnect = false;

// ---------- สี ----------
uint16_t C_BG, C_TXT, C_DIM, C_LABEL, C_OK, C_WARN, C_RED, C_PINK, C_PANEL, C_SKY;

// ---------- เลย์เอาต์ (พิกัดบนจอ 320x240) ----------
const int MENU_X = 250, MENU_Y = 0, MENU_W = 70, MENU_H = 34;


// ---------- ความสว่างจอ ----------
float autoBri = 70;
void applyBrightness(int pct) {
  lcd.setBrightness(pct <= 0 ? 0 : (uint8_t)constrain(pct * 255 / 100, 8, 255));
}
void updateBrightness() {                      // โหมด AUTO: มืดหรี่, สว่างเพิ่ม
  static uint32_t t = 0;
  if (!briAuto || millis() - t < 150) return;
  t = millis();
  int v = analogRead(LDR_PIN);
  float f = constrain((float)(LDR_DARK - v) / (float)(LDR_DARK - LDR_BRIGHT), 0.0f, 1.0f);
  autoBri += ((15.0f + 85.0f * f) - autoBri) * 0.12f;
  applyBrightness((int)autoBri);
}

void drawDots() {                                        // จุดบอกหน้า
  int n = 0, act = 0;
  for (int p = 0; p < N_PAGES; p++) if (pageAvail(p)) { if (p == page) act = n; n++; }
  for (int i = 0; i < n; i++) {
    int x = 160 + (int)((i - (n - 1) / 2.0f) * 16);
    lcd.fillCircle(x, 233, 4, TFT_BLACK);
    lcd.fillCircle(x, 233, 3, i == act ? C_TXT : lcd.color565(110, 118, 150));
  }
}


// ---------- Bluetooth / ELM327 ----------
// ส่งคำสั่งแล้วรอ '>' เก็บคำตอบ (ไม่มีช่องว่าง/ขึ้นบรรทัดใหม่) ลงบัฟเฟอร์ ไม่ใช้ String เพื่อไม่ให้แรมแตกเป็นเสี่ยง ๆ เวลาใช้งานนาน
int elmBuf(const char *cmd, char *out, size_t cap, uint32_t timeoutMs) {
  while (SerialBT.available()) SerialBT.read();
  SerialBT.print(cmd);
  SerialBT.print("\r");
  size_t n = 0;
  uint32_t t = millis();
  while (millis() - t < timeoutMs) {
    while (SerialBT.available()) {
      char c = SerialBT.read();
      if (c == '>') { out[n] = 0; return (int)n; }
      if (c != '\r' && c != '\n' && c != ' ' && n + 1 < cap) out[n++] = c;
    }
    delay(1);
  }
  out[n] = 0;
  return (int)n;
}

String elm(const char *cmd, uint32_t timeoutMs = 1000) {      // ใช้เฉพาะตอนตั้งค่า ELM327 (ไม่ใช้ในรอบอ่านค่าสด)
  char b[160];
  elmBuf(cmd, b, sizeof(b), timeoutMs);
  return String(b);
}

bool readPid(const char *cmd, const char *head, int nBytes, int *out) {
  char r[96];
  if (elmBuf(cmd, r, sizeof(r), 1200) <= 0) return false;
  char *p = strstr(r, head);
  if (!p) return false;
  p += 4;
  if ((int)strlen(p) < 2 * nBytes) return false;
  for (int k = 0; k < nBytes; k++) {
    char h[3] = {p[2 * k], p[2 * k + 1], 0};
    out[k] = (int)strtol(h, NULL, 16);
  }
  return true;
}

// อ่านรายการ PID ที่รถรองรับ (4 ไบต์ = 32 บิต เริ่มจากบิตสูงสุด = PID แรกของกลุ่ม)
bool readMask(const char *cmd, const char *head, uint32_t *mask) {
  char r[48];
  if (elmBuf(cmd, r, sizeof(r), 1500) <= 0) return false;
  char *p = strstr(r, head);
  if (!p) return false;
  p += 4;
  if (strlen(p) < 8) return false;
  char h[9];
  memcpy(h, p, 8); h[8] = 0;
  for (int i = 0; i < 8; i++) if (!isxdigit((unsigned char)h[i])) return false;
  *mask = strtoul(h, NULL, 16);
  return true;
}

void detectPids() {
  pidMaskKnown[0] = readMask("0100", "4100", &pidMask[0]);
  pidMaskKnown[1] = pidMaskKnown[0] && (pidMask[0] & 1) && readMask("0120", "4120", &pidMask[1]);
  pidMaskKnown[2] = pidMaskKnown[1] && (pidMask[1] & 1) && readMask("0140", "4140", &pidMask[2]);
  Serial.printf("PID support: %08X %08X %08X (known %d%d%d)\n", (unsigned)pidMask[0], (unsigned)pidMask[1], (unsigned)pidMask[2],
                pidMaskKnown[0], pidMaskKnown[1], pidMaskKnown[2]);
}

bool pidSupported(int pid) {
  if (pid < 1 || pid > 0x60) return true;
  int g = (pid - 1) / 32, bit = 31 - ((pid - 1) % 32);
  if (!pidMaskKnown[g]) return true;                     // ยังไม่รู้ = ให้ลองอ่านดู
  return (pidMask[g] >> bit) & 1;
}

bool metricAvailable(int m) {
  switch (m) {
    case M_LOAD: return pidSupported(0x04);
    case M_TPS:  return pidSupported(0x11);
    case M_MAP:  return pidSupported(0x0B);
    case M_MAF:  return pidSupported(0x10);
    case M_FUEL: return pidSupported(0x2F);
    case M_STFT: return pidSupported(0x06);
    default:     return true;                            // น้ำ/IAT/แบต ใช้ได้ทุกคัน
  }
}

// ---- อ่านรอบ + ความเร็ว + TPS ในคำสั่งเดียว (OBD-II CAN ขอได้หลาย PID ต่อครั้ง รถตอบกลับเป็นหลายเฟรม ELM327 ประกอบให้) ----
int  fastMode = 0;            // 0 = อ่านทีละ PID (เดิม), 1 = หลาย PID + บอก ELM ว่าคาดว่าตอบ 1 ชุด (เร็วสุด), 2 = หลาย PID ธรรมดา
bool fastTps  = false;        // รถรองรับ TPS (PID 11) -> ขอรวมไปด้วย
static int hx2(const char *s) {
  if (!isxdigit((unsigned char)s[0]) || !isxdigit((unsigned char)s[1])) return -1;
  char h[3] = {s[0], s[1], 0};
  return (int)strtol(h, NULL, 16);
}
bool readFast(int *rp, int *sp, float *tp) {
  char r[110], c[110];
  const char *cmd = fastTps ? (fastMode == 1 ? "010C0D111" : "010C0D11") : (fastMode == 1 ? "010C0D1" : "010C0D");
  if (elmBuf(cmd, r, sizeof(r), 700) <= 0) return false;
  int n = 0;
  for (int i = 0; r[i] && n < (int)sizeof(c) - 1; i++) {             // ตัดเลขนำหน้าบรรทัดของคำตอบหลายเฟรม ("0:" "1:") และเลขความยาว
    if (r[i] == ':') { if (n > 0) n--; continue; }
    c[n++] = r[i];
  }
  c[n] = 0;
  char *p = strstr(c, "41");
  if (!p) return false;
  bool gotRpm = false;
  int i = 0, L = strlen(p);
  while (i + 2 <= L) {
    if (p[i] == '4' && p[i + 1] == '1') { i += 2; continue; }       // ECU ตอบแยกเป็นชุด ๆ: ข้ามหัว 41
    int pid = hx2(p + i); i += 2;
    if (pid == 0x0C && i + 4 <= L) { int a = hx2(p + i), b = hx2(p + i + 2); if (a < 0 || b < 0) break; *rp = (a * 256 + b) / 4; gotRpm = true; i += 4; }
    else if (pid == 0x0D && i + 2 <= L) { int a = hx2(p + i); if (a < 0) break; *sp = a; i += 2; }
    else if (pid == 0x11 && i + 2 <= L) { int a = hx2(p + i); if (a < 0) break; *tp = a * 100.0f / 255.0f; i += 2; }
    else break;
  }
  return gotRpm;
}

bool pollMetric(int m) {
  int b[2];
  char r[24];
  switch (m) {
    case M_COOLANT:
      if (readPid("0105", "4105", 1, b)) { coolant = b[0] - 40; metricVal[m] = coolant; metricHave[m] = true; return true; }
      break;
    case M_IAT:
      if (readPid("010F", "410F", 1, b)) { iat = b[0] - 40; metricVal[m] = iat; metricHave[m] = true; return true; }
      break;
    case M_BATT:                                         // ATRV ตอบเช่น "12.6V"
      if (elmBuf("ATRV", r, sizeof(r), 800) > 0) {
        float v = atof(r);
        if (v > 5.0f && v < 20.0f) { metricVal[m] = v; metricHave[m] = true; return true; }
      }
      break;
    case M_LOAD:                                         // 0104: A*100/255 %
      if (readPid("0104", "4104", 1, b)) { metricVal[m] = b[0] * 100.0f / 255.0f; metricHave[m] = true; return true; }
      break;
    case M_TPS:                                          // 0111: A*100/255 %
      if (readPid("0111", "4111", 1, b)) { metricVal[m] = b[0] * 100.0f / 255.0f; metricHave[m] = true; return true; }
      break;
    case M_MAP:                                          // 010B: A kPa (สัมบูรณ์ ~101 ที่ความดันบรรยากาศ)
      if (readPid("010B", "410B", 1, b)) { metricVal[m] = b[0]; metricHave[m] = true; return true; }
      break;
    case M_MAF:                                          // 0110: (256A+B)/100 g/s
      if (readPid("0110", "4110", 2, b)) { metricVal[m] = (b[0] * 256 + b[1]) / 100.0f; metricHave[m] = true; return true; }
      break;
    case M_FUEL:                                         // 012F: A*100/255 % ระดับน้ำมัน
      if (readPid("012F", "412F", 1, b)) { metricVal[m] = b[0] * 100.0f / 255.0f; metricHave[m] = true; return true; }
      break;
    case M_STFT:                                         // 0106: (A-128)*100/128 % short term fuel trim (bank 1)
      if (readPid("0106", "4106", 1, b)) { metricVal[m] = (b[0] - 128) * 100.0f / 128.0f; metricHave[m] = true; return true; }
      break;
  }
  return false;
}

void applyPin() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  SerialBT.setPin(btPin, strlen(btPin));
#else
  SerialBT.setPin(btPin);
#endif
}

bool connectOBD() {
  setStatus("BT...");
  applyPin();
  bool ok = false;
  unsigned int a[6];
  if (btMac.length() == 17 &&
      sscanf(btMac.c_str(), "%x:%x:%x:%x:%x:%x", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]) == 6) {
    uint8_t m[6];
    for (int i = 0; i < 6; i++) m[i] = (uint8_t)a[i];
    ok = SerialBT.connect(m);                 // ต่อด้วย MAC ที่เลือกไว้ (เร็วกว่า)
  } else {
    ok = SerialBT.connect(OBD_NAME);          // ยังไม่ได้เลือก -> ค้นหาด้วยชื่อ
  }
  if (!ok) { obdOk = false; setStatus("NO OBD"); return false; }
  elm("ATZ", 2500);
  elm("ATE0");
  elm("ATL0");
  elm("ATS0");
  elm("ATH0");
  elm("ATSP0");
  elm("0100", 10000);
  detectPids();
  fastTps = pidSupported(0x11);
  fastMode = 0;                                          // ทดสอบวิธีอ่านหลาย PID: ได้ก็ใช้ ไม่ได้ถอยกลับไปอ่านทีละตัว
  for (int mdl = 1; mdl <= 2 && !fastMode; mdl++) {
    fastMode = mdl;
    int a = 0, b2 = 0; float c = 0; bool ok = false;
    for (int k = 0; k < 3 && !ok; k++) ok = readFast(&a, &b2, &c);
    if (!ok) fastMode = 0;
  }
  Serial.printf("fast multi-PID mode: %d (TPS %d)\n", fastMode, fastTps);
  obdOk = true;
  setStatus("OBD OK");
  return true;
}

// ---------- อ่านค่า OBD ----------
// ---- (สมูท 1) เก็บค่ารอบ/ความเร็วพร้อมเวลาที่อ่านได้ 8 ค่าล่าสุด ให้จอไล่ค่าระหว่างจุด (interpolation) ----
struct RSamp { uint32_t t; float r, s; };
RSamp rsBuf[8];
volatile uint32_t rsN = 0;
portMUX_TYPE rsMux = portMUX_INITIALIZER_UNLOCKED;
void obdSample() {                                       // เรียกทุกครั้งที่อ่านรอบได้ใหม่ (จาก OBD task)
  RSamp v = {millis(), (float)rpm, (float)speedKmh};
  portENTER_CRITICAL(&rsMux);
  rsBuf[rsN & 7] = v; rsN = rsN + 1;
  portEXIT_CRITICAL(&rsMux);
}
// ค่ารองที่แต่ละหน้าใช้ (-1 = จบรายการ)
int pageMetrics(int pg, int *out) {
  static const int8_t L[13][9] = {
    {M_COOLANT, M_IAT, M_MAP, M_MAF, M_LOAD, M_TPS, M_FUEL, M_BATT, -1},   // หน้า 1
    {M_COOLANT, M_IAT, M_LOAD, M_MAF, M_FUEL, M_BATT, M_MAP, -1, -1},       // หน้า 2
    {M_COOLANT, M_BATT, M_TPS, M_MAP, -1, -1, -1, -1, -1},                  // หน้า 3
    {M_COOLANT, M_MAP, M_BATT, -1, -1, -1, -1, -1, -1},                     // หน้า 4
    {M_COOLANT, M_MAP, M_TPS, M_LOAD, M_IAT, M_BATT, -1, -1, -1},           // หน้า 5
    {M_COOLANT, M_IAT, M_BATT, M_TPS, M_LOAD, M_FUEL, -1, -1, -1},          // หน้า 6
    {M_COOLANT, M_IAT, M_MAP, M_MAF, M_LOAD, M_STFT, M_BATT, -1, -1},       // หน้า 7
    {M_COOLANT, M_IAT, M_BATT, -1, -1, -1, -1, -1, -1},                    // หน้า 8
    {M_COOLANT, -1, -1, -1, -1, -1, -1, -1, -1},                           // หน้า 9 (+ ค่าที่เลือกในช่องล่าง เพิ่มด้านล่าง)
    {M_COOLANT, -1, -1, -1, -1, -1, -1, -1, -1},                           // หน้า 10
    {M_COOLANT, -1, -1, -1, -1, -1, -1, -1, -1},                           // หน้า 11
    {M_COOLANT, -1, -1, -1, -1, -1, -1, -1, -1},                           // หน้า 12 (แบบหน้า 10)
    {M_COOLANT, -1, -1, -1, -1, -1, -1, -1, -1}};                          // หน้า 13 (+ ค่าที่เลือกในช่องล่าง แบบหน้า 9)
  int n = 0;
  pg = constrain(pg, 0, N_PAGES - 1);
  for (int i = 0; i < 9 && L[pg][i] >= 0; i++) {
    int m = L[pg][i];
    if (!metricAvailable(m) || (fastMode && fastTps && m == M_TPS)) continue;   // TPS มาพร้อมรอบ/ความเร็วแล้ว
    out[n++] = m;
  }
  if (isHPage(pg)) for (int s = 0; s < 2; s++) {           // หน้า 9 / 13: ค่าที่เลือกไว้ในช่องล่างซ้าย/ขวา
    extern volatile int slotMetric[2];
    int m = slotMetric[s]; bool dup = false;
    for (int i = 0; i < n; i++) if (out[i] == m) dup = true;
    if (!dup && metricAvailable(m) && !(fastMode && fastTps && m == M_TPS)) out[n++] = m;
  }
  return n;
}
uint32_t lastTempRead = 0, lastVoltRead = 0;
int failCount = 0;

void pollOBD() {
  if (demoMode) { vTaskDelay(30 / portTICK_PERIOD_MS); return; }   // เดโม: ไม่อ่านค่าจริง
  int b[2];
  bool any = false;

  static int rpmFail = 0, cyc = 0, fastFail = 0;
  if (fastMode) {                                        // รอบ+ความเร็ว+TPS ในคำสั่งเดียว (ถี่ ~20-30 ครั้ง/วินาที)
    int rp = 0, sp = -1; float tp = -1;
    if (readFast(&rp, &sp, &tp)) {
      rpm = rp; any = true; rpmFail = 0; fastFail = 0;
      if (sp >= 0) speedKmh = min(sp, MAX_SPEED);
      obdSample();
      if (tp >= 0) { metricVal[M_TPS] = tp; metricHave[M_TPS] = true; }
    } else {
      if (++rpmFail >= 5 && rpm) { rpm = 0; obdSample(); }
      if (++fastFail >= 12) { fastMode = 0; fastFail = 0; Serial.println("multi-PID failed -> single PID mode"); }
    }
  } else {
  if (readPid("010C", "410C", 2, b)) { rpm = ((b[0] * 256) + b[1]) / 4; any = true; rpmFail = 0; obdSample(); }
  else if (++rpmFail >= 5 && rpm) { rpm = 0; obdSample(); }              // อ่านพลาดครั้งสองครั้งไม่ใช่เครื่องดับ: ค้างค่าเดิมไว้ (กันรอบหายวูบ) พลาดติดกัน 5 ครั้งค่อยเป็น 0
  if ((cyc++ & 1) == 0 && readPid("010D", "410D", 1, b)) { speedKmh = min(b[0], MAX_SPEED); any = true; }   // ความเร็วอ่านสลับรอบ = รอบเครื่องอัปเดตถี่ขึ้น
  if (readPid("010C", "410C", 2, b)) { rpm = ((b[0] * 256) + b[1]) / 4; any = true; rpmFail = 0; obdSample(); }
  }

  // ค่ารอง: น้ำ (ใช้เตือนความร้อนเสมอ) + ค่าที่แสดงในสองช่องล่าง อ่านสลับกันทุก ~400 ms
  // (อ่านเฉพาะที่ใช้อยู่ เพื่อไม่ให้รอบ/ความเร็วอัปเดตช้า)
  static uint32_t lastSec = 0;
  static int rrIdx = 0;
  int u = urgentMetric;
  if (u >= 0) {
    urgentMetric = -1;
    if (pollMetric(u)) any = true;
  } else if (millis() - lastSec > 400) {
    lastSec = millis();
    int need[N_METRICS], cnt = 0;
    cnt = pageMetrics(page, need);                       // อ่านเฉพาะค่าที่หน้าปัจจุบันใช้ (น้ำอ่านเสมอเพื่อเตือนความร้อน)
    if (cnt > 4) lastSec -= 50;                          // ~350 ms ต่อค่า (ไม่ให้ค่ารองแย่งเวลาจากรอบเครื่องมากเกินไป)
    if (cnt > 0 && pollMetric(need[rrIdx++ % cnt])) any = true;
  }

  if (any) failCount = 0; else failCount++;
  if (failCount > 15) {
    failCount = 0;
    if (!SerialBT.connected()) { obdOk = false; setStatus("NO OBD"); }
  }
}

void obdTask(void *) {
  uint32_t lastTry = 0, retryMs = 8000;
  for (;;) {
    if (obdPause) { obdIdle = true; vTaskDelay(50 / portTICK_PERIOD_MS); continue; }
    obdIdle = false;
    if (btReconnect) { btReconnect = false; lastTry = 0; retryMs = 8000; }
    if (!SerialBT.connected()) {
      obdOk = false;
      if (lastTry == 0 || millis() - lastTry > retryMs) {
        lastTry = millis();
        if (connectOBD()) retryMs = 8000;
        else retryMs = (retryMs * 2 > 60000) ? 60000 : retryMs * 2;   // ต่อไม่ติดบ่อย ๆ = รอนานขึ้น (ลดการกินแรม/สแกนซ้ำตอนรถดับ)
      } else {
        vTaskDelay(100 / portTICK_PERIOD_MS);
      }
    } else {
      pollOBD();
    }
    vTaskDelay(1);
  }
}

void pauseOBD() {
  obdPause = true;
  uint32_t t = millis();
  while (!obdIdle && millis() - t < 20000) delay(20);
  if (SerialBT.connected()) SerialBT.disconnect();
  obdOk = false;
  setStatus("NO OBD");
}
void resumeOBD() { btReconnect = true; obdPause = false; }

// หยุดอ่านค่า OBD ชั่วคราวโดยไม่ตัดการเชื่อมต่อ (ใช้ตอนอ่าน/ลบ DTC เพราะต้องคุยกับ ELM327 เอง)
void holdOBD() {
  obdPause = true;
  uint32_t t = millis();
  while (!obdIdle && millis() - t < 20000) delay(20);
}
void releaseOBD() { obdPause = false; }

#if LINK_ESPNOW
// ======================================================================================
// รับค่ารถจากบอร์ด S3 ทาง ESP-NOW (S3 กระจายค่า ~30 ครั้ง/วินาที บนช่อง WiFi 1)
// ======================================================================================
volatile uint32_t nowLast = 0;         // millis() ที่ได้ข้อมูลล่าสุด
volatile uint8_t  nowLink = 0;         // สถานะ OBD ฝั่ง S3 (DN_LINK_*)
volatile uint32_t nowPkts = 0;         // นับข้อมูลที่ได้ (ไว้ดูอัตรา)
uint32_t nowRpmT = 0xFFFFFFFF;
int nowCh = DIDINOW_CHANNEL;
volatile bool nowDtcGot = false;
volatile uint16_t nowDtcWait = 0;
DidiNowDtc nowDtcRes;
static const uint8_t NOW_BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

#if ESP_ARDUINO_VERSION_MAJOR >= 3
void nowRecv(const esp_now_recv_info_t *, const uint8_t *d, int n) {
#else
void nowRecv(const uint8_t *, const uint8_t *d, int n) {
#endif
  if (n < 6 || ((const DidiNowData *)d)->magic != DIDINOW_MAGIC) return;
  uint8_t type = d[4];
  if (type == DN_DTC && n >= (int)sizeof(DidiNowDtc)) {
    const DidiNowDtc *r = (const DidiNowDtc *)d;
    if (nowDtcWait && r->id == nowDtcWait && !nowDtcGot) { memcpy(&nowDtcRes, r, sizeof(nowDtcRes)); nowDtcGot = true; }
    return;
  }
  if (type != DN_DATA || n < (int)sizeof(DidiNowData)) return;
  const DidiNowData *m = (const DidiNowData *)d;
  nowLast = millis();
  nowLink = m->link;
  nowPkts = nowPkts + 1;
  if (demoMode) return;                                   // เดโมของจอนี้เอง: ไม่ใช้ค่าจาก S3
  if (m->link == DN_LINK_WAIT) {                          // S3 ยังไม่ได้ค่าจากรถ
    rpm = 0; speedKmh = 0;
    for (int i = 0; i < N_METRICS; i++) metricHave[i] = false;
    return;
  }
  rpm = (int)lroundf(m->rpm);
  speedKmh = min((int)lroundf(m->spd), MAX_SPEED);
  coolant = (int)lroundf(m->ect);
  iat = (int)lroundf(m->iat);
  const float v[N_METRICS] = {m->ect, m->iat, m->volt, m->load, m->tps, m->map, m->maf, m->fuel, m->stft};
  for (int i = 0; i < N_METRICS; i++) { metricVal[i] = v[i]; metricHave[i] = true; }
  if (m->rpmT == 0 || m->rpmT != nowRpmT) { nowRpmT = m->rpmT; obdSample(); }   // ค่ารอบใหม่ (โหมดจำลองของ S3 = ทุกชุด)
}

void nowBegin() {
  obdIdle = true;                                         // ไม่มีงานอ่าน OBD บนจอนี้ (holdOBD/pauseOBD ไม่ต้องรอ)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(nowCh, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) { setStatus("NOW ERR"); Serial.println("ESP-NOW init FAILED"); return; }
  esp_now_register_recv_cb(nowRecv);
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, NOW_BCAST, 6);
  p.channel = 0;
  p.ifidx = WIFI_IF_STA;
  p.encrypt = false;
  esp_now_add_peer(&p);
  Serial.printf("ESP-NOW ready (channel %d)\n", nowCh);
}

// เรียกทุกรอบ loop: สถานะ + ถ้าไม่ได้ข้อมูล ไล่หาช่องที่ S3 ใช้
void nowTick(uint32_t now) {
  static uint32_t lastHop = 0;
  uint32_t age = now - nowLast;
  bool alive = nowLast && age < 2000;
  if (!demoMode) {
    if (!alive) { obdOk = false; setStatus("NO LINK"); }
    else if (nowLink == DN_LINK_OK) { obdOk = true; setStatus("OBD OK"); }
    else if (nowLink == DN_LINK_DEMO) { obdOk = true; setStatus("S3 DEMO"); }
    else { obdOk = false; setStatus("NO OBD"); }
  }
  if ((!nowLast || age > 3000) && now - lastHop > 250) {  // ไม่ได้ยิน S3 → เปลี่ยนช่อง 1..13 วนหา
    lastHop = now;
    nowCh = nowCh % 13 + 1;
    esp_wifi_set_channel(nowCh, WIFI_SECOND_CHAN_NONE);
  }
}
bool linkConnected() { return nowLast && millis() - nowLast < 2000 && nowLink == DN_LINK_OK; }

// ขอ DTC จาก S3 (รอสูงสุด ~10 วินาที) คืน true ถ้าได้คำตอบ
bool nowDtc(uint8_t cmd) {
  static uint16_t id = 1;
  DidiNowCmd c = {DIDINOW_MAGIC, DN_CMD, cmd, ++id};
  nowDtcGot = false;
  nowDtcWait = c.id;
  uint32_t t0 = millis(), sent = 0;
  for (int tries = 0; millis() - t0 < 10000 && !nowDtcGot; delay(10)) {
    if (tries < 2 && millis() - t0 >= sent) { esp_now_send(NOW_BCAST, (const uint8_t *)&c, sizeof(c)); sent = millis() - t0 + 4000; tries++; }
  }
  nowDtcWait = 0;
  return nowDtcGot;
}
#else
bool linkConnected() { return SerialBT.connected(); }
#endif

uint32_t lastBlink = 0, lastFrame = 0, lastFast = 0;
bool blinkOn = false, fastBlink = false;
float dispRpm = 0, dispSpeed = 0, dispTemp = 0;

static inline void easeTo(float &cur, float target, float dt, float tau) {
  cur += (target - cur) * (1.0f - expf(-dt / tau));
  if (fabsf(target - cur) < 0.05f) cur = target;
}

// ======================================================================================
// หน้า 5 (หน้าปัดกลม + ช่องค่าสองข้าง) และ หน้า 6 (ไมล์แบบแถบ) : วาดด้วยกราฟิก พื้นหลังเรียบ ไม่ใช้ JPEG
// ตัวเลขวาดลงสไปรท์เล็ก ๆ (pSpr) แล้วดันขึ้นจอทีเดียว = ไม่กะพริบ สไปรท์ถูกจองเฉพาะตอนอยู่หน้า 5/6
// ======================================================================================
uint16_t P_CT, P_CG, P_CK, P_CR, P_CEDGE;                // ช่องกลางหน้า 3: พื้นดำ + ตัวเลขสีสด (เกียร์เขียว / ความเร็วขาว / รอบเหลือง)
uint16_t carbonRow[4][336];                              // ลายคาร์บอน: คาบ 4x4 พิกเซล ใช้พิกัดจอจริงเป็นเฟส จึงต่อกันเนียนทุกชิ้นสไปรท์
void carbonInit(lgfx::LGFXBase *g) {
  uint16_t dk = g->color565(14, 14, 16), md = g->color565(30, 30, 34), lt = g->color565(52, 52, 58);
  for (int y = 0; y < 4; y++) for (int x = 0; x < 336; x++) {
    int xx = x & 3, bx = xx >> 1, by = y >> 1;
    bool horiz = ((bx ^ by) & 1) == 0;                   // ก้อน 2x2 สลับทิศเส้นใย
    int k = horiz ? (y & 1) : (xx & 1);                  // 0 = สว่าง 1 = มืด
    carbonRow[y][x] = GAUGE_CARBON ? (k == 0 ? lt : ((bx + by) & 1 ? md : dk)) : g->color565(0, 0, 0);   // 0 = พื้นดำสนิท
  }
}
void carbonRows(lgfx::LGFXBase *g, int gx, int gy, int w, int h, int dx, int dy) {   // วาดลายของพิกัดจอ (gx,gy) กว้าง w สูง h ลงที่ (dx,dy) ของ g
  for (int r = 0; r < h; r++) g->pushImage(dx, dy + r, w, 1, &carbonRow[(gy + r) & 3][gx & 3]);
}
void initNewColors();
void eColors();
void gColors();
void gReset();
uint16_t P_BG, P_BG4, P_TILE, P_TILE4, P_EDGE, P_VAL, P_WHT, P_LAB, P_YEL, P_ON, P_OFF, P_RIM, P_BAND, P_RED, P_GRN, P_GRNOFF;
void initPageColors() {
  P_BG   = lcd.color565(0, 0, 0);      P_BG4   = lcd.color565(10, 11, 14);
  P_TILE = lcd.color565(40, 40, 44);   P_TILE4 = lcd.color565(32, 34, 38);
  P_EDGE = lcd.color565(78, 78, 84);   P_LAB   = lcd.color565(150, 152, 162);
  P_VAL  = lcd.color565(245, 236, 150); P_WHT  = lcd.color565(240, 240, 246);
  P_YEL  = lcd.color565(255, 196, 40);
  P_CT = lcd.color565(4, 5, 8); P_CG = lcd.color565(90, 255, 130); P_CK = lcd.color565(255, 255, 255); P_CR = lcd.color565(255, 214, 60); P_CEDGE = lcd.color565(120, 130, 150);
  carbonInit(&lcd);
  initNewColors();
  eColors();
  gColors();
  P_ON   = lcd.color565(228, 204, 64);  P_OFF   = lcd.color565(70, 66, 34);
  P_RIM  = lcd.color565(58, 58, 64);   P_BAND  = lcd.color565(240, 214, 72);
  P_RED  = lcd.color565(206, 40, 40);
  P_GRN  = lcd.color565(70, 210, 110); P_GRNOFF = lcd.color565(26, 58, 36);
}

lgfx::LGFX_Sprite pSpr(&lcd);
bool pSprOk = false;
bool pSprWanted = false;                                  // อนุญาตจองสไปรท์เฉพาะตอน Bluetooth ต่อติดแล้ว (ไม่แย่งแรมจาก BT)
int PSPR_W = 112, PSPR_H = 60;
// ---------- ก้อนแรมกลาง: จองครั้งเดียวตอนบูต (ก่อนเปิด Bluetooth) แล้วทุกหน้าแบ่งใช้ ----------
// จอแสดงทีละหน้า -> ใช้ก้อนเดียวกันได้: หน้า 9 ใช้ทั้งก้อน, หน้า 10/11 = สไปรท์ตัวเลข + พื้นหลังใต้ตัวเลข, หน้าอื่น = สไปรท์ตัวเลข
// เปลี่ยนหน้ากี่รอบก็ไม่ต้องขอแรมใหม่ -> ไม่มีกรณีจองไม่ได้/แรมแตกเป็นชิ้น  (ตั้ง USE_RAM_POOL 0 = กลับไปจองทีละหน้าแบบเดิม)
#define USE_RAM_POOL   1
#define POOL_FULL      49792     // หน้า 9: พื้นหลังใต้ตัวเลข 46 KB + แถบสไปรท์วาดตัวเลข 3 KB
#define POOL_LITE      49792     // (เท่ากับ FULL แล้ว: หน้า 9 ไม่ต้องใช้บัฟเฟอร์สองชั้นอีก)
#define POOL_MIN_FREE  40000     // หลังเปิด Bluetooth ถ้าแรมว่างเหลือน้อยกว่านี้ -> เตือนใน Serial
const size_t PSPR_BYTES = 112 * 60 * 2;
uint8_t *gPool = nullptr;
size_t gPoolSize = 0, gPoolOff = 0;
bool gPoolLite = false;
void poolInit() {
#if USE_RAM_POOL
  gPool = (uint8_t *)malloc(POOL_FULL);
  if (gPool) gPoolSize = POOL_FULL;
  else if ((gPool = (uint8_t *)malloc(POOL_LITE)) != nullptr) { gPoolSize = POOL_LITE; gPoolLite = true; }
  Serial.printf("RAM pool: %u bytes %s (free heap %u, largest %u)\n", (unsigned)gPoolSize, gPool ? (gPoolLite ? "LITE" : "FULL") : "FAILED -> per-page alloc",
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
#endif
}
void poolReset(size_t off) { gPoolOff = off; }
void *poolTake(size_t n) {                                   // แบ่งชิ้นจากก้อนกลาง (คืน nullptr ถ้าไม่พอ)
  if (!gPool) return nullptr;
  n = (n + 3) & ~(size_t)3;
  if (gPoolOff + n > gPoolSize) return nullptr;
  void *p = gPool + gPoolOff; gPoolOff += n; return p;
}
// สไปรท์ใช้แรมจากก้อนกลาง (ถ้าไม่มีก้อนกลาง/ไม่พอ ใช้ createSprite ปกติ)
bool sprFromPool(lgfx::LGFX_Sprite *sp, int w, int h) {
  void *p = poolTake((size_t)w * h * 2);
  if (p) { sp->setColorDepth(16); sp->setBuffer(p, w, h, 16); return true; }
  sp->setColorDepth(16);
  return sp->createSprite(w, h) != nullptr;
}

void pSprEnsure() {                                       // ลองสไปรท์ใหญ่ก่อน ถ้าแรมไม่พอลองเล็กลง (ทุกที่วาดเป็นชิ้น ๆ ได้ ไม่กะพริบ)
  if (pSprOk || isPoolPage(page)) return;                      // หน้า 9 / 13 ไม่ใช้สไปรท์นี้ (ใช้ก้อนกลางทั้งก้อน)
  if (gPool) {                                              // มีก้อนกลาง: สไปรท์อยู่ต้นก้อนเสมอ ไม่ต้องรอ/ไม่ต้องคืนให้ Bluetooth
    PSPR_W = 112; PSPR_H = 60;
    pSpr.setColorDepth(16); pSpr.setBuffer(gPool, PSPR_W, PSPR_H, 16); pSprOk = true;
    return;
  }
  if (!pSprWanted) return;
  static const int SZ[4][2] = {{112, 60}, {112, 32}, {96, 24}, {64, 16}};
  pSpr.setColorDepth(16);
  for (int i = 0; i < 4 && !pSprOk; i++) {
    PSPR_W = SZ[i][0]; PSPR_H = SZ[i][1];
    pSprOk = pSpr.createSprite(PSPR_W, PSPR_H) != nullptr;
  }
  Serial.printf("page sprite %s %dx%d (free heap %u, largest %u)\n", pSprOk ? "ok" : "FAILED", PSPR_W, PSPR_H, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}
void pSprFree() { if (pSprOk) { pSpr.deleteSprite(); pSprOk = false; } }

// ---- ตัวอักษรค่าในกล่องพื้นเรียบ: วาดลงสไปรท์ (กล่องไม่เกิน 112x60) แล้วดันขึ้นจอ ----
// base = เส้นฐานตัวอักษรในกล่อง, center=true จัดกึ่งกลาง ไม่งั้นชิดขวา
void drawVal(int x, int y, int w, int h, const char *s, const lgfx::IFont *f, float sc, uint16_t fg, uint16_t bg, bool center, int base) {
  if (!pSprOk) {                                           // ไม่มีสไปรท์: วาดตรงบนจอ (อาจกะพริบเล็กน้อย)
    lcd.fillRect(x, y, w, h, bg);
    lcd.setFont(f); lcd.setTextSize(sc); lcd.setTextColor(fg);
    lcd.setTextDatum(center ? lgfx::textdatum_t::baseline_center : lgfx::textdatum_t::baseline_right);
    lcd.drawString(s, center ? x + w / 2 : x + w - 1, y + base);
    lcd.setTextSize(1.0f); lcd.setTextDatum(lgfx::textdatum_t::top_left);
    return;
  }
  for (int ty = y; ty < y + h; ty += PSPR_H)               // กล่องใหญ่กว่าสไปรท์ = วาดทีละชิ้น ชิ้นละ 1 push (ไม่เห็นช่วงลบ)
    for (int tx = x; tx < x + w; tx += PSPR_W) {
      int tw = min(PSPR_W, x + w - tx), th = min(PSPR_H, y + h - ty);
      pSpr.fillRect(0, 0, PSPR_W, PSPR_H, bg);
      pSpr.setFont(f); pSpr.setTextSize(sc); pSpr.setTextColor(fg);
      pSpr.setTextDatum(center ? lgfx::textdatum_t::baseline_center : lgfx::textdatum_t::baseline_right);
      pSpr.drawString(s, (center ? x + w / 2 : x + w - 1) - tx, y + base - ty);
      pSpr.setTextSize(1.0f); pSpr.setTextDatum(lgfx::textdatum_t::top_left);
      lcd.setClipRect(tx, ty, tw, th); pSpr.pushSprite(tx, ty); lcd.clearClipRect();
    }
}

// ป้ายสถานะ (ฟอนต์ Font2 ขนาดตายตัว ใช้ padding ลบของเก่าได้พอดี)
void pStatusText(int x, int y, bool right, const char *msg, uint16_t col, uint16_t bg, int padW) {
  lcd.setFont(&fonts::Font2);
  lcd.setTextDatum(right ? lgfx::textdatum_t::top_right : lgfx::textdatum_t::top_left);
  lcd.setTextColor(col, bg);
  lcd.setTextPadding(padW);
  lcd.drawString(msg, x, y);
  lcd.setTextPadding(0);
  lcd.setTextDatum(lgfx::textdatum_t::top_left);
}

// ---- แคชของค่าที่วาดล่าสุด (วาดใหม่เฉพาะที่เปลี่ยน) ----
char    pTxt[16][8];
int8_t  pBar[12];
uint16_t pCol[16];
float   pd[N_METRICS];            // ค่ารองที่ทำให้นุ่มแล้ว
bool    pdHave[N_METRICS];
extern int nLit, nFlash, nWBar, eLedKey; extern float nArcA;
float   p3Ang = -999;
const char *pStatPtr = nullptr; int pStatKey = -1;
void pReset() {
  memset(pTxt, 0, sizeof(pTxt)); memset(pBar, -1, sizeof(pBar)); memset(pCol, 0, sizeof(pCol));
  memset(pdHave, 0, sizeof(pdHave));
  p3Ang = -999; pStatPtr = nullptr; pStatKey = -1;
  nLit = -1; nFlash = -1; nWBar = -1; nArcA = -999; eLedKey = -1;
  gReset();
}
bool chgTxt(int i, const char *s, uint16_t col) {
  if (strcmp(pTxt[i], s) == 0 && pCol[i] == col) return false;
  strncpy(pTxt[i], s, 7); pTxt[i][7] = 0; pCol[i] = col; return true;
}
bool chgBar(int i, int v) { if (pBar[i] == v) return false; pBar[i] = (int8_t)v; return true; }

// เกียร์ที่ประมาณจากรอบ/ความเร็ว (รอบต่อ km/h ของแต่ละเกียร์) ปรับตารางนี้ให้ตรงกับรถของคุณ
const float GEAR_DEF[6] = {118, 68, 47, 36, 29, 24};     // ค่าเริ่มต้น (กด RESET ในเมนู GEAR)
float GEAR_RPM_PER_KMH[6] = {118, 68, 47, 36, 29, 24};   // ใช้งานจริง: ตั้ง/เรียนรู้ได้ในเมนู SETTINGS > GEAR แล้วเก็บลง NVS
int gearCount = 6;
char demoGear = 'N';
char gearChar() {
  if (demoMode) return demoGear;
  if (speedKmh < 4) return 'N';
  if (rpm < 600) return '-';
  float ratio = (float)rpm / (float)speedKmh, bestErr = 1e9f; int best = -1;
  for (int g = 0; g < gearCount; g++) {
    float e = fabsf(ratio - GEAR_RPM_PER_KMH[g]) / GEAR_RPM_PER_KMH[g];
    if (e < bestErr) { bestErr = e; best = g; }
  }
  return (bestErr < 0.15f && best >= 0) ? (char)('1' + best) : '-';
}

float tripKm = 0;                                 // ระยะทริป (km) เก็บลง NVS ทุก ~60 วินาที แตะช่อง TRIP ในหน้า 6 เพื่อรีเซ็ต
float demoTrip = 0;
void pdUpdate(float dt) {                          // ทำให้ค่ารองนุ่ม (ไม่กระโดด)
  for (int m = 0; m < N_METRICS; m++) {
    if (!metricHave[m]) { pdHave[m] = false; continue; }
    float v = metricVal[m];
    if (!pdHave[m]) { pd[m] = v; pdHave[m] = true; }
    else easeTo(pd[m], v, dt, 0.4f);
  }
}
int gScaleMax() { return constrain((maxRpm + 999) / 1000 + 2, 8, 12); }   // ตัวเลขสูงสุดบนหน้าปัด (x1000 rpm): redline ปัดขึ้น + 2

// =============================== PEAKS: ค่าสูงสุด/ต่ำสุด + ขีด peak hold ===============================
enum { PK_RPM, PK_SPEED, PK_COOL, PK_IAT, PK_BOOST, PK_LOAD, PK_BATT, N_PK };
const char *const PK_NAME[N_PK] = {"RPM", "SPEED km/h", "COOLANT C", "IAT C", "BOOST bar", "LOAD %", "BATT V (min)"};
float pkTrip[N_PK], pkAll[N_PK];              // TRIP = เริ่มใหม่ทุกครั้งที่ต่อ OBD ติด, ALL TIME = เก็บลงหน่วยความจำถาวร
bool  pkDirty = false;
uint32_t pkSaveAt = 0;
void pkClear(float *a) { for (int i = 0; i < N_PK; i++) a[i] = -1e9f; a[PK_BATT] = 1e9f; }   // แบต = ค่าต่ำสุด
bool pkHave(const float *a, int i) { return i == PK_BATT ? a[i] < 1e8f : a[i] > -1e8f; }
void pkLoad() {
  if (prefs.isKey("pkall") && prefs.getBytesLength("pkall") == sizeof(pkAll)) prefs.getBytes("pkall", pkAll, sizeof(pkAll));
  else pkClear(pkAll);
  pkClear(pkTrip);
}
void pkSave() { prefs.putBytes("pkall", pkAll, sizeof(pkAll)); pkDirty = false; pkSaveAt = millis(); }
void pkSet(int i, float v) {
  if (i == PK_BATT) {
    if (v < pkTrip[i]) pkTrip[i] = v;
    if (v < pkAll[i]) { pkAll[i] = v; pkDirty = true; }
  } else {
    if (v > pkTrip[i]) pkTrip[i] = v;
    if (v > pkAll[i]) { pkAll[i] = v; pkDirty = true; }
  }
}
void peaksUpdate(uint32_t now) {
  static bool was = false;
  bool live = obdOk && !demoMode;                      // เก็บเฉพาะค่าจริงจากรถ (ไม่เก็บตอนเดโม)
  if (live && !was) pkClear(pkTrip);                   // ต่อ OBD ติดใหม่ = เริ่มทริปใหม่
  was = live;
  if (live) {
    int r = rpm, s = speedKmh;
    if (r > 0 && r < 12000) pkSet(PK_RPM, r);
    if (s >= 0 && s <= MAX_SPEED) pkSet(PK_SPEED, s);
    if (metricHave[M_COOLANT]) pkSet(PK_COOL, metricVal[M_COOLANT]);
    if (metricHave[M_IAT])     pkSet(PK_IAT, metricVal[M_IAT]);
    if (metricHave[M_MAP])     pkSet(PK_BOOST, (metricVal[M_MAP] - 101.3f) / 100.0f);
    if (metricHave[M_LOAD])    pkSet(PK_LOAD, metricVal[M_LOAD]);
    if (metricHave[M_BATT] && metricVal[M_BATT] > 6.0f && metricVal[M_BATT] < 20.0f) pkSet(PK_BATT, metricVal[M_BATT]);
  }
  if (pkDirty && now - pkSaveAt > 60000) pkSave();     // เขียนไม่ถี่เกินนาทีละครั้ง (ถนอมแฟลช)
}

// peak hold ของรอบ: ขึ้นตามเข็มทันที ค้างไว้ 2.5 วินาที แล้วค่อย ๆ ไหลลงมาหาเข็ม
float pkHoldRpm = 0;
uint32_t pkHoldT = 0;
void pkHoldUpdate(uint32_t now, float dt) {
  if (dispRpm >= pkHoldRpm) { pkHoldRpm = dispRpm; pkHoldT = now; }
  else if (now - pkHoldT > 2500) { pkHoldRpm -= 2500.0f * dt; if (pkHoldRpm < dispRpm) pkHoldRpm = dispRpm; }
}
float p3Mk = -999;                            // องศาของขีด peak บนหน้าปัด (-999 = ซ่อน)

// =============================== หน้า 5 : หน้าปัดกลม ===============================
const int G_CX = 160, G_CY = 128, NEEDLE_LEN = 84;
const int T3_X[2] = {0, 259}, T3_Y[3] = {20, 93, 166}, T3_W = 61, T3_H = 69;
const char *const T3_LAB[6] = {"BOOST", "TPS %", "LOAD %", "WATER C", "AIR C", "BATT V"};
// ช่องกลาง: GEAR / KPH / RPM
const int C3[3][4] = {{172, 140, 28, 38}, {204, 140, 46, 38}, {172, 181, 78, 35}};
const char *const C3_LAB[3] = {"GEAR", "KPH", "RPM"};

void tileFrame(int x, int y, int w, int h, uint16_t fill) {
  lcd.fillRoundRect(x, y, w, h, 3, fill);
  lcd.drawRoundRect(x, y, w, h, 3, P_EDGE);
}

void p3DrawGauge(lgfx::LGFXBase *g, int ox, int oy) {            // วงแหวน + แถบสี + ขีด + ตัวเลข + ป้าย (ไม่รวมเข็ม)
  int cx = G_CX - ox, cy = G_CY - oy, sm = gScaleMax();
  float redF = constrain(maxRpm / (1000.0f * sm), 0.0f, 1.0f);
  float aRed = 90.0f + 270.0f * redF;
  g->fillCircle(cx, cy, 96, P_RIM);
  g->fillArc(cx, cy, 66, 90, 90.0f, aRed, P_BAND);
  if (aRed < 359.5f) g->fillArc(cx, cy, 66, 90, aRed, 360.0f, P_RED);
  g->fillCircle(cx, cy, 65, TFT_BLACK);
  g->drawCircle(cx, cy, 57, lcd.color565(150, 128, 40));
  const int GW = g->width(), GH = g->height();
  auto inb = [&](int px, int py, int m) { return px > -m && px < GW + m && py > -m && py < GH + m; };   // ข้ามของที่อยู่นอกชิ้นที่กำลังวาด (เร็วขึ้นมาก)
  uint16_t tickCol = lcd.color565(20, 20, 20);
  for (int n = 0; n <= sm * 5; n++) {                           // ขีดทุก 200 rpm ยาวทุก 1000
    float a = (90.0f + 270.0f * n / (sm * 5)) * 0.0174533f;
    int r1 = (n % 5 == 0) ? 76 : 71;
    int x0 = cx + (int)(cosf(a) * 66), y0 = cy + (int)(sinf(a) * 66), x1 = cx + (int)(cosf(a) * r1), y1 = cy + (int)(sinf(a) * r1);
    if (inb(x0, y0, 4) || inb(x1, y1, 4)) g->drawLine(x0, y0, x1, y1, tickCol);
  }
  g->setFont(&fonts::FreeSansBold9pt7b);
  g->setTextDatum(lgfx::textdatum_t::middle_center);
  for (int n = 0; n <= sm; n++) {
    float a = (90.0f + 270.0f * n / sm) * 0.0174533f;
    char b[4]; snprintf(b, sizeof(b), "%d", n);
    int nx = cx + (int)(cosf(a) * 78), ny = cy + (int)(sinf(a) * 78);
    if (!inb(nx, ny, 14)) continue;
    g->setTextColor((n * 1000 >= maxRpm) ? TFT_WHITE : lcd.color565(20, 20, 20));
    g->drawString(b, nx, ny);
  }
  if (cx + 32 > 0 && cx - 32 < GW && cy - 28 > -6 && cy - 38 < GH) {
    g->setFont(&fonts::Font0);
    g->setTextColor(P_LAB);
    g->drawString("x1000 RPM", cx, cy - 32);
  }
  if (p3Mk > -900) {                                            // ขีด peak hold (ขอบดำ + ขีดขาว)
    float a = p3Mk * 0.0174533f, ca = cosf(a), sa = sinf(a);
    g->drawWideLine(cx + (int)(ca * 62), cy + (int)(sa * 62), cx + (int)(ca * 95), cy + (int)(sa * 95), 2.6f, TFT_BLACK);
    g->drawWideLine(cx + (int)(ca * 64), cy + (int)(sa * 64), cx + (int)(ca * 93), cy + (int)(sa * 93), 1.2f, TFT_WHITE);
  }
  g->setTextDatum(lgfx::textdatum_t::top_left);
}

// ช่อง GEAR/KPH/RPM อยู่ทับหน้าปัด: ทุกครั้งที่เข็มวาดทับ ต้องวาดช่องกลับพร้อมค่าล่าสุด (ในสไปรท์เดียวกัน = ไม่กะพริบ)
void p3DrawCenter(lgfx::LGFXBase *g, int ox, int oy) {
  for (int i = 0; i < 3; i++) {
    int x = C3[i][0] - ox, y = C3[i][1] - oy, w = C3[i][2], h = C3[i][3];
    if (x >= PSPR_W && g != &lcd) continue;
    g->fillRoundRect(x, y, w, h, 3, P_CT);
    g->drawRoundRect(x, y, w, h, 3, P_CEDGE);
    g->setFont(&fonts::Font0); g->setTextColor(P_LAB);
    g->setTextDatum(lgfx::textdatum_t::top_left);
    g->drawString(C3_LAB[i], x + (w - g->textWidth(C3_LAB[i])) / 2, y + h - 10);
    const char *s = pTxt[6 + i];
    if (!s[0]) continue;
    int n = strlen(s); float sc = (i == 0) ? 1.3f : (i == 1 ? (n <= 2 ? 1.3f : 1.0f) : (n >= 5 ? 1.0f : 1.2f));
    int base = (i == 2) ? 23 : 26;
    g->setFont(&fonts::FreeSansBold12pt7b); g->setTextSize(sc); g->setTextColor(pCol[6 + i]);
    g->setTextDatum(lgfx::textdatum_t::baseline_center);
    g->drawString(s, x + 1 + (w - 2) / 2, y + 2 + base);
    g->setTextSize(1.0f); g->setTextDatum(lgfx::textdatum_t::top_left);
  }
}

void p3DrawNeedle(lgfx::LGFXBase *g, int ox, int oy, float deg) {
  float a = deg * 0.0174533f, ca = cosf(a), sa = sinf(a), px = -sa, py = ca;
  int cx = G_CX - ox, cy = G_CY - oy;
  uint16_t red = lcd.color565(236, 44, 44), dark = lcd.color565(8, 8, 10);
  int tx = cx + (int)(ca * NEEDLE_LEN), ty = cy + (int)(sa * NEEDLE_LEN);
  g->fillTriangle(tx, ty, cx + (int)(px * 5 - ca * 14), cy + (int)(py * 5 - sa * 14), cx + (int)(-px * 5 - ca * 14), cy + (int)(-py * 5 - sa * 14), dark);   // ขอบดำ
  g->fillTriangle(tx - (int)ca, ty - (int)sa, cx + (int)(px * 3 - ca * 12), cy + (int)(py * 3 - sa * 12), cx + (int)(-px * 3 - ca * 12), cy + (int)(-py * 3 - sa * 12), red);
  g->fillCircle(cx, cy, 9, lcd.color565(88, 88, 94));
  g->fillCircle(cx, cy, 5, dark);
}

// วาดหน้าปัด+เข็มในกรอบ (x0,y0,w,h) ทีละชิ้นขนาดสไปรท์ (ไม่กะพริบ) ถ้าไม่มีสไปรท์ใช้คลิปตรง ๆ บนจอ
void p3Render(int x0, int y0, int w, int h, float deg) {
  for (int ty = y0; ty < y0 + h; ty += PSPR_H)
    for (int tx = x0; tx < x0 + w; tx += PSPR_W) {
      int tw = min(PSPR_W, x0 + w - tx), th = min(PSPR_H, y0 + h - ty);
      if (pSprOk) {
        carbonRows(&pSpr, tx, ty, PSPR_W, PSPR_H, 0, 0);
        p3DrawGauge(&pSpr, tx, ty);
        p3DrawNeedle(&pSpr, tx, ty, deg);
        p3DrawCenter(&pSpr, tx, ty);
        lcd.setClipRect(tx, ty, tw, th); pSpr.pushSprite(tx, ty); lcd.clearClipRect();
      } else {
        lcd.setClipRect(tx, ty, tw, th);
        carbonRows(&lcd, tx, ty, tw, th, tx, ty);
        p3DrawGauge(&lcd, 0, 0); p3DrawNeedle(&lcd, 0, 0, deg); p3DrawCenter(&lcd, 0, 0);
        lcd.clearClipRect();
      }
    }
}

void p3NeedleStep(float deg) {
  float a0 = (p3Ang < -900) ? deg : p3Ang;
  int minx = G_CX - 14, maxx = G_CX + 14, miny = G_CY - 14, maxy = G_CY + 14;
  float as[2] = {a0, deg};
  for (int k = 0; k < 2; k++) {
    float r = as[k] * 0.0174533f;
    for (int j = 0; j < 2; j++) {
      int d = j ? NEEDLE_LEN : -16;
      int x = G_CX + (int)(cosf(r) * d), y = G_CY + (int)(sinf(r) * d);
      minx = min(minx, x - 6); maxx = max(maxx, x + 6); miny = min(miny, y - 6); maxy = max(maxy, y + 6);
    }
  }
  minx = max(minx, 0); miny = max(miny, 0); maxx = min(maxx, 320); maxy = min(maxy, 240);
  p3Render(minx, miny, maxx - minx, maxy - miny, deg);
  p3Ang = deg;
}

void p3MarkerStep(float mk) {                                     // ย้ายขีด peak: วาดใหม่เฉพาะบริเวณขีดเก่า/ใหม่
  float as[2] = {p3Mk, mk};
  p3Mk = mk;
  int minx = 9999, miny = 9999, maxx = -1, maxy = -1;
  for (int k = 0; k < 2; k++) {
    if (as[k] < -900) continue;
    float r = as[k] * 0.0174533f;
    for (int j = 0; j < 2; j++) {
      int d = j ? 98 : 60;
      int x = G_CX + (int)(cosf(r) * d), y = G_CY + (int)(sinf(r) * d);
      minx = min(minx, x - 5); maxx = max(maxx, x + 5); miny = min(miny, y - 5); maxy = max(maxy, y + 5);
    }
  }
  if (maxx < 0) return;
  minx = max(minx, 0); miny = max(miny, 0); maxx = min(maxx, 320); maxy = min(maxy, 240);
  p3Render(minx, miny, maxx - minx, maxy - miny, p3Ang < -900 ? 90.0f : p3Ang);
}

void p3Static() {
  carbonRows(&lcd, 0, 0, 320, 240, 0, 0);
  for (int c = 0; c < 2; c++)
    for (int r = 0; r < 3; r++) {
      int x = T3_X[c], y = T3_Y[r];
      tileFrame(x, y, T3_W, T3_H, P_TILE);
      lcd.setFont(&fonts::Font0); lcd.setTextColor(P_LAB, P_TILE);
      lcd.setCursor(x + 14, y + 58); lcd.print(T3_LAB[c * 3 + r]);
      for (int s = 0; s < 10; s++) lcd.fillRect(x + 4, y + 60 - s * 6, 6, 4, P_OFF);
    }
  for (int i = 0; i < 3; i++) {
    lcd.fillRoundRect(C3[i][0], C3[i][1], C3[i][2], C3[i][3], 3, P_CT); lcd.drawRoundRect(C3[i][0], C3[i][1], C3[i][2], C3[i][3], 3, P_CEDGE);
    lcd.setFont(&fonts::Font0); lcd.setTextColor(P_LAB, P_CT);
    int lw = lcd.textWidth(C3_LAB[i]);
    lcd.setCursor(C3[i][0] + (C3[i][2] - lw) / 2, C3[i][1] + C3[i][3] - 10); lcd.print(C3_LAB[i]);
  }
  p3DrawGauge(&lcd, 0, 0);
  lcd.setFont(&fonts::FreeSansBold9pt7b); lcd.setTextColor(C_PINK);
}

void p3Tile(int i, bool have, float v, const char *fmt, float frac, const lgfx::IFont *f) {
  int c = i / 3, r = i % 3, x = T3_X[c], y = T3_Y[r];
  char b[8];
  if (have) snprintf(b, sizeof(b), fmt, v); else strcpy(b, "--");
  int bl = strlen(b);
  if (bl >= 4 || (bl == 3 && !strchr(b, '.'))) f = &fonts::FreeSansBold12pt7b;     // ตัวเลขยาว: ใช้ฟอนต์เล็กลงให้พอดีช่อง
  if (chgTxt(i, b, P_VAL)) drawVal(x + 10, y + 6, 49, 36, b, f, 1.0f, P_VAL, P_TILE, false, (f == &fonts::FreeSansBold12pt7b) ? 27 : 31);
  int lit = have ? (int)lroundf(constrain(frac, 0.0f, 1.0f) * 10.0f) : 0;
  if (chgBar(i, lit)) for (int s = 0; s < 10; s++) lcd.fillRect(x + 4, y + 60 - s * 6, 6, 4, s < lit ? P_ON : P_OFF);
}

void updatePage3(uint32_t now, float dt, bool over, bool fastChanged) {
  pSprEnsure();
  pdUpdate(dt);
  float frac = constrain(dispRpm / (1000.0f * gScaleMax()), 0.0f, 1.0f);
  float deg = 90.0f + 270.0f * frac;
  static uint32_t lastNd = 0;
  if (p3Ang < -900 || (fabsf(deg - p3Ang) > 0.45f && now - lastNd >= 12)) { p3NeedleStep(deg); lastNd = now; }   // ขยับเกินครึ่งพิกเซล และไม่ถี่เกิน 12 ms
  float mk = (pkHoldRpm - dispRpm > 150.0f) ? 90.0f + 270.0f * constrain(pkHoldRpm / (1000.0f * gScaleMax()), 0.0f, 1.0f) : -999.0f;
  if (p3Ang > -900 && ((mk < -900) != (p3Mk < -900) || (mk > -900 && fabsf(mk - p3Mk) > 0.6f))) p3MarkerStep(mk);

  const lgfx::IFont *f18 = &fonts::FreeSansBold18pt7b, *f12 = &fonts::FreeSansBold12pt7b;
  float boost = (pd[M_MAP] - 101.3f) / 100.0f;
  p3Tile(0, metricHave[M_MAP],  boost,     "%.1f", (boost + 1.0f) / 2.0f, f18);
  p3Tile(1, metricHave[M_TPS],  pd[M_TPS], "%.0f", pd[M_TPS] / 100.0f,    f18);
  p3Tile(2, metricHave[M_LOAD], pd[M_LOAD],"%.0f", pd[M_LOAD] / 100.0f,   f18);
  p3Tile(3, true, pd[M_COOLANT] > 0 || metricHave[M_COOLANT] ? pd[M_COOLANT] : dispTemp, "%.0f", (pd[M_COOLANT] - 40.0f) / 90.0f, f18);
  p3Tile(4, metricHave[M_IAT],  pd[M_IAT], "%.0f", (pd[M_IAT] + 10.0f) / 90.0f, f18);
  p3Tile(5, metricHave[M_BATT], pd[M_BATT],"%.1f", (pd[M_BATT] - 11.0f) / 4.0f, f12);

  char b[8];
  char gc = gearChar();
  b[0] = gc; b[1] = 0;
  if (chgTxt(6, b, P_CG)) drawVal(C3[0][0] + 1, C3[0][1] + 2, C3[0][2] - 2, 26, b, &fonts::FreeSansBold12pt7b, 1.3f, P_CG, P_CT, true, 26);
  snprintf(b, sizeof(b), "%d", (int)lroundf(dispSpeed));
  if (chgTxt(7, b, P_CK)) drawVal(C3[1][0] + 1, C3[1][1] + 2, C3[1][2] - 2, 26, b, &fonts::FreeSansBold12pt7b, strlen(b) <= 2 ? 1.3f : 1.0f, P_CK, P_CT, true, 26);
  snprintf(b, sizeof(b), "%d", ((int)lroundf(dispRpm) + 5) / 10 * 10);
  uint16_t rc = (over && fastBlink) ? C_RED : P_CR;
  static uint32_t lastRt3 = 0;
  bool rt3 = (now - lastRt3 >= 80) || rc != pCol[8];     // ตัวเลขรอบเปลี่ยนไม่ถี่เกิน ~12 ครั้ง/วินาที (ลดการกะพริบ)
  if (rt3 && chgTxt(8, b, rc)) { lastRt3 = now; drawVal(C3[2][0] + 1, C3[2][1] + 2, C3[2][2] - 2, 23, b, &fonts::FreeSansBold12pt7b, strlen(b) >= 5 ? 1.0f : 1.2f, rc, P_CT, true, 23); }

  const char *msg = over ? (fastBlink ? "REDLINE" : "") : curStatus();
  int key = over ? (fastBlink ? 1 : 2) : 0;
  if (msg != pStatPtr || key != pStatKey) {
    pStatPtr = msg; pStatKey = key;
    pStatusText(6, 3, false, msg, over ? C_RED : (strcmp(msg, "OBD OK") == 0 ? C_OK : C_WARN), P_BG, 70);
  }
}

// =============================== หน้า 6 : ไมล์แบบแถบ ===============================
const int B4_X = 103, B4_Y = 5, B4_W = 160, B4_H = 25, B4_SEGS = 40;
const int T4_BX[5] = {1, 65, 129, 193, 257};
const int T4_Y = 178, T4_W = 62, T4_H = 48;
const char *const T4_LAB[5] = {"COOLANT", "IAT C", "BATT V", "TPS %", "LOAD %"};
const int M4[5] = {M_COOLANT, M_IAT, M_BATT, M_TPS, M_LOAD};

uint16_t p4Grad(float t) {                         // เหลือง -> ส้ม -> แดง
  int g = 215 - (int)(190.0f * t);
  return lcd.color565(255, g < 0 ? 0 : g, 0);
}

void p4Static() {
  lcd.fillScreen(P_BG4);
  int sm = gScaleMax();
  tileFrame(3, 55, 100, 54, P_TILE4);   tileFrame(3, 113, 100, 56, P_TILE4);
  tileFrame(108, 55, 59, 114, P_TILE4); tileFrame(171, 55, 144, 114, P_TILE4);
  lcd.setFont(&fonts::Font2); lcd.setTextColor(P_LAB, P_TILE4);
  lcd.setCursor(8, 58);  lcd.print("TRIP km");
  lcd.setCursor(8, 116); lcd.print("FUEL %");
  lcd.setCursor(108 + (59 - lcd.textWidth("GEAR")) / 2, 58); lcd.print("GEAR");
  lcd.setCursor(176, 58); lcd.print("SPEED");
  lcd.setCursor(311 - lcd.textWidth("km/h"), 150); lcd.print("km/h");
  for (int i = 0; i < 5; i++) {
    tileFrame(T4_BX[i], T4_Y, T4_W, T4_H, P_TILE4);
    lcd.setCursor(T4_BX[i] + 4, T4_Y + 2); lcd.print(T4_LAB[i]);
    for (int s = 0; s < 10; s++) lcd.fillRect(T4_BX[i] + 4 + s * 5, 219, 4, 4, P_GRNOFF);
  }
  for (int s = 0; s < 10; s++) lcd.fillRect(8 + s * 9, 158, 8, 5, P_GRNOFF);
  lcd.setFont(&fonts::Font2); lcd.setTextColor(P_LAB, P_BG4);
  lcd.setCursor(5, 31); lcd.print("RPM");
  // ตัวเลขใต้แถบรอบ
  lcd.setFont(&fonts::Font0);
  for (int n = 1; n <= sm; n++) {
    int x = B4_X + (int)((long)B4_W * n / sm);
    uint16_t c = (n * 1000 >= maxRpm) ? C_RED : TFT_WHITE;
    lcd.drawFastVLine(x, 32, 4, c);
    char b[4]; snprintf(b, sizeof(b), "%d", n);
    lcd.setTextColor(c, P_BG4);
    lcd.setCursor(x - lcd.textWidth(b) / 2, 38); lcd.print(b);
  }
}

void p4Tile(int idx, int bx, bool have, float v, const char *fmt, float frac) {   // ช่องล่าง 5 ช่อง
  char b[8];
  if (have) snprintf(b, sizeof(b), fmt, v); else strcpy(b, "--");
  if (chgTxt(idx, b, P_WHT)) drawVal(bx + 3, 196, 56, 22, b, &fonts::FreeSansBold12pt7b, 1.0f, P_WHT, P_TILE4, false, 20);
  int lit = have ? (int)lroundf(constrain(frac, 0.0f, 1.0f) * 10.0f) : 0;
  if (chgBar(idx, lit)) for (int s = 0; s < 10; s++) lcd.fillRect(bx + 4 + s * 5, 219, 4, 4, s < lit ? P_GRN : P_GRNOFF);
}

void updatePage4(uint32_t now, float dt, bool over, bool fastChanged) {
  pSprEnsure();
  pdUpdate(dt);
  char b[10];
  // RPM ตัวเอียง + แถบแบ่งช่อง + จุดเตือนเปลี่ยนเกียร์
  snprintf(b, sizeof(b), "%d", ((int)lroundf(dispRpm) + 5) / 10 * 10);
  uint16_t rc = (over && fastBlink) ? C_RED : P_WHT;
  static uint32_t lastRt4 = 0;
  if (((now - lastRt4 >= 80) || rc != pCol[0]) && chgTxt(0, b, rc)) { lastRt4 = now; drawVal(2, 0, 98, 32, b, &fonts::FreeSansBoldOblique18pt7b, 1.2f, rc, P_BG4, false, 31); }
  int sm = gScaleMax();
  int lit = (int)lroundf(constrain(dispRpm / (1000.0f * sm), 0.0f, 1.0f) * B4_SEGS);
  int barKey = lit * 2 + ((over && fastBlink) ? 1 : 0);
  int mkSeg = (pkHoldRpm - dispRpm > 150.0f) ? constrain((int)lroundf(constrain(pkHoldRpm / (1000.0f * sm), 0.0f, 1.0f) * B4_SEGS) - 1, 0, B4_SEGS - 1) : -1;
  if (mkSeg >= 0 && mkSeg < lit) mkSeg = -1;               // ขีด peak อยู่ใต้ช่องที่สว่างอยู่แล้ว = ซ่อน
  bool barA = chgBar(0, barKey & 0x7F), barB = chgBar(11, mkSeg + 1);
  if (barA || barB) {
    bool flash = over && fastBlink;
    for (int s = 0; s < B4_SEGS; s++)
      lcd.fillRect(B4_X + s * 4, B4_Y, 3, B4_H, s < lit ? (flash ? C_RED : p4Grad(s / (float)(B4_SEGS - 1))) : (s == mkSeg ? (uint16_t)TFT_WHITE : lcd.color565(50, 12, 10)));
  }
  bool shiftOn = dispRpm >= maxRpm * 0.85f;
  int dk = over ? (fastBlink ? 2 : 3) : (shiftOn ? 1 : 0);
  if (chgBar(1, dk)) lcd.fillCircle(89, 39, 5, dk == 1 || dk == 2 ? (dk == 2 ? C_RED : lcd.color565(255, 160, 30)) : lcd.color565(60, 46, 20));

  // TRIP / FUEL
  float trip = demoMode ? demoTrip : tripKm;
  snprintf(b, sizeof(b), "%.1f", trip);
  if (chgTxt(1, b, P_WHT)) drawVal(8, 74, 92, 34, b, &fonts::FreeSansBold18pt7b, 1.0f, P_WHT, P_TILE4, false, 31);
  bool hf = metricHave[M_FUEL];
  if (hf) snprintf(b, sizeof(b), "%.0f", pd[M_FUEL]); else strcpy(b, "--");
  if (chgTxt(2, b, P_WHT)) drawVal(8, 130, 92, 28, b, &fonts::FreeSansBold18pt7b, 1.0f, P_WHT, P_TILE4, false, 26);
  int fl = hf ? (int)lroundf(constrain(pd[M_FUEL] / 100.0f, 0.0f, 1.0f) * 10.0f) : 0;
  if (chgBar(2, fl)) for (int s = 0; s < 10; s++) lcd.fillRect(8 + s * 9, 158, 8, 5, s < fl ? P_GRN : P_GRNOFF);

  // GEAR (ใหญ่ สีเหลือง) / SPEED (ใหญ่มาก)
  b[0] = gearChar(); b[1] = 0;
  if (chgTxt(3, b, P_YEL)) drawVal(110, 86, 55, 58, b, &fonts::FreeSansBold24pt7b, 1.6f, P_YEL, P_TILE4, true, 56);
  snprintf(b, sizeof(b), "%d", (int)lroundf(dispSpeed));
  if (chgTxt(4, b, P_WHT)) drawVal(200, 88, 112, 58, b, &fonts::FreeSansBold24pt7b, 1.4f, P_WHT, P_TILE4, false, 54);

  // ช่องล่าง 5 ช่อง
  p4Tile(5, T4_BX[0], true, pd[M_COOLANT] > 0 || metricHave[M_COOLANT] ? pd[M_COOLANT] : dispTemp, "%.0f", (pd[M_COOLANT] - 40.0f) / 90.0f);
  p4Tile(6, T4_BX[1], metricHave[M_IAT],  pd[M_IAT],  "%.0f", (pd[M_IAT] + 10.0f) / 90.0f);
  p4Tile(7, T4_BX[2], metricHave[M_BATT], pd[M_BATT], "%.1f", (pd[M_BATT] - 11.0f) / 4.0f);
  p4Tile(8, T4_BX[3], metricHave[M_TPS],  pd[M_TPS],  "%.0f", pd[M_TPS] / 100.0f);
  p4Tile(9, T4_BX[4], metricHave[M_LOAD], pd[M_LOAD], "%.0f", pd[M_LOAD] / 100.0f);

  const char *msg = over ? (fastBlink ? "REDLINE" : "") : curStatus();
  int key = over ? (fastBlink ? 1 : 2) : 0;
  if (msg != pStatPtr || key != pStatKey) {
    pStatPtr = msg; pStatKey = key;
    pStatusText(318, 29, true, msg, over ? C_RED : (strcmp(msg, "OBD OK") == 0 ? C_OK : C_WARN), P_BG4, 54);
  }
}

// ---- เดโม: จำลองการขับ (เร่ง 0-140 km/h เปลี่ยนเกียร์ -> ชน redline -> ลดความเร็วจนหยุด วนซ้ำ ~28 วินาที) ----
void demoTick(uint32_t now) {
  static uint32_t t0 = 0, lastT = 0; static bool wasOn = false; static int g = 0;
  if (!wasOn) { wasOn = true; t0 = now; lastT = now; demoTrip = 0; g = 0; }
  float t = (now - t0) / 1000.0f, dt = (now - lastT) / 1000.0f; lastT = now;
  float u = fmodf(t, 28.0f);
  float spd = (u < 19.0f) ? 140.0f * (u / 19.0f) : (u < 20.5f ? 140.0f : 140.0f * (1.0f - (u - 20.5f) / 7.5f));
  if (spd < 0) spd = 0;
  if (u < 0.05f) g = 0;
  float limit = min(6200.0f, (float)maxRpm - 300.0f);
  float r = spd * GEAR_RPM_PER_KMH[g];
  while (r > limit && g < gearCount - 1) { g++; r = spd * GEAR_RPM_PER_KMH[g]; }
  while (r < 1500.0f && g > 0 && spd > 5) { g--; r = spd * GEAR_RPM_PER_KMH[g]; if (r > limit) { g++; r = spd * GEAR_RPM_PER_KMH[g]; break; } }
  if (r < 850.0f) r = 850.0f + 40.0f * sinf(t * 3.0f);
  if (u >= 17.5f && u < 19.5f) r = maxRpm + 80.0f + 60.0f * sinf(t * 30.0f);      // ชนลิมิตรอบ = ทดสอบ redline
  float thr = (u < 19.5f) ? 28.0f + 57.0f * constrain((r - 1500.0f) / (limit - 1500.0f), 0.0f, 1.0f) : 4.0f;
  rpm = (int)r; speedKmh = (int)spd;
  coolant = (int)(60.0f + 28.0f * (1.0f - expf(-t / 15.0f)));
  iat = (int)(32.0f + 4.0f * sinf(t / 5.0f));
  float vals[N_METRICS];
  vals[M_COOLANT] = coolant; vals[M_IAT] = iat; vals[M_BATT] = 13.7f + 0.3f * sinf(t / 3.0f);
  vals[M_LOAD] = thr * 0.9f + 8.0f; vals[M_TPS] = thr; vals[M_MAP] = 35.0f + thr * 1.55f;
  vals[M_MAF] = 3.0f + thr * 1.6f;  vals[M_FUEL] = 62.0f - fmodf(t, 28.0f) * 0.05f;
  vals[M_STFT] = -1.6f + 2.2f * sinf(t / 2.3f);
  for (int m = 0; m < N_METRICS; m++) { metricVal[m] = vals[m]; metricHave[m] = true; }
  demoGear = (spd < 2.0f) ? 'N' : (char)('1' + g);
  demoTrip += spd * dt / 3600.0f;
}
void demoStop() {                                   // กลับสู่ค่าจริงจาก OBD
  demoMode = false;
  rpm = 0; speedKmh = 0;
  for (int m = 0; m < N_METRICS; m++) metricHave[m] = false;
}
// ======================================================================================
// หน้า 1-4 แบบใหม่: วาดด้วยกราฟิกทั้งหมด (ไม่ใช้รูปพื้นหลัง) พื้นดำ ตัวเลขวาดผ่านสไปรท์ = ไม่กะพริบ
//   หน้า 1 (PG_A) : ความเร็วมุมซ้ายบน + กราฟแท่งรอบทรงลาด + RPM ตัวเอียง + ช่องค่า 8 ช่อง + เกียร์กลาง
//   หน้า 2 (PG_B) : แถบรอบสีฟ้าด้านบน + ช่องสีเหลือง (น้ำ/อากาศ/โหลด/แอร์โฟลว์/น้ำมัน/รอบ/แบต/บูสต์/ทริป) + เกียร์กลาง
//   หน้า 3 (PG_C) : แท่งรอบโค้งแบบจอรถแข่ง + ความเร็วใหญ่ + ตารางค่าตัวเลขเซเว่นเซกเมนต์
//   หน้า 4 (PG_D) : เกจครึ่งวงแหวน + เกียร์กลางวง + กล่องความเร็ว + บูสต์ + แบต + แถบน้ำ
// ======================================================================================
uint16_t mix565(uint16_t a, uint16_t b, float f) {                // ผสมสี a -> b ตามสัดส่วน f (ใช้กับปลายแถบ sub-pixel)
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31, br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  int r = ar + (int)lroundf((br - ar) * f), g = ag + (int)lroundf((bg - ag) * f), bl = ab + (int)lroundf((bb - ab) * f);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}
uint16_t N_ORG, N_GRY, N_NAVY, N_NAVYE, N_LBLUE, N_YEL, N_BLUE, N_BARDK, N_BARRED, N_OLIVE, N_DKRED, N_CYEL, N_CLINE, N_CWHT, N_CUNL, N_ARCNAVY, N_RIM, N_RIMRED, N_MARK, N_GLAB, N_BOXBG, N_BOXEDGE;
void initNewColors() {
  N_ORG   = lcd.color565(255, 172, 30);   N_GRY    = lcd.color565(206, 206, 212);
  N_NAVY  = lcd.color565(14, 38, 86);     N_NAVYE  = lcd.color565(80, 130, 210);
  N_LBLUE = lcd.color565(100, 170, 232);  N_YEL    = lcd.color565(255, 204, 0);
  N_BLUE  = lcd.color565(34, 104, 232);   N_BARDK  = lcd.color565(18, 20, 28);
  N_BARRED= lcd.color565(64, 10, 12);     N_OLIVE  = lcd.color565(56, 56, 22);
  N_DKRED = lcd.color565(72, 22, 24);     N_CYEL   = lcd.color565(228, 228, 60);
  N_CLINE = lcd.color565(40, 110, 225);   N_CWHT   = lcd.color565(236, 236, 246);
  N_CUNL  = lcd.color565(42, 42, 48);     N_ARCNAVY= lcd.color565(22, 52, 84);
  N_RIM   = lcd.color565(128, 128, 136);  N_RIMRED = lcd.color565(200, 40, 40);
  N_MARK  = lcd.color565(255, 92, 60);    N_GLAB   = lcd.color565(170, 180, 205);
  N_BOXBG = lcd.color565(4, 10, 22);      N_BOXEDGE= lcd.color565(70, 110, 150);
}

// ---- ตัวช่วย ----
float fitSc(const lgfx::IFont *f, const char *s, float sc, int maxW) {      // ย่อขนาดตัวอักษรให้พอดีช่อง
  lcd.setFont(f); lcd.setTextSize(1.0f);
  int w = lcd.textWidth(s);
  lcd.setTextSize(1.0f);
  if (w > 0 && w * sc > maxW) sc = (float)maxW / (float)w;
  return sc;
}
// วาดค่าลงกล่อง (เฉพาะเมื่อข้อความ/สีเปลี่ยน) i = ช่องแคช 0..15
void valBox(int i, const char *s, uint16_t fg, int x, int y, int w, int h, const lgfx::IFont *f, float sc, uint16_t bg, bool center, int base) {
  if (!chgTxt(i, s, fg)) return;
  drawVal(x, y, w, h, s, f, fitSc(f, s, sc, w - 2), fg, bg, center, base);
}
void fmtV(char *b, bool have, float v, int dec) {
  if (!have) { strcpy(b, "--"); return; }
  snprintf(b, 8, dec ? "%.1f" : "%.0f", v);
  if (strcmp(b, "-0") == 0) strcpy(b, "0");
  if (strcmp(b, "-0.0") == 0) strcpy(b, "0.0");
}
void fmtRpm(char *b) { snprintf(b, 8, "%d", ((int)lroundf(dispRpm) + 5) / 10 * 10); }
bool  coolHave() { return metricHave[M_COOLANT]; }
uint16_t coolCol(uint16_t normal) { return (coolHave() && coolant >= tempAlert) ? (blinkOn ? C_RED : C_WARN) : normal; }
uint16_t battCol(uint16_t normal) { return (metricHave[M_BATT] && pd[M_BATT] < 11.8f) ? C_RED : normal; }
float boostBar() { return (pd[M_MAP] - 101.3f) / 100.0f; }
void menuLabel() {}                                   // เปิดเมนูด้วยการปัดขึ้น (ไม่แสดงปุ่ม MENU แล้ว)
void txtAt(const char *s, int x, int y, const lgfx::IFont *f, uint16_t col, lgfx::textdatum_t d, float sc = 1.0f) {
  lcd.setFont(f); lcd.setTextSize(sc); lcd.setTextColor(col); lcd.setTextDatum(d);
  lcd.drawString(s, x, y);
  lcd.setTextSize(1.0f); lcd.setTextDatum(lgfx::textdatum_t::top_left);
}
void nStatus(int x, int y, bool right, uint16_t bg, int pad) {         // OBD OK / NO OBD / REDLINE
  bool over = rpm >= maxRpm;
  const char *msg = over ? (fastBlink ? "REDLINE" : "") : curStatus();
  int key = over ? (fastBlink ? 1 : 2) : 0;
  if (msg == pStatPtr && key == pStatKey) return;
  pStatPtr = msg; pStatKey = key;
  pStatusText(x, y, right, msg, over ? C_RED : (strcmp(msg, "OBD OK") == 0 ? C_OK : C_WARN), bg, pad);
}
// แคชของแถบรอบ (วาดใหม่เฉพาะส่วนที่เปลี่ยน)
int   nLit = -1, nFlash = -1, nWBar = -1;
float nArcA = -999;

// =============================== หน้า 1 (PG_A) ===============================
const int A_X0 = 62, A_X1 = 244, A_BASE = 76, A_TOPL = 62, A_TOPR = 24, A_PITCH = 4;
const int A_NB = (A_X1 - A_X0) / A_PITCH;            // 47 แท่ง
int aKneeX() { return A_X0 + (int)((long)(A_X1 - A_X0) * 3000 / (gScaleMax() * 1000L)); }   // ทางลาดจบที่ 3000 รอบ
int aTopY(int x) {
  int k = aKneeX();
  if (x >= k) return A_TOPR;
  return A_TOPL - (A_TOPL - A_TOPR) * (x - A_X0) / max(1, k - A_X0);
}
const int A_TX[4] = {4, 66, 196, 258}, A_TY[2] = {114, 172}, A_TW = 58;
const char *const A_LAB[8] = {"WATER", "AIR.T", "BOOST.P", "AIR.FLOW", "CALC.LOAD", "THRO.P", "FUEL.L", "BAT"};
const char *const A_LO[8]  = {"40", "10", "0.0", "0", "0", "0", "0", "10.0"};
const char *const A_HI[8]  = {"120", "60", "2.5", "655", "100", "100", "100", "16.0"};
const bool A_ORANGE[8] = {false, true, true, true, false, true, false, true};

void aBar(int i, float lit, bool flash) {                // lit 0..1 (เศษ = แท่งปลายสว่างบางส่วน)
  int x = A_X0 + i * A_PITCH, top = aTopY(x + 1);
  float r = (i + 0.5f) / A_NB * gScaleMax() * 1000.0f;
  uint16_t on = (flash || r >= maxRpm) ? C_RED : N_ORG;
  uint16_t c = lit >= 1.0f ? on : (lit <= 0.0f ? N_GRY : mix565(N_GRY, on, lit));
  lcd.fillRect(x, top, A_PITCH - 1, A_BASE - top, c);
}
// ตัวช่วยแถบแบบแท่ง: litF = จำนวนแท่งที่สว่าง (มีเศษ) -> key รวมเศษ 8 ระดับ, วาดใหม่เฉพาะแท่งที่เปลี่ยน
template <typename F> void barsUpdate(float litF, int nb, bool flash, F draw) {
  litF = constrain(litF, 0.0f, (float)nb);
  int full = (int)litF, q = (int)((litF - full) * 8.0f);
  int key = full * 8 + q;
  auto fr = [&](int i) { return i < full ? 1.0f : (i == full ? q / 8.0f : 0.0f); };
  if (nFlash != (int)flash || nLit < 0) { for (int i = 0; i < nb; i++) draw(i, fr(i), flash); }
  else if (key != nLit) {
    int of = nLit / 8;
    for (int i = min(of, full); i <= max(of, full) && i < nb; i++) draw(i, fr(i), flash);
  }
  nLit = key; nFlash = flash;
}

void aStatic() {
  lcd.fillScreen(TFT_BLACK);
  int sm = gScaleMax(), k = aKneeX();
  for (int t = 0; t < 2; t++) {                                               // เส้นขอบบนของกราฟ (หนา 2 px)
    lcd.drawLine(A_X0 - 2, A_TOPL - 3 + t, k, A_TOPR - 3 + t, TFT_WHITE);
    lcd.drawFastHLine(k, A_TOPR - 3 + t, A_X1 - k, TFT_WHITE);
  }
  lcd.fillRect(A_X0 - 4, A_BASE + 1, A_X1 - A_X0 + 4, 2, TFT_WHITE);         // เส้นฐาน
  for (int n = 1; n <= sm; n++) {                                             // ตัวเลขรอบตามแนวขอบบน
    int x = A_X0 + (A_X1 - A_X0) * n / sm, ty = aTopY(min(x, A_X1 - 1)) - 3;
    lcd.drawFastVLine(x, ty - 2, 3, TFT_WHITE);
    char b[4]; snprintf(b, sizeof(b), "%d", n);
    txtAt(b, x, ty - 4, &fonts::FreeSans9pt7b, (n * 1000 >= maxRpm) ? C_RED : TFT_WHITE, lgfx::textdatum_t::bottom_center);
  }
  for (int i = 0; i < A_NB; i++) aBar(i, 0.0f, false);
  txtAt("RPM", 252, 80, &fonts::Font2, C_RED, lgfx::textdatum_t::top_right);
  txtAt("Km/h", 6, 32, &fonts::Font2, TFT_WHITE, lgfx::textdatum_t::top_left);
  lcd.fillRect(0, 108, 320, 2, TFT_WHITE);
  for (int i = 0; i < 8; i++) {                                               // ช่องค่า 8 ช่อง
    int x = A_TX[i % 4], y = A_TY[i / 4];
    lcd.setFont(&fonts::Font0); lcd.setTextColor(TFT_WHITE);
    lcd.setTextDatum(lgfx::textdatum_t::top_left);  lcd.drawString(A_LO[i], x + 1, y);
    lcd.setTextDatum(lgfx::textdatum_t::top_right); lcd.drawString(A_HI[i], x + A_TW - 1, y);
    lcd.setTextDatum(lgfx::textdatum_t::top_left);
    lcd.fillRect(x, y + 8, A_TW, 30, A_ORANGE[i] ? N_ORG : TFT_BLACK);
    lcd.drawRect(x, y + 8, A_TW, 30, TFT_WHITE);
    lcd.fillRect(x, y + 40, A_TW, 14, N_NAVY);
    lcd.drawRect(x, y + 40, A_TW, 14, N_NAVYE);
    txtAt(A_LAB[i], x + A_TW / 2, y + 47, &fonts::Font0, TFT_WHITE, lgfx::textdatum_t::middle_center);
  }
  lcd.fillRoundRect(129, 113, 62, 112, 6, N_LBLUE);
  lcd.drawRoundRect(129, 113, 62, 112, 6, TFT_WHITE);
  menuLabel();
}

void aTile(int i, const char *s, uint16_t whiteCol) {
  int x = A_TX[i % 4], y = A_TY[i / 4];
  bool o = A_ORANGE[i];
  uint16_t fg = o ? (whiteCol == C_TXT ? (uint16_t)TFT_BLACK : whiteCol) : whiteCol;
  valBox(2 + i, s, fg, x + 2, y + 9, A_TW - 4, 28, &fonts::FreeSansBold18pt7b, 1.0f, o ? N_ORG : (uint16_t)TFT_BLACK, false, 25);
}

void updateA(uint32_t now, bool over) {
  char b[8];
  bool flash = over && fastBlink;
  barsUpdate(dispRpm / (gScaleMax() * 1000.0f) * A_NB, A_NB, flash, aBar);

  snprintf(b, 8, "%d", (int)lroundf(dispSpeed));
  valBox(0, b, TFT_WHITE, 2, 0, 60, 31, &fonts::FreeSansBoldOblique18pt7b, 1.0f, TFT_BLACK, false, 28);
  fmtRpm(b);
  static uint32_t lastR = 0;
  uint16_t rc = flash ? C_RED : (uint16_t)TFT_WHITE;
  if (now - lastR >= 80 || rc != pCol[1]) {
    lastR = now;
    valBox(1, b, rc, 100, 80, 120, 27, &fonts::FreeSansBoldOblique18pt7b, 1.0f, TFT_BLACK, true, 26);
  }
  fmtV(b, coolHave(), pd[M_COOLANT], 0);            aTile(0, b, coolCol(C_TXT));
  fmtV(b, metricHave[M_IAT], pd[M_IAT], 0);         aTile(1, b, C_TXT);
  fmtV(b, metricHave[M_MAP], boostBar(), 1);        aTile(2, b, C_TXT);
  fmtV(b, metricHave[M_MAF], pd[M_MAF], 0);         aTile(3, b, C_TXT);
  fmtV(b, metricHave[M_LOAD], pd[M_LOAD], 1);       aTile(4, b, C_TXT);
  fmtV(b, metricHave[M_TPS], pd[M_TPS], 1);         aTile(5, b, C_TXT);
  fmtV(b, metricHave[M_FUEL], pd[M_FUEL], 0);       aTile(6, b, C_TXT);
  fmtV(b, metricHave[M_BATT], pd[M_BATT], 1);       aTile(7, b, battCol(C_TXT));
  b[0] = gearChar(); b[1] = 0;
  valBox(10, b, TFT_WHITE, 132, 118, 56, 102, &fonts::FreeSansBold24pt7b, 2.0f, N_LBLUE, true, 86);
  nStatus(4, 88, false, TFT_BLACK, 74);
}

// =============================== หน้า 2 (PG_B) ===============================
const int BB_X = 5, BB_Y = 19, BB_W = 240, BB_H = 14;
void bBarSpan(int x0, int x1, int litW, bool flash) {          // วาดแถบช่วงพิกเซล [x0,x1) (ไล่สีเป็นช่วง ๆ)
  int xr = (int)(BB_W * constrain(maxRpm / (gScaleMax() * 1000.0f), 0.0f, 1.0f));
  int cuts[4] = {x0, constrain(litW, x0, x1), constrain(xr, x0, x1), x1};
  if (cuts[2] < cuts[1]) { int t = cuts[1]; cuts[1] = cuts[2]; cuts[2] = t; }
  for (int s = 0; s < 3; s++) {
    int a = cuts[s], e = cuts[s + 1];
    if (e <= a) continue;
    bool lit = a < litW, red = a >= xr;
    uint16_t c = lit ? ((flash || red) ? C_RED : N_BLUE) : (red ? N_BARRED : N_BARDK);
    lcd.fillRect(BB_X + a, BB_Y, e - a, BB_H, c);
  }
}
// แถบต่อเนื่อง: ปลายแถบ 1 คอลัมน์ผสมสีตามเศษพิกเซล (8 ระดับ) -> แถบเลื่อนดูต่อเนื่องแม้ขยับไม่ถึง 1 px
// span(x0,x1,litW,flash) วาดช่วงพิกเซลแบบเต็ม, colAt(x,lit,flash) สีของคอลัมน์ x
template <typename S, typename C> void contBarUpdate(float wF, int W, bool flash, int bx, int by, int bh, S span, C colAt) {
  wF = constrain(wF, 0.0f, (float)W);
  int full = (int)wF, q = (int)((wF - full) * 8.0f);
  int key = full * 8 + q;
  if (nFlash != (int)flash || nLit < 0) span(0, W, full, flash);
  else if (key != nLit) { int of = nLit / 8; span(min(of, full), min(max(of, full) + 1, W), full, flash); }
  else return;
  if (q > 0 && full < W) lcd.drawFastVLine(bx + full, by, bh, mix565(colAt(full, false, flash), colAt(full, true, flash), q / 8.0f));
  nLit = key; nFlash = flash;
}
uint16_t bColAt(int x, bool lit, bool flash) {
  int xr = (int)(BB_W * constrain(maxRpm / (gScaleMax() * 1000.0f), 0.0f, 1.0f));
  bool red = x >= xr;
  return lit ? ((flash || red) ? C_RED : N_BLUE) : (red ? N_BARRED : N_BARDK);
}
struct BTile { int x, y, w, h; const char *lab; };
const BTile BT[9] = {
  {4, 80, 56, 54, "WATER T"}, {62, 80, 56, 54, "AIR T"}, {4, 136, 56, 54, "ENG LOAD"}, {62, 136, 56, 54, "AIRFLOW"},
  {4, 194, 114, 28, "FUEL %"}, {202, 80, 114, 54, "r/min"}, {202, 136, 56, 54, "BATT V"}, {260, 136, 56, 54, "BOOST"},
  {202, 194, 114, 28, "TRIP km"}};

void bStatic() {
  lcd.fillScreen(TFT_BLACK);
  int sm = gScaleMax();
  for (int n = 1; n <= sm; n++) {
    int x = BB_X + BB_W * n / sm;
    char b[4]; snprintf(b, sizeof(b), "%d", n);
    uint16_t c = (n * 1000 >= maxRpm) ? C_RED : TFT_WHITE;
    txtAt(b, min(x, 312), 1, &fonts::FreeSansBold9pt7b, c, lgfx::textdatum_t::top_center);
    lcd.drawFastVLine(min(x, BB_X + BB_W - 1), 15, 3, c);
  }
  lcd.drawRect(BB_X - 1, BB_Y - 1, BB_W + 2, BB_H + 2, lcd.color565(90, 90, 100));
  bBarSpan(0, BB_W, 0, false);
  lcd.fillRoundRect(104, 36, 112, 33, 4, N_YEL);
  txtAt("Km/h", 220, 45, &fonts::Font2, TFT_WHITE, lgfx::textdatum_t::top_left);
  txtAt("GEAR", 160, 71, &fonts::Font0, TFT_WHITE, lgfx::textdatum_t::top_center);
  lcd.fillRoundRect(122, 80, 76, 115, 5, N_BLUE);
  lcd.drawRoundRect(122, 80, 76, 115, 5, TFT_WHITE);
  for (int i = 0; i < 9; i++) {
    const BTile &t = BT[i];
    lcd.fillRoundRect(t.x, t.y, t.w, t.h, 4, N_YEL);
    lcd.setFont(&fonts::Font2);
    float sc = fitSc(&fonts::Font2, t.lab, 1.0f, (t.h < 40 ? 48 : t.w - 6));
    txtAt(t.lab, t.x + 4, t.y + 2, &fonts::Font2, TFT_BLACK, lgfx::textdatum_t::top_left, sc);
  }
  menuLabel();
}

void bTile(int i, const char *s, uint16_t col, float sc) {
  const BTile &t = BT[i];
  int vx = (i == 8) ? 60 : 50;                          // TRIP km ป้ายยาวกว่า
  if (t.h < 40) valBox(3 + i, s, col, t.x + vx, t.y + 2, t.w - vx - 4, t.h - 4, &fonts::FreeSansBold18pt7b, sc, N_YEL, false, 22);
  else          valBox(3 + i, s, col, t.x + 3, t.y + 18, t.w - 6, t.h - 21, &fonts::FreeSansBold18pt7b, sc, N_YEL, false, 30);
}

void updateB(uint32_t now, bool over) {
  char b[8];
  bool flash = over && fastBlink;
  contBarUpdate(dispRpm / (gScaleMax() * 1000.0f) * BB_W, BB_W, flash, BB_X, BB_Y, BB_H, bBarSpan, bColAt);

  snprintf(b, 8, "%d", (int)lroundf(dispSpeed));
  valBox(0, b, TFT_BLACK, 107, 38, 106, 29, &fonts::FreeSansBoldOblique18pt7b, 1.1f, N_YEL, true, 27);
  b[0] = gearChar(); b[1] = 0;
  valBox(1, b, TFT_WHITE, 125, 84, 70, 107, &fonts::FreeSansBold24pt7b, 2.2f, N_BLUE, true, 91);
  fmtV(b, coolHave(), pd[M_COOLANT], 0);       bTile(0, b, (coolHave() && coolant >= tempAlert) ? (blinkOn ? C_RED : (uint16_t)TFT_BLACK) : (uint16_t)TFT_BLACK, 1.2f);
  fmtV(b, metricHave[M_IAT], pd[M_IAT], 0);    bTile(1, b, TFT_BLACK, 1.2f);
  fmtV(b, metricHave[M_LOAD], pd[M_LOAD], 0);  bTile(2, b, TFT_BLACK, 1.2f);
  fmtV(b, metricHave[M_MAF], pd[M_MAF], 1);    bTile(3, b, TFT_BLACK, 1.2f);
  fmtV(b, metricHave[M_FUEL], pd[M_FUEL], 0);  bTile(4, b, TFT_BLACK, 1.0f);
  fmtRpm(b);
  static uint32_t lastR = 0;
  uint16_t rc = flash ? C_RED : (uint16_t)TFT_BLACK;
  if (now - lastR >= 80 || rc != pCol[8]) { lastR = now; bTile(5, b, rc, 1.3f); }
  fmtV(b, metricHave[M_BATT], pd[M_BATT], 1);  bTile(6, b, battCol(TFT_BLACK), 1.2f);
  fmtV(b, metricHave[M_MAP], boostBar(), 1);   bTile(7, b, TFT_BLACK, 1.2f);
  fmtV(b, true, demoMode ? demoTrip : tripKm, 1); bTile(8, b, TFT_BLACK, 1.0f);
  nStatus(124, 204, false, TFT_BLACK, 74);
}

// =============================== หน้า 3 (PG_C) ===============================
const int C_EX = 108, C_EY = 196, C_RXI = 47, C_RYI = 118, C_RXO = 107, C_RYO = 190;
const int C_NC = 20;                                   // แท่งในส่วนโค้ง (0-3000 รอบ)
const int C_SX0 = 109, C_SX1 = 263, C_SP = 6;          // ส่วนตรง: x เริ่ม/จบ, ระยะห่างแท่ง
const int C_NS = (C_SX1 - C_SX0) / C_SP;               // 25 แท่ง
const int C_NB = C_NC + C_NS;
float cBarRpm(int i) {                                 // รอบที่ปลายแท่ง i
  if (i < C_NC) return 3000.0f * (i + 1) / C_NC;
  return 3000.0f + (gScaleMax() * 1000.0f - 3000.0f) * (i - C_NC + 1) / C_NS;
}
int cLitCount(float r) {
  if (r <= 3000.0f) return (int)lroundf(constrain(r / 3000.0f, 0.0f, 1.0f) * C_NC);
  return C_NC + (int)lroundf(constrain((r - 3000.0f) / (gScaleMax() * 1000.0f - 3000.0f), 0.0f, 1.0f) * C_NS);
}
float cLitF(float r) {                                 // จำนวนแท่งที่สว่าง (มีเศษ)
  if (r <= 3000.0f) return constrain(r / 3000.0f, 0.0f, 1.0f) * C_NC;
  return C_NC + constrain((r - 3000.0f) / (gScaleMax() * 1000.0f - 3000.0f), 0.0f, 1.0f) * C_NS;
}
void cBar(int i, float litf, bool flash) {
  bool red = cBarRpm(i) > maxRpm + 1;
  bool lit = litf > 0.0f;
  if (i < C_NC) {
    float a0 = (180.0f + 90.0f * i / C_NC + 0.9f) * 0.0174533f, a1 = (180.0f + 90.0f * (i + 1) / C_NC - 0.9f) * 0.0174533f;
    int ix0 = C_EX + (int)lroundf(C_RXI * cosf(a0)), iy0 = C_EY + (int)lroundf(C_RYI * sinf(a0));
    int ix1 = C_EX + (int)lroundf(C_RXI * cosf(a1)), iy1 = C_EY + (int)lroundf(C_RYI * sinf(a1));
    int ox0 = C_EX + (int)lroundf(C_RXO * cosf(a0)), oy0 = C_EY + (int)lroundf(C_RYO * sinf(a0));
    int ox1 = C_EX + (int)lroundf(C_RXO * cosf(a1)), oy1 = C_EY + (int)lroundf(C_RYO * sinf(a1));
    uint16_t c = lit ? ((flash || red) ? C_RED : N_CWHT) : N_CUNL;
    if (lit && litf < 1.0f) c = mix565(N_CUNL, c, litf);
    lcd.fillTriangle(ix0, iy0, ox0, oy0, ox1, oy1, c);
    lcd.fillTriangle(ix0, iy0, ox1, oy1, ix1, iy1, c);
  } else {
    int x = C_SX0 + (i - C_NC) * C_SP;
    uint16_t off = red ? N_DKRED : N_OLIVE;
    uint16_t c = lit ? ((flash || red) ? C_RED : N_CYEL) : off;
    if (lit && litf < 1.0f) c = mix565(off, c, litf);
    lcd.fillRect(x, C_EY - C_RYO, C_SP - 1, C_RYO - C_RYI - 2, c);
  }
}
const int C_COL[2] = {182, 252}, C_COLW[2] = {64, 66}, C_ROWY[3] = {99, 143, 187};
const char *const C_NAME[6] = {"WATER", "RPM", "BATT", "TPS", "BOOST", "TRIP"};
const char *const C_UNIT[6] = {"C", "", "V", "%", "bar", "km"};

void cStatic() {
  lcd.fillScreen(TFT_BLACK);
  int sm = gScaleMax();
  for (int i = 0; i < C_NB; i++) cBar(i, 0.0f, false);
  for (int t = 0; t < 2; t++) {                                 // เส้นสีฟ้าใต้แท่ง
    int rx = C_RXI - 4 + t, ry = C_RYI - 4 + t, px = -1, py = -1;
    for (int d = 180; d <= 270; d += 3) {
      float a = d * 0.0174533f;
      int x = C_EX + (int)lroundf(rx * cosf(a)), y = C_EY + (int)lroundf(ry * sinf(a));
      if (px >= 0) lcd.drawLine(px, py, x, y, N_CLINE);
      px = x; py = y;
    }
    lcd.drawFastHLine(C_EX, C_EY - ry, 320 - C_EX, N_CLINE);
  }
  for (int n = 0; n <= 3; n++) {                                // ตัวเลขในส่วนโค้ง 0-3
    float a = (180.0f + 30.0f * n) * 0.0174533f;
    char b[4]; snprintf(b, sizeof(b), "%d", n);
    txtAt(b, C_EX + (int)(33 * cosf(a)), C_EY + (int)(100 * sinf(a)), &fonts::FreeSansBoldOblique12pt7b, TFT_WHITE, lgfx::textdatum_t::middle_center);
  }
  for (int n = 4; n <= sm; n++) {                               // ตัวเลขส่วนตรง (เกิน 6 แสดงเฉพาะเลขคู่)
    if (n > 6 && (n & 1)) continue;
    int x = C_SX0 + (C_SX1 - C_SX0) * (n - 3) / (sm - 3);
    char b[4]; snprintf(b, sizeof(b), "%d", n);
    txtAt(b, min(x, 306), 90, &fonts::FreeSansBoldOblique12pt7b, (n * 1000 >= maxRpm) ? C_RED : TFT_WHITE, lgfx::textdatum_t::middle_center);
  }
  for (int c = 0; c < 2; c++)
    for (int r = 0; r < 3; r++) {
      int x = C_COL[c], y = C_ROWY[r], i = r * 2 + c;
      txtAt(C_NAME[i], x, y, &fonts::Font2, N_GLAB, lgfx::textdatum_t::top_left);
      if (C_UNIT[i][0]) txtAt(C_UNIT[i], x + C_COLW[c], y, &fonts::Font2, N_GLAB, lgfx::textdatum_t::top_right);
      if (r < 2) lcd.drawFastHLine(x, y + 41, C_COLW[c], N_CLINE);
    }
  txtAt("KM/H", 138, 170, &fonts::FreeSansBold9pt7b, TFT_WHITE, lgfx::textdatum_t::middle_center);
  txtAt("GEAR", 138, 214, &fonts::Font0, N_GLAB, lgfx::textdatum_t::top_center);
  txtAt("x1000 RPM", 4, 226, &fonts::Font0, N_GLAB, lgfx::textdatum_t::top_left);
  menuLabel();
}

void cVal(int i, const char *s, uint16_t col) {
  int c = i % 2, r = i / 2;
  valBox(2 + i, s, col, C_COL[c], C_ROWY[r] + 14, C_COLW[c], 26, &fonts::Font7, 0.52f, TFT_BLACK, false, 26);
}

void updateC(uint32_t now, bool over) {
  char b[8];
  bool flash = over && fastBlink;
  barsUpdate(cLitF(dispRpm), C_NB, flash, cBar);

  snprintf(b, 8, "%d", (int)lroundf(dispSpeed));
  valBox(0, b, TFT_WHITE, 100, 106, 80, 54, &fonts::FreeSansBold24pt7b, 1.5f, TFT_BLACK, true, 52);
  b[0] = gearChar(); b[1] = 0;
  valBox(1, b, N_LBLUE, 118, 182, 40, 30, &fonts::FreeSansBold18pt7b, 1.1f, TFT_BLACK, true, 29);
  fmtV(b, coolHave(), pd[M_COOLANT], 0);       cVal(0, b, coolCol(TFT_WHITE));
  fmtRpm(b);
  static uint32_t lastR = 0;
  uint16_t rc = flash ? C_RED : (uint16_t)TFT_WHITE;
  if (now - lastR >= 80 || rc != pCol[3]) { lastR = now; cVal(1, b, rc); }
  fmtV(b, metricHave[M_BATT], pd[M_BATT], 1);  cVal(2, b, battCol(TFT_WHITE));
  fmtV(b, metricHave[M_TPS], pd[M_TPS], 0);    cVal(3, b, TFT_WHITE);
  fmtV(b, metricHave[M_MAP], boostBar(), 1);   cVal(4, b, TFT_WHITE);
  fmtV(b, true, demoMode ? demoTrip : tripKm, 1); cVal(5, b, TFT_WHITE);
  nStatus(4, 204, false, TFT_BLACK, 74);
}

// =============================== หน้า 4 (PG_D) ===============================
const int D_CX = 112, D_CY = 130, D_RI = 74, D_RO = 100, D_RIM0 = 101, D_RIM1 = 106;
const float D_A0 = 80.0f, D_A1 = 285.0f;
float dAng(float r) { return D_A0 + (D_A1 - D_A0) * constrain(r / (gScaleMax() * 1000.0f), 0.0f, 1.0f); }
const int DW_X = 246, DW_Y = 211, DW_W = 70, DW_H = 13;   // แถบน้ำ 0-120 องศา

void dBandSpan(float lo, float hi, float aNow, bool flash) {      // วาดแถบวงแหวนช่วงมุม [lo,hi]
  lo = max(lo, D_A0); hi = min(hi, D_A1);
  if (hi <= lo) return;
  float aRed = dAng(maxRpm);
  float cut[4] = {lo, constrain(aNow, lo, hi), constrain(aRed, lo, hi), hi};
  if (cut[2] < cut[1]) { float t = cut[1]; cut[1] = cut[2]; cut[2] = t; }
  for (int s = 0; s < 3; s++) {
    float a = cut[s], e = cut[s + 1];
    if (e - a < 0.05f) continue;
    float mid = (a + e) * 0.5f;
    bool lit = mid < aNow, red = mid >= aRed;
    uint16_t c = lit ? ((flash || red) ? C_RED : P_BAND) : (red ? N_DKRED : N_ARCNAVY);
    lcd.fillArc(D_CX, D_CY, D_RI, D_RO, a, e, c);
  }
}
void dWater(int px) {
  lcd.fillRect(DW_X, DW_Y, px, DW_H, lcd.color565(48, 140, 232));
  lcd.fillRect(DW_X + px, DW_Y, DW_W - px, DW_H, lcd.color565(44, 46, 52));
}

void dStatic() {
  lcd.fillScreen(TFT_BLACK);
  int sm = gScaleMax();
  float aRed = dAng(maxRpm);
  lcd.fillArc(D_CX, D_CY, D_RIM0, D_RIM1, D_A0, aRed, N_RIM);
  if (aRed < D_A1) lcd.fillArc(D_CX, D_CY, D_RIM0 - 1, D_RIM1 + 2, aRed, D_A1, N_RIMRED);
  for (int n = 0; n <= sm * 2; n++) {                           // ขีดบนขอบทุก 500 รอบ
    float a = dAng(n * 500.0f) * 0.0174533f, ca = cosf(a), sa = sinf(a);
    int r0 = (n % 2 == 0) ? D_RIM0 - 1 : D_RIM0 + 1;
    lcd.drawLine(D_CX + (int)(ca * r0), D_CY + (int)(sa * r0), D_CX + (int)(ca * D_RIM1), D_CY + (int)(sa * D_RIM1), TFT_WHITE);
  }
  dBandSpan(D_A0, D_A1, D_A0, false);
  for (int n = 0; n <= sm; n++) {                               // ตัวเลขในวง (เกิน 6 แสดงเฉพาะเลขคู่)
    if (n > 6 && (n & 1)) continue;
    float a = dAng(n * 1000.0f) * 0.0174533f;
    char b[4]; snprintf(b, sizeof(b), "%d", n);
    txtAt(b, D_CX + (int)(60 * cosf(a)), D_CY + (int)(60 * sinf(a)), &fonts::FreeSansBoldOblique12pt7b,
          (n * 1000 >= maxRpm) ? C_RED : TFT_WHITE, lgfx::textdatum_t::middle_center);
  }
  txtAt("GEAR", 106, 150, &fonts::Font0, N_GLAB, lgfx::textdatum_t::top_center);
  txtAt("x1000 RPM", 4, 226, &fonts::Font0, N_GLAB, lgfx::textdatum_t::top_left);
  txtAt("BOOST", 184, 10, &fonts::Font2, N_GLAB, lgfx::textdatum_t::top_left);
  txtAt("bar", 236, 10, &fonts::Font2, N_GLAB, lgfx::textdatum_t::top_left);
  lcd.fillRoundRect(167, 66, 150, 100, 8, N_BOXBG);
  lcd.drawRoundRect(167, 66, 150, 100, 8, N_BOXEDGE);
  lcd.drawRoundRect(168, 67, 148, 98, 7, N_BOXEDGE);
  txtAt("KM/H", 309, 160, &fonts::FreeSansBold9pt7b, TFT_WHITE, lgfx::textdatum_t::bottom_right);
  lcd.drawFastHLine(168, 172, 150, lcd.color565(60, 80, 110));
  txtAt("BATT", 172, 177, &fonts::Font2, N_GLAB, lgfx::textdatum_t::top_left);
  txtAt("V", 222, 177, &fonts::Font2, N_GLAB, lgfx::textdatum_t::top_left);
  txtAt("WATER", 246, 177, &fonts::Font2, N_GLAB, lgfx::textdatum_t::top_left);
  lcd.setFont(&fonts::Font0); lcd.setTextColor(TFT_WHITE);
  lcd.setTextDatum(lgfx::textdatum_t::top_left);   lcd.drawString("0", DW_X, 196);
  lcd.setTextDatum(lgfx::textdatum_t::top_center); lcd.drawString("60", DW_X + DW_W / 2, 196);
  lcd.setTextColor(C_RED);
  lcd.setTextDatum(lgfx::textdatum_t::top_right);  lcd.drawString("120", DW_X + DW_W, 196);
  lcd.setTextDatum(lgfx::textdatum_t::top_left);
  for (int k = 0; k <= 12; k++) lcd.drawFastVLine(DW_X + DW_W * k / 12 - (k == 12), 205, (k % 6 == 0) ? 4 : 2, TFT_WHITE);
  dWater(0);
  menuLabel();
}

void updateD(uint32_t now, bool over) {
  char b[8];
  bool flash = over && fastBlink;
  float a = dAng(dispRpm);
  if (nFlash != (int)flash || nArcA < -900) { dBandSpan(D_A0, D_A1, a, flash); nArcA = -999; }
  if (nArcA < -900 || fabsf(a - nArcA) > 0.25f) {
    if (nArcA > -900) dBandSpan(min(a, nArcA) - 1.6f, max(a, nArcA) + 1.6f, a, flash);
    if (a > D_A0 + 1.0f) lcd.fillArc(D_CX, D_CY, D_RI, D_RO, a - 0.9f, min(a + 0.9f, D_A1), N_MARK);   // ขีดแดงปลายแถบ
    nArcA = a;
  }
  nFlash = flash;

  b[0] = gearChar(); b[1] = 0;
  valBox(0, b, TFT_WHITE, 84, 102, 46, 46, &fonts::FreeSansBold24pt7b, 1.2f, TFT_BLACK, true, 42);
  fmtV(b, metricHave[M_MAP], boostBar(), 1);
  valBox(1, b, TFT_WHITE, 180, 28, 100, 34, &fonts::FreeSansBold18pt7b, 1.1f, TFT_BLACK, true, 31);
  snprintf(b, 8, "%d", (int)lroundf(dispSpeed));
  valBox(2, b, TFT_WHITE, 172, 70, 138, 74, &fonts::FreeSansBold24pt7b, 2.0f, N_BOXBG, false, 70);
  fmtV(b, metricHave[M_BATT], pd[M_BATT], 1);
  valBox(3, b, battCol(TFT_WHITE), 170, 192, 72, 36, &fonts::FreeSansBold18pt7b, 1.3f, TFT_BLACK, false, 34);
  fmtV(b, coolHave(), pd[M_COOLANT], 0);
  valBox(4, b, coolCol(TFT_WHITE), 290, 176, 26, 17, &fonts::FreeSansBold9pt7b, 1.0f, TFT_BLACK, false, 14);
  int px = coolHave() ? (int)lroundf(constrain(pd[M_COOLANT] / 120.0f, 0.0f, 1.0f) * DW_W) : 0;
  if (px != nWBar) { dWater(px); nWBar = px; }
  nStatus(4, 3, false, TFT_BLACK, 74);
}

// =============================== หน้า 7 (PG_E) : จอแข่งสีส้ม (สไตล์ dash รถแข่ง) ===============================
//  แถวไฟ shift 13 ดวงด้านบน + แถบรอบส้ม + ค่าซ้าย/ขวา 4+4 ช่อง + รอบตรงกลาง + "Race" + เกียร์ตัวเอียงใหญ่
uint16_t E_OR, E_ORD, E_LEDOFF, E_LEDRIM, E_LG, E_LY, E_BAROFF;
const int E_NLED = 13, E_LED_Y = 8;
const int E_BX = 4, E_BY = 20, E_BW = 236, E_BH = 7;               // แถบรอบ
const int E_ROWY[4] = {34, 82, 130, 178};                           // แถวป้าย (ค่าอยู่ใต้ป้าย 16 px)
const char *const E_LLAB[4] = {"Water TMP", "Air TMP", "Air Press", "AIR FLOW"};
const char *const E_RLAB[4] = {"Km/h", "ENGINE L", "Short FT", "Batt Vol"};
int eLedKey = -1;

void eColors() {
  E_OR = lcd.color565(255, 104, 16);  E_ORD = lcd.color565(200, 84, 20);
  E_LEDOFF = lcd.color565(46, 44, 40); E_LEDRIM = lcd.color565(110, 108, 100);
  E_LG = lcd.color565(60, 230, 90);   E_LY = lcd.color565(255, 170, 20);
  E_BAROFF = lcd.color565(34, 14, 4);
}
int eLedX(int i) { return 13 + i * 24; }
void eLeds(int lit, bool over, bool flash) {                        // ไฟ shift: เริ่มติดที่ 50% ของ redline
  for (int i = 0; i < E_NLED; i++) {
    uint16_t c = E_LEDOFF;
    if (over) c = flash ? C_RED : lcd.color565(80, 150, 255);
    else if (i < lit) c = i < 5 ? E_LG : (i < 10 ? E_LY : C_RED);
    lcd.fillRect(eLedX(i) - 9, E_LED_Y - 6, 19, 13, E_LEDRIM);          // ไฟสี่เหลี่ยม มีขอบเทา
    lcd.fillRect(eLedX(i) - 8, E_LED_Y - 5, 17, 11, c);
  }
}
void eBarSpan(int x0, int x1, int litW, bool flash) {
  int xr = (int)(E_BW * constrain(maxRpm / (gScaleMax() * 1000.0f), 0.0f, 1.0f));
  for (int x = x0; x < x1;) {                                     // ช่วงสีเดียวกันวาดทีเดียว
    bool lit = x < litW, red = x >= xr;
    int e = x1;
    if (x < litW) e = min(e, litW); else e = min(e, x1);
    if (x < xr) e = min(e, xr);
    uint16_t c = lit ? ((flash || red) ? C_RED : E_OR) : E_BAROFF;
    lcd.fillRect(E_BX + x, E_BY, e - x, E_BH, c);
    x = e;
  }
}
uint16_t eColAt(int x, bool lit, bool flash) {
  int xr = (int)(E_BW * constrain(maxRpm / (gScaleMax() * 1000.0f), 0.0f, 1.0f));
  return lit ? ((flash || x >= xr) ? C_RED : E_OR) : E_BAROFF;
}
void eStatic() {
  lcd.fillScreen(TFT_BLACK);
  eLeds(0, false, false);
  eBarSpan(0, E_BW, 0, false);
  for (int r = 0; r < 4; r++) {
    txtAt(E_LLAB[r], 6, E_ROWY[r], &fonts::FreeSansOblique9pt7b, E_ORD, lgfx::textdatum_t::top_left);
    txtAt(E_RLAB[r], 314, E_ROWY[r], &fonts::FreeSansOblique9pt7b, E_ORD, lgfx::textdatum_t::top_right);
  }
  txtAt("r/min", 190, 52, &fonts::FreeSansOblique9pt7b, E_OR, lgfx::textdatum_t::top_left);
  txtAt("Race", 160, 76, &fonts::FreeSansBold12pt7b, E_OR, lgfx::textdatum_t::top_center);
}
void eVal(int i, const char *s, uint16_t col) {                    // i 0-3 ซ้าย, 4-7 ขวา
  int r = i % 4, y = E_ROWY[r] + 16;
  if (i < 4) valBox(2 + i, s, col, 6, y, 84, 26, &fonts::Font7, 0.5f, TFT_BLACK, false, 25);
  else       valBox(2 + i, s, col, 230, y, 84, 26, &fonts::Font7, 0.5f, TFT_BLACK, false, 25);
}
void updateE(uint32_t now, bool over) {
  char b[8];
  bool flash = over && fastBlink;
  float ratio = dispRpm / (float)max(maxRpm, 1000);
  int lit = ratio > 0.5f ? constrain((int)ceilf((ratio - 0.5f) / 0.5f * E_NLED), 0, E_NLED) : 0;
  int key = over ? (flash ? 101 : 102) : lit;
  if (key != eLedKey) { eLeds(lit, over, flash); eLedKey = key; }
  contBarUpdate(dispRpm / (gScaleMax() * 1000.0f) * E_BW, E_BW, flash, E_BX, E_BY, E_BH, eBarSpan, eColAt);

  fmtRpm(b);
  static uint32_t lastR = 0;
  uint16_t rc = flash ? C_RED : E_OR;
  if (now - lastR >= 80 || rc != pCol[0]) { lastR = now; valBox(0, b, rc, 106, 34, 80, 30, &fonts::Font7, 0.6f, TFT_BLACK, false, 29); }
  b[0] = gearChar(); b[1] = 0;
  valBox(1, b, E_OR, 100, 102, 120, 118, &fonts::FreeSansBoldOblique24pt7b, 2.6f, TFT_BLACK, true, 106);
  uint16_t hot = (coolHave() && coolant >= tempAlert) ? (blinkOn ? C_RED : E_OR) : E_OR;
  fmtV(b, coolHave(), pd[M_COOLANT], 0);          eVal(0, b, hot);
  fmtV(b, metricHave[M_IAT], pd[M_IAT], 0);       eVal(1, b, E_OR);
  fmtV(b, metricHave[M_MAP], pd[M_MAP] / 100.0f, 1); eVal(2, b, E_OR);       // ความดันท่อไอดี (bar สัมบูรณ์)
  fmtV(b, metricHave[M_MAF], pd[M_MAF], 1);       eVal(3, b, E_OR);
  snprintf(b, 8, "%d", (int)lroundf(dispSpeed));  eVal(4, b, E_OR);
  fmtV(b, metricHave[M_LOAD], pd[M_LOAD], 1);     eVal(5, b, E_OR);
  fmtV(b, metricHave[M_STFT], pd[M_STFT], 1);     eVal(6, b, E_OR);
  fmtV(b, metricHave[M_BATT], pd[M_BATT], 1);     eVal(7, b, metricHave[M_BATT] && pd[M_BATT] < 11.8f ? C_RED : E_OR);
  nStatus(4, 222, false, TFT_BLACK, 74);
}

// ---- (สมูท 1) ค่าที่จอควรแสดง ณ เวลา now: ย้อนหลัง interpDelay ms แล้วลากเส้นตรงระหว่างสองค่าที่ล้อมเวลานั้น ----
// interpDelay ปรับเองตามจังหวะที่ ELM327 ส่งค่ามา (ราว 1.2 เท่าของช่วงห่างเฉลี่ย, 30-160 ms)
float interpDelay = 80.0f;
bool interpGet(uint32_t now, float *r, float *sp) {
  RSamp b[8]; uint32_t n;
  portENTER_CRITICAL(&rsMux);
  n = rsN; memcpy(b, rsBuf, sizeof(b));
  portEXIT_CRITICAL(&rsMux);
  if (n == 0) return false;
  int cnt = (n < 8) ? (int)n : 8;
  const RSamp &nw = b[(n - 1) & 7], &od = b[(n - cnt) & 7];
  if ((int32_t)(now - nw.t) > 1000) return false;               // ไม่มีค่าใหม่นานเกิน 1 วินาที = ไม่ไล่ค่า
  if (cnt >= 3) {
    float avg = (float)(nw.t - od.t) / (cnt - 1);
    interpDelay += (constrain(avg * 1.2f, 30.0f, 160.0f) - interpDelay) * 0.05f;
  }
  float tr = (float)now - interpDelay;
  if (tr >= (float)nw.t) { *r = nw.r; *sp = nw.s; return true; }
  for (int k = 1; k < cnt; k++) {                                // หาคู่ค่าที่ล้อมเวลา tr (ใหม่ -> เก่า)
    const RSamp &b1 = b[(n - k) & 7], &b0 = b[(n - k - 1) & 7];
    if (tr >= (float)b0.t) {
      float span = (float)(b1.t - b0.t), f = span > 0 ? (tr - b0.t) / span : 1.0f;
      *r = b0.r + (b1.r - b0.r) * f; *sp = b0.s + (b1.s - b0.s) * f;
      return true;
    }
  }
  *r = od.r; *sp = od.s;
  return true;
}

// ---- (สมูท 2) สปริงหน่วงพอดี (critically damped): ออกตัวนุ่ม หยุดนุ่ม ไม่เด้งเกิน ----
// w ยิ่งมากยิ่งตามไว (rad/s): รอบ 30 (~0.15 วินาที), ความเร็ว 14
float velRpm = 0, velSpeed = 0;
void springTo(float &x, float &v, float target, float dt, float w) {
  int n = (int)ceilf(dt / 0.004f); if (n < 1) n = 1; if (n > 50) n = 50;
  float h = dt / n;
  for (int i = 0; i < n; i++) {
    float a = w * w * (target - x) - 2.0f * w * v;
    v += a * h; x += v * h;
  }
  if (x < 0 && target >= 0) { x = 0; v = 0; }
}

// ---- ตัววัดความลื่น: พิมพ์ fps และเวลาวาดต่อเฟรมลง Serial ทุก 5 วินาที ----
#define FPS_LOG 1
uint32_t fpsFrames = 0, fpsDrawUs = 0, fpsMaxUs = 0, fpsT0 = 0;

// =============================== หน้า 8 (PG_F) : เกจโค้ง + ค่าสูงสุด/ต่ำสุด ===============================
// ซ้าย: วงแหวนรอบ 240 องศา (ฟ้า = รอบ, แดง = โซน redline) + เข็มส้ม + เกียร์ในวงกลม + ความเร็ว + PEAK รอบ/ความเร็ว
//       ขีดเหลืองบนวงแหวน = รอบสูงสุดของรอบนี้ (peak)
// ขวา : COOLANT (MAX) / IAT (MAX) / VOLT (MIN) ตัวเลข + แถบ + ขีดเหลืองที่ค่าสูงสุด/ต่ำสุด
// ค่าสูงสุด/ต่ำสุดเริ่มนับใหม่ทุกครั้งที่ต่อ OBD ติด (หรือเริ่มเดโม)
const int H_CX = 110, H_CY = 114, G_RI = 90, G_RO = 99, G_NUMR = 72, G_ND0 = 27, G_ND1 = 78;
const float G_A0 = 150.0f, G_SWEEP = 240.0f;
uint16_t G_BLUE, G_TRK, G_RED, G_RED2, G_ORG, G_YEL, G_GRY, G_DIM, G_GEARBG;
int   gSm() { return max(4, maxRpm / 1000 + 1); }                    // 7500 -> 8
float gAng(float r) { return G_A0 + G_SWEEP * constrain(r / (gSm() * 1000.0f), 0.0f, 1.0f); }
float gNeedleA = -999, gBandA = -999, gPkA = -999; int gFlash = -1; char gGear = 0;
float gPkR = 0, gPkS = 0, gMaxC = -999, gMaxI = -999, gMinV = 999;
char  gT[10][24]; uint16_t gC[10]; int gBarPx[6];
void gReset() {
  gNeedleA = gBandA = gPkA = -999; gFlash = -1; gGear = 0;
  memset(gT, 0, sizeof(gT)); memset(gC, 0, sizeof(gC));
  for (int &b : gBarPx) b = -1;
}
void gColors() {
  G_BLUE = lcd.color565(80, 190, 250); G_TRK = lcd.color565(40, 40, 43);
  G_RED = lcd.color565(222, 30, 58);   G_RED2 = lcd.color565(255, 80, 90);
  G_ORG = lcd.color565(255, 90, 30);   G_YEL = lcd.color565(246, 200, 44);
  G_GRY = lcd.color565(150, 150, 156); G_DIM = lcd.color565(120, 120, 126); G_GEARBG = lcd.color565(20, 20, 22);
}
// ข้อความ (จัดซ้าย/กลาง/ขวา) วาดผ่านสไปรท์เป็นชิ้น ๆ วาดใหม่เฉพาะเมื่อเปลี่ยน, deg = ใส่วงกลมองศาท้ายตัวเลข
void gText(int i, const char *s, uint16_t fg, int x, int y, int w, int h, const lgfx::IFont *f, float sc, int align, int base, bool deg = false) {
  if (strcmp(gT[i], s) == 0 && gC[i] == fg) return;
  strncpy(gT[i], s, 23); gT[i][23] = 0; gC[i] = fg;
  sc = fitSc(f, s, sc, w - (deg ? 10 : 2));
  lgfx::textdatum_t dt = align == 0 ? lgfx::textdatum_t::baseline_left : (align == 1 ? lgfx::textdatum_t::baseline_center : lgfx::textdatum_t::baseline_right);
  int ax = align == 0 ? x : (align == 1 ? x + w / 2 : x + w - 1);
  auto paint = [&](lgfx::LGFXBase *g, int ox, int oy) {
    g->setFont(f); g->setTextSize(sc); g->setTextColor(fg); g->setTextDatum(dt);
    g->drawString(s, ax - ox, y + base - oy);
    if (deg) {
      int tw = g->textWidth(s), ex = (align == 0 ? ax + tw : (align == 1 ? ax + tw / 2 : ax)) + 5;
      int top = y + base - (int)(g->fontHeight() * 0.62f);
      g->drawCircle(ex - ox, top + 2 - oy, 2, fg); g->drawCircle(ex - ox, top + 2 - oy, 3, fg);
    }
    g->setTextSize(1.0f); g->setTextDatum(lgfx::textdatum_t::top_left);
  };
  if (!pSprOk) { lcd.fillRect(x, y, w, h, TFT_BLACK); paint(&lcd, 0, 0); return; }
  for (int ty = y; ty < y + h; ty += PSPR_H)
    for (int tx = x; tx < x + w; tx += PSPR_W) {
      int tw = min(PSPR_W, x + w - tx), th = min(PSPR_H, y + h - ty);
      pSpr.fillRect(0, 0, PSPR_W, PSPR_H, TFT_BLACK);
      paint(&pSpr, tx, ty);
      lcd.setClipRect(tx, ty, tw, th); pSpr.pushSprite(tx, ty); lcd.clearClipRect();
    }
}
// ---- ส่วนกลางเกจ (ตัวเลข + เข็ม + วงกลมเกียร์) วาดเป็นชิ้นผ่านสไปรท์ ----
void gBandPaint(lgfx::LGFXBase *g, int ox, int oy, float lo, float hi, float aNow, bool flash);
void gPeakPaint(lgfx::LGFXBase *g, int ox, int oy, float a);
void gPaintCenter(lgfx::LGFXBase *g, int ox, int oy, float deg) {
  int cx = H_CX - ox, cy = H_CY - oy, sm = gSm(), GW = g->width(), GH = g->height();
  // วงแหวน + ขีด peak + ตัวเลขความเร็ว อยู่ในชิ้นเดียวกัน -> ดันขึ้นจอทีเดียว ไม่เห็นพื้นดำแวบ
  if (gBandA > -900) { gBandPaint(g, ox, oy, G_A0, G_A0 + G_SWEEP, gBandA, gFlash == 1); gPeakPaint(g, ox, oy, gPkA); }
  if (gT[0][0] && GH + oy > 150 && oy < 184) {
    float sc = fitSc(&fonts::FreeSansBold18pt7b, gT[0], 1.15f, 88);
    g->setFont(&fonts::FreeSansBold18pt7b); g->setTextSize(sc); g->setTextColor(gC[0]);
    g->setTextDatum(lgfx::textdatum_t::baseline_center); g->drawString(gT[0], H_CX - ox, 181 - oy);
    g->setTextSize(1.0f);
  }
  g->setFont(&fonts::FreeSans9pt7b); g->setTextSize(1.0f); g->setTextDatum(lgfx::textdatum_t::middle_center);
  for (int k = 0; k <= sm; k++) {
    float a = gAng(k * 1000.0f) * 0.0174533f;
    int nx = cx + (int)lroundf(G_NUMR * cosf(a)), ny = cy + (int)lroundf(G_NUMR * sinf(a));
    if (nx < -12 || nx > GW + 12 || ny < -12 || ny > GH + 12) continue;
    char b[4]; snprintf(b, sizeof(b), "%d", k);
    g->setTextColor(((k + 1) * 1000 > maxRpm) ? G_RED : G_GRY);
    g->drawString(b, nx, ny);
  }
  if (deg > -900) {
    float a = deg * 0.0174533f, ca = cosf(a), sa = sinf(a);
    g->drawWideLine(cx + ca * G_ND0, cy + sa * G_ND0, cx + ca * G_ND1, cy + sa * G_ND1, 2.2f, (rpm >= maxRpm && fastBlink) ? C_RED : G_ORG);
  }
  g->fillCircle(cx, cy, 24, G_ORG);
  g->fillCircle(cx, cy, 22, G_GEARBG);
  g->setFont(&fonts::Font0); g->setTextColor(G_DIM); g->setTextDatum(lgfx::textdatum_t::middle_center);
  g->drawString("GEAR", cx, cy - 11);
  char gb[2] = {gGear ? gGear : '-', 0};
  g->setFont(&fonts::FreeSansBold12pt7b); g->setTextColor(TFT_WHITE); g->setTextDatum(lgfx::textdatum_t::baseline_center);
  g->drawString(gb, cx, cy + 14);
  g->setTextDatum(lgfx::textdatum_t::top_left);
}
void gRenderCenter(int x0, int y0, int w, int h, float deg) {
  x0 = max(x0, 0); y0 = max(y0, 0); w = min(w, 320 - x0); h = min(h, 240 - y0);
  for (int ty = y0; ty < y0 + h; ty += PSPR_H)
    for (int tx = x0; tx < x0 + w; tx += PSPR_W) {
      int tw = min(PSPR_W, x0 + w - tx), th = min(PSPR_H, y0 + h - ty);
      if (pSprOk) {
        pSpr.fillRect(0, 0, PSPR_W, PSPR_H, TFT_BLACK);
        gPaintCenter(&pSpr, tx, ty, deg);
        lcd.setClipRect(tx, ty, tw, th); pSpr.pushSprite(tx, ty); lcd.clearClipRect();
      } else {
        lcd.setClipRect(tx, ty, tw, th); lcd.fillRect(tx, ty, tw, th, TFT_BLACK);
        gPaintCenter(&lcd, 0, 0, deg); lcd.clearClipRect();
      }
    }
}
// fillArc ที่ข้ามมุม 360 องศา: แยกเป็นสองช่วง (กันวงแหวนแหว่ง)  g = จอหรือสไปรท์, (ox,oy) = ตำแหน่งชิ้นบนจอ
void arcG(lgfx::LGFXBase *g, int ox, int oy, int r0, int r1, float a, float e, uint16_t c) {
  while (a >= 360.0f) { a -= 360.0f; e -= 360.0f; }
  if (e <= 360.0f) { g->fillArc(H_CX - ox, H_CY - oy, r0, r1, a, e, c); return; }
  g->fillArc(H_CX - ox, H_CY - oy, r0, r1, a, 360.0f, c);
  g->fillArc(H_CX - ox, H_CY - oy, r0, r1, 0.0f, e - 360.0f, c);
}
void arcW(int r0, int r1, float a, float e, uint16_t c) { arcG(&lcd, 0, 0, r0, r1, a, e, c); }
// วงแหวน: วาดช่วงมุม [lo,hi] ตามสี (ติด = ฟ้า, ยังไม่ติด = เทาเข้ม, โซน redline = แดงเสมอ)
void gBandPaint(lgfx::LGFXBase *g, int ox, int oy, float lo, float hi, float aNow, bool flash) {
  float A1 = G_A0 + G_SWEEP;
  lo = max(lo, G_A0); hi = min(hi, A1);
  if (hi <= lo) return;
  float aRed = gAng(maxRpm);
  float cut[4] = {lo, constrain(aNow, lo, hi), constrain(aRed, lo, hi), hi};
  if (cut[2] < cut[1]) { float t = cut[1]; cut[1] = cut[2]; cut[2] = t; }
  for (int s = 0; s < 3; s++) {
    float a = cut[s], e = cut[s + 1];
    if (e - a < 0.05f) continue;
    float mid = (a + e) * 0.5f;
    bool lit = mid < aNow, red = mid >= aRed;
    uint16_t c = red ? (lit ? G_RED2 : G_RED) : (lit ? (flash ? C_RED : G_BLUE) : G_TRK);
    arcG(g, ox, oy, G_RI, G_RO, a, e, c);
  }
}
void gBandSpan(float lo, float hi, float aNow, bool flash) { gBandPaint(&lcd, 0, 0, lo, hi, aNow, flash); }
void gPeakPaint(lgfx::LGFXBase *g, int ox, int oy, float a) {
  if (a < -900) return;
  float r = a * 0.0174533f;
  g->drawWideLine(H_CX - ox + cosf(r) * 84, H_CY - oy + sinf(r) * 84, H_CX - ox + cosf(r) * 104, H_CY - oy + sinf(r) * 104, 1.2f, G_YEL);
}
void gPeakMark(float a, bool draw, float aNow, bool flash) {          // ขีดเหลือง peak บนวงแหวน (r 84-104)
  if (a < -900) return;
  if (!draw) {                                                      // ลบ: วงแหวนกลับเป็นปกติ + พื้นดำด้านใน/นอก
    arcW(81, G_RI - 1, a - 2.2f, a + 2.2f, TFT_BLACK);
    arcW(G_RO + 1, 109, a - 2.2f, a + 2.2f, TFT_BLACK);
    gBandSpan(a - 2.2f, a + 2.2f, aNow, flash);
    return;
  }
  gPeakPaint(&lcd, 0, 0, a);
}
const int GR_X = 218, GR_W = 92, GR_Y[3] = {40, 97, 154};
int gLabW[3] = {56, 20, 31};                                      // ความกว้างป้าย (วัดตอนวาดหน้า)
const char *const GR_LAB[3] = {"COOLANT", "IAT", "VOLT"};
void gBar(int row, float frac, float mark) {
  int px = (int)lroundf(constrain(frac, 0.0f, 1.0f) * GR_W), mk = (mark > -900) ? (int)lroundf(constrain(mark, 0.0f, 1.0f) * (GR_W - 2)) : -1;
  int key = px * 128 + (mk + 1);
  if (gBarPx[row] == key) return;
  gBarPx[row] = key;
  int y = GR_Y[row] + 43;
  lcd.fillRect(GR_X, y - 3, GR_W, 10, TFT_BLACK);
  lcd.fillRect(GR_X, y, px, 4, G_BLUE);
  lcd.fillRect(GR_X + px, y, GR_W - px, 4, G_TRK);
  if (mk >= 0) lcd.fillRect(GR_X + mk, y - 3, 2, 10, G_YEL);
}
void gStatic() {
  lcd.fillScreen(TFT_BLACK);
  gReset();
  gRenderCenter(H_CX - 84, H_CY - 84, 168, 140, -999);           // ส่วนกลางก่อน แล้ววงแหวนทับ (กรอบส่วนกลางกว้างถึงขอบวงแหวน)
  gBandSpan(G_A0, G_A0 + G_SWEEP, G_A0, false);
  txtAt("km/h - x1000 RPM", H_CX, 197, &fonts::Font2, G_DIM, lgfx::textdatum_t::middle_center);
  lcd.setFont(&fonts::Font2); lcd.setTextSize(1.0f);
  for (int r = 0; r < 3; r++) { gLabW[r] = lcd.textWidth(GR_LAB[r]); txtAt(GR_LAB[r], GR_X, GR_Y[r], &fonts::Font2, G_GRY, lgfx::textdatum_t::top_left); }
  menuLabel();
}
void updateG(uint32_t now, bool over) {
  char b[24];
  bool flash = over && fastBlink;
  // ---- ค่าสูงสุด/ต่ำสุด: เริ่มใหม่เมื่อต่อ OBD ติดหรือเริ่มเดโม ----
  static bool wasLive = false;
  bool live = obdOk || demoMode;
  if (live && !wasLive) { gPkR = 0; gPkS = 0; gMaxC = -999; gMaxI = -999; gMinV = 999; gPkA = -999; }
  wasLive = live;
  if (live) {
    if (rpm > gPkR && rpm < 12000) gPkR = rpm;
    if (speedKmh > gPkS && speedKmh <= MAX_SPEED) gPkS = speedKmh;
    if (metricHave[M_COOLANT] && pd[M_COOLANT] > gMaxC) gMaxC = pd[M_COOLANT];
    if (metricHave[M_IAT] && pd[M_IAT] > gMaxI) gMaxI = pd[M_IAT];
    if (metricHave[M_BATT] && pd[M_BATT] > 6.0f && pd[M_BATT] < gMinV) gMinV = pd[M_BATT];
  }
  // ---- วงแหวน ----
  float a = gAng(dispRpm);
  float pa = gPkR > 0 ? gAng(gPkR) : -999;
  if (gFlash != (int)flash || gBandA < -900) { gBandSpan(G_A0, G_A0 + G_SWEEP, a, flash); gBandA = a; gFlash = flash; if (gPkA > -900) gPeakMark(gPkA, true, a, flash); }
  else if (fabsf(a - gBandA) > 0.25f) {
    gBandSpan(min(a, gBandA) - 0.3f, max(a, gBandA) + 0.3f, a, flash); gBandA = a;
    if (gPkA > -900 && fabsf(gPkA - a) < 4.0f) gPeakMark(gPkA, true, a, flash);
  }
  if (fabsf(pa - gPkA) > 0.4f || (pa < -900) != (gPkA < -900)) {
    gPeakMark(gPkA, false, a, flash);
    gPkA = pa;
    gPeakMark(gPkA, true, a, flash);
  }
  // ---- เข็ม + เกียร์ (วาดเฉพาะกรอบที่เข็มเก่า/ใหม่ผ่าน) ----
  char gc = gearChar();
  static uint32_t lastNd = 0;
  bool gearChg = gc != gGear;
  if (gearChg || gNeedleA < -900 || (fabsf(a - gNeedleA) > 0.4f && now - lastNd >= 12) || (over && now - lastNd >= 110)) {
    gGear = gc;
    float as[2] = {gNeedleA < -900 ? a : gNeedleA, a};
    int x0 = H_CX - 26, x1 = H_CX + 26, y0 = H_CY - 26, y1 = H_CY + 26;
    for (float an : as) {
      float r = an * 0.0174533f;
      for (int d : {G_ND0, G_ND1 + 2}) {
        int x = H_CX + (int)(cosf(r) * d), y = H_CY + (int)(sinf(r) * d);
        x0 = min(x0, x - 4); x1 = max(x1, x + 4); y0 = min(y0, y - 4); y1 = max(y1, y + 4);
      }
    }
    // ตัวเลขสเกลที่เข็มผ่านต้องวาดใหม่ทั้งตัว: ขยายกรอบให้ครอบตัวเลขที่ซ้อน
    for (int k = 0; k <= gSm(); k++) {
      float r = gAng(k * 1000.0f) * 0.0174533f;
      int nx = H_CX + (int)lroundf(G_NUMR * cosf(r)), ny = H_CY + (int)lroundf(G_NUMR * sinf(r));
      if (nx + 8 >= x0 && nx - 8 <= x1 && ny + 8 >= y0 && ny - 8 <= y1) { x0 = min(x0, nx - 9); x1 = max(x1, nx + 9); y0 = min(y0, ny - 9); y1 = max(y1, ny + 9); }
    }
    gRenderCenter(x0, y0, x1 - x0, y1 - y0, a);
    gNeedleA = a; lastNd = now;

  }
  // ---- ความเร็ว / PEAK ----
  snprintf(b, sizeof(b), "%d", (int)lroundf(dispSpeed));
  gText(0, b, TFT_WHITE, H_CX - 45, 150, 90, 34, &fonts::FreeSansBold18pt7b, 1.15f, 1, 31);
  if (gPkR > 0) snprintf(b, sizeof(b), "PEAK %d rpm  %d km/h", (int)gPkR, (int)gPkS); else strcpy(b, "PEAK --");
  gText(1, b, G_YEL, 2, 208, 214, 18, &fonts::DejaVu12, 1.0f, 1, 13);
  // ---- ขวา: COOLANT / IAT / VOLT ----
  struct Rw { bool have; float v, lo, hi, mk; const char *mkLab; int dec; uint16_t col; };
  Rw rw[3] = {
    {coolHave(), pd[M_COOLANT], 40, 120, gMaxC, "MAX", 0, coolCol(TFT_WHITE)},
    {metricHave[M_IAT], pd[M_IAT], 0, 90, gMaxI, "MAX", 0, TFT_WHITE},
    {metricHave[M_BATT], pd[M_BATT], 10, 16, gMinV < 900 ? gMinV : -999, "MIN", 1, battCol(TFT_WHITE)}};
  for (int r = 0; r < 3; r++) {
    const Rw &w = rw[r];
    fmtV(b, w.have, w.v, w.dec);
    gText(2 + r, b, w.col, GR_X, GR_Y[r] + 14, 70, 24, &fonts::FreeSansBold12pt7b, 1.0f, 0, 20, w.have && r < 2);
    bool hm = w.mk > -900;
    if (hm) snprintf(b, sizeof(b), w.dec ? "%s %.1f" : "%s %.0f", w.mkLab, w.mk); else snprintf(b, sizeof(b), "%s --", w.mkLab);
    int mx = GR_X + gLabW[r] + 3;                                    // กล่อง MAX/MIN เริ่มหลังป้าย ไม่ทับกัน
    gText(5 + r, b, G_YEL, mx, GR_Y[r] + 2, 318 - mx, 12, &fonts::DejaVu9, 1.0f, 2, 10);
    gBar(r, w.have ? (w.v - w.lo) / (w.hi - w.lo) : 0.0f, hm ? (w.mk - w.lo) / (w.hi - w.lo) : -999);
  }
  nStatus(4, 3, false, TFT_BLACK, 74);
}

// =============================== หน้า 9 (PG_H) : ภาพพื้นหลัง (anime.h) + ตัวเลขลอยบนรูป (เลย์เอาต์หน้า 1 แบบเดิม) ===============================
// รอบ / แถบรอบ / ไฟ shift / ความเร็ว / ช่องล่าง 2 ช่อง (แตะเพื่อสลับค่า) / ขอบจอวาบแดงตอน redline
// พื้นหลังใต้ตัวเลขเก็บในแรม (~67 KB) จองเฉพาะตอนอยู่หน้านี้ คืนทันทีเมื่อเปลี่ยนหน้า/เข้าเมนู
#if HAS_ANIME || HAS_ANIME2
// ภาพพื้นหลังของหน้าที่แสดงอยู่: หน้า 9 = anime.h, หน้า 13 = anime2.h (ตัวเลข/ช่องเหมือนกันทุกอย่าง)
// ชุดนี้ลบหน้า 9 ออกแล้ว (ไม่มี anime.h) หน้า 13 ใช้โค้ดชุดนี้ต่อได้เอง
const uint8_t *hJpg = nullptr; uint32_t hJpgLen = 0;
// ---- หน้า 1: ใบหน้าตัวละครเปลี่ยนตามช่วงรอบ ----
// แบ่งรอบ 0..redline (maxRpm) เป็น N_FACES ช่วงเท่า ๆ กัน (12 ช่วง: redline 7500 = ช่วงละ 625 รอบ)
// ลำดับใบหน้า = ลำดับรูปที่ใส่ตอนสร้าง faces.h ด้วย make_face_page.py (รอบต่ำ -> สูง)
int curRange = 0, curFace = 0;
void facePatchTo(lgfx::LGFXBase *g) { g->drawJpg(FACE_JPG[curFace], FACE_LEN[curFace], FACE_X, FACE_Y); }
int faceRangeFor(int r) { int red = max(maxRpm, 1000); return constrain((int)((long)r * N_FACES / red), 0, N_FACES - 1); }
void updateFace(uint32_t now) {                              // ขึ้นช่วงสูงทันที / ลงช่วงต่ำต้องต่ำกว่าเส้นแบ่ง 100 รอบ และค้างเกิน 250 ms
  static int pend = -1; static uint32_t pendT = 0;
  int red = max(maxRpm, 1000);
  int c = faceRangeFor(rpm);
  if (c < curRange && rpm >= curRange * red / N_FACES - 100) c = curRange;
  int nr = curRange;
  if (c == curRange) pend = -1;
  else if (c > curRange) { nr = c; pend = -1; }
  else if (pend != c) { pend = c; pendT = now; }
  else if (now - pendT > 250) { nr = c; pend = -1; }
  if (nr == curRange) return;
  curRange = nr;
  if (curRange == curFace) return;
  curFace = curRange;
  facePatchTo(&lcd);                                         // วาดเฉพาะแพตช์ใบหน้า (ไม่ชนช่องตัวเลขใด ๆ)
}
const uint8_t *hLoaded = nullptr;                            // ภาพที่ถอดลงแรมอยู่ตอนนี้
void hSelectImg() {
#if HAS_ANIME2
  if (page == PG_K) { hJpg = anime2_jpg; hJpgLen = anime2_jpg_len; return; }
#endif
#if HAS_FACE
  hJpg = face_bg_jpg; hJpgLen = face_bg_len;
#endif
}
volatile int slotMetric[2] = {M_COOLANT, M_IAT};          // ช่องล่างซ้าย / ขวา แสดงค่าอะไร (จำไว้ใน NVS)
const int H_BAR_W = 140, H_BAR_H = 15;                 // แถบรอบสั้นลงเพื่อไม่ชนใบหน้าตัวละคร
uint16_t hGrad[H_BAR_W];
static const int8_t OL8[8][2] = {{-1,-1},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1}};
struct Canvas { lgfx::LGFXBase *g; int ox, oy; };
struct Strip {
  int x = 0, y = 0, w = 0, h = 0;
  lgfx::LGFX_Sprite *bg = nullptr, *work = nullptr;
  void free_() { if (bg) { bg->deleteSprite(); delete bg; bg = nullptr; } if (work) { work->deleteSprite(); delete work; work = nullptr; } }
  void alloc(int X, int Y, int W, int H, bool wantWork) {
    x = X; y = Y; w = W; h = H;
    if (!bg) { bg = new lgfx::LGFX_Sprite(&lcd); if (!sprFromPool(bg, W, H)) { delete bg; bg = nullptr; Serial.printf("page9 strip alloc FAILED %dx%d\n", W, H); } }
    if (wantWork && gPoolLite && W * H > 4000) wantWork = false;   // ก้อนกลางแบบประหยัด: ตัดบัฟเฟอร์สองชั้นเฉพาะช่องใหญ่
    if (wantWork && bg && !work) { work = new lgfx::LGFX_Sprite(&lcd); if (!sprFromPool(work, W, H)) { delete work; work = nullptr; } }
  }
  Canvas begin() {
    if (bg && work) { bg->pushSprite(work, 0, 0); return {work, 0, 0}; }
    if (bg) bg->pushSprite(x, y);
    else { lcd.setClipRect(x, y, w, h); lcd.drawJpg(hJpg, hJpgLen, 0, 0); lcd.clearClipRect(); }   // แรมไม่พอ: ถอดภาพเฉพาะกรอบ (ช้า แต่ไม่เลอะ)
    lcd.setClipRect(x, y, w, h);                             // วาดตรงบนจอ: ไม่ให้ขอบตัวเลขล้นออกนอกกรอบ (ไม่งั้นเหลือเส้นค้าง)
    return {&lcd, x, y};
  }
  void end() { if (work) work->pushSprite(x, y); else lcd.clearClipRect(); }
};
Strip sRpm, sSpeed, sTemp, sIat, sStat, sShift, sEdgeT, sEdgeB, sEdgeL, sEdgeR;
Strip *const H_STRIPS[10] = {&sRpm, &sSpeed, &sTemp, &sIat, &sStat, &sShift, &sEdgeT, &sEdgeB, &sEdgeL, &sEdgeR};
// แถบรอบ: ทึบทั้งแถบ ไม่ต้องใช้พื้นหลังรูป -> ประกอบทั้งแถบในบัฟเฟอร์เล็ก แล้วส่งขึ้นจอทีเดียว (ไม่กะพริบ)
const int HB_X = 4, HB_Y = 70, HB_W = H_BAR_W + 2, HB_H = 9;
uint16_t hBarBuf[HB_W * HB_H];
bool hReady = false;
// วาดทุกช่องผ่าน "แถบ" สไปรท์เล็ก 164x10: ทีละแถบ = พื้นหลังจากแรม + ตัวเลข (ขอบดำ+สีขาว) แล้วส่งขึ้นจอทีเดียว
// จอจึงไม่เห็นช่วงที่ขอบดำวาดเสร็จแต่ตัวขาวยังไม่วาด (สาเหตุเลขรอบกะพริบดำ) และไม่ต้องใช้บัฟเฟอร์สองชั้นขนาดเต็มช่อง
const int HBAND_W = 164, HBAND_H = 10;
lgfx::LGFX_Sprite hBand(&lcd);
bool hBandOk = false;
template <typename F> void stripPaint(Strip &s, F paint);

void hLbl(lgfx::LGFXBase *g, int ox, int oy, const char *t, int x, int y, uint16_t col) {   // ตัวหนังสือมีขอบดำ
  g->setTextColor(TFT_BLACK);
  for (auto &o : OL8) g->drawString(t, x + o[0] - ox, y + o[1] - oy);
  g->setTextColor(col); g->drawString(t, x - ox, y - oy);
}
// ป้ายคงที่ทั้งหมด: วาดทั้งบนจอและลงพื้นหลังของแต่ละช่อง (ตัวเลขวาดทับแล้วป้ายไม่แหว่ง)
void hLabels(lgfx::LGFXBase *g, int ox, int oy) {
  g->setFont(&fonts::Font2); g->setTextSize(1.0f); g->setTextDatum(lgfx::textdatum_t::top_left);
  hLbl(g, ox, oy, "RPM", 6, 4, C_LABEL);
  hLbl(g, ox, oy, "SPEED", 6, 89, C_LABEL);
  hLbl(g, ox, oy, "km/h", 136, 132, C_LABEL);
  for (int s = 0; s < 2; s++) {
    int x = (s == 0) ? 8 : 98, m = slotMetric[s];
    hLbl(g, ox, oy, METRIC_LABEL[m], x, 156, C_LABEL);
    hLbl(g, ox, oy, METRIC_UNIT[m], x, 216, C_LABEL);
  }
}
void hFree() { for (Strip *s : H_STRIPS) s->free_(); if (hBandOk) { hBand.deleteSprite(); hBandOk = false; } hReady = false; }
void hBuildBg() {                                            // ถอดภาพใต้แต่ละช่องลงแรม + ป้าย
  for (Strip *s : H_STRIPS) if (s->bg) { s->bg->drawJpg(hJpg, hJpgLen, 0, 0, s->w, s->h, s->x, s->y); hLabels(s->bg, s->x, s->y); }
}
void hAlloc() {
  if (hReady) return;
  poolReset(0);                                             // หน้า 9 ใช้ก้อนกลางตั้งแต่ต้นก้อน (สไปรท์ตัวเลขถูกปลดก่อนแล้ว)
  sRpm.alloc(20, 16, 130, 49, false);                     // เลขรอบชิดซ้าย: ขอบขวา x=150 ไม่ทับใบหน้า
  sShift.alloc(4, 81, H_BAR_W, 8, false);
  sSpeed.alloc(30, 98, 104, 48, false);
  sTemp.alloc(6, 180, 76, 34, false);  sIat.alloc(94, 180, 76, 34, false);  sStat.alloc(96, 4, 72, 13, false);
  sEdgeT.alloc(0, 0, 320, 4, false);   sEdgeB.alloc(0, 236, 320, 4, false);
  sEdgeL.alloc(0, 4, 4, 232, false);   sEdgeR.alloc(316, 4, 4, 232, false);
  hBandOk = sprFromPool(&hBand, HBAND_W, HBAND_H);
  hBuildBg();
  hReady = true;
  Serial.printf("page9 strips: pool used %u/%u, free heap %u, largest %u\n", (unsigned)gPoolOff, (unsigned)gPoolSize, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
}
void hTxt(Canvas &c, const char *s, int x, int y, uint16_t col, bool right, int sh = 1) {
  int xx = c.ox + (right ? x - c.g->textWidth(s) : x);
  c.g->setTextColor(TFT_BLACK);
  for (auto &o : OL8) c.g->drawString(s, xx + o[0] * sh, c.oy + y + o[1] * sh);
  c.g->setTextColor(col); c.g->drawString(s, xx, c.oy + y);
}
int  hdRpm = -999, hdSpeed = -999, hdEdge = -1, hdShift = -1, hdStatKey = -1; bool hdRpmOver = false;
const char *hdStatus = nullptr;
float hDispSlot[2] = {0, 0}; bool hDispHave[2] = {false, false};
char hSlotTxt[2][12]; uint16_t hSlotCol[2]; float hShimmer = 0; bool hPrevBlink = false;
void hReset() { hdRpm = hdSpeed = -999; hdEdge = hdShift = hdStatKey = -1; hdStatus = nullptr; hSlotTxt[0][0] = hSlotTxt[1][0] = 0; }

void hStatic() {
  hSelectImg();
  if (hReady && hLoaded != hJpg) hFree();                   // เปลี่ยนระหว่างหน้า 9 <-> 13: ถอดพื้นหลังใหม่
  hAlloc();
  hLoaded = hJpg;
  curRange = curFace = faceRangeFor(rpm);                    // เข้าหน้าแล้วเริ่มที่ใบหน้าตามรอบตอนนี้
  if (!lcd.drawJpg(hJpg, hJpgLen, 0, 0)) lcd.fillScreen(C_SKY); else facePatchTo(&lcd);
  hLabels(&lcd, 0, 0);
  for (int k = 1000; k < maxRpm; k += 1000) lcd.drawFastVLine(HB_X + 1 + (int)((long)k * H_BAR_W / maxRpm), HB_Y - 4, 3, C_LABEL);   // ขีดทุก 1000 rpm (วาดครั้งเดียว)
  hReset();
}
template <typename F> void stripPaint(Strip &s, F paint) {
  if (s.bg && s.work) { s.bg->pushSprite(s.work, 0, 0); Canvas c = {s.work, 0, 0}; paint(c); s.work->pushSprite(s.x, s.y); return; }
  if (s.bg && hBandOk && s.w <= HBAND_W) {
    for (int by = 0; by < s.h; by += HBAND_H) {
      int bh = min(HBAND_H, s.h - by);
      s.bg->pushSprite(&hBand, 0, -by);                     // พื้นหลังแถวนี้
      Canvas c = {&hBand, 0, -by}; paint(c);                // ตัวเลข (ส่วนที่อยู่ในแถบนี้)
      lcd.setClipRect(s.x, s.y + by, s.w, bh); hBand.pushSprite(s.x, s.y + by); lcd.clearClipRect();
    }
    return;
  }
  Canvas c = s.begin(); paint(c); s.end();                  // สำรอง: วาดตรงบนจอ (อาจกะพริบ)
}
void hDrawRpm(int val, bool over) {
  int dx = 0, dy = 0;
  if (over) { dx = random(-3, 1); dy = random(-1, 2); }                 // ตัวเลขสั่นตอน redline
  char b[8]; snprintf(b, sizeof(b), "%d", val);
  uint16_t col = (over && fastBlink) ? C_RED : C_TXT;
  stripPaint(sRpm, [&](Canvas &c) {
    c.g->setFont(&fonts::Font7); c.g->setTextSize(1.0f); c.g->setTextDatum(lgfx::textdatum_t::top_left);
    hTxt(c, b, 128 + dx, 1 + dy, col, true, 2);
  });
}
void hDrawSpeed(int val) {
  char b[8]; snprintf(b, sizeof(b), "%d", val);
  stripPaint(sSpeed, [&](Canvas &c) {
    c.g->setFont(&fonts::Font7); c.g->setTextSize(1.0f); c.g->setTextDatum(lgfx::textdatum_t::top_left);
    hTxt(c, b, 100, 0, C_TXT, true, 2);
  });
}
void hDrawSlot(int s, const char *txt, uint16_t col) {
  Strip &st = (s == 0) ? sTemp : sIat;
  stripPaint(st, [&](Canvas &c) {
    c.g->setFont(&fonts::Font7); c.g->setTextSize(0.7f); c.g->setTextDatum(lgfx::textdatum_t::top_left);
    hTxt(c, txt, 2, 0, col, false);
    c.g->setTextSize(1.0f);
  });
}
void hDrawStatus(const char *msg, bool red, bool hot) {
  stripPaint(sStat, [&](Canvas &c) {
    c.g->setFont(&fonts::Font2); c.g->setTextSize(1.0f); c.g->setTextDatum(lgfx::textdatum_t::top_left);
    if (red) { if (fastBlink) hTxt(c, "REDLINE", 70, 0, C_RED, true); }
    else if (hot) { if (blinkOn) hTxt(c, "HOT!", 70, 0, C_RED, true); }
    else hTxt(c, msg, 70, 0, strcmp(msg, "OBD OK") == 0 ? C_OK : C_WARN, true);
  });
}
void hDrawShift(float ratio, bool over) {                    // ไฟ shift 10 ดวง: เริ่มติดที่ 75% ของ redline
  int lit = over ? 10 : (ratio > 0.75f ? constrain((int)ceilf((ratio - 0.75f) / 0.25f * 10.0f), 0, 10) : 0);
  int key = lit * 4 + (over ? (fastBlink ? 1 : 2) : 0);
  if (key == hdShift) return;
  hdShift = key;
  stripPaint(sShift, [&](Canvas &c) {
    for (int i = 0; i < 10; i++) {
      int cx = c.ox + 10 + i * ((H_BAR_W - 14) / 9), cy = c.oy + 4;
      uint16_t col = lcd.color565(40, 44, 70);
      if (i < lit) { col = i < 4 ? C_OK : (i < 7 ? C_WARN : C_RED); if (over) col = fastBlink ? C_RED : lcd.color565(90, 170, 255); }
      c.g->fillCircle(cx, cy, 4, TFT_BLACK); c.g->fillCircle(cx, cy, 3, col);
    }
  });
}
void hDrawEdges(bool on) {                                   // ขอบจอวาบแดงตอนถึง redline
  int want = on ? 1 : 0;
  if (hdEdge == want) return;
  hdEdge = want;
  Strip *e[] = {&sEdgeT, &sEdgeB, &sEdgeL, &sEdgeR};
  for (Strip *s : e) { Canvas c = s->begin(); if (on) c.g->fillRect(c.ox, c.oy, s->w, s->h, C_RED); s->end(); }
}
uint16_t hMixWhite(uint16_t c, float f) { return mix565(c, 0xFFFF, f); }
void hDrawRpmLine(uint32_t now) {                           // แถบรอบเส้นตรง: แสงวิ่ง + ปลายแถบเต้น + ปลาย sub-pixel
  float ratio = constrain(dispRpm / (float)maxRpm, 0.0f, 1.0f);
  float wf = ratio * H_BAR_W; int w = (int)wf;
  bool flash = rpm >= maxRpm && fastBlink;
  uint16_t trk = lcd.color565(45, 50, 80);
  float pulse = 0.5f + 0.5f * sinf(now / 140.0f);
  uint16_t col[H_BAR_W];
  for (int x = 0; x < H_BAR_W; x++) {
    uint16_t c = trk;
    if (x < w) {
      c = flash ? C_RED : hGrad[x];
      float d = fabsf(x - hShimmer);
      if (d < 14) c = hMixWhite(c, 0.55f * (1.0f - d / 14.0f));                        // แสงวิ่ง
      int fromEnd = w - 1 - x;
      if (fromEnd < 3) c = hMixWhite(c, (0.30f + 0.45f * pulse) * (1.0f - fromEnd / 3.0f));   // ปลายแถบ
    } else if (x == w && wf - w > 0.05f) c = mix565(trk, flash ? C_RED : hGrad[x], wf - w);     // ปลาย sub-pixel
    col[x] = c;
  }
  for (int y = 0; y < HB_H; y++)
    for (int x = 0; x < HB_W; x++)
      hBarBuf[y * HB_W + x] = (y == 0 || y == HB_H - 1 || x == 0 || x == HB_W - 1) ? (uint16_t)TFT_BLACK : col[x - 1];
  lcd.pushImage(HB_X, HB_Y, HB_W, HB_H, (const lgfx::rgb565_t *)hBarBuf);   // บัฟเฟอร์เป็นสี RGB565 ปกติ (ไม่สลับไบต์)
}
void hRedrawSlot(int s) {                                    // เปลี่ยนค่าที่แสดงในช่องล่าง: ถอดพื้นหลัง+ป้ายใหม่
  int x0 = (s == 0) ? 2 : 90;
  lcd.setClipRect(x0, 154, 84, 80); lcd.drawJpg(hJpg, hJpgLen, 0, 0); hLabels(&lcd, 0, 0); lcd.clearClipRect();
  hBuildBg();
  hdRpm = hdSpeed = -999; hdShift = -1;
  hSlotTxt[s][0] = 0;
  if (s == 1) drawDots();
}
void hCycleSlot(int s) {                                     // แตะช่องล่าง -> ค่าถัดไปที่รถรองรับ (ไม่ซ้ำอีกช่อง)
  int other = slotMetric[1 - s], m = slotMetric[s];
  for (int i = 0; i < N_METRICS; i++) { m = (m + 1) % N_METRICS; if (m != other && metricAvailable(m)) break; }
  slotMetric[s] = m;
  prefs.putInt(s ? "slotB" : "slotA", m);
  hDispHave[s] = false;
  urgentMetric = m;
  hRedrawSlot(s);
}
void updateH(uint32_t now, float dt, bool over, bool fastChanged) {
  updateFace(now);
  bool blinkChanged = blinkOn != hPrevBlink; hPrevBlink = blinkOn;
  int shownRpm = ((int)lroundf(dispRpm) + 5) / 10 * 10, shownSpeed = (int)lroundf(dispSpeed);
  static uint32_t lastR = 0;                                 // เลขรอบเปลี่ยนไม่เกิน 20 ครั้ง/วินาที (เร็วกว่านี้ตาอ่านไม่ทันอยู่แล้ว)
  if ((shownRpm != hdRpm || over || over != hdRpmOver) && (now - lastR >= 50 || over != hdRpmOver)) {
    hDrawRpm(shownRpm, over); hdRpm = shownRpm; hdRpmOver = over; lastR = now;
  }
  if (shownSpeed != hdSpeed) { hDrawSpeed(shownSpeed); hdSpeed = shownSpeed; }
  for (int sl = 0; sl < 2; sl++) {
    int m = slotMetric[sl];
    bool have = metricHave[m];
    float target = metricVal[m];
    if (!have) hDispHave[sl] = false;
    else if (!hDispHave[sl]) { hDispSlot[sl] = target; hDispHave[sl] = true; }
    else easeTo(hDispSlot[sl], target, dt, 0.5f);
    char buf[12];
    if (!have) strcpy(buf, "--");
    else if (m == M_BATT || m == M_STFT || (m == M_MAF && hDispSlot[sl] < 100.0f)) snprintf(buf, sizeof(buf), "%.1f", hDispSlot[sl]);
    else snprintf(buf, sizeof(buf), "%d", (int)lroundf(hDispSlot[sl]));
    uint16_t col = C_TXT; bool bl = false;
    if (m == M_COOLANT && have && coolant >= tempAlert) { bl = true; col = blinkOn ? C_RED : C_WARN; }
    else if (m == M_BATT && have) col = (hDispSlot[sl] < 11.8f) ? C_RED : (hDispSlot[sl] > 14.8f ? C_WARN : C_TXT);
    if (strcmp(buf, hSlotTxt[sl]) != 0 || col != hSlotCol[sl] || (bl && blinkChanged)) { hDrawSlot(sl, buf, col); strcpy(hSlotTxt[sl], buf); hSlotCol[sl] = col; }
  }
  bool coolShown = (slotMetric[0] == M_COOLANT || slotMetric[1] == M_COOLANT);
  bool hot = !coolShown && metricHave[M_COOLANT] && coolant >= tempAlert;   // น้ำร้อนแต่ไม่ได้แสดง -> เตือนที่มุมจอ
  int stKey = (over ? 1 : 0) | (hot ? 2 : 0);
  if (curStatus() != hdStatus || stKey != hdStatKey || (over && fastChanged) || (hot && blinkChanged)) {
    hdStatus = curStatus(); hdStatKey = stKey; hDrawStatus(curStatus(), over, hot);
  }
  hDrawEdges(over && fastBlink);
  float ratio = constrain(dispRpm / (float)maxRpm, 0.0f, 1.0f);
  hDrawShift(ratio, over);
  hShimmer += (60.0f + 140.0f * ratio) * dt;
  if (hShimmer > ratio * H_BAR_W + 16) hShimmer = -16;
  hDrawRpmLine(now);
}
void hInitColors() {
  for (int x = 0; x < H_BAR_W; x++) {
    float t = x / (float)(H_BAR_W - 1); uint8_t r, g;
    if (t < 0.6f) { r = 90 + (uint8_t)(t / 0.6f * 165); g = 220; } else { r = 255; g = 220 - (uint8_t)((t - 0.6f) / 0.4f * 160); }
    hGrad[x] = lcd.color565(r, g, (uint8_t)(90 * (1.0f - t)));
  }
}
bool inRect(int x, int y, int rx, int ry, int rw, int rh);
bool hTap(int x, int y) {                                    // แตะช่องล่าง = สลับค่า (คืน true ถ้าใช้แตะนี้)
  if (inRect(x, y, 4, 150, 84, 88)) { hCycleSlot(0); return true; }
  if (inRect(x, y, 92, 150, 82, 88)) { hCycleSlot(1); return true; }
  return false;
}
#else
volatile int slotMetric[2] = {M_COOLANT, M_IAT};
void hFree() {} void hStatic() { lcd.fillScreen(TFT_BLACK); } void updateH(uint32_t, float, bool, bool) {} void hInitColors() {} bool hTap(int, int) { return false; } void hReset() {}
#endif

// =============================== หน้า 10 (PG_I) : รถวิ่ง (ภาพเคลื่อนไหวจาก GIF) / หน้า 11 (PG_J) : ภาพนิ่ง ===============================
// ทั้งสองหน้าใช้เลย์เอาต์เดียวกัน ต่างกันที่ภาพ (หน้า 11 ไม่มีส่วนเคลื่อนไหว)
// car.h = ภาพเต็มจอเฟรมแรก, wheels.h = ส่วนล่างที่เคลื่อนไหว (ROAD_Y ลงไป) สร้างด้วย tools/make_gif_page.py
// ส่วนบน (ท้องฟ้า) นิ่ง: รอบ + ความร้อนตัวเล็ก + แถบรอบ วาดทับภาพ โดยเก็บพื้นหลังใต้ตัวเลขไว้ในแรม (เฉพาะตอนอยู่หน้านี้ ~23 KB)
#if HAS_CARPAGE || HAS_PHOTO2 || HAS_CARPAGE2 || HAS_CARPAGE3
static const int8_t OL8F[8][2] = {{-1,-1},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1}};
struct FBox { int x, y, w, h; lgfx::LGFX_Sprite *bg; };
FBox fBox[3] = {{4, 4, 112, 48, nullptr}, {130, 4, 100, 48, nullptr}, {236, 4, 82, 16, nullptr}};    // รอบ / ความร้อน / สถานะ
const int F_BX = 6, F_BY = 58, F_BW = 228, F_BH = 6;                                                   // แถบรอบ
int fRoadIdx = -1; uint32_t fRoadT = 0;
const uint8_t *fImg = nullptr; uint32_t fImgLen = 0; bool fAnim = false;   // ภาพของหน้าที่แสดงอยู่
const uint8_t *const *fRoadJpg = nullptr; const unsigned int *fRoadLen = nullptr;   // เฟรมเคลื่อนไหวของหน้าที่แสดงอยู่ (หน้า 10 / 12)
int fRoadN = 0, fRoadY = 0; float fRoadFps = 25.0f;
void fFree() { for (auto &b : fBox) if (b.bg) { b.bg->deleteSprite(); delete b.bg; b.bg = nullptr; } }
void fAlloc() {
  poolReset(PSPR_BYTES);                                    // หน้า 10/11: ต่อจากสไปรท์ตัวเลข (อยู่ต้นก้อน)
  for (auto &b : fBox) {
    if (b.bg) continue;
    b.bg = new lgfx::LGFX_Sprite(&lcd);
    if (!sprFromPool(b.bg, b.w, b.h)) { delete b.bg; b.bg = nullptr; Serial.println("car page bg alloc FAILED"); continue; }
    b.bg->drawJpg(fImg, fImgLen, 0, 0, b.w, b.h, b.x, b.y);
  }
}
// วาดข้อความ (มีขอบดำ) ลงกล่อง: พื้นหลังจากภาพที่เก็บไว้ -> สไปรท์ -> ดันขึ้นจอทีเดียว (ไม่กะพริบ)
void fBoxDraw(int i, const char *lab, const char *val, const char *unit, uint16_t col) {
  FBox &b = fBox[i];
  bool viaSpr = pSprOk && b.w <= PSPR_W && b.h <= PSPR_H && b.bg;
  lgfx::LGFXBase *g = viaSpr ? (lgfx::LGFXBase *)&pSpr : (lgfx::LGFXBase *)&lcd;
  int ox = viaSpr ? 0 : b.x, oy = viaSpr ? 0 : b.y;
  if (viaSpr) b.bg->pushSprite(&pSpr, 0, 0);
  else if (b.bg) b.bg->pushSprite(b.x, b.y);
  auto outl = [&](const char *s, int x, int y, uint16_t c, int sh) {
    g->setTextColor(TFT_BLACK);
    for (auto &o : OL8F) g->drawString(s, ox + x + o[0] * sh, oy + y + o[1] * sh);
    g->setTextColor(c); g->drawString(s, ox + x, oy + y);
  };
  g->setTextDatum(lgfx::textdatum_t::top_left);
  if (lab) { g->setFont(&fonts::Font2); g->setTextSize(1.0f); outl(lab, 2, 0, C_LABEL, 1); }
  if (unit) { g->setFont(&fonts::Font2); g->setTextSize(1.0f); outl(unit, b.w - 2 - g->textWidth(unit), 31, C_LABEL, 1); }
  if (val) {
    if (lab) { g->setFont(&fonts::Font7); g->setTextSize(0.6f); outl(val, 2, 17, col, 1); }
    else     { g->setFont(&fonts::Font2); g->setTextSize(1.0f); outl(val, b.w - 2 - g->textWidth(val), 0, col, 1); }
  }
  g->setTextSize(1.0f);
  if (viaSpr) { lcd.setClipRect(b.x, b.y, b.w, b.h); pSpr.pushSprite(b.x, b.y); lcd.clearClipRect(); }
}
uint16_t fColAt(int x, bool lit, bool flash) {
  int xr = (int)(F_BW * constrain(maxRpm / (gScaleMax() * 1000.0f), 0.0f, 1.0f));
  bool red = x >= xr;
  return lit ? ((flash || red) ? C_RED : lcd.color565(70, 200, 255)) : (red ? lcd.color565(70, 14, 18) : lcd.color565(22, 26, 44));
}
void fBarSpan(int x0, int x1, int litW, bool flash) {
  for (int x = x0; x < x1;) {
    uint16_t c = fColAt(x, x < litW, flash);
    int e = x + 1;
    while (e < x1 && fColAt(e, e < litW, flash) == c) e++;
    lcd.fillRect(F_BX + x, F_BY, e - x, F_BH, c);
    x = e;
  }
}
void fStatic() {
  fFree();                                                   // หน้า 10 / 11 / 12 ใช้ภาพคนละภาพ: ถอดพื้นหลังใหม่
#if HAS_CARPAGE
  if (page == PG_I) { fImg = car_jpg; fImgLen = car_jpg_len; fAnim = true;
    fRoadJpg = ROAD_JPG; fRoadLen = ROAD_LEN; fRoadN = N_ROAD_FRAMES; fRoadY = ROAD_Y; fRoadFps = BAND_FPS; }
#endif
#if HAS_CARPAGE2
  if (page == PG_L) { fImg = car2_jpg; fImgLen = car2_jpg_len; fAnim = true;
    fRoadJpg = ROAD2_JPG; fRoadLen = ROAD2_LEN; fRoadN = N_ROAD2_FRAMES; fRoadY = ROAD2_Y; fRoadFps = BAND2_FPS; }
#endif
#if HAS_CARPAGE3
  if (page == PG_K) { fImg = car3_jpg; fImgLen = car3_jpg_len; fAnim = true;
    fRoadJpg = ROAD3_JPG; fRoadLen = ROAD3_LEN; fRoadN = N_ROAD3_FRAMES; fRoadY = ROAD3_Y; fRoadFps = BAND3_FPS; }
#endif
#if HAS_PHOTO2
  if (page == PG_J) { fImg = photo2_jpg; fImgLen = photo2_jpg_len; fAnim = false; }
#endif
  lcd.drawJpg(fImg, fImgLen, 0, 0);
  fAlloc();
  lcd.fillRect(F_BX - 1, F_BY - 1, F_BW + 2, F_BH + 2, TFT_BLACK);
  int sm = gScaleMax();
  for (int n = 1; n < sm; n++) lcd.drawFastVLine(F_BX + F_BW * n / sm, F_BY - 3, 2, (n * 1000 >= maxRpm) ? C_RED : C_LABEL);
  fBarSpan(0, F_BW, 0, false);
  fRoadIdx = -1;
}
void updateF(uint32_t now, bool over) {
  char b[8];
  bool flash = over && fastBlink;
  // ภาพเคลื่อนไหวส่วนล่าง (หน้า 10 / 12): เล่นทีละเฟรมตามจังหวะ GIF
#if HAS_CARPAGE || HAS_CARPAGE2 || HAS_CARPAGE3
  const float fps = (CAR_FPS > 0) ? (float)CAR_FPS : fRoadFps;
  if (fAnim && fRoadN > 0 && (fRoadIdx < 0 || now - fRoadT >= (uint32_t)(1000.0f / fps))) {
    fRoadT = (fRoadIdx < 0) ? now : fRoadT + (uint32_t)(1000.0f / fps);
    if (now - fRoadT > 200) fRoadT = now;                      // ช้าเกิน (เช่นหลังเมนู) ไม่ต้องไล่ตาม
    fRoadIdx = (fRoadIdx + 1) % fRoadN;
    lcd.drawJpg(fRoadJpg[fRoadIdx], fRoadLen[fRoadIdx], 0, fRoadY);
    drawDots();
  }
#endif
  contBarUpdate(dispRpm / (gScaleMax() * 1000.0f) * F_BW, F_BW, flash, F_BX, F_BY, F_BH, fBarSpan, fColAt);
  fmtRpm(b);
  static uint32_t lastR = 0;
  uint16_t rc = flash ? C_RED : C_TXT;
  if ((now - lastR >= 80 || rc != pCol[0]) && chgTxt(0, b, rc)) { lastR = now; fBoxDraw(0, "RPM", b, nullptr, rc); }
  fmtV(b, coolHave(), pd[M_COOLANT], 0);
  uint16_t cc = coolCol(C_TXT);
  if (chgTxt(1, b, cc)) fBoxDraw(1, "COOLANT", b, "deg C", cc);
  bool o2 = rpm >= maxRpm;
  const char *msg = o2 ? (fastBlink ? "REDLINE" : "") : curStatus();
  uint16_t sc = o2 ? C_RED : (strcmp(msg, "OBD OK") == 0 ? C_OK : C_WARN);
  if (chgTxt(2, msg, sc)) fBoxDraw(2, nullptr, msg, nullptr, sc);
}
#else
void fFree() {}
void fStatic() { lcd.fillScreen(TFT_BLACK); }
void updateF(uint32_t, bool) {}
#endif

// =============================== หน้า 2 (PG_DN) : GIF นักเต้นเต็มจอ + ค่าซ้อนทับด้านบน (เลย์เอาต์เดียวกับหน้ารถวิ่งเดิม) ===============================
// GIF ขยับทั้งจอ จึงเก็บเฟรมละ 2 ชิ้น: ส่วนบน (DANCE_TOP_H แถว) ถอดลงสไปรท์ 320xN -> วาดตัวเลข RPM / COOLANT / แถบรอบ / สถานะ ทับ -> ดันขึ้นจอทีเดียว (ไม่กะพริบ)
// ส่วนล่างถอดตรงขึ้นจอ (เฟรมใหม่ทับเฟรมเก่าทั้งผืน) สไปรท์แถบบนจองจากก้อนแรมกลาง (~45 KB) เฉพาะตอนอยู่หน้านี้
#if HAS_DANCE
static const int8_t OL8D[8][2] = {{-1,-1},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1}};
static const int8_t OL4D[4][2] = {{0,-1},{-1,0},{1,0},{0,1}};
const int DN_SPR_H = 72;                                     // สไปรท์แถบ 320x72 (ใช้ซ้ำทุกแถบ: ถอดทีละแถบแล้วดันขึ้นจอทีเดียว เร็วกว่าถอดตรงขึ้นจอทีละบล็อก)
lgfx::LGFX_Sprite dnBand(&lcd);
bool dnBandOk = false;
int dnIdx = -1; uint32_t dnT = 0;
char dnR[8] = "", dnC[8] = "", dnS[12] = ""; uint16_t dnRc = 0, dnCc = 0, dnSc = 0;
const int DN_BX = 6, DN_BY = 58, DN_BW = 228, DN_BH = 6;     // แถบรอบ
void dnFree() { if (dnBandOk) { dnBand.deleteSprite(); dnBandOk = false; } }
void dnStatic() {
  dnFree();
  poolReset(0);                                              // หน้านี้ใช้ก้อนกลางตั้งแต่ต้นก้อน (สไปรท์ตัวเลขถูกปลดก่อนแล้ว)
  dnBandOk = sprFromPool(&dnBand, 320, DN_SPR_H);
  if (!dnBandOk) Serial.println("dance band sprite FAILED -> draw direct");
  lcd.fillScreen(TFT_BLACK);
  dnIdx = -1;
}
uint16_t dnColAt(int x, bool lit, bool flash) {
  int xr = (int)(DN_BW * constrain(maxRpm / (gScaleMax() * 1000.0f), 0.0f, 1.0f));
  bool red = x >= xr;
  return lit ? ((flash || red) ? C_RED : lcd.color565(70, 200, 255)) : (red ? lcd.color565(70, 14, 18) : lcd.color565(22, 26, 44));
}
void dnOutl(lgfx::LGFXBase *g, const char *s, int x, int y, uint16_t c, bool big) {   // ตัวเลขใหญ่ใช้ขอบ 4 ทิศ (เร็วกว่า 8 ทิศเท่าตัว)
  g->setTextColor(TFT_BLACK);
  if (big) for (auto &o : OL4D) g->drawString(s, x + o[0], y + o[1]);
  else     for (auto &o : OL8D) g->drawString(s, x + o[0], y + o[1]);
  g->setTextColor(c); g->drawString(s, x, y);
}
void dnTopText(lgfx::LGFXBase *g, int lit, bool flash) {     // ตัวเลขทับแถบบน (พิกัดจอ แถบอยู่ที่ y=0)
  g->setTextDatum(lgfx::textdatum_t::top_left);
  g->setFont(&fonts::Font2); g->setTextSize(1.0f);
  dnOutl(g, "RPM", 6, 4, C_LABEL, false);
  dnOutl(g, "COOLANT", 132, 4, C_LABEL, false);
  dnOutl(g, "deg C", 228 - g->textWidth("deg C"), 35, C_LABEL, false);
  if (dnS[0]) dnOutl(g, dnS, 316 - g->textWidth(dnS), 4, dnSc, false);
  g->setFont(&fonts::Font7); g->setTextSize(0.6f);
  dnOutl(g, dnR, 6, 21, dnRc, true);
  dnOutl(g, dnC, 132, 21, dnCc, true);
  g->setTextSize(1.0f);
  g->fillRect(DN_BX - 1, DN_BY - 1, DN_BW + 2, DN_BH + 2, TFT_BLACK);
  int sm = gScaleMax();
  for (int n = 1; n < sm; n++) g->drawFastVLine(DN_BX + DN_BW * n / sm, DN_BY - 3, 2, (n * 1000 >= maxRpm) ? C_RED : C_LABEL);
  for (int x = 0; x < DN_BW;) {
    uint16_t c = dnColAt(x, x < lit, flash);
    int e = x + 1;
    while (e < DN_BW && dnColAt(e, e < lit, flash) == c) e++;
    g->fillRect(DN_BX + x, DN_BY, e - x, DN_BH, c);
    x = e;
  }
}
void dnUpdate(uint32_t now, bool over) {
  const uint32_t frameMs = 1000 / (DANCE_FPS > 0 ? DANCE_FPS : 10);
  if (dnIdx < 0) dnT = now;
  else if (now - dnT >= frameMs) { dnT += frameMs; if (now - dnT > 200) dnT = now; }   // ช้าเกิน (เช่นหลังเมนู) ไม่ต้องไล่ตาม
  else return;
  dnIdx = (dnIdx + 1) % DANCE_N;
  bool flash = over && fastBlink;
  fmtRpm(dnR); fmtV(dnC, coolHave(), pd[M_COOLANT], 0);
  dnRc = flash ? C_RED : C_TXT; dnCc = coolCol(C_TXT);
  const char *msg = over ? (fastBlink ? "REDLINE" : "") : curStatus();
  strncpy(dnS, msg, sizeof(dnS) - 1); dnS[sizeof(dnS) - 1] = 0;
  dnSc = over ? C_RED : (strcmp(msg, "OBD OK") == 0 ? C_OK : C_WARN);
  int lit = (int)(DN_BW * constrain(dispRpm / (gScaleMax() * 1000.0f), 0.0f, 1.0f));
  // วาดจากบนลงล่างทีละแถบ (ตัวเลขทับแถบแรกก่อนดันขึ้นจอ) -> หัว/ลำตัวอัปเดตต่อเนื่องกันภายในไม่กี่ ms ไม่มีรอยต่อเวลา
  for (int k = 0; k < DANCE_STRIPS; k++) {
    int y0 = DANCE_STRIP_Y[k], h = DANCE_STRIP_Y[k + 1] - y0, id = dnIdx * DANCE_STRIPS + k;
    if (dnBandOk) {
      dnBand.drawJpg(DANCE_PTR[id], DANCE_LEN[id], 0, 0);
      if (k == 0) dnTopText(&dnBand, lit, flash);
      lcd.setClipRect(0, y0, 320, h); dnBand.pushSprite(0, y0); lcd.clearClipRect();
    } else {                                                  // ไม่มีสไปรท์: วาดตรงบนจอ (อาจกะพริบ)
      lcd.drawJpg(DANCE_PTR[id], DANCE_LEN[id], 0, y0);
      if (k == 0) dnTopText(&lcd, lit, flash);
    }
  }
  drawDots();
}
#else
void dnFree() {} void dnStatic() { lcd.fillScreen(TFT_BLACK); } void dnUpdate(uint32_t, bool) {}
#endif

void p3Static();
void p4Static();
void drawStatic(bool) {                                    // วาดส่วนคงที่ของหน้าปัจจุบัน
  if (!isHPage(page)) hFree();
  if (!(page == PG_DN)) dnFree();
  if (isPoolPage(page)) pSprFree();            // หน้า 9 / 13 ใช้แรมพื้นหลังของตัวเอง (~67 KB) แทนสไปรท์ตัวเลข
  if (!isFPage(page)) fFree();                             // หน้า 10/11/12: พื้นหลังใต้ตัวเลข (~23 KB) เฉพาะตอนอยู่หน้านี้
  pSprEnsure();
  switch (page) {
    case PG_B: bStatic(); break;
    case PG_C: cStatic(); break;
    case PG_D: dStatic(); break;
    case PG_E: eStatic(); break;
    case PG_F: gStatic(); break;
    case PG_H: hStatic(); break;
    case PG_DN: dnStatic(); break;
    case PG_K: fStatic(); break;
    case PG_I: fStatic(); break;
    case PG_J: fStatic(); break;
    case PG_L: fStatic(); break;
    case PAGE_GAUGE: p3Static(); break;
    default: p4Static(); break;
  }
  drawDots();
}

void updateUI() {
  uint32_t now = millis();
  if (now - lastFrame < FRAME_MS) return;
  float dt = (now - lastFrame) / 1000.0f;
  if (dt > 0.1f) dt = 0.1f;                                    // หลังออกจากเมนู/เปลี่ยนหน้า ไม่ให้กระโดด
  lastFrame = now;

  if (now - lastBlink > 350) { lastBlink = now; blinkOn = !blinkOn; }
  bool fastChanged = false;
  if (now - lastFast > 110) { lastFast = now; fastBlink = !fastBlink; fastChanged = true; }

  float tgR = rpm, tgS = speedKmh;
  if (!demoMode && obdOk) interpGet(now, &tgR, &tgS);          // เดโมเป็นค่าต่อเนื่องอยู่แล้ว ไม่ต้องไล่ค่า
  springTo(dispRpm,   velRpm,   tgR, dt, 30.0f);
  springTo(dispSpeed, velSpeed, tgS, dt, 14.0f);
  easeTo(dispTemp,  coolant,  dt, 0.50f);
  peaksUpdate(now);
  pkHoldUpdate(now, dt);
  bool rpmOver = rpm >= maxRpm;
  if (!demoMode) tripKm += speedKmh * dt / 3600.0f;          // ระยะทริป

  // สไปรท์ตัวเลข (~13 KB) ใช้เมื่อ OBD ต่อติด/เดโมเท่านั้น: ตอนกำลังต่อ BT ให้แรมกับ Bluetooth
  static uint32_t lostAt = 0;
  if (obdOk || demoMode) { lostAt = 0; pSprWanted = true; }
  else if (lostAt == 0) lostAt = now;
  else if (now - lostAt > 1500 && pSprWanted) { pSprWanted = false; pSprFree(); }

  pSprEnsure();
  pdUpdate(dt);
  uint32_t us0 = micros();
  switch (page) {
    case PG_B: updateB(now, rpmOver); break;
    case PG_C: updateC(now, rpmOver); break;
    case PG_D: updateD(now, rpmOver); break;
    case PG_E: updateE(now, rpmOver); break;
    case PG_F: updateG(now, rpmOver); break;
    case PG_H: updateH(now, dt, rpmOver, fastChanged); break;
    case PG_DN: dnUpdate(now, rpmOver); break;
    case PG_K: updateF(now, rpmOver); break;
    case PG_I: updateF(now, rpmOver); break;
    case PG_J: updateF(now, rpmOver); break;
    case PG_L: updateF(now, rpmOver); break;
    case PAGE_GAUGE: updatePage3(now, dt, rpmOver, fastChanged); break;
    default: updatePage4(now, dt, rpmOver, fastChanged); break;
  }
#if FPS_LOG
  uint32_t du = micros() - us0;
  fpsFrames++; fpsDrawUs += du; if (du > fpsMaxUs) fpsMaxUs = du;
  if (now - fpsT0 >= 5000) {
    if (fpsT0) Serial.printf("page %d: %.1f fps, draw avg %.1f ms max %.1f ms, interp delay %.0f ms\n", page + 1,
                             fpsFrames * 1000.0f / (now - fpsT0), fpsDrawUs / 1000.0f / (float)(fpsFrames ? fpsFrames : 1), fpsMaxUs / 1000.0f, interpDelay);
    fpsT0 = now; fpsFrames = 0; fpsDrawUs = 0; fpsMaxUs = 0;
  }
#endif
}

void invalidateUI() { pReset(); }


// ---------- เมนู ----------
bool inRect(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

void waitRelease() {
  int32_t x, y;
  while (getTouchXY(&x, &y)) delay(10);
  delay(60);
}

void drawBtn(int x, int y, int w, int h, const char *label, uint16_t col) {
  lcd.fillRoundRect(x, y, w, h, 8, col);
  lcd.setFont(&fonts::FreeSansBold12pt7b);
  lcd.setTextColor(C_BG, col);
  lcd.setCursor(x + (w - lcd.textWidth(label)) / 2, y + (h - lcd.fontHeight()) / 2);
  lcd.print(label);
}

void drawSmallBtn(int x, int y, int w, int h, const String &label, uint16_t col) {
  lcd.fillRoundRect(x, y, w, h, 6, col);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(C_BG, col);
  lcd.setCursor(x + (w - lcd.textWidth(label.c_str())) / 2, y + (h - lcd.fontHeight()) / 2);
  lcd.print(label);
}



// ---- หน้า Bluetooth ----
struct BtDev { String name; String mac; };
BtDev devs[8];
int devCount = 0, devPage = 0;

void btStatus(const String &s) {
  lcd.fillRect(0, 22, 246, 20, C_BG);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(C_DIM, C_BG);
  lcd.setCursor(8, 24);
  lcd.print(s);
}

void btDrawList() {
  lcd.fillRect(0, 44, 320, 152, C_BG);
  lcd.setFont(&fonts::Font2);
  for (int i = 0; i < 4; i++) {
    int idx = devPage * 4 + i;
    if (idx >= devCount) break;
    int y = 46 + i * 38;
    bool sel = (devs[idx].mac == btMac);
    lcd.drawRoundRect(8, y, 304, 34, 6, sel ? C_OK : C_PINK);
    lcd.setTextColor(sel ? C_OK : C_TXT, C_BG);
    lcd.setCursor(16, y + 2);
    lcd.print(devs[idx].name.length() ? devs[idx].name : String("(no name)"));
    lcd.setTextColor(C_DIM, C_BG);
    lcd.setCursor(16, y + 17);
    lcd.print(devs[idx].mac);
  }
  if (devCount > 4) drawSmallBtn(250, 22, 62, 20, "MORE", C_PINK);
  else lcd.fillRect(250, 22, 62, 20, C_BG);
}

void btScan() {
#if LINK_ESPNOW
  return;
#else
  btStatus("Scanning... (8 sec)");
  devCount = 0; devPage = 0;
  BTScanResults *res = SerialBT.discover(8000);
  if (res) {
    int n = res->getCount();
    for (int i = 0; i < n && devCount < 8; i++) {
      BTAdvertisedDevice *d = res->getDevice(i);
      if (!d) continue;
      devs[devCount].name = String(d->getName().c_str());
      devs[devCount].mac  = String(d->getAddress().toString().c_str());
      devCount++;
    }
  }
  btDrawList();
  btStatus(devCount ? ("Found " + String(devCount) + " - tap to select") : String("No device found"));
#endif
}

#if LINK_ESPNOW
void nowScreen() {                     // หน้า "Bluetooth" ในโหมด ESP-NOW = ดูสถานะการรับค่าจาก S3
  lcd.fillScreen(C_BG);
  lcd.setFont(&fonts::FreeSansBold12pt7b);
  lcd.setTextColor(C_PINK, C_BG);
  lcd.setCursor(70, 2); lcd.print("LINK (ESP-NOW)");
  drawSmallBtn(216, 200, 96, 36, "BACK", C_PINK);
  waitRelease();
  uint32_t lastDraw = 0, p0 = nowPkts, t0 = millis();
  int rate = 0;
  while (true) {
    int32_t x, y;
    if (getTouchXY(&x, &y) && inRect(x, y, 216, 200, 96, 36)) break;
    uint32_t now = millis();
    nowTick(now);
    if (now - t0 >= 1000) { rate = nowPkts - p0; p0 = nowPkts; t0 = now; }
    if (now - lastDraw > 300) {
      lastDraw = now;
      bool alive = nowLast && now - nowLast < 2000;
      lcd.fillRect(0, 40, 320, 150, C_BG);
      lcd.setFont(&fonts::FreeSansBold9pt7b);
      lcd.setTextColor(alive ? C_OK : C_RED, C_BG);
      lcd.setCursor(12, 48); lcd.print(alive ? "S3 board: connected" : "S3 board: no signal");
      lcd.setTextColor(C_TXT, C_BG);
      const char *ls = nowLink == DN_LINK_OK ? "OBD OK" : (nowLink == DN_LINK_DEMO ? "S3 in DEMO" : "S3 waiting for OBD");
      lcd.setCursor(12, 78); lcd.print(String("OBD: ") + (alive ? ls : "-"));
      lcd.setCursor(12, 108); lcd.print(String("WiFi channel: ") + nowCh);
      lcd.setCursor(12, 138); lcd.print(String("Packets/s: ") + rate);
      lcd.setFont(&fonts::Font2);
      lcd.setTextColor(C_DIM, C_BG);
      lcd.setCursor(12, 170); lcd.print("Values come from the Didissnap S3 board");
    }
    delay(10);
  }
  waitRelease();
}
#endif
void btScreen() {
#if LINK_ESPNOW
  nowScreen();
  return;
#endif
  lcd.fillScreen(C_BG);
  lcd.setFont(&fonts::FreeSansBold12pt7b);
  lcd.setTextColor(C_PINK, C_BG);
  lcd.setCursor(100, 2); lcd.print("BLUETOOTH");
  btStatus("Please wait...");
  pauseOBD();                          // หยุดอ่าน OBD ชั่วคราวเพื่อสแกนได้
  devCount = 0; devPage = 0;
  btStatus(btName.length() ? ("Saved: " + btName) : String("No device saved (auto: ") + OBD_NAME + ")");
  btDrawList();
  drawSmallBtn(8, 200, 96, 36, "SCAN", C_OK);
  drawSmallBtn(112, 200, 96, 36, String("PIN ") + btPin, C_PINK);
  drawSmallBtn(216, 200, 96, 36, "BACK", C_PINK);

  waitRelease();
  while (true) {
    int32_t x, y;
    if (getTouchXY(&x, &y)) {
      if (inRect(x, y, 216, 200, 96, 36)) break;
      else if (inRect(x, y, 8, 200, 96, 36)) { btScan(); }
      else if (inRect(x, y, 112, 200, 96, 36)) {
        strcpy(btPin, strcmp(btPin, "1234") == 0 ? "0000" : "1234");
        prefs.putString("btPin", btPin);
        drawSmallBtn(112, 200, 96, 36, String("PIN ") + btPin, C_PINK);
      }
      else if (devCount > 4 && inRect(x, y, 250, 22, 62, 20)) {
        devPage = (devPage + 1) % ((devCount + 3) / 4);
        btDrawList();
      }
      else {
        for (int i = 0; i < 4; i++) {
          int idx = devPage * 4 + i;
          if (idx < devCount && inRect(x, y, 8, 46 + i * 38, 304, 34)) {
            if (devs[idx].mac == btMac) {            // แตะซ้ำ = ยกเลิกการเลือก
              btMac = ""; btName = "";
              btStatus(String("Cleared (auto: ") + OBD_NAME + ")");
            } else {
              btMac = devs[idx].mac;
              btName = devs[idx].name.length() ? devs[idx].name : devs[idx].mac;
              btStatus("Saved: " + btName);
            }
            prefs.putString("btMac", btMac);
            prefs.putString("btName", btName);
            btDrawList();
          }
        }
      }
      waitRelease();
    }
    delay(10);
  }
  resumeOBD();
  waitRelease();
}

// ---- หน้า Display (ปรับแสง) ----
void drawBriValue() {
  lcd.fillRect(76, 44, 168, 40, C_BG);
  lcd.setFont(&fonts::FreeSansBold18pt7b);
  lcd.setTextColor(C_TXT, C_BG);
  String v = briAuto ? String("AUTO") : (String(briPct) + "%");
  lcd.setCursor(160 - lcd.textWidth(v.c_str()) / 2, 48);
  lcd.print(v);
}

void drawModeBtn() {
  drawSmallBtn(8, 108, 304, 40, briAuto ? "MODE: AUTO (light sensor)" : "MODE: MANUAL", briAuto ? C_OK : C_PINK);
}

void drawDemoBtn() { drawBtn(8, 200, 96, 36, "DEMO", demoMode ? C_WARN : lcd.color565(120, 190, 255)); }
void drawFlipBtn() { drawSmallBtn(110, 200, 100, 36, flip180 ? "FLIP 180: ON" : "FLIP 180", flip180 ? C_OK : C_PINK); }

void drawDisplayScreen() {
  lcd.fillScreen(C_BG);
  lcd.setFont(&fonts::FreeSansBold12pt7b);
  lcd.setTextColor(C_PINK, C_BG);
  lcd.setCursor(112, 2); lcd.print("DISPLAY");
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(C_DIM, C_BG);
  lcd.setCursor(10, 28); lcd.print("BRIGHTNESS");
  lcd.setCursor(10, 92); lcd.print("MODE");
  lcd.setCursor(10, 156); lcd.print("AUTO dims in the dark, brightens in daylight");
  lcd.setCursor(10, 176);
  lcd.print(String("Last reset: ") + prefs.getString("lastRst", "none") + "  crashes: " + prefs.getInt("crashes", 0));
  lcd.setCursor(10, 188);
  lcd.print(String("Min heap: ") + (prefs.getUInt("minHeap", 0) / 1024) + " KB  after BT: " + (prefs.getUInt("btHeap", 0) / 1024) + " KB");
  lcd.setCursor(10, 200);
  lcd.print(String("RAM pool: ") + (int)(gPoolSize / 1024) + " KB " + (gPool ? (gPoolLite ? "(LITE)" : "(FULL)") : "(OFF)") + "  free now: " + (int)(ESP.getFreeHeap() / 1024) + " KB");
  drawBtn(8, 44, 60, 40, "-", C_PINK);  drawBtn(252, 44, 60, 40, "+", C_PINK);
  drawBriValue();
  drawModeBtn();
  drawDemoBtn();
  drawFlipBtn();
  drawBtn(216, 200, 96, 36, "BACK", C_OK);
}

bool displayScreen() {                                   // คืน true = เริ่มเดโมแล้ว (ให้ออกจากเมนูไปหน้าหลักเลย)
  bool leaveMenu = false;
  drawDisplayScreen();
  waitRelease();
  uint32_t lastTap = 0;
  while (true) {
    int32_t x, y;
    if (getTouchXY(&x, &y) && millis() - lastTap > 160) {
      lastTap = millis();
      if (inRect(x, y, 8, 44, 60, 40))        { briPct = max(10, briPct - 10);  briAuto = false; applyBrightness(briPct); drawBriValue(); drawModeBtn(); }
      else if (inRect(x, y, 252, 44, 60, 40)) { briPct = min(100, briPct + 10); briAuto = false; applyBrightness(briPct); drawBriValue(); drawModeBtn(); }
      else if (inRect(x, y, 8, 108, 304, 40)) {
        briAuto = !briAuto;
        if (!briAuto) applyBrightness(briPct);
        drawBriValue(); drawModeBtn();
        waitRelease();
      }
      else if (inRect(x, y, 110, 200, 100, 36)) {          // FLIP 180: กลับจอทันที แล้ววาดหน้านี้ใหม่
        flip180 = !flip180;
        prefs.putBool("flip", flip180);
        applyFlip();
        drawDisplayScreen();
        waitRelease();
      }
      else if (inRect(x, y, 8, 200, 96, 36)) {
        if (demoMode) { demoStop(); drawDemoBtn(); waitRelease(); }                 // กดอีกครั้ง = หยุดเดโม
        else { demoMode = true; leaveMenu = true; break; }                        // เริ่มเดโม แล้วออกไปดูที่หน้าจอหลักเลย
      }
      else if (inRect(x, y, 216, 200, 96, 36)) break;
    }
    updateBrightness();
    delay(10);
  }
  prefs.putInt("bri", briPct);
  prefs.putBool("briAuto", briAuto);
  return leaveMenu;
}

// ---- DTC (รหัสข้อขัดข้อง) ----
struct DtcDesc { const char *code; const char *txt; };
const DtcDesc DTC_TABLE[] = {
  {"P0100", "MAF sensor circuit"}, {"P0101", "MAF sensor range"}, {"P0102", "MAF sensor low"},
  {"P0103", "MAF sensor high"}, {"P0105", "MAP sensor circuit"}, {"P0110", "IAT sensor circuit"},
  {"P0112", "IAT sensor low"}, {"P0113", "IAT sensor high"}, {"P0115", "Coolant temp circuit"},
  {"P0117", "Coolant temp low"}, {"P0118", "Coolant temp high"}, {"P0120", "Throttle pos circuit"},
  {"P0125", "Coolant too cold"}, {"P0128", "Thermostat below temp"}, {"P0130", "O2 sensor B1S1 circuit"},
  {"P0133", "O2 sensor B1S1 slow"}, {"P0135", "O2 heater B1S1"}, {"P0171", "System too lean B1"},
  {"P0172", "System too rich B1"}, {"P0174", "System too lean B2"}, {"P0175", "System too rich B2"},
  {"P0201", "Injector cyl 1 circuit"}, {"P0202", "Injector cyl 2 circuit"}, {"P0203", "Injector cyl 3 circuit"},
  {"P0204", "Injector cyl 4 circuit"}, {"P0300", "Random misfire"}, {"P0301", "Misfire cylinder 1"},
  {"P0302", "Misfire cylinder 2"}, {"P0303", "Misfire cylinder 3"}, {"P0304", "Misfire cylinder 4"},
  {"P0325", "Knock sensor circuit"}, {"P0335", "Crank sensor circuit"}, {"P0340", "Cam sensor circuit"},
  {"P0401", "EGR flow low"}, {"P0420", "Catalyst eff. low B1"}, {"P0430", "Catalyst eff. low B2"},
  {"P0440", "EVAP system"}, {"P0442", "EVAP small leak"}, {"P0455", "EVAP large leak"},
  {"P0500", "Vehicle speed sensor"}, {"P0505", "Idle control system"}, {"P0562", "System voltage low"},
  {"P0563", "System voltage high"}, {"P0601", "ECM memory error"}, {"P0700", "Transmission control"},
  {"U0100", "Lost comm with ECM"}
};

const char *dtcText(const String &c) {
  for (size_t i = 0; i < sizeof(DTC_TABLE) / sizeof(DTC_TABLE[0]); i++)
    if (c.equals(DTC_TABLE[i].code)) return DTC_TABLE[i].txt;
  return "see service manual";
}

String dtcFromBytes(int b0, int b1) {                // 2 ไบต์ -> เช่น P0133
  char t[8];
  snprintf(t, sizeof(t), "%c%d%X%X%X", "PCBU"[(b0 >> 6) & 3], (b0 >> 4) & 3, b0 & 15, (b1 >> 4) & 15, b1 & 15);
  return String(t);
}

// เหมือน elm() แต่เก็บ '\r' ไว้ เพื่อแยกบรรทัด (ตอบกลับ Mode 03 หลายเฟรมต้องแยกบรรทัด)
String elmRaw(const char *cmd, uint32_t timeoutMs) {
  while (SerialBT.available()) SerialBT.read();
  SerialBT.print(cmd);
  SerialBT.print("\r");
  String r;
  uint32_t t = millis();
  while (millis() - t < timeoutMs) {
    while (SerialBT.available()) {
      char c = SerialBT.read();
      if (c == '>') return r;
      if (c == '\n' || c == ' ') continue;
      r += c;
    }
    delay(1);
  }
  return r;
}

bool allHex(const String &s) {
  for (size_t i = 0; i < s.length(); i++) if (!isxdigit((unsigned char)s[i])) return false;
  return s.length() > 0;
}
int hex2(const String &s, int i) { return (int)strtol(s.substring(i, i + 2).c_str(), NULL, 16); }

#define MAX_DTC 16
String dtcList[MAX_DTC];
int    dtcN = 0;
bool   dtcMil = false;
int    dtcReportedCount = -1;

// อ่านรหัสที่เก็บไว้ (Mode 03): return จำนวนรหัส, -1 = ผิดพลาด
int dtcRead() {
  dtcN = 0; dtcMil = false; dtcReportedCount = -1;
#if LINK_ESPNOW
  if (!nowDtc(DN_CMD_DTC_READ) || !nowDtcRes.ok) return -1;
  for (int i = 0; i < nowDtcRes.n && dtcN < MAX_DTC; i++) dtcList[dtcN++] = String(nowDtcRes.codes[i]);
  return dtcN;
#endif
  int b[2];
  if (readPid("0101", "4101", 1, b)) { dtcMil = (b[0] & 0x80) != 0; dtcReportedCount = b[0] & 0x7F; }

  String pr = elmRaw("ATDPN", 800);                    // โปรโตคอลที่ใช้อยู่ (6-9 = CAN)
  bool can = pr.indexOf('6') >= 0 || pr.indexOf('7') >= 0 || pr.indexOf('8') >= 0 || pr.indexOf('9') >= 0;

  String r = elmRaw("03", 6000);
  if (r.indexOf("NODATA") >= 0) return 0;
  if (r.indexOf("UNABLE") >= 0 || r.indexOf("ERROR") >= 0 || r.indexOf('?') >= 0 || r.length() == 0) return -1;

  String lines[12]; int nl = 0, st = 0;
  for (int i = 0; i <= (int)r.length() && nl < 12; i++) {
    if (i == (int)r.length() || r[i] == '\r') {
      String ln = r.substring(st, i); st = i + 1; ln.trim();
      if (ln.length() >= 2 && ln[1] == ':') ln = ln.substring(2);     // "0:" "1:" ของเฟรมต่อเนื่อง
      if (!allHex(ln) || ln.length() == 3) continue;                  // ข้าม SEARCHING... และบรรทัดบอกความยาว
      lines[nl++] = ln;
    }
  }
  if (can) {
    String j;
    for (int i = 0; i < nl; i++) j += lines[i];
    if (!j.startsWith("43") || j.length() < 4) return 0;
    int cnt = hex2(j, 2);
    for (int k = 0; k < cnt && 4 + 4 * k + 4 <= (int)j.length() && dtcN < MAX_DTC; k++) {
      int b0 = hex2(j, 4 + 4 * k), b1 = hex2(j, 6 + 4 * k);
      if (b0 || b1) dtcList[dtcN++] = dtcFromBytes(b0, b1);
    }
  } else {
    for (int i = 0; i < nl; i++) {
      if (!lines[i].startsWith("43")) continue;
      for (int p = 0; p < 3; p++) {
        int idx = 2 + 4 * p;
        if (idx + 4 > (int)lines[i].length()) break;
        int b0 = hex2(lines[i], idx), b1 = hex2(lines[i], idx + 2);
        if ((b0 || b1) && dtcN < MAX_DTC) dtcList[dtcN++] = dtcFromBytes(b0, b1);
      }
    }
  }
  return dtcN;
}

// ลบรหัส (Mode 04) ใช้ได้ตอนสวิตช์ ON เครื่องดับ: return true ถ้ารถตอบรับ (44)
bool dtcClear() {
#if LINK_ESPNOW
  return nowDtc(DN_CMD_DTC_CLEAR) && nowDtcRes.ok;
#endif
  String r = elmRaw("04", 6000);
  return r.indexOf("44") >= 0 && r.indexOf("NODATA") < 0 && r.indexOf("7F") < 0;
}

int dtcPage = 0;

void dtcStatus(const String &s, uint16_t col) {
  lcd.fillRect(0, 22, 250, 20, C_BG);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(col, C_BG);
  lcd.setCursor(8, 24);
  lcd.print(s);
}

void dtcDrawList() {
  lcd.fillRect(0, 44, 320, 152, C_BG);
  for (int i = 0; i < 4; i++) {
    int idx = dtcPage * 4 + i;
    if (idx >= dtcN) break;
    int y = 46 + i * 38;
    lcd.drawRoundRect(8, y, 304, 34, 6, C_PINK);
    lcd.setFont(&fonts::FreeSansBold9pt7b);
    lcd.setTextColor(C_WARN, C_BG);
    lcd.setCursor(16, y + 8);
    lcd.print(dtcList[idx]);
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(C_TXT, C_BG);
    lcd.setCursor(92, y + 9);
    lcd.print(dtcText(dtcList[idx]));
  }
  if (dtcN > 4) drawSmallBtn(250, 22, 62, 20, "MORE", C_PINK);
  else lcd.fillRect(250, 22, 62, 20, C_BG);
}

void dtcDoRead() {
  if (!linkConnected()) { dtcN = 0; dtcDrawList(); dtcStatus("OBD not connected", C_RED); return; }
  dtcStatus("Reading...", C_DIM);
  int n = dtcRead();
  dtcPage = 0;
  dtcDrawList();
  if (n < 0) { dtcStatus("Read failed (engine off? key ON)", C_RED); return; }
  String m = String("MIL ") + (dtcMil ? "ON" : "OFF") + " | " + (n ? String(n) + " code(s)" : String("no codes"));
  dtcStatus(m, dtcMil || n ? C_WARN : C_OK);
}

bool dtcConfirm() {                                   // กล่องยืนยันก่อนลบ
  lcd.fillRoundRect(20, 54, 280, 132, 10, lcd.color565(30, 34, 62));
  lcd.drawRoundRect(20, 54, 280, 132, 10, C_WARN);
  lcd.setFont(&fonts::FreeSansBold12pt7b);
  lcd.setTextColor(C_WARN, lcd.color565(30, 34, 62));
  lcd.setCursor(160 - lcd.textWidth("Clear all codes?") / 2, 62);
  lcd.print("Clear all codes?");
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(C_TXT, lcd.color565(30, 34, 62));
  lcd.setCursor(30, 96);  lcd.print("Key ON, engine OFF.");
  lcd.setCursor(30, 114); lcd.print("Also resets readiness monitors.");
  drawSmallBtn(36, 142, 100, 32, "YES, CLEAR", C_WARN);
  drawSmallBtn(184, 142, 100, 32, "CANCEL", C_PINK);
  waitRelease();
  while (true) {
    int32_t x, y;
    if (getTouchXY(&x, &y)) {
      if (inRect(x, y, 36, 142, 100, 32)) { waitRelease(); return true; }
      if (inRect(x, y, 184, 142, 100, 32)) { waitRelease(); return false; }
    }
    delay(10);
  }
}

void dtcScreen() {
  lcd.fillScreen(C_BG);
  lcd.setFont(&fonts::FreeSansBold12pt7b);
  lcd.setTextColor(C_PINK, C_BG);
  lcd.setCursor(130, 2); lcd.print("DTC");
  drawSmallBtn(8, 200, 96, 36, "READ", C_OK);
  drawSmallBtn(112, 200, 96, 36, "CLEAR", C_WARN);
  drawSmallBtn(216, 200, 96, 36, "BACK", C_PINK);
  dtcStatus("Please wait...", C_DIM);
  holdOBD();                                          // หยุดอ่านค่าสด เพื่อคุยกับ ELM327 เรื่องรหัส
  dtcN = 0;
  dtcDoRead();

  waitRelease();
  while (true) {
    int32_t x, y;
    if (getTouchXY(&x, &y)) {
      if (inRect(x, y, 216, 200, 96, 36)) break;
      else if (inRect(x, y, 8, 200, 96, 36)) dtcDoRead();
      else if (inRect(x, y, 112, 200, 96, 36)) {
        if (dtcConfirm()) {
          lcd.fillRect(0, 22, 250, 20, C_BG);
          dtcDrawList();
          dtcStatus("Clearing...", C_DIM);
          bool ok = linkConnected() && dtcClear();
          delay(500);
          dtcDoRead();
          if (!ok) dtcStatus("Clear failed (engine must be OFF)", C_RED);
        } else {
          dtcDrawList();
          dtcStatus(dtcN ? String(dtcN) + " code(s)" : String("no codes"), C_DIM);
        }
      }
      else if (dtcN > 4 && inRect(x, y, 250, 22, 62, 20)) {
        dtcPage = (dtcPage + 1) % ((dtcN + 3) / 4);
        dtcDrawList();
      }
      waitRelease();
    }
    delay(10);
  }
  releaseOBD();
  waitRelease();
}

// ---- หน้าตั้งระยะเกียร์ (GEAR) : ค่า = รอบเครื่อง ต่อ 1 km/h ของแต่ละเกียร์ ----
// วิธีเรียนรู้: เลือกช่องเกียร์ > ขับคงที่ในเกียร์นั้น (ไม่เหยียบคลัตช์) > กด SET NOW
int gearSel = 0;
void gearDrawCells() {
  for (int g = 0; g < 6; g++) {
    int cx = 8 + (g % 3) * 104, cy = 26 + (g / 3) * 40;
    bool on = g < gearCount, sel = (g == gearSel);
    uint16_t col = !on ? lcd.color565(40, 44, 66) : (sel ? C_OK : C_PINK);
    lcd.fillRoundRect(cx, cy, 96, 34, 6, col);
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(on ? C_BG : C_DIM, col);
    char b[16];
    if (on) snprintf(b, sizeof(b), "%d: %d", g + 1, (int)(GEAR_RPM_PER_KMH[g] + 0.5f)); else snprintf(b, sizeof(b), "%d: --", g + 1);
    lcd.setCursor(cx + (96 - lcd.textWidth(b)) / 2, cy + (34 - lcd.fontHeight()) / 2);
    lcd.print(b);
  }
}
void gearDrawValue() {
  lcd.setFont(&fonts::FreeSansBold18pt7b);
  lcd.setTextColor(C_TXT, C_BG);
  lcd.fillRect(76, 108, 168, 40, C_BG);
  char b[12]; snprintf(b, sizeof(b), "%d", (int)(GEAR_RPM_PER_KMH[gearSel] + 0.5f));
  lcd.setTextDatum(lgfx::textdatum_t::top_center);
  lcd.drawString(b, 160, 112);
  lcd.setTextDatum(lgfx::textdatum_t::top_left);
}
void gearDrawGearsBtn() {
  char b[12]; snprintf(b, sizeof(b), "GEARS %d", gearCount);
  drawSmallBtn(112, 166, 96, 28, b, C_PINK);
}
void gearScreen() {
  lcd.fillScreen(C_BG);
  lcd.setFont(&fonts::FreeSansBold12pt7b);
  lcd.setTextColor(C_PINK, C_BG);
  lcd.setCursor(100, 2); lcd.print("GEAR CAL");
  gearDrawCells();
  drawBtn(8, 108, 60, 40, "-", C_PINK);  drawBtn(252, 108, 60, 40, "+", C_PINK);
  gearDrawValue();
  drawSmallBtn(8, 166, 96, 28, "SET NOW", C_OK);
  gearDrawGearsBtn();
  drawSmallBtn(216, 166, 96, 28, "RESET", C_WARN);
  drawBtn(90, 200, 140, 36, "BACK", C_OK);
  waitRelease();
  uint32_t lastTap = 0, lastLive = 0;
  while (true) {
    int32_t x, y;
    if (getTouchXY(&x, &y) && millis() - lastTap > 160) {
      lastTap = millis();
      bool hit = false;
      for (int g = 0; g < gearCount; g++)
        if (inRect(x, y, 8 + (g % 3) * 104, 26 + (g / 3) * 40, 96, 34)) { gearSel = g; gearDrawCells(); gearDrawValue(); hit = true; waitRelease(); }
      if (hit) {}
      else if (inRect(x, y, 8, 108, 60, 40))   { GEAR_RPM_PER_KMH[gearSel] = max(5.0f, GEAR_RPM_PER_KMH[gearSel] - 1.0f);   gearDrawCells(); gearDrawValue(); }
      else if (inRect(x, y, 252, 108, 60, 40)) { GEAR_RPM_PER_KMH[gearSel] = min(400.0f, GEAR_RPM_PER_KMH[gearSel] + 1.0f); gearDrawCells(); gearDrawValue(); }
      else if (inRect(x, y, 8, 166, 96, 28)) {
        if (speedKmh >= 10 && rpm >= 800) {
          GEAR_RPM_PER_KMH[gearSel] = (float)rpm / (float)speedKmh;
          gearDrawCells(); gearDrawValue(); waitRelease();
        }
      }
      else if (inRect(x, y, 112, 166, 96, 28)) {
        gearCount = (gearCount >= 6) ? 3 : gearCount + 1;
        if (gearSel >= gearCount) gearSel = gearCount - 1;
        gearDrawCells(); gearDrawValue(); gearDrawGearsBtn(); waitRelease();
      }
      else if (inRect(x, y, 216, 166, 96, 28)) {
        for (int g = 0; g < 6; g++) GEAR_RPM_PER_KMH[g] = GEAR_DEF[g];
        gearCount = 6; gearSel = 0;
        gearDrawCells(); gearDrawValue(); gearDrawGearsBtn(); waitRelease();
      }
      else if (inRect(x, y, 90, 200, 140, 36)) break;
    }
    if (millis() - lastLive > 300) {             // ค่าสด: รอบ/ความเร็วตอนนี้ และสัดส่วนปัจจุบัน
      lastLive = millis();
      char b[48];
      if (speedKmh >= 10 && rpm >= 800) snprintf(b, sizeof(b), "now %d  (%d rpm / %d km/h)", (int)((float)rpm / speedKmh + 0.5f), (int)rpm, (int)speedKmh);
      else snprintf(b, sizeof(b), "drive steady in this gear, then SET NOW");
      lcd.setFont(&fonts::Font2);
      lcd.setTextColor(C_DIM, C_BG);
      lcd.fillRect(0, 150, 320, 14, C_BG);
      lcd.setCursor(10, 150); lcd.print(b);
    }
    delay(10);
  }
  prefs.putInt("gcnt", gearCount);
  for (int g = 0; g < 6; g++) {
    char k[5] = {'g', 'r', (char)('0' + g), 0, 0};
    prefs.putFloat(k, GEAR_RPM_PER_KMH[g]);
  }
}

// ---- PEAKS (ค่าสูงสุดของทริปนี้ / ตลอดกาล) ----
void drawPeakVals() {
  lcd.setFont(&fonts::Font2);
  lcd.setTextDatum(lgfx::textdatum_t::top_right);
  for (int i = 0; i < N_PK; i++) {
    int y = 46 + i * 21;
    const float *arr[2] = {pkTrip, pkAll};
    for (int c = 0; c < 2; c++) {
      char b[12];
      if (!pkHave(arr[c], i)) strcpy(b, "--");
      else if (i == PK_BOOST) snprintf(b, sizeof(b), "%.2f", arr[c][i]);
      else if (i == PK_BATT)  snprintf(b, sizeof(b), "%.1f", arr[c][i]);
      else snprintf(b, sizeof(b), "%d", (int)lroundf(arr[c][i]));
      lcd.setTextColor(c == 0 ? C_TXT : C_WARN, C_BG);
      lcd.setTextPadding(70);
      lcd.drawString(b, c == 0 ? 206 : 312, y);
    }
  }
  lcd.setTextPadding(0);
  lcd.setTextDatum(lgfx::textdatum_t::top_left);
}

void peaksScreen() {
  lcd.fillScreen(C_BG);
  lcd.setFont(&fonts::FreeSansBold12pt7b);
  lcd.setTextColor(C_PINK, C_BG);
  lcd.setCursor(118, 2); lcd.print("PEAKS");
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(C_DIM, C_BG);
  lcd.setCursor(150, 28); lcd.print("THIS TRIP");
  lcd.setTextDatum(lgfx::textdatum_t::top_right); lcd.drawString("ALL TIME", 312, 28); lcd.setTextDatum(lgfx::textdatum_t::top_left);
  lcd.setTextColor(C_LABEL, C_BG);
  for (int i = 0; i < N_PK; i++) { lcd.setCursor(10, 46 + i * 21); lcd.print(PK_NAME[i]); }
  drawPeakVals();
  drawSmallBtn(8, 200, 96, 36, "RESET TRIP", C_PINK);
  drawSmallBtn(112, 200, 96, 36, "RESET ALL", C_PINK);
  drawBtn(216, 200, 96, 36, "BACK", C_OK);
  waitRelease();
  uint32_t lastTap = 0, lastDraw = 0, armAt = 0;
  bool armed = false;
  while (true) {
    int32_t x, y;
    uint32_t now = millis();
    if (getTouchXY(&x, &y) && now - lastTap > 160) {
      lastTap = now;
      if (inRect(x, y, 8, 200, 96, 36)) { pkClear(pkTrip); drawPeakVals(); waitRelease(); }
      else if (inRect(x, y, 112, 200, 96, 36)) {
        if (!armed) { armed = true; armAt = now; drawSmallBtn(112, 200, 96, 36, "SURE? TAP", C_WARN); }
        else { pkClear(pkAll); pkClear(pkTrip); pkSave(); armed = false; drawSmallBtn(112, 200, 96, 36, "RESET ALL", C_PINK); drawPeakVals(); }
        waitRelease();
      }
      else if (inRect(x, y, 216, 200, 96, 36)) break;
    }
    if (armed && now - armAt > 3000) { armed = false; drawSmallBtn(112, 200, 96, 36, "RESET ALL", C_PINK); }
    peaksUpdate(now);
    if (now - lastDraw > 500) { lastDraw = now; drawPeakVals(); }
    delay(10);
  }
  if (pkDirty) pkSave();
}

void setBriF(float f);
// ---------- เมนูแบบรายการ: ปัดขึ้นจากหน้าไหนก็ได้เพื่อเปิด, ปัดลงเพื่อปิด ----------
// แต่ละข้อ: หมายเลข + หัวข้อ + คำอธิบายสั้น ๆ ว่าตั้งอะไร + ค่าปัจจุบันด้านขวา  แตะเพื่อเข้าไปตั้งค่า
const int MN_HDR = 30, MN_ROW = 30, MN_N = 7;
const char *const MN_TITLE[MN_N] = {"MAX RPM", "COOLANT ALERT", "DISPLAY", "BLUETOOTH", "ENGINE CODES", "GEAR", "PEAKS"};
const char *const MN_DESC[MN_N]  = {"Redline + shift light point", "Warn when water temp reaches", "Brightness, auto light, flip, demo",
                                    "Connect / pair the OBD2 adapter", "Read & clear fault codes (DTC)", "Gear ratios for the gear display",
                                    "Max / min records: trip & all time"};
uint16_t MN_BG, MN_LINE, MN_BADGE, MN_HI;
void mnColors() {
  MN_BG = C_BG; MN_LINE = lcd.color565(30, 34, 62); MN_BADGE = lcd.color565(40, 46, 90); MN_HI = lcd.color565(34, 40, 82);
}
void mnValue(int i, char *b, uint16_t *col) {                  // ค่าปัจจุบันที่แสดงด้านขวาของแต่ละข้อ
  *col = C_PINK; b[0] = 0;
  switch (i) {
    case 0: snprintf(b, 16, "%d", maxRpm); break;
    case 1: snprintf(b, 16, "%d C", tempAlert); break;
    case 2: if (briAuto) strcpy(b, "AUTO"); else snprintf(b, 16, "%d%%", briPct); if (demoMode) strcpy(b, "DEMO"); break;
    case 3: strcpy(b, curStatus()); *col = obdOk ? C_OK : C_WARN; break;
    case 5: snprintf(b, 16, "%d SPD", gearCount); break;
  }
}
void mnRow(int i, bool hi) {
  int y = MN_HDR + i * MN_ROW;
  uint16_t bg = hi ? MN_HI : MN_BG;
  lcd.fillRect(0, y, 320, MN_ROW, bg);
  if (i < MN_N - 1) lcd.drawFastHLine(10, y + MN_ROW - 1, 300, MN_LINE);
  lcd.fillCircle(17, y + 15, 10, MN_BADGE);                    // หมายเลขข้อ
  char nb[3]; snprintf(nb, sizeof(nb), "%d", i + 1);
  lcd.setFont(&fonts::Font2); lcd.setTextColor(TFT_WHITE); lcd.setTextDatum(lgfx::textdatum_t::middle_center);
  lcd.drawString(nb, 17, y + 15);
  lcd.setTextDatum(lgfx::textdatum_t::top_left);
  lcd.setFont(&fonts::FreeSansBold9pt7b); lcd.setTextColor(TFT_WHITE);
  lcd.drawString(MN_TITLE[i], 34, y + 2);
  lcd.setFont(&fonts::Font0); lcd.setTextColor(C_DIM);
  lcd.drawString(MN_DESC[i], 35, y + 20);
  char v[16]; uint16_t vc; mnValue(i, v, &vc);
  lcd.setTextDatum(lgfx::textdatum_t::middle_right);
  if (v[0]) { lcd.setFont(&fonts::Font2); lcd.setTextColor(vc); lcd.drawString(v, 300, y + 9); }
  lcd.setFont(&fonts::Font2); lcd.setTextColor(C_DIM); lcd.drawString(">", 314, y + 15);
  lcd.setTextDatum(lgfx::textdatum_t::top_left);
}
void mnHeader() {
  lcd.fillRect(0, 0, 320, MN_HDR, MN_BG);
  lcd.fillRoundRect(140, 3, 40, 4, 2, C_DIM);                   // ที่จับ (แบบแผ่นเลื่อนขึ้น)
  lcd.setFont(&fonts::FreeSansBold12pt7b); lcd.setTextColor(C_PINK);
  lcd.drawString("SETTINGS", 10, 7);
  lcd.setFont(&fonts::Font0); lcd.setTextColor(C_DIM); lcd.setTextDatum(lgfx::textdatum_t::top_right);
  lcd.drawString("swipe down to close", 314, 12);
  lcd.setTextDatum(lgfx::textdatum_t::top_left);
  lcd.drawFastHLine(0, MN_HDR - 1, 320, MN_LINE);
}
void drawMenuPage() { mnHeader(); for (int i = 0; i < MN_N; i++) mnRow(i, false); }
void mnOpenAnim() {                                            // เมนูเลื่อนขึ้นจากขอบล่าง
  for (int y = 240; y > 0; y -= 20) { lcd.fillRect(0, y - 20, 320, 20, MN_BG); delay(5); }
  mnHeader();
  for (int i = 0; i < MN_N; i++) { mnRow(i, false); delay(8); }
}

// ---- หน้าปรับค่าตัวเลข (MAX RPM / COOLANT ALERT): - ค่า + และ DONE  กดค้างเพื่อเปลี่ยนเร็ว ----
void valueScreen(const char *title, const char *desc, int *val, int lo, int hi, int step, const char *unit) {
  auto drawVal = [&]() {
    lcd.fillRect(76, 70, 168, 60, C_BG);
    lcd.setFont(&fonts::FreeSansBold24pt7b); lcd.setTextColor(TFT_WHITE); lcd.setTextDatum(lgfx::textdatum_t::middle_center);
    char b[12]; snprintf(b, sizeof(b), "%d", *val); lcd.drawString(b, 160, 96);
    lcd.setFont(&fonts::Font2); lcd.setTextColor(C_DIM); lcd.drawString(unit, 160, 124);
    lcd.setTextDatum(lgfx::textdatum_t::top_left);
  };
  lcd.fillScreen(C_BG);
  lcd.setFont(&fonts::FreeSansBold12pt7b); lcd.setTextColor(C_PINK); lcd.drawString(title, 10, 8);
  lcd.setFont(&fonts::Font2); lcd.setTextColor(C_DIM); lcd.drawString(desc, 10, 36);
  drawBtn(8, 70, 60, 54, "-", C_PINK); drawBtn(252, 70, 60, 54, "+", C_PINK);
  lcd.setFont(&fonts::Font0); lcd.setTextColor(C_DIM); lcd.setTextDatum(lgfx::textdatum_t::top_center);
  char r[64]; snprintf(r, sizeof(r), "range %d - %d, step %d (hold to repeat)", lo, hi, step); lcd.drawString(r, 160, 146);
  lcd.setTextDatum(lgfx::textdatum_t::top_left);
  drawBtn(90, 180, 140, 44, "DONE", C_OK);
  drawVal();
  waitRelease();
  uint32_t pressT = 0, lastRep = 0;
  while (true) {
    int32_t x, y;
    if (getTouchXY(&x, &y)) {
      uint32_t now = millis();
      int d = inRect(x, y, 8, 70, 60, 54) ? -step : (inRect(x, y, 252, 70, 60, 54) ? step : 0);
      if (d) {
        if (pressT == 0) { pressT = now; lastRep = now; *val = constrain(*val + d, lo, hi); drawVal(); }
        else if (now - pressT > 450 && now - lastRep > 90) { lastRep = now; *val = constrain(*val + d, lo, hi); drawVal(); }
      } else if (inRect(x, y, 90, 180, 140, 44)) { waitRelease(); break; }
    } else pressT = 0;
    delay(10);
  }
}

void menuScreen() {
  hFree();                                               // คืนแรมพื้นหลังหน้า 1
  dnFree();                                              // คืนแรมสไปรท์แถบบนหน้า 2
  fFree();                                               // คืนแรมพื้นหลังหน้า 10
  pSprFree();                                            // คืนแรมสไปรท์ตัวเลขก่อนเข้าเมนู (ให้ Bluetooth/DTC ใช้)
  mnOpenAnim();
  waitRelease();
  bool down = false; int sx = 0, sy = 0, lx = 0, ly = 0; uint32_t tl = 0, lastRefresh = millis();
  int hiRow = -1;
  while (true) {
    int32_t x, y;
    uint32_t now = millis();
    if (getTouchXY(&x, &y)) {
      if (!down) {
        down = true; sx = x; sy = y;
        hiRow = (y >= MN_HDR) ? (y - MN_HDR) / MN_ROW : -1;   // ไฮไลต์ข้อที่แตะ
        if (hiRow >= 0 && hiRow < MN_N) mnRow(hiRow, true); else hiRow = -1;
      }
      lx = x; ly = y; tl = now;
      if (ly - sy > 55 && abs(lx - sx) < 70) break;          // ปัดลง = ปิดเมนู
    } else if (down && now - tl > 60) {
      down = false;
      int dx = lx - sx, dy = ly - sy;
      if (hiRow >= 0) mnRow(hiRow, false);
      if (abs(dx) < 25 && abs(dy) < 25 && hiRow >= 0) {
        bool leave = false;
        switch (hiRow) {
          case 0: valueScreen("MAX RPM", "Redline: shift light + red zone start here", &maxRpm, 2000, MAX_RPM_LIMIT, 250, "rpm");
                  prefs.putInt("maxRpm", maxRpm); break;
          case 1: valueScreen("COOLANT ALERT", "Water temp number blinks red at this value", &tempAlert, 70, 130, 1, "deg C");
                  prefs.putInt("tempAlert", tempAlert); break;
          case 2: leave = displayScreen(); break;           // เริ่มเดโม = ออกจากเมนูเลย
          case 3: btScreen(); break;
          case 4: dtcScreen(); break;
          case 5: gearScreen(); break;
          case 6: peaksScreen(); break;
        }
        if (leave) break;
        drawMenuPage();
        waitRelease();
      }
      hiRow = -1;
    }
    if (!down && now - lastRefresh > 700) { lastRefresh = now; mnRow(3, false); }   // สถานะ Bluetooth อัปเดตสด
    delay(10);
  }
  waitRelease();
  for (int i = 9; i >= 0; i--) { setBriF(i / 10.0f); delay(10); }   // ปิดเมนู: หรี่ -> วาดหน้าเดิม -> สว่าง
  drawStatic(false);
  invalidateUI();
  for (int i = 1; i <= 10; i++) { setBriF(i / 10.0f); delay(12); }
}

// ---------- setup / loop ----------
void calibrateTouchIfNeeded(bool force) {
  uint16_t cal[8];
  bool rotChanged = prefs.getInt("calRot", -1) != LCD_ROTATION;      // เปลี่ยนทิศจอ = ต้องคาลิเบรตทัชใหม่
  if (!force && !rotChanged && prefs.isKey("cal")) {
    prefs.getBytes("cal", cal, sizeof(cal));
    lcd.setTouchCalibrate(cal);
    return;
  }
  lcd.setRotation(LCD_ROTATION);
  lcd.setBrightness(180);
  lcd.fillScreen(TFT_BLACK);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(TFT_WHITE);
  lcd.setCursor(20, 110);
  lcd.print("Touch the arrows at each corner");
  lcd.calibrateTouch(cal, TFT_WHITE, TFT_BLACK, 20);
  lcd.setTouchCalibrate(cal);
  prefs.putBytes("cal", cal, sizeof(cal));
  prefs.putInt("calRot", LCD_ROTATION);
}

// ---------- หน้าจอเปิดเครื่อง: หน้าปัดวัดรอบ เข็มกวาด + ข้อความฟอนต์ 8 บิต (ฝังในโค้ดทั้งหมด) ----------
uint16_t splashGrad(float t) {
  float r, g;
  if (t < 0.6f) { r = 90 + t / 0.6f * 165; g = 220; }
  else          { r = 255; g = 220 - (t - 0.6f) / 0.4f * 160; }
  return lcd.color565((int)r, (int)g, (int)(90 * (1.0f - t)));
}

void setBriF(float f) {
  applyBrightness((int)((briAuto ? 70 : briPct) * constrain(f, 0.0f, 1.0f)));
}

// ฟอนต์พิกเซล 8x8 (บิตซ้ายสุด = MSB) เฉพาะตัวที่ใช้: 0-9 ช่องว่าง . และตัวอักษรของข้อความ
struct Glyph8 { char c; uint8_t r[8]; };
const Glyph8 FONT8[] = {
  {' ', {0,0,0,0,0,0,0,0}},
  {'.', {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}},
  {'0', {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00}},
  {'1', {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00}},
  {'2', {0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0x00}},
  {'3', {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00}},
  {'4', {0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0x00}},
  {'5', {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00}},
  {'6', {0x3C,0x60,0x60,0x7C,0x66,0x66,0x3C,0x00}},
  {'7', {0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0x00}},
  {'8', {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00}},
  {'9', {0x3C,0x66,0x66,0x3E,0x06,0x06,0x3C,0x00}},
  {'B', {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00}},
  {'D', {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00}},
  {'G', {0x3C,0x66,0x60,0x6E,0x66,0x66,0x3E,0x00}},
  {'I', {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}},
  {'O', {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}},
  {'T', {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00}},
  {'Y', {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00}},
  {'a', {0x00,0x00,0x3C,0x06,0x3E,0x66,0x3E,0x00}},
  {'d', {0x06,0x06,0x3E,0x66,0x66,0x66,0x3E,0x00}},
  {'e', {0x00,0x00,0x3C,0x66,0x7E,0x60,0x3C,0x00}},
  {'g', {0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x3C}},
  {'i', {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00}},
  {'n', {0x00,0x00,0x7C,0x66,0x66,0x66,0x66,0x00}},
  {'p', {0x00,0x00,0x7C,0x66,0x66,0x7C,0x60,0x60}},
  {'s', {0x00,0x00,0x3E,0x60,0x3C,0x06,0x7C,0x00}},
  {'u', {0x00,0x00,0x66,0x66,0x66,0x66,0x3E,0x00}},
  {'y', {0x00,0x00,0x66,0x66,0x66,0x3E,0x06,0x3C}},
};
const int N_FONT8 = sizeof(FONT8) / sizeof(FONT8[0]);

void glyph8(lgfx::LGFXBase *g, int x, int y, char ch, int sc, uint16_t col) {   // วาดตัวอักษร 8x8 ขยาย sc เท่า
  for (int i = 0; i < N_FONT8; i++) {
    if (FONT8[i].c != ch) continue;
    for (int r = 0; r < 8; r++) {
      uint8_t b = FONT8[i].r[r];
      int c = 0;
      while (c < 8) {
        if (!(b & (0x80 >> c))) { c++; continue; }
        int c0 = c;
        while (c < 8 && (b & (0x80 >> c))) c++;
        g->fillRect(x + c0 * sc, y + r * sc, (c - c0) * sc, sc, col);   // วาดเป็นแถบยาวต่อแถว
      }
    }
    return;
  }
}

// ข้อความ 3 แถว (ตัวอักษรทุกตัวกว้าง 8*scale)
struct SplashRow { const char *s; int sc, y; };
const SplashRow SP_ROW[3] = {{"OYT", 3, 150}, {"OBDII Gauge", 2, 182}, {"By Didissnap.", 2, 206}};
uint16_t spRowCol[3];
int spRowX(int r) { return 160 - (int)strlen(SP_ROW[r].s) * 8 * SP_ROW[r].sc / 2; }

const int SP_CX = 160, SP_CY = 96, SP_R1 = 94, SP_R0 = 88, SP_NEEDLE = 74;
int spDiv = 7;                                   // จำนวนขีดใหญ่ (x1000 rpm) = redline ปัดขึ้น

void spDrawRows(lgfx::LGFXBase *g, int ox, int oy, const int n[3]) {      // ข้อความที่เผยแล้ว
  for (int r = 0; r < 3; r++)
    for (int i = 0; i < n[r]; i++)
      glyph8(g, spRowX(r) + i * 8 * SP_ROW[r].sc - ox, SP_ROW[r].y - oy, SP_ROW[r].s[i], SP_ROW[r].sc, spRowCol[r]);
}

void spDrawDial(lgfx::LGFXBase *g, int ox, int oy) {                       // วงแหวนสี + ขีด + ตัวเลข (ไม่รวมเข็ม)
  int cx = SP_CX - ox, cy = SP_CY - oy;
  for (int i = 0; i < 27; i++) {
    float a0 = 135 + i * 10.0f, a1 = a0 + 10.5f;
    g->fillArc(cx, cy, SP_R0, SP_R1, a0, a1, splashGrad((i + 0.5f) / 27.0f));
  }
  g->setTextColor(0);
  int steps = spDiv * 2;                                                   // ขีดทุก 500 rpm
  for (int k = 0; k <= steps; k++) {
    float a = (135.0f + 270.0f * k / steps) * 0.0174533f, ca = cosf(a), sa = sinf(a);
    bool major = (k % 2) == 0;
    int rin = SP_R0 - 3 - (major ? 11 : 6), rout = SP_R0 - 3;
    uint16_t col = major ? lcd.color565(240, 240, 255) : lcd.color565(120, 128, 170);
    g->drawLine(cx + ca * rin, cy + sa * rin, cx + ca * rout, cy + sa * rout, col);
    if (major) g->drawLine(cx + ca * rin + 1, cy + sa * rin, cx + ca * rout + 1, cy + sa * rout, col);
    if (major) {
      char b[4]; snprintf(b, sizeof(b), "%d", k / 2);
      int sc = (spDiv >= 10) ? 1 : 2, w = (int)strlen(b) * 8 * sc;
      int nx = cx + ca * (SP_R0 - 28) - w / 2, ny = cy + sa * (SP_R0 - 28) - 4 * sc;
      for (int q = 0; b[q]; q++) glyph8(g, nx + q * 8 * sc, ny, b[q], sc, lcd.color565(230, 232, 250));
    }
  }
}

void spDrawNeedle(lgfx::LGFXBase *g, int ox, int oy, float deg) {
  float a = deg * 0.0174533f, ca = cosf(a), sa = sinf(a), px = -sa, py = ca;
  int cx = SP_CX - ox, cy = SP_CY - oy;
  uint16_t red = lcd.color565(255, 60, 60), dark = lcd.color565(10, 12, 30);
  int tx = cx + ca * SP_NEEDLE, ty = cy + sa * SP_NEEDLE;
  int bx0 = cx + px * 4 - ca * 14, by0 = cy + py * 4 - sa * 14, bx1 = cx - px * 4 - ca * 14, by1 = cy - py * 4 - sa * 14;
  g->fillTriangle(tx, ty, bx0, by0, bx1, by1, dark);                       // ขอบดำรอบเข็ม
  g->fillTriangle(tx - ca, ty - sa, cx + px * 3 - ca * 12, cy + py * 3 - sa * 12, cx - px * 3 - ca * 12, cy - py * 3 - sa * 12, red);
  g->fillCircle(cx, cy, 8, dark);
  g->fillCircle(cx, cy, 6, lcd.color565(200, 205, 230));
  g->fillCircle(cx, cy, 3, red);
}

float spAngle = -999;
void spUpdateNeedle(float deg, const int n[3]) {                           // เข็มใหม่: วาดผ่านสไปรท์เล็ก ๆ ไม่กะพริบ
  float a0 = (spAngle < -900) ? deg : spAngle;
  int minx = SP_CX - 16, maxx = SP_CX + 16, miny = SP_CY - 16, maxy = SP_CY + 16;
  float as[2] = {a0, deg};
  for (float a : as) {
    float r = a * 0.0174533f;
    for (int d : {-14, SP_NEEDLE}) {
      int x = SP_CX + (int)(cosf(r) * d), y = SP_CY + (int)(sinf(r) * d);
      minx = min(minx, x - 7); maxx = max(maxx, x + 7); miny = min(miny, y - 7); maxy = max(maxy, y + 7);
    }
  }
  minx = max(minx, 0); miny = max(miny, 0); maxx = min(maxx, 320); maxy = min(maxy, 240);
  int w = maxx - minx, h = maxy - miny;
  lgfx::LGFX_Sprite sp(&lcd);
  sp.setColorDepth(16);
  if (w * h <= 9000 && sp.createSprite(w, h)) {
    sp.fillScreen(C_BG);
    spDrawDial(&sp, minx, miny);
    spDrawRows(&sp, minx, miny, n);
    spDrawNeedle(&sp, minx, miny, deg);
    sp.pushSprite(minx, miny);
    sp.deleteSprite();
  } else {                                                                  // แรมไม่พอ: วาดตรง ๆ ในกรอบเดิม
    lcd.setClipRect(minx, miny, w, h);
    lcd.fillRect(minx, miny, w, h, C_BG);
    spDrawDial(&lcd, 0, 0); spDrawRows(&lcd, 0, 0, n); spDrawNeedle(&lcd, 0, 0, deg);
    lcd.clearClipRect();
  }
  spAngle = deg;
}

void splash() {
  lcd.setBrightness(0);
  lcd.fillScreen(C_BG);
  spDiv = constrain((maxRpm + 999) / 1000, 2, 10);
  spRowCol[0] = lcd.color565(255, 220, 0);                                  // OYT = เหลือง
  spRowCol[1] = lcd.color565(245, 245, 255);
  spRowCol[2] = lcd.color565(255, 170, 190);
  int n[3] = {0, 0, 0};
  spAngle = -999;
  spDrawDial(&lcd, 0, 0);
  spUpdateNeedle(135, n);
  uint32_t t0 = millis(), tl = SPLASH_MS;
  while (true) {
    uint32_t t = millis() - t0;
    if (t >= tl) break;
    float f = 1.0f;
    if (t < 500) f = t / 500.0f;                    // ค่อย ๆ สว่างขึ้น
    if (t > tl - 500) f = (tl - t) / 500.0f;        // ค่อย ๆ มืดลงก่อนเข้าหน้าหลัก
    setBriF(f);

    // เข็มกวาดแบบ self-test: ขึ้นสุด -> ค้าง -> ลงเหลือรอบเดินเบา
    float s;
    if (t < 500) s = 0;
    else if (t < 2300) { float u = (t - 500) / 1800.0f; s = 1 - (1 - u) * (1 - u); }
    else if (t < 2800) s = 1;
    else if (t < 3800) { float u = (t - 2800) / 1000.0f; u = u * u * (3 - 2 * u); s = 1 - 0.88f * u; }
    else s = 0.12f;
    float deg = 135 + 270 * s;

    // ข้อความพิมพ์ทีละตัวแบบ 8 บิต: แถว 1 เริ่ม 500 ms, แถว 2 เริ่ม 1000 ms, แถว 3 เริ่ม 1800 ms
    const int rowT0[3] = {500, 1000, 1800}, rowDt[3] = {160, 60, 60};
    for (int r = 0; r < 3; r++) {
      int want = (t < (uint32_t)rowT0[r]) ? 0 : min((int)strlen(SP_ROW[r].s), 1 + (int)((t - rowT0[r]) / rowDt[r]));
      if (want > n[r]) {
        for (int i = n[r]; i < want; i++) glyph8(&lcd, spRowX(r) + i * 8 * SP_ROW[r].sc, SP_ROW[r].y, SP_ROW[r].s[i], SP_ROW[r].sc, spRowCol[r]);
        n[r] = want;
      }
    }
    if (fabsf(deg - spAngle) > 0.4f) spUpdateNeedle(deg, n);

    int32_t x, y;
    if (t > 800 && t < tl - 500 && getTouchXY(&x, &y)) t0 = millis() - (tl - 500);   // แตะเพื่อข้าม
    delay(4);
  }
  lcd.setBrightness(0);
}

// ---------- หน้าโลโก้หลังหน้าเปิดเครื่อง: วาดภาพตอนจอมืด แล้วเร่งไฟจอขึ้นช้า ๆ ----------
#if HAS_LOGO
void logoBri(float f) {                          // f 0..1 -> ไฟจอ (ยกกำลังสองให้ตาเห็นสว่างขึ้นสม่ำเสมอ ละเอียด 255 ขั้น)
  f = constrain(f, 0.0f, 1.0f);
  int target = (briAuto ? 70 : briPct) * 255 / 100;
  lcd.setBrightness((uint8_t)(target * f * f + 0.5f));
}
void logoScreen() {
  lcd.setBrightness(0);
  lcd.fillScreen(TFT_BLACK);
  lcd.drawJpg(logo_jpg, logo_jpg_len, 0, 0);
  const uint32_t tIn = LOGO_FADE_MS, tHold = tIn + LOGO_HOLD_MS, tEnd = tHold + LOGO_OUT_MS;
  uint32_t t0 = millis();
  while (true) {
    uint32_t t = millis() - t0;
    if (t >= tEnd) break;
    float f;
    if (t < tIn) { float u = t / (float)tIn; f = u * u * (3 - 2 * u); }        // ค่อย ๆ สว่างขึ้น (เริ่มช้า-กลางเร็ว-จบนุ่ม)
    else if (t < tHold) f = 1.0f;
    else f = 1.0f - (t - tHold) / (float)LOGO_OUT_MS;                         // ค่อย ๆ มืดลง
    logoBri(f);
    int32_t x, y;
    if (t > 600 && t < tHold && getTouchXY(&x, &y)) {                         // แตะเพื่อข้าม: มืดลงจากความสว่างตอนนี้
      float fNow = (t < tIn) ? f : 1.0f;
      t0 = millis() - (tHold + (uint32_t)((1.0f - fNow) * LOGO_OUT_MS));
    }
    delay(8);
  }
  lcd.setBrightness(0);
}
#endif

const char *resetReasonText(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:  return "POWERON";
    case ESP_RST_EXT:      return "EXT";
    case ESP_RST_SW:       return "SW";
    case ESP_RST_PANIC:    return "PANIC";
    case ESP_RST_INT_WDT:  return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT:      return "WDT";
    case ESP_RST_DEEPSLEEP:return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    default:               return "UNKNOWN";
  }
}
bool fastBoot = false;               // รีบูตเพราะเครื่องค้าง/ไฟตก/แรมต่ำ -> ข้ามหน้าเปิดเครื่องให้กลับมาใช้งานไว

// เฝ้าดูแรมทุก 5 วินาที (บันทึกลง Serial และจำค่าต่ำสุดไว้ดูในเมนู DISPLAY)
// HEAP_AUTO_RESTART = 1 จะรีสตาร์ตเองเมื่อแรมหายไปมากผิดปกติ (ปิดไว้ก่อน เพราะถ้าตั้งเกณฑ์ผิด จอจะกะพริบรีบูตวน)
#define HEAP_AUTO_RESTART 0
uint32_t lastHeapChk = 0, lastHeapSave = 0;
int lowHeapCount = 0;
uint32_t heapBaseline = 0;           // แรมว่างตอนระบบนิ่งแล้ว (วัดที่ ~30 วินาทีหลังบูต)
void heapWatch(uint32_t now) {
  if (now - lastHeapChk < 5000) return;
  lastHeapChk = now;
  uint32_t fh = ESP.getFreeHeap(), mn = ESP.getMinFreeHeap(), big = ESP.getMaxAllocHeap();
  Serial.printf("heap free %u  min %u  largest %u\n", fh, mn, big);
  if (heapBaseline == 0 && now > 30000) heapBaseline = fh;
  // ต่ำผิดปกติ = เหลือน้อยกว่า 4KB หรือหายไปเกินครึ่งจากช่วงที่นิ่ง (สัญญาณว่าแรมรั่ว)
#if HEAP_AUTO_RESTART
  if (now > 600000 && prefs.getInt("hrCount", 0) != 0) prefs.putInt("hrCount", 0);       // ทำงานปกติเกิน 10 นาที = ล้างตัวนับรีสตาร์ต
  if (heapBaseline && fh < heapBaseline * 45 / 100 && prefs.getInt("hrCount", 0) < 2) {  // รีสตาร์ตสูงสุด 2 ครั้งติด กันรีบูตวน
    if (++lowHeapCount >= 3) {
      Serial.println("heap dropped a lot: restarting");
      prefs.putBool("hr", true);
      prefs.putInt("hrCount", prefs.getInt("hrCount", 0) + 1);
      delay(100);
      ESP.restart();
    }
  } else lowHeapCount = 0;
#endif
  if (now - lastHeapSave > 60000) {                       // บันทึกค่าแรมต่ำสุดที่เคยเจอ (ดูได้ในเมนู DISPLAY)
    lastHeapSave = now;
    if (mn + 2048 < prefs.getUInt("minHeap", 0xFFFFFFFFu)) prefs.putUInt("minHeap", mn);
  }
}

void setup() {
  Serial.begin(115200);
#if LINK_ESPNOW
  esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);  // ไม่ใช้ Bluetooth เลย: คืนแรม Bluetooth ทั้งหมด
#else
  esp_bt_controller_mem_release(ESP_BT_MODE_BLE);   // ใช้แต่ Bluetooth Classic: คืนแรมส่วน BLE (~20KB+)
#endif
  Serial.printf("boot heap: %u (largest block %u)\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  lcd.init();
  lcd.setRotation(LCD_ROTATION);
  Serial.printf("LCD %dx%d rotation=%d panel=%s\n", (int)lcd.width(), (int)lcd.height(), LCD_ROTATION, PANEL_ST7789 ? "ST7789" : "ILI9341");   // ควรได้ 320x240
  lcd.setBrightness(0);

  C_BG    = lcd.color565(8, 10, 28);
  C_PANEL = lcd.color565(6, 8, 24);
  C_SKY   = lcd.color565(28, 40, 94);
  C_TXT   = lcd.color565(245, 245, 255);
  C_DIM   = lcd.color565(130, 140, 180);
  C_LABEL = lcd.color565(200, 208, 240);
  C_OK    = lcd.color565(90, 220, 130);
  C_WARN  = lcd.color565(255, 190, 60);
  C_RED   = lcd.color565(255, 60, 60);
  C_PINK  = lcd.color565(255, 170, 190);
  initPageColors();
  mnColors();
  hInitColors();
  pReset();

  prefs.begin("dash", false);
  maxRpm    = constrain(prefs.getInt("maxRpm", DEF_MAX_RPM), 2000, MAX_RPM_LIMIT);
  pkLoad();
  if (!prefs.getBool("rpm75", false)) { prefs.putBool("rpm75", true); if (maxRpm == 6500) { maxRpm = 7500; prefs.putInt("maxRpm", maxRpm); } }   // ครั้งเดียว: ค่าเดิมของโรงงาน 6500 -> 7500 ให้ตรงกับรถ
  tempAlert = prefs.getInt("tempAlert", DEF_TEMP_ALERT);
  briPct    = constrain(prefs.getInt("bri", 80), 10, 100);
  briAuto   = prefs.getBool("briAuto", false);
  btMac     = prefs.getString("btMac", "");
  btName    = prefs.getString("btName", "");
  String pin = prefs.getString("btPin", DEF_PIN);
  strncpy(btPin, pin.c_str(), sizeof(btPin) - 1);
  btPin[sizeof(btPin) - 1] = 0;
  page = constrain(prefs.getInt("page", 0), 0, N_PAGES - 1);
  if (!pageAvail(page)) page = nextPage(page);
  tripKm = prefs.getFloat("trip", 0.0f);
  slotMetric[0] = constrain(prefs.getInt("slotA", M_COOLANT), 0, N_METRICS - 1);    // หน้า 9: ค่าที่เลือกไว้ในช่องล่าง
  slotMetric[1] = constrain(prefs.getInt("slotB", M_IAT), 0, N_METRICS - 1);
  if (slotMetric[0] == slotMetric[1]) slotMetric[1] = (slotMetric[0] == M_IAT) ? M_COOLANT : M_IAT;
  gearCount = constrain(prefs.getInt("gcnt", 6), 3, 6);
  for (int g = 0; g < 6; g++) {
    char k[5] = {'g', 'r', (char)('0' + g), 0, 0};
    float v = prefs.getFloat(k, GEAR_DEF[g]);
    GEAR_RPM_PER_KMH[g] = (v >= 5.0f && v <= 400.0f) ? v : GEAR_DEF[g];
  }
  pinMode(LDR_PIN, INPUT);

  {                                              // บันทึกสาเหตุที่เครื่องรีบูต (ดูได้ในเมนู DISPLAY และ Serial)
    esp_reset_reason_t rr = esp_reset_reason();
    bool crashed = (rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT || rr == ESP_RST_TASK_WDT || rr == ESP_RST_WDT || rr == ESP_RST_BROWNOUT);
    bool heapRst = prefs.getBool("hr", false);
    Serial.printf("reset reason: %s%s\n", resetReasonText(rr), heapRst ? " (low-heap restart)" : "");
    if (crashed) { prefs.putInt("crashes", prefs.getInt("crashes", 0) + 1); prefs.putString("lastRst", resetReasonText(rr)); }
    if (heapRst) { prefs.putString("lastRst", "LOW_HEAP"); prefs.putBool("hr", false); }
    fastBoot = crashed || heapRst;
  }

  int32_t tx, ty;
  bool holdAtBoot = getTouchXY(&tx, &ty);     // แตะค้างตอนเปิดเครื่อง = คาลิเบรตทัชใหม่
  calibrateTouchIfNeeded(holdAtBoot);          // คาลิเบรตที่ทิศปกติเสมอ แล้วค่อยกลับจอตามที่ตั้งไว้
  flip180 = prefs.getBool("flip", false);
  applyFlip();

  Serial.printf("before splash: heap %u (largest block %u)\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  poolInit();                                    // จองก้อนแรมกลางตอนแรมยังว่างเป็นก้อนใหญ่สุด

  if (!fastBoot) splash();                       // หน้าปัดเข็มกวาด SPLASH_MS มิลลิวินาที (แตะข้ามได้) ข้ามถ้าเพิ่งรีบูตเพราะค้าง
#if HAS_LOGO
  if (!fastBoot) logoScreen();                   // ภาพโลโก้ค่อย ๆ สว่างขึ้น (แตะข้ามได้)
#endif

  drawStatic(false);                             // วาดพื้นหลังก่อนเริ่ม Bluetooth (ตอนนี้แรมยังเหลือมากสุด)
  for (int i = 1; i <= 20; i++) { setBriF(i / 20.0f); delay(20); }   // ภาพหลักค่อย ๆ สว่าง

#if LINK_ESPNOW
  nowBegin();                                    // รับค่าจากบอร์ด S3
  Serial.printf("after ESP-NOW: heap %u (largest block %u)\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#else
  SerialBT.begin("ESP32-OBD", true);            // master
  xTaskCreatePinnedToCore(obdTask, "obd", 8192, NULL, 1, NULL, 0);
  Serial.printf("after Bluetooth: heap %u (largest block %u)\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
  if (ESP.getFreeHeap() < POOL_MIN_FREE) Serial.printf("WARNING: low free heap after Bluetooth (%u bytes)\n", (unsigned)ESP.getFreeHeap());
  prefs.putUInt("btHeap", ESP.getFreeHeap());    // ไว้ดูในเมนู DISPLAY
}

// เปลี่ยนหน้า: หรี่จอ -> วาดหน้าใหม่ -> ค่อย ๆ สว่าง
void switchPage(int np) {
  for (int i = 9; i >= 0; i--) { setBriF(i / 10.0f); delay(12); }
  page = np;
  prefs.putInt("page", page);
  drawStatic(false);
  invalidateUI();
  for (int i = 1; i <= 10; i++) { setBriF(i / 10.0f); delay(15); }
}

bool     tDown = false;
int      tSx = 0, tSy = 0, tLx = 0, tLy = 0;
uint32_t tStart = 0, tLast = 0;

void loop() {
  int32_t x, y;
  uint32_t now = millis();
  if (getTouchXY(&x, &y)) {
    tLast = now;
    if (!tDown) { tDown = true; tSx = x; tSy = y; tStart = now; }
    tLx = x; tLy = y;
    if (tSy - tLy > 50 && abs(tLx - tSx) < 50) {        // ลากขึ้น = เปิดเมนู (เปิดทันทีระหว่างลาก)
      tDown = false;
      menuScreen();
      waitRelease();
      return;
    }
  } else if (tDown && now - tLast > 80) {                 // ปล่อยนิ้วแล้ว -> ตัดสินว่าปัดหรือแตะ
    tDown = false;
    int dx = tLx - tSx, dy = tLy - tSy;
    if (N_PAGES > 1 && abs(dx) > 50 && abs(dy) < 45 && now - tStart < 900) {
      switchPage(nextPage(page));                         // ปัดซ้ายหรือขวา = สลับหน้า
    } else if (abs(dx) < 20 && abs(dy) < 20 && inRect(tSx, tSy, MENU_X, MENU_Y, MENU_W, MENU_H)) {   // แตะมุมขวาบน = เปิดเมนู (สำรอง)
      menuScreen();
      waitRelease();
      tDown = false;
    } else if (isHPage(page) && abs(dx) < 20 && abs(dy) < 20 && hTap(tSx, tSy)) {
      // หน้า 9: แตะช่องล่าง = สลับค่า
    } else if (page == PAGE_DASH && abs(dx) < 20 && abs(dy) < 20 && inRect(tSx, tSy, 3, 55, 100, 54)) {
      tripKm = 0; demoTrip = 0; prefs.putFloat("trip", 0.0f);     // แตะช่อง TRIP = รีเซ็ตระยะทริป
      pTxt[1][0] = 0;
    }
  }
  updateBrightness();
  if (demoMode) demoTick(now);
#if LINK_ESPNOW
  nowTick(now);
#endif
  updateUI();
  {                                                      // เก็บระยะทริปลง NVS ทุก 60 วินาที (เฉพาะเมื่อเปลี่ยน)
    static uint32_t lastTripSave = 0; static float savedTrip = 0;
    if (now - lastTripSave > 60000) { lastTripSave = now; if (fabsf(tripKm - savedTrip) >= 0.1f) { prefs.putFloat("trip", tripKm); savedTrip = tripKm; } }
  }
  heapWatch(now);
  delay(1);
}
