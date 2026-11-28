// skin.cpp — อ่านสกิน JSON แล้วสร้างวิดเจ็ต LVGL · อัปเดตเฉพาะค่าที่เปลี่ยน (LVGL วาดใหม่แค่ตรงนั้น = ลื่น)
#include "skin.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <string>
#include <vector>
#include "cJSON.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "fonts/animnum.h"
#include "src/libs/gif/gifdec.h"
#include "src/misc/lv_area_private.h"

extern "C" {
LV_FONT_DECLARE(f_num170)
LV_FONT_DECLARE(f_num96)
LV_FONT_DECLARE(f_num60)
LV_FONT_DECLARE(f_t40)
LV_FONT_DECLARE(f_t28)
LV_FONT_DECLARE(f_t20)
}

namespace skin {

// ---------------------------------------------------------------- สกินในตัว (1024x600)
const char* const BUILTIN = R"JSON({
  "name": "Didissnap Dark",
  "accent": "#FF7A00",
  "bg": "#0A0A0C",
  "pages": [
    {"name": "MAIN", "w": [
      {"t": "shift", "x": 24, "y": 8, "w": 976, "h": 16, "v": "rpm", "from": 4800, "to": 6600, "n": 24},
      {"t": "arc", "x": 24, "y": 36, "size": 548, "width": 30, "v": "rpm", "min": 0, "max": 8000, "red": 6500, "scale": 1000},
      {"t": "value", "x": 98, "y": 196, "w": 400, "v": "spd", "font": "n170", "dec": 0, "align": "center"},
      {"t": "label", "x": 98, "y": 398, "w": 400, "text": "km/h", "font": "t28", "color": "#8A8A90", "align": "center"},
      {"t": "value", "x": 148, "y": 444, "w": 300, "v": "rpm", "font": "n60", "dec": 0, "align": "center", "color": "accent"},
      {"t": "label", "x": 148, "y": 520, "w": 300, "text": "rpm", "font": "t20", "color": "#8A8A90", "align": "center"},
      {"t": "tile", "x": 596, "y": 40, "w": 404, "h": 124, "v": "ect", "title": "น้ำหล่อเย็น", "title_en": "COOLANT", "unit": "°C", "dec": 0, "warn": 105, "font": "n96"},
      {"t": "tile", "x": 596, "y": 172, "w": 404, "h": 124, "v": "iat", "title": "อากาศเข้า", "title_en": "INTAKE AIR", "unit": "°C", "dec": 0, "warn": 60, "font": "n96"},
      {"t": "tile", "x": 596, "y": 304, "w": 404, "h": 124, "v": "volt", "title": "แบตเตอรี่", "title_en": "BATTERY", "unit": "V", "dec": 1, "low": 12.0, "font": "n96"},
      {"t": "tile", "x": 596, "y": 436, "w": 404, "h": 124, "v": "load", "title": "โหลดเครื่อง", "title_en": "ENGINE LOAD", "unit": "%", "dec": 0, "font": "n96"}
    ]},
    {"name": "RED", "bg": "#161616", "w": [
      {"t": "rect", "x": 12, "y": 12, "w": 1000, "h": 58, "color": "#D01420", "radius": 0},
      {"t": "rect", "x": 12, "y": 70, "w": 1000, "h": 58, "color": "#B80E18", "radius": 0},
      {"t": "rect", "x": 12, "y": 128, "w": 1000, "h": 58, "color": "#D01420", "radius": 0},
      {"t": "rect", "x": 12, "y": 186, "w": 1000, "h": 58, "color": "#B80E18", "radius": 0},
      {"t": "rect", "x": 12, "y": 244, "w": 1000, "h": 58, "color": "#D01420", "radius": 0},
      {"t": "rect", "x": 12, "y": 302, "w": 1000, "h": 58, "color": "#B80E18", "radius": 0},
      {"t": "rect", "x": 12, "y": 360, "w": 1000, "h": 58, "color": "#D01420", "radius": 0},
      {"t": "rect", "x": 12, "y": 418, "w": 1000, "h": 58, "color": "#B80E18", "radius": 0},
      {"t": "rect", "x": 12, "y": 476, "w": 1000, "h": 58, "color": "#D01420", "radius": 0},
      {"t": "rect", "x": 560, "y": 56, "w": 440, "h": 110, "color": "#000000", "opa": 20, "radius": 0},
      {"t": "rect", "x": 560, "y": 174, "w": 440, "h": 110, "color": "#000000", "opa": 56, "radius": 0},
      {"t": "rect", "x": 560, "y": 292, "w": 440, "h": 110, "color": "#000000", "opa": 20, "radius": 0},
      {"t": "rect", "x": 560, "y": 410, "w": 440, "h": 110, "color": "#000000", "opa": 56, "radius": 0},
      {"t": "value", "x": 560, "y": 28, "w": 352, "v": "iat", "font": "n96", "dec": 0, "align": "right"},
      {"t": "label", "x": 918, "y": 62, "w": 80, "text": "°C", "font": "t28"},
      {"t": "label", "x": 560, "y": 130, "w": 424, "text": "INTAKE AIR TEMP", "text_th": "อากาศเข้า", "font": "t28", "align": "right"},
      {"t": "value", "x": 560, "y": 146, "w": 352, "v": "ect", "font": "n96", "dec": 0, "align": "right", "warn": 105},
      {"t": "label", "x": 918, "y": 180, "w": 80, "text": "°C", "font": "t28"},
      {"t": "label", "x": 560, "y": 248, "w": 424, "text": "WATER TEMP", "text_th": "น้ำหล่อเย็น", "font": "t28", "align": "right"},
      {"t": "value", "x": 560, "y": 264, "w": 352, "v": "volt", "font": "n96", "dec": 1, "align": "right", "low": 12.0},
      {"t": "label", "x": 918, "y": 298, "w": 80, "text": "V", "font": "t28"},
      {"t": "label", "x": 560, "y": 366, "w": 424, "text": "BATTERY", "text_th": "แบตเตอรี่", "font": "t28", "align": "right"},
      {"t": "value", "x": 560, "y": 382, "w": 352, "v": "load", "font": "n96", "dec": 0, "align": "right"},
      {"t": "label", "x": 918, "y": 416, "w": 80, "text": "%", "font": "t28"},
      {"t": "label", "x": 560, "y": 484, "w": 424, "text": "ENGINE LOAD", "text_th": "โหลดเครื่อง", "font": "t28", "align": "right"},
      {"t": "circle", "x": 270, "y": 284, "r": 246, "color": "#0C0C0C"},
      {"t": "ticks", "x": 270, "y": 284, "r": 229, "v": "rpm", "min": 0, "max": 8000, "red": 6500, "n": 100, "len": 16, "width": 7, "rot": 120, "sweep": 300, "on": "#F2F2F2", "off": "#5A5A5A", "redon": "#FF3A3A"},
      {"t": "ticks", "x": 270, "y": 284, "r": 246, "min": 0, "max": 8000, "red": 6500, "n": 8, "len": 12, "width": 9, "rot": 120, "sweep": 300, "on": "#FF2A2A", "off": "#FF2A2A", "labels": 1, "font": "t40", "label": "#E8E8E8", "redlabel": "#FF6A6A", "gap": 26},
      {"t": "circle", "x": 270, "y": 284, "r": 155, "color": "#E0101C", "grad": "#7A0006", "border": "#B8B8B8", "bw": 16},
      {"t": "needle", "x": 270, "y": 284, "r0": 158, "r1": 196, "v": "rpm", "min": 0, "max": 8000, "rot": 120, "sweep": 300, "width": 14, "color": "#FFD21E"},
      {"t": "rect", "x": 208, "y": 142, "w": 124, "h": 60, "color": "#E0141E", "radius": 8},
      {"t": "value", "x": 208, "y": 130, "w": 124, "v": "gear", "font": "n60", "align": "center"},
      {"t": "value", "x": 90, "y": 206, "w": 360, "v": "rpm", "font": "n96", "dec": 0, "align": "center"},
      {"t": "rect", "x": 155, "y": 338, "w": 230, "h": 3, "color": "#FFFFFF", "radius": 0},
      {"t": "value", "x": 120, "y": 334, "w": 300, "v": "spd", "font": "n60", "dec": 0, "align": "center"},
      {"t": "label", "x": 170, "y": 406, "w": 200, "text": "KMH", "font": "t20", "align": "center"},
      {"t": "rect", "x": 12, "y": 544, "w": 1000, "h": 46, "color": "#C8C8C8", "radius": 4},
      {"t": "shift", "x": 330, "y": 553, "w": 364, "h": 28, "v": "rpm", "from": 4500, "to": 6500, "n": 10, "off": "#3A3A3A"}
    ]},
    {"name": "RACE", "w": [
      {"t": "shift", "x": 24, "y": 8, "w": 976, "h": 40, "v": "rpm", "from": 4800, "to": 6600, "n": 16},
      {"t": "bar", "x": 24, "y": 60, "w": 976, "h": 34, "v": "rpm", "min": 0, "max": 8000, "red": 6500},
      {"t": "value", "x": 24, "y": 98, "w": 360, "v": "rpm", "font": "n60", "dec": 0, "color": "accent"},
      {"t": "label", "x": 24, "y": 172, "w": 200, "text": "rpm", "font": "t20", "color": "#8A8A90"},
      {"t": "value", "x": 262, "y": 120, "w": 500, "v": "spd", "font": "n170", "dec": 0, "align": "center"},
      {"t": "label", "x": 262, "y": 322, "w": 500, "text": "km/h", "font": "t28", "color": "#8A8A90", "align": "center"},
      {"t": "value", "x": 800, "y": 120, "w": 200, "v": "gear", "font": "n170", "align": "center", "color": "accent"},
      {"t": "label", "x": 800, "y": 322, "w": 200, "text": "GEAR", "font": "t28", "color": "#8A8A90", "align": "center"},
      {"t": "tile", "x": 24, "y": 410, "w": 154, "h": 160, "v": "ect", "title": "น้ำ", "title_en": "WATER", "unit": "°C", "dec": 0, "warn": 105},
      {"t": "tile", "x": 188, "y": 410, "w": 154, "h": 160, "v": "iat", "title": "อากาศ", "title_en": "AIR", "unit": "°C", "dec": 0, "warn": 60},
      {"t": "tile", "x": 352, "y": 410, "w": 154, "h": 160, "v": "volt", "title": "แบต", "title_en": "BATT", "unit": "V", "dec": 1, "low": 12.0},
      {"t": "tile", "x": 516, "y": 410, "w": 154, "h": 160, "v": "tps", "title": "คันเร่ง", "title_en": "THROTTLE", "unit": "%", "dec": 0},
      {"t": "tile", "x": 680, "y": 410, "w": 154, "h": 160, "v": "map", "title": "MAP", "unit": "kPa", "dec": 0},
      {"t": "tile", "x": 844, "y": 410, "w": 156, "h": 160, "v": "afr", "title": "AFR", "unit": "", "dec": 1}
    ]},
    {"name": "DATA", "w": [
      {"t": "tile", "x": 24, "y": 36, "w": 320, "h": 176, "v": "rpm", "title": "รอบเครื่อง", "title_en": "ENGINE RPM", "unit": "rpm", "dec": 0, "warn": 6500, "font": "n96"},
      {"t": "tile", "x": 352, "y": 36, "w": 320, "h": 176, "v": "spd", "title": "ความเร็ว", "title_en": "SPEED", "unit": "km/h", "dec": 0, "font": "n96"},
      {"t": "tile", "x": 680, "y": 36, "w": 320, "h": 176, "v": "ect", "title": "น้ำหล่อเย็น", "title_en": "COOLANT", "unit": "°C", "dec": 0, "warn": 105, "font": "n96"},
      {"t": "tile", "x": 24, "y": 220, "w": 320, "h": 176, "v": "iat", "title": "อากาศเข้า", "title_en": "INTAKE AIR", "unit": "°C", "dec": 0, "font": "n96"},
      {"t": "tile", "x": 352, "y": 220, "w": 320, "h": 176, "v": "volt", "title": "แบตเตอรี่", "title_en": "BATTERY", "unit": "V", "dec": 1, "low": 12.0, "font": "n96"},
      {"t": "tile", "x": 680, "y": 220, "w": 320, "h": 176, "v": "load", "title": "โหลด", "title_en": "LOAD", "unit": "%", "dec": 0, "font": "n96"},
      {"t": "tile", "x": 24, "y": 404, "w": 320, "h": 176, "v": "tps", "title": "คันเร่ง", "title_en": "THROTTLE", "unit": "%", "dec": 0, "font": "n96"},
      {"t": "tile", "x": 352, "y": 404, "w": 320, "h": 176, "v": "map", "title": "MAP", "unit": "kPa", "dec": 0, "font": "n96"},
      {"t": "tile", "x": 680, "y": 404, "w": 320, "h": 176, "v": "maf", "title": "MAF", "unit": "g/s", "dec": 1, "font": "n96"}
    ]},
    {"name": "EXTRA", "w": [
      {"t": "tile", "x": 24, "y": 36, "w": 320, "h": 268, "v": "cvt", "title": "เกียร์ CVT", "title_en": "CVT TEMP", "unit": "°C", "dec": 0, "warn": 110, "font": "n96"},
      {"t": "tile", "x": 352, "y": 36, "w": 320, "h": 268, "v": "oil", "title": "น้ำมันเครื่อง", "title_en": "OIL TEMP", "unit": "°C", "dec": 0, "warn": 125, "font": "n96"},
      {"t": "tile", "x": 680, "y": 36, "w": 320, "h": 268, "v": "timing", "title": "จุดระเบิด", "title_en": "TIMING", "unit": "°", "dec": 1, "font": "n96"},
      {"t": "tile", "x": 24, "y": 312, "w": 320, "h": 268, "v": "afr", "title": "AFR", "unit": "", "dec": 1, "font": "n96"},
      {"t": "tile", "x": 352, "y": 312, "w": 320, "h": 268, "v": "stft", "title": "STFT", "unit": "%", "dec": 1, "font": "n96"},
      {"t": "tile", "x": 680, "y": 312, "w": 320, "h": 268, "v": "fuel", "title": "น้ำมัน", "title_en": "FUEL", "unit": "%", "dec": 0, "low": 12, "font": "n96"}
    ]},
    {"name": "STRIP", "w": [
      {"t": "value", "x": 16, "y": 0, "w": 300, "v": "rpm", "font": "n96", "dec": 0, "warn": 6500},
      {"t": "label", "x": 18, "y": 118, "w": 200, "text": "RPM", "font": "t20", "color": "#BBBBBB"},
      {"t": "shift", "x": 320, "y": 14, "w": 683, "h": 44, "v": "rpm", "mode": "heat", "color": "#FF7A00", "off": "#3A2A10", "from": 0, "to": 8000, "red": 6500, "n": 60, "gap": 3, "radius": 1, "tall_every": 5, "tall_h": 16},
      {"t": "label", "x": 375, "y": 80, "w": 60, "text": "1", "font": "t28", "color": "#FFFFFF", "align": "center"},
      {"t": "label", "x": 460, "y": 80, "w": 60, "text": "2", "font": "t28", "color": "#FFFFFF", "align": "center"},
      {"t": "label", "x": 546, "y": 80, "w": 60, "text": "3", "font": "t28", "color": "#FFFFFF", "align": "center"},
      {"t": "label", "x": 631, "y": 80, "w": 60, "text": "4", "font": "t28", "color": "#FFFFFF", "align": "center"},
      {"t": "label", "x": 716, "y": 80, "w": 60, "text": "5", "font": "t28", "color": "#FFFFFF", "align": "center"},
      {"t": "label", "x": 802, "y": 80, "w": 60, "text": "6", "font": "t28", "color": "#FFFFFF", "align": "center"},
      {"t": "label", "x": 887, "y": 80, "w": 60, "text": "7", "font": "t28", "color": "#FF3A3A", "align": "center"},
      {"t": "label", "x": 973, "y": 80, "w": 60, "text": "8", "font": "t28", "color": "#FF3A3A", "align": "center"},
      {"t": "tile", "x": 12, "y": 150, "w": 300, "h": 130, "v": "trip", "title_en": "TRIP km", "unit": "", "dec": 1},
      {"t": "tile", "x": 12, "y": 290, "w": 300, "h": 130, "v": "fuel", "title_en": "FUEL %", "unit": "", "dec": 0, "low": 12},
      {"t": "shift", "x": 28, "y": 398, "w": 170, "h": 10, "v": "fuel", "mode": "level", "color": "#00E05A", "off": "#333333", "from": 0, "to": 100, "n": 10, "gap": 4, "radius": 1},
      {"t": "tile", "x": 324, "y": 150, "w": 176, "h": 270, "v": "gear", "title_en": "GEAR", "unit": "", "font": "n170", "color": "#FFC400"},
      {"t": "tile", "x": 512, "y": 150, "w": 500, "h": 270, "v": "spd", "title_en": "SPEED", "unit": "km/h", "font": "n170"},
      {"t": "tile", "x": 12, "y": 432, "w": 192, "h": 136, "v": "ect", "title_en": "COOLANT", "unit": "", "dec": 0, "warn": 105},
      {"t": "shift", "x": 26, "y": 546, "w": 110, "h": 9, "v": "ect", "mode": "level", "color": "#00E05A", "off": "#333333", "from": 40, "to": 130, "n": 6, "gap": 4, "radius": 1},
      {"t": "tile", "x": 213, "y": 432, "w": 192, "h": 136, "v": "iat", "title_en": "IAT °C", "unit": "", "dec": 0},
      {"t": "shift", "x": 227, "y": 546, "w": 110, "h": 9, "v": "iat", "mode": "level", "color": "#00E05A", "off": "#333333", "from": 0, "to": 80, "n": 6, "gap": 4, "radius": 1},
      {"t": "tile", "x": 414, "y": 432, "w": 192, "h": 136, "v": "volt", "title_en": "BATT V", "unit": "", "dec": 1, "low": 12.0},
      {"t": "shift", "x": 428, "y": 546, "w": 110, "h": 9, "v": "volt", "mode": "level", "color": "#00E05A", "off": "#333333", "from": 10, "to": 16, "n": 6, "gap": 4, "radius": 1},
      {"t": "tile", "x": 615, "y": 432, "w": 192, "h": 136, "v": "tps", "title_en": "TPS %", "unit": "", "dec": 0},
      {"t": "shift", "x": 629, "y": 546, "w": 110, "h": 9, "v": "tps", "mode": "level", "color": "#00E05A", "off": "#333333", "from": 0, "to": 100, "n": 6, "gap": 4, "radius": 1},
      {"t": "tile", "x": 816, "y": 432, "w": 192, "h": 136, "v": "load", "title_en": "LOAD %", "unit": "", "dec": 0},
      {"t": "shift", "x": 830, "y": 546, "w": 110, "h": 9, "v": "load", "mode": "level", "color": "#00E05A", "off": "#333333", "from": 0, "to": 100, "n": 6, "gap": 4, "radius": 1}
    ]},
    {"name": "RACE DASH", "w": [
      {"t": "shift", "x": 17, "y": 12, "w": 990, "h": 34, "v": "rpm", "from": 3850, "to": 7000, "n": 12, "gap": 10, "off": "#2A2A2A"},
      {"t": "bar", "x": 17, "y": 58, "w": 990, "h": 14, "v": "rpm", "min": 0, "max": 8000, "color": "#FF7A00"},
      {"t": "label", "x": 17, "y": 92, "w": 300, "text": "Water TMP", "font": "t28", "color": "#FF7A00"},
      {"t": "value", "x": 17, "y": 128, "w": 240, "v": "ect", "font": "n60", "dec": 0, "align": "center", "color": "#FF7A00", "warn": 105},
      {"t": "label", "x": 707, "y": 92, "w": 300, "text": "Km/h", "font": "t28", "color": "#FF7A00", "align": "right"},
      {"t": "value", "x": 767, "y": 128, "w": 240, "v": "spd", "font": "n60", "dec": 0, "align": "center", "color": "#FF7A00"},
      {"t": "label", "x": 17, "y": 216, "w": 300, "text": "Air TMP", "font": "t28", "color": "#FF7A00"},
      {"t": "value", "x": 17, "y": 252, "w": 240, "v": "iat", "font": "n60", "dec": 0, "align": "center", "color": "#FF7A00"},
      {"t": "label", "x": 707, "y": 216, "w": 300, "text": "ENGINE L", "font": "t28", "color": "#FF7A00", "align": "right"},
      {"t": "value", "x": 767, "y": 252, "w": 240, "v": "load", "font": "n60", "dec": 1, "align": "center", "color": "#FF7A00"},
      {"t": "label", "x": 17, "y": 340, "w": 300, "text": "Air Press", "font": "t28", "color": "#FF7A00"},
      {"t": "value", "x": 17, "y": 376, "w": 240, "v": "map", "font": "n60", "dec": 0, "align": "center", "color": "#FF7A00"},
      {"t": "label", "x": 707, "y": 340, "w": 300, "text": "Short FT", "font": "t28", "color": "#FF7A00", "align": "right"},
      {"t": "value", "x": 767, "y": 376, "w": 240, "v": "stft", "font": "n60", "dec": 1, "align": "center", "color": "#FF7A00"},
      {"t": "label", "x": 17, "y": 464, "w": 300, "text": "AIR FLOW", "font": "t28", "color": "#FF7A00"},
      {"t": "value", "x": 17, "y": 500, "w": 240, "v": "maf", "font": "n60", "dec": 1, "align": "center", "color": "#FF7A00"},
      {"t": "label", "x": 707, "y": 464, "w": 300, "text": "Batt Vol", "font": "t28", "color": "#FF7A00", "align": "right"},
      {"t": "value", "x": 767, "y": 500, "w": 240, "v": "volt", "font": "n60", "dec": 1, "align": "center", "color": "#FF7A00", "low": 12.0},
      {"t": "value", "x": 262, "y": 96, "w": 250, "v": "rpm", "font": "n60", "dec": 0, "align": "right", "color": "#FF7A00", "warn": 6500},
      {"t": "label", "x": 520, "y": 118, "w": 140, "text": "r/min", "font": "t28", "color": "#FF7A00"},
      {"t": "label", "x": 362, "y": 180, "w": 300, "text": "Race", "font": "t40", "color": "#FF7A00", "align": "center"},
      {"t": "value", "x": 312, "y": 250, "w": 400, "v": "gear", "font": "n170", "dec": 0, "align": "center", "color": "#FF7A00"}
    ]},
    {"name": "ECU", "bake": 1, "w": [
      {"t": "value", "x": 20, "y": 8, "w": 320, "v": "rpm", "font": "n96", "dec": 0, "color": "#FFFFFF", "warn": 6500},
      {"t": "label", "x": 300, "y": 72, "w": 120, "text": "RPM", "font": "t28", "color": "#FFFFFF"},
      {"t": "cover", "x": 20, "y": 110, "w": 960, "h": 32, "v": "rpm", "min": 0, "max": 8000, "grad": ["#FFE600", "#FFB000", "#FF5A00", "#FF1010"], "off": "#1A1A1A"},
      {"t": "rect", "x": 139, "y": 110, "w": 3, "h": 32, "color": "#000000", "radius": 0},
      {"t": "rect", "x": 259, "y": 110, "w": 3, "h": 32, "color": "#000000", "radius": 0},
      {"t": "rect", "x": 379, "y": 110, "w": 3, "h": 32, "color": "#000000", "radius": 0},
      {"t": "rect", "x": 499, "y": 110, "w": 3, "h": 32, "color": "#000000", "radius": 0},
      {"t": "rect", "x": 619, "y": 110, "w": 3, "h": 32, "color": "#000000", "radius": 0},
      {"t": "rect", "x": 739, "y": 110, "w": 3, "h": 32, "color": "#000000", "radius": 0},
      {"t": "rect", "x": 859, "y": 110, "w": 3, "h": 32, "color": "#000000", "radius": 0},
      {"t": "label", "x": 110, "y": 146, "w": 60, "text": "1", "font": "t20", "color": "#DDDDDD", "align": "center"},
      {"t": "label", "x": 230, "y": 146, "w": 60, "text": "2", "font": "t20", "color": "#DDDDDD", "align": "center"},
      {"t": "label", "x": 350, "y": 146, "w": 60, "text": "3", "font": "t20", "color": "#DDDDDD", "align": "center"},
      {"t": "label", "x": 470, "y": 146, "w": 60, "text": "4", "font": "t20", "color": "#DDDDDD", "align": "center"},
      {"t": "label", "x": 590, "y": 146, "w": 60, "text": "5", "font": "t20", "color": "#DDDDDD", "align": "center"},
      {"t": "label", "x": 710, "y": 146, "w": 60, "text": "6", "font": "t20", "color": "#DDDDDD", "align": "center"},
      {"t": "label", "x": 830, "y": 146, "w": 60, "text": "7", "font": "t20", "color": "#FF4040", "align": "center"},
      {"t": "label", "x": 950, "y": 146, "w": 60, "text": "8", "font": "t20", "color": "#FF4040", "align": "center"},
      {"t": "label", "x": 50, "y": 182, "w": 200, "text": "TRIP km", "font": "t28", "color": "#F5C518", "align": "right"},
      {"t": "value", "x": 50, "y": 214, "w": 200, "v": "trip", "font": "t40", "dec": 1, "align": "right"},
      {"t": "label", "x": 50, "y": 262, "w": 200, "text": "FUEL %", "font": "t28", "color": "#F5C518", "align": "right"},
      {"t": "value", "x": 50, "y": 294, "w": 200, "v": "fuel", "font": "t40", "dec": 0, "align": "right", "low": 12},
      {"t": "label", "x": 330, "y": 182, "w": 200, "text": "GEAR", "font": "t28", "color": "#F5C518", "align": "center"},
      {"t": "value", "x": 330, "y": 168, "w": 200, "v": "gear", "font": "n170", "dec": 0, "align": "center"},
      {"t": "label", "x": 700, "y": 182, "w": 280, "text": "SPEED", "font": "t28", "color": "#F5C518", "align": "right"},
      {"t": "value", "x": 560, "y": 168, "w": 420, "v": "spd", "font": "n170", "dec": 0, "align": "right"},
      {"t": "label", "x": 900, "y": 348, "w": 80, "text": "km/h", "font": "t20", "color": "#CCCCCC", "align": "right"},
      {"t": "label", "x": 5, "y": 372, "w": 190, "text": "WATER °C", "font": "t28", "color": "#F5C518", "align": "center"},
      {"t": "value", "x": 5, "y": 404, "w": 190, "v": "ect", "font": "t40", "dec": 0, "align": "center", "warn": 105},
      {"t": "label", "x": 205, "y": 372, "w": 190, "text": "BATT V", "font": "t28", "color": "#F5C518", "align": "center"},
      {"t": "value", "x": 205, "y": 404, "w": 190, "v": "volt", "font": "t40", "dec": 1, "align": "center", "low": 12.0},
      {"t": "label", "x": 405, "y": 372, "w": 190, "text": "BOOST bar", "font": "t28", "color": "#F5C518", "align": "center"},
      {"t": "value", "x": 405, "y": 404, "w": 190, "v": "boost", "font": "t40", "dec": 2, "align": "center"},
      {"t": "label", "x": 605, "y": 372, "w": 190, "text": "LOAD %", "font": "t28", "color": "#F5C518", "align": "center"},
      {"t": "value", "x": 605, "y": 404, "w": 190, "v": "load", "font": "t40", "dec": 0, "align": "center"},
      {"t": "label", "x": 805, "y": 372, "w": 190, "text": "IAT °C", "font": "t28", "color": "#F5C518", "align": "center"},
      {"t": "value", "x": 805, "y": 404, "w": 190, "v": "iat", "font": "t40", "dec": 0, "align": "center"},
      {"t": "bar", "x": 20, "y": 470, "w": 180, "h": 38, "v": "tps", "min": 0, "max": 100, "color": "#2EC84A"},
      {"t": "label", "x": 28, "y": 474, "w": 80, "text": "TPS", "font": "t28", "color": "#000000"},
      {"t": "value", "x": 110, "y": 474, "w": 84, "v": "tps", "font": "t28", "dec": 0, "align": "right", "color": "#000000"},
      {"t": "label", "x": 190, "y": 446, "w": 60, "text": "0", "font": "t20", "color": "#AAAAAA", "align": "center"},
      {"t": "label", "x": 370, "y": 446, "w": 60, "text": "50", "font": "t20", "color": "#AAAAAA", "align": "center"},
      {"t": "label", "x": 550, "y": 446, "w": 60, "text": "100", "font": "t20", "color": "#AAAAAA", "align": "center"},
      {"t": "bar", "x": 220, "y": 470, "w": 360, "h": 38, "v": "fuel", "min": 0, "max": 100, "color": "#2EC84A"},
      {"t": "label", "x": 220, "y": 474, "w": 360, "text": "FUEL", "font": "t28", "color": "#000000", "align": "center"},
      {"t": "lamp", "x": 600, "y": 470, "w": 180, "h": 38, "v": "rpm", "on": 400, "text": "ENGINE", "font": "t28", "bw": 1},
      {"t": "lamp", "x": 800, "y": 470, "w": 180, "h": 38, "v": "link", "on": 0.5, "off": "#FFC400", "text": "LINK", "font": "t28", "bw": 1}
    ]},
    {"name": "OBD2", "w": [
      {"t": "label", "x": 16, "y": 6, "w": 400, "text": "OBD2 LIVE DATA", "font": "t40", "color": "#FF7A00"},
      {"t": "label", "x": 420, "y": 20, "w": 500, "text": "values from Superlink · - = not supported", "font": "t20", "color": "#888888"},
      {"t": "label", "x": 16, "y": 76, "w": 330, "text": "Engine RPM", "font": "t28", "color": "#B8B8B8"},
      {"t": "value", "x": 316, "y": 76, "w": 120, "v": "rpm", "font": "t28", "dec": 0, "align": "right", "color": "#FFFFFF"},
      {"t": "label", "x": 444, "y": 82, "w": 70, "text": "rpm", "font": "t20", "color": "#888888"},
      {"t": "rect", "x": 16, "y": 119, "w": 488, "h": 1, "color": "#242424", "radius": 0},
      {"t": "label", "x": 16, "y": 127, "w": 330, "text": "Vehicle speed", "font": "t28", "color": "#B8B8B8"},
      {"t": "value", "x": 316, "y": 127, "w": 120, "v": "spd", "font": "t28", "dec": 0, "align": "right", "color": "#FFFFFF"},
      {"t": "label", "x": 444, "y": 133, "w": 70, "text": "km/h", "font": "t20", "color": "#888888"},
      {"t": "rect", "x": 16, "y": 170, "w": 488, "h": 1, "color": "#242424", "radius": 0},
      {"t": "label", "x": 16, "y": 178, "w": 330, "text": "Coolant temp", "font": "t28", "color": "#B8B8B8"},
      {"t": "value", "x": 316, "y": 178, "w": 120, "v": "ect", "font": "t28", "dec": 0, "align": "right", "color": "#FFFFFF"},
      {"t": "label", "x": 444, "y": 184, "w": 70, "text": "°C", "font": "t20", "color": "#888888"},
      {"t": "rect", "x": 16, "y": 221, "w": 488, "h": 1, "color": "#242424", "radius": 0},
      {"t": "label", "x": 16, "y": 229, "w": 330, "text": "Intake air temp", "font": "t28", "color": "#B8B8B8"},
      {"t": "value", "x": 316, "y": 229, "w": 120, "v": "iat", "font": "t28", "dec": 0, "align": "right", "color": "#FFFFFF"},
      {"t": "label", "x": 444, "y": 235, "w": 70, "text": "°C", "font": "t20", "color": "#888888"},
      {"t": "rect", "x": 16, "y": 272, "w": 488, "h": 1, "color": "#242424", "radius": 0},
      {"t": "label", "x": 16, "y": 280, "w": 330, "text": "Engine load", "font": "t28", "color": "#B8B8B8"},
      {"t": "value", "x": 316, "y": 280, "w": 120, "v": "load", "font": "t28", "dec": 1, "align": "right", "color": "#FFFFFF"},
      {"t": "label", "x": 444, "y": 286, "w": 70, "text": "%", "font": "t20", "color": "#888888"},
      {"t": "rect", "x": 16, "y": 323, "w": 488, "h": 1, "color": "#242424", "radius": 0},
      {"t": "label", "x": 16, "y": 331, "w": 330, "text": "Throttle position", "font": "t28", "color": "#B8B8B8"},
      {"t": "value", "x": 316, "y": 331, "w": 120, "v": "tps", "font": "t28", "dec": 1, "align": "right", "color": "#FFFFFF"},
      {"t": "label", "x": 444, "y": 337, "w": 70, "text": "%", "font": "t20", "color": "#888888"},
      {"t": "rect", "x": 16, "y": 374, "w": 488, "h": 1, "color": "#242424", "radius": 0},
      {"t": "label", "x": 16, "y": 382, "w": 330, "text": "MAP (intake press)", "font": "t28", "color": "#B8B8B8"},
      {"t": "value", "x": 316, "y": 382, "w": 120, "v": "map", "font": "t28", "dec": 0, "align": "right", "color": "#FFFFFF"},
      {"t": "label", "x": 444, "y": 388, "w": 70, "text": "kPa", "font": "t20", "color": "#888888"},
      {"t": "rect", "x": 16, "y": 425, "w": 488, "h": 1, "color": "#242424", "radius": 0},
      {"t": "label", "x": 16, "y": 433, "w": 330, "text": "MAF air flow", "font": "t28", "color": "#B8B8B8"},
      {"t": "value", "x": 316, "y": 433, "w": 120, "v": "maf", "font": "t28", "dec": 2, "align": "right", "color": "#FFFFFF"},
      {"t": "label", "x": 444, "y": 439, "w": 70, "text": "g/s", "font": "t20", "color": "#888888"},
      {"t": "rect", "x": 16, "y": 476, "w": 488, "h": 1, "color": "#242424", "radius": 0},
      {"t": "label", "x": 16, "y": 484, "w": 330, "text": "Short fuel trim B1", "font": "t28", "color": "#B8B8B8"},
      {"t": "value", "x": 316, "y": 484, "w": 120, "v": "stft", "font": "t28", "dec": 1, "align": "right", "color": "#FFFFFF"},
      {"t": "label", "x": 444, "y": 490, "w": 70, "text": "%", "font": "t20", "color": "#888888"},
      {"t": "rect", "x": 16, "y": 527, "w": 488, "h": 1, "color": "#242424", "radius": 0},
      {"t": "label", "x": 16, "y": 535, "w": 330, "text": "Timing advance", "font": "t28", "color": "#B8B8B8"},
      {"t": "value", "x": 316, "y": 535, "w": 120, "v": "timing", "font": "t28", "dec": 1, "align": "right", "color": "#FFFFFF"},
      {"t": "label", "x": 444, "y": 541, "w": 70, "text": "°", "font": "t20", "color": "#888888"},
      {"t": "rect", "x": 16, "y": 578, "w": 488, "h": 1, "color": "#242424", "radius": 0},
      {"t": "label", "x": 520, "y": 76, "w": 330, "text": "ECU voltage", "font": "t28", "color": "#B8B8B8"},
      {"t": "value", "x": 820, "y": 76, "w": 120, "v": "volt", "font": "t28", "dec": 2, "align": "right", "color": "#FFFFFF"},
      {"t": "label", "x": 948, "y": 82, "w": 70, "text": "V", "font": "t20", "color": "#888888"},
      {"t": "rect", "x": 520, "y": 119, "w": 488, "h": 1, "color": "#242424", "radius": 0},
      {"t": "label", "x": 520, "y": 127, "w": 330, "text": "Fuel level", "font": "t28", "color": "#B8B8B8"},
      {"t": "value", "x": 820, "y": 127, "w": 120, "v": "fuel", "font": "t28", "dec": 1, "align": "right", "color": "#FFFFFF"},
      {"t": "label", "x": 948, "y": 133, "w": 70, "text": "%", "font": "t20", "color": "#888888"},
      {"t": "rect", "x": 520, "y": 170, "w": 488, "h": 1, "color": "#242424", "radius": 0},
      {"t": "label", "x": 520, "y": 178, "w": 330, "text": "Air-fuel ratio", "font": "t28", "color": "#B8B8B8"},
      {"t": "value", "x": 820, "y": 178, "w": 120, "v": "afr", "font": "t28", "dec": 2, "align": "right", "color": "#FFFFFF"},
      {"t": "label", "x": 948, "y": 184, "w": 70, "text": "", "font": "t20", "color": "#888888"},
      {"t": "rect", "x": 520, "y": 221, "w": 488, "h": 1, "color": "#242424", "radius": 0},
      {"t": "label", "x": 520, "y": 229, "w": 330, "text": "Engine oil temp", "font": "t28", "color": "#B8B8B8"},
      {"t": "value", "x": 820, "y": 229, "w": 120, "v": "oil", "font": "t28", "dec": 0, "align": "right", "color": "#FFFFFF"},
      {"t": "label", "x": 948, "y": 235, "w": 70, "text": "°C", "font": "t20", "color": "#888888"},
      {"t": "rect", "x": 520, "y": 272, "w": 488, "h": 1, "color": "#242424", "radius": 0},
      {"t": "label", "x": 520, "y": 280, "w": 330, "text": "CVT temp", "font": "t28", "color": "#B8B8B8"},
      {"t": "value", "x": 820, "y": 280, "w": 120, "v": "cvt", "font": "t28", "dec": 0, "align": "right", "color": "#FFFFFF"},
      {"t": "label", "x": 948, "y": 286, "w": 70, "text": "°C", "font": "t20", "color": "#888888"},
      {"t": "rect", "x": 520, "y": 323, "w": 488, "h": 1, "color": "#242424", "radius": 0},
      {"t": "label", "x": 520, "y": 331, "w": 330, "text": "Boost", "font": "t28", "color": "#B8B8B8"},
      {"t": "value", "x": 820, "y": 331, "w": 120, "v": "boost", "font": "t28", "dec": 2, "align": "right", "color": "#FFFFFF"},
      {"t": "label", "x": 948, "y": 337, "w": 70, "text": "bar", "font": "t20", "color": "#888888"},
      {"t": "rect", "x": 520, "y": 374, "w": 488, "h": 1, "color": "#242424", "radius": 0},
      {"t": "label", "x": 520, "y": 382, "w": 330, "text": "Gear (calc)", "font": "t28", "color": "#B8B8B8"},
      {"t": "value", "x": 820, "y": 382, "w": 120, "v": "gear", "font": "t28", "dec": 0, "align": "right", "color": "#FFFFFF"},
      {"t": "label", "x": 948, "y": 388, "w": 70, "text": "", "font": "t20", "color": "#888888"},
      {"t": "rect", "x": 520, "y": 425, "w": 488, "h": 1, "color": "#242424", "radius": 0},
      {"t": "label", "x": 520, "y": 433, "w": 330, "text": "Trip", "font": "t28", "color": "#B8B8B8"},
      {"t": "value", "x": 820, "y": 433, "w": 120, "v": "trip", "font": "t28", "dec": 1, "align": "right", "color": "#FFFFFF"},
      {"t": "label", "x": 948, "y": 439, "w": 70, "text": "km", "font": "t20", "color": "#888888"},
      {"t": "rect", "x": 520, "y": 476, "w": 488, "h": 1, "color": "#242424", "radius": 0},
      {"t": "label", "x": 520, "y": 484, "w": 330, "text": "Custom C1", "font": "t28", "color": "#B8B8B8"},
      {"t": "value", "x": 820, "y": 484, "w": 120, "v": "c1", "font": "t28", "dec": 1, "align": "right", "color": "#FFFFFF"},
      {"t": "label", "x": 948, "y": 490, "w": 70, "text": "", "font": "t20", "color": "#888888"},
      {"t": "rect", "x": 520, "y": 527, "w": 488, "h": 1, "color": "#242424", "radius": 0},
      {"t": "label", "x": 520, "y": 535, "w": 330, "text": "Custom C2", "font": "t28", "color": "#B8B8B8"},
      {"t": "value", "x": 820, "y": 535, "w": 120, "v": "c2", "font": "t28", "dec": 1, "align": "right", "color": "#FFFFFF"},
      {"t": "label", "x": 948, "y": 541, "w": 70, "text": "", "font": "t20", "color": "#888888"},
      {"t": "rect", "x": 520, "y": 578, "w": 488, "h": 1, "color": "#242424", "radius": 0}
    ]},
    {"name": "ANIME", "bg": "#000000", "w": [
      {"t": "rect", "x": 13, "y": 16, "w": 627, "h": 324, "color": "#000000", "radius": 0, "bw": 4, "border": "#FF5CC4"},
      {"t": "rect", "x": 20, "y": 23, "w": 613, "h": 310, "color": "#000000", "radius": 0, "bw": 2, "border": "#8C46E6"},
      {"t": "label", "x": 30, "y": 140, "w": 593, "text": "SEND A GIF FROM THE APP", "font": "t40", "color": "#C4B0DC", "align": "center"},
      {"t": "label", "x": 30, "y": 196, "w": 593, "text": "or SD card: skins/anime.gif (602 x 306)", "font": "t20", "color": "#C4B0DC", "align": "center"},
      {"t": "gifrpm", "x": 26, "y": 29, "w": 601, "h": 306, "src": "S:skins/anime.gif", "v": "rpm", "min": 0, "max": 5500},
      {"t": "rect", "x": 9, "y": 12, "w": 34, "h": 6, "color": "#FFF4FC", "radius": 0},
      {"t": "rect", "x": 9, "y": 12, "w": 6, "h": 34, "color": "#FFF4FC", "radius": 0},
      {"t": "rect", "x": 612, "y": 334, "w": 34, "h": 6, "color": "#FFF4FC", "radius": 0},
      {"t": "rect", "x": 640, "y": 306, "w": 6, "h": 34, "color": "#FFF4FC", "radius": 0},
      {"t": "segbar", "x": 13, "y": 356, "w": 627, "h": 30, "v": "rpm", "min": 0, "max": 8000, "red": 6500, "n": 32},
      {"t": "label", "x": 26, "y": 404, "w": 120, "text": "km/h", "font": "t28", "color": "#C4B0DC"},
      {"t": "animnum", "x": 40, "y": 350, "w": 300, "size": "L", "v": "spd", "align": "right"},
      {"t": "label", "x": 372, "y": 404, "w": 70, "text": "RPM", "font": "t28", "color": "#C4B0DC"},
      {"t": "animnum", "x": 440, "y": 380, "w": 210, "size": "M", "v": "rpm", "align": "right", "warn": 6500},
      {"t": "label", "x": 670, "y": 20, "w": 200, "text": "COOLANT", "font": "t28", "color": "#C4B0DC"},
      {"t": "label", "x": 900, "y": 20, "w": 110, "text": "°C", "font": "t28", "color": "#C4B0DC", "align": "right"},
      {"t": "animnum", "x": 700, "y": 14, "w": 315, "size": "M", "v": "ect", "align": "right", "warn": 105},
      {"t": "segbar", "x": 668, "y": 146, "w": 342, "h": 24, "v": "ect", "min": 40, "max": 130, "n": 17, "peakmark": 1, "red": 105},
      {"t": "label", "x": 670, "y": 212, "w": 200, "text": "IAT", "font": "t28", "color": "#C4B0DC"},
      {"t": "label", "x": 900, "y": 212, "w": 110, "text": "°C", "font": "t28", "color": "#C4B0DC", "align": "right"},
      {"t": "animnum", "x": 700, "y": 206, "w": 315, "size": "M", "v": "iat", "align": "right"},
      {"t": "segbar", "x": 668, "y": 338, "w": 342, "h": 24, "v": "iat", "min": -10, "max": 80, "n": 17, "peakmark": 1},
      {"t": "label", "x": 670, "y": 404, "w": 200, "text": "TPS", "font": "t28", "color": "#C4B0DC"},
      {"t": "label", "x": 900, "y": 404, "w": 110, "text": "%", "font": "t28", "color": "#C4B0DC", "align": "right"},
      {"t": "animnum", "x": 700, "y": 398, "w": 315, "size": "M", "v": "tps", "align": "right"},
      {"t": "segbar", "x": 668, "y": 530, "w": 342, "h": 24, "v": "tps", "min": 0, "max": 100, "n": 17, "peakmark": 1}
    ]},
    {"name": "PHOTO", "bake": 1, "bg": "#000000", "w": [
      {"t": "label", "x": 0, "y": 240, "w": 1024, "text": "SEND A PHOTO FROM THE APP", "font": "t40", "color": "#8A8A90", "align": "center"},
      {"t": "label", "x": 0, "y": 296, "w": 1024, "text": "or copy to SD card: skins/photo.jpg (1024 x 600)", "font": "t28", "color": "#8A8A90", "align": "center"},
      {"t": "image", "src": "S:skins/photo.jpg", "x": 0, "y": 0},
      {"t": "rect", "x": 0, "y": 480, "w": 1024, "h": 120, "color": "#000000", "opa": 150, "radius": 0},
      {"t": "value", "x": 24, "y": 482, "w": 300, "v": "spd", "font": "n96", "dec": 0},
      {"t": "label", "x": 250, "y": 548, "w": 120, "text": "km/h", "font": "t28", "color": "#C8C8CC"},
      {"t": "value", "x": 380, "y": 500, "w": 260, "v": "rpm", "font": "n60", "dec": 0, "align": "right", "color": "accent", "warn": 6500},
      {"t": "label", "x": 648, "y": 548, "w": 80, "text": "rpm", "font": "t28", "color": "#C8C8CC"},
      {"t": "value", "x": 740, "y": 500, "w": 180, "v": "ect", "font": "n60", "dec": 0, "align": "right", "warn": 105},
      {"t": "label", "x": 928, "y": 548, "w": 80, "text": "°C", "font": "t28", "color": "#C8C8CC"}
    ]},
    {"name": "PEAK", "w": [
      {"t": "tile", "x": 24, "y": 24, "w": 320, "h": 156, "v": "rpm", "peak": "max", "title_en": "MAX RPM", "unit": "rpm", "dec": 0},
      {"t": "tile", "x": 352, "y": 24, "w": 320, "h": 156, "v": "spd", "peak": "max", "title_en": "MAX SPEED", "unit": "km/h", "dec": 0},
      {"t": "tile", "x": 680, "y": 24, "w": 320, "h": 156, "v": "ect", "peak": "max", "title_en": "MAX COOLANT", "unit": "°C", "dec": 0, "warn": 105},
      {"t": "tile", "x": 24, "y": 188, "w": 320, "h": 156, "v": "iat", "peak": "max", "title_en": "MAX INTAKE AIR", "unit": "°C", "dec": 0},
      {"t": "tile", "x": 352, "y": 188, "w": 320, "h": 156, "v": "map", "peak": "max", "title_en": "MAX MAP", "unit": "kPa", "dec": 0},
      {"t": "tile", "x": 680, "y": 188, "w": 320, "h": 156, "v": "cvt", "peak": "max", "title_en": "MAX CVT TEMP", "unit": "°C", "dec": 0, "warn": 110},
      {"t": "tile", "x": 24, "y": 352, "w": 320, "h": 156, "v": "oil", "peak": "max", "title_en": "MAX OIL TEMP", "unit": "°C", "dec": 0, "warn": 125},
      {"t": "tile", "x": 352, "y": 352, "w": 320, "h": 156, "v": "volt", "peak": "min", "title_en": "MIN BATTERY", "unit": "V", "dec": 1, "low": 12.0},
      {"t": "tile", "x": 680, "y": 352, "w": 320, "h": 156, "v": "afr", "peak": "min", "title_en": "MIN AFR (RICH)", "unit": "", "dec": 1},
      {"t": "button", "x": 362, "y": 520, "w": 300, "h": 52, "text": "RESET PEAKS", "action": "peak_reset", "color": "#3A2A1A"}
    ]}
  ]
})JSON";

// ---------------------------------------------------------------- วิดเจ็ต
enum Type { T_LABEL, T_VALUE, T_TILE, T_ARC, T_BAR, T_SHIFT, T_RECT, T_IMAGE, T_TICKS, T_NEEDLE, T_ANUM, T_SEGBAR, T_LAMP, T_COVER, T_GIFRPM };

struct W {
  Type type;
  int key = -1;
  int dec = 0;
  float mn = 0, mx = 100, red = 1e9f, warn = 1e9f, low = -1e9f, from = 0, to = 1;
  lv_obj_t* obj = nullptr;      // ตัวหลัก (label ค่า / arc ชี้ค่า / bar)
  lv_obj_t* obj2 = nullptr;
  std::vector<lv_obj_t*> segs;  // ไฟเปลี่ยนเกียร์
  lv_color_t col, warnCol, lowCol, offCol;
  lv_scale_section_t* sec[2] = {nullptr, nullptr};   // ticks: ช่วงที่ติด / ติดในโซนแดง
  lv_obj_t* litArc = nullptr;                         // ticks ตอนอบรูป: arc ที่ใช้รูป "ขีดติดทั้งวง" เป็นลาย
  lv_draw_buf_t* litBuf = nullptr;
  lv_point_precise_t* pts = nullptr;                  // needle
  float cx = 0, cy = 0, r0 = 0, r1 = 0, rot = 0, sweep = 0;
  int lastI = -1;
  char last[16] = "\x01";
  float shown = 0;              // ค่าที่แสดง (หน่วงให้นุ่ม)
  int lastState = -1;
  int64_t lastTxtUs = 0;
  int peak = 0;                 // 1 = แสดงค่าสูงสุด · 2 = ค่าต่ำสุด
  int mode = 0;                 // shift: 0 ไฟรอบ เขียว/เหลือง/แดง · 1 แถบระดับ (สีเดียว / ไล่ความร้อน)
  int align = 0;                // animnum: 0 ซ้าย 1 กลาง 2 ขวา
  int bx = 0, bw = 0;           // cover: ช่วงของแถบ
  const an_set_t* aset = nullptr;
  std::vector<lv_draw_buf_t*> frames;   // gifrpm: เฟรมที่ถอดไว้ (RGB565)
  float pk = 0;                 // segbar: ค่าสูงสุดที่จำไว้         // ตัวเลขเปลี่ยนได้ไม่เกิน ~12 ครั้ง/วิ (อ่านทัน + วาดน้อยลง)
};

struct Saved { lv_obj_t* o; lv_opa_t bg, border; };
struct Page {
  char name[24];
  std::vector<W> w;
  lv_obj_t* tile = nullptr;
  lv_draw_buf_t* bake = nullptr;   // รูปส่วนนิ่งของหน้า (RGB565 เต็มจอ)
  lv_obj_t* bakeImg = nullptr;
  std::vector<lv_obj_t*> hid;      // ของนิ่งที่ซ่อนไว้ตอนใช้รูป
  std::vector<Saved> clear;        // กล่องที่มีค่าอยู่ข้างใน: ทำพื้นใสชั่วคราว
  lv_opa_t tileBg = LV_OPA_COVER;
  std::vector<lv_obj_t*> live;     // ปุ่ม / GIF: ไม่อบเป็นรูป
  std::vector<lv_obj_t*> gifs;     // หยุดเล่นตอนไม่ได้แสดงหน้านี้
  int forceBake = -1;              // "bake": 1 บังคับอบ · 0 ไม่อบ · ไม่ใส่ = อัตโนมัติ
};

static std::vector<Page> pages;
static lv_color_t acc;
static bool en = false;
static std::vector<lv_style_t*> styles;   // สไตล์ของ section (ลบตอนสร้างใหม่)
static lv_style_t* newStyle() { lv_style_t* st = (lv_style_t*)lv_malloc(sizeof(lv_style_t)); lv_style_init(st); styles.push_back(st); return st; }
static const lv_color_t C_DIM = lv_color_hex(0x8A8A90);
static const lv_color_t C_RED = lv_color_hex(0xFF3B30);
static const lv_color_t C_BLUE = lv_color_hex(0x3AA0FF);

int pageCount() { return (int)pages.size(); }
const char* pageName(int i) { return i >= 0 && i < (int)pages.size() ? pages[i].name : ""; }
lv_color_t accent() { return acc; }

static const lv_font_t* fontOf(const char* n, const lv_font_t* def) {
  if (!n) return def;
  if (!strcmp(n, "n170")) return &f_num170;
  if (!strcmp(n, "n96")) return &f_num96;
  if (!strcmp(n, "n60")) return &f_num60;
  if (!strcmp(n, "t40")) return &f_t40;
  if (!strcmp(n, "t28")) return &f_t28;
  if (!strcmp(n, "t20")) return &f_t20;
  return def;
}
static lv_color_t colorOf(const char* s, lv_color_t def) {
  if (!s) return def;
  if (!strcasecmp(s, "accent")) return acc;
  if (s[0] == '#') s++;
  if (strlen(s) != 6) return def;
  return lv_color_hex((uint32_t)strtoul(s, nullptr, 16));
}

static const char* jStr(cJSON* o, const char* k) {
  cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k);
  return cJSON_IsString(v) ? v->valuestring : nullptr;
}
static float jNum(cJSON* o, const char* k, float def) {
  cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k);
  return cJSON_IsNumber(v) ? (float)v->valuedouble : def;
}
static int jInt(cJSON* o, const char* k, int def) { return (int)lroundf(jNum(o, k, (float)def)); }
// ข้อความตามภาษา: "text" / "text_en" / "text_th"
static const char* jTxt(cJSON* o, const char* k) {
  char kk[24];
  snprintf(kk, sizeof(kk), "%s_%s", k, en ? "en" : "th");
  const char* v = jStr(o, kk);
  return v ? v : jStr(o, k);
}

static lv_text_align_t alignOf(const char* a) {
  if (a && !strcmp(a, "center")) return LV_TEXT_ALIGN_CENTER;
  if (a && !strcmp(a, "right")) return LV_TEXT_ALIGN_RIGHT;
  return LV_TEXT_ALIGN_LEFT;
}

// LVGL เผื่อขอบป้ายไว้ 1/4 ความสูงตัวอักษร (เผื่อตัวเอียง) — ตัวเลขใหญ่ = เผื่อ 30-50 px รอบป้าย → วาดใหม่กว้างเกินจำเป็น
// ป้ายเราตัดข้อความในกรอบอยู่แล้ว (LONG_CLIP) จึงเผื่อแค่ 2 px
static void tightExt(lv_event_t* e) {
  int32_t* s = (int32_t*)lv_event_get_param(e);
  if (s) *s = 2;
}
static lv_obj_t* mkLabel(lv_obj_t* p, const char* txt, const lv_font_t* f, lv_color_t c, int x, int y, int w, lv_text_align_t al) {
  lv_obj_t* l = lv_label_create(p);
  lv_obj_add_event_cb(l, tightExt, LV_EVENT_REFR_EXT_DRAW_SIZE, nullptr);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, c, 0);
  lv_obj_set_style_text_align(l, al, 0);
  lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
  if (w > 0) lv_obj_set_width(l, w);
  lv_obj_set_pos(l, x, y);
  lv_label_set_text(l, txt ? txt : "");
  lv_obj_refresh_ext_draw_size(l);
  return l;
}

static void plain(lv_obj_t* o) {
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(o, LV_OBJ_FLAG_EVENT_BUBBLE);   // ให้ปัดหน้าได้แม้แตะโดนวิดเจ็ต
}

static void mkArc(lv_obj_t* p, W& w, cJSON* j) {
  int x = jInt(j, "x", 0), y = jInt(j, "y", 0), sz = jInt(j, "size", 400), th = jInt(j, "width", 28);
  w.mn = jNum(j, "min", 0);
  w.mx = jNum(j, "max", 8000);
  w.red = jNum(j, "red", 1e9f);
  w.col = colorOf(jStr(j, "color"), acc);
  w.warnCol = C_RED;
  auto base = [&](lv_obj_t* a) {
    lv_obj_set_size(a, sz, sz);
    lv_obj_set_pos(a, x, y);
    lv_arc_set_rotation(a, 135);
    lv_arc_set_bg_angles(a, 0, 270);
    lv_obj_remove_style(a, nullptr, LV_PART_KNOB);
    lv_obj_set_style_arc_rounded(a, false, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, false, LV_PART_INDICATOR);
    plain(a);
  };
  lv_obj_t* bg = lv_arc_create(p);                // ราง
  base(bg);
  lv_obj_set_style_arc_width(bg, th, LV_PART_MAIN);
  lv_obj_set_style_arc_color(bg, lv_color_hex(0x1C1C22), LV_PART_MAIN);
  lv_obj_set_style_arc_opa(bg, LV_OPA_TRANSP, LV_PART_INDICATOR);
  if (w.red < w.mx) {                             // โซนแดง
    lv_obj_t* r = lv_arc_create(p);
    base(r);
    int a0 = (int)lroundf(270.f * (w.red - w.mn) / (w.mx - w.mn));
    lv_arc_set_bg_angles(r, a0, 270);
    lv_obj_set_style_arc_width(r, th / 3, LV_PART_MAIN);
    lv_obj_set_style_arc_color(r, lv_color_hex(0xB01818), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(r, LV_OPA_TRANSP, LV_PART_INDICATOR);
  }
  lv_obj_t* a = lv_arc_create(p);                 // ตัวชี้ค่า
  base(a);
  lv_arc_set_range(a, 0, 1000);
  lv_arc_set_value(a, 0);
  lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_arc_width(a, th, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(a, w.col, LV_PART_INDICATOR);
  w.obj = a;
  float step = jNum(j, "scale", 0);               // ตัวเลขรอบวง (เช่น ทุก 1000)
  if (step > 0) {
    int n = (int)lroundf((w.mx - w.mn) / step);
    lv_obj_t* s = lv_scale_create(p);
    int inset = th + 10;
    lv_obj_set_size(s, sz - inset * 2, sz - inset * 2);
    lv_obj_set_pos(s, x + inset, y + inset);
    lv_scale_set_mode(s, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_angle_range(s, 270);
    lv_scale_set_rotation(s, 135);
    lv_scale_set_range(s, 0, n);
    lv_scale_set_total_tick_count(s, n * 2 + 1);
    lv_scale_set_major_tick_every(s, 2);
    lv_scale_set_label_show(s, true);
    lv_obj_set_style_text_font(s, &f_t28, 0);
    lv_obj_set_style_text_color(s, lv_color_hex(0xC8C8CC), 0);
    lv_obj_set_style_length(s, 14, LV_PART_INDICATOR);
    lv_obj_set_style_length(s, 7, LV_PART_ITEMS);
    lv_obj_set_style_line_width(s, 3, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(s, 2, LV_PART_ITEMS);
    lv_obj_set_style_line_color(s, lv_color_hex(0x9A9AA0), LV_PART_INDICATOR);
    lv_obj_set_style_line_color(s, lv_color_hex(0x55555C), LV_PART_ITEMS);
    lv_obj_set_style_arc_width(s, 0, LV_PART_MAIN);
    plain(s);
  }
}

static void mkTile(lv_obj_t* p, W& w, cJSON* j) {
  int x = jInt(j, "x", 0), y = jInt(j, "y", 0), ww = jInt(j, "w", 200), hh = jInt(j, "h", 140);
  lv_obj_t* t = lv_obj_create(p);
  lv_obj_set_pos(t, x, y);
  lv_obj_set_size(t, ww, hh);
  lv_obj_set_style_bg_color(t, colorOf(jStr(j, "bg"), lv_color_hex(0x141418)), 0);
  lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(t, lv_color_hex(0x2A2A32), 0);
  lv_obj_set_style_border_width(t, 1, 0);
  lv_obj_set_style_radius(t, 14, 0);
  lv_obj_set_style_pad_all(t, 0, 0);
  plain(t);
  bool row = ww > hh * 2.2f;                      // ช่องยาว: ชื่อซ้าย ค่าขวา
  bool big = ww >= 300 || row;
  const lv_font_t* tf = big ? &f_t28 : &f_t20;
  const lv_font_t* vf = fontOf(jStr(j, "font"), hh >= 160 && ww >= 300 ? &f_num96 : &f_num60);
  lv_color_t accC = colorOf(jStr(j, "color"), lv_color_hex(0xF2F2F4));
  mkLabel(t, jTxt(j, "title"), tf, C_DIM, 16, 10, ww - 32, LV_TEXT_ALIGN_LEFT);
  const char* unit = jStr(j, "unit");
  int vh = vf->line_height;
  if (row) {
    if (unit) mkLabel(t, unit, &f_t20, C_DIM, 16, hh - 34, 120, LV_TEXT_ALIGN_LEFT);
    w.obj = mkLabel(t, "-", vf, accC, 120, (hh - vh) / 2, ww - 140, LV_TEXT_ALIGN_RIGHT);
  } else {
    if (unit) mkLabel(t, unit, &f_t20, C_DIM, 16, hh - 32, ww - 32, LV_TEXT_ALIGN_RIGHT);
    int top = tf->line_height + 8;
    w.obj = mkLabel(t, "-", vf, accC, 4, top + (hh - top - 28 - vh) / 2, ww - 8, LV_TEXT_ALIGN_CENTER);
  }
  w.col = accC;
}

static void mkShift(lv_obj_t* p, W& w, cJSON* j) {
  int x = jInt(j, "x", 0), y = jInt(j, "y", 0), ww = jInt(j, "w", 900), hh = jInt(j, "h", 20), n = jInt(j, "n", 16);
  if (n < 2) n = 2;
  if (n > 64) n = 64;
  w.from = jNum(j, "from", 5000);
  w.to = jNum(j, "to", 7000);
  w.red = jNum(j, "red", 1e9f);
  w.offCol = colorOf(jStr(j, "off"), lv_color_hex(0x1A1A20));
  const char* md = jStr(j, "mode");
  if (md && !strcmp(md, "level")) w.mode = 1;          // แถบระดับ: ติดตามค่า from→to สีเดียว (เกิน red = แดง)
  if (md && !strcmp(md, "heat")) w.mode = 2;           // แถบระดับไล่ความร้อน: สีหลัก → แดง
  w.col = colorOf(jStr(j, "color"), lv_color_hex(0x2BD46A));
  int gap = jInt(j, "gap", 6), sw = (ww - gap * (n - 1)) / n;
  int tallEvery = jInt(j, "tall_every", 0), tallH = jInt(j, "tall_h", 0);
  int rad = jInt(j, "radius", hh / 3);
  for (int i = 0; i < n; i++) {
    lv_obj_t* s = lv_obj_create(p);
    int h2 = (tallEvery > 0 && i % tallEvery == 0) ? hh + tallH : hh;
    lv_obj_set_pos(s, x + i * (sw + gap), y);
    lv_obj_set_size(s, sw, h2);
    lv_obj_set_style_radius(s, rad, 0);
    lv_obj_set_style_border_width(s, 0, 0);
    lv_obj_set_style_pad_all(s, 0, 0);
    lv_obj_set_style_bg_color(s, w.offCol, 0);
    plain(s);
    w.segs.push_back(s);
  }
}

static void mkBar(lv_obj_t* p, W& w, cJSON* j) {
  lv_obj_t* b = lv_bar_create(p);
  lv_obj_set_pos(b, jInt(j, "x", 0), jInt(j, "y", 0));
  lv_obj_set_size(b, jInt(j, "w", 400), jInt(j, "h", 24));
  w.mn = jNum(j, "min", 0);
  w.mx = jNum(j, "max", 100);
  w.red = jNum(j, "red", 1e9f);
  w.col = colorOf(jStr(j, "color"), acc);
  lv_bar_set_range(b, 0, 1000);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x1C1C22), LV_PART_MAIN);
  lv_obj_set_style_bg_color(b, w.col, LV_PART_INDICATOR);
  lv_obj_set_style_radius(b, 6, LV_PART_MAIN);
  lv_obj_set_style_radius(b, 6, LV_PART_INDICATOR);
  lv_obj_set_style_anim_duration(b, 0, 0);
  plain(b);
  w.obj = b;
}


// ---- ตัวเลขแบบไตเติลอนิเมะ: รูปทีละตัวอักษร (ชมพู→ขาว ขอบขาว+ม่วง เงาเยื้อง)
static const an_glyph_t* anGlyph(const an_set_t* st, char c) {
  for (int i = 0; i < st->n; i++) if (st->g[i].ch == c) return &st->g[i];
  return nullptr;
}
static void mkAnum(lv_obj_t* p, W& w, cJSON* j) {
  const char* sz = jStr(j, "size");
  w.aset = sz && !strcmp(sz, "L") ? &an_L : &an_M;
  lv_obj_t* box = lv_obj_create(p);
  lv_obj_remove_style_all(box);
  lv_obj_set_pos(box, jInt(j, "x", 0), jInt(j, "y", 0));
  lv_obj_set_size(box, jInt(j, "w", 300), w.aset->h);
  plain(box);
  for (int i = 0; i < 7; i++) {
    lv_obj_t* im = lv_image_create(box);
    lv_obj_add_flag(im, LV_OBJ_FLAG_HIDDEN);
    plain(im);
  }
  const char* al = jStr(j, "align");
  w.align = al && !strcmp(al, "center") ? 1 : al && !strcmp(al, "right") ? 2 : 0;
  w.obj = box;
}
static void anumSet(W& w, const char* txt, bool hot) {
  int n = (int)strlen(txt);
  if (n > 7) n = 7;
  int total = 0;
  for (int i = 0; i < n; i++) { const an_glyph_t* g = anGlyph(w.aset, txt[i]); if (g) total += g->adv + w.aset->outline; }
  int bw = lv_obj_get_width(w.obj);
  int x = w.align == 2 ? bw - total - w.aset->outline : w.align == 1 ? (bw - total) / 2 : 0;
  for (int i = 0; i < 7; i++) {
    lv_obj_t* im = lv_obj_get_child(w.obj, i);
    const an_glyph_t* g = i < n ? anGlyph(w.aset, txt[i]) : nullptr;
    if (!g) { lv_obj_add_flag(im, LV_OBJ_FLAG_HIDDEN); continue; }
    lv_image_set_src(im, g->img);
    lv_obj_set_pos(im, x + g->ofs, 0);
    lv_obj_set_style_image_recolor(im, lv_color_hex(0xFF2030), 0);
    lv_obj_set_style_image_recolor_opa(im, hot ? 150 : 0, 0);
    lv_obj_remove_flag(im, LV_OBJ_FLAG_HIDDEN);
    x += g->adv + w.aset->outline;
  }
}

// ---- แถบช่องเฉียง (แบบหน้า Anime): ม่วง→ชมพู→ขาว · เกิน red = แดง · ขีดขาว = ค่าสูงสุด
static void segbarDraw(lv_event_t* e) {
  W* w = (W*)lv_event_get_user_data(e);
  lv_obj_t* o = (lv_obj_t*)lv_event_get_current_target(e);
  lv_layer_t* layer = lv_event_get_layer(e);
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  int n = w->bw, H = lv_area_get_height(&a), Wd = lv_area_get_width(&a);
  float pitch = (float)(Wd - 4) / n, sk = H * 0.35f;
  float f = (w->shown - w->mn) / (w->mx - w->mn), fr = (w->red - w->mn) / (w->mx - w->mn), fp = (w->pk - w->mn) / (w->mx - w->mn);
  int pkSeg = w->peak ? (int)fminf(n - 1, fmaxf(0, fp * n)) : -1;
  bool flash = w->shown >= w->red && w->lastState;
  for (int s2 = 0; s2 < n; s2++) {
    float t = (float)s2 / n;
    bool lit = t < f, rz = t >= fr;
    lv_color_t c;
    if (s2 == pkSeg) c = lv_color_hex(0xFFF4FC);
    else if (flash) c = lv_color_hex(0xFF3246);
    else if (lit) {
      float u = t / fmaxf(fr, .01f);
      c = rz ? lv_color_hex(0xFF3246) : u < .55f ? lv_color_mix(lv_color_hex(0xFF5CC4), lv_color_hex(0x8C46E6), (uint8_t)(255 * u / .55f))
                                                 : lv_color_mix(lv_color_hex(0xFFF4FC), lv_color_hex(0xFF5CC4), (uint8_t)(255 * (u - .55f) / .45f));
    } else c = rz ? lv_color_hex(0x3C0E16) : lv_color_hex(0x221630);
    float x0 = a.x1 + s2 * pitch, x1 = x0 + pitch - 3;
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.bg_color = c;
    d.bg_opa = LV_OPA_COVER;
    typedef lv_value_precise_t P;
    d.p[0] = {(P)(x0 + sk), (P)a.y1}; d.p[1] = {(P)(x1 + sk), (P)a.y1}; d.p[2] = {(P)x0, (P)a.y2};
    lv_draw_triangle(layer, &d);
    d.p[0] = {(P)(x1 + sk), (P)a.y1}; d.p[1] = {(P)x1, (P)a.y2}; d.p[2] = {(P)x0, (P)a.y2};
    lv_draw_triangle(layer, &d);
  }
}
static void mkSegbar(lv_obj_t* p, W& w, cJSON* j) {
  lv_obj_t* o = lv_obj_create(p);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, jInt(j, "x", 0), jInt(j, "y", 0));
  lv_obj_set_size(o, jInt(j, "w", 600), jInt(j, "h", 28));
  w.mn = jNum(j, "min", 0);
  w.mx = jNum(j, "max", 100);
  w.red = jNum(j, "red", 1e9f);
  w.bw = jInt(j, "n", 20);
  if (w.bw < 2) w.bw = 2;
  if (!w.peak && jInt(j, "peakmark", 0)) w.peak = 3;   // ขีดค่าสูงสุด (ใช้ pmax)
  plain(o);
  w.obj = o;
}

// ---- ไฟสถานะ: ติดเมื่อค่า >= "on" (เช่น ENGINE รอบ > 400, LINK = 1) · มีข้อความในกล่องได้
static void mkLamp(lv_obj_t* p, W& w, cJSON* j) {
  lv_obj_t* o = lv_obj_create(p);
  lv_obj_set_pos(o, jInt(j, "x", 0), jInt(j, "y", 0));
  lv_obj_set_size(o, jInt(j, "w", 180), jInt(j, "h", 40));
  lv_obj_set_style_radius(o, jInt(j, "radius", 4), 0);
  lv_obj_set_style_border_width(o, jInt(j, "bw", 0), 0);
  lv_obj_set_style_border_color(o, colorOf(jStr(j, "border"), lv_color_hex(0x666666)), 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  w.col = colorOf(jStr(j, "color"), lv_color_hex(0x2EC84A));
  w.offCol = colorOf(jStr(j, "off"), lv_color_hex(0x555555));
  w.warnCol = colorOf(jStr(j, "warncolor"), C_RED);
  w.from = jNum(j, "on", 0.5f);
  lv_obj_set_style_bg_color(o, w.offCol, 0);
  plain(o);
  if (const char* t = jTxt(j, "text")) {
    lv_obj_t* l = lv_label_create(o);
    lv_obj_set_style_text_font(l, fontOf(jStr(j, "font"), &f_t20), 0);
    lv_obj_set_style_text_color(l, colorOf(jStr(j, "text_color"), lv_color_hex(0x000000)), 0);
    lv_label_set_text(l, t);
    lv_obj_center(l);
  }
  w.obj = o;
}

// ---- แถบไล่สีแบบ ECU: พื้นไล่สี (นิ่ง อบเป็นรูปได้) + แผ่นทึบปิดส่วนที่ยังไม่ถึง (ขยับตามค่า)
static void mkCover(lv_obj_t* p, W& w, cJSON* j) {
  int x = jInt(j, "x", 0), y = jInt(j, "y", 0), ww = jInt(j, "w", 900), hh = jInt(j, "h", 30);
  w.mn = jNum(j, "min", 0);
  w.mx = jNum(j, "max", 8000);
  cJSON* g = cJSON_GetObjectItemCaseSensitive(j, "grad");
  int ns = cJSON_IsArray(g) ? cJSON_GetArraySize(g) : 0;
  if (ns >= 2) {
    for (int i = 0; i < ns - 1; i++) {
      int xa = x + ww * i / (ns - 1), xb = x + ww * (i + 1) / (ns - 1);
      lv_obj_t* r = lv_obj_create(p);
      lv_obj_remove_style_all(r);
      lv_obj_set_pos(r, xa, y);
      lv_obj_set_size(r, xb - xa, hh);
      lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(r, colorOf(cJSON_GetArrayItem(g, i)->valuestring, acc), 0);
      lv_obj_set_style_bg_grad_color(r, colorOf(cJSON_GetArrayItem(g, i + 1)->valuestring, acc), 0);
      lv_obj_set_style_bg_grad_dir(r, LV_GRAD_DIR_HOR, 0);
      plain(r);
    }
  }
  lv_obj_t* c = lv_obj_create(p);
  lv_obj_remove_style_all(c);
  lv_obj_set_pos(c, x, y);
  lv_obj_set_size(c, ww, hh);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(c, colorOf(jStr(j, "off"), lv_color_hex(0x1A1A1A)), 0);
  plain(c);
  w.bx = x;
  w.bw = ww;
  w.obj = c;
}

// ---- GIF ตามรอบ (แบบหน้า Anime ของ 4.3): ถอดทุกเฟรมเก็บไว้ (RGB565) แล้วเลือกเฟรมตามรอบ — เหยียบ = เดินหน้า ปล่อย = ถอยกลับ
static void mkGifRpm(lv_obj_t* p, Page& pg, W& w, cJSON* j) {
  const char* src = jStr(j, "src");
  lv_obj_t* im = lv_image_create(p);
  lv_obj_set_pos(im, jInt(j, "x", 0), jInt(j, "y", 0));
  plain(im);
  w.obj = im;
  w.mn = jNum(j, "min", 0);
  w.mx = jNum(j, "max", 5500);
  if (!src) return;
  gd_GIF* gif = gd_open_gif_file(src);
  if (!gif) { ESP_LOGW("skin", "gifrpm: open %s failed", src); return; }
  size_t one = (size_t)gif->width * gif->height * 2;
  int nf = 0;
  gif->loop_count = 1;                                      // ไม่ให้วนซ้ำ (GIF ที่ตั้งเล่นวนไม่รู้จบ)
  while (nf < 400 && gd_get_frame(gif) == 1) nf++;          // นับเฟรมก่อน
  int budget = (int)((3u * 1024 * 1024) / (one ? one : 1)); // ไม่เกิน ~3 MB
  int step = nf > budget && budget > 0 ? (nf + budget - 1) / budget : 1;
  gd_rewind(gif);
  int k = 0;
  while (k < 400 && gd_get_frame(gif) == 1) {
    gd_render_frame(gif, gif->canvas);
    if (k++ % step) continue;
    lv_draw_buf_t* b = lv_draw_buf_create(gif->width, gif->height, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
    if (!b) break;
    const uint8_t* c = gif->canvas;                         // ARGB8888 (B,G,R,A)
    for (int y = 0; y < gif->height; y++) {
      uint16_t* d = (uint16_t*)(b->data + y * b->header.stride);
      for (int x = 0; x < gif->width; x++, c += 4) d[x] = (uint16_t)(((c[2] & 0xF8) << 8) | ((c[1] & 0xFC) << 3) | (c[0] >> 3));
    }
    w.frames.push_back(b);
  }
  gd_close_gif(gif);
  if (!w.frames.empty()) lv_image_set_src(im, w.frames[0]);
  int zw = jInt(j, "w", 0);                                 // ขยายให้พอดีกรอบ (ถ้าให้ "w")
  if (zw > 0 && !w.frames.empty()) {
    lv_image_set_scale(im, (uint32_t)(256 * zw / w.frames[0]->header.w));
    lv_obj_set_size(im, zw, jInt(j, "h", w.frames[0]->header.h * zw / w.frames[0]->header.w));
    lv_image_set_inner_align(im, LV_IMAGE_ALIGN_STRETCH);
  }
  pg.live.push_back(im);
  ESP_LOGI("skin", "gifrpm %s: %d of %d frames", src, (int)w.frames.size(), nf);
}

// รูป: .gif = เล่นวน (ไม่อบเป็นรูปนิ่ง · หยุดเล่นตอนไม่ได้อยู่หน้านั้น) · อื่น ๆ (jpg/png) = รูปนิ่ง
static bool isGif(const char* src) {
  size_t n = strlen(src);
  return n > 4 && !strcasecmp(src + n - 4, ".gif");
}
static lv_obj_t* mkImage(lv_obj_t* tile, Page& pg, const char* src) {
  lv_obj_t* im;
  if (isGif(src)) {
    im = lv_gif_create(tile);
    lv_gif_set_src(im, src);
    pg.live.push_back(im);
    pg.gifs.push_back(im);
  } else {
    im = lv_image_create(tile);
    lv_image_set_src(im, src);
  }
  plain(im);
  return im;
}
static void onPeakReset(lv_event_t*) { vd::resetPeaks(); }

static void buildWidget(lv_obj_t* tile, Page& pg, cJSON* j) {
  const char* t = jStr(j, "t");
  if (!t) return;
  W w;
  w.col = lv_color_hex(0xF2F2F4);
  w.warnCol = C_RED;
  w.lowCol = C_BLUE;
  const char* k = jStr(j, "v");
  if (k) w.key = vd::keyOf(k);
  w.dec = jInt(j, "dec", 0);
  w.warn = jNum(j, "warn", 1e9f);
  w.low = jNum(j, "low", -1e9f);
  if (const char* pk = jStr(j, "peak")) w.peak = !strcmp(pk, "min") ? 2 : 1;
  if (!strcmp(t, "label")) {
    w.type = T_LABEL;
    mkLabel(tile, jTxt(j, "text"), fontOf(jStr(j, "font"), &f_t28), colorOf(jStr(j, "color"), lv_color_hex(0xF2F2F4)),
            jInt(j, "x", 0), jInt(j, "y", 0), jInt(j, "w", 0), alignOf(jStr(j, "align")));
    return;   // ไม่ต้องอัปเดต
  } else if (!strcmp(t, "value")) {
    w.type = T_VALUE;
    w.col = colorOf(jStr(j, "color"), lv_color_hex(0xF2F2F4));
    w.obj = mkLabel(tile, "-", fontOf(jStr(j, "font"), &f_num96), w.col, jInt(j, "x", 0), jInt(j, "y", 0), jInt(j, "w", 300),
                    alignOf(jStr(j, "align")));
  } else if (!strcmp(t, "tile")) {
    w.type = T_TILE;
    mkTile(tile, w, j);
  } else if (!strcmp(t, "arc")) {
    w.type = T_ARC;
    mkArc(tile, w, j);
  } else if (!strcmp(t, "bar")) {
    w.type = T_BAR;
    mkBar(tile, w, j);
  } else if (!strcmp(t, "shift")) {
    w.type = T_SHIFT;
    mkShift(tile, w, j);
  } else if (!strcmp(t, "rect") || !strcmp(t, "circle")) {
    lv_obj_t* r = lv_obj_create(tile);
    bool circ = t[0] == 'c';
    if (circ) {                                     // วงกลม: x,y = จุดศูนย์กลาง · r = รัศมี
      int rr = jInt(j, "r", 100);
      lv_obj_set_pos(r, jInt(j, "x", 0) - rr, jInt(j, "y", 0) - rr);
      lv_obj_set_size(r, rr * 2, rr * 2);
      lv_obj_set_style_radius(r, LV_RADIUS_CIRCLE, 0);
    } else {
      lv_obj_set_pos(r, jInt(j, "x", 0), jInt(j, "y", 0));
      lv_obj_set_size(r, jInt(j, "w", 100), jInt(j, "h", 100));
      lv_obj_set_style_radius(r, jInt(j, "radius", 12), 0);
    }
    lv_obj_set_style_bg_color(r, colorOf(jStr(j, "color"), lv_color_hex(0x141418)), 0);
    lv_obj_set_style_bg_opa(r, (lv_opa_t)jInt(j, "opa", 255), 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    if (jStr(j, "grad")) {                          // ไล่สีบน→ล่าง
      lv_obj_set_style_bg_grad_color(r, colorOf(jStr(j, "grad"), lv_color_hex(0)), 0);
      lv_obj_set_style_bg_grad_dir(r, LV_GRAD_DIR_VER, 0);
    }
    lv_obj_set_style_border_width(r, jInt(j, "bw", 0), 0);
    lv_obj_set_style_border_color(r, colorOf(jStr(j, "border"), lv_color_hex(0x2A2A32)), 0);
    plain(r);
    return;
  } else if (!strcmp(t, "ticks")) {
    w.type = T_TICKS;
    int cx = jInt(j, "x", 0), cy = jInt(j, "y", 0), rr = jInt(j, "r", 200), n = jInt(j, "n", 100);
    w.mn = jNum(j, "min", 0);
    w.mx = jNum(j, "max", 8000);
    w.red = jNum(j, "red", 1e9f);
    lv_obj_t* sc = lv_scale_create(tile);
    lv_obj_set_pos(sc, cx - rr, cy - rr);
    lv_obj_set_size(sc, rr * 2, rr * 2);
    lv_scale_set_mode(sc, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_rotation(sc, jInt(j, "rot", 135));
    lv_scale_set_angle_range(sc, jInt(j, "sweep", 270));
    lv_scale_set_range(sc, (int32_t)w.mn, (int32_t)w.mx);
    lv_scale_set_total_tick_count(sc, n + 1);
    bool labels = jInt(j, "labels", 0) != 0;
    lv_scale_set_major_tick_every(sc, labels ? 1 : n + 2);
    lv_scale_set_label_show(sc, labels);
    int len = jInt(j, "len", 14), lw = jInt(j, "width", 4);
    w.r1 = (float)len;
    w.rot = (float)jInt(j, "rot", 135);
    w.sweep = (float)jInt(j, "sweep", 270);
    lv_color_t off = colorOf(jStr(j, "off"), lv_color_hex(0x5A5A5A));
    lv_color_t on = colorOf(jStr(j, "on"), lv_color_hex(0xF2F2F2));
    lv_color_t redon = colorOf(jStr(j, "redon"), on);
    lv_obj_set_style_arc_width(sc, 0, LV_PART_MAIN);
    lv_obj_set_style_length(sc, len, LV_PART_ITEMS);
    lv_obj_set_style_length(sc, len, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(sc, lw, LV_PART_ITEMS);
    lv_obj_set_style_line_width(sc, lw, LV_PART_INDICATOR);
    lv_obj_set_style_line_color(sc, off, LV_PART_ITEMS);
    lv_obj_set_style_line_color(sc, off, LV_PART_INDICATOR);
    if (labels) {
      lv_scale_set_range(sc, (int32_t)(w.mn / 1000), (int32_t)(w.mx / 1000));   // ตัวเลขเป็นหลักพัน
      lv_obj_set_style_text_font(sc, fontOf(jStr(j, "font"), &f_t40), LV_PART_INDICATOR);
      lv_obj_set_style_text_color(sc, colorOf(jStr(j, "label"), lv_color_hex(0xE8E8E8)), LV_PART_INDICATOR);
      lv_obj_set_style_text_letter_space(sc, jInt(j, "gap", 10), LV_PART_INDICATOR);   // ระยะตัวเลขจากขีด
      if (w.red < w.mx) {
        lv_scale_section_t* rs = lv_scale_add_section(sc);
        lv_scale_section_set_range(rs, (int32_t)ceilf(w.red / 1000), (int32_t)(w.mx / 1000));
        lv_style_t* st = newStyle();
        lv_style_set_text_color(st, colorOf(jStr(j, "redlabel"), lv_color_hex(0xFF6A6A)));
        lv_style_set_line_color(st, off);
        lv_style_set_line_width(st, lw);
        lv_style_set_length(st, len);
        lv_scale_section_set_style(rs, LV_PART_INDICATOR, st);
      }
    }
    plain(sc);
    w.obj = sc;
    if (w.key >= 0 && !labels) {                    // ขีดที่ติดตามค่า (ขาว) + ในโซนแดง (แดง) — section ที่เพิ่มทีหลังทับก่อน
      lv_style_t* s1 = newStyle();
      lv_style_set_line_color(s1, on);
      lv_style_set_line_width(s1, lw);
      lv_style_set_length(s1, len);
      w.sec[0] = lv_scale_add_section(sc);
      lv_scale_section_set_style(w.sec[0], LV_PART_ITEMS, s1);
      lv_scale_section_set_range(w.sec[0], (int32_t)w.mx + 1, (int32_t)w.mx + 1);
      lv_style_t* s2 = newStyle();
      lv_style_set_line_color(s2, redon);
      lv_style_set_line_width(s2, lw);
      lv_style_set_length(s2, len);
      w.sec[1] = lv_scale_add_section(sc);
      lv_scale_section_set_style(w.sec[1], LV_PART_ITEMS, s2);
      lv_scale_section_set_range(w.sec[1], (int32_t)w.mx + 1, (int32_t)w.mx + 1);
    } else {
      return;   // ไม่ต้องอัปเดต
    }
  } else if (!strcmp(t, "needle")) {
    w.type = T_NEEDLE;
    w.cx = jNum(j, "x", 0); w.cy = jNum(j, "y", 0);
    w.r0 = jNum(j, "r0", 100); w.r1 = jNum(j, "r1", 140);
    w.rot = jNum(j, "rot", 135); w.sweep = jNum(j, "sweep", 270);
    w.mn = jNum(j, "min", 0); w.mx = jNum(j, "max", 8000);
    lv_obj_t* ln = lv_line_create(tile);
    w.pts = (lv_point_precise_t*)lv_malloc(sizeof(lv_point_precise_t) * 2);
    memset(w.pts, 0, sizeof(lv_point_precise_t) * 2);
    lv_obj_set_pos(ln, 0, 0);
    lv_obj_set_size(ln, 1, 1);
    lv_obj_set_style_line_width(ln, jInt(j, "width", 10), 0);
    lv_obj_set_style_line_color(ln, colorOf(jStr(j, "color"), lv_color_hex(0xFFD21E)), 0);
    lv_obj_set_style_line_rounded(ln, true, 0);
    lv_line_set_points(ln, w.pts, 2);
    plain(ln);
    w.obj = ln;
  } else if (!strcmp(t, "image")) {
    const char* src = jStr(j, "src");
    if (!src) return;
    lv_obj_t* im = mkImage(tile, pg, src);
    lv_obj_set_pos(im, jInt(j, "x", 0), jInt(j, "y", 0));
    return;
  } else if (!strcmp(t, "animnum")) {
    w.type = T_ANUM;
    mkAnum(tile, w, j);
  } else if (!strcmp(t, "segbar")) {
    w.type = T_SEGBAR;
    mkSegbar(tile, w, j);
  } else if (!strcmp(t, "lamp")) {
    w.type = T_LAMP;
    mkLamp(tile, w, j);
  } else if (!strcmp(t, "cover")) {
    w.type = T_COVER;
    mkCover(tile, w, j);
  } else if (!strcmp(t, "gifrpm")) {
    w.type = T_GIFRPM;
    if (w.key < 0) w.key = vd::RPM;
    mkGifRpm(tile, pg, w, j);
  } else if (!strcmp(t, "button")) {             // ปุ่มแตะ: "action": "peak_reset"
    lv_obj_t* b = lv_button_create(tile);
    lv_obj_set_pos(b, jInt(j, "x", 0), jInt(j, "y", 0));
    lv_obj_set_size(b, jInt(j, "w", 240), jInt(j, "h", 56));
    lv_obj_set_style_bg_color(b, colorOf(jStr(j, "color"), lv_color_hex(0x2A2A32)), 0);
    lv_obj_set_style_radius(b, jInt(j, "radius", 12), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t* l = lv_label_create(b);
    lv_obj_set_style_text_font(l, fontOf(jStr(j, "font"), &f_t28), 0);
    lv_obj_set_style_text_color(l, colorOf(jStr(j, "text_color"), lv_color_hex(0xF2F2F4)), 0);
    lv_label_set_text(l, jTxt(j, "text") ? jTxt(j, "text") : "OK");
    lv_obj_center(l);
    lv_obj_add_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE);
    const char* act = jStr(j, "action");
    if (act && !strcmp(act, "peak_reset")) lv_obj_add_event_cb(b, onPeakReset, LV_EVENT_CLICKED, nullptr);
    pg.live.push_back(b);
    return;
  } else {
    return;
  }
  if (w.key < 0 && w.type != T_LABEL) return;
  pg.w.push_back(w);
}

// ---------------------------------------------------------------- ซ่อนหน้า (ตามชื่อหน้า · เก็บใน NVS ฝั่ง ui)
static std::vector<std::string> allNames;
static std::string hideList;              // "|ECU|OBD2|"
static bool isHiddenName(const char* nm) {
  std::string k = std::string("|") + nm + "|";
  return hideList.find(k) != std::string::npos;
}
void setHidden(const char* list) { hideList = list ? list : ""; }
const char* hiddenList() { return hideList.c_str(); }
int allCount() { return (int)allNames.size(); }
const char* allName(int i) { return i >= 0 && i < (int)allNames.size() ? allNames[i].c_str() : ""; }
bool isHidden(int i) { return i >= 0 && i < (int)allNames.size() && isHiddenName(allNames[i].c_str()); }
void setHiddenAt(int i, bool hide) {
  if (i < 0 || i >= (int)allNames.size()) return;
  std::string k = "|" + allNames[i] + "|";
  size_t at = hideList.find(k);
  if (hide && at == std::string::npos) { if (hideList.empty()) hideList = "|"; hideList += allNames[i] + "|"; }
  if (!hide && at != std::string::npos) hideList.erase(at, k.size() - 1);
  if (hideList == "|") hideList.clear();
}

int build(lv_obj_t* tv, const char* json, char* err, size_t errLen, bool english) {
  en = english;
  cJSON* root = cJSON_Parse(json);
  if (!root) {
    snprintf(err, errLen, "JSON ผิดใกล้: %.30s", cJSON_GetErrorPtr() ? cJSON_GetErrorPtr() : "?");
    return 0;
  }
  cJSON* jp = cJSON_GetObjectItemCaseSensitive(root, "pages");
  if (!cJSON_IsArray(jp) || cJSON_GetArraySize(jp) == 0) {
    snprintf(err, errLen, "ไม่มี \"pages\"");
    cJSON_Delete(root);
    return 0;
  }
  acc = colorOf(jStr(root, "accent"), lv_color_hex(0xFF7A00));
  lv_color_t bg = colorOf(jStr(root, "bg"), lv_color_hex(0x0A0A0C));
  for (int k = 0; k < (int)pages.size(); k++) {   // รูปที่อบไว้ไม่ใช่วิดเจ็ต → คืนหน่วยความจำเอง
    Page& pg = pages[k];
    if (pg.bake) lv_draw_buf_destroy(pg.bake);
    for (auto& w : pg.w) {
      if (w.litBuf) lv_draw_buf_destroy(w.litBuf);
      for (auto* f : w.frames) lv_draw_buf_destroy(f);
    }
  }
  lv_obj_clean(tv);
  for (auto& pg : pages)
    for (auto& w : pg.w)
      if (w.pts) lv_free(w.pts);
  pages.clear();
  for (auto* st : styles) { lv_style_reset(st); lv_free(st); }
  styles.clear();
  int i = 0;
  cJSON* p;
  allNames.clear();
  int nShow = 0;
  cJSON_ArrayForEach(p, jp) {
    const char* nm = jStr(p, "name") ? jStr(p, "name") : "PAGE";
    allNames.push_back(nm);
    if (!isHiddenName(nm)) nShow++;
  }
  cJSON_ArrayForEach(p, jp) {
    const char* nm = jStr(p, "name") ? jStr(p, "name") : "PAGE";
    if (nShow > 0 && isHiddenName(nm)) continue;        // ซ่อนหน้านี้ (เมนู → หน้า) · ซ่อนหมดทุกหน้า = แสดงทั้งหมด
    lv_obj_t* tile = lv_tileview_add_tile(tv, i, 0, LV_DIR_HOR);
    lv_obj_set_style_bg_color(tile, colorOf(jStr(p, "bg"), bg), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    pages.emplace_back();
    Page& pg = pages.back();
    pg.tile = tile;
    const char* img = jStr(p, "image");
    if (img) {
      lv_obj_t* im = mkImage(tile, pg, img);
      lv_obj_set_pos(im, 0, 0);
    }
    snprintf(pg.name, sizeof(pg.name), "%s", jStr(p, "name") ? jStr(p, "name") : "PAGE");
    cJSON* wl = cJSON_GetObjectItemCaseSensitive(p, "w");
    cJSON* w;
    cJSON_ArrayForEach(w, wl) buildWidget(tile, pg, w);
    pg.forceBake = jInt(p, "bake", -1);
    for (auto& ww : pg.w)
      if (ww.type == T_SEGBAR) lv_obj_add_event_cb(ww.obj, segbarDraw, LV_EVENT_DRAW_MAIN, &ww);
    i++;
  }
  cJSON_Delete(root);
  return i;
}

// ---------------------------------------------------------------- อบส่วนนิ่งเป็นรูป
static void dynObjs(Page& pg, std::vector<lv_obj_t*>& out) {
  for (auto* o : pg.live) out.push_back(o);
  for (auto& w : pg.w) {
    if (w.type == T_SHIFT) for (auto* s : w.segs) out.push_back(s);
    else if (w.type == T_TICKS) { if (w.litArc) out.push_back(w.litArc); }
    else if (w.obj) out.push_back(w.obj);
  }
}
static bool has(const std::vector<lv_obj_t*>& v, lv_obj_t* o) {
  for (auto* x : v) if (x == o) return true;
  return false;
}
static bool hasDynInside(lv_obj_t* o, const std::vector<lv_obj_t*>& dyn) {
  for (auto* d : dyn)
    for (lv_obj_t* p = lv_obj_get_parent(d); p; p = lv_obj_get_parent(p))
      if (p == o) return true;
  return false;
}
// ของนิ่งที่วางทับของที่ขยับ (เช่น ป้าย "TPS" บนบาร์) ต้องวาดสดต่อ ไม่งั้นบาร์จะทับป้ายที่อยู่ในรูป
static lv_obj_t* topOf(lv_obj_t* tile, lv_obj_t* o) {
  while (o && lv_obj_get_parent(o) != tile) o = lv_obj_get_parent(o);
  return o;
}
static bool overDyn(lv_obj_t* tile, lv_obj_t* c, const std::vector<lv_obj_t*>& dyn) {
  lv_area_t a;
  lv_obj_get_coords(c, &a);
  int ci = lv_obj_get_index(c);
  for (auto* d : dyn) {
    lv_obj_t* t = topOf(tile, d);
    if (!t || t == c || lv_obj_get_index(t) >= ci) continue;
    lv_area_t b, r;
    lv_obj_get_coords(d, &b);
    if (lv_area_intersect(&r, &a, &b)) return true;
  }
  return false;
}
static void strip(Page& pg, lv_obj_t* o, const std::vector<lv_obj_t*>& dyn) {
  uint32_t n = lv_obj_get_child_count(o);
  for (uint32_t i = 0; i < n; i++) {
    lv_obj_t* c = lv_obj_get_child(o, i);
    if (c == pg.bakeImg || has(dyn, c)) continue;
    if (lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN)) continue;
    if (o == pg.tile && overDyn(pg.tile, c, dyn)) continue;   // วาดสดต่อ (อยู่บนของที่ขยับ)
    if (hasDynInside(c, dyn)) {
      pg.clear.push_back({c, lv_obj_get_style_bg_opa(c, 0), lv_obj_get_style_border_opa(c, 0)});
      lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_opa(c, LV_OPA_TRANSP, 0);
      strip(pg, c, dyn);
    } else {
      lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
      pg.hid.push_back(c);
    }
  }
}
static void unbake(Page& pg) {
  if (!pg.bake) return;
  if (pg.bakeImg) { lv_obj_delete(pg.bakeImg); pg.bakeImg = nullptr; }
  for (auto* o : pg.hid) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  for (auto& c : pg.clear) { lv_obj_set_style_bg_opa(c.o, c.bg, 0); lv_obj_set_style_border_opa(c.o, c.border, 0); }
  pg.hid.clear();
  pg.clear.clear();
  lv_obj_set_style_bg_opa(pg.tile, pg.tileBg, 0);
  for (auto& w : pg.w) {
    if (w.litArc) { lv_obj_delete(w.litArc); w.litArc = nullptr; }
    if (w.litBuf) { lv_draw_buf_destroy(w.litBuf); w.litBuf = nullptr; }
    if (w.type == T_TICKS) { lv_obj_remove_flag(w.obj, LV_OBJ_FLAG_HIDDEN); w.lastI = -999; }
  }
  lv_draw_buf_destroy(pg.bake);
  pg.bake = nullptr;
}
static void bake(Page& pg) {
  if (pg.bake || !pg.tile) return;
  if (!pg.gifs.empty()) return;       // มี GIF (อาจเป็นพื้นหลัง) → อบแล้วลำดับชั้นเพี้ยน ไม่อบ
  bool worth = false;                 // หน้าที่มีแค่ช่องตัวเลข: พื้นสีเรียบวาดเร็วกว่าอ่านรูปจาก PSRAM → ไม่ต้องอบ
  for (auto& w : pg.w) if (w.type == T_ARC || w.type == T_TICKS || w.type == T_NEEDLE || w.type == T_COVER) worth = true;
  if (pg.forceBake >= 0) worth = pg.forceBake;
  if (!worth) return;
  int64_t t0 = esp_timer_get_time();
  lv_obj_update_layout(pg.tile);
  // 1) ขีดที่ติดตามค่า: ถ่ายรูปตอน "ติดทั้งวง" (พื้นใส) ไว้เป็นลายของ arc · แล้วคืนเป็นดับทั้งวงให้ไปอยู่ในรูปพื้น
  for (auto& w : pg.w) {
    if (w.type != T_TICKS || !w.sec[0]) continue;
    lv_scale_section_set_range(w.sec[0], (int32_t)w.mn, (int32_t)w.mx);
    if (w.red < w.mx) lv_scale_section_set_range(w.sec[1], (int32_t)w.red, (int32_t)w.mx);
    w.litBuf = lv_snapshot_take(w.obj, LV_COLOR_FORMAT_ARGB8888);
    lv_scale_section_set_range(w.sec[0], (int32_t)w.mx + 1, (int32_t)w.mx + 1);
    lv_scale_section_set_range(w.sec[1], (int32_t)w.mx + 1, (int32_t)w.mx + 1);
  }
  // 2) ซ่อนของที่ขยับ แล้วถ่ายรูปทั้งหน้า
  std::vector<lv_obj_t*> dyn;
  dynObjs(pg, dyn);
  for (auto* o : dyn) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  pg.bake = lv_snapshot_take(pg.tile, LV_COLOR_FORMAT_RGB565);
  for (auto* o : dyn) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  if (!pg.bake) {
    ESP_LOGW("skin", "bake %s: no memory (psram free %u)", pg.name, (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    for (auto& w : pg.w) if (w.litBuf) { lv_draw_buf_destroy(w.litBuf); w.litBuf = nullptr; }
    return;
  }
  // 3) ขีดที่ติดตามค่า → arc ใช้รูปเป็นลาย (วาดแค่ส่วนมุมที่เปลี่ยน แทนวาดสเกล 100 ขีดใหม่ทั้งวง)
  for (auto& w : pg.w) {
    if (w.type != T_TICKS || !w.litBuf) continue;
    lv_area_t a;
    lv_obj_get_coords(w.obj, &a);
    int sz = w.litBuf->header.w;
    int ext = (sz - lv_area_get_width(&a)) / 2;           // รูปใหญ่กว่าตัวสเกลเท่าที่ LVGL เผื่อขอบไว้
    lv_area_t tc;
    lv_obj_get_coords(pg.tile, &tc);
    lv_obj_t* arc = lv_arc_create(pg.tile);
    lv_obj_remove_style_all(arc);
    lv_obj_set_pos(arc, a.x1 - ext - tc.x1, a.y1 - ext - tc.y1);
    lv_obj_set_size(arc, sz, sz);
    lv_arc_set_rotation(arc, (int32_t)w.rot);
    lv_arc_set_bg_angles(arc, 0, (int32_t)w.sweep);
    lv_arc_set_range(arc, 0, 1000);
    lv_arc_set_value(arc, 0);
    lv_obj_set_style_arc_width(arc, ext + (int)w.r1 + 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_image_src(arc, w.litBuf, LV_PART_INDICATOR);
    plain(arc);
    w.litArc = arc;
    w.lastI = -999;
    lv_obj_add_flag(w.obj, LV_OBJ_FLAG_HIDDEN);   // สเกลตัวจริงอยู่ในรูปพื้นแล้ว
  }
  // 4) รูปพื้น + ซ่อนของนิ่ง
  pg.bakeImg = lv_image_create(pg.tile);
  lv_image_set_src(pg.bakeImg, pg.bake);
  lv_obj_set_pos(pg.bakeImg, 0, 0);
  plain(pg.bakeImg);
  lv_obj_move_to_index(pg.bakeImg, 0);
  dyn.clear();
  dynObjs(pg, dyn);
  strip(pg, pg.tile, dyn);
  pg.tileBg = lv_obj_get_style_bg_opa(pg.tile, 0);
  lv_obj_set_style_bg_opa(pg.tile, LV_OPA_TRANSP, 0);
  ESP_LOGI("skin", "baked %s in %d ms (psram free %u)", pg.name, (int)((esp_timer_get_time() - t0) / 1000),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
void setActive(int page) {
  for (int i = 0; i < (int)pages.size(); i++) {
    for (auto* g : pages[i].gifs) { if (i == page) lv_gif_resume(g); else lv_gif_pause(g); }
    if (i != page) unbake(pages[i]);
  }
  if (page >= 0 && page < (int)pages.size()) bake(pages[page]);
}

// ---------------------------------------------------------------- อัปเดต
static int64_t gNow = 0;
static void setText(W& w, const char* s) {
  if (!strcmp(w.last, s)) return;
  if (gNow - w.lastTxtUs < 80000 && w.last[0] != '\x01' && strcmp(s, "-")) return;
  w.lastTxtUs = gNow;
  strncpy(w.last, s, sizeof(w.last) - 1);
  lv_label_set_text(w.obj, s);
}
static void setColor(W& w, int state) {   // 0 ปกติ · 1 เตือน · 2 ต่ำ
  if (state == w.lastState) return;
  w.lastState = state;
  lv_obj_set_style_text_color(w.obj, state == 1 ? w.warnCol : state == 2 ? w.lowCol : w.col, 0);
}

void update(int page, const vd::Snap& s) {
  if (page < 0 || page >= (int)pages.size()) return;
  static bool blink = false;
  int64_t nowUs = esp_timer_get_time();
  gNow = nowUs;
  static int frame = 0;
  frame++;
  blink = (nowUs / 90000) & 1;
  // เข็ม/บาร์ไหลตามเวลา (ไม่ขึ้นกับจำนวนเฟรม): ค่ารถมาทีละ ~10 ครั้ง/วิ → ไหลต่อเนื่องแทนกระโดดแล้วหยุด
  static int64_t lastUs = 0;
  float dt = lastUs ? (nowUs - lastUs) / 1e6f : 0.016f;
  lastUs = nowUs;
  if (dt > 0.2f) dt = 0.2f;
  const float k = 1.f - expf(-dt / 0.085f);
  for (W& w : pages[page].w) {
    bool pkv = w.peak == 1 || w.peak == 2;
    bool have = w.key >= 0 && (pkv ? s.phave[w.key] : s.have[w.key]);
    float v = !have ? 0 : w.peak == 1 ? s.pmax[w.key] : w.peak == 2 ? s.pmin[w.key] : s.v[w.key];
    switch (w.type) {
      case T_VALUE:
      case T_TILE: {
        char b[16];
        if (!have) strcpy(b, "-");
        else if (w.key == vd::GEAR) { if (v < 0.5f) strcpy(b, "N"); else snprintf(b, sizeof(b), "%d", (int)lroundf(v)); }
        else snprintf(b, sizeof(b), "%.*f", w.dec, (double)v);
        setText(w, b);
        setColor(w, !have ? 0 : v >= w.warn ? 1 : v <= w.low ? 2 : 0);
        break;
      }
      case T_ANUM: {
        char b[16];
        if (!have) strcpy(b, "-");
        else if (w.key == vd::GEAR) { if (v < 0.5f) strcpy(b, "N"); else snprintf(b, sizeof(b), "%d", (int)lroundf(v)); }
        else snprintf(b, sizeof(b), "%.*f", w.dec, (double)v);
        bool hot = have && v >= w.warn;
        if (strcmp(w.last, b) || hot != (w.lastState == 1)) {
          if (gNow - w.lastTxtUs < 80000 && w.last[0] != '\x01') break;
          w.lastTxtUs = gNow;
          strncpy(w.last, b, sizeof(w.last) - 1);
          w.lastState = hot ? 1 : 0;
          anumSet(w, b, hot);
        }
        break;
      }
      case T_SEGBAR: {
        w.shown += (v - w.shown) * k;
        if (w.peak == 3 && w.key >= 0 && s.phave[w.key]) w.pk = s.pmax[w.key];
        int n = w.bw;
        int lit = have ? (int)floorf((w.shown - w.mn) / (w.mx - w.mn) * n) : 0;
        bool flash = have && w.shown >= w.red && blink;
        int pkSeg = w.peak == 3 ? (int)((w.pk - w.mn) / (w.mx - w.mn) * n) : -1;
        int stt = lit * 4 + (flash ? 1 : 0) + pkSeg * 1000;
        if (stt == w.lastI) break;
        w.lastI = stt;
        w.lastState = flash ? 1 : 0;
        lv_obj_invalidate(w.obj);
        break;
      }
      case T_LAMP: {
        int st = !have ? 0 : v >= w.warn ? 2 : v >= w.from ? 1 : 0;
        if (st == w.lastState) break;
        w.lastState = st;
        lv_obj_set_style_bg_color(w.obj, st == 2 ? w.warnCol : st == 1 ? w.col : w.offCol, 0);
        break;
      }
      case T_COVER: {
        w.shown += (v - w.shown) * k;
        float f = have ? (w.shown - w.mn) / (w.mx - w.mn) : 0;
        if (f < 0) f = 0;
        if (f > 1) f = 1;
        int px = (int)lroundf(w.bw * f);
        if (px == w.lastI) break;
        w.lastI = px;
        lv_obj_set_x(w.obj, w.bx + px);
        lv_obj_set_width(w.obj, w.bw - px);
        if (px >= w.bw) lv_obj_add_flag(w.obj, LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(w.obj, LV_OBJ_FLAG_HIDDEN);
        break;
      }
      case T_GIFRPM: {
        if (w.frames.empty()) break;
        float f = have ? (v - w.mn) / (w.mx - w.mn) : 0;
        if (f < 0) f = 0;
        if (f > 1) f = 1;
        int idx = (int)lroundf(f * (w.frames.size() - 1));
        if (idx == w.lastI) break;
        w.lastI = idx;
        lv_image_set_src(w.obj, w.frames[idx]);
        break;
      }
      case T_ARC: {
        w.shown += (v - w.shown) * k;
        int iv = (int)lroundf(1000.f * (w.shown - w.mn) / (w.mx - w.mn));
        if (iv < 0) iv = 0;
        if (iv > 1000) iv = 1000;
        if (lv_arc_get_value(w.obj) != iv) lv_arc_set_value(w.obj, iv);
        int stt = w.shown >= w.red ? 1 : 0;
        if (stt != w.lastState) {
          w.lastState = stt;
          lv_obj_set_style_arc_color(w.obj, stt ? w.warnCol : w.col, LV_PART_INDICATOR);
        }
        break;
      }
      case T_BAR: {
        w.shown += (v - w.shown) * k;
        if (frame & 1) break;                                 // บาร์ยาวทั้งจอ: 30 ครั้ง/วิ พอ
        int iv = (int)lroundf(1000.f * (w.shown - w.mn) / (w.mx - w.mn));
        if (iv < 0) iv = 0;
        if (iv > 1000) iv = 1000;
        if (lv_bar_get_value(w.obj) != iv) lv_bar_set_value(w.obj, iv, LV_ANIM_OFF);
        int stt = w.shown >= w.red ? 1 : 0;
        if (stt != w.lastState) {
          w.lastState = stt;
          lv_obj_set_style_bg_color(w.obj, stt ? w.warnCol : w.col, LV_PART_INDICATOR);
        }
        break;
      }
      case T_SHIFT: {
        int n = (int)w.segs.size();
        float f = (v - w.from) / (w.to - w.from);
        int lit = have ? (int)floorf(f * n) : 0;
        if (lit < 0) lit = 0;
        if (lit > n) lit = n;
        if (w.mode) {                                         // แถบระดับ
          int stt2 = lit + (have && v >= w.red ? 500 : 0);
          if (stt2 == w.lastState) break;
          w.lastState = stt2;
          for (int i = 0; i < n; i++) {
            float fi = (float)i / n, fr = (w.red - w.from) / (w.to - w.from);
            lv_color_t c = w.offCol;
            if (i < lit) {
              if (fi >= fr) c = C_RED;
              else if (w.mode == 2) c = lv_color_mix(lv_color_hex(0xFF2A2A), w.col, (uint8_t)(255 * fminf(1.f, fi / fmaxf(fr, .01f)) * 0.7f));
              else c = w.col;
            }
            lv_obj_set_style_bg_color(w.segs[i], c, 0);
          }
          break;
        }
        int stt = (have && v >= w.to) ? (blink ? 1000 : 1001) : lit;
        if (stt == w.lastState) break;
        w.lastState = stt;
        for (int i = 0; i < n; i++) {
          lv_color_t c;
          if (stt >= 1000) c = stt == 1000 ? lv_color_hex(0x2A7BFF) : w.offCol;   // เกินรอบ: กะพริบฟ้า
          else if (i >= lit) c = w.offCol;
          else c = i < n / 2 ? lv_color_hex(0x2BD46A) : i < n * 4 / 5 ? lv_color_hex(0xFFC21A) : lv_color_hex(0xFF3B30);
          lv_obj_set_style_bg_color(w.segs[i], c, 0);
        }
        break;
      }
      case T_TICKS: {
        w.shown += (v - w.shown) * k;
        if (w.litArc) {                                       // อบรูปแล้ว: เลื่อน arc ที่มีลายขีดติด
          int ia = have ? (int)lroundf(1000.f * (w.shown - w.mn) / (w.mx - w.mn)) : 0;
          if (ia < 0) ia = 0;
          if (ia > 1000) ia = 1000;
          if (ia != w.lastI) { w.lastI = ia; lv_arc_set_value(w.litArc, ia); }
          break;
        }
        int iv = have ? (int)lroundf(w.shown) : (int)w.mn - 1;
        int step = (int)((w.mx - w.mn) / 50);                 // ขยับทีละ 2% (สเกลใหญ่วาดใหม่ทั้งวง = แพง)
        if (step < 1) step = 1;
        iv = iv / step * step;
        if (iv == w.lastI) break;
        w.lastI = iv;
        lv_scale_section_set_range(w.sec[0], (int32_t)w.mn, iv);
        if (iv >= (int)w.red) lv_scale_section_set_range(w.sec[1], (int32_t)w.red, iv);
        else lv_scale_section_set_range(w.sec[1], (int32_t)w.mx + 1, (int32_t)w.mx + 1);
        lv_obj_invalidate(w.obj);
        break;
      }
      case T_NEEDLE: {
        w.shown += (v - w.shown) * k;
        float f = (w.shown - w.mn) / (w.mx - w.mn);
        if (f < 0) f = 0;
        if (f > 1) f = 1;
        int iv = (int)lroundf(f * 1000);
        if (iv == w.lastI) break;
        w.lastI = iv;
        float a = (w.rot + w.sweep * f) * (float)M_PI / 180.f;
        float x0 = w.cx + cosf(a) * w.r0, y0 = w.cy + sinf(a) * w.r0, x1 = w.cx + cosf(a) * w.r1, y1 = w.cy + sinf(a) * w.r1;
        // กรอบเล็กพอดีเข็ม → LVGL วาดใหม่แค่ตรงนั้น
        int lw = lv_obj_get_style_line_width(w.obj, 0);
        int bx = (int)floorf(fminf(x0, x1)) - lw, by = (int)floorf(fminf(y0, y1)) - lw;
        w.pts[0].x = (lv_value_precise_t)(x0 - bx); w.pts[0].y = (lv_value_precise_t)(y0 - by);
        w.pts[1].x = (lv_value_precise_t)(x1 - bx); w.pts[1].y = (lv_value_precise_t)(y1 - by);
        lv_obj_set_pos(w.obj, bx, by);
        lv_obj_set_size(w.obj, (int)fabsf(x1 - x0) + lw * 2 + 2, (int)fabsf(y1 - y0) + lw * 2 + 2);
        lv_line_set_points(w.obj, w.pts, 2);
        break;
      }
      default: break;
    }
  }
}

}  // namespace skin
