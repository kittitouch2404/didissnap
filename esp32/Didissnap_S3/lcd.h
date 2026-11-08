// lcd.h — จอ RGB 480x272 ของ Panlee ZX4D30NE01S (ใช้ esp_lcd ที่มากับ ESP32 core 3.x)
// วาดลงบัฟเฟอร์หลัง แล้วสลับตอนจอเริ่มเฟรมใหม่ (ไม่มีภาพฉีก)
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
static uint16_t* fb[2] = {nullptr, nullptr};
static int front = 0;
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
  c.num_fbs = 2;
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
  void *a = nullptr, *b = nullptr;
  if (esp_lcd_rgb_panel_get_frame_buffer(panel, 2, &a, &b) != ESP_OK || !a || !b) return false;
  fb[0] = (uint16_t*)a;
  fb[1] = (uint16_t*)b;
  memset(fb[0], 0, LCD_W * LCD_H * 2);
  memset(fb[1], 0, LCD_W * LCD_H * 2);
  ledcAttach(PIN_BL, 5000, 8);
  ledcWrite(PIN_BL, 0);
  return true;
}

inline uint16_t* back() { return fb[front ^ 1]; }

// ส่งบัฟเฟอร์หลังขึ้นจอ แล้วรอจนจอเปลี่ยนไปแสดงเฟรมใหม่จริง
inline void present() {
  uint16_t* b = back();
  esp_cache_msync(b, LCD_W * LCD_H * 2, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  xSemaphoreTake(vsyncSem, 0);
  esp_lcd_panel_draw_bitmap(panel, 0, 0, LCD_W, LCD_H, b);
  front ^= 1;
  xSemaphoreTake(vsyncSem, pdMS_TO_TICKS(40));
}

inline void brightness(int pct) { ledcWrite(PIN_BL, constrain(pct, 0, 100) * 255 / 100); }

}  // namespace lcd
