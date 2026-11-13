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
// 1 = วาด 2 คอร์ · 0 = คอร์เดียว
#define DIDI_2CORE 1
#include <Arduino.h>
#include <Preferences.h>
#include "esp_system.h"
#define DIDI_MAX_FILE (4 * 1024 * 1024)   // รูป/GIF ไฟล์ละไม่เกิน 4 MB (โหลดลง PSRAM ตอนแสดง)
#define DIDI_TX_POWER WIFI_POWER_8_5dBm   // กำลังส่ง WiFi ต่ำลง กินไฟน้อย (มือถืออยู่ในรถใกล้ ๆ)
#include "DidissnapLink.h"
#include "obdble.h"
#include "didinow_s3.h"
#include "DidissnapConfig.h"
#include "DidissnapButtons.h"
#include "DidissnapPics.h"
#include "esp_core_dump.h"
#pragma GCC push_options
#pragma GCC optimize("O2")   // เฉพาะส่วนวาดภาพ: เน้นความเร็ว (Arduino ตั้ง -Os ไว้)
#include "lcd.h"
#include "gfx.h"
#include "data.h"
#include "bg.h"
#include "pages.h"
#include "menu.h"
#include "perf.h"
#include "bootanim.h"
#pragma GCC pop_options

#define PIN_BTN1 41      // แพด RXD1
#define PIN_BTN2 2       // แพด RXD2 (ทดสอบรอบแรกขึ้น IO1 เลยอ่านทั้งสองขา)
#define PIN_BTN2_ALT 1

Preferences prefs;
static bool demo = true;
static bool wifiOn = true;
static bool obdOn = false;   // ต่อตัวอ่าน OBD แบบ BLE (เมนู OBD BLE)
static int lastBri = -1;

void resetPeaks() { data::resetPeaks(); }
void resetTrip() { D.trip = 0; }
void onButton(int b, bool lng) { menu::button(b, lng); }
void onHold2() {
  if (didicfg::cfg.page == pages::PG_RECORD) { perf::toggle(RAW.spd); menu::toast(perf::state == perf::READY || perf::state == perf::WAIT ? "ARMED" : "STOP"); }
}

// วาดทีละแถบลง RAM ภายในชิป (เร็ว) แล้วคัดลอกลง PSRAM
// (ถ้าวาดตรงลง PSRAM ทั้งจอทุกเฟรม จอจะแย่ง PSRAM ไม่ทัน → ภาพเลื่อน/ซ้อน)
// วาดพร้อมกัน 2 คอร์: คอร์ 1 (loop) กับงาน "draw2" บนคอร์ 0 แย่งกันหยิบแถบที่ยังไม่ได้วาด
#define STRIP_H 68                       // 272 / 68 = 4 แถบ
#define N_STRIPS (LCD_H / STRIP_H)
static uint16_t* stripBuf = nullptr;
static uint16_t* stripBuf2 = nullptr;
// คัดลอกแถบที่วาดเสร็จลงบัฟเฟอร์หลัง ทีละแถบเล็ก 4 แถว (ข้ามแถบที่เหมือนเดิม)
static void copyStrip(int y0, bool flip, const uint16_t* src) {
  for (int r = 0; r < STRIP_H; r += lcd::BAND_H) {
    int band = (y0 + r) / lcd::BAND_H;
    lcd::putBand(flip ? lcd::NB - 1 - band : band, src + r * LCD_W, flip);
  }
}

static uint32_t perfDraw = 0;
static int perfFps = 0, perfLcd = 0, perfMax = 0, perfFlash = 0;   // perfFlash = หยุดจอเพราะใช้แฟลช (ครั้ง/วินาที)

static volatile int nextStrip = N_STRIPS;
static uint16_t* volatile jobFb = nullptr;
static volatile bool jobFlip = false, jobMenu = false;
static volatile int jobPage = 0;
static SemaphoreHandle_t workGo = nullptr, workDone = nullptr;

static void drawStrip(int k, uint16_t* sb) {
  int y0 = k * STRIP_H;
  gfx::strip(sb, y0, y0 + STRIP_H);
  if (jobMenu) menu::draw();
  else pages::draw(jobPage);
  menu::drawToast();
  if (k == N_STRIPS - 1 && menu::perfInfo) {     // ตัวเลข fps มุมล่างซ้าย (เมนู PERF INFO) — รายละเอียดเต็มดูใน Serial
    char pb[48];
    snprintf(pb, sizeof(pb), "%d fps  draw %d ms  %dcore", perfFps, (int)(perfDraw / 1000), (workGo && menu::drawCores == 2) ? 2 : 1);
    gfx::fillA(0, LCD_H - 14, gfx::textWidth(pb, 11, 600) + 8, 14, 0, 180);
    gfx::text(pb, 4, LCD_H - 3, 11, gfx::rgb(0x00E05A), gfx::LEFT, 600);
  }
  copyStrip(y0, jobFlip, sb);
}
static void renderStrips(uint16_t* sb) {
  int k;
  while ((k = __atomic_fetch_add(&nextStrip, 1, __ATOMIC_SEQ_CST)) < N_STRIPS) drawStrip(k, sb);
}
static void drawWorker(void*) {
  for (;;) {
    xSemaphoreTake(workGo, portMAX_DELAY);
    renderStrips(stripBuf2);
    xSemaphoreGive(workDone);
  }
}
// วาดทั้งเฟรมลง fb
static void renderFrame(uint16_t* fb, int page, bool menuOpen, bool flip) {
  jobFb = fb;
  jobPage = page;
  jobMenu = menuOpen;
  jobFlip = flip;
  gfx::frameMs = millis();
  pages::rfTick((int)lroundf(D.rpm));
  __atomic_store_n(&nextStrip, 0, __ATOMIC_SEQ_CST);
  bool two = workGo && menu::drawCores == 2;
  if (two) xSemaphoreGive(workGo);
  renderStrips(stripBuf);
  if (two) xSemaphoreTake(workDone, portMAX_DELAY);
}

// อัตรารีเฟรชจอตามเมนู REFRESH (ตั้งตอนเปิดเครื่อง — เปลี่ยนในเมนูแล้วเครื่องรีสตาร์ทเอง)
static const int LCD_HZ[4] = {44, 30, 60, 60}, LCD_EVERY[4] = {1, 1, 2, 1};
static uint32_t restartAt = 0;
static void applyPerfOpt() {
  gfx::fontsFrom(menu::fontMem == 1);
}
static void onPerfOpt() {
  applyPerfOpt();
  prefs.putInt("cores", menu::drawCores);
  prefs.putInt("fontMem", menu::fontMem);
  prefs.putInt("perf", menu::perfInfo);
  prefs.putInt("copySpd", (int)lcd::copyMBps);
}
static void onBounce(int v) {
  prefs.putInt("bounce", v);
  menu::toast("RESTARTING...");
  restartAt = millis() + 1200;
}
static void onObd(int v) {
  prefs.putInt("obdBle", v & 1);
  menu::toast("RESTARTING...");
  restartAt = millis() + 1200;
}
static void onObdForget() {
  obdble::forget();
  menu::toast("OBD FORGOTTEN");
}
static const char* obdStatus() { return obdOn ? obdble::status() : "off"; }
static void onLcdMode(int m) {
  prefs.putInt("lcdMode", m & 3);
  menu::toast("RESTARTING...");
  restartAt = millis() + 1200;
}

// แอนิเมชันตอนเปิดเครื่อง (bootanim.h ~6 วินาที) — กดปุ่มไหนก็ได้เพื่อข้าม
static void bootFlush(const uint16_t* buf) {
  lcd::paceCopy(lcd::back(), buf, LCD_W * LCD_H * 2);
  lcd::presentFull();
}
static bool bootSkip() {
  return digitalRead(PIN_BTN1) == LOW || digitalRead(PIN_BTN2) == LOW || digitalRead(PIN_BTN2_ALT) == LOW;
}
static void bootLogo() {
  if (!lcd::ok()) return;
  pinMode(PIN_BTN1, INPUT_PULLUP);
  pinMode(PIN_BTN2, INPUT_PULLUP);
  pinMode(PIN_BTN2_ALT, INPUT_PULLUP);
  delay(5);
  bool held = bootSkip();                 // ปุ่มค้างอยู่ตั้งแต่เปิดเครื่อง (หรือขาลอย) → ไม่ใช้เป็นปุ่มข้าม
  lcd::brightness(didicfg::cfg.bri);
  bootanim::play(bootFlush, held ? nullptr : bootSkip);
  lcd::flush();
}

// เลือกรูปพื้นหลังตามหน้าที่แสดง
static void chooseBackground(int page) {
  using namespace didipics;
  bg::runSpeed = D.spd;
  if (page == pages::PG_PIC) bg::select(bg::BI_FACE);   // หน้า 13 = ตัวละครเปลี่ยนหน้าตามรอบ
  else if (page >= pages::PG_PIC && page <= pages::PG_PHOTO) {
    String p = slotPath(page - pages::PG_PIC);
    if (p.length()) bg::select(bg::FILE_PIC, p, version);
    else bg::select(bg::NONE);                       // ไม่มีรูปที่อัป = พื้นดำ + ข้อความ (pages::noPics)
  } else if (page == pages::PG_RPM) {
    String n = forRpm(D.rpm, didicfg::cfg.maxRpm);
    if (n.length()) bg::select(bg::FILE_PIC, path(n), version);
    else bg::select(bg::NONE);
  } else bg::select(bg::NONE);
}

// บอกขั้นตอนตอนเปิดเครื่อง + จำขั้นที่ทำอยู่ข้ามการรีเซ็ต (ถ้าดับ จะรู้ว่าดับตรงไหน)
//  ถ้าดับที่ขั้นเดิม 2 รอบติด → รอบต่อไปข้ามขั้นนั้น (safe mode) แล้วขึ้นข้อความบนจอ
RTC_NOINIT_ATTR uint32_t g_magic, g_step, g_crashStep, g_crashCount;
static const char* const STEP_NAME[] = {"", "prefs", "picture storage (format)", "lcd", "wifi", "config + web", "boot logo",
                                        "pictures (LittleFS)", "background task", "2-core draw + refresh", "first frames"};
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
  crashWhy = r == ESP_RST_BROWNOUT ? "BROWNOUT (power too weak)" : r == ESP_RST_PANIC ? "PANIC (crash)"
           : r == ESP_RST_INT_WDT ? "WATCHDOG (interrupt)" : r == ESP_RST_TASK_WDT ? "WATCHDOG (task)" : bad ? "WATCHDOG (other)" : "";
  if (g_magic == 0xD1D1 && bad && g_step >= 1 && g_step <= 10) {
    crashedAt = g_step;
    if (g_crashStep == g_step && g_crashCount < 100) g_crashCount++; else { g_crashStep = g_step; g_crashCount = 1; }
  } else { g_crashStep = 0; g_crashCount = 0; }
  if (r == ESP_RST_POWERON) { g_crashStep = 0; g_crashCount = 0; crashedAt = 0; }
}
// ขึ้นข้อความบนจอว่ารอบก่อนดับตรงไหน (ถ่ายรูปส่งมาได้เลย)
static esp_core_dump_summary_t cd;
static bool cdOk = false;
static void readCoredump() {
  memset(&cd, 0, sizeof(cd));
  cdOk = esp_core_dump_get_summary(&cd) == ESP_OK;
  if (!cdOk) return;
  Serial.printf("    coredump: task %s cause %u pc 0x%08x vaddr 0x%08x\n    backtrace:", cd.exc_task, (unsigned)cd.ex_info.exc_cause,
                (unsigned)cd.exc_pc, (unsigned)cd.ex_info.exc_vaddr);
  for (unsigned i = 0; i < cd.exc_bt_info.depth && i < 16; i++) Serial.printf(" 0x%08x", (unsigned)cd.exc_bt_info.bt[i]);
  Serial.println();
}
static void showCrash() {
  if (!crashedAt || !lcd::ok()) return;
  gfx::target(lcd::back());
  gfx::clear(0);
  gfx::text("LAST BOOT CRASHED", 240, 60, 26, gfx::rgb(0xFF2A2A), gfx::CENTER, 900);
  char b[64];
  snprintf(b, sizeof(b), "at step %d: %s", crashedAt, STEP_NAME[crashedAt]);
  gfx::text(b, 240, 98, 19, 0xFFFF, gfx::CENTER, 700);
  gfx::text(crashWhy, 240, 124, 16, gfx::rgb(0xFFC400), gfx::CENTER, 700);
  snprintf(b, sizeof(b), "%u time(s) in a row%s", (unsigned)g_crashCount, g_crashCount >= 2 ? " -> SAFE MODE: step skipped" : "");
  gfx::text(b, 240, 150, 14, gfx::rgb(0xAAAAAA), gfx::CENTER, 600);
  if (cdOk) {                                         // ข้อมูลจาก coredump: งานไหนพัง ที่ตำแหน่งไหน
    snprintf(b, sizeof(b), "task %s  cause %u  pc %08x  addr %08x", cd.exc_task, (unsigned)cd.ex_info.exc_cause, (unsigned)cd.exc_pc, (unsigned)cd.ex_info.exc_vaddr);
    gfx::text(b, 240, 198, 11, gfx::rgb(0x8FD3FF), gfx::CENTER, 600);
    char bt[120] = "bt";
    for (unsigned i = 0; i < cd.exc_bt_info.depth && i < 7; i++) { char t[12]; snprintf(t, sizeof(t), " %08x", (unsigned)cd.exc_bt_info.bt[i]); strlcat(bt, t, sizeof(bt)); }
    gfx::text(bt, 240, 214, 10, gfx::rgb(0x8FD3FF), gfx::CENTER, 500);
  }
  gfx::text("Take a photo and send it to Claude", 240, 240, 13, gfx::rgb(0x888888), gfx::CENTER, 600);
  lcd::presentFull();
  lcd::brightness(80);
  delay(cdOk ? 8000 : 4000);
  esp_core_dump_image_erase();
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 1500) delay(10);
  checkLastBoot();
  Serial.printf("\n=== Didissnap_S3 === reset reason %d\n", (int)esp_reset_reason());
  if (crashedAt) readCoredump();
  if (crashedAt) Serial.printf("    รอบที่แล้วดับที่ [boot %d] %s (%s) ติดกัน %u ครั้ง\n", crashedAt, STEP_NAME[crashedAt], crashWhy, (unsigned)g_crashCount);
  Serial.printf("    PSRAM %u KB  flash %u MB\n", ESP.getPsramSize() / 1024, ESP.getFlashChipSize() >> 20);
  step(1, "prefs");
  prefs.begin("dash", false);

  // เตรียมพื้นที่รูป "ก่อน" เปิดจอ: ตอนลบแฟลชก้อนใหญ่ถ้าจอทำงานอยู่ จะค้าง (WATCHDOG interrupt)
  step(2, "picture storage");
  bool fsOk = false;
  if (!skip(2)) {
    fsOk = LittleFS.begin(false);
    if (!fsOk) {
      Serial.println("    formatting LittleFS (first boot only, ~1 min, screen stays dark) ...");
      Serial.flush();
      uint32_t f0 = millis();
      disableCore0WDT();
      disableCore1WDT();
      fsOk = LittleFS.begin(true);
      enableCore0WDT();
      Serial.printf("    format %s in %u s\n", fsOk ? "done" : "FAILED", (unsigned)((millis() - f0) / 1000));
    }
  } else Serial.println("SAFE MODE: skip picture storage");

  gfx::copyFontsToPsram();
  step(3, "lcd");
  bool safeLcd = skip(3);                           // ดับตอนเปิดจอ 2 รอบติด → แบบเดิมทั้งหมด
  bool safeDraw = safeLcd || skip(9) || skip(10);   // ดับตอนวาด 2 รอบติด → วาดคอร์เดียว + 44 Hz
  menu::lcdMode = safeDraw ? 0 : (prefs.getInt("lcdMode", 0) & 3);
  if (safeLcd) Serial.println("SAFE MODE: LCD without presenter task");
  menu::bounceMode = safeLcd ? 0 : (prefs.getInt("bounce", 1) & 1);
  menu::onBounce = onBounce;
  if (!lcd::begin(LCD_HZ[menu::lcdMode], LCD_EVERY[menu::lcdMode], !safeLcd, menu::bounceMode)) Serial.println("LCD init FAILED (เช็ค PSRAM = OPI PSRAM)");
  showCrash();

  step(4, "wifi");                                  // ต้องเปิด WiFi ก่อนเว็บเซิร์ฟเวอร์ (ไม่งั้น crash)
  if (skip(4)) { Serial.println("SAFE MODE: skip WiFi"); wifiOn = false; }
  else if (!didilink::begin()) { Serial.println("WiFi FAILED"); wifiOn = false; }
  if (wifiOn) didinow::begin();                     // ส่งค่ารถให้จอ CYD ทาง ESP-NOW
  step(5, "config + web");
  didicfg::onPeaksReset = resetPeaks;
  didicfg::onTripReset = resetTrip;
  if (wifiOn) didicfg::begin(prefs, pages::NAMES, pages::N);   // โหลดค่าตั้ง + เว็บตั้งค่า
  else { didicfg::prefs = &prefs; didicfg::pageNames = pages::NAMES; didicfg::pageCount = pages::N; didicfg::load(); }
  step(6, "boot logo");
  if (!skip(6)) bootLogo();
  else lcd::brightness(didicfg::cfg.bri);

  step(7, "pictures");
  bool picsOn = fsOk && !skip(7);
  if (!picsOn) Serial.println("pictures OFF");
  else if (!didipics::begin()) { Serial.println("LittleFS FAILED (เช็คว่ามี partitions.csv ในโฟลเดอร์)"); picsOn = false; }
  stripBuf = (uint16_t*)heap_caps_malloc(LCD_W * STRIP_H * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!stripBuf) { Serial.println("strip buffer: no internal RAM -> PSRAM"); stripBuf = (uint16_t*)ps_malloc(LCD_W * STRIP_H * 2); }
  step(8, "background task");
  if (!picsOn || skip(8)) Serial.println("background task OFF");
  else if (!bg::begin()) Serial.println("bg buffers FAILED");
  menu::onPeaks = resetPeaks;
  menu::onTrip = resetTrip;
  menu::onHold2 = onHold2;
  step(9, "2-core draw + refresh");
  if (!safeDraw && DIDI_2CORE) stripBuf2 = (uint16_t*)heap_caps_malloc(LCD_W * STRIP_H * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (safeDraw) Serial.println("SAFE MODE: draw on 1 core, 44 Hz");
  else if (stripBuf2) {                                  // วาด 2 คอร์
    workGo = xSemaphoreCreateBinary();
    workDone = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(drawWorker, "draw2", 8192, nullptr, 3, nullptr, 0);
  } else Serial.println("2nd strip buffer: no internal RAM -> draw on 1 core");
  menu::onLcdMode = onLcdMode;
  menu::drawCores = safeDraw ? 1 : constrain(prefs.getInt("cores", 2), 1, 2);
  menu::fontMem = 0;   // ทดสอบแล้ว PSRAM/แฟลชเร็วเท่ากัน
  menu::perfInfo = prefs.getInt("perf", 0) & 1;   // ตัวเลข fps มุมล่างซ้าย (เมนู PERF INFO)
  lcd::copyMBps = constrain(prefs.getInt("copySpd", PSRAM_COPY_MBPS), 10, 80);
  menu::onPerfOpt = onPerfOpt;
  applyPerfOpt();
  perf::begin();
  didibtn::onEvent = onButton;
  didibtn::begin(PIN_BTN1, PIN_BTN2, -1, PIN_BTN2_ALT);
  // ตัวอ่าน OBD แบบ BLE (เริ่มหลังจองแรมจอ/วาดเสร็จ เพราะ Bluetooth ใช้ RAM ภายในเยอะ)
  menu::obdMode = prefs.getInt("obdBle", 1) & 1;
  menu::onObd = onObd;
  menu::onObdForget = onObdForget;
  menu::obdStatus = obdStatus;
  menu::dtcStart = [](int c) { return obdOn && obdble::startDtc(c); };
  menu::dtcPoll = [](char* b, size_t n) { return obdOn ? obdble::pollDtc(b, n) : 0; };
  step(11, "obd ble");
  if (skip(11)) Serial.println("SAFE MODE: skip OBD BLE");
  else if (menu::obdMode) {
    obdOn = true;
    obdble::begin(&prefs);
    didilink::onDtcRead = obdble::readDtc;
    didilink::onDtcClear = obdble::clearDtc;
  }
  if (skip(10)) didicfg::cfg.page = 0;   // ดับตอนวาดหน้าที่เลือกไว้ → เริ่มหน้า 1
  step(10, "first frames");
  Serial.printf("Didissnap_S3 ready  heap %u (internal %u)  psram %u  draw cores %d\n", ESP.getFreeHeap(), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), ESP.getFreePsram(), (workGo && menu::drawCores == 2) ? 2 : 1);
}

static uint32_t lastUs = 0, fpsT = 0, drawAcc = 0, perfT = 0, lcdF0 = 0, frameT0 = 0;
static int frames = 0, perfFrames = 0, maxAcc = 0;

void loop() {
  if (restartAt && millis() > restartAt) ESP.restart();
  uint32_t us = micros();
  float dt = lastUs ? fminf(.1f, (us - lastUs) / 1e6f) : .016f;
  lastUs = us;

  if (wifiOn) {
    didilink::loop();
    didicfg::loop();   // หน้าเว็บ + สลับหน้าอัตโนมัติ
  }
  didibtn::loop();
  didicfg::tick();   // บันทึกค่าตั้งลงแฟลชทีหลัง

  if (obdOn) {                       // มีตัวอ่าน OBD: ใช้ค่าจริง (ยังไม่ได้ค่า = รอบ/ความเร็ว 0)
    demo = false;
    bool lv = obdble::live();
    if (!lv) { RAW.rpm = 0; RAW.spd = 0; }
    D.link = lv ? 2 : 1;
    menu::obdLive = lv;
  } else D.link = demo ? 0 : 2;
  data::update(dt, demo);
  if (wifiOn) {                      // ESP-NOW: ส่งค่าให้จอ CYD + ตอบคำขอ DTC
    didinow::loop((uint8_t)D.link);
    uint16_t id;
    uint8_t cmd = didinow::takeCmd(&id);
    if (cmd) {
      bool run = obdOn && obdble::state == obdble::RUNNING;
      if (cmd == DN_CMD_DTC_READ) didinow::reply(cmd, id, run, run ? obdble::readDtc() : String("[]"));
      else if (cmd == DN_CMD_DTC_CLEAR) didinow::reply(cmd, id, run && obdble::clearDtc(), String("[]"));
    }
  }
  perf::sample(RAW.spd);
  if (wifiOn) didilink::send(D.rpm, D.spd, D.ect, D.iat, D.volt, D.load, D.tps, D.map, D.maf, D.fuel);

  const auto& c = didicfg::cfg;
  int bri = c.briAuto ? 85 : c.bri;
  if (bri != lastBri) { lcd::brightness(bri); lastBri = bri; }

  chooseBackground(menu::open ? -1 : c.page);
  uint32_t tr0 = micros();
  renderFrame(lcd::back(), c.page, menu::open, c.flip);
  uint32_t tr1 = micros();
  lcd::present();
  drawAcc += tr1 - tr0;
  // หน้าที่เล่น GIF: วาดไม่เกิน 20 fps (GIF เองก็ 20 fps) — ลดงาน PSRAM ไม่ให้จอวูบ
  if (bg::gifPlaying && !menu::open) {
    uint32_t el = (micros() - tr0) / 1000;
    if (el < 50) delay(50 - el);
  }
  int ft = frameT0 ? (int)((tr0 - frameT0) / 1000) : 0;   // เวลาระหว่างเฟรม (หาอาการกระตุก)
  frameT0 = tr0;
  if (ft > maxAcc) maxAcc = ft;

  frames++;
  perfFrames++;
  if (millis() - perfT >= 1000) {
    perfFps = perfFrames;
    perfDraw = perfFrames ? drawAcc / perfFrames : 0;
    perfLcd = lcd::frames - lcdF0;
    perfMax = maxAcc;
    static uint32_t fl0 = 0;
    perfFlash = (int)(lcd::flashPauses - fl0);
    fl0 = lcd::flashPauses;
    maxAcc = 0;
    lcdF0 = lcd::frames;
    perfFrames = 0;
    drawAcc = 0;
    perfT = millis();
  }
  if (g_magic && millis() > 8000) { g_magic = 0; g_crashCount = 0; Serial.println("[boot] running OK"); }
  if (millis() - fpsT >= 5000) {
    Serial.printf("fps %.1f  draw %u ms  max %d ms  lcd %d Hz  bands %u/s  flash-pause %u  page %d  heap %u  psram %u\n", frames / 5.f, (unsigned)(perfDraw / 1000), perfMax, perfLcd, (unsigned)(lcd::bandsCopied / 5), (unsigned)lcd::flashPauses, c.page + 1, ESP.getFreeHeap(), ESP.getFreePsram());
    lcd::bandsCopied = 0;
    frames = 0;
    fpsT = millis();
  }
}
