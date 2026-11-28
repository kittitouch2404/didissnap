// board.h — ฮาร์ดแวร์ของ Waveshare ESP32-S3-Touch-LCD-7B (จอ 1024x600, ทัช GT911, ชิปขยายขา CH32V003, ช่อง SD)
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LCD_W 1024
#define LCD_H 600

// เปิดจอ + ทัช + LVGL (คืนจอที่สร้างแล้ว) · ต้องเรียกก่อนอย่างอื่น
// mode 1 = วาดใน RAM ภายใน (เร็ว · อาจเห็นรอยต่อจาง ๆ ตอนเข็มวิ่ง) · 0 = วาดตรงลงเฟรมบัฟเฟอร์ PSRAM 2 ชุด (ไม่ฉีก แต่ช้า)
lv_display_t* board_init(int mode);
int board_render_mode(void);   // โหมดที่ใช้จริง (ถ้า RAM ไม่พอจะถอยไปโหมด 0)
// ความสว่าง 0-100 %
void board_backlight(int pct);
// SD card (โหมด 1 บิต) ที่ /sdcard · true = ใช้ได้
bool board_sd_mount(void);
bool board_sd_ok(void);
bool board_touch_ok(void);   // เจอชิปทัช GT911 ไหม
// เรียกทุก ~2 วิ: ย้ำค่าให้ชิปขยายขา (ถ้ามันรีสตาร์ท ไฟจอกลับมาเอง)
void board_keepalive(void);

#ifdef __cplusplus
}
#endif
