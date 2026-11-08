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
    USB CDC On Boot: Enabled · Partition Scheme: Custom (ใช้ partitions.csv ในโฟลเดอร์นี้)
*/
#include <Arduino.h>
#include <Preferences.h>
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

void setup() {
  Serial.begin(115200);
  prefs.begin("dash", false);
  if (!lcd::begin()) Serial.println("LCD init FAILED (เช็ค PSRAM = OPI PSRAM)");

  didicfg::onPeaksReset = resetPeaks;
  didicfg::onTripReset = resetTrip;
  didicfg::begin(prefs, pages::NAMES, pages::N);   // โหลดค่าตั้ง (ความสว่าง กลับจอ ฯลฯ)
  bootLogo();

  if (!didilink::begin()) Serial.println("WiFi FAILED");
  if (!didipics::begin()) Serial.println("LittleFS FAILED (เช็ค Partition Scheme = Custom)");
  if (!bg::begin()) Serial.println("bg buffers FAILED");
  menu::onPeaks = resetPeaks;
  menu::onTrip = resetTrip;
  menu::onHold2 = onHold2;
  perf::begin();
  didibtn::onEvent = onButton;
  didibtn::begin(PIN_BTN1, PIN_BTN2, -1, PIN_BTN2_ALT);
  Serial.printf("Didissnap_S3 ready  heap %u  psram %u\n", ESP.getFreeHeap(), ESP.getFreePsram());
}

static uint32_t lastUs = 0, fpsT = 0;
static int frames = 0;

void loop() {
  uint32_t us = micros();
  float dt = lastUs ? fminf(.1f, (us - lastUs) / 1e6f) : .016f;
  lastUs = us;

  didilink::loop();
  didicfg::loop();     // หน้าเว็บ + สลับหน้าอัตโนมัติ
  didibtn::loop();

  D.link = demo ? 0 : 2;
  data::update(dt, demo);
  perf::sample(RAW.spd);
  didilink::send(D.rpm, D.spd, D.ect, D.iat, D.volt, D.load, D.tps, D.map, D.maf, D.fuel);

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
  if (millis() - fpsT >= 5000) {
    Serial.printf("fps %.1f  page %d  heap %u  psram %u\n", frames / 5.f, c.page + 1, ESP.getFreeHeap(), ESP.getFreePsram());
    frames = 0;
    fpsT = millis();
  }
}
