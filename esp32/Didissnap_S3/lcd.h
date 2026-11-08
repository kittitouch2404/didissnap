// lcd.h — จอ RGB 480x272 ของ Panlee ZX4D30NE01S (ใช้ esp_lcd ที่มากับ ESP32 core 3.x)
// บัฟเฟอร์จอชุดเดียว (แบบสเก็ตช์ทดสอบ) + บัฟเฟอร์หลังของเราเอง คัดลอกขึ้นจอตอน vsync
// (num_fbs = 2 บนบอร์ดนี้ทำให้ภาพเฟรมสลับเลื่อนซ้อนกันเป็นสองชุด)
#pragma once
#include <Arduino.h>
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "esp_cache.h"
#include "freertos/semphr.h"

#define LCD_W 480
#define LCD_H 272
#define PIN_BL 45

namespace lcd {

static esp_lcd_panel_handle_t panel = nullptr;
static uint16_t* fb = nullptr;     // บัฟเฟอร์ที่จออ่าน (PSRAM)
static uint16_t* bb = nullptr;     // บัฟเฟอร์หลังที่เราวาด (PSRAM)
static SemaphoreHandle_t vsyncSem = nullptr;

static bool IRAM_ATTR onVsync(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t*, void*) {
  BaseType_t hp = pdFALSE;
  xSemaphoreGiveFromISR(vsyncSem, &hp);
  return hp == pdTRUE;
}

inline bool begin() {
  esp_lcd_rgb_panel_config_t c;
  memset(&c, 0, sizeof(c));
  c.clk_src = LCD_CLK_SRC_DEFAULT;
  c.timings.pclk_hz = 14000000;
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
  // ไม่ใช้ bounce buffer: core ตั้ง LCD_RGB_ISR_IRAM_SAFE ไว้ ถ้าใช้ bounce buffer ตอนชิปเขียนแฟลช (PSRAM ปิดชั่วคราว)
  // จะค้างจน watchdog รีเซ็ต — ภาพที่เลื่อนตอนเขียนแฟลช core แก้ให้เองทุก vsync (LCD_RGB_RESTART_IN_VSYNC)
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
  bb = (uint16_t*)heap_caps_aligned_alloc(64, LCD_W * LCD_H * 2, MALLOC_CAP_SPIRAM);
  if (!bb) return false;
  fb = (uint16_t*)a;
  memset(fb, 0, LCD_W * LCD_H * 2);
  memset(bb, 0, LCD_W * LCD_H * 2);
  esp_cache_msync(fb, LCD_W * LCD_H * 2, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  ledcAttach(PIN_BL, 5000, 8);
  ledcWrite(PIN_BL, 0);
  return true;
}

inline bool ok() { return fb && bb; }
static uint16_t* dummy = nullptr;   // ถ้าจอเริ่มไม่ได้ วาดลงที่นี่แทน (ไม่ค้าง)
inline uint16_t* back() {
  if (!ok()) { if (!dummy) dummy = (uint16_t*)heap_caps_malloc(LCD_W * LCD_H * 2, MALLOC_CAP_SPIRAM); return dummy; }
  return bb;
}

// ส่งบัฟเฟอร์หลังขึ้นจอ แล้วรอจนจอเปลี่ยนไปแสดงเฟรมใหม่จริง
inline void present() {
  if (!ok()) { delay(16); return; }
  // รอจอเริ่มช่วงว่างระหว่างเฟรม แล้วคัดลอกทั้งจอ (เร็วกว่าจอไล่แสดงภาพ → ไม่ฉีก)
  xSemaphoreTake(vsyncSem, 0);
  xSemaphoreTake(vsyncSem, pdMS_TO_TICKS(40));
  memcpy(fb, bb, LCD_W * LCD_H * 2);
  esp_cache_msync(fb, LCD_W * LCD_H * 2, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
}

inline void brightness(int pct) { ledcWrite(PIN_BL, constrain(pct, 0, 100) * 255 / 100); }

}  // namespace lcd
