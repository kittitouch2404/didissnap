/*
  ESP32 CYD (ESP32-2432S028, จอ 2.8" 320x240) + ELM327 Bluetooth 3.0 (Classic)
  แสดง: รอบเครื่อง / ความเร็ว / อุณหภูมิน้ำ / แรงดันแบต
  - หน้า 1-4 วาดด้วยกราฟิกทั้งหมด (ไม่ใช้รูปพื้นหลัง): 1 กราฟแท่งรอบทรงลาด, 2 ช่องสีเหลือง, 3 แท่งรอบโค้งแบบจอรถแข่ง, 4 เกจครึ่งวง
  - หน้า 5 หน้าปัดกลม, หน้า 6 ไมล์แบบแถบ (ปัดซ้าย/ขวาเพื่อสลับหน้า)
  - หน้า 12 = Jazz #66 วิ่งทางด่วนกลางวัน (แบบหน้า 10), หน้า 13 = ภาพนิ่ง J's Racing (แบบหน้า 9)
  - ตัวเลขวาดผ่านสไปรท์เล็ก ๆ วาดใหม่เฉพาะส่วนที่เปลี่ยน = ไม่กะพริบ
  - ค่าที่แสดง: รอบ / ความเร็ว / เกียร์ / น้ำ / IAT / บูสต์ / แอร์โฟลว์ / โหลด / TPS / AFR / แบต / ทริป
  - หน้า 9 = GIF ในกรอบ + ตัวเลขไตเติลอนิเมะ (gifbox.h)
  - ปัดขึ้นเพื่อเปิดเมนู (รายการ 7 ข้อ): รอบสูงสุด / อุณหภูมิเตือน / จอ / Bluetooth / DTC / เกียร์ / ค่าสูงสุด

  Library : LovyanGFX (ติดตั้งจาก Library Manager)
  Board   : ESP32 Dev Module
  Partition Scheme : "Custom" (partitions.csv ในโฟลเดอร์นี้ ~3.9 MB) หรือ "Huge APP (3MB)" -- 13 หน้า ~2.45 MB เกิน 2MB
*/

#include <Arduino.h>
// ---------- ทางรับข้อมูลจากรถ ----------
// 1 = ต่อ CAN ตรงจากปลั๊ก OBD2 (ทรานซีฟเวอร์ SN65HVD230: CTX -> GPIO22, CRX -> GPIO27) ไม่ใช้ Bluetooth
//     + จอหลับเองเมื่อสาย CAN เงียบ (ดับรถ) และตื่นเองเมื่อมีสัญญาณบนสาย CAN หรือแตะจอ
// 0 = Bluetooth ELM327 (Vgate) แบบเดิม
#define OBD_LINK_CAN   0
// ---- ต่อ ESP32-C3 (ตัวอ่าน OBD ที่ช่อง OBD2) ทาง Bluetooth BLE: C3 อ่าน CAN/K-line เอง แล้วส่งให้จอนี้ + จอ S3 + แอปพร้อมกัน ----
//  1 = ใช้ C3 (ไม่ใช้ Vgate / ไม่ใช้ WiFi)   0 = แบบเดิม (Vgate Bluetooth + WiFi จากแอป)
//  ต้องลงไลบรารี "NimBLE-Arduino" (by h2zero, 2.x)
#define OBD_LINK_C3    1
#if OBD_LINK_C3
#undef OBD_LINK_CAN
#define OBD_LINK_CAN   0
#endif
#if OBD_LINK_CAN
#include "driver/twai.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#define CAN_TX_PIN       22      // GPIO22 (ช่อง CN1/P3) -> CTX ของทรานซีฟเวอร์
#define CAN_RX_PIN       27      // GPIO27 (ช่อง CN1)    -> CRX ของทรานซีฟเวอร์ (ใช้ปลุกจอด้วย)
#define CAN_SLEEP_IDLE_S 60      // สาย CAN เงียบ + ECU ไม่ตอบ นานเท่านี้ (วินาที) -> จอหลับ (0 = ไม่หลับ)
#define CAN_TIMER_WAKE_S 0       // ตอนหลับ ตื่นมาลองถาม ECU ทุก ๆ กี่วินาที (0 = ไม่ใช้, ตื่นด้วยสัญญาณ CAN/แตะจอเท่านั้น)
#elif OBD_LINK_C3
#include <NimBLEDevice.h>
#include "didinow.h"
#include "c3pair.h"           // จับคู่กับ C3 ตัวเดียว (ไม่ต่อผิดคันตอนจอดรวมกัน)
#define LINK_WIFI      0
#else
#include <BluetoothSerial.h>
// ---- รับค่าจากจอแอนดรอยด์ทาง WiFi (แอปต่อ Vgate แล้วส่งค่าต่อให้จอนี้) ไม่เจอค่อยใช้ Bluetooth ต่อ Vgate เอง ----
#define LINK_WIFI      1                 // 0 = ไม่มีระบบ WiFi (Bluetooth อย่างเดียวแบบเดิม)
// ฮอตสปอตที่จอรู้จัก (สูงสุด 3 ชื่อ) จอสแกนแล้วต่อตัวที่เจอและสัญญาณแรงสุด  ไม่ใช้ช่องไหนเว้นเป็น "" ไว้
#define WIFI_SSID      "DIDISSNAP"       // ชื่อที่ 1
#define WIFI_PASS      "09012416"
#define WIFI_SSID2     ""                // ชื่อที่ 2 (เช่น มือถืออีกเครื่อง)
#define WIFI_PASS2     ""
#define WIFI_SSID3     ""                // ชื่อที่ 3
#define WIFI_PASS3     ""
#define WIFI_UDP_PORT  35000             // แอปส่งค่ามาที่พอร์ตนี้ (UDP)
#define WIFI_BEACON_PORT 35001           // จอประกาศตัวเอง "CYD-OBD" ทุก 1 วินาที (แอปใช้หา IP จอได้)
#define WIFI_FIRST_MS  15000             // เปิดเครื่องแล้วรอค่าทาง WiFi นานเท่านี้ ไม่มา = เปลี่ยนไปใช้ Bluetooth
#define WIFI_LOST_MS   10000             // กำลังใช้ WiFi แล้วค่าเงียบนานเท่านี้ = เปลี่ยนไปใช้ Bluetooth (โหมด AUTO)
#if LINK_WIFI
#include <WiFi.h>
#include <WiFiUdp.h>
#endif
#endif
#include <Preferences.h>
#include "esp_bt.h"
#include "esp_system.h"
#include "esp_timer.h"

// ข้อมูล GIF ของหน้า 9-11 (ประกาศไว้บนสุด: Arduino IDE สร้างต้นแบบฟังก์ชันไว้ด้านบนไฟล์ ชนิดนี้ต้องมีก่อน)
struct QGif { const uint8_t *const *jpg; const uint32_t *len; const uint16_t *dms; int n, ms, w, h; };
struct QInfo { const char *lab, *unit; int8_t dec; float lo, hi; };   // ชื่อ/หน่วย/ช่วงของค่า (ต้องประกาศก่อนฟังก์ชัน)

#define LGFX_USE_V1
#include <LovyanGFX.hpp>


#if __has_include("anime.h")
#include "anime.h"          // หน้า 9: ภาพพื้นหลัง anime_jpg 320x240 (สร้างด้วย tools/make_pixel_bg.py)
#define HAS_ANIME 1
#else
#define HAS_ANIME 0         // ไม่มีไฟล์ = ไม่มีหน้า 9
#endif

#if __has_include("car.h") && __has_include("wheels.h")
#include "car.h"            // หน้า 10 (รถวิ่ง): car_jpg = ภาพเต็มจอเฟรมแรก 320x240
#include "wheels.h"         // หน้า 10: ส่วนล่างที่เคลื่อนไหว ROAD_JPG[] (สร้างทั้งคู่ด้วย tools/make_gif_page.py)
#define HAS_CARPAGE 1
#else
#define HAS_CARPAGE 0       // ไม่มีไฟล์ = ไม่มีหน้า 10
#endif

#if __has_include("photo2.h")
#include "photo2.h"         // หน้า 11: ภาพนิ่ง photo2_jpg 320x240 (สร้างด้วย tools/make_pixel_bg.py --name photo2)
#define HAS_PHOTO2 1
#else
#define HAS_PHOTO2 0        // ไม่มีไฟล์ = ไม่มีหน้า 11
#endif

#if __has_include("car2.h") && __has_include("wheels2.h")
#include "car2.h"           // หน้า 12 (รถวิ่ง Jazz #66 ทางด่วนกลางวัน): car2_jpg = ภาพเต็มจอเฟรมแรก 320x240
#include "wheels2.h"        // หน้า 12: ส่วนล่างที่เคลื่อนไหว ROAD2_JPG[] (สร้างด้วย tools/make_gif_page.py ... --suffix 2)
#define HAS_CARPAGE2 1
#else
#define HAS_CARPAGE2 0      // ไม่มีไฟล์ = ไม่มีหน้า 12
#endif

#define HAS_ANIME2 0        // หน้า 13 เปลี่ยนเป็นรถวิ่ง (car3.h / wheels3.h) แล้ว ไม่ใช้ anime2.h

#if __has_include("car3.h") && __has_include("wheels3.h")
#include "car3.h"           // หน้า 13 (รถวิ่ง Jazz แดง #69 กลางคืนใต้พระจันทร์): car3_jpg = ภาพเต็มจอเฟรมแรก 320x240
#include "wheels3.h"        // หน้า 13: ส่วนล่างที่เคลื่อนไหว ROAD3_JPG[] (สร้างด้วย tools/make_gif_page.py ... --suffix 3)
#define HAS_CARPAGE3 1
#else
#define HAS_CARPAGE3 0      // ไม่มีไฟล์ = ไม่มีหน้า 13
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
#define DEF_MAX_RPM    5000      // redline เริ่มต้น (เปลี่ยนได้ในเมนู MAX RPM)
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
#define SPK_PIN        26        // ช่อง SPEAK บนบอร์ด CYD (ผ่านชิปขยายเสียง) ต่อบัสเซอร์/ลำโพงเล็กตรงนี้
#define USE_SOUND      0         // 0 = ปิดระบบเสียงทั้งหมด (ไม่แตะขา GPIO26 เลย)
#define SPK_CH         0         // ช่อง PWM ของเสียง (ไฟจอใช้ช่อง 7 คนละ timer กัน)
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
#if OBD_LINK_C3
// โหมด C3: ไม่มี Bluetooth แบบเดิม — ตัวแทนว่าง ๆ ให้โค้ดเมนู Bluetooth เดิมคอมไพล์ผ่าน (ไม่ได้ใช้งาน)
struct BTAddrStub { std::string toString() { return ""; } };
struct BTAdvertisedDevice { std::string getName() { return ""; } BTAddrStub getAddress() { return {}; } };
struct BTScanResults { int getCount() { return 0; } BTAdvertisedDevice *getDevice(int) { return nullptr; } };
bool c3Up();
struct NoBT {
  bool begin(const char *, bool = false) { return true; }
  bool connect(const char *) { return false; }
  bool connect(uint8_t *) { return false; }
  bool connected() { return c3Up(); }
  void disconnect() {}
  BTScanResults *discover(int) { return nullptr; }
  int available() { return 0; }
  int read() { return -1; }
  template <class T> void print(T) {}
  void setPin(const char *, int = 0) {}
} SerialBT;
#elif !OBD_LINK_CAN
BluetoothSerial SerialBT;
#endif
Preferences prefs;

// ---------- กลับจอ 180 องศา (MENU -> DISPLAY -> FLIP 180) ----------
bool flip180 = false;
int  curRotation() { return flip180 ? (LCD_ROTATION ^ 2) : LCD_ROTATION; }   // 3<->1, 7<->5
// =============================== SOUND: เสียงเตือนทางช่อง SPEAK ===============================
// เล่นเสียงแบบไม่หน่วงจอ: ตั้งรูปแบบเสียง (ความถี่, ms) แล้วตัวจับเวลา 5 ms เปลี่ยนโน้ตให้เอง
enum { SND_SHIFT, SND_TEMP, SND_BATT, SND_OBD, SND_CLICK, SND_N };
const char *const SND_NAME[SND_N] = {"SHIFT (redline)", "WATER TEMP", "BATTERY LOW", "OBD CONNECT/LOST", "TOUCH CLICK"};
bool sndOn = true;                                     // เปิด/ปิดเสียงทั้งหมด
uint8_t sndMask = 0x0F;                                // เปิดรายเรื่อง (บิตตาม SND_*) ค่าเริ่มต้น: ทุกอย่างยกเว้นเสียงคลิก
int sndVol = 3;                                        // ความดัง 1-5
struct SNote { uint16_t f, ms; };                      // f = 0 คือเงียบ
const SNote P_SHIFT[] = {{3200, 45}, {0, 55}};
const SNote P_TEMP[]  = {{1800, 350}, {0, 200}, {1800, 350}, {0, 200}, {1800, 350}};
const SNote P_TEMP1[] = {{1800, 350}};
const SNote P_BATT[]  = {{900, 150}, {0, 120}, {900, 150}};
const SNote P_CONN[]  = {{1200, 80}, {1800, 120}};
const SNote P_LOST[]  = {{1800, 100}, {900, 220}};
const SNote P_CLICK[] = {{4000, 8}};
const SNote P_VOL[]   = {{2000, 90}};
const SNote *sndCur = nullptr; int sndLen = 0, sndIdx = 0, sndPrio = -1; uint32_t sndEnd = 0;
portMUX_TYPE sndMux = portMUX_INITIALIZER_UNLOCKED;
static void sndTone(uint16_t f) {
  static const uint8_t VOLD[6] = {0, 3, 8, 20, 50, 128};  // ความดัง = ความกว้างพัลส์ (128 = ดังสุด)
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  if (f) { ledcWriteTone(SPK_PIN, f); ledcWrite(SPK_PIN, VOLD[constrain(sndVol, 1, 5)]); } else ledcWrite(SPK_PIN, 0);
#else
  if (f) { ledcWriteTone(SPK_CH, f); ledcWrite(SPK_CH, VOLD[constrain(sndVol, 1, 5)]); } else ledcWrite(SPK_CH, 0);
#endif
}
// เสียงทั้งหมดเล่นใน task ของตัวเอง (สั่ง PWM จากที่เดียว ไม่ชนกับ loop/ตัวจับเวลา) ที่อื่นแค่ "ฝากคำสั่ง" ไว้
volatile bool sndNew = false;
static void sndTask(void *) {
  uint16_t curF = 0;
  for (;;) {
    uint16_t f = curF; bool chg = false;
    portENTER_CRITICAL(&sndMux);
    if (sndNew) { sndNew = false; f = sndCur ? sndCur[0].f : 0; chg = true; }
    else if (sndCur && (int32_t)(millis() - sndEnd) >= 0) {
      if (++sndIdx < sndLen) { f = sndCur[sndIdx].f; sndEnd = millis() + sndCur[sndIdx].ms; }
      else { sndCur = nullptr; sndPrio = -1; f = 0; }
      chg = true;
    }
    portEXIT_CRITICAL(&sndMux);
    if (chg && f != curF) { sndTone(f); curF = f; }
    vTaskDelay(pdMS_TO_TICKS(4));
  }
}
void sndBegin() {
#if USE_SOUND
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttachChannel(SPK_PIN, 2000, 8, SPK_CH);
  ledcWrite(SPK_PIN, 0);
#else
  ledcSetup(SPK_CH, 2000, 8); ledcAttachPin(SPK_PIN, SPK_CH); ledcWrite(SPK_CH, 0);
#endif
  xTaskCreatePinnedToCore(sndTask, "snd", 3072, NULL, 1, NULL, 1);
#endif
}
// prio: 0 คลิก, 1 shift, 2 OBD, 3 แบต, 4 น้ำร้อน  (เสียงสำคัญกว่าตัดเสียงที่เล่นอยู่ได้)  force = เล่นแม้ปิดเรื่องนั้นไว้ (ปุ่ม TEST)
bool sndPlay(int kind, const SNote *p, int n, int prio, bool force = false) {
#if !USE_SOUND
  return false;
#endif
  if (!force && (!sndOn || !(sndMask & (1 << kind)))) return false;
  bool ok = false;
  portENTER_CRITICAL(&sndMux);
  if (!sndCur || prio > sndPrio || force) { sndCur = p; sndLen = n; sndIdx = 0; sndPrio = prio; sndEnd = millis() + p[0].ms; sndNew = true; ok = true; }
  portEXIT_CRITICAL(&sndMux);
  return ok;
}
bool sndBusy() { return sndCur != nullptr; }
#define SND_PLAY(k, P, pr) sndPlay(k, P, sizeof(P) / sizeof(P[0]), pr)

bool getTouchXY(int32_t *x, int32_t *y) {
  static bool wasDown = false;
  if (!lcd.getTouch(x, y)) { wasDown = false; return false; }
  if (!wasDown) { wasDown = true; SND_PLAY(SND_CLICK, P_CLICK, 0); }      // เสียงคลิกตอนเริ่มแตะ (ถ้าเปิดไว้)
  if (flip180 && TOUCH_FLIP_FIX) { *x = 319 - *x; *y = 239 - *y; }
  return true;
}
void applyFlip() { lcd.setRotation(curRotation()); }

// ---------- ค่าที่ตั้งได้ (เก็บใน NVS) ----------
int    maxRpm    = DEF_MAX_RPM;
int    tempAlert = DEF_TEMP_ALERT;
// ชนิดเครื่องยนต์ (เมนูข้อ 1 ENGINE / REDLINE): ช่วง redline, ช่วง lambda ของ AFR, ช่อง TPS (ดีเซล = แป้นคันเร่ง PID 49)
bool   engDiesel = false;
#define GAS_RL_MIN 4000
#define GAS_RL_MAX 9000
#define GAS_RL_DEF 7000
#define DSL_RL_MIN 4500
#define DSL_RL_MAX 5500
#define DSL_RL_DEF 5000
inline int   rlMin() { return engDiesel ? DSL_RL_MIN : GAS_RL_MIN; }
inline int   rlMax() { return engDiesel ? DSL_RL_MAX : GAS_RL_MAX; }
inline float lamMax() { return engDiesel ? 8.0f : 1.9f; }          // ดีเซลวิ่งบาง lambda สูงได้ถึง ~4-8
inline int   tpsPid() { return engDiesel ? 0x49 : 0x11; }           // ดีเซลไม่มีลิ้นปีกผีเสื้อ -> ตำแหน่งแป้นคันเร่ง
int    afrSt10  = 147;                    // ค่า stoich x10 (น้ำมันเบนซิน 14.7) ตั้งในเมนู AFR SCALE
int    afrOfs10 = 0;                      // ชดเชย AFR x10 (+/-) ไว้ปรับให้ตรงกับเกจไวด์แบนด์ตัวอื่น
volatile float afrLambda = 0;             // lambda ล่าสุดที่อ่านได้ (โชว์สดในหน้าเมนู)
String btName = "";                 // อุปกรณ์ที่เลือกไว้ (ว่าง = ค้นหาด้วยชื่อ OBD_NAME)
String btMac  = "";
char   btPin[8] = DEF_PIN;
volatile int page = 0;               // หน้าที่แสดงอยู่ (ปัดซ้าย/ขวาเพื่อเปลี่ยน)
const int PG_A = 0, PG_B = 1, PG_C = 2, PG_D = 3;   // หน้า 1-4 แบบใหม่ (กราฟิกล้วน)
const int PAGE_GAUGE = 4, PAGE_DASH = 5;            // หน้า 5 หน้าปัดกลม, หน้า 6 ไมล์แบบแถบ
const int PG_E = 6;                                 // หน้า 7 จอแข่งสีส้ม
const int PG_F = 7;                                 // หน้า 8 เกจโค้ง + ค่าสูงสุด/ต่ำสุด
const int PG_H = 8;                                 // หน้า 9 ภาพพื้นหลัง + ตัวเลขลอย (หน้า 1 แบบเดิม)
const int PG_I = 9;                                 // หน้า 10 รถวิ่ง (ภาพเคลื่อนไหวจาก GIF)
const int PG_J = 10;                                // หน้า 11 ภาพนิ่ง + รอบ/ความร้อนด้านบน (แบบหน้า 2 เดิม)
const int PG_L = 11;                                // หน้า 12 รถวิ่งที่ 2 (car2.h / wheels2.h) เลย์เอาต์เดียวกับหน้า 10
const int PG_K = 12;                                // หน้า 13 รถวิ่งที่ 3 (car3.h / wheels3.h) เลย์เอาต์เดียวกับหน้า 10 / 12
const int PG_Q = 13;                                // หน้า 9 (ตามลำดับที่เห็น) GIF ในกรอบ + ตัวเลขไตเติลอนิเมะ (gifbox.h)
const int PG_Q2 = 14, PG_Q3 = 15;                   // หน้า 10 / 11 แบบเดียวกัน (gifbox2.h / gifbox3.h)
const int PG_Q4 = 16, PG_Q5 = 17;                   // หน้า 12 / 13 แบบเดียวกัน (gifbox4.h / gifbox5.h)
const int N_PAGES = 18;
#define Q_NP 5                                      // จำนวนหน้า GIF (หน้า 9-13)
inline bool isQPage(int p) { return p == PG_Q || (p >= PG_Q2 && p <= PG_Q5); }
inline int  qPageIdx(int p) { return p == PG_Q ? 0 : p - PG_Q2 + 1; }
inline bool isFPage(int p) { return p == PG_I || p == PG_J || p == PG_L || p == PG_K; }   // หน้าที่ใช้เลย์เอาต์หน้า 10 (รอบ/ความร้อนด้านบน)
inline bool isHPage(int p) { return p == PG_H; }   // หน้าที่ใช้เลย์เอาต์หน้า 9 (ภาพพื้นหลัง + ตัวเลขลอย)
uint32_t pageOff = 0;                                // หน้าที่ปิดไว้ (บิตตามเลขหน้าภายใน) ตั้งในเมนู PAGES
bool pageAvail(int p) { return p >= 0 && p < N_PAGES && !(pageOff & (1UL << p)) && !(p == PG_H && !HAS_ANIME) && !(p == PG_I && !HAS_CARPAGE) && !(p == PG_J && !HAS_PHOTO2) && !(p == PG_K && !HAS_CARPAGE3) && !(p == PG_L && !HAS_CARPAGE2); }
int nextPage(int p) { do { p = (p + 1) % N_PAGES; } while (!pageAvail(p)); return p; }

// ---- โหมดเดโม: จำลองค่าขับรถเองทั้งหมด (เริ่ม/หยุดที่ปุ่ม DEMO ในเมนู) ----
volatile bool demoMode = false;
int    briPct  = 80;                // ความสว่าง 10-100 %
bool   briAuto = false;             // true = ปรับอัตโนมัติด้วยเซ็นเซอร์แสง

// ---------- ค่าที่อ่านได้ (OBD task เขียน, UI อ่าน) ----------
volatile int   rpm = 0, speedKmh = 0, coolant = 0, iat = 0;

// ---- ค่าที่แสดงในช่องล่างของหน้าหลัก: แตะที่ช่องเพื่อสลับไปค่าถัดไป ----
enum { M_COOLANT, M_IAT, M_BATT, M_LOAD, M_TPS, M_MAP, M_MAF, M_AFR, M_STFT, M_OIL, M_TIMING, N_METRICS };
const char *const METRIC_LABEL[N_METRICS] = {"COOLANT", "IAT", "BATT", "LOAD", "TPS", "MAP", "MAF", "AFR", "STFT", "OIL T", "TIMING"};
const char *const METRIC_UNIT[N_METRICS]  = {"deg C", "deg C", "volt", "%", "%", "kPa", "g/s", "%", "%", "deg C", "deg"};
volatile float metricVal[N_METRICS];     // ค่าล่าสุดของแต่ละตัว
volatile bool  metricHave[N_METRICS];    // เคยอ่านได้แล้วหรือยัง (ยังไม่ได้ = แสดง --)
// ค่าพิเศษที่เลือกได้เฉพาะหน้า GIF (ไม่ได้ขอจาก OBD ตรงๆ)
//  Q_CVT = อัตราทดพูลเลย์ CVT คำนวณจากรอบ/ความเร็ว  Q_ATF = อุณหภูมิน้ำมันเกียร์ CVT (ทดลอง: แอปอ่านผ่าน Mode 22 แล้วส่งมาทาง WiFi)
enum { Q_CVT = N_METRICS, Q_ATF, Q_FUELI, Q_FUELA, Q_C1, Q_C2, Q_C3, Q_C4, Q_NM };   // Q_C1-4 = Custom PID ที่ตั้งในแอป (ชื่อ/หน่วย/ช่วง ส่งมาทาง WiFi)
#define N_CUST 4
char  cLab[N_CUST][12] = {"CUSTOM 1", "CUSTOM 2", "CUSTOM 3", "CUSTOM 4"}, cUnit[N_CUST][6] = {"", "", "", ""};
float cLo[N_CUST] = {0, 0, 0, 0}, cHi[N_CUST] = {100, 100, 100, 100}; int8_t cDec[N_CUST] = {1, 1, 1, 1};
bool  cDef[N_CUST] = {false, false, false, false};          // แอปตั้งช่องนี้ไว้
volatile float custVal[N_CUST]; volatile uint32_t cMs[N_CUST]; volatile int cGen = 0;   // cGen เปลี่ยน = ชื่อ/หน่วยเปลี่ยน ต้องวาดช่องใหม่   // + อัตราสิ้นเปลือง ตอนนี้ / เฉลี่ย (km/L จาก MAF)
#define CVT_TIRE_MM   1910      // เส้นรอบวงล้อ (mm)  Jazz GK 175/65R15 หรือ 185/55R16 ~ 1910
#define CVT_FIXED_X1000 4989    // ทดคงที่หลังพูลเลย์ = secondary 1.266 x final 3.941 (Fit/Jazz GK CVT)  -> พูลเลย์ 2.53 (ต่ำสุด) ถึง 0.41 (OD)
volatile float atfTemp = 0; volatile bool atfHave = false; volatile uint32_t atfMs = 0;
volatile int   urgentMetric = -1;        // สั่งให้ OBD task อ่านค่านี้ทันที (ตอนเพิ่งสลับ)
uint32_t pidMask[3];                     // PID ที่รถรองรับ (จาก 0100 / 0120 / 0140)
bool     pidMaskKnown[3] = {false, false, false};
volatile bool  obdOk = false;
const char * volatile statusMsg = "BT...";
void setStatus(const char *m) { statusMsg = m; }
bool statusOk(const char *m) { return !strcmp(m, "OBD OK") || !strcmp(m, "WIFI OK"); }
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
#if OBD_LINK_CAN
// =============================== CAN ตรง (TWAI ในชิป ESP32) ===============================
// ทำตัวเป็น "ELM327 จำลอง": รับคำสั่งแบบเดียวกับ ELM (เช่น "010C", "ATRV") แล้วถาม ECU ผ่าน CAN 500k 11 บิต
// ตอบกลับเป็นข้อความ hex แบบ ELM (ไม่มีช่องว่าง) โค้ดส่วนอื่นจึงใช้ได้เหมือนเดิมทั้งหมด
volatile uint32_t canLastRx = 0, canRxCount = 0, canEcuMs = 0, canBootMs = 0;
bool canUp = false;
volatile bool canSleepReq = false;
bool canBegin() {
  if (canUp) return true;
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_NORMAL);
  g.rx_queue_len = 32; g.tx_queue_len = 4;
  twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&g, &t, &f) != ESP_OK) return false;
  if (twai_start() != ESP_OK) { twai_driver_uninstall(); return false; }
  canUp = true;
  return true;
}
void canEnd() { if (canUp) { twai_stop(); twai_driver_uninstall(); canUp = false; } }
void canHealth() {                                       // หลุด bus-off (เช่น ถอดปลั๊ก) -> กู้คืนเอง
  twai_status_info_t st;
  if (twai_get_status_info(&st) != ESP_OK) return;
  if (st.state == TWAI_STATE_BUS_OFF) twai_initiate_recovery();
  else if (st.state == TWAI_STATE_STOPPED) twai_start();
}
// ส่งได้เมื่อ: คุยกับ ECU อยู่ / สายมีสัญญาณใน 5 วินาทีล่าสุด / เพิ่งเปิดเครื่อง 20 วินาที  (ไม่ส่งตอนสายเงียบ = ไม่ไปปลุกกล่องอื่นในรถตอนจอด)
bool canTxAllowed() {
  uint32_t n = millis();
  return obdOk || (canRxCount && n - canLastRx < 5000) || n - canBootMs < 20000;
}
bool canSend(uint32_t id, const uint8_t *d) {
  twai_message_t m = {};
  m.identifier = id; m.data_length_code = 8; m.flags = TWAI_MSG_FLAG_SS;   // ส่งครั้งเดียว ไม่ส่งซ้ำวนถ้าไม่มีใครรับ
  memcpy(m.data, d, 8);
  return twai_transmit(&m, pdMS_TO_TICKS(20)) == ESP_OK;
}
// ถาม ECU (ISO 15765 / ISO-TP): req = mode + PID..., ได้ payload เช่น 41 0C 1A F8  คืนจำนวนไบต์ (-1 = ไม่ตอบ)
int canReq(const uint8_t *req, int n, uint8_t *out, int cap, uint32_t tmo) {
  if (!canUp || n < 1 || n > 7 || !canTxAllowed()) return -1;
  canHealth();
  twai_message_t m;
  while (twai_receive(&m, 0) == ESP_OK) { canLastRx = millis(); canRxCount++; }   // ทิ้งเฟรมเก่าในคิว
  uint8_t f[8] = {(uint8_t)n, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55};
  memcpy(f + 1, req, n);
  if (!canSend(0x7DF, f)) return -1;
  uint32_t t0 = millis(), fbT = 0; int got = 0, total = 0, fbN = 0; uint32_t src = 0; uint8_t sn = 1, fb[8];
  while (millis() - t0 < tmo) {
    if (fbN && millis() - fbT > 30) { int c = min(fbN, cap); memcpy(out, fb, c); return c; }   // ไม่มีคำตอบจาก ECU เครื่อง (7E8) ใช้ของกล่องอื่น
    if (twai_receive(&m, pdMS_TO_TICKS(3)) != ESP_OK) continue;
    canLastRx = millis(); canRxCount++;
    if ((m.flags & TWAI_MSG_FLAG_EXTD) || m.identifier < 0x7E8 || m.identifier > 0x7EF || m.data_length_code < 2) continue;
    if (src && m.identifier != src) continue;
    uint8_t pci = m.data[0] >> 4;
    if (pci == 0 && !src) {                                // คำตอบเฟรมเดียว
      int L = min(m.data[0] & 15, 7);
      if (L >= 3 && m.data[1] == 0x7F && m.data[3] == 0x78) { t0 = millis(); continue; }   // ECU บอกว่า "รอสักครู่"
      if (m.identifier == 0x7E8) { int c = min(L, cap); memcpy(out, m.data + 1, c); return c; }
      if (!fbN) { fbN = L; memcpy(fb, m.data + 1, L); fbT = millis(); }
    } else if (pci == 1 && !src) {                         // เฟรมแรกของคำตอบยาว -> ส่ง flow control ให้ส่งที่เหลือมาเลย
      total = ((m.data[0] & 15) << 8) | m.data[1]; src = m.identifier;
      got = min(6, min(total, cap)); memcpy(out, m.data + 2, got);
      uint8_t fc[8] = {0x30, 0x00, 0x00, 0x55, 0x55, 0x55, 0x55, 0x55};
      canSend(src - 8, fc); sn = 1; t0 = millis();
    } else if (pci == 2 && src) {                          // เฟรมต่อเนื่อง
      if ((m.data[0] & 15) != (sn & 15)) break;
      sn++;
      int c = min(7, min(total - got, cap - got));
      if (c > 0) { memcpy(out + got, m.data + 1, c); got += c; }
      if (got >= total || got >= cap) return got;
      t0 = millis();
    }
  }
  if (fbN) { int c = min(fbN, cap); memcpy(out, fb, c); return c; }
  return got > 0 ? got : -1;
}
int elmBuf(const char *cmd, char *out, size_t cap, uint32_t timeoutMs) {
  out[0] = 0;
  uint8_t o[48];
  if ((cmd[0] == 'A' || cmd[0] == 'a') && (cmd[1] == 'T' || cmd[1] == 't')) {
    if (!strcasecmp(cmd, "ATRV")) {                        // แรงดันแบต = PID 42 (แรงดันที่กล่อง ECU)
      uint8_t q[2] = {0x01, 0x42};
      int n = canReq(q, 2, o, sizeof(o), 150);
      if (n >= 4 && o[0] == 0x41 && o[1] == 0x42) snprintf(out, cap, "%.1fV", (o[2] * 256 + o[3]) / 1000.0f);
    } else if (!strcasecmp(cmd, "ATDPN")) strncpy(out, "A6", cap);
    else strncpy(out, "OK", cap);
    return strlen(out);
  }
  uint8_t q[8]; int nh = 0, nb = 0;
  for (const char *c = cmd; *c; c++) if (isxdigit((unsigned char)*c)) nh++;
  nh &= ~1;                                                // ตัวเลขท้ายเดี่ยว ๆ (เช่น "1" = จำนวนคำตอบของ ELM) ไม่ใช้
  for (int i = 0; i < nh && nb < 7; i += 2) { char h[3] = {cmd[i], cmd[i + 1], 0}; q[nb++] = (uint8_t)strtol(h, NULL, 16); }
  int n = canReq(q, nb, o, sizeof(o), min<uint32_t>(timeoutMs, nb && q[0] >= 3 && q[0] <= 4 ? 3000 : 250));
  if (n <= 0) { strncpy(out, "NODATA", cap); return 0; }
  canEcuMs = millis();
  int k = 0;
  for (int i = 0; i < n && k + 2 < (int)cap; i++) k += snprintf(out + k, cap - k, "%02X", o[i]);
  return k;
}
bool linkUp() { return canUp; }
void canSleepNow() {                                     // ตั้งทางปลุกแล้ว deep sleep (ขาไฟจอ/เสียงถูกล็อก LOW ไว้ก่อนเรียก)
  esp_sleep_enable_ext0_wakeup((gpio_num_t)CAN_RX_PIN, 0);    // บิตแรกบนสาย CAN = ตื่น
  if (digitalRead(36) == HIGH) esp_sleep_enable_ext1_wakeup(1ULL << 36, ESP_EXT1_WAKEUP_ALL_LOW);   // แตะจอ = ตื่น
  if (CAN_TIMER_WAKE_S) esp_sleep_enable_timer_wakeup((uint64_t)CAN_TIMER_WAKE_S * 1000000ULL);
  Serial.flush();
  esp_deep_sleep_start();
}
#elif OBD_LINK_C3
// =============================== ESP32-C3 ทาง BLE ===============================
//  ค่าสด: C3 ส่ง DidiNowGw มาเอง ~30 ครั้ง/วินาที (ไม่ต้องถามทีละ PID)
//  คำสั่งอื่น (อ่าน/ลบ DTC ฯลฯ): ทำตัวเป็น ELM จำลอง ฝากให้ C3 ถามรถแล้วตอบคำตอบดิบกลับมา
void mafSample();
void obdSample();
static NimBLEClient *c3Cli = nullptr;
static NimBLERemoteCharacteristic *c3Cmd = nullptr;
static volatile bool c3Link = false;
static volatile uint32_t c3Last = 0, c3Seq = 0;
static DidiNowGw c3Pkt = {};
static portMUX_TYPE c3Mux = portMUX_INITIALIZER_UNLOCKED;
static DidiNowRawRes c3Res = {};
static volatile uint16_t c3ResId = 0;
static uint16_t c3MyId = 0x4000;
bool c3Up() { return c3Link; }
static void c3Notify(NimBLERemoteCharacteristic *, uint8_t *d, size_t n, bool) {
  if (n < 6 || ((DidiNowCmd *)d)->magic != DIDINOW_MAGIC) return;
  if (c3pair::onPacket(d, (int)n)) return;               // ผลจับคู่
  uint8_t t = ((DidiNowCmd *)d)->type;
  if (t == DN_GW && n >= offsetof(DidiNowGw, timing)) {
    portENTER_CRITICAL(&c3Mux);
    memset(&c3Pkt, 0, sizeof(c3Pkt));
    memcpy(&c3Pkt, d, min(n, sizeof(c3Pkt)));
    if (n < sizeof(c3Pkt)) { c3Pkt.timing = -999; c3Pkt.oil = -999; }
    portEXIT_CRITICAL(&c3Mux);
    c3Last = millis(); c3Seq++;
  } else if (t == DN_RAWRES && n >= sizeof(DidiNowRawRes)) {
    memcpy(&c3Res, d, sizeof(c3Res)); c3ResId = c3Res.id;
  } else if (t == DN_CUST && n >= sizeof(DidiNowCust)) {        // Custom PID ที่ C3 ถามเอง (ตั้งในแอป)
    const DidiNowCust *m = (const DidiNowCust *)d;
    for (int k = 0; k < N_CUST; k++) {
      if (((m->on >> k) & 1) != cDef[k]) { cDef[k] = (m->on >> k) & 1; cGen++; }
      if ((m->have >> k) & 1) { custVal[k] = m->v[k]; cMs[k] = millis(); }
    }
    if ((m->have >> 4) & 1) { atfTemp = m->v[4]; atfHave = true; atfMs = millis(); }   // ช่อง 4 = CVT TEMP
  } else if (t == DN_CUSTINFO && n >= sizeof(DidiNowCustInfo)) {
    const DidiNowCustInfo *m = (const DidiNowCustInfo *)d;
    int k = m->slot;
    if (k < N_CUST && m->on) {
      char lab[12] = "", un[6] = "";
      memcpy(lab, m->name, 11); memcpy(un, m->unit, 5);
      for (char *c = lab; *c; c++) *c = toupper(*c);
      if (!lab[0]) snprintf(lab, sizeof(lab), "CUSTOM %d", k + 1);
      float lo = m->lo, hi = m->hi > m->lo ? m->hi : m->lo + 1; int dc = constrain(m->dec, 0, 2);
      if (strcmp(lab, cLab[k]) || strcmp(un, cUnit[k]) || lo != cLo[k] || hi != cHi[k] || dc != cDec[k] || !cDef[k]) {
        strcpy(cLab[k], lab); strcpy(cUnit[k], un); cLo[k] = lo; cHi[k] = hi; cDec[k] = dc; cDef[k] = true; cGen++;
      }
    }
  }
}
class C3CliCb : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient *, int reason) override { c3Link = false; c3Cmd = nullptr; Serial.printf("C3: BLE disconnected (%d)\n", reason); }
};
static bool c3Connect() {                                // หา C3 แล้วต่อ (บล็อกได้หลายวินาที)
  NimBLEScan *sc = NimBLEDevice::getScan();
  sc->setActiveScan(true); sc->setInterval(60); sc->setWindow(50);
  NimBLEScanResults res = sc->getResults(4000, false);
  const NimBLEAdvertisedDevice *found = c3pair::pick(res);   // C3 ของเราเท่านั้น (ดู c3pair.h)
  if (!found) return false;
  if (!c3Cli) {
    c3Cli = NimBLEDevice::createClient();
    static C3CliCb cb;
    c3Cli->setClientCallbacks(&cb, false);
    c3Cli->setConnectionParams(12, 24, 0, 300);
    c3Cli->setConnectTimeout(5000);
  }
  if (!c3Cli->connect(found)) return false;
  NimBLERemoteService *sv = c3Cli->getService(DIDIBLE_SVC);
  NimBLERemoteCharacteristic *dc = sv ? sv->getCharacteristic(DIDIBLE_DATA) : nullptr;
  c3Cmd = sv ? sv->getCharacteristic(DIDIBLE_CMD) : nullptr;
  if (!dc || !c3Cmd || !dc->subscribe(true, c3Notify)) { c3Cli->disconnect(); return false; }
  if (!c3pair::login(c3Cmd, found->getAddress())) { c3Cli->disconnect(); c3Cmd = nullptr; return false; }
  c3Link = true;
  Serial.printf("C3: BLE connected %s (MTU %u)\n", found->getAddress().toString().c_str(), c3Cli->getMTU());
  return true;
}
// ถามรถผ่าน C3 (คำตอบดิบ)  คืนจำนวนไบต์ หรือ -1
static int c3Ask(const uint8_t *q, int n, uint8_t *out, int cap, uint32_t tmo) {
  if (!c3Link || !c3Cmd || n < 1) return -1;
  DidiNowRaw r = {};
  r.magic = DIDINOW_MAGIC; r.type = DN_RAWREQ; r.len = min(n, 7); r.id = ++c3MyId; r.hdr = 0;
  memcpy(r.data, q, r.len);
  if (!c3Cmd->writeValue((uint8_t *)&r, sizeof(r), false)) return -1;
  uint32_t t0 = millis();
  while (c3ResId != r.id && millis() - t0 < tmo + 500) delay(5);
  if (c3ResId != r.id || c3Res.n <= 0) return -1;
  int k = min((int)c3Res.n, cap);
  memcpy(out, c3Res.data, k);
  return k;
}
int elmBuf(const char *cmd, char *out, size_t cap, uint32_t timeoutMs) {
  out[0] = 0;
  if ((cmd[0] == 'A' || cmd[0] == 'a') && (cmd[1] == 'T' || cmd[1] == 't')) {
    if (!strcasecmp(cmd, "ATRV")) { if (c3Pkt.volt > 1) snprintf(out, cap, "%.1fV", c3Pkt.volt); }
    else if (!strcasecmp(cmd, "ATDPN")) strncpy(out, dnIsK(c3Pkt.proto) ? "3" : "A6", cap);
    else strncpy(out, "OK", cap);
    return strlen(out);
  }
  uint8_t q[8], o[48]; int nh = 0, nb = 0;
  for (const char *c = cmd; *c; c++) if (isxdigit((unsigned char)*c)) nh++;
  nh &= ~1;
  for (int i = 0; i < nh && nb < 7; i += 2) { char h[3] = {cmd[i], cmd[i + 1], 0}; q[nb++] = (uint8_t)strtol(h, NULL, 16); }
  int n = c3Ask(q, nb, o, sizeof(o), max<uint32_t>(timeoutMs, 1500));
  if (n <= 0) { strncpy(out, "NODATA", cap); return 0; }
  int k = 0;
  for (int i = 0; i < n && k + 2 < (int)cap; i++) k += snprintf(out + k, cap - k, "%02X", o[i]);
  return k;
}
bool linkUp() { return c3Link; }
// ค่าล่าสุดจาก C3 -> ตัวแปรของจอ
static void c3Apply() {
  static uint32_t applied = 0;
  if (c3Seq == applied) return;
  applied = c3Seq;
  DidiNowGw m;
  portENTER_CRITICAL(&c3Mux); memcpy(&m, &c3Pkt, sizeof(m)); portEXIT_CRITICAL(&c3Mux);
  if (m.link != DN_LINK_OK && m.link != DN_LINK_DEMO) return;
  rpm = (int)m.rpm;
  speedKmh = min((int)lroundf(m.spd), MAX_SPEED);
  coolant = (int)lroundf(m.ect); metricVal[M_COOLANT] = coolant; metricHave[M_COOLANT] = true;
  iat = (int)lroundf(m.iat); metricVal[M_IAT] = iat; metricHave[M_IAT] = true;
  if (m.volt > 1) { metricVal[M_BATT] = m.volt; metricHave[M_BATT] = true; }
  metricVal[M_LOAD] = m.load; metricHave[M_LOAD] = true;
  metricVal[M_TPS] = m.tps; metricHave[M_TPS] = true;
  if (m.map > 0) { metricVal[M_MAP] = m.map; metricHave[M_MAP] = true; }
  if (m.maf > 0) { metricVal[M_MAF] = m.maf; metricHave[M_MAF] = true; mafSample(); }
  metricVal[M_STFT] = m.stft; metricHave[M_STFT] = true;
  if ((m.afrMode == 1 || m.afrMode == 3) && m.afr > 5) {       // C3 คิด AFR ด้วย 14.7 -> แปลงกลับเป็น lambda แล้วใช้สเกลของจอนี้
    float lam = m.afr / 14.7f;
    if (lam > 0.5f && lam < lamMax()) { afrLambda = lam; metricVal[M_AFR] = lam * afrSt10 / 10.0f + afrOfs10 / 10.0f; metricHave[M_AFR] = true; }
  }
  if (m.timing > -100) { metricVal[M_TIMING] = m.timing; metricHave[M_TIMING] = true; }
  if (m.oil > -100) { metricVal[M_OIL] = m.oil; metricHave[M_OIL] = true; }
  obdSample();
}
// งานอ่านค่าแบบ C3: ต่อ BLE + รับค่า (เรียกจาก obdTask)
static void c3Tick() {
  static uint32_t lastTry = 0;
  if (!c3Link) {
    obdOk = false; setStatus("C3...");
    if (!lastTry || millis() - lastTry > 1500) { lastTry = millis(); if (c3Connect()) setStatus("C3 BLE"); }
    else vTaskDelay(50 / portTICK_PERIOD_MS);
    return;
  }
  bool fresh = c3Last && millis() - c3Last < 1500;
  bool car = fresh && (c3Pkt.link == DN_LINK_OK || c3Pkt.link == DN_LINK_DEMO);
  if (car != obdOk) {
    obdOk = car;
    if (car) { for (int g = 0; g < 3; g++) { pidMask[g] = 0xFFFFFFFF; pidMaskKnown[g] = false; } }   // ค่าทุกตัวมากับ C3 แล้ว
    setStatus(car ? (c3Pkt.link == DN_LINK_DEMO ? "C3 DEMO" : "C3 OK") : (fresh ? "C3: NO CAR" : "C3 BLE"));
  }
  c3Apply();
  vTaskDelay(10 / portTICK_PERIOD_MS);
}
#else
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
bool linkUp() { return SerialBT.connected(); }
#endif

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

// AFR: เซนเซอร์ A/F (ไวด์แบนด์ตัวหน้า) ของรถ  0134 = แบบกระแส (Honda ส่วนใหญ่), 0124 = แบบแรงดัน, 0144 = ค่าที่ ECU สั่ง (สำรอง)
// ทั้งสามแบบ 2 ไบต์แรก = lambda = (256A+B) * 2 / 65536  -> AFR = lambda x stoich + ชดเชย (ตั้งในเมนู AFR SCALE)
int afrPid() {
  if (pidSupported(0x34)) return 0x34;
  if (pidSupported(0x24)) return 0x24;
  if (pidSupported(0x44)) return 0x44;
  return 0;
}
bool metricAvailable(int m) {
  switch (m) {
    case M_LOAD: return pidSupported(0x04);
    case M_TPS:  return pidSupported(tpsPid());
    case M_MAP:  return pidSupported(0x0B);
    case M_MAF:  return pidSupported(0x10);
    case M_AFR: return afrPid() != 0;
    case M_STFT: return pidSupported(0x06);
    case Q_ATF:  return atfHave;
    case M_OIL:  return pidSupported(0x5C);              // อุณหภูมิน้ำมันเครื่อง (บางรุ่นมี)
    case M_TIMING: return pidSupported(0x0E);            // องศาจุดระเบิด
    case Q_FUELI: case Q_FUELA: return pidSupported(0x10);   // คำนวณจาก MAF
    case Q_C1: case Q_C2: case Q_C3: case Q_C4: return cDef[m - Q_C1];                         // มีค่าก็ต่อเมื่อแอปส่งมา (WiFi)
    default:     return true;                            // น้ำ/IAT/แบต ใช้ได้ทุกคัน
  }
}

// ---- อ่านรอบ + ความเร็ว + TPS ในคำสั่งเดียว (OBD-II CAN ขอได้หลาย PID ต่อครั้ง รถตอบกลับเป็นหลายเฟรม ELM327 ประกอบให้) ----
int  fastMode = 0;            // 0 = อ่านทีละ PID (เดิม), 1 = หลาย PID + บอก ELM ว่าคาดว่าตอบ 1 ชุด (เร็วสุด), 2 = หลาย PID ธรรมดา
bool fastTps  = false;        // รถรองรับ TPS (PID 11) -> ขอรวมไปด้วย
bool fastExtra = false;       // ขอ LOAD(04) / MAP(0B) / AFR(34/24/44) รวมไปในคำสั่งเดียวกับรอบด้วย (ค่าวิ่งตามคันเร่งทันที)
bool fastHas[N_METRICS];      // ค่าไหนมากับคำสั่งเร็วแล้ว (ไม่ต้องอ่านแยกในรอบวน)
// ค่าที่เปลี่ยนเร็วตามเท้า: อ่านถี่ ค่าอื่น (น้ำ/IAT/แบต/น้ำมัน) เปลี่ยนช้า อ่านห่างได้
static inline bool isFastMetric(int m) { return m == M_TPS || m == M_LOAD || m == M_MAP || m == M_AFR; }   // MAF ของ Honda เป็นค่าคำนวณ อ่านช้าได้
static int hx2(const char *s) {
  if (!isxdigit((unsigned char)s[0]) || !isxdigit((unsigned char)s[1])) return -1;
  char h[3] = {s[0], s[1], 0};
  return (int)strtol(h, NULL, 16);
}
bool readFast(int *rp, int *sp, float *tp) {
  char r[160], c[160];
  char cmd[20] = "010C0D";                               // ELM327 ขอได้สูงสุด 6 PID ต่อครั้ง (CAN): 0C 0D 11 04 0B + AFR(34)
  if (fastTps) strcat(cmd, engDiesel ? "49" : "11");
  if (fastExtra) {
    if (pidSupported(0x04)) strcat(cmd, "04");
    if (pidSupported(0x0B)) strcat(cmd, "0B");
    int ap = afrPid();
    if (ap) { char h[3]; snprintf(h, sizeof(h), "%02X", ap); strcat(cmd, h); }
  }
  if (fastMode == 1) strcat(cmd, "1");
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
    else if ((pid == 0x11 || pid == 0x49) && i + 2 <= L) { int a = hx2(p + i); if (a < 0) break; *tp = a * 100.0f / 255.0f; i += 2; }
    else if (pid == 0x04 && i + 2 <= L) { int a = hx2(p + i); if (a < 0) break; metricVal[M_LOAD] = a * 100.0f / 255.0f; metricHave[M_LOAD] = true; i += 2; }
    else if (pid == 0x0B && i + 2 <= L) { int a = hx2(p + i); if (a < 0) break; metricVal[M_MAP] = a; metricHave[M_MAP] = true; i += 2; }
    else if ((pid == 0x34 || pid == 0x24 || pid == 0x44) && i + (pid == 0x44 ? 4 : 8) <= L) {   // AFR: lambda = 2 ไบต์แรก (34/24 มีอีก 2 ไบต์ต่อท้าย)
      int a = hx2(p + i), b = hx2(p + i + 2); if (a < 0 || b < 0) break;
      float lam = (a * 256 + b) * 2.0f / 65536.0f;
      if (lam > 0.5f && lam < lamMax()) { afrLambda = lam; metricVal[M_AFR] = lam * afrSt10 / 10.0f + afrOfs10 / 10.0f; metricHave[M_AFR] = true; }
      i += (pid == 0x44) ? 4 : 8;
    }
    else break;
  }
  return gotRpm;
}

// ---- อัตราสิ้นเปลือง: น้ำมัน (ลิตร/ชม.) = MAF g/s x 3600 / (AFR stoich x ความหนาแน่น g/L)  km/L = ความเร็ว / ลิตรต่อชม. ----
//  เฉลี่ย: นับเฉพาะช่วงที่อ่าน MAF ได้ต่อเนื่อง (เว้นช่วงห่างเกิน 10 วินาที)  รีเซ็ตเมื่อเปิดจอใหม่
volatile float fuelLph = 0; volatile bool fuelHave = false;
volatile double fuelUsedL = 0, fuelDistKm = 0; volatile uint32_t fuelMs = 0, mafMs = 0;
void mafSample() {
  uint32_t now = millis();
  float maf = metricVal[M_MAF];
  float dens = engDiesel ? 832.0f : 745.0f;                 // g/L
  fuelLph = maf * 3600.0f / (afrSt10 / 10.0f * dens);
  fuelHave = true;
  if (mafMs && now - mafMs < 10000) {
    float dt = (now - mafMs) / 1000.0f;
    fuelUsedL += fuelLph * dt / 3600.0f;
    fuelDistKm += speedKmh * dt / 3600.0f;
  }
  mafMs = now; fuelMs = now;
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
      if (engDiesel ? readPid("0149", "4149", 1, b) : readPid("0111", "4111", 1, b)) { metricVal[m] = b[0] * 100.0f / 255.0f; metricHave[m] = true; return true; }
      break;
    case M_MAP:                                          // 010B: A kPa (สัมบูรณ์ ~101 ที่ความดันบรรยากาศ)
      if (readPid("010B", "410B", 1, b)) { metricVal[m] = b[0]; metricHave[m] = true; return true; }
      break;
    case M_MAF:                                          // 0110: (256A+B)/100 g/s
      if (readPid("0110", "4110", 2, b)) { metricVal[m] = (b[0] * 256 + b[1]) / 100.0f; metricHave[m] = true; mafSample(); return true; }
      break;
    case M_AFR: {                                        // AFR จาก lambda ของเซนเซอร์ A/F ตัวหน้า
      int pid = afrPid();
      if (!pid) break;
      char cmd[6], head[6];
      snprintf(cmd, sizeof(cmd), "01%02X", pid); snprintf(head, sizeof(head), "41%02X", pid);
      if (readPid(cmd, head, 2, b)) {
        float lam = (b[0] * 256 + b[1]) * 2.0f / 65536.0f;
        if (lam > 0.5f && lam < lamMax()) { afrLambda = lam; metricVal[m] = lam * afrSt10 / 10.0f + afrOfs10 / 10.0f; metricHave[m] = true; return true; }   // ตัดค่าเพี้ยน/ตอนตัดน้ำมัน (lambda สูงมาก)
      }
      break;
    }
    case M_STFT:                                         // 0106: (A-128)*100/128 % short term fuel trim (bank 1)
      if (readPid("0106", "4106", 1, b)) { metricVal[m] = (b[0] - 128) * 100.0f / 128.0f; metricHave[m] = true; return true; }
      break;
    case M_OIL:                                          // 015C: A-40 C
      if (readPid("015C", "415C", 1, b)) { metricVal[m] = b[0] - 40; metricHave[m] = true; return true; }
      break;
    case M_TIMING:                                       // 010E: A/2-64 องศาก่อนศูนย์ตายบน
      if (readPid("010E", "410E", 1, b)) { metricVal[m] = b[0] / 2.0f - 64.0f; metricHave[m] = true; return true; }
      break;
  }
  return false;
}

#if !OBD_LINK_CAN && !OBD_LINK_C3
void applyPin() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  SerialBT.setPin(btPin, strlen(btPin));
#else
  SerialBT.setPin(btPin);
#endif
}
#endif

bool connectOBD() {
#if OBD_LINK_C3
  return c3Link;
#elif OBD_LINK_CAN
  setStatus("CAN...");
  if (!canBegin()) { obdOk = false; setStatus("NO CAN"); return false; }
  { char r[40];
    if (elmBuf("0100", r, sizeof(r), 300) <= 0 || !strstr(r, "4100")) { obdOk = false; setStatus("NO ECU"); return false; } }
#else
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
#endif
  detectPids();
  fastTps = pidSupported(tpsPid());
  fastMode = 0;                                          // ทดสอบวิธีอ่านหลาย PID: ได้ก็ใช้ ไม่ได้ถอยกลับไปอ่านทีละตัว
  for (int ex = 1; ex >= 0 && !fastMode; ex--) {         // ลองแบบรวม LOAD/MAP/AFR ก่อน ไม่ได้ค่อยเหลือแค่ รอบ/ความเร็ว/TPS
    fastExtra = ex;
    for (int mdl = 1; mdl <= 2 && !fastMode; mdl++) {
      fastMode = mdl;
      int a = 0, b2 = 0; float c = 0; bool ok = false;
      for (int k = 0; k < 3 && !ok; k++) ok = readFast(&a, &b2, &c);
      if (!ok) fastMode = 0;
    }
  }
  for (int m = 0; m < N_METRICS; m++) fastHas[m] = false;
  if (fastMode) {
    fastHas[M_TPS] = fastTps;
    if (fastExtra) { fastHas[M_LOAD] = pidSupported(0x04); fastHas[M_MAP] = pidSupported(0x0B); fastHas[M_AFR] = afrPid() != 0; }
  }
  Serial.printf("fast multi-PID mode: %d (TPS %d, LOAD/MAP/AFR %d)\n", fastMode, fastTps, fastExtra);
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
  static const int8_t L[18][9] = {
    {M_COOLANT, M_IAT, M_MAP, M_MAF, M_LOAD, M_TPS, M_AFR, M_BATT, -1},   // หน้า 1
    {M_COOLANT, M_IAT, M_LOAD, M_MAF, M_AFR, M_BATT, M_MAP, -1, -1},       // หน้า 2
    {M_COOLANT, M_BATT, M_TPS, M_MAP, -1, -1, -1, -1, -1},                  // หน้า 3
    {M_COOLANT, M_MAP, M_BATT, -1, -1, -1, -1, -1, -1},                     // หน้า 4
    {M_COOLANT, M_MAP, M_TPS, M_LOAD, M_IAT, M_BATT, -1, -1, -1},           // หน้า 5
    {M_COOLANT, M_IAT, M_BATT, M_TPS, M_LOAD, M_AFR, -1, -1, -1},          // หน้า 6
    {M_COOLANT, M_IAT, M_MAP, M_MAF, M_LOAD, M_STFT, M_BATT, -1, -1},       // หน้า 7
    {M_COOLANT, M_IAT, M_BATT, -1, -1, -1, -1, -1, -1},                    // หน้า 8
    {M_COOLANT, -1, -1, -1, -1, -1, -1, -1, -1},                           // หน้า 9 (+ ค่าที่เลือกในช่องล่าง เพิ่มด้านล่าง)
    {M_COOLANT, -1, -1, -1, -1, -1, -1, -1, -1},                           // หน้า 10
    {M_COOLANT, -1, -1, -1, -1, -1, -1, -1, -1},                           // หน้า 11
    {M_COOLANT, -1, -1, -1, -1, -1, -1, -1, -1},                           // หน้า 12 (แบบหน้า 10)
    {M_COOLANT, -1, -1, -1, -1, -1, -1, -1, -1},                           // หน้า 13 (+ ค่าที่เลือกในช่องล่าง แบบหน้า 9)
    {M_COOLANT, M_IAT, M_BATT, -1, -1, -1, -1, -1, -1},                     // PG_Q: GIF ในกรอบ + น้ำ/IAT/แบต
    {M_COOLANT, M_IAT, M_BATT, -1, -1, -1, -1, -1, -1},                     // PG_Q2
    {M_COOLANT, M_IAT, M_BATT, -1, -1, -1, -1, -1, -1},                     // PG_Q3
    {M_COOLANT, M_IAT, M_BATT, -1, -1, -1, -1, -1, -1},                     // PG_Q4
    {M_COOLANT, M_IAT, M_BATT, -1, -1, -1, -1, -1, -1}};                    // PG_Q5
  int n = 0;
  pg = constrain(pg, 0, N_PAGES - 1);
  for (int i = 0; i < 9 && L[pg][i] >= 0; i++) {
    int m = L[pg][i];
    if (!metricAvailable(m) || (fastMode && fastHas[m])) continue;   // ค่านี้มาพร้อมรอบ/ความเร็วแล้ว
    out[n++] = m;
  }
  if (isQPage(pg)) {                                        // หน้า GIF ในกรอบ: ค่าที่เลือกไว้ใน 3 ช่องขวา (แยกกันแต่ละหน้า)
    extern volatile int qSlots[Q_NP][3];
    volatile int *qSlot = qSlots[qPageIdx(pg)];
    n = 0; out[n++] = M_COOLANT;                             // น้ำอ่านเสมอ (เสียง/สีเตือนความร้อน)
    extern int qLay[5];
    if (qLay[qPageIdx(pg)] == 1) return n;                  // แบบ B ไม่มีช่องค่า: อ่านแค่น้ำ รอบเครื่องจะได้ถี่ขึ้น
    for (int s = 0; s < 3; s++) { int m = qSlot[s]; if (m == Q_FUELI || m == Q_FUELA) m = M_MAF;
      bool dup = false; for (int i = 0; i < n; i++) if (out[i] == m) dup = true;
      if (!dup && m < N_METRICS && m != M_COOLANT && metricAvailable(m) && !(fastMode && fastHas[m])) out[n++] = m; }
  }
  if (isHPage(pg)) for (int s = 0; s < 2; s++) {           // หน้า 9 / 13: ค่าที่เลือกไว้ในช่องล่างซ้าย/ขวา
    extern volatile int slotMetric[2];
    int m = slotMetric[s]; bool dup = false;
    for (int i = 0; i < n; i++) if (out[i] == m) dup = true;
    if (!dup && metricAvailable(m) && !(fastMode && fastHas[m])) out[n++] = m;
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
      if (++fastFail >= 12) {
        fastFail = 0;
        if (fastExtra) { fastExtra = false; fastHas[M_LOAD] = fastHas[M_MAP] = fastHas[M_AFR] = false; Serial.println("multi-PID: drop LOAD/MAP/AFR"); }
        else { fastMode = 0; fastHas[M_TPS] = false; Serial.println("multi-PID failed -> single PID mode"); }
      }
    }
  } else {
  if (readPid("010C", "410C", 2, b)) { rpm = ((b[0] * 256) + b[1]) / 4; any = true; rpmFail = 0; obdSample(); }
  else if (++rpmFail >= 5 && rpm) { rpm = 0; obdSample(); }              // อ่านพลาดครั้งสองครั้งไม่ใช่เครื่องดับ: ค้างค่าเดิมไว้ (กันรอบหายวูบ) พลาดติดกัน 5 ครั้งค่อยเป็น 0
  if ((cyc++ & 1) == 0 && readPid("010D", "410D", 1, b)) { speedKmh = min(b[0], MAX_SPEED); any = true; }   // ความเร็วอ่านสลับรอบ = รอบเครื่องอัปเดตถี่ขึ้น
  if (readPid("010C", "410C", 2, b)) { rpm = ((b[0] * 256) + b[1]) / 4; any = true; rpmFail = 0; obdSample(); }
  }

  // ค่ารอง แบ่ง 2 กลุ่ม (อ่านเฉพาะค่าที่หน้าปัจจุบันใช้):
  //  - กลุ่มเร็ว TPS/LOAD/MAP/MAF: วิ่งตามคันเร่ง -> อ่านแทรกทุกรอบของการอ่านรอบเครื่อง (ถ้าไม่ได้มากับคำสั่งเร็วแล้ว)
  //  - กลุ่มช้า น้ำ/IAT/แบต/น้ำมัน/ฟิวทริม: เปลี่ยนช้า -> อ่านวนทีละค่าทุก ~500 ms
  static uint32_t lastSlow = 0;
  static int rrFast = 0, rrSlow = 0;
  int u = urgentMetric;
  if (u >= 0) {
    urgentMetric = -1;
    if (pollMetric(u)) any = true;
  } else {
    int need[N_METRICS + 2], cnt = pageMetrics(page, need);
    int fl[N_METRICS], nf = 0, sl[N_METRICS + 2], ns = 0;
    for (int i = 0; i < cnt; i++) { if (isFastMetric(need[i])) fl[nf++] = need[i]; else sl[ns++] = need[i]; }
    if (nf > 0 && pollMetric(fl[rrFast++ % nf])) any = true;
    if (ns > 0 && millis() - lastSlow > 500) {
      lastSlow = millis();
      if (pollMetric(sl[rrSlow++ % ns])) any = true;
    }
  }

  if (any) failCount = 0; else failCount++;
  if (failCount > 15) {
    failCount = 0;
#if OBD_LINK_CAN
    obdOk = false; setStatus("NO ECU");                       // ECU ไม่ตอบติดกัน -> กลับไปรอ/ต่อใหม่
#else
    if (!SerialBT.connected()) { obdOk = false; setStatus("NO OBD"); }
#endif
  }
}

#if !OBD_LINK_CAN && LINK_WIFI
// ---- โหมด WiFi: รับแพ็กเก็ตจากแอป  "OBD,rpm,kmh,coolant,iat,batt,load,tps,map,maf,lambda,stft,cvtTemp,oil,timing,c1,c2,c3,c4"
//  + ทุก ~2 วินาที "CPN,<ชื่อ>;<หน่วย>;<ต่ำ>;<สูง>;<ทศนิยม>,..." x4 (ช่องว่าง = ไม่ได้ตั้ง)  (ช่องว่าง = ไม่มีค่า) ----
int  linkMode = 0;                         // 0 AUTO (WiFi ก่อน แล้ว Bluetooth), 1 BT ONLY, 2 WIFI ONLY  (เมนู BLUETOOTH -> MODE)
volatile bool wifiPhase = false;           // true = ตอนนี้ใช้ WiFi (Bluetooth ยังไม่เปิด)
WiFiUDP wUdp;
static void wifiApply(char *p) {
  if (strncmp(p, "OBD,", 4)) return;
  char *f[18]; int n = 0;
  for (char *t = p + 4; n < 18; ) { f[n++] = t; char *c = strchr(t, ','); if (!c) break; *c = 0; t = c + 1; }
  auto has = [&](int i) { return i < n && f[i][0] && !(f[i][0] == '-' && f[i][1] == 0); };   // ว่าง หรือ "-" = ไม่มีค่า
  if (has(0)) { rpm = atoi(f[0]); }
  if (has(1)) speedKmh = min(atoi(f[1]), MAX_SPEED);
  if (has(2)) { coolant = atoi(f[2]); metricVal[M_COOLANT] = coolant; metricHave[M_COOLANT] = true; }
  if (has(3)) { iat = atoi(f[3]); metricVal[M_IAT] = iat; metricHave[M_IAT] = true; }
  const int MI[6] = {M_BATT, M_LOAD, M_TPS, M_MAP, M_MAF, -1};
  for (int k = 0; k < 5; k++) if (has(4 + k)) { metricVal[MI[k]] = atof(f[4 + k]); metricHave[MI[k]] = true; }
  if (has(8)) mafSample();
  if (has(9)) { float lam = atof(f[9]); if (lam > 0.5f && lam < lamMax()) { afrLambda = lam; metricVal[M_AFR] = lam * afrSt10 / 10.0f + afrOfs10 / 10.0f; metricHave[M_AFR] = true; } }
  if (has(10)) { metricVal[M_STFT] = atof(f[10]); metricHave[M_STFT] = true; }
  if (has(11)) { atfTemp = atof(f[11]); atfHave = true; atfMs = millis(); }   // CVT TEMP (ทดลอง)
  if (has(12)) { metricVal[M_OIL] = atof(f[12]); metricHave[M_OIL] = true; }
  if (has(13)) { metricVal[M_TIMING] = atof(f[13]); metricHave[M_TIMING] = true; }
  for (int k = 0; k < N_CUST; k++) if (has(14 + k)) { custVal[k] = atof(f[14 + k]); cMs[k] = millis(); }
  obdSample();
}
static void wifiCustom(char *p) {                           // ชื่อ/หน่วย/ช่วงของ Custom PID จากแอป
  char *t = p + 4; bool chg = false;
  for (int k = 0; k < N_CUST; k++) {
    char *e = strchr(t, ','); if (e) *e = 0;
    char *fld[5] = {0}; int nf = 0;
    for (char *u = t; nf < 5; ) { fld[nf++] = u; char *c = strchr(u, ';'); if (!c) break; *c = 0; u = c + 1; }
    bool def = fld[0] && fld[0][0];
    char lab[12] = "", un[6] = "";
    if (def) { strncpy(lab, fld[0], 11); if (nf > 1) strncpy(un, fld[1], 5); for (char *c = lab; *c; c++) *c = toupper(*c); }
    float lo = nf > 2 ? atof(fld[2]) : 0, hi = nf > 3 ? atof(fld[3]) : 100; int dc = nf > 4 ? constrain(atoi(fld[4]), 0, 2) : 1;
    if (hi <= lo) hi = lo + 1;
    if (def != cDef[k] || (def && (strcmp(lab, cLab[k]) || strcmp(un, cUnit[k]) || lo != cLo[k] || hi != cHi[k] || dc != cDec[k]))) {
      cDef[k] = def; if (def) { strcpy(cLab[k], lab); strcpy(cUnit[k], un); cLo[k] = lo; cHi[k] = hi; cDec[k] = dc; }
      chg = true;
    }
    if (!e) break;
    t = e + 1;
  }
  if (chg) cGen++;
}
const char *const W_SSID[3] = {WIFI_SSID, WIFI_SSID2, WIFI_SSID3};
const char *const W_PASS[3] = {WIFI_PASS, WIFI_PASS2, WIFI_PASS3};
String wifiCur = "";                                         // ชื่อฮอตสปอตที่ต่ออยู่ (โชว์ในเมนู)
static bool wifiPick() {                                     // สแกนแล้วต่อฮอตสปอตที่รู้จักและสัญญาณแรงสุด
  int n = WiFi.scanNetworks(false, false);
  int best = -1, bestRssi = -999;
  for (int i = 0; i < n; i++)
    for (int k = 0; k < 3; k++)
      if (W_SSID[k][0] && WiFi.SSID(i).equalsIgnoreCase(W_SSID[k]) && WiFi.RSSI(i) > bestRssi) { best = k; bestRssi = WiFi.RSSI(i); }   // ไม่สนตัวพิมพ์เล็ก/ใหญ่
  if (best < 0) {                                            // ไม่เจอ: พิมพ์ชื่อที่เห็นทั้งหมดไว้เช็ก (จอเห็นแค่ WiFi 2.4 GHz)
    Serial.printf("WiFi: none of the known hotspots found (%d networks):\n", n);
    for (int i = 0; i < n && i < 40; i++) Serial.printf("   %3d dBm  ch%-2d  \"%s\"\n", WiFi.RSSI(i), WiFi.channel(i), WiFi.SSID(i).c_str());
    WiFi.scanDelete();
    return false;
  }
  WiFi.scanDelete();
  wifiCur = W_SSID[best];
  Serial.printf("WiFi: connecting to %s (%d dBm)\n", W_SSID[best], bestRssi);
  WiFi.begin(W_SSID[best], W_PASS[best]);
  return true;
}
// คืน true = ยังอยู่โหมด WiFi, false = เปลี่ยนไป Bluetooth แล้ว
static bool wifiTick() {
  static bool started = false, got = false, joining = false; static uint32_t t0 = 0, lastPkt = 0, lastBeacon = 0, lastScan = 0;
  uint32_t now = millis();
  if (!started) {
    started = true; t0 = now;
    WiFi.mode(WIFI_STA); WiFi.setSleep(false);                 // ไม่ประหยัดไฟ = ค่าไม่มาเป็นชุด ๆ
    wUdp.begin(WIFI_UDP_PORT);
    setStatus("WIFI...");
  }
  if (WiFi.status() != WL_CONNECTED && (!joining || now - lastScan > 8000) && now - lastScan > 4000) {   // ยังไม่ต่อ: สแกนหาใหม่เป็นระยะ
    lastScan = now;
    WiFi.disconnect();
    joining = wifiPick();
    now = millis();
  }
  char buf[256];
  int sz;
  while ((sz = wUdp.parsePacket()) > 0) {
    int len = wUdp.read(buf, sizeof(buf) - 1); if (len < 0) len = 0; buf[len] = 0;
    if (!strncmp(buf, "CPN,", 4)) { wifiCustom(buf); continue; }
    if (!strncmp(buf, "OBD,", 4)) { wifiApply(buf); got = true; lastPkt = now; if (!obdOk) Serial.println("WiFi: data from app"); obdOk = true; setStatus("WIFI OK"); }
  }
  if (WiFi.status() == WL_CONNECTED && now - lastBeacon > 1000) {   // ประกาศตัวให้แอปรู้ IP
    lastBeacon = now;
    IPAddress bc = WiFi.localIP(); bc[3] = 255;
    wUdp.beginPacket(bc, WIFI_BEACON_PORT); wUdp.print("CYD-OBD"); wUdp.endPacket();
  }
  if (got && now - lastPkt > 3000 && obdOk) { obdOk = false; setStatus(WiFi.status() == WL_CONNECTED ? "NO APP" : "WIFI..."); }   // ค่าหยุดมา: ค้างค่าเดิมไว้ก่อน
  if (!obdOk) {                                              // บอกว่าติดตรงไหน: ยังไม่ต่อฮอตสปอต / ต่อแล้วแต่แอปยังไม่ส่งค่า
    static bool lastConn = false;
    bool conn = WiFi.status() == WL_CONNECTED;
    if (conn != lastConn) {
      lastConn = conn;
      setStatus(conn ? "NO APP" : "WIFI...");
      if (conn) Serial.printf("WiFi: connected, IP %s - waiting for app data on UDP %d\n", WiFi.localIP().toString().c_str(), WIFI_UDP_PORT);
    }
  }
  bool giveUp = linkMode == 0 && ((!got && now - t0 > WIFI_FIRST_MS) || (got && now - lastPkt > WIFI_LOST_MS));
  if (!giveUp) return true;
  Serial.println("WiFi: no data -> Bluetooth");                 // ปิด WiFi คืนแรม แล้วเปิด Bluetooth หา Vgate แบบเดิม
  wUdp.stop(); WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
  vTaskDelay(300 / portTICK_PERIOD_MS);
  obdOk = false; setStatus("BT...");
  SerialBT.begin("ESP32-OBD", true);
  wifiPhase = false;
  return false;
}
#endif

void obdTask(void *) {
  uint32_t lastTry = 0, retryMs = 8000;
  for (;;) {
    if (obdPause) { obdIdle = true; vTaskDelay(50 / portTICK_PERIOD_MS); continue; }
    obdIdle = false;
#if !OBD_LINK_CAN && LINK_WIFI
    if (wifiPhase) { if (wifiTick()) { vTaskDelay(5 / portTICK_PERIOD_MS); continue; } lastTry = 0; }
#endif
    if (btReconnect) { btReconnect = false; lastTry = 0; retryMs = 8000; }
#if OBD_LINK_C3
    c3Tick();
    (void)lastTry; (void)retryMs;
#elif OBD_LINK_CAN
    {                                                     // สถานะสาย CAN ทุก 5 วินาที (ดูใน Serial Monitor)
      static uint32_t lastLog = 0, lastCnt = 0;
      if (millis() - lastLog > 5000) {
        lastLog = millis();
        Serial.printf("CAN: %u frames/5s, ECU %s, idle %u ms\n", (unsigned)(canRxCount - lastCnt), obdOk ? "OK" : "--", (unsigned)(millis() - canLastRx));
        lastCnt = canRxCount;
      }
      uint32_t idle = millis() - max((uint32_t)canLastRx, max((uint32_t)canEcuMs, (uint32_t)canBootMs));
      if (CAN_SLEEP_IDLE_S && !obdOk && !demoMode && idle > CAN_SLEEP_IDLE_S * 1000UL) canSleepReq = true;   // สายเงียบนาน -> ให้ loop สั่งหลับ
    }
    if (!obdOk) {
      if (lastTry == 0 || millis() - lastTry > 2000) { lastTry = millis(); connectOBD(); }
      else vTaskDelay(50 / portTICK_PERIOD_MS);
      { twai_message_t m; if (canUp) while (twai_receive(&m, 0) == ESP_OK) { canLastRx = millis(); canRxCount++; } }   // ฟังสายระหว่างรอ
    } else {
      pollOBD();
    }
#else
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
#endif
    vTaskDelay(1);
  }
}

void pauseOBD() {
  obdPause = true;
  uint32_t t = millis();
  while (!obdIdle && millis() - t < 20000) delay(20);
#if !OBD_LINK_CAN
  if (SerialBT.connected()) SerialBT.disconnect();
#endif
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
void qColors();
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
  qColors();
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
#if HAS_ANIME
#define POOL_FULL      49792     // หน้า 9: พื้นหลังใต้ตัวเลข 46 KB + แถบสไปรท์วาดตัวเลข 3 KB
#elif HAS_CARPAGE || HAS_PHOTO2 || HAS_CARPAGE2 || HAS_CARPAGE3
#define POOL_FULL      36416     // ไม่มีหน้า 9: หน้ารถวิ่ง = สไปรท์ตัวเลข 13.1 KB + พื้นหลังใต้ตัวเลข 22.4 KB
#else
#define POOL_FULL      13440     // มีแต่หน้า 1-8 (กราฟิกล้วน): สไปรท์วาดตัวเลข 112x60 อย่างเดียว
#endif
#define POOL_LITE      POOL_FULL // (เท่ากับ FULL)
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
  if (pSprOk || isHPage(page)) return;                      // หน้า 9 / 13 ไม่ใช้สไปรท์นี้ (ใช้ก้อนกลางทั้งก้อน)
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
    else easeTo(pd[m], v, dt, isFastMetric(m) ? 0.10f : 0.4f);   // ค่าตามคันเร่งตามทันไว ค่าอุณหภูมิ/แบตนุ่ม
  }
}
int gScaleMax() { return constrain((maxRpm + 999) / 1000 + 2, 6, 12); }   // ตัวเลขสูงสุดบนหน้าปัด (x1000 rpm): redline ปัดขึ้น + 2 (redline 5000 = หน้าปัดถึง 7000)

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
    pStatusText(6, 3, false, msg, over ? C_RED : (statusOk(msg) ? C_OK : C_WARN), P_BG, 70);
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
  lcd.setCursor(8, 116); lcd.print("AFR");
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

  // TRIP / AFR
  float trip = demoMode ? demoTrip : tripKm;
  snprintf(b, sizeof(b), "%.1f", trip);
  if (chgTxt(1, b, P_WHT)) drawVal(8, 74, 92, 34, b, &fonts::FreeSansBold18pt7b, 1.0f, P_WHT, P_TILE4, false, 31);
  bool hf = metricHave[M_AFR];
  if (hf) snprintf(b, sizeof(b), "%.1f", pd[M_AFR]); else strcpy(b, "--");
  if (chgTxt(2, b, P_WHT)) drawVal(8, 130, 92, 28, b, &fonts::FreeSansBold18pt7b, 1.0f, P_WHT, P_TILE4, false, 26);
  int fl = hf ? (int)lroundf(constrain((pd[M_AFR] - 10.0f) / 10.0f, 0.0f, 1.0f) * 10.0f) : 0;   // แถบ AFR 10-20
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
    pStatusText(318, 29, true, msg, over ? C_RED : (statusOk(msg) ? C_OK : C_WARN), P_BG4, 54);
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
  vals[M_MAF] = 3.0f + thr * 1.6f;  vals[M_AFR] = thr > 60.0f ? 12.6f : 14.7f + sinf(t * 3.0f) * 0.3f;
  vals[M_STFT] = -1.6f + 2.2f * sinf(t / 2.3f);
  vals[M_OIL] = coolant + 6; vals[M_TIMING] = thr > 60.0f ? 22.0f : 12.0f + thr * 0.2f;
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
  pStatusText(x, y, right, msg, over ? C_RED : (statusOk(msg) ? C_OK : C_WARN), bg, pad);
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
const char *const A_LAB[8] = {"WATER", "AIR.T", "BOOST.P", "AIR.FLOW", "CALC.LOAD", "THRO.P", "AFR", "BAT"};
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
  fmtV(b, metricHave[M_AFR], pd[M_AFR], 1);       aTile(6, b, C_TXT);
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
  {4, 194, 114, 28, "AFR"}, {202, 80, 114, 54, "r/min"}, {202, 136, 56, 54, "BATT V"}, {260, 136, 56, 54, "BOOST"},
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
  fmtV(b, metricHave[M_AFR], pd[M_AFR], 1);  bTile(4, b, TFT_BLACK, 1.0f);
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
//  แถบรอบส้มด้านบน + ค่าซ้าย/ขวา 4+4 ช่อง + รอบตรงกลาง + "Race"  (ไม่มีไฟ shift, มีเกียร์ตรงกลาง)
uint16_t E_OR, E_ORD, E_LEDOFF, E_LEDRIM, E_LG, E_LY, E_BAROFF;
const int E_BX = 4, E_BY = 10, E_BW = 312, E_BH = 9;               // แถบรอบ (เต็มความกว้าง แทนที่แถวไฟ shift เดิม)
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
  contBarUpdate(dispRpm / (gScaleMax() * 1000.0f) * E_BW, E_BW, flash, E_BX, E_BY, E_BH, eBarSpan, eColAt);

  fmtRpm(b);
  static uint32_t lastR = 0;
  uint16_t rc = flash ? C_RED : E_OR;
  if (now - lastR >= 80 || rc != pCol[0]) { lastR = now; valBox(0, b, rc, 106, 34, 80, 30, &fonts::Font7, 0.6f, TFT_BLACK, false, 29); }
  b[0] = gearChar(); b[1] = 0;                                     // เกียร์ตัวใหญ่ตรงกลาง (แบบเดิม)
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
const uint8_t *hLoaded = nullptr;                            // ภาพที่ถอดลงแรมอยู่ตอนนี้
void hSelectImg() {
#if HAS_ANIME2
  if (page == PG_K) { hJpg = anime2_jpg; hJpgLen = anime2_jpg_len; return; }
#endif
#if HAS_ANIME
  hJpg = anime_jpg; hJpgLen = anime_jpg_len;
#endif
}
volatile int slotMetric[2] = {M_COOLANT, M_IAT};          // ช่องล่างซ้าย / ขวา แสดงค่าอะไร (จำไว้ใน NVS)
const int H_BAR_W = 164, H_BAR_H = 15;
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
  sRpm.alloc(38, 16, 130, 49, false);
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
  if (!lcd.drawJpg(hJpg, hJpgLen, 0, 0)) lcd.fillScreen(C_SKY);
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
    else hTxt(c, msg, 70, 0, statusOk(msg) ? C_OK : C_WARN, true);
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
    else if (m == M_BATT || m == M_STFT || m == M_AFR || (m == M_MAF && hDispSlot[sl] < 100.0f)) snprintf(buf, sizeof(buf), "%.1f", hDispSlot[sl]);
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
  uint16_t sc = o2 ? C_RED : (statusOk(msg) ? C_OK : C_WARN);
  if (chgTxt(2, msg, sc)) fBoxDraw(2, nullptr, msg, nullptr, sc);
}
#else
void fFree() {}
void fStatic() { lcd.fillScreen(TFT_BLACK); }
void updateF(uint32_t, bool) {}
#endif

// =============================== หน้า 9 (PG_Q) : GIF ในกรอบ + ตัวเลขแบบไตเติลอนิเมะ (ไล่สีชมพู->ขาว ขอบขาว+ม่วง เงาเยื้อง) ===============================
//  ซ้าย: กรอบภาพเคลื่อนไหว (gifbox.h) / แถบรอบเฉียง / ความเร็ว / รอบ    ขวา: COOLANT / IAT / VOLT + แถบเฉียง
#if __has_include("gifbox.h")
#include "gifbox.h"             // หน้า 9: สร้างด้วย tools/make_gif_box.py (ไม่มีไฟล์ = กรอบว่าง)
#define HAS_GIFBOX 1
#else
#define HAS_GIFBOX 0
#endif
#if __has_include("gifbox2.h")
#include "gifbox2.h"            // หน้า 10: make_gif_box.py ภาพ.gif . --suffix 2
#define HAS_GIFBOX2 1
#else
#define HAS_GIFBOX2 0
#endif
#if __has_include("gifbox3.h")
#include "gifbox3.h"            // หน้า 11: make_gif_box.py ภาพ.gif . --suffix 3
#define HAS_GIFBOX3 1
#else
#define HAS_GIFBOX3 0
#endif
#if __has_include("gifbox4.h")
#include "gifbox4.h"            // หน้า 12: make_gif_box.py ภาพ.gif . --suffix 4
#define HAS_GIFBOX4 1
#else
#define HAS_GIFBOX4 0
#endif
#if __has_include("gifbox5.h")
#include "gifbox5.h"            // หน้า 13: make_gif_box.py ภาพ.gif . --suffix 5
#define HAS_GIFBOX5 1
#else
#define HAS_GIFBOX5 0
#endif
#if HAS_GIFBOX
#ifndef GBOX_HAS_DMS
const uint16_t *const GBOX_DMS = nullptr;
#endif
const QGif QG1 = {GBOX_JPG, GBOX_LEN, GBOX_DMS, GBOX_N, GBOX_MS, GBOX_W, GBOX_H};
#endif
#if HAS_GIFBOX2
const QGif QG2 = {GBOX2_JPG, GBOX2_LEN, GBOX2_DMS, GBOX2_N, GBOX2_MS, GBOX2_W, GBOX2_H};
#endif
#if HAS_GIFBOX3
const QGif QG3 = {GBOX3_JPG, GBOX3_LEN, GBOX3_DMS, GBOX3_N, GBOX3_MS, GBOX3_W, GBOX3_H};
#endif
#if HAS_GIFBOX4
const QGif QG4 = {GBOX4_JPG, GBOX4_LEN, GBOX4_DMS, GBOX4_N, GBOX4_MS, GBOX4_W, GBOX4_H};
#endif
#if HAS_GIFBOX5
const QGif QG5 = {GBOX5_JPG, GBOX5_LEN, GBOX5_DMS, GBOX5_N, GBOX5_MS, GBOX5_W, GBOX5_H};
#endif
const QGif *qGifOf(int p) {                                         // GIF ของหน้านั้น (nullptr = ไม่มีไฟล์)
  int i = qPageIdx(p);
#if HAS_GIFBOX
  if (i == 0) return &QG1;
#endif
#if HAS_GIFBOX2
  if (i == 1) return &QG2;
#endif
#if HAS_GIFBOX3
  if (i == 2) return &QG3;
#endif
#if HAS_GIFBOX4
  if (i == 3) return &QG4;
#endif
#if HAS_GIFBOX5
  if (i == 4) return &QG5;
#endif
  return nullptr;
}
const QGif *qG = nullptr;                                           // GIF ของหน้าที่แสดงอยู่
int qPi = 0;                                                        // หน้า GIF ที่แสดงอยู่ 0/1/2
const int Q_FX = 4, Q_FY = 16, Q_FW = 196, Q_FH = 108;             // กรอบภาพ (ภาพอยู่ด้านใน 188x100 ที่ 8,20)
const int Q_IX = 8, Q_IY = 20, Q_IW = 188, Q_IH = 100;
const int Q_RBX = 4, Q_RBY = 130, Q_RBW = 196, Q_RBH = 10, Q_RBN = 32;   // แถบรอบ (ช่องเฉียง 32 ช่อง)
const int Q_RX = 206, Q_RW = 110, Q_BLK = 74;                       // คอลัมน์ขวา: 3 ช่อง สูงช่องละ 74
const int Q_MBN = 17;                                               // แถบค่าทางขวา 17 ช่อง
uint16_t Q_PK, Q_WH, Q_OUT, Q_SHD, Q_OFF, Q_LAB, Q_RD, Q_RDOFF, Q_VIO;
// สีตัวเลขของแต่ละหน้า GIF (กดค้างที่ตัวเลขความเร็ว/รอบ เพื่อเลือกสี) : สี (0-359) + ความสด (0-100)
#define QC_HUE_DEF 322
#define QC_SAT_DEF 64
// รูปแบบหน้า GIF: 0 = A (กรอบภาพ + ค่า 3 ช่องขวา + แถบ), 1 = B (ภาพใหญ่ด้านบน + รอบ/ความเร็วด้านล่าง ไม่มีแถบ)
int qLay[5] = {0, 0, 0, 0, 0};
const int QB_FX = 2, QB_FY = 2, QB_FW = 316, QB_FH = 172, QB_IX = 6, QB_IY = 6, QB_IW = 308, QB_IH = 164;
int qHue[5] = {QC_HUE_DEF, QC_HUE_DEF, QC_HUE_DEF, QC_HUE_DEF, QC_HUE_DEF}, qSat[5] = {QC_SAT_DEF, QC_SAT_DEF, QC_SAT_DEF, QC_SAT_DEF, QC_SAT_DEF};
uint16_t qTop, qOut, qShd, qFIn;                                    // สีตัวเลขของหน้าที่แสดงอยู่: เนื้อด้านบน / ขอบ / เงา / เส้นในของกรอบ
// แบบ B: ตำแหน่ง/ขนาดภาพของแต่ละหน้า  zoom 100-300 %, x/y = -100..100 % ของระยะที่ขยับได้ (0 = กลาง)
int qZm[5] = {100, 100, 100, 100, 100}, qPx[5] = {0, 0, 0, 0, 0}, qPy[5] = {0, 0, 0, 0, 0};
uint16_t hsv565(float h, float sv, float v) {
  h = fmodf(h + 360.0f, 360.0f) / 60.0f; int i = (int)h; float f = h - i;
  float p = v * (1 - sv), q = v * (1 - sv * f), t = v * (1 - sv * (1 - f)), r, g, b;
  switch (i) { case 0: r = v; g = t; b = p; break; case 1: r = q; g = v; b = p; break; case 2: r = p; g = v; b = t; break;
               case 3: r = p; g = q; b = v; break; case 4: r = t; g = p; b = v; break; default: r = v; g = p; b = q; }
  return lcd.color565((uint8_t)(r * 255), (uint8_t)(g * 255), (uint8_t)(b * 255));
}
void qPalette(int pi) {                                             // สีตัวเลขจาก สี+ความสด (ค่าเริ่มต้น = ชมพู ขอบม่วง)
  float h = qHue[pi], sv = qSat[pi] / 100.0f;
  qTop = hsv565(h, sv, 1.0f);
  qOut = hsv565(h - 47, 0.80f, sv < 0.08f ? 0.30f : 0.44f);
  qShd = hsv565(h - 47, 0.85f, 0.17f);
  qFIn = hsv565(h - 47, min(1.0f, sv * 1.1f), 0.90f);              // กรอบรูปใช้สีเดียวกับตัวเลข (ค่าเริ่มต้น = ชมพูนอก ม่วงใน)
}
uint8_t qDist[112 * 60];                                            // ระยะห่างจากตัวอักษร (ทำขอบ/เงา)
int qGifIdx = 0; uint32_t qGifT = 0;
// โหมดภาพของแต่ละหน้า GIF (หน้า 9, 10, 11, 12, 13):
//   1 = ภาพค้างตามรอบ (รอบ = เฟรม)   2 = เล่นวนลูป เร็วตามความเร็วรถ (จอดนิ่ง = ภาพหยุด)   0 = เล่นวนลูป เร็ว/ช้าตามรอบ
#define QGIF_MODES_INIT {1, 1, 1, 2, 2}
#define QGIF_KMH_1X     60                                          // โหมด 2: ที่ความเร็วนี้ (km/h) ภาพเล่นเท่าความเร็วไฟล์จริง (120 km/h = 2 เท่า)
#define QGIF_KMH_MAXRATE 3.0f                                       // โหมด 2: เร็วสุดกี่เท่า
#define QGIF_RPM_LO  0                                              // ต่ำกว่ารอบนี้ = ค้างเฟรมแรก, จากรอบนี้ถึง redline = ไล่เฟรมจนถึงเฟรมสุดท้าย
#define QGIF_RPM_LO2 0                                              // หน้า 10: รอบที่เป็นเฟรมแรก
#define QGIF_RPM_HI  0                                              // รอบที่ถึงเฟรมสุดท้าย (0 = redline) ตั้งต่ำลงเช่น 5000 = ภาพขยับเยอะในรอบใช้งานจริง
#define QGIF_RPM_HI2 0                                              // หน้า 10
#define QGIF_RPM_HI3 0                                              // หน้า 11
#define QGIF_RPM_LO3 0                                              // หน้า 11: รอบที่เป็นเฟรมแรก
#define QGIF_RPM_LO4 0                                              // หน้า 12
#define QGIF_RPM_HI4 0
#define QGIF_RPM_LO5 0                                              // หน้า 13
#define QGIF_RPM_HI5 0
const int QGIF_MODES[Q_NP] = QGIF_MODES_INIT;
const int QGIF_LO[Q_NP] = {QGIF_RPM_LO, QGIF_RPM_LO2, QGIF_RPM_LO3, QGIF_RPM_LO4, QGIF_RPM_LO5};
const int QGIF_HI[Q_NP] = {QGIF_RPM_HI, QGIF_RPM_HI2, QGIF_RPM_HI3, QGIF_RPM_HI4, QGIF_RPM_HI5};
#define QGIF_STEP_MS 20                                             // เปลี่ยนภาพได้ถี่สุดทุก 20 ms (~50 ครั้ง/วินาที)
#define QGIF_RATE_LO 0.6f                                           // ความเร็วเล่น GIF ที่รอบ 0 (0.6 = ช้ากว่าไฟล์จริง)
#define QGIF_RATE_HI 2.4f                                           // ความเร็วเล่น GIF ที่ redline (2.4 เท่า)
// ช่องขวา 3 ช่อง: แตะเพื่อสลับค่า (จำไว้ใน NVS)  ข้อมูลของแต่ละค่า: ชื่อ / หน่วย / ทศนิยม / ช่วงของแถบ
volatile int qSlots[Q_NP][3] = {{M_COOLANT, M_IAT, M_BATT}, {M_COOLANT, M_IAT, M_BATT}, {M_COOLANT, M_IAT, M_BATT}, {M_COOLANT, M_IAT, M_BATT}, {M_COOLANT, M_IAT, M_BATT}};
#define qSlot qSlots[qPi]
const QInfo Q_INF[Q_NM] = {
  {"COOLANT", "C", 0, 40, 130},  {"IAT", "C", 0, -10, 80},     {"VOLT", "V", 1, 11, 15},
  {"LOAD", "%", 0, 0, 100},      {"TPS", "%", 0, 0, 100}, {"BOOST", "bar", 2, -1.0f, 1.0f},
  {"AIRFLOW", "g/s", 1, 0, 120}, {"AFR", "", 1, 10, 20},       {"F.TRIM", "%", 1, -25, 25},
  {"OIL TEMP", "C", 0, 40, 140}, {"TIMING", "deg", 0, -10, 50},
  {"CVT RATIO", "", 2, 0.4f, 2.6f}, {"CVT TEMP", "C", 0, 40, 120},
  {"FUEL NOW", "km/L", 1, 0, 30}, {"FUEL AVG", "km/L", 1, 0, 30},
  {"", "", 1, 0, 100}, {"", "", 1, 0, 100}, {"", "", 1, 0, 100}, {"", "", 1, 0, 100}};
QInfo qInf(int m) {                                                  // ช่อง Custom ใช้ชื่อ/หน่วย/ช่วงที่แอปส่งมา
  if (m >= Q_C1 && m < Q_C1 + N_CUST) { int k = m - Q_C1; return {cLab[k], cUnit[k], cDec[k], cLo[k], cHi[k]}; }
  return Q_INF[m];
}
float qPk[Q_NM]; bool qPkHave[Q_NM];                      // MAX (แบต = MIN) ของแต่ละค่า ตั้งแต่ต่อ OBD ติด
// อัตราทดพูลเลย์ CVT = รอบเครื่อง / (รอบล้อ x ทดคงที่)  ใช้ได้ตอนวิ่ง >= 8 km/h (ต่ำกว่านั้นทอร์คคอนเวอร์เตอร์ยังลื่น ค่าไม่จริง)
float cvtRatioS = 0; bool cvtHaveS = false;
static void cvtUpdate() {
  int r = rpm, s = speedKmh;
  if (s < 8 || r < 500) { cvtHaveS = false; return; }
  float wheelRpm = s * 1000000.0f / 60.0f / CVT_TIRE_MM;
  float v = r / (wheelRpm * (CVT_FIXED_X1000 / 1000.0f));
  if (v < 0.3f || v > 3.2f) { cvtHaveS = false; return; }
  cvtRatioS = cvtHaveS ? cvtRatioS + (v - cvtRatioS) * 0.25f : v; cvtHaveS = true;
}
float qVal(int m) {
  if (m == Q_CVT) return cvtRatioS;
  if (m == Q_ATF) return atfTemp;
  if (m >= Q_C1 && m < Q_C1 + N_CUST) return custVal[m - Q_C1];
  if (m == Q_FUELI) return min(99.9f, speedKmh / max(0.2f, (float)fuelLph));
  if (m == Q_FUELA) return fuelUsedL > 0.01 ? min(99.9, fuelDistKm / fuelUsedL) : 0.0f;
  return m == M_MAP ? (pd[M_MAP] - 101.3f) / 100.0f : pd[m];
}
bool qHave(int m) {
  if (m == Q_CVT) return cvtHaveS;
  if (m == Q_ATF) return atfHave && millis() - atfMs < 5000;
  if (m == Q_FUELI) return fuelHave && speedKmh >= 3 && millis() - fuelMs < 5000;   // จอดนิ่ง = --
  if (m == Q_FUELA) return fuelUsedL > 0.01 && fuelDistKm > 0.1;
  if (m >= Q_C1 && m < Q_C1 + N_CUST) return cDef[m - Q_C1] && cMs[m - Q_C1] && millis() - cMs[m - Q_C1] < 5000;
  return m == M_COOLANT ? coolHave() : metricHave[m];
}

void qColors() {
  Q_PK = lcd.color565(255, 92, 196);  Q_WH = lcd.color565(255, 244, 252);
  Q_OUT = lcd.color565(74, 22, 112);  Q_SHD = lcd.color565(28, 6, 44);
  Q_OFF = lcd.color565(34, 22, 48);   Q_LAB = lcd.color565(196, 176, 220);
  Q_RD = lcd.color565(255, 50, 70);   Q_RDOFF = lcd.color565(60, 14, 22);
  Q_VIO = lcd.color565(140, 70, 230);
}
static inline uint16_t qMix(uint16_t a, uint16_t b, float t) {      // ผสมสี RGB565
  t = constrain(t, 0.0f, 1.0f);
  int r = ((a >> 11) & 31) + (int)((((b >> 11) & 31) - ((a >> 11) & 31)) * t);
  int g = ((a >> 5) & 63) + (int)((((b >> 5) & 63) - ((a >> 5) & 63)) * t);
  int l = (a & 31) + (int)(((b & 31) - (a & 31)) * t);
  return (r << 11) | (g << 5) | l;
}
// ตัวเลขไตเติลอนิเมะ: วาดตัวอักษรลงสไปรท์ -> หาระยะห่างจากตัวอักษร -> ระบายเนื้อไล่สี / ขอบขาว / ขอบม่วง / เงาเยื้องขวาล่าง
// align 0 ซ้าย 1 กลาง 2 ขวา, base = เส้นฐานในกล่อง, hot = ไล่สีแดง (เตือน)
void qText(int slot, const char *s, int x, int y, int w, int h, const lgfx::IFont *f, float sc, int align, int base, bool hot) {
  if (!chgTxt(slot, s, hot ? 1 : 0)) return;
  sc = fitSc(f, s, sc, w - 14);                                     // ข้อความยาว (เช่น -0.65) ย่อให้พอดีกล่อง
  if (!pSprOk || w > PSPR_W || h > PSPR_H) { drawVal(x, y, w, h, s, f, sc, hot ? Q_RD : qTop, TFT_BLACK, align == 1, base); return; }
  pSpr.fillRect(0, 0, w, h, 0);
  pSpr.setFont(f); pSpr.setTextSize(sc); pSpr.setTextColor(0xFFFF);
  pSpr.setTextDatum(align == 0 ? lgfx::textdatum_t::baseline_left : align == 1 ? lgfx::textdatum_t::baseline_center : lgfx::textdatum_t::baseline_right);
  pSpr.drawString(s, align == 0 ? 4 : align == 1 ? w / 2 - 2 : w - 8, base);
  pSpr.setTextSize(1.0f); pSpr.setTextDatum(lgfx::textdatum_t::top_left);
  int top = h, bot = 0;
  for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
    bool ink = pSpr.readPixelValue(i, j) != 0;
    qDist[j * w + i] = ink ? 0 : 250;
    if (ink) { if (j < top) top = j; if (j > bot) bot = j; }
  }
  // ระยะห่างแบบ chamfer (ตรง 2, ทแยง 3) สองรอบ
  for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
    uint8_t &d = qDist[j * w + i]; int v = d;
    if (i > 0) v = min(v, qDist[j * w + i - 1] + 2);
    if (j > 0) { v = min(v, qDist[(j - 1) * w + i] + 2); if (i > 0) v = min(v, qDist[(j - 1) * w + i - 1] + 3); if (i < w - 1) v = min(v, qDist[(j - 1) * w + i + 1] + 3); }
    d = (uint8_t)v;
  }
  for (int j = h - 1; j >= 0; j--) for (int i = w - 1; i >= 0; i--) {
    uint8_t &d = qDist[j * w + i]; int v = d;
    if (i < w - 1) v = min(v, qDist[j * w + i + 1] + 2);
    if (j < h - 1) { v = min(v, qDist[(j + 1) * w + i] + 2); if (i < w - 1) v = min(v, qDist[(j + 1) * w + i + 1] + 3); if (i > 0) v = min(v, qDist[(j + 1) * w + i - 1] + 3); }
    d = (uint8_t)v;
  }
  uint16_t c0 = hot ? Q_RD : qTop, c1 = hot ? lcd.color565(255, 220, 200) : Q_WH;
  float span = (bot > top) ? (float)(bot - top) : 1.0f;
  const int SO = 3;                                                 // เงาเยื้อง 3 px
  pSpr.startWrite();
  for (int j = 0; j < h; j++) {
    uint16_t fill = qMix(c0, c1, (j - top) / span * 1.15f - 0.05f);
    for (int i = 0; i < w; i++) {
      int d = qDist[j * w + i]; uint16_t c;
      if (d == 0) c = fill;
      else if (d <= 3) c = Q_WH;                                    // ขอบขาวบาง
      else if (d <= 7) c = qOut;                                    // ขอบสีเข้ม
      else if (i >= SO && j >= SO && qDist[(j - SO) * w + i - SO] <= 7) c = qShd;    // เงา
      else c = TFT_BLACK;
      pSpr.writePixel(i, j, c);
    }
  }
  pSpr.endWrite();
  lcd.setClipRect(x, y, w, h); pSpr.pushSprite(x, y); lcd.clearClipRect();
}
// ช่องเฉียง (สี่เหลี่ยมด้านขนาน) แถบใหม่
void qSeg(int x, int y, int w, int h, uint16_t c) {
  for (int k = 0; k < h; k++) lcd.drawFastHLine(x + (h - 1 - k) * 3 / max(h - 1, 1), y + k, w, c);
}
uint16_t qSegCol(float t) { return t < 0.55f ? qMix(Q_VIO, Q_PK, t / 0.55f) : qMix(Q_PK, Q_WH, (t - 0.55f) / 0.45f); }   // ม่วง -> ชมพู -> ขาว
// แถบรอบ: ช่องที่เกิน redline เป็นแดง, ถึง redline กะพริบแดงทั้งแถบ
void qRpmBar(int lit, int flash) {
  int red = (int)(Q_RBN * constrain(maxRpm / (gScaleMax() * 1000.0f), 0.0f, 1.0f));
  float pitch = (Q_RBW - 3) / (float)Q_RBN;
  for (int s = 0; s < Q_RBN; s++) {
    uint16_t c = (s < lit) ? (flash == 1 ? Q_RD : (s >= red ? Q_RD : qSegCol(s / (float)(Q_RBN - 1)))) : (s >= red ? Q_RDOFF : Q_OFF);
    qSeg(Q_RBX + (int)(s * pitch), Q_RBY, (int)pitch - 1, Q_RBH, c);
  }
}
void qMetBar(int i, int lit, int mk) {                               // แถบค่าทางขวา + ขีดค่าสูงสุด (ช่องสีขาว)
  int y = 4 + i * Q_BLK + 57;
  for (int s = 0; s < Q_MBN; s++)
    qSeg(Q_RX + 2 + s * 6, y, 5, 8, s < lit ? qSegCol(s / (float)(Q_MBN - 1)) : (s == mk ? Q_LAB : Q_OFF));
}
void qFrameAt(int fx, int fy, int fw, int fh) {                       // กรอบภาพ: สีตัวเลขนอก สีเข้มกว่าใน + มุมเน้นสีขาว
  lcd.drawRoundRect(fx, fy, fw, fh, 7, qTop);
  lcd.drawRoundRect(fx + 1, fy + 1, fw - 2, fh - 2, 6, qFIn);
  lcd.drawRoundRect(fx + 2, fy + 2, fw - 4, fh - 4, 5, qOut);
  for (int k = 0; k < 2; k++) {
    lcd.drawFastHLine(fx + 8, fy + k, 18, Q_WH);  lcd.drawFastHLine(fx + fw - 26, fy + fh - 1 - k, 18, Q_WH);
    lcd.drawFastVLine(fx + k, fy + 8, 14, Q_WH);  lcd.drawFastVLine(fx + fw - 1 - k, fy + fh - 22, 14, Q_WH);
  }
}
void qFrame() { qFrameAt(Q_FX, Q_FY, Q_FW, Q_FH); }
bool qBig() { return qLay[qPi] == 1; }
void qGifFrame() {                                                    // ภาพเท่ากรอบ = วาดตรง, ไม่เท่า = ย่อ/ขยายให้พอดีกรอบ (แปลงไฟล์ให้ขนาดตรงจะคมกว่า)
  if (!qG) return;
  int x = qBig() ? QB_IX : Q_IX, y = qBig() ? QB_IY : Q_IY, w = qBig() ? QB_IW : Q_IW, h = qBig() ? QB_IH : Q_IH;
  bool moved = qBig() && (qZm[qPi] != 100 || qPx[qPi] || qPy[qPi]);
  if (!moved && qG->w == w && qG->h == h) { lcd.drawJpg(qG->jpg[qGifIdx], qG->len[qGifIdx], x, y, w, h); return; }
  float sc = min(w / (float)qG->w, h / (float)qG->h);
  if (qBig()) sc *= qZm[qPi] / 100.0f;                               // แบบ B: ซูม + เลื่อนตำแหน่งได้ (ตั้งในหน้าเลือกสี -> MOVE)
  int dw = (int)(qG->w * sc), dh = (int)(qG->h * sc);
  int x0 = x + (w - dw) / 2, y0 = y + (h - dh) / 2;
  if (qBig()) { x0 += qPx[qPi] * abs(w - dw) / 200; y0 += qPy[qPi] * abs(h - dh) / 200; }
  if (sc == 1.0f && !moved) { lcd.drawJpg(qG->jpg[qGifIdx], qG->len[qGifIdx], x0, y0, dw, dh); return; }
  lcd.setClipRect(x, y, w, h);
  lcd.drawJpg(qG->jpg[qGifIdx], qG->len[qGifIdx], x0, y0, 0, 0, 0, 0, sc, sc);
  lcd.clearClipRect();
}
// ---- แบบ B: ลากนิ้วบนภาพ = เลื่อนตำแหน่ง, - / + = ซูม ----
void drawSmallBtn(int x, int y, int w, int h, const String &label, uint16_t col);
void waitRelease();
bool inRect(int x, int y, int rx, int ry, int rw, int rh);
void qMoveScreen(int pi) {
  lcd.fillScreen(TFT_BLACK);
  qPalette(pi);
  qFrameAt(QB_FX, QB_FY, QB_FW, QB_FH);
  drawSmallBtn(4, 192, 48, 40, "-", C_PINK); drawSmallBtn(56, 192, 48, 40, "+", C_PINK);
  drawSmallBtn(110, 192, 96, 40, "RESET", C_WARN); drawSmallBtn(212, 192, 104, 40, "DONE", C_OK);
  lcd.setFont(&fonts::Font0); lcd.setTextColor(C_DIM, TFT_BLACK); lcd.setTextDatum(lgfx::textdatum_t::top_left);
  lcd.drawString("drag the picture to move it", 8, 179);
  auto redraw = [&]() {
    lcd.fillRect(QB_IX, QB_IY, QB_IW, QB_IH, TFT_BLACK);
    if (qG) qGifFrame(); else lcd.fillRect(QB_IX, QB_IY, QB_IW, QB_IH, qShd);
    char b[24]; snprintf(b, sizeof(b), "ZOOM %d%%  X %+d  Y %+d  ", qZm[pi], qPx[pi], qPy[pi]);
    lcd.setFont(&fonts::Font0); lcd.setTextColor(qTop, TFT_BLACK); lcd.setTextDatum(lgfx::textdatum_t::top_right);
    lcd.drawString(b, 316, 179); lcd.setTextDatum(lgfx::textdatum_t::top_left);
  };
  int saveLay = qLay[pi]; qLay[pi] = 1;                              // วาดแบบ B ระหว่างตั้ง
  redraw(); waitRelease();
  bool drag = false; int32_t lx = 0, ly = 0; uint32_t lastDraw = 0;
  while (true) {
    int32_t x, y;
    if (getTouchXY(&x, &y)) {
      if (y < 176) {                                                  // ลากบนภาพ
        if (drag) {
          int dw = abs(QB_IW - (int)((qG ? qG->w : QB_IW) * min(QB_IW / (float)(qG ? qG->w : QB_IW), QB_IH / (float)(qG ? qG->h : QB_IH)) * qZm[pi] / 100.0f));
          int dh = abs(QB_IH - (int)((qG ? qG->h : QB_IH) * min(QB_IW / (float)(qG ? qG->w : QB_IW), QB_IH / (float)(qG ? qG->h : QB_IH)) * qZm[pi] / 100.0f));
          if (dw > 0) qPx[pi] = constrain(qPx[pi] + (int)((x - lx) * 200 / dw), -100, 100);
          if (dh > 0) qPy[pi] = constrain(qPy[pi] + (int)((y - ly) * 200 / dh), -100, 100);
        }
        drag = true; lx = x; ly = y;
        if (millis() - lastDraw > 80) { lastDraw = millis(); redraw(); }
      } else {
        drag = false;
        if (inRect(x, y, 4, 192, 48, 40)) { qZm[pi] = max(100, qZm[pi] - 10); redraw(); delay(120); }
        else if (inRect(x, y, 56, 192, 48, 40)) { qZm[pi] = min(300, qZm[pi] + 10); redraw(); delay(120); }
        else if (inRect(x, y, 110, 192, 96, 40)) { qZm[pi] = 100; qPx[pi] = qPy[pi] = 0; redraw(); waitRelease(); }
        else if (inRect(x, y, 212, 192, 104, 40)) { waitRelease(); break; }
      }
    } else { if (drag) redraw(); drag = false; }
    delay(10);
  }
  qLay[pi] = saveLay;
  char k[4] = {'q', 'z', (char)('0' + pi), 0}; prefs.putInt(k, qZm[pi]);
  k[1] = 'x'; prefs.putInt(k, qPx[pi]);
  k[1] = 'y'; prefs.putInt(k, qPy[pi]);
}
void qSlotStatic(int i) {                                            // ส่วนคงที่ของช่องขวา i: ชื่อ + หน่วย + แถบว่าง
  int y0 = 4 + i * Q_BLK, m = qSlot[i];
  lcd.fillRect(Q_RX - 4, y0, 320 - (Q_RX - 4), Q_BLK - 2, TFT_BLACK);
  QInfo qi = qInf(m);
  txtAt(qi.lab, Q_RX + 2, y0, &fonts::Font2, Q_LAB, lgfx::textdatum_t::top_left);
  const char *u = qi.unit;
  if (!strcmp(u, "C")) { lcd.drawCircle(Q_RX + 95, y0 + 28, 2, Q_LAB); txtAt("C", Q_RX + 99, y0 + 26, &fonts::Font0, Q_LAB, lgfx::textdatum_t::top_left); }
  else txtAt(u, Q_RX + 93, y0 + 26, &fonts::Font0, Q_LAB, lgfx::textdatum_t::top_left);
  qMetBar(i, 0, -1);
  pTxt[3 + i][0] = 0; pTxt[6 + i][0] = 0; pBar[2 + i] = -1; pBar[5 + i] = -1;   // ให้วาดค่าใหม่
}
void qTap(int x, int y) {                                            // แตะช่องขวา = เปลี่ยนเป็นค่าถัดไปที่รถรองรับ (ไม่ซ้ำกับอีก 2 ช่อง)
  if (x < Q_RX - 4 || y < 4 || y >= 4 + 3 * Q_BLK) return;
  int i = (y - 4) / Q_BLK, m = qSlot[i];
  for (int k = 0; k < Q_NM; k++) {
    m = (m + 1) % Q_NM;
    if (m == qSlot[(i + 1) % 3] || m == qSlot[(i + 2) % 3]) continue;
    if (metricAvailable(m) || demoMode) break;
  }
  qSlot[i] = m;
  char k[5] = {'q', 's', (char)('0' + i), 0, 0};
  if (qPi) { k[0] = 'q'; k[1] = (char)('0' + qPi); k[2] = 's'; k[3] = (char)('0' + i); }   // หน้า 10/11: q1s0.. q2s2
  prefs.putInt(k, m);
  if (m < N_METRICS) urgentMetric = m;                                 // อ่านค่าใหม่ทันที
  qSlotStatic(i);
}
// ---- หน้าเลือกสีตัวเลข (กดค้างที่ตัวเลขความเร็ว/รอบในหน้า GIF) ----
void drawSmallBtn(int x, int y, int w, int h, const String &label, uint16_t col);
void waitRelease();
bool inRect(int x, int y, int rx, int ry, int rw, int rh);
void drawStatic(bool);
void invalidateUI();
void qColorScreen(int pi) {
  const int HX = 10, HW = 300, HY = 128, HH = 24, SY = 170, SH = 20;
  auto preview = [&]() {
    qPalette(pi); pTxt[0][0] = 0; pTxt[1][0] = 0;
    qFrameAt(2, 48, 316, 64);                                          // ตัวอย่างกรอบรูป (สีตามตัวเลข)
    qText(0, "7500", 40, 54, 112, 52, &fonts::FreeSansBoldOblique24pt7b, 1.0f, 2, 42, false);
    qText(1, "88", 170, 60, 112, 44, &fonts::FreeSansBoldOblique18pt7b, 1.0f, 0, 32, false);
  };
  auto bars = [&]() {
    for (int x = 0; x < HW; x++) lcd.drawFastVLine(HX + x, HY, HH, hsv565(x * 360.0f / HW, 0.9f, 1.0f));
    for (int x = 0; x < HW; x++) lcd.drawFastVLine(HX + x, SY, SH, hsv565(qHue[pi], x / (float)(HW - 1), 1.0f));
    lcd.fillRect(0, HY + HH, 320, 7, C_BG); lcd.fillRect(0, SY + SH, 320, 7, C_BG); lcd.fillRect(0, SY - 12, 320, 12, C_BG); lcd.fillRect(0, HY - 12, 320, 12, C_BG);
    int hx = HX + qHue[pi] * (HW - 1) / 359, sx = HX + qSat[pi] * (HW - 1) / 100;
    lcd.fillTriangle(hx - 5, HY + HH + 6, hx + 5, HY + HH + 6, hx, HY + HH, TFT_WHITE);
    lcd.fillTriangle(sx - 5, SY + SH + 6, sx + 5, SY + SH + 6, sx, SY + SH, TFT_WHITE);
    lcd.drawFastVLine(hx, HY, HH, TFT_BLACK); lcd.drawFastVLine(sx, SY, SH, TFT_BLACK);
    lcd.setFont(&fonts::Font0); lcd.setTextColor(C_DIM, C_BG); lcd.setTextDatum(lgfx::textdatum_t::top_left);
    lcd.drawString("COLOR", HX, HY - 11); lcd.drawString("STRENGTH (left = white)", HX, SY - 11);
  };
  pSprEnsure();
  lcd.fillScreen(C_BG);
  lcd.setFont(&fonts::FreeSansBold12pt7b); lcd.setTextColor(C_PINK); lcd.setTextDatum(lgfx::textdatum_t::top_left);
  char t[24]; snprintf(t, sizeof(t), "TEXT COLOR  P%d", pi + 9); lcd.drawString(t, 10, 6);
  lcd.fillRect(0, 48, 320, 64, TFT_BLACK);
  drawSmallBtn(10, 204, 96, 32, "RESET", C_WARN); drawSmallBtn(214, 204, 96, 32, "DONE", C_OK);
  drawSmallBtn(236, 4, 80, 30, "MOVE", C_PINK);                       // แบบ B: เลื่อน/ซูมภาพ
  auto styleBtn = [&]() { drawSmallBtn(112, 204, 96, 32, qLay[pi] ? "STYLE B" : "STYLE A", C_PINK); };
  styleBtn();
  bars(); preview();
  waitRelease();
  uint32_t lastPv = 0; bool dirty = false;
  while (true) {
    int32_t x, y;
    uint32_t now = millis();
    if (getTouchXY(&x, &y)) {
      if (y >= HY - 8 && y < HY + HH + 10) { qHue[pi] = constrain((int)((x - HX) * 359 / (HW - 1)), 0, 359); bars(); dirty = true; }
      else if (y >= SY - 8 && y < SY + SH + 10) { qSat[pi] = constrain((int)((x - HX) * 100 / (HW - 1)), 0, 100); bars(); dirty = true; }
      else if (inRect(x, y, 112, 204, 96, 32)) { qLay[pi] ^= 1; styleBtn(); waitRelease(); }   // A = กรอบ+ค่าขวา, B = ภาพใหญ่+รอบ/ความเร็ว
      else if (inRect(x, y, 10, 204, 96, 32)) { qHue[pi] = QC_HUE_DEF; qSat[pi] = QC_SAT_DEF; bars(); dirty = true; waitRelease(); }
      else if (inRect(x, y, 214, 204, 96, 32)) { waitRelease(); break; }
      else if (inRect(x, y, 236, 4, 80, 30)) {                         // เลื่อน/ซูมภาพแบบ B แล้วกลับมาหน้าเลือกสี
        waitRelease(); qMoveScreen(pi);
        lcd.fillScreen(C_BG);
        lcd.setFont(&fonts::FreeSansBold12pt7b); lcd.setTextColor(C_PINK); lcd.setTextDatum(lgfx::textdatum_t::top_left); lcd.drawString(t, 10, 6);
        lcd.fillRect(0, 48, 320, 64, TFT_BLACK);
        drawSmallBtn(10, 204, 96, 32, "RESET", C_WARN); drawSmallBtn(214, 204, 96, 32, "DONE", C_OK); drawSmallBtn(236, 4, 80, 30, "MOVE", C_PINK);
        styleBtn(); bars(); preview();
      }
    }
    if (dirty && now - lastPv > 60) { lastPv = now; dirty = false; preview(); }
    delay(10);
  }
  char k[4] = {'q', 'h', (char)('0' + pi), 0}; prefs.putInt(k, qHue[pi]);
  k[1] = 't'; prefs.putInt(k, qSat[pi]);
  k[1] = 'l'; prefs.putInt(k, qLay[pi]);
  drawStatic(false); invalidateUI();                                  // กลับหน้าเดิม วาดใหม่ด้วยสีใหม่
}
void qStatic() {
  lcd.fillScreen(TFT_BLACK);
  qPi = qPageIdx(page); qG = qGifOf(page); qPalette(qPi);
  if (qBig()) qFrameAt(QB_FX, QB_FY, QB_FW, QB_FH); else qFrame();
  if (qG) { qGifIdx = 0; qGifT = millis(); qGifFrame(); }
  else {
    int x = qBig() ? QB_IX : Q_IX, y = qBig() ? QB_IY : Q_IY, w = qBig() ? QB_IW : Q_IW, h = qBig() ? QB_IH : Q_IH;
    lcd.fillRect(x, y, w, h, Q_SHD);
    char ph[24]; if (qPi) snprintf(ph, sizeof(ph), "put gifbox%d.h here", qPi + 1); else strcpy(ph, "put gifbox.h here");
    txtAt(ph, x + w / 2, y + h / 2 - 8, &fonts::Font2, Q_LAB, lgfx::textdatum_t::top_center);
  }
  if (qBig()) {                                                       // B: รอบซ้ายล่าง ความเร็วขวาล่าง ไม่มีแถบ
    txtAt("RPM", 120, 210, &fonts::Font2, Q_LAB, lgfx::textdatum_t::top_left);
    txtAt("km/h", 280, 210, &fonts::Font2, Q_LAB, lgfx::textdatum_t::top_left);
    return;
  }
  qRpmBar(0, 0);
  txtAt("km/h", 120, 178, &fonts::Font2, Q_LAB, lgfx::textdatum_t::top_left);
  txtAt("RPM", 120, 212, &fonts::Font2, Q_LAB, lgfx::textdatum_t::top_left);
  for (int i = 0; i < 3; i++) qSlotStatic(i);
}
void updateQ(uint32_t now, bool over) {
  char b[12];
  if (qG) {
    int qm = QGIF_MODES[qPi];
    if (qm == 1)
    {                                                               // ภาพค้างตามรอบ: รอบต่ำ = เฟรมแรก, redline = เฟรมสุดท้าย
      const int LO = QGIF_LO[qPi];
      const int HI0 = QGIF_HI[qPi];
      const int HI = HI0 > 0 ? HI0 : maxRpm;                         // 0 = ถึงเฟรมสุดท้ายที่ redline
      float pos = constrain((dispRpm - LO) / (float)max(HI - LO, 500), 0.0f, 1.0f) * (qG->n - 1);
      int tgt = qGifIdx;
      if (pos > qGifIdx + 0.6f) tgt = (int)floorf(pos + 0.4f);      // กันภาพสั่นตอนรอบแกว่งอยู่ตรงรอยต่อ
      else if (pos < qGifIdx - 0.6f) tgt = (int)ceilf(pos - 0.4f);
      if (tgt != qGifIdx && now - qGifT >= QGIF_STEP_MS) {           // ไปเฟรมตามรอบทันที (รอบผ่านสปริงนุ่มมาแล้ว ภาพไม่กระตุก)
        qGifT = now;                                                 // ไม่ไล่ทีละเฟรม -> ภาพไม่ตามหลังรอบ ไม่ดูสโลว์แม้ GIF มีเฟรมเยอะ
        qGifIdx = constrain(tgt, 0, qG->n - 1); qGifFrame();
      }
    }
    else if (qm == 2) {                                             // เล่นวนลูป: ยิ่งขับเร็ว ภาพยิ่งขยับเร็ว (จอดนิ่ง = ภาพหยุด)
      static float acc = 0; static uint32_t lastT = 0;
      if (now - lastT > 200) lastT = now;                           // เพิ่งเข้าหน้านี้/เพิ่งออกจากเมนู
      float rate = constrain(dispSpeed / (float)QGIF_KMH_1X, 0.0f, QGIF_KMH_MAXRATE);   // QGIF_KMH_1X km/h = ความเร็วเท่าไฟล์จริง
      if (dispSpeed < 1.0f) rate = 0;
      acc += (now - lastT) * rate; lastT = now;
      float base = qG->dms ? qG->dms[qGifIdx] : qG->ms;
      if (acc >= base && now - qGifT >= 35) {                        // ถึงเวลาเฟรมถัดไป (ถ้าเร็วมากข้ามเฟรมได้ ภาพไม่ช้ากว่าความเร็วจริง)
        int step = 0;
        while (acc >= base && step < qG->n) { acc -= base; step++; base = qG->dms ? qG->dms[(qGifIdx + step) % qG->n] : qG->ms; }
        qGifT = now;
        qGifIdx = (qGifIdx + step) % qG->n; qGifFrame();
      }
    } else
    {                                                               // เล่นวนลูป: ความเร็วตามรอบเครื่อง
      float base = qG->dms ? qG->dms[qGifIdx] : qG->ms;
      float r = constrain(dispRpm / (float)max(maxRpm, 1000), 0.0f, 1.2f);
      float rate = QGIF_RATE_LO + (QGIF_RATE_HI - QGIF_RATE_LO) * r;
      uint32_t dly = (uint32_t)max(35.0f, base / rate);
      if (now - qGifT >= dly) {
        qGifT = (now - qGifT > dly + 200) ? now : qGifT + dly;
        qGifIdx = (qGifIdx + 1) % qG->n; qGifFrame();
      }
    }
  }
  if (qBig()) {                                                       // B: แค่รอบ + ความเร็ว ตัวใหญ่
    fmtRpm(b);
    qText(2, b, 4, 176, 112, 52, &fonts::FreeSansBoldOblique24pt7b, 1.0f, 2, 42, over);
    snprintf(b, sizeof(b), "%d", (int)lroundf(dispSpeed));
    qText(0, b, 164, 176, 112, 52, &fonts::FreeSansBoldOblique24pt7b, 1.0f, 2, 42, false);
    return;
  }
  bool flash = over && fastBlink;
  int lit = (int)lroundf(constrain(dispRpm / (gScaleMax() * 1000.0f), 0.0f, 1.0f) * Q_RBN);
  int fk = over ? (flash ? 1 : 2) : 0;
  if (chgBar(0, lit) | chgBar(1, fk)) qRpmBar(lit, fk == 1 ? 1 : 0);
  nStatus(4, 0, false, TFT_BLACK, 110);
  snprintf(b, sizeof(b), "%d", (int)lroundf(dispSpeed));
  qText(0, b, 4, 140, 112, 52, &fonts::FreeSansBoldOblique24pt7b, 1.0f, 2, 42, false);
  fmtRpm(b);
  qText(2, b, 4, 192, 112, 36, &fonts::FreeSansBoldOblique18pt7b, 1.0f, 2, 29, over);
  // ค่าทางขวา: ตัวเลข + แถบ + MAX/MIN ของทริป
  static bool lastObd = false;
  static int lastGen = 0;
  if (lastGen != cGen) { lastGen = cGen; for (int i = 0; i < 3; i++) if (qSlot[i] >= Q_C1) { qSlotStatic(i); qPkHave[qSlot[i]] = false; } }   // แอปเปลี่ยนชื่อ Custom
  cvtUpdate();
  if (obdOk && !lastObd) memset(qPkHave, 0, sizeof(qPkHave));        // ต่อ OBD ติดใหม่ = เริ่ม MAX/MIN ใหม่
  lastObd = obdOk;
  for (int i = 0; i < 3; i++) {
    int y0 = 4 + i * Q_BLK, m = qSlot[i];
    const QInfo q = qInf(m);
    bool have = qHave(m), isMin = (m == M_BATT || m == Q_CVT);   // CVT: จำค่าต่ำสุด (OD)
    float v = qVal(m);
    if (have) {
      if (!qPkHave[m]) { qPk[m] = v; qPkHave[m] = true; }
      else if (isMin ? v < qPk[m] : v > qPk[m]) qPk[m] = v;
    }
    bool warn = (m == M_COOLANT && have && coolant >= tempAlert && blinkOn) || (m == M_BATT && have && v < 11.8f);
    if (!have) strcpy(b, "--"); else snprintf(b, sizeof(b), q.dec == 2 ? "%.2f" : q.dec ? "%.1f" : "%.0f", v);
    if (!strcmp(b, "-0") || !strcmp(b, "-0.0") || !strcmp(b, "-0.00")) memmove(b, b + 1, strlen(b));
    qText(3 + i, b, Q_RX - 4, y0 + 15, 96, 40, &fonts::FreeSansBoldOblique18pt7b, 1.0f, 2, 30, warn);
    float span = q.hi - q.lo;
    int l = have ? (int)lroundf(constrain((v - q.lo) / span, 0.0f, 1.0f) * Q_MBN) : 0;
    int mk = qPkHave[m] ? constrain((int)lroundf(constrain((qPk[m] - q.lo) / span, 0.0f, 1.0f) * Q_MBN) - 1, 0, Q_MBN - 1) : -1;
    if (chgBar(2 + i, l) | chgBar(5 + i, mk)) qMetBar(i, l, mk);
    char vb[10];
    if (qPkHave[m]) snprintf(vb, sizeof(vb), q.dec == 2 ? "%.2f" : q.dec ? "%.1f" : "%.0f", qPk[m]); else strcpy(vb, "--");
    snprintf(b, sizeof(b), "%s %s", isMin ? "MIN" : "MAX", vb);
    if (chgTxt(6 + i, b, qTop)) {
      lcd.setFont(&fonts::Font0); lcd.setTextColor(qTop, TFT_BLACK); lcd.setTextDatum(lgfx::textdatum_t::top_right);
      lcd.setTextPadding(52); lcd.drawString(b, 316, y0 + 4); lcd.setTextPadding(0); lcd.setTextDatum(lgfx::textdatum_t::top_left);
    }
  }
}

void p3Static();
void p4Static();
void drawStatic(bool) {                                    // วาดส่วนคงที่ของหน้าปัจจุบัน
  if (!isHPage(page)) hFree(); else pSprFree();            // หน้า 9 / 13 ใช้แรมพื้นหลังของตัวเอง (~67 KB) แทนสไปรท์ตัวเลข
  if (!isFPage(page)) fFree();                             // หน้า 10/11/12: พื้นหลังใต้ตัวเลข (~23 KB) เฉพาะตอนอยู่หน้านี้
  pSprEnsure();
  switch (page) {
    case PG_A: aStatic(); break;
    case PG_B: bStatic(); break;
    case PG_C: cStatic(); break;
    case PG_D: dStatic(); break;
    case PG_E: eStatic(); break;
    case PG_Q: case PG_Q2: case PG_Q3: case PG_Q4: case PG_Q5: qStatic(); break;
    case PG_F: gStatic(); break;
    case PG_H: hStatic(); break;
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
    case PG_A: updateA(now, rpmOver); break;
    case PG_B: updateB(now, rpmOver); break;
    case PG_C: updateC(now, rpmOver); break;
    case PG_D: updateD(now, rpmOver); break;
    case PG_E: updateE(now, rpmOver); break;
    case PG_Q: case PG_Q2: case PG_Q3: case PG_Q4: case PG_Q5: updateQ(now, rpmOver); break;
    case PG_F: updateG(now, rpmOver); break;
    case PG_H: updateH(now, dt, rpmOver, fastChanged); break;
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



#if OBD_LINK_CAN
// ---- หน้าสถานะ CAN (แทนหน้า Bluetooth) ----
void btScreen() {
  lcd.fillScreen(C_BG);
  lcd.setFont(&fonts::FreeSansBold12pt7b); lcd.setTextColor(C_PINK, C_BG);
  lcd.setCursor(10, 4); lcd.print("CAN BUS (direct)");
  lcd.setFont(&fonts::Font2); lcd.setTextColor(C_DIM, C_BG);
  lcd.setCursor(10, 34); lcd.printf("CTX = GPIO%d   CRX = GPIO%d   500 kbit/s", CAN_TX_PIN, CAN_RX_PIN);
  lcd.setCursor(10, 52); lcd.printf("OBD2 pin 6 = CAN-H, pin 14 = CAN-L, pin 4/5 = GND");
  if (CAN_SLEEP_IDLE_S) { lcd.setCursor(10, 70); lcd.printf("sleep after %d s quiet bus, wake on CAN / touch", CAN_SLEEP_IDLE_S); }
  drawBtn(90, 196, 140, 38, "BACK", C_OK);
  waitRelease();
  uint32_t last = 0, lastCnt = canRxCount;
  while (true) {
    int32_t x, y;
    if (getTouchXY(&x, &y) && inRect(x, y, 90, 196, 140, 38)) { waitRelease(); break; }
    if (millis() - last > 1000) {
      last = millis();
      twai_status_info_t st = {}; bool sOk = canUp && twai_get_status_info(&st) == ESP_OK;
      const char *stt = !canUp ? "not started" : !sOk ? "?" : st.state == TWAI_STATE_RUNNING ? "running" : st.state == TWAI_STATE_BUS_OFF ? "BUS OFF (check wiring)" : "recovering";
      char b[64];
      lcd.fillRect(0, 96, 320, 92, C_BG);
      lcd.setFont(&fonts::FreeSansBold9pt7b);
      lcd.setTextColor(sOk && st.state == TWAI_STATE_RUNNING ? C_OK : C_WARN, C_BG); snprintf(b, sizeof(b), "Driver: %s", stt); lcd.setCursor(10, 100); lcd.print(b);
      lcd.setTextColor(C_TXT, C_BG); snprintf(b, sizeof(b), "Bus traffic: %u frames/s", (unsigned)(canRxCount - lastCnt)); lcd.setCursor(10, 124); lcd.print(b);
      lastCnt = canRxCount;
      lcd.setTextColor(obdOk ? C_OK : C_WARN, C_BG); lcd.setCursor(10, 148); lcd.print(obdOk ? "ECU: answering (OBD OK)" : "ECU: no answer (key ON?)");
      if (sOk) { lcd.setTextColor(C_DIM, C_BG); snprintf(b, sizeof(b), "tx err %u  rx err %u", (unsigned)st.tx_error_counter, (unsigned)st.rx_error_counter); lcd.setCursor(10, 172); lcd.print(b); }
    }
    delay(20);
  }
}
#elif OBD_LINK_C3
// ---- หน้า C3 (จับคู่): ดูว่าต่อ C3 ตัวไหน / ให้ C3 เปิดรับจอใหม่ / ลืม C3 แล้วหาตัวใหม่ ----
void c3Draw(const char *note) {
  lcd.fillRect(0, 34, 320, 160, C_BG);
  lcd.setFont(&fonts::FreeSansBold9pt7b);
  lcd.setTextColor(c3Link ? C_OK : C_WARN, C_BG);
  lcd.setCursor(10, 40); lcd.printf("Status: %s", curStatus());
  lcd.setTextColor(C_TXT, C_BG);
  lcd.setCursor(10, 66); lcd.printf("C3: %s", c3pair::paired() ? (c3pair::c3Name.length() ? c3pair::c3Name.c_str() : c3pair::saved.c_str()) : "not paired");
  lcd.setFont(&fonts::Font2); lcd.setTextColor(C_DIM, C_BG);
  if (c3pair::stAt) {
    const DidiNowAuthSt &st = c3pair::st;
    lcd.setCursor(10, 92); lcd.printf("Paired screens: %d  ", st.n);
    for (int i = 0; i < st.n && i < DN_PAIR_MAX; i++) lcd.printf("%s%.7s", i ? ", " : "", st.names[i]);
    lcd.setCursor(10, 110);
    uint32_t ago = (millis() - c3pair::stAt) / 1000;
    if (st.open == 255) lcd.print("Pairing: open (no screen paired yet)");
    else if (st.open && st.open * 2 > ago) lcd.printf("Pairing: open %u s", (unsigned)(st.open * 2 - ago));
    else lcd.print("Pairing: closed");
  }
  lcd.setCursor(10, 130); lcd.print(c3pair::msg);
  lcd.setTextColor(C_DIM, C_BG);
  lcd.setCursor(10, 152); lcd.print("New phone/screen: PAIR NEW here, then start it");
  lcd.setCursor(10, 170); lcd.print("Or plug the C3 in again (2 min pairing)");
  if (note) { lcd.setTextColor(C_WARN, C_BG); lcd.setCursor(10, 186); lcd.print(note); }
}
void btScreen() {
  lcd.fillScreen(C_BG);
  lcd.setFont(&fonts::FreeSansBold12pt7b); lcd.setTextColor(C_PINK, C_BG);
  lcd.setCursor(10, 4); lcd.print("C3 LINK (pairing)");
  c3Draw(nullptr);
  drawSmallBtn(8, 200, 96, 36, "PAIR NEW", C_OK);
  drawSmallBtn(112, 200, 96, 36, "FORGET C3", C_WARN);
  drawSmallBtn(216, 200, 96, 36, "BACK", C_PINK);
  waitRelease();
  uint32_t last = millis(), armAt = 0;
  while (true) {
    int32_t x, y;
    if (getTouchXY(&x, &y)) {
      if (inRect(x, y, 216, 200, 96, 36)) { waitRelease(); break; }
      else if (inRect(x, y, 8, 200, 96, 36)) {
        bool ok = c3Link && c3Cmd && c3pair::openPairing(c3Cmd);
        c3Draw(ok ? "C3 accepts new screens for 3 min" : "C3 not connected");
      } else if (inRect(x, y, 112, 200, 96, 36)) {
        if (!armAt || millis() - armAt > 3000) { armAt = millis(); c3Draw("Tap FORGET C3 again to confirm"); }
        else {
          armAt = 0;
          c3pair::forget(c3Link ? c3Cmd : nullptr);
          delay(300);
          if (c3Cli && c3Link) c3Cli->disconnect();
          c3Draw("Forgot - will pair with a C3 in pairing mode");
        }
      }
      waitRelease();
    }
    if (millis() - last > 1000) { last = millis(); c3Draw(nullptr); }
    delay(20);
  }
}
#else
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
}

void btScreen() {
  lcd.fillScreen(C_BG);
  lcd.setFont(&fonts::FreeSansBold12pt7b);
  lcd.setTextColor(C_PINK, C_BG);
  lcd.setCursor(100, 2); lcd.print("BLUETOOTH");
  btStatus("Please wait...");
#if LINK_WIFI
  static const char *const LM[3] = {"MODE: AUTO", "MODE: BT", "MODE: WIFI"};
  int lm0 = linkMode;
  bool inWifi = wifiPhase;
  if (!inWifi)
#endif
  pauseOBD();                          // หยุดอ่าน OBD ชั่วคราวเพื่อสแกนได้
  devCount = 0; devPage = 0;
  btStatus(btName.length() ? ("Saved: " + btName) : String("No device saved (auto: ") + OBD_NAME + ")");
  btDrawList();
  drawSmallBtn(8, 200, 96, 36, "SCAN", C_OK);
  drawSmallBtn(112, 200, 96, 36, String("PIN ") + btPin, C_PINK);
  drawSmallBtn(216, 200, 96, 36, "BACK", C_PINK);
#if LINK_WIFI
  drawSmallBtn(196, 2, 118, 20, LM[linkMode], C_WARN);           // AUTO = WiFi ก่อน / BT = Bluetooth อย่างเดียว / WIFI = WiFi อย่างเดียว
  if (inWifi) btStatus(String("WiFi mode (") + (wifiCur.length() ? wifiCur : String("searching")) + ") - " + curStatus());
#endif

  waitRelease();
  while (true) {
    int32_t x, y;
    if (getTouchXY(&x, &y)) {
      if (inRect(x, y, 216, 200, 96, 36)) break;
#if LINK_WIFI
      else if (inRect(x, y, 196, 2, 118, 20)) {
        linkMode = (linkMode + 1) % 3; prefs.putInt("lnk", linkMode);
        drawSmallBtn(196, 2, 118, 20, LM[linkMode], C_WARN);
        btStatus(linkMode != lm0 ? "Mode changed - restarts on BACK" : "Mode unchanged");
      }
      else if (inWifi && inRect(x, y, 8, 200, 96, 36)) { btStatus("Using WiFi now - set MODE: BT to scan"); }
#endif
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
#if LINK_WIFI
  if (linkMode != lm0) { prefs.end(); delay(100); ESP.restart(); }   // เปลี่ยนโหมด = เริ่มเครื่องใหม่ให้ใช้โหมดใหม่
  if (!inWifi)
#endif
  resumeOBD();
  waitRelease();
}
#endif

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
#if OBD_LINK_CAN || OBD_LINK_C3
  char b[200];
  elmBuf(cmd, b, sizeof(b), timeoutMs);
  return String(b);
#else
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
#endif
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
  if (!linkUp()) { dtcN = 0; dtcDrawList(); dtcStatus("OBD not connected", C_RED); return; }
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
          bool ok = linkUp() && dtcClear();
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
const int MN_N = USE_SOUND ? 10 : 9;                                // ปิดเสียง = ไม่มีข้อ SOUND
const int MN_HDR = 24, MN_ROW = MN_N > 9 ? 21 : 24;
const char *const MN_TITLE[10] = {"ENGINE / REDLINE", "COOLANT ALERT", "DISPLAY", OBD_LINK_CAN ? "CAN BUS" : OBD_LINK_C3 ? "C3 LINK" : "BLUETOOTH", "ENGINE CODES", "GEAR", "PEAKS", "AFR SCALE", "PAGES", "SOUND"};
const char *const MN_DESC[10]  = {"Gas / diesel + redline point", "Warn when water temp reaches", "Brightness, auto light, flip, demo",
                                    OBD_LINK_CAN ? "Direct CAN status (GPIO22/27)" : OBD_LINK_C3 ? "Pair / forget the C3 at OBD port" : "Connect / pair the OBD2 adapter", "Read & clear fault codes (DTC)", "Gear ratios for the gear display",
                                    "Max / min records: trip & all time", "Fuel type (E20/E85...) + AFR trim", "Show / hide each page", "Alarm beeps on SPEAK connector"};
uint16_t MN_BG, MN_LINE, MN_BADGE, MN_HI;
void mnColors() {
  MN_BG = C_BG; MN_LINE = lcd.color565(30, 34, 62); MN_BADGE = lcd.color565(40, 46, 90); MN_HI = lcd.color565(34, 40, 82);
}
void mnValue(int i, char *b, uint16_t *col) {                  // ค่าปัจจุบันที่แสดงด้านขวาของแต่ละข้อ
  *col = C_PINK; b[0] = 0;
  switch (i) {
    case 0: snprintf(b, 16, "%s %d", engDiesel ? "DSL" : "GAS", maxRpm); break;
    case 1: snprintf(b, 16, "%d C", tempAlert); break;
    case 2: if (briAuto) strcpy(b, "AUTO"); else snprintf(b, 16, "%d%%", briPct); if (demoMode) strcpy(b, "DEMO"); break;
    case 3: strcpy(b, curStatus()); *col = obdOk ? C_OK : C_WARN; break;
    case 5: snprintf(b, 16, "%d SPD", gearCount); break;
    case 8: { int on = 0, all = 0; for (int p = 0; p < N_PAGES; p++) { uint32_t o = pageOff; pageOff = 0; bool a = pageAvail(p); pageOff = o; if (a) { all++; if (!(pageOff & (1UL << p))) on++; } }
              snprintf(b, 16, "%d / %d", on, all); break; }
    case 9: if (sndOn) snprintf(b, 16, "ON  vol %d", sndVol); else { strcpy(b, "OFF"); *col = C_DIM; } break;
    case 7: if (afrOfs10) snprintf(b, 16, "%.1f %+.1f", afrSt10 / 10.0f, afrOfs10 / 10.0f); else snprintf(b, 16, "%.1f", afrSt10 / 10.0f); break;
  }
}
void mnRow(int i, bool hi) {
  int y = MN_HDR + i * MN_ROW;
  uint16_t bg = hi ? MN_HI : MN_BG;
  lcd.fillRect(0, y, 320, MN_ROW, bg);
  if (i < MN_N - 1) lcd.drawFastHLine(10, y + MN_ROW - 1, 300, MN_LINE);
  lcd.fillCircle(17, y + 12, 9, MN_BADGE);                     // หมายเลขข้อ
  char nb[3]; snprintf(nb, sizeof(nb), "%d", i + 1);
  lcd.setFont(&fonts::Font2); lcd.setTextColor(TFT_WHITE); lcd.setTextDatum(lgfx::textdatum_t::middle_center);
  lcd.drawString(nb, 17, y + 12);
  lcd.setTextDatum(lgfx::textdatum_t::top_left);
  lcd.setFont(&fonts::FreeSansBold9pt7b); lcd.setTextColor(TFT_WHITE);
  lcd.drawString(MN_TITLE[i], 34, y);
  lcd.setFont(&fonts::Font0); lcd.setTextColor(C_DIM);
  lcd.drawString(MN_DESC[i], 35, y + 15);
  char v[16]; uint16_t vc; mnValue(i, v, &vc);
  lcd.setTextDatum(lgfx::textdatum_t::middle_right);
  if (v[0]) { lcd.setFont(&fonts::Font2); lcd.setTextColor(vc); lcd.drawString(v, 300, y + 7); }
  lcd.setFont(&fonts::Font2); lcd.setTextColor(C_DIM); lcd.drawString(">", 314, y + 12);
  lcd.setTextDatum(lgfx::textdatum_t::top_left);
}
void mnHeader() {
  lcd.fillRect(0, 0, 320, MN_HDR, MN_BG);
  lcd.fillRoundRect(150, 2, 40, 4, 2, C_DIM);                   // ที่จับ (แบบแผ่นเลื่อนขึ้น)
  lcd.setFont(&fonts::FreeSansBold12pt7b); lcd.setTextColor(C_PINK);
  lcd.drawString("SETTINGS", 10, 4);
  lcd.setFont(&fonts::Font0); lcd.setTextColor(C_DIM); lcd.setTextDatum(lgfx::textdatum_t::top_right);
  lcd.drawString("swipe down to close", 314, 9);
  lcd.setTextDatum(lgfx::textdatum_t::top_left);
  lcd.drawFastHLine(0, MN_HDR - 1, 320, MN_LINE);
}
void drawMenuPage() {
  mnHeader(); for (int i = 0; i < MN_N; i++) mnRow(i, false);
  int yEnd = MN_HDR + MN_N * MN_ROW;                             // ล้างส่วนที่เหลือด้านล่าง (ปุ่มของหน้าที่เพิ่งออกมา)
  if (yEnd < 240) lcd.fillRect(0, yEnd, 320, 240 - yEnd, MN_BG);
}
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

// ---- AFR SCALE: เลือกชนิดน้ำมัน (ค่า stoich) + ปรับละเอียดได้ทุกค่า + ชดเชยให้ตรงกับเกจไวด์แบนด์ ----
const char *const AFR_PRE_N[6] = {"GAS", "E10", "E20", "E85", "LPG", "CNG"};
const int AFR_PRE_V[6] = {147, 141, 138, 98, 155, 172};
void afrScreen() {
  auto valRow = [&](int y, const char *lab, const char *v) {
    lcd.fillRect(70, y, 180, 44, C_BG);
    lcd.setFont(&fonts::Font0); lcd.setTextColor(C_DIM); lcd.setTextDatum(lgfx::textdatum_t::top_center);
    lcd.drawString(lab, 160, y + 1);
    lcd.setFont(&fonts::FreeSansBold18pt7b); lcd.setTextColor(TFT_WHITE); lcd.setTextDatum(lgfx::textdatum_t::middle_center);
    lcd.drawString(v, 160, y + 27);
    lcd.setTextDatum(lgfx::textdatum_t::top_left);
  };
  auto drawPre = [&]() {
    for (int i = 0; i < 6; i++) drawSmallBtn(8 + i * 51, 102, 47, 30, AFR_PRE_N[i], AFR_PRE_V[i] == afrSt10 ? C_OK : C_DIM);
  };
  auto drawAll = [&]() {
    char b[16];
    snprintf(b, sizeof(b), "%.1f", afrSt10 / 10.0f);  valRow(54, "STOICH (AFR at lambda 1.00)", b);
    snprintf(b, sizeof(b), "%+.1f", afrOfs10 / 10.0f); valRow(140, "TRIM (+/- AFR to match gauge)", b);
    drawPre();
  };
  lcd.fillScreen(C_BG);
  lcd.setFont(&fonts::FreeSansBold12pt7b); lcd.setTextColor(C_PINK); lcd.drawString("AFR SCALE", 10, 6);
  lcd.setFont(&fonts::Font2); lcd.setTextColor(C_DIM); lcd.drawString("AFR = lambda x STOICH + TRIM", 10, 32);
  drawBtn(8, 54, 56, 44, "-", C_PINK);  drawBtn(256, 54, 56, 44, "+", C_PINK);
  drawBtn(8, 140, 56, 44, "-", C_PINK); drawBtn(256, 140, 56, 44, "+", C_PINK);
  drawBtn(196, 202, 116, 34, "DONE", C_OK);
  drawSmallBtn(8, 202, 90, 34, "RESET", C_WARN);
  drawAll();
  waitRelease();
  uint32_t pressT = 0, lastRep = 0, lastLive = 0;
  while (true) {
    int32_t x, y;
    uint32_t now = millis();
    if (getTouchXY(&x, &y)) {
      int dS = inRect(x, y, 8, 54, 56, 44) ? -1 : (inRect(x, y, 256, 54, 56, 44) ? 1 : 0);
      int dO = inRect(x, y, 8, 140, 56, 44) ? -1 : (inRect(x, y, 256, 140, 56, 44) ? 1 : 0);
      if (dS || dO) {                                           // แตะ = ทีละ 0.1  กดค้าง = เปลี่ยนต่อเนื่อง
        bool go = false;
        if (pressT == 0) { pressT = now; lastRep = now; go = true; }
        else if (now - pressT > 450 && now - lastRep > 80) { lastRep = now; go = true; }
        if (go) {
          if (dS) afrSt10 = constrain(afrSt10 + dS, 50, 200);
          if (dO) afrOfs10 = constrain(afrOfs10 + dO, -30, 30);
          drawAll();
        }
      } else if (y >= 102 && y < 132 && pressT == 0) {
        pressT = now;
        for (int i = 0; i < 6; i++) if (inRect(x, y, 8 + i * 51, 102, 47, 30)) { afrSt10 = AFR_PRE_V[i]; drawAll(); }
      } else if (inRect(x, y, 196, 202, 116, 34)) { waitRelease(); break; }
      else if (inRect(x, y, 8, 202, 90, 34) && pressT == 0) { pressT = now; afrSt10 = 147; afrOfs10 = 0; drawAll(); }
    } else pressT = 0;
    if (now - lastLive > 300) {                                 // ค่าสดจากรถ: lambda -> AFR ที่จะขึ้นจอ
      lastLive = now;
      char b[40];
      if (metricHave[M_AFR] && afrLambda > 0) snprintf(b, sizeof(b), "now  L %.2f = AFR %.1f", (float)afrLambda, afrLambda * afrSt10 / 10.0f + afrOfs10 / 10.0f);
      else strcpy(b, "now  -- (no A/F data)");
      lcd.fillRect(0, 186, 320, 14, C_BG);
      lcd.setFont(&fonts::Font2); lcd.setTextColor(C_TXT); lcd.drawString(b, 10, 186);
    }
    delay(10);
  }
  prefs.putInt("afrSt", afrSt10);
  prefs.putInt("afrOf", afrOfs10);
}

// ---- SOUND: เปิด/ปิดเสียงทั้งหมด, ความดัง, เปิดรายเรื่อง + ปุ่ม TEST ----
void soundScreen() {
  const int Y0 = 34, RH = 29;                                       // แถว: 0 = ALL SOUND, 1 = VOLUME, 2-6 = แต่ละเรื่อง
  auto pill = [&](int y, bool on) { drawSmallBtn(160, y + 3, 70, 23, on ? "ON" : "OFF", on ? C_OK : C_DIM); };
  auto row = [&](int r) {
    int y = Y0 + r * RH;
    lcd.fillRect(0, y, 320, RH, C_BG);
    lcd.drawFastHLine(8, y + RH - 1, 304, lcd.color565(30, 34, 62));
    lcd.setFont(&fonts::Font2); lcd.setTextColor(r == 0 ? TFT_WHITE : C_TXT); lcd.setTextDatum(lgfx::textdatum_t::middle_left);
    if (r == 0) lcd.drawString("ALL SOUND", 10, y + 14);
    else if (r == 1) lcd.drawString("VOLUME", 10, y + 14);
    else lcd.drawString(SND_NAME[r - 2], 10, y + 14);
    lcd.setTextDatum(lgfx::textdatum_t::top_left);
    if (r == 0) pill(y, sndOn);
    else if (r == 1) {
      drawSmallBtn(160, y + 3, 34, 23, "-", C_PINK); drawSmallBtn(278, y + 3, 34, 23, "+", C_PINK);
      for (int v = 1; v <= 5; v++) lcd.fillRect(200 + (v - 1) * 15, y + 22 - v * 3, 11, v * 3 + 2, v <= sndVol ? C_OK : lcd.color565(50, 54, 80));
    } else {
      pill(y, sndMask & (1 << (r - 2)));
      drawSmallBtn(240, y + 3, 72, 23, "TEST", C_PINK);
    }
  };
  lcd.fillScreen(C_BG);
  lcd.setFont(&fonts::FreeSansBold12pt7b); lcd.setTextColor(C_PINK); lcd.drawString("SOUND", 10, 6);
  drawSmallBtn(236, 3, 76, 26, "DONE", C_OK);
  for (int r = 0; r < 7; r++) row(r);
  waitRelease();
  const SNote *TP[SND_N] = {P_SHIFT, P_TEMP, P_BATT, P_LOST, P_CLICK};
  const int TN[SND_N] = {2, 5, 3, 2, 1};
  while (true) {
    int32_t x, y;
    if (getTouchXY(&x, &y)) {
      if (inRect(x, y, 236, 3, 76, 26)) { waitRelease(); break; }
      int r = (y - Y0) / RH, ry = Y0 + r * RH;
      if (y >= Y0 && r < 7) {
        if (r == 0 && inRect(x, y, 160, ry, 70, RH)) sndOn = !sndOn;
        else if (r == 1 && inRect(x, y, 160, ry, 34, RH)) { sndVol = max(1, sndVol - 1); sndPlay(SND_SHIFT, P_VOL, 1, 9, true); }
        else if (r == 1 && inRect(x, y, 278, ry, 34, RH)) { sndVol = min(5, sndVol + 1); sndPlay(SND_SHIFT, P_VOL, 1, 9, true); }
        else if (r >= 2 && inRect(x, y, 160, ry, 70, RH)) sndMask ^= (1 << (r - 2));
        else if (r >= 2 && inRect(x, y, 240, ry, 72, RH)) {          // TEST: เล่นเสียงนั้น (SHIFT เล่นต่อกัน ~1 วินาที)
          int k = r - 2;
          if (k == SND_SHIFT) { for (int i = 0; i < 10; i++) { sndPlay(k, TP[k], TN[k], 9, true); delay(100); } }
          else sndPlay(k, TP[k], TN[k], 9, true);
        }
        row(r);
        waitRelease();
      }
    }
    delay(10);
  }
  prefs.putBool("snd", sndOn); prefs.putInt("sndMask", sndMask); prefs.putInt("sndVol", sndVol);
}

// ---- ENGINE / REDLINE: เลือกเบนซิน/ดีเซล + ตั้ง redline (ช่วงตามชนิดเครื่อง ทีละ 100) ----
void engineScreen() {
  const uint16_t SEL = C_OK, OFF = lcd.color565(60, 64, 96);
  auto drawType = [&]() {
    drawSmallBtn(10, 40, 146, 34, "GASOLINE", engDiesel ? OFF : SEL);
    drawSmallBtn(164, 40, 146, 34, "DIESEL", engDiesel ? SEL : OFF);
    lcd.fillRect(0, 160, 320, 30, C_BG);
    lcd.setFont(&fonts::Font0); lcd.setTextColor(C_DIM, C_BG); lcd.setTextDatum(lgfx::textdatum_t::top_center);
    char r[64]; snprintf(r, sizeof(r), "range %d - %d, step 100 (hold to repeat)", rlMin(), rlMax()); lcd.drawString(r, 160, 162);
    lcd.drawString(engDiesel ? "TPS = accel pedal (PID 49), AFR lambda up to 8" : "TPS = throttle (PID 11), AFR lambda up to 1.9", 160, 176);
    lcd.setTextDatum(lgfx::textdatum_t::top_left);
  };
  auto drawVal = [&]() {
    lcd.fillRect(76, 90, 168, 64, C_BG);
    lcd.setFont(&fonts::FreeSansBold24pt7b); lcd.setTextColor(TFT_WHITE); lcd.setTextDatum(lgfx::textdatum_t::middle_center);
    char b[12]; snprintf(b, sizeof(b), "%d", maxRpm); lcd.drawString(b, 160, 114);
    lcd.setFont(&fonts::Font2); lcd.setTextColor(C_DIM); lcd.drawString("redline rpm", 160, 144);
    lcd.setTextDatum(lgfx::textdatum_t::top_left);
  };
  lcd.fillScreen(C_BG);
  lcd.setFont(&fonts::FreeSansBold12pt7b); lcd.setTextColor(C_PINK); lcd.drawString("ENGINE / REDLINE", 10, 8);
  drawBtn(8, 92, 60, 54, "-", C_PINK); drawBtn(252, 92, 60, 54, "+", C_PINK);
  drawBtn(90, 196, 140, 40, "DONE", C_OK);
  drawType(); drawVal();
  waitRelease();
  bool wasDiesel = engDiesel;
  uint32_t pressT = 0, lastRep = 0;
  while (true) {
    int32_t x, y;
    if (getTouchXY(&x, &y)) {
      uint32_t now = millis();
      bool tg = inRect(x, y, 10, 40, 146, 34), td = inRect(x, y, 164, 40, 146, 34);
      if ((tg && engDiesel) || (td && !engDiesel)) {                 // เปลี่ยนชนิดเครื่อง: redline ไปค่าเริ่มต้นของชนิดนั้น
        engDiesel = td; maxRpm = engDiesel ? DSL_RL_DEF : GAS_RL_DEF;
        drawType(); drawVal(); waitRelease(); continue;
      }
      int d = inRect(x, y, 8, 92, 60, 54) ? -100 : (inRect(x, y, 252, 92, 60, 54) ? 100 : 0);
      if (d) {
        if (pressT == 0) { pressT = now; lastRep = now; maxRpm = constrain(maxRpm + d, rlMin(), rlMax()); drawVal(); }
        else if (now - pressT > 450 && now - lastRep > 90) { lastRep = now; maxRpm = constrain(maxRpm + d, rlMin(), rlMax()); drawVal(); }
      } else if (inRect(x, y, 90, 196, 140, 40)) { waitRelease(); break; }
    } else pressT = 0;
    delay(10);
  }
  if (engDiesel != wasDiesel) {                                       // ค่า STOICH ตามน้ำมัน (ถ้ายังเป็นค่าเริ่มต้นของอีกชนิด)
    if (engDiesel && afrSt10 == 147) afrSt10 = 145;
    else if (!engDiesel && afrSt10 == 145) afrSt10 = 147;
    prefs.putInt("afrSt", afrSt10);
    fastTps = pidSupported(tpsPid());                                 // ช่อง TPS เปลี่ยน PID (อ่านแยกจนกว่าจะต่อ OBD ใหม่)
    if (fastMode) fastHas[M_TPS] = false;
    metricHave[M_TPS] = false;
  }
  prefs.putBool("eng", engDiesel); prefs.putInt("maxRpm", maxRpm);
}

// ---- PAGES: เปิด/ปิดแต่ละหน้า (ปัดซ้าย/ขวาจะข้ามหน้าที่ปิด) ----
void pagesScreen() {
  int ids[N_PAGES], n = 0;
  uint32_t o = pageOff; pageOff = 0;
  for (int p = 0; p < N_PAGES; p++) if (pageAvail(p)) ids[n++] = p;  // หน้าที่มีอยู่จริงตามลำดับที่เห็น
  pageOff = o;
  auto btn = [&](int i) {
    char t[16]; snprintf(t, sizeof(t), "PAGE %d", i + 1);
    bool on = !(pageOff & (1UL << ids[i]));
    drawSmallBtn(8 + (i % 3) * 104, 34 + (i / 3) * 34, 96, 30, t, on ? C_OK : lcd.color565(60, 64, 96));
  };
  lcd.fillScreen(C_BG);
  lcd.setFont(&fonts::FreeSansBold12pt7b); lcd.setTextColor(C_PINK); lcd.setTextDatum(lgfx::textdatum_t::top_left);
  lcd.drawString("PAGES", 10, 6);
  lcd.setFont(&fonts::Font0); lcd.setTextColor(C_DIM); lcd.drawString("green = shown, tap to hide/show", 100, 13);
  for (int i = 0; i < n; i++) btn(i);
  drawSmallBtn(214, 206, 98, 30, "DONE", C_OK);
  waitRelease();
  while (true) {
    int32_t x, y;
    if (getTouchXY(&x, &y)) {
      if (inRect(x, y, 214, 206, 98, 30)) { waitRelease(); break; }
      for (int i = 0; i < n; i++) if (inRect(x, y, 8 + (i % 3) * 104, 34 + (i / 3) * 34, 96, 30)) {
        uint32_t nv = pageOff ^ (1UL << ids[i]);
        int left = 0; for (int k = 0; k < n; k++) if (!(nv & (1UL << ids[k]))) left++;
        if (left > 0) { pageOff = nv; btn(i); }                       // ต้องเหลืออย่างน้อย 1 หน้า
        waitRelease();
      }
    }
    delay(10);
  }
  prefs.putUInt("pgoff", pageOff);
  if (!pageAvail(page)) { page = nextPage(page); prefs.putInt("page", page); }   // หน้าที่เปิดอยู่ถูกปิด -> ไปหน้าถัดไป
}

void menuScreen() {
  hFree();                                               // คืนแรมพื้นหลังหน้า 9
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
          case 0: engineScreen(); break;
          case 1: valueScreen("COOLANT ALERT", "Water temp number blinks red at this value", &tempAlert, 70, 130, 1, "deg C");
                  prefs.putInt("tempAlert", tempAlert); break;
          case 2: leave = displayScreen(); break;           // เริ่มเดโม = ออกจากเมนูเลย
          case 3: btScreen(); break;
          case 4: dtcScreen(); break;
          case 5: gearScreen(); break;
          case 6: peaksScreen(); break;
          case 7: afrScreen(); break;
          case 8: pagesScreen(); break;
          case 9: soundScreen(); break;
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
#if OBD_LINK_CAN
  bool wokeFromSleep = esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED;
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {   // ตื่นตามเวลา: ถาม ECU เงียบ ๆ (จอยังดับ) ไม่ตอบ = หลับต่อ
    canBootMs = millis();
    bool ok = false;
    if (canBegin()) { char r[40]; for (int k = 0; k < 3 && !ok; k++) { ok = elmBuf("0100", r, sizeof(r), 200) > 0 && strstr(r, "4100"); if (!ok) delay(150); } }
    if (!ok) { canEnd(); canSleepNow(); }
  }
  gpio_hold_dis((gpio_num_t)21); gpio_hold_dis((gpio_num_t)26); gpio_deep_sleep_hold_dis();   // ปล่อยขาที่ล็อกไว้ตอนหลับ
  esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);  // ไม่ใช้ Bluetooth เลย: คืนแรมทั้งหมดของ Bluetooth
  if (wokeFromSleep) Serial.printf("wake from sleep (cause %d)\n", (int)esp_sleep_get_wakeup_cause());
#elif OBD_LINK_C3
  esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);   // ใช้แต่ BLE (ต่อ C3): คืนแรมส่วน Bluetooth Classic
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
  engDiesel = prefs.getBool("eng", false);
  maxRpm    = constrain(prefs.getInt("maxRpm", DEF_MAX_RPM), 2000, MAX_RPM_LIMIT);
  if (prefs.getInt("rlv", 0) < 1) {                        // ครั้งแรกของเวอร์ชันนี้: ตั้ง redline เป็น 5000 (ค่าเก่าที่เคยจำไว้ถูกแทน)
    maxRpm = DEF_MAX_RPM; prefs.putInt("maxRpm", maxRpm); prefs.putInt("rlv", 1);
  }
  maxRpm = constrain(maxRpm, rlMin(), rlMax());                  // อยู่ในช่วงของชนิดเครื่องเสมอ
  pkLoad();
  if (!prefs.getBool("rpm75", false)) { prefs.putBool("rpm75", true); if (maxRpm == 6500) { maxRpm = 7500; prefs.putInt("maxRpm", maxRpm); } }   // ครั้งเดียว: ค่าเดิมของโรงงาน 6500 -> 7500 ให้ตรงกับรถ
  tempAlert = prefs.getInt("tempAlert", DEF_TEMP_ALERT);
  afrSt10  = constrain(prefs.getInt("afrSt", 147), 50, 200);
  afrOfs10 = constrain(prefs.getInt("afrOf", 0), -30, 30);
  sndOn   = prefs.getBool("snd", true);
  sndMask = prefs.getInt("sndMask", 0x0F) & 0x1F;
  sndVol  = constrain(prefs.getInt("sndVol", 3), 1, 5);
  sndBegin();
  briPct    = constrain(prefs.getInt("bri", 80), 10, 100);
  briAuto   = prefs.getBool("briAuto", false);
  btMac     = prefs.getString("btMac", "");
  btName    = prefs.getString("btName", "");
  String pin = prefs.getString("btPin", DEF_PIN);
  strncpy(btPin, pin.c_str(), sizeof(btPin) - 1);
  btPin[sizeof(btPin) - 1] = 0;
  pageOff = prefs.getUInt("pgoff", 0);
  page = constrain(prefs.getInt("page", 0), 0, N_PAGES - 1);
  if (!pageAvail(page)) page = nextPage(page);
  tripKm = prefs.getFloat("trip", 0.0f);
  for (int pi = 0; pi < 5; pi++) {                         // หน้า GIF: สีตัวเลขของแต่ละหน้า
    char k[4] = {'q', 'h', (char)('0' + pi), 0}; qHue[pi] = constrain(prefs.getInt(k, QC_HUE_DEF), 0, 359);
    k[1] = 't'; qSat[pi] = constrain(prefs.getInt(k, QC_SAT_DEF), 0, 100);
    k[1] = 'l'; qLay[pi] = constrain(prefs.getInt(k, 0), 0, 1);
    k[1] = 'z'; qZm[pi] = constrain(prefs.getInt(k, 100), 100, 300);
    k[1] = 'x'; qPx[pi] = constrain(prefs.getInt(k, 0), -100, 100);
    k[1] = 'y'; qPy[pi] = constrain(prefs.getInt(k, 0), -100, 100);
  }
  { const int QD[3] = {M_COOLANT, M_IAT, M_BATT};                   // หน้า GIF: ค่าใน 3 ช่องขวา
    for (int pi = 0; pi < Q_NP; pi++) {
      volatile int *qs = qSlots[pi];
      for (int i = 0; i < 3; i++) {
        char k[5] = {'q', 's', (char)('0' + i), 0, 0};
        if (pi) { k[1] = (char)('0' + pi); k[2] = 's'; k[3] = (char)('0' + i); }
        qs[i] = constrain(prefs.getInt(k, QD[i]), 0, Q_NM - 1);
      }
      if (qs[0] == qs[1] || qs[0] == qs[2] || qs[1] == qs[2]) for (int i = 0; i < 3; i++) qs[i] = QD[i];
    } }
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
#if OBD_LINK_CAN
    if (wokeFromSleep) fastBoot = true;          // ตื่นจากหลับ (สตาร์ทรถ/แตะจอ) = ข้ามหน้าเปิดเครื่อง ขึ้นหน้าปัดทันที
#endif
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

#if OBD_LINK_C3
  NimBLEDevice::init("Didissnap-CYD");
  c3pair::begin("CYD");
  NimBLEDevice::setMTU(DIDIBLE_MTU);
#elif OBD_LINK_CAN
  canBootMs = millis();
  canBegin();
#else
#if LINK_WIFI
  linkMode = constrain(prefs.getInt("lnk", 0), 0, 2);
  wifiPhase = (linkMode != 1);                   // AUTO / WIFI ONLY: เริ่มที่ WiFi ก่อน (Bluetooth เปิดทีหลังถ้าจำเป็น)
  if (!wifiPhase) SerialBT.begin("ESP32-OBD", true);
#else
  SerialBT.begin("ESP32-OBD", true);            // master
#endif
#endif
  xTaskCreatePinnedToCore(obdTask, "obd", 8192, NULL, 1, NULL, 0);
  Serial.printf("after Bluetooth: heap %u (largest block %u)\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
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

// ---- ตรวจเงื่อนไขเตือน แล้วสั่งเสียง (เรียกทุกเฟรมจาก loop) ----
void alertTick(uint32_t now) {
  static bool lastObd = false, hot = false, low = false;
  static uint32_t nextHot = 0, lowSince = 0, nextLow = 0, lastShift = 0;
  if (obdOk != lastObd) {                                         // ต่อ OBD ติด / หลุด
    lastObd = obdOk;
    if (obdOk) SND_PLAY(SND_OBD, P_CONN, 2); else SND_PLAY(SND_OBD, P_LOST, 2);
  }
  if (demoMode || !obdOk) { hot = low = false; lowSince = 0; return; }
  // น้ำร้อน: บี๊บยาว 3 ครั้งตอนเริ่มร้อน แล้วเตือนซ้ำทุก 30 วินาทีจนกว่าจะเย็นลง 3 องศา
  if (metricHave[M_COOLANT] && coolant >= tempAlert) {
    if (!hot) { hot = true; SND_PLAY(SND_TEMP, P_TEMP, 4); nextHot = now + 30000; }
    else if ((int32_t)(now - nextHot) >= 0) { SND_PLAY(SND_TEMP, P_TEMP1, 4); nextHot = now + 30000; }
  } else if (hot && coolant <= tempAlert - 3) hot = false;
  // แบตต่ำ: ต่ำกว่า 11.8 V ติดต่อกัน 5 วินาที (ไม่เตือนตอนสตาร์ท) แล้วเตือนซ้ำทุก 60 วินาที
  if (metricHave[M_BATT] && metricVal[M_BATT] < 11.8f) {
    if (!lowSince) lowSince = now;
    if (!low && now - lowSince > 5000) { low = true; SND_PLAY(SND_BATT, P_BATT, 3); nextLow = now + 60000; }
    else if (low && (int32_t)(now - nextLow) >= 0) { SND_PLAY(SND_BATT, P_BATT, 3); nextLow = now + 60000; }
  } else if (!metricHave[M_BATT] || metricVal[M_BATT] > 12.0f) { lowSince = 0; low = false; }
  // ถึงรอบ redline: บี๊บสั้นถี่ ๆ ตลอดที่ยังเกิน
  if (rpm >= maxRpm && now - lastShift >= 100 && !sndBusy()) { lastShift = now; SND_PLAY(SND_SHIFT, P_SHIFT, 1); }
}

#if OBD_LINK_CAN
// ---- หลับ: ปิดไฟจอ/จอ/CAN แล้ว deep sleep  ตื่นเมื่อมีสัญญาณบนสาย CAN (GPIO27 ลง LOW) หรือแตะจอ (GPIO36) ----
void canGoSleep() {
  canSleepReq = false;
  if (digitalRead(CAN_RX_PIN) == LOW) return;                 // สายยังมีสัญญาณอยู่ ไม่หลับ
  Serial.println("CAN bus quiet -> sleep");
  for (int i = 9; i >= 0; i--) { setBriF(i / 10.0f); delay(30); }
  obdPause = true;
  uint32_t t = millis();
  while (!obdIdle && millis() - t < 3000) delay(10);
  canEnd();
  lcd.sleep();
  gpio_reset_pin((gpio_num_t)21); gpio_set_direction((gpio_num_t)21, GPIO_MODE_OUTPUT); gpio_set_level((gpio_num_t)21, 0);   // ไฟจอดับสนิทตอนหลับ
  gpio_reset_pin((gpio_num_t)26); gpio_set_direction((gpio_num_t)26, GPIO_MODE_OUTPUT); gpio_set_level((gpio_num_t)26, 0);   // ช่องเสียงเงียบ
  gpio_hold_en((gpio_num_t)21); gpio_hold_en((gpio_num_t)26); gpio_deep_sleep_hold_en();
  canSleepNow();
}
#endif

void loop() {
#if OBD_LINK_CAN
  if (canSleepReq) canGoSleep();
#endif
  int32_t x, y;
  uint32_t now = millis();
  if (getTouchXY(&x, &y)) {
    tLast = now;
    if (!tDown) { tDown = true; tSx = x; tSy = y; tStart = now; }
    tLx = x; tLy = y;
    if (isQPage(page) && now - tStart > 900 && abs(tLx - tSx) < 15 && abs(tLy - tSy) < 15 && (qBig() ? tSy > 172 : (tSx < 200 && tSy > 140))) {
      tDown = false;                                     // หน้า GIF: กดค้างที่ตัวเลขความเร็ว/รอบ = เลือกสีตัวเลขของหน้านี้
      qColorScreen(qPageIdx(page));
      waitRelease();
      return;
    }
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
    } else if (isQPage(page) && !qBig() && abs(dx) < 20 && abs(dy) < 20 && tSx >= Q_RX - 4) {
      qTap(tSx, tSy);                                     // หน้า GIF: แตะช่องขวา = สลับค่า
    } else if (isHPage(page) && abs(dx) < 20 && abs(dy) < 20 && hTap(tSx, tSy)) {
      // หน้า 9: แตะช่องล่าง = สลับค่า
    } else if (page == PAGE_DASH && abs(dx) < 20 && abs(dy) < 20 && inRect(tSx, tSy, 3, 55, 100, 54)) {
      tripKm = 0; demoTrip = 0; prefs.putFloat("trip", 0.0f);     // แตะช่อง TRIP = รีเซ็ตระยะทริป
      pTxt[1][0] = 0;
    }
  }
  updateBrightness();
  alertTick(now);
  if (demoMode) demoTick(now);
  updateUI();
  {                                                      // เก็บระยะทริปลง NVS ทุก 60 วินาที (เฉพาะเมื่อเปลี่ยน)
    static uint32_t lastTripSave = 0; static float savedTrip = 0;
    if (now - lastTripSave > 60000) { lastTripSave = now; if (fabsf(tripKm - savedTrip) >= 0.1f) { prefs.putFloat("trip", tripKm); savedTrip = tripKm; } }
  }
  heapWatch(now);
  delay(1);
}
