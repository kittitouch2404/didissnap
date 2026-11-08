// lcd.h — จอ RGB 480x272 ของ Panlee ZX4D30NE01S (ใช้ esp_lcd ที่มากับ ESP32 core 3.x)
//
// ใช้โหมด refresh_on_demand: งาน "lcd" สั่งส่งภาพทีละเฟรมต่อกันไปเอง
//  - ทำไม: ใน core ตั้ง LCD_RGB_RESTART_IN_VSYNC ไว้ ถ้าปล่อยให้จอส่งภาพเองตลอด interrupt จะเริ่ม DMA ใหม่ทุกเฟรม
//    ถ้าจังหวะนั้นชิปกำลังอ่าน/เขียนแฟลช (PSRAM ใช้ไม่ได้ชั่วคราว) interrupt จะค้าง → WATCHDOG (interrupt) รีเซ็ต
//  - งาน lcd อยู่ในแฟลช: ตอนชิปใช้แฟลช งานนี้หยุดรอเฉย ๆ จอแค่ว่างไปแป๊บ ไม่ค้าง
//  - คัดลอกภาพใหม่ลงบัฟเฟอร์จอ "ระหว่างเฟรม" ตอนจอไม่ได้อ่าน PSRAM → ไม่แย่งกัน ภาพไม่เลื่อน/ซ้อน/ฉีก
#pragma once
#include <Arduino.h>
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "esp_cache.h"
#include "freertos/semphr.h"
#include "rom/ets_sys.h"

#define LCD_W 480
#define LCD_H 272
#define PIN_BL 45
// ความเร็วส่งภาพ (ผู้ผลิตใช้ 14 MHz) — 12 MHz = ~1 เฟรมทุก 19 ms
#ifndef LCD_PCLK_HZ
#define LCD_PCLK_HZ 12000000
#endif

namespace lcd {

static const size_t FB_BYTES = LCD_W * LCD_H * 2;
static esp_lcd_panel_handle_t panel = nullptr;
static uint16_t* fb = nullptr;              // บัฟเฟอร์ที่จออ่าน (PSRAM)
static uint16_t* bb[2] = {nullptr, nullptr}; // บัฟเฟอร์หลัง 2 ชุด: ชุดหนึ่งวาดอยู่ อีกชุดรอคัดลอกขึ้นจอ
static int wIdx = 0;                         // ชุดที่กำลังวาด
static volatile int pending = -1;            // ชุดที่วาดเสร็จ รอคัดลอก (-1 = ไม่มี)
static SemaphoreHandle_t frameSem = nullptr; // จอส่งภาพครบ 1 เฟรม
static SemaphoreHandle_t copiedSem = nullptr;
static volatile uint32_t frames = 0;

static bool IRAM_ATTR onVsync(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t*, void*) {
  BaseType_t hp = pdFALSE;
  xSemaphoreGiveFromISR(frameSem, &hp);
  return hp == pdTRUE;
}

static void refreshTask(void*) {
  for (;;) {
    int p = pending;
    if (p >= 0) {                            // จอว่างอยู่ (ส่งเฟรมก่อนครบแล้ว) → คัดลอกภาพใหม่
      memcpy(fb, bb[p], FB_BYTES);
      esp_cache_msync(fb, FB_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
      pending = -1;
      xSemaphoreGive(copiedSem);
    }
    xSemaphoreTake(frameSem, 0);
    esp_lcd_rgb_panel_refresh(panel);        // ส่ง 1 เฟรม
    xSemaphoreTake(frameSem, pdMS_TO_TICKS(60));
    frames++;
  }
}

inline bool begin() {
  esp_lcd_rgb_panel_config_t c;
  memset(&c, 0, sizeof(c));
  c.clk_src = LCD_CLK_SRC_DEFAULT;
  c.timings.pclk_hz = LCD_PCLK_HZ;
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
  c.flags.refresh_on_demand = 1;
  if (esp_lcd_new_rgb_panel(&c, &panel) != ESP_OK) return false;
  frameSem = xSemaphoreCreateBinary();
  copiedSem = xSemaphoreCreateBinary();
  esp_lcd_rgb_panel_event_callbacks_t cb;
  memset(&cb, 0, sizeof(cb));
  cb.on_vsync = onVsync;
  esp_lcd_rgb_panel_register_event_callbacks(panel, &cb, nullptr);
  esp_lcd_panel_reset(panel);
  esp_lcd_panel_init(panel);
  void* a = nullptr;
  if (esp_lcd_rgb_panel_get_frame_buffer(panel, 1, &a) != ESP_OK || !a) return false;
  fb = (uint16_t*)a;
  for (int i = 0; i < 2; i++) {
    bb[i] = (uint16_t*)heap_caps_aligned_alloc(64, FB_BYTES, MALLOC_CAP_SPIRAM);
    if (!bb[i]) return false;
    memset(bb[i], 0, FB_BYTES);
  }
  memset(fb, 0, FB_BYTES);
  esp_cache_msync(fb, FB_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  ledcAttach(PIN_BL, 5000, 8);
  ledcWrite(PIN_BL, 0);
  xTaskCreatePinnedToCore(refreshTask, "lcd", 4096, nullptr, configMAX_PRIORITIES - 2, nullptr, 0);
  return true;
}

inline bool ok() { return fb && bb[0] && bb[1]; }
static uint16_t* dummy = nullptr;   // ถ้าจอเริ่มไม่ได้ วาดลงที่นี่แทน (ไม่ค้าง)
inline uint16_t* back() {
  if (!ok()) { if (!dummy) dummy = (uint16_t*)heap_caps_malloc(FB_BYTES, MALLOC_CAP_SPIRAM); return dummy; }
  return bb[wIdx];
}

// คัดลอกลง PSRAM ทีละช่วงเล็ก ๆ เว้นจังหวะ (กันแย่ง PSRAM ตอนจอกำลังส่งภาพ)
inline void paceCopy(uint16_t* dst, const uint16_t* src, size_t bytes) {
  const size_t CH = LCD_W * 2 * 8;
  uint8_t* d = (uint8_t*)dst;
  const uint8_t* s = (const uint8_t*)src;
  for (size_t o = 0; o < bytes; o += CH) {
    size_t n = bytes - o < CH ? bytes - o : CH;
    memcpy(d + o, s + o, n);
    esp_cache_msync(d + o, n, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    esp_rom_delay_us(40);
  }
}

// ส่งภาพที่วาดเสร็จให้งาน lcd คัดลอกขึ้นจอ แล้วสลับไปวาดอีกชุด
inline void present() {
  if (!ok()) { delay(16); return; }
  esp_cache_msync(bb[wIdx], FB_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  if (pending >= 0) xSemaphoreTake(copiedSem, pdMS_TO_TICKS(100));   // ชุดก่อนยังไม่ได้ขึ้นจอ → รอ
  xSemaphoreTake(copiedSem, 0);
  pending = wIdx;
  wIdx ^= 1;
}

inline void brightness(int pct) { ledcWrite(PIN_BL, constrain(pct, 0, 100) * 255 / 100); }

}  // namespace lcd
