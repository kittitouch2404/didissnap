// lcd.h — จอ RGB 480x272 ของ Panlee ZX4D30NE01S (ใช้ esp_lcd ที่มากับ ESP32 core 3.x)
//
// โหมดส่งภาพต่อเนื่อง (stream) + บัฟเฟอร์จอชุดเดียว + บัฟเฟอร์หลังของเราเอง คัดลอกขึ้นจอตอน vsync
//  - แบบนี้ภาพนิ่งสุด (โหมด refresh_on_demand เริ่มเฟรมใหม่ทุกครั้งแล้วภาพเลื่อนไปมา → สั่น/ซ้อน)
//  - ปัญหาเดิมของโหมดนี้: ตอนชิปอ่าน/เขียนแฟลช (LittleFS, Preferences, WiFi) cache/PSRAM ปิดชั่วคราว
//    แต่ DMA ของจอยังดึงภาพจาก PSRAM และ interrupt ของจอยังสั่งเริ่ม DMA ใหม่ → ค้าง → INT WDT รีเซ็ต
//  - แก้: ดักทุกครั้งที่ชิปจะใช้แฟลช (hook start/end ของไดรเวอร์แฟลช) → หยุดจอ+DMA ก่อน
//    ใช้แฟลชเสร็จ → เริ่มส่งภาพใหม่จากบรรทัดบนสุดทันที (จอแค่ค้างภาพเดิมไม่กี่ ms)
#pragma once
#include <Arduino.h>
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "esp_cache.h"
#include "esp_flash.h"
#include "freertos/semphr.h"
#include "rom/ets_sys.h"
#include "soc/gdma_channel.h"
#include "hal/gdma_ll.h"
#include "hal/lcd_ll.h"

#define LCD_W 480
#define LCD_H 272
#define PIN_BL 45
// ความเร็วส่งภาพ: ผู้ผลิตใช้ 14 MHz แต่ตอนวาดทั้งจอ PSRAM ไม่พอ → ภาพเลื่อน/ซ้อน
// 10 MHz = จอรีเฟรช ~44 ครั้ง/วินาที
#ifndef LCD_PCLK_HZ
#define LCD_PCLK_HZ 10000000
#endif

namespace lcd {

static const size_t FB_BYTES = LCD_W * LCD_H * 2;
static const uint32_t DOTS_PER_FRAME = (10 + 80 + LCD_W + 60) * (10 + 20 + LCD_H + 60);   // รวมช่วงว่าง
static esp_lcd_panel_handle_t panel = nullptr;
static uint16_t* fb = nullptr;               // บัฟเฟอร์ที่จออ่าน (PSRAM)
static uint16_t* bb[2] = {nullptr, nullptr}; // บัฟเฟอร์หลัง 2 ชุด: วาดชุดหนึ่ง ระหว่างนั้นอีกชุดถูกคัดลอกขึ้นจอ
static int wIdx = 0;
static volatile int pending = -1;
static volatile uint32_t handed = 0, copied = 0;
static SemaphoreHandle_t vsyncSem = nullptr, pendSem = nullptr, doneSem = nullptr;
static volatile uint32_t frames = 0;         // จอส่งภาพไปกี่เฟรมแล้ว (นับ vsync)
static volatile uint32_t flashPauses = 0;    // นับว่าหยุดจอเพราะใช้แฟลชกี่ครั้ง (ดูใน Serial)
static volatile int minVsyncs = 1;           // ขึ้นภาพใหม่ได้ทุกกี่ vsync (2 = ล็อกครึ่งหนึ่งของรีเฟรช)
static int curHz = 0;

static bool IRAM_ATTR onVsync(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t*, void*) {
  frames++;
  BaseType_t hp = pdFALSE;
  xSemaphoreGiveFromISR(vsyncSem, &hp);
  return hp == pdTRUE;
}

// ---------- หยุด/เริ่มจอรอบการใช้แฟลช ----------
static int dmaCh = -1;              // ช่อง GDMA ที่ต่อกับจอ
static uint32_t dmaHead = 0;        // ที่อยู่ descriptor แรกของเฟรม
static volatile int flashDepth = 0;
static esp_flash_os_functions_t myOs;
static const esp_flash_os_functions_t* origOs = nullptr;

static void IRAM_ATTR hwPause() {
  if (dmaCh < 0) return;
  lcd_ll_enable_interrupt(&LCD_CAM, LCD_LL_EVENT_VSYNC_END, false);   // ไม่ให้ไดรเวอร์สั่ง DMA ใหม่
  esp_rom_delay_us(8);                                                 // เผื่อ ISR อีกคอร์กำลังทำงานอยู่
  lcd_ll_stop(&LCD_CAM);
  gdma_ll_tx_stop(&GDMA, dmaCh);
  flashPauses++;
}

static void IRAM_ATTR hwResume() {
  if (dmaCh < 0) return;
  // เหมือนที่ไดรเวอร์ทำตอนเริ่มส่งเฟรม: ล้าง FIFO แล้วเริ่มจากบรรทัดแรก
  gdma_ll_tx_reset_channel(&GDMA, dmaCh);
  lcd_ll_stop(&LCD_CAM);
  lcd_ll_fifo_reset(&LCD_CAM);
  gdma_ll_tx_set_desc_addr(&GDMA, dmaCh, dmaHead);
  gdma_ll_tx_start(&GDMA, dmaCh);
  esp_rom_delay_us(1);
  lcd_ll_start(&LCD_CAM);
  lcd_ll_clear_interrupt_status(&LCD_CAM, LCD_LL_EVENT_VSYNC_END);
  lcd_ll_enable_interrupt(&LCD_CAM, LCD_LL_EVENT_VSYNC_END, true);
}

static esp_err_t IRAM_ATTR osStart(void* arg) {
  if (__atomic_fetch_add(&flashDepth, 1, __ATOMIC_SEQ_CST) == 0) hwPause();
  return origOs->start(arg);
}

static esp_err_t IRAM_ATTR osEnd(void* arg) {
  esp_err_t r = origOs->end(arg);      // เปิด cache คืนก่อน
  if (__atomic_sub_fetch(&flashDepth, 1, __ATOMIC_SEQ_CST) == 0) hwResume();
  return r;
}

static void hookFlash() {
  if (origOs || !esp_flash_default_chip || !esp_flash_default_chip->os_func) return;
  origOs = esp_flash_default_chip->os_func;
  myOs = *origOs;
  myOs.start = osStart;
  myOs.end = osEnd;
  esp_flash_default_chip->os_func = &myOs;
}

static void findDma() {
  for (int i = 0; i < SOC_GDMA_PAIRS_PER_GROUP; i++) {
    if (GDMA.channel[i].out.peri_sel.sel == SOC_GDMA_TRIG_PERIPH_LCD0) {
      dmaHead = GDMA.channel[i].out.link.addr;
      dmaCh = i;
      return;
    }
  }
}

inline void paceCopy(uint16_t* dst, const uint16_t* src, size_t bytes);

// งานคัดลอกภาพขึ้นจอ (คอร์ 1): รอ vsync แล้วคัดลอกทั้งจอ ระหว่างนั้นโปรแกรมหลักวาดเฟรมถัดไปลงอีกบัฟเฟอร์ได้เลย
static void presenter(void*) {
  uint32_t lastF = 0;
  for (;;) {
    xSemaphoreTake(pendSem, portMAX_DELAY);
    int p = pending;
    if (p < 0) continue;
    do {
      xSemaphoreTake(vsyncSem, 0);
      xSemaphoreTake(vsyncSem, pdMS_TO_TICKS(60));
    } while ((int)(frames - lastF) < minVsyncs);
    paceCopy(fb, bb[p], FB_BYTES);
    lastF = frames;
    pending = -1;
    copied++;
    xSemaphoreGive(doneSem);
  }
}

static bool usePresenter = false;

// hz = อัตรารีเฟรช (ตั้งตอนเริ่มเท่านั้น — เปลี่ยนระหว่างทำงานแล้วเสี่ยงดับ), every = ขึ้นภาพทุกกี่ vsync
// presenter = false → คัดลอกขึ้นจอเองในโปรแกรมหลักแบบเดิม (โหมดปลอดภัย)
inline bool begin(int hz = 44, int every = 1, bool presenter = true) {
  esp_lcd_rgb_panel_config_t c;
  memset(&c, 0, sizeof(c));
  c.clk_src = LCD_CLK_SRC_DEFAULT;
  c.timings.pclk_hz = hz == 44 ? LCD_PCLK_HZ : (uint32_t)hz * DOTS_PER_FRAME;   // 44 Hz = 10 MHz พอดี (แบบที่ใช้ได้)
  minVsyncs = every < 1 ? 1 : every;
  c.timings.h_res = LCD_W;
  c.timings.v_res = LCD_H;
  c.timings.hsync_pulse_width = 10;
  c.timings.hsync_back_porch = 80;
  c.timings.hsync_front_porch = 60;
  c.timings.vsync_pulse_width = 10;
  c.timings.vsync_back_porch = 20;
  c.timings.vsync_front_porch = 60;
  c.timings.flags.pclk_active_neg = 1;
  c.data_width = 16;
  c.num_fbs = 1;
  c.dma_burst_size = 64;
  c.hsync_gpio_num = 47;
  c.vsync_gpio_num = 48;
  c.de_gpio_num = 40;
  c.pclk_gpio_num = 14;
  c.disp_gpio_num = -1;
  const int data[16] = {9, 10, 11, 12, 13, 16, 17, 18, 8, 3, 46, 4, 5, 6, 7, 15};
  for (int i = 0; i < 16; i++) c.data_gpio_nums[i] = data[i];
  c.flags.fb_in_psram = 1;
  if (esp_lcd_new_rgb_panel(&c, &panel) != ESP_OK) return false;
  vsyncSem = xSemaphoreCreateBinary();
  esp_lcd_rgb_panel_event_callbacks_t cb;
  memset(&cb, 0, sizeof(cb));
  cb.on_vsync = onVsync;
  esp_lcd_rgb_panel_register_event_callbacks(panel, &cb, nullptr);
  esp_lcd_panel_reset(panel);
  esp_lcd_panel_init(panel);
  void* a = nullptr;
  if (esp_lcd_rgb_panel_get_frame_buffer(panel, 1, &a) != ESP_OK || !a) return false;
  for (int i = 0; i < 2; i++) {
    bb[i] = (uint16_t*)heap_caps_aligned_alloc(64, FB_BYTES, MALLOC_CAP_SPIRAM);
    if (!bb[i]) return false;
    memset(bb[i], 0, FB_BYTES);
  }
  fb = (uint16_t*)a;
  memset(fb, 0, FB_BYTES);
  esp_cache_msync(fb, FB_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  ledcAttach(PIN_BL, 5000, 8);
  ledcWrite(PIN_BL, 0);
  delay(30);                 // ให้จอส่งภาพไปสักเฟรมก่อน ค่อยจำตำแหน่ง DMA
  findDma();
  hookFlash();
  curHz = hz;
  Serial.printf("LCD stream %d Hz (every %d), DMA ch %d head 0x%05x, flash hook %s, presenter %s\n", hz, minVsyncs, dmaCh,
                (unsigned)dmaHead, origOs ? "on" : "OFF", presenter ? "on" : "off");
  if (presenter) {
    pendSem = xSemaphoreCreateBinary();
    doneSem = xSemaphoreCreateBinary();
    usePresenter = xTaskCreatePinnedToCore(lcd::presenter, "lcd", 4096, nullptr, 6, nullptr, 1) == pdPASS;
  }
  
  return true;
}

inline bool ok() { return fb && bb[0] && bb[1]; }

// คัดลอกลง PSRAM ทีละช่วงเล็ก ๆ เว้นจังหวะให้จออ่าน PSRAM ได้ (คัดลอกรวดเดียว → บรรทัดเลื่อน ภาพซ้อน)
inline void paceCopy(uint16_t* dst, const uint16_t* src, size_t bytes) {
  const size_t CH = LCD_W * 2 * 8;
  uint8_t* d = (uint8_t*)dst;
  const uint8_t* s = (const uint8_t*)src;
  for (size_t o = 0; o < bytes; o += CH) {
    size_t n = bytes - o < CH ? bytes - o : CH;
    memcpy(d + o, s + o, n);
    esp_cache_msync(d + o, n, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    esp_rom_delay_us(60);
  }
}
static uint16_t* dummy = nullptr;   // ถ้าจอเริ่มไม่ได้ วาดลงที่นี่แทน (ไม่ค้าง)
inline uint16_t* back() {
  if (!ok()) { if (!dummy) dummy = (uint16_t*)heap_caps_malloc(FB_BYTES, MALLOC_CAP_SPIRAM); return dummy; }
  return bb[wIdx];
}

// ส่งเฟรมที่วาดเสร็จให้งาน lcd ขึ้นจอที่ vsync ถัดไป แล้วสลับไปวาดอีกบัฟเฟอร์
// (ถ้าเฟรมก่อนยังไม่ได้ขึ้นจอ รอก่อน — บัฟเฟอร์ที่จะวาดต่อต้องคัดลอกเสร็จแล้ว)
inline void present() {
  if (!ok()) { delay(16); return; }
  if (!usePresenter) {                       // แบบเดิม: รอ vsync แล้วคัดลอกเอง
    xSemaphoreTake(vsyncSem, 0);
    xSemaphoreTake(vsyncSem, pdMS_TO_TICKS(60));
    paceCopy(fb, bb[wIdx], FB_BYTES);
    return;
  }
  while (copied != handed) xSemaphoreTake(doneSem, pdMS_TO_TICKS(50));
  pending = wIdx;
  handed++;
  xSemaphoreGive(pendSem);
  wIdx ^= 1;
}
// รอจนเฟรมล่าสุดขึ้นจอจริง (ใช้ตอนเปิดเครื่อง/หน้าจอแจ้งปัญหา)
inline void flush() {
  if (!ok() || !usePresenter) return;
  while (copied != handed) xSemaphoreTake(doneSem, pdMS_TO_TICKS(50));
}

inline void brightness(int pct) { ledcWrite(PIN_BL, constrain(pct, 0, 100) * 255 / 100); }

}  // namespace lcd
