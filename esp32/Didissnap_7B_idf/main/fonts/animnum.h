// animnum.h — ตัวเลขแบบไตเติลอนิเมะ (รูปทีละตัว) จาก tools/mkanimnum.py
#pragma once
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { char ch; const lv_image_dsc_t* img; int16_t adv; int16_t ofs; } an_glyph_t;
typedef struct { const an_glyph_t* g; int n; int16_t h; int16_t outline; } an_set_t;
extern const an_set_t an_L;   // ใหญ่ (ความเร็ว)
extern const an_set_t an_M;   // กลาง (รอบ / ค่าทางขวา)
#ifdef __cplusplus
}
#endif
