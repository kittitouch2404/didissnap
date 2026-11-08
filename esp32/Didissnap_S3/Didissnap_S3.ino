/*
  Didissnap_S3 — เฟิร์มแวร์หน้าปัดสำหรับ Panlee ZX4D30NE01S (ESP32-S3 + จอ 4.3" 480x272)

  ตอนนี้ (เฟส 1):
    - หน้าปัด 16 หน้า: 1-8 แบบพรีวิว, 9-12 ยกมาจากแอป (Red stripe, ECU, Record, ตาราง OBD2), 13-16 หน้ารูป
    - ปุ่ม 2 ปุ่ม + เมนูบนจอ (ปุ่ม 1 = RXD1/IO41, ปุ่ม 2 = RXD2/IO2 หรือ IO1)
    - WiFi "Didissnap" (12345678): ส่งค่าให้แอป, หน้าเว็บตั้งค่า 192.168.4.1, รับรูปจากแอป
    - หน้า 13-15 ใช้รูปที่ส่งจากแอปได้ (JPG/GIF) หน้า 16 สลับรูปตามรอบ
    - หน้า Record: ปุ่ม 2 กดค้าง = ARM / STOP
    - ค่ารถ: โหมดจำลอง (DEMO) — เฟส 2 จะต่อ Vgate iCar Pro BLE จริง

  ไฟล์ในโฟลเดอร์นี้ต้องอยู่ครบ: lcd.h gfx.h fonts.h data.h pages.h menu.h bg.h bimg.h
                                Didissnap*.h partitions.csv
  Library: "AnimatedGIF" (Larry Bank) — ลงจาก Library Manager
  Arduino IDE (สำคัญทุกข้อ):
    Board: ESP32S3 Dev Module · Flash Mode: DIO 80MHz · Flash Size: 16MB · PSRAM: OPI PSRAM
    USB CDC On Boot: Enabled · Partition Scheme: 16M Flash (3MB APP/9.9MB FATFS) หรือ Custom
    (ตัวที่ใช้จริงคือ partitions.csv ในโฟลเดอร์นี้ — core หยิบไปใช้เองเสมอ)
*/
#include <Arduino.h>
#include <Preferences.h>
#include "esp_system.h"
#define DIDI_TX_POWER WIFI_POWER_8_5dBm   // กำลังส่ง WiFi ต่ำลง กินไฟน้อย (มือถืออยู่ในรถใกล้ ๆ)
#include "DidissnapLink.h"
#include "DidissnapConfig.h"
#include "DidissnapButtons.h"
#include "DidissnapPics.h"
#include "lcd.h"
#include "gfx.h"
#include "data.h"
#include "bg.h"
#include "pages.h"
#include "menu.h"
#include "perf.h"

#define PIN_BTN1 41      // แพด RXD1
#define PIN_BTN2 2       // แพด RXD2 (ทดสอบรอบแรกขึ้น IO1 เลยอ่านทั้งสองขา)
#define PIN_BTN2_ALT 1

Preferences prefs;
static bool demo = true;
static bool wifiOn = true;
static int lastBri = -1;

void resetPeaks() { data::resetPeaks(); }
void resetTrip() { D.trip = 0; }
void onButton(int b, bool lng) { menu::button(b, lng); }
void onHold2() {
  if (didicfg::cfg.page == pages::PG_RECORD) { perf::toggle(RAW.spd); menu::toast(perf::state == perf::READY || perf::state == perf::WAIT ? "ARMED" : "STOP"); }
}

// กลับจอ 180° (หมุนทั้งเฟรม)
static void flip180(uint16_t* p) {
  uint16_t *a = p, *b = p + LCD_W * LCD_H - 1;
  while (a < b) { uint16_t t = *a; *a++ = *b; *b-- = t; }
}

static void bootLogo() {
  uint16_t* fb = lcd::back();
  bg::decodeJpeg(BI_LOGO, sizeof(BI_LOGO), fb);
  lcd::present();
  for (int i = 0; i <= 100; i += 4) { lcd::brightness(i * didicfg::cfg.bri / 100); delay(12); }
  delay(1200);
}

// เลือกรูปพื้นหลังตามหน้าที่แสดง
static void chooseBackground(int page) {
  using namespace didipics;
  bg::runSpeed = D.spd;
  if (page >= pages::PG_PIC && page <= pages::PG_PHOTO) {
    String p = slotPath(page - pages::PG_PIC);
    if (p.length()) bg::select(bg::FILE_PIC, p, version);
    else bg::select(page == pages::PG_PIC ? bg::BI_JPG9 : page == pages::PG_RUN ? bg::BI_RUN_ANIM : bg::BI_JPG11);
  } else if (page == pages::PG_RPM) {
    String n = forRpm(D.rpm, didicfg::cfg.maxRpm);
    if (n.length()) bg::select(bg::FILE_PIC, path(n), version);
    else bg::select(bg::NONE);
  } else bg::select(bg::NONE);
}

// บอกขั้นตอนตอนเปิดเครื่อง + จำขั้นที่ทำอยู่ข้ามการรีเซ็ต (ถ้าดับ จะรู้ว่าดับตรงไหน)
//  ถ้าดับที่ขั้นเดิม 2 รอบติด → รอบต่อไปข้ามขั้นนั้น (safe mode) แล้วขึ้นข้อความบนจอ
RTC_NOINIT_ATTR uint32_t g_magic, g_step, g_crashStep, g_crashCount;
static const char* const STEP_NAME[] = {"", "prefs", "lcd", "wifi", "config + web", "boot logo", "pictures (LittleFS)",
                                        "background task", "ready", "first frames"};
static int crashedAt = 0;          // ขั้นที่รอบที่แล้วดับ (0 = ไม่ได้ดับ)
static const char* crashWhy = "";
static bool skip(int n) { return crashedAt == n && g_crashCount >= 2; }
static void step(int n, const char* what) {
  g_magic = 0xD1D1;
  g_step = n;
  Serial.printf("[boot %d] %s\n", n, what);
  Serial.flush();
}
static void checkLastBoot() {
  esp_reset_reason_t r = esp_reset_reason();
  bool bad = r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT || r == ESP_RST_BROWNOUT;
  crashWhy = r == ESP_RST_BROWNOUT ? "BROWNOUT (power too weak)" : r == ESP_RST_PANIC ? "PANIC (crash)" : bad ? "WATCHDOG (hang)" : "";
  if (g_magic == 0xD1D1 && bad && g_step >= 1 && g_step <= 9) {
    crashedAt = g_step;
    if (g_crashStep == g_step && g_crashCount < 100) g_crashCount++; else { g_crashStep = g_step; g_crashCount = 1; }
  } else { g_crashStep = 0; g_crashCount = 0; }
  if (r == ESP_RST_POWERON) { g_crashStep = 0; g_crashCount = 0; crashedAt = 0; }
}
// ขึ้นข้อความบนจอว่ารอบก่อนดับตรงไหน (ถ่ายรูปส่งมาได้เลย)
static void showCrash() {
  if (!crashedAt) return;
  gfx::G = lcd::back();
  gfx::clear(0);
  gfx::text("LAST BOOT CRASHED", 240, 70, 26, gfx::rgb(0xFF2A2A), gfx::CENTER, 900);
  char b[64];
  snprintf(b, sizeof(b), "at step %d: %s", crashedAt, STEP_NAME[crashedAt]);
  gfx::text(b, 240, 112, 19, 0xFFFF, gfx::CENTER, 700);
  gfx::text(crashWhy, 240, 142, 16, gfx::rgb(0xFFC400), gfx::CENTER, 700);
  snprintf(b, sizeof(b), "%u time(s) in a row%s", (unsigned)g_crashCount, g_crashCount >= 2 ? " -> SAFE MODE: step skipped" : "");
  gfx::text(b, 240, 172, 14, gfx::rgb(0xAAAAAA), gfx::CENTER, 600);
  gfx::text("Take a photo and send it to Claude", 240, 230, 13, gfx::rgb(0x888888), gfx::CENTER, 600);
  lcd::present();
  lcd::brightness(80);
  delay(4000);
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 1500) delay(10);
  checkLastBoot();
  Serial.printf("\n=== Didissnap_S3 === reset reason %d\n", (int)esp_reset_reason());
  if (crashedAt) Serial.printf("    รอบที่แล้วดับที่ [boot %d] %s (%s) ติดกัน %u ครั้ง\n", crashedAt, STEP_NAME[crashedAt], crashWhy, (unsigned)g_crashCount);
  Serial.printf("    PSRAM %u KB  flash %u MB\n", ESP.getPsramSize() / 1024, ESP.getFlashChipSize() >> 20);
  step(1, "prefs");
  prefs.begin("dash", false);
  step(2, "lcd");
  if (!lcd::begin()) Serial.println("LCD init FAILED (เช็ค PSRAM = OPI PSRAM)");
  showCrash();

  step(3, "wifi");                                  // ต้องเปิด WiFi ก่อนเว็บเซิร์ฟเวอร์ (ไม่งั้น crash)
  if (skip(3)) { Serial.println("SAFE MODE: skip WiFi"); wifiOn = false; }
  else if (!didilink::begin()) { Serial.println("WiFi FAILED"); wifiOn = false; }
  step(4, "config + web");
  didicfg::onPeaksReset = resetPeaks;
  didicfg::onTripReset = resetTrip;
  if (wifiOn) didicfg::begin(prefs, pages::NAMES, pages::N);   // โหลดค่าตั้ง + เว็บตั้งค่า
  else { didicfg::prefs = &prefs; didicfg::pageNames = pages::NAMES; didicfg::pageCount = pages::N; didicfg::load(); }
  step(5, "boot logo");
  if (!skip(5)) bootLogo();
  else lcd::brightness(didicfg::cfg.bri);

  step(6, "pictures (LittleFS - first boot formats, may take ~10 s)");
  if (!skip(6) && !LittleFS.begin(false)) {        // ยังไม่เคยเตรียมพื้นที่รูป → ฟอร์แมต (ครั้งแรกครั้งเดียว ~1 นาที)
    gfx::G = lcd::back();
    gfx::clear(0);
    gfx::text("PREPARING PICTURE STORAGE", 240, 120, 20, gfx::rgb(0xFF7A00), gfx::CENTER, 900);
    gfx::text("first boot only - about 1 minute", 240, 150, 14, 0xFFFF, gfx::CENTER, 600);
    gfx::text("do not unplug", 240, 175, 14, gfx::rgb(0xAAAAAA), gfx::CENTER, 600);
    lcd::present();
    lcd::brightness(80);
    Serial.println("    formatting LittleFS (first boot) ...");
    uint32_t f0 = millis();
    LittleFS.begin(true);
    Serial.printf("    format done in %u s\n", (unsigned)((millis() - f0) / 1000));
  }
  if (skip(6)) Serial.println("SAFE MODE: skip pictures");
  else if (!didipics::begin()) Serial.println("LittleFS FAILED (เช็คว่ามี partitions.csv ในโฟลเดอร์)");
  step(7, "background task");
  if (skip(7) || skip(6)) Serial.println("SAFE MODE: skip background task");
  else if (!bg::begin()) Serial.println("bg buffers FAILED");
  menu::onPeaks = resetPeaks;
  menu::onTrip = resetTrip;
  menu::onHold2 = onHold2;
  perf::begin();
  didibtn::onEvent = onButton;
  didibtn::begin(PIN_BTN1, PIN_BTN2, -1, PIN_BTN2_ALT);
  if (skip(9)) didicfg::cfg.page = 0;   // ดับตอนวาดหน้าที่เลือกไว้ → เริ่มหน้า 1
  step(9, "first frames");
  Serial.printf("Didissnap_S3 ready  heap %u  psram %u\n", ESP.getFreeHeap(), ESP.getFreePsram());
}

static uint32_t lastUs = 0, fpsT = 0;
static int frames = 0;

void loop() {
  uint32_t us = micros();
  float dt = lastUs ? fminf(.1f, (us - lastUs) / 1e6f) : .016f;
  lastUs = us;

  if (wifiOn) {
    didilink::loop();
    didicfg::loop();   // หน้าเว็บ + สลับหน้าอัตโนมัติ
  }
  didibtn::loop();

  D.link = demo ? 0 : 2;
  data::update(dt, demo);
  perf::sample(RAW.spd);
  if (wifiOn) didilink::send(D.rpm, D.spd, D.ect, D.iat, D.volt, D.load, D.tps, D.map, D.maf, D.fuel);

  const auto& c = didicfg::cfg;
  int bri = c.briAuto ? 85 : c.bri;
  if (bri != lastBri) { lcd::brightness(bri); lastBri = bri; }

  chooseBackground(menu::open ? -1 : c.page);
  gfx::G = lcd::back();
  if (menu::open) menu::draw();
  else pages::draw(c.page);
  menu::drawToast();
  if (c.flip) flip180(gfx::G);
  lcd::present();

  frames++;
  if (g_magic && millis() > 8000) { g_magic = 0; g_crashCount = 0; Serial.println("[boot] running OK"); }
  if (millis() - fpsT >= 5000) {
    Serial.printf("fps %.1f  page %d  heap %u  psram %u\n", frames / 5.f, c.page + 1, ESP.getFreeHeap(), ESP.getFreePsram());
    frames = 0;
    fpsT = millis();
  }
}
