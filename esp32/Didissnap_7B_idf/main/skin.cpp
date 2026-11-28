// skin.cpp — อ่านสกิน JSON แล้วสร้างวิดเจ็ต LVGL · อัปเดตเฉพาะค่าที่เปลี่ยน (LVGL วาดใหม่แค่ตรงนั้น = ลื่น)
#include "skin.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <vector>
#include "cJSON.h"
#include "esp_timer.h"

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
      {"t": "ticks", "x": 270, "y": 284, "r": 229, "v": "rpm", "min": 0, "max": 8000, "red": 6500, "n": 100, "len": 16, "width": 7,
       "rot": 120, "sweep": 300, "on": "#F2F2F2", "off": "#5A5A5A", "redon": "#FF3A3A"},
      {"t": "ticks", "x": 270, "y": 284, "r": 246, "min": 0, "max": 8000, "red": 6500, "n": 8, "len": 12, "width": 9,
       "rot": 120, "sweep": 300, "on": "#FF2A2A", "off": "#FF2A2A", "labels": 1, "font": "t40", "label": "#E8E8E8", "redlabel": "#FF6A6A", "gap": 26},
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
    ]}
  ]
})JSON";

// ---------------------------------------------------------------- วิดเจ็ต
enum Type { T_LABEL, T_VALUE, T_TILE, T_ARC, T_BAR, T_SHIFT, T_RECT, T_IMAGE, T_TICKS, T_NEEDLE };

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
  lv_point_precise_t* pts = nullptr;                  // needle
  float cx = 0, cy = 0, r0 = 0, r1 = 0, rot = 0, sweep = 0;
  int lastI = -1;
  char last[16] = "\x01";
  float shown = 0;              // ค่าที่แสดง (หน่วงให้นุ่ม)
  int lastState = -1;
};

struct Page {
  char name[24];
  std::vector<W> w;
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

static lv_obj_t* mkLabel(lv_obj_t* p, const char* txt, const lv_font_t* f, lv_color_t c, int x, int y, int w, lv_text_align_t al) {
  lv_obj_t* l = lv_label_create(p);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, c, 0);
  lv_obj_set_style_text_align(l, al, 0);
  lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
  if (w > 0) lv_obj_set_width(l, w);
  lv_obj_set_pos(l, x, y);
  lv_label_set_text(l, txt ? txt : "");
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
  lv_obj_add_flag(t, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
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
  if (n > 40) n = 40;
  w.from = jNum(j, "from", 5000);
  w.to = jNum(j, "to", 7000);
  w.offCol = colorOf(jStr(j, "off"), lv_color_hex(0x1A1A20));
  int gap = 6, sw = (ww - gap * (n - 1)) / n;
  for (int i = 0; i < n; i++) {
    lv_obj_t* s = lv_obj_create(p);
    lv_obj_set_pos(s, x + i * (sw + gap), y);
    lv_obj_set_size(s, sw, hh);
    lv_obj_set_style_radius(s, hh / 3, 0);
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
    lv_obj_t* im = lv_image_create(tile);
    lv_image_set_src(im, src);
    lv_obj_set_pos(im, jInt(j, "x", 0), jInt(j, "y", 0));
    plain(im);
    return;
  } else {
    return;
  }
  if (w.key < 0 && w.type != T_LABEL) return;
  pg.w.push_back(w);
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
  lv_obj_clean(tv);
  for (auto& pg : pages)
    for (auto& w : pg.w)
      if (w.pts) lv_free(w.pts);
  pages.clear();
  for (auto* st : styles) { lv_style_reset(st); lv_free(st); }
  styles.clear();
  int i = 0;
  cJSON* p;
  cJSON_ArrayForEach(p, jp) {
    lv_obj_t* tile = lv_tileview_add_tile(tv, i, 0, LV_DIR_HOR);
    lv_obj_set_style_bg_color(tile, colorOf(jStr(p, "bg"), bg), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    const char* img = jStr(p, "image");
    if (img) {
      lv_obj_t* im = lv_image_create(tile);
      lv_image_set_src(im, img);
      lv_obj_set_pos(im, 0, 0);
      plain(im);
    }
    pages.emplace_back();
    Page& pg = pages.back();
    snprintf(pg.name, sizeof(pg.name), "%s", jStr(p, "name") ? jStr(p, "name") : "PAGE");
    cJSON* wl = cJSON_GetObjectItemCaseSensitive(p, "w");
    cJSON* w;
    cJSON_ArrayForEach(w, wl) buildWidget(tile, pg, w);
    i++;
  }
  cJSON_Delete(root);
  return i;
}

// ---------------------------------------------------------------- อัปเดต
static void setText(W& w, const char* s) {
  if (!strcmp(w.last, s)) return;
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
  blink = (esp_timer_get_time() / 90000) & 1;
  for (W& w : pages[page].w) {
    bool have = w.key >= 0 && s.have[w.key];
    float v = have ? s.v[w.key] : 0;
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
      case T_ARC: {
        w.shown += (v - w.shown) * 0.45f;
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
        w.shown += (v - w.shown) * 0.45f;
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
        w.shown += (v - w.shown) * 0.45f;
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
        w.shown += (v - w.shown) * 0.45f;
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
