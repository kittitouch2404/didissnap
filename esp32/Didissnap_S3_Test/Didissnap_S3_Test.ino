/*
  Didissnap_S3_Test v4 — ทดสอบบอร์ด Panlee ZX4D30NE01S (ESP32-S3 + จอ 4.3" 480x272 RGB)
  ** ไม่ต้องลง Library เพิ่ม ** ใช้ไดรเวอร์จอ RGB (esp_lcd) ที่มากับ ESP32 core 3.x
  ค่าขา/timing เหมือนโค้ดของผู้ผลิตทุกตัว: github.com/wireless-tag-com/ZX4D30NE01S

  v4: แยกขั้นตอนให้ห่างกัน เพื่อดูว่าบอร์ดดับ (เสียง USB เด้ง) ตอนไหน
   ขั้น A (0-6 วิ)  : จอดำ ไฟจอปิด ยังไม่ทำอะไร       → ถ้าเด้งช่วงนี้ = ไม่ใช่โค้ด (ไฟ/สาย/บอร์ด)
   ขั้น B (6-12 วิ) : เปิดไฟจอ 30% ค้างไว้             → ถ้าเด้งตอนไฟจอติด = ไฟเลี้ยงไม่พอ
   ขั้น C (12-18 วิ): เปิดไฟจอ 100% ค้างไว้
   ขั้น D           : เริ่มจอ RGB แล้วเต็มจอทีละสี     → ถ้าเด้งตอนนี้ = ต้องแก้โค้ดจอ
   บอร์ดจำได้ว่ารอบก่อนดับที่ขั้นไหน และพิมพ์บอกใน Serial Monitor ตอนเปิดรอบถัดไป
  ทำอะไร
   0) ไฟจอกะพริบ 3 ครั้ง แล้วเต็มจอ แดง → เขียว → น้ำเงิน → ขาว (ดูขั้นตอนใน Serial Monitor 115200)
   1) หน้าทดสอบ: แถบสี + กรอบขอบจอ + แถบวิ่ง + fps
   2) ตาราง GPIO สด: เอาสายแตะแพด (RXD2 / TXD2) กับ GND → ช่องที่เป็น LOW สีแดง = เลข GPIO ของแพดนั้น
   3) แตะ IO0 กับ GND ค้าง = วนความสว่าง 100 / 50 / 15 %

  Arduino IDE: Board = ESP32S3 Dev Module · Flash Size = 16MB · PSRAM = OPI PSRAM
               USB CDC On Boot = Disabled (ถ้า Serial Monitor เงียบ ลอง Enabled)
  ต้องใช้ ESP32 core 3.x (Boards Manager → esp32 by Espressif)
*/
#include <Arduino.h>
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "esp_cache.h"
#include "font6x10.h"
#include "esp_system.h"

RTC_NOINIT_ATTR uint32_t g_magic, g_stage;   // จำขั้นตอนข้ามการรีเซ็ต
void stage(uint32_t s) { g_magic = 0xD1D1; g_stage = s; Serial.printf("[stage %c]\n", (char)s); }

#define W 480
#define H 272
#define PIN_BL 45    // ไฟจอ (HIGH = ติด)
#define PIN_DISP 21  // LCD_DISP ใน datasheet (ลองตั้ง HIGH เผื่อบอร์ดรุ่นนี้ต้องใช้)

static uint16_t* fb = nullptr;   // เฟรมบัฟเฟอร์ใน PSRAM (RGB565)
static esp_lcd_panel_handle_t panel = nullptr;

const uint16_t C_BLACK = 0x0000, C_WHITE = 0xFFFF, C_RED = 0xF800, C_GREEN = 0x07E0, C_BLUE = 0x001F,
               C_ORANGE = 0xFBE0, C_GREY = 0x7BEF, C_DARK = 0x2104, C_YELLOW = 0xFFE0;

bool lcdBegin() {
  esp_lcd_rgb_panel_config_t c;
  memset(&c, 0, sizeof(c));
  c.clk_src = LCD_CLK_SRC_DEFAULT;
  c.timings.pclk_hz = 14000000;
  c.timings.h_res = W;
  c.timings.v_res = H;
  c.timings.hsync_pulse_width = 10;
  c.timings.hsync_back_porch = 80;
  c.timings.hsync_front_porch = 60;
  c.timings.vsync_pulse_width = 10;
  c.timings.vsync_back_porch = 20;
  c.timings.vsync_front_porch = 60;
  c.timings.flags.pclk_active_neg = 1;
  c.data_width = 16;
  c.dma_burst_size = 64;
  c.hsync_gpio_num = 47;
  c.vsync_gpio_num = 48;
  c.de_gpio_num = 40;
  c.pclk_gpio_num = 14;
  c.disp_gpio_num = -1;
  const int data[16] = {9, 10, 11, 12, 13,      // B0-B4
                        16, 17, 18, 8, 3, 46,   // G0-G5
                        4, 5, 6, 7, 15};        // R0-R4
  for (int i = 0; i < 16; i++) c.data_gpio_nums[i] = data[i];
  c.flags.fb_in_psram = 1;
  esp_err_t e = esp_lcd_new_rgb_panel(&c, &panel);
  if (e != ESP_OK) { Serial.printf("    esp_lcd_new_rgb_panel error %d (%s)\n", e, esp_err_to_name(e)); return false; }
  esp_lcd_panel_reset(panel);
  esp_lcd_panel_init(panel);
  void* p = nullptr;
  e = esp_lcd_rgb_panel_get_frame_buffer(panel, 1, &p);
  if (e != ESP_OK || !p) { Serial.printf("    get_frame_buffer error %d\n", e); return false; }
  fb = (uint16_t*)p;
  return true;
}

// เขียนจาก cache ลง PSRAM ให้จออ่านเห็น
void flush() { esp_cache_msync(fb, W * H * 2, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED); }

void rect(int x, int y, int w, int h, uint16_t col) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > W) w = W - x;
  if (y + h > H) h = H - y;
  for (int j = 0; j < h; j++) {
    uint16_t* r = fb + (y + j) * W + x;
    for (int i = 0; i < w; i++) r[i] = col;
  }
}
void frame(int x, int y, int w, int h, uint16_t c) { rect(x, y, w, 1, c); rect(x, y + h - 1, w, 1, c); rect(x, y, 1, h, c); rect(x + w - 1, y, 1, h, c); }

void text(int x, int y, const char* s, int sc, uint16_t col, int bg = -1) {
  for (; *s; s++, x += 6 * sc) {
    int ch = (unsigned char)*s;
    if (ch < 32 || ch > 126) ch = '?';
    const uint8_t* g = FONT6x10[ch - 32];
    for (int r = 0; r < 10; r++)
      for (int b = 0; b < 6; b++) {
        bool on = g[r] >> (5 - b) & 1;
        if (on) rect(x + b * sc, y + r * sc, sc, sc, col);
        else if (bg >= 0) rect(x + b * sc, y + r * sc, sc, sc, (uint16_t)bg);
      }
  }
}
void textf(int x, int y, int sc, uint16_t col, int bg, const char* f, ...) {
  char b[96];
  va_list a; va_start(a, f); vsnprintf(b, sizeof(b), f, a); va_end(a);
  text(x, y, b, sc, col, bg);
}

// ---- แพดที่น่าจะว่าง (ไม่ใช้กับจอ/PSRAM/USB)
const int PROBE[] = {0, 1, 2, 38, 39, 41, 42};
const int NP = sizeof(PROBE) / sizeof(PROBE[0]);
int last[NP], lows[NP];
int bri = 255;
void setBri(int v) { bri = v; ledcWrite(PIN_BL, v); }

void drawPin(int i, int v) {
  int x = 12 + (i % 4) * 116, y = 122 + (i / 4) * 54;
  rect(x + 1, y + 26, 106, 19, v ? C_DARK : C_RED);
  textf(x + 6, y + 28, 2, C_WHITE, -1, "%s %d", v ? "HIGH" : "LOW", lows[i]);
}

void drawStatic() {
  rect(0, 0, W, H, C_BLACK);
  const uint16_t bars[] = {C_RED, C_GREEN, C_BLUE, C_WHITE};   // ต้องเรียง แดง เขียว น้ำเงิน ขาว
  for (int i = 0; i < 4; i++) rect(i * 120, 0, 120, 40, bars[i]);
  frame(0, 0, W, H, C_ORANGE);                                   // กรอบขอบจอ ต้องเห็นครบ 4 ด้าน
  text(12, 50, "Didissnap S3 TEST", 3, C_ORANGE);
  textf(12, 84, 1, C_GREY, -1, "480x272  heap %u KB  psram %u KB", ESP.getFreeHeap() / 1024, ESP.getFreePsram() / 1024);
  text(12, 98, "Touch a pad to GND -> its GPIO turns RED", 1, C_GREY);
  for (int i = 0; i < NP; i++) {
    int x = 12 + (i % 4) * 116, y = 122 + (i / 4) * 54;
    frame(x, y, 108, 46, C_GREY);
    textf(x + 6, y + 5, 2, C_WHITE, -1, "IO%d", PROBE[i]);
    last[i] = -1;
  }
}

const char* why(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "POWER ON (เสียบไฟใหม่)";
    case ESP_RST_BROWNOUT: return "BROWNOUT (ไฟตก/ไฟไม่พอ)";
    case ESP_RST_PANIC: return "PANIC (โค้ดค้าง/crash)";
    case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT: case ESP_RST_WDT: return "WATCHDOG (ค้าง)";
    case ESP_RST_SW: return "SOFTWARE reset";
    case ESP_RST_EXT: return "EXTERNAL (ปุ่ม/ขา EN)";
    default: return "OTHER";
  }
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);   // รอ Serial Monitor (USB CDC)
  esp_reset_reason_t rr = esp_reset_reason();
  Serial.println("\n=== Didissnap S3 test v4 (esp_lcd) ===");
  Serial.printf("[0] reset reason: %s\n", why(rr));
  if (g_magic == 0xD1D1) Serial.printf("[0] รอบที่แล้วดับตอนขั้น %c\n", (char)g_stage);
  stage('A');
  delay(6000);
  pinMode(PIN_BL, OUTPUT);
  ledcAttach(PIN_BL, 5000, 8);
  stage('B');
  ledcWrite(PIN_BL, 77);
  delay(6000);
  stage('C');
  ledcWrite(PIN_BL, 255);
  delay(6000);
  stage('D');
  Serial.printf("[1] chip %s  flash %u MB  PSRAM %u KB\n", ESP.getChipModel(), ESP.getFlashChipSize() >> 20, ESP.getPsramSize() / 1024);
  if (ESP.getPsramSize() == 0) Serial.println("    !! ไม่เจอ PSRAM -> Tools > PSRAM ต้องเป็น OPI PSRAM");

  pinMode(PIN_DISP, OUTPUT);
  digitalWrite(PIN_DISP, HIGH);

  bool ok = lcdBegin();
  Serial.printf("[3] lcd init = %s  fb=%p  PSRAM free %u KB\n", ok ? "OK" : "FAILED", fb, ESP.getFreePsram() / 1024);
  setBri(255);
  if (!ok) return;

  const uint16_t full[] = {C_RED, C_GREEN, C_BLUE, C_WHITE};
  const char* nm[] = {"RED", "GREEN", "BLUE", "WHITE"};
  for (int i = 0; i < 4; i++) {
    rect(0, 0, W, H, full[i]);
    flush();
    Serial.printf("[4] full screen %s\n", nm[i]);
    delay(800);
  }
  for (int i = 0; i < NP; i++) { pinMode(PROBE[i], INPUT_PULLUP); lows[i] = 0; }
  drawStatic();
  flush();
  Serial.println("[5] test screen ready");
  stage('E');
}

uint32_t frames = 0, fpsT = 0, briT = 0;
int barX = 0;

void loop() {
  if (!fb) { delay(1000); Serial.println("lcd init failed - see [3]"); return; }
  rect(barX, 238, 60, 26, C_DARK);
  barX = (barX + 4) % 420;
  rect(barX, 238, 60, 26, C_ORANGE);
  frames++;
  if (millis() - fpsT >= 1000) {
    rect(392, 50, 84, 24, C_BLACK);
    textf(392, 52, 2, C_YELLOW, -1, "%u fps", frames);
    frames = 0;
    fpsT = millis();
  }
  for (int i = 0; i < NP; i++) {
    int v = digitalRead(PROBE[i]);
    if (v != last[i]) {
      if (last[i] == 1 && v == 0) lows[i]++;
      if (last[i] != -1) Serial.printf("IO%d -> %s\n", PROBE[i], v ? "HIGH" : "LOW");
      last[i] = v;
      drawPin(i, v);
    }
  }
  if (digitalRead(0) == LOW && millis() - briT > 700) {
    setBri(bri == 255 ? 128 : bri == 128 ? 40 : 255);
    briT = millis();
    Serial.printf("brightness %d\n", bri);
  }
  flush();
  delay(10);
}
