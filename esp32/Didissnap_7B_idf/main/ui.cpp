// ui.cpp — หน้าปัด (tileview จากสกิน) · ชิปสถานะ · จุดบอกหน้า · ปุ่มเมนู · หน้าตั้งค่า (ภาษาไทย)
#include "ui.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <string>
#include <vector>
#include "board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "nvs.h"
#include "skin.h"
#include "slink.h"
#include "vdata.h"

extern "C" {
LV_FONT_DECLARE(f_t40)
LV_FONT_DECLARE(f_t28)
LV_FONT_DECLARE(f_t20)
}

#define FW_VER "7B 2026.11.28a"

namespace ui {

static const char* TAG = "ui";
static lv_obj_t* tv;
static lv_obj_t* chip;
static lv_obj_t* chipDot;
static lv_obj_t* chipTxt;
static lv_obj_t* dots;
static lv_obj_t* toastL;
static lv_obj_t* setPanel;
static lv_obj_t* stLink;
static lv_obj_t* stSd;
static lv_obj_t* ddSkin;
static lv_obj_t* swDemo;
static lv_obj_t* swFps;
static lv_obj_t* slBri;
static lv_obj_t* btnForget;
static int curPage = 0;
static int64_t pageChangedAt = 0;
static int64_t toastUntil = 0;
static int64_t forgetArm = 0;
static std::vector<std::string> skinFiles;

// ---------------------------------------------------------------- ค่าตั้ง (NVS "ui")
struct Cfg { int bri = 85; int demo = 0; int fps = 0; int page = 0; char skin[48] = ""; };
static Cfg cfg;
static void cfgLoad() {
  nvs_handle_t h;
  if (nvs_open("ui", NVS_READONLY, &h) != ESP_OK) return;
  int32_t v;
  if (nvs_get_i32(h, "bri", &v) == ESP_OK) cfg.bri = v;
  if (nvs_get_i32(h, "demo", &v) == ESP_OK) cfg.demo = v;
  if (nvs_get_i32(h, "fps", &v) == ESP_OK) cfg.fps = v;
  if (nvs_get_i32(h, "page", &v) == ESP_OK) cfg.page = v;
  size_t n = sizeof(cfg.skin);
  if (nvs_get_str(h, "skin", cfg.skin, &n) != ESP_OK) cfg.skin[0] = 0;
  nvs_close(h);
}
static void cfgSave() {
  nvs_handle_t h;
  if (nvs_open("ui", NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_i32(h, "bri", cfg.bri);
  nvs_set_i32(h, "demo", cfg.demo);
  nvs_set_i32(h, "fps", cfg.fps);
  nvs_set_i32(h, "page", cfg.page);
  nvs_set_str(h, "skin", cfg.skin);
  nvs_commit(h);
  nvs_close(h);
}

static void toast(const char* t, int ms = 2500) {
  lv_label_set_text(toastL, t);
  lv_obj_remove_flag(toastL, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(toastL);
  toastUntil = esp_timer_get_time() + (int64_t)ms * 1000;
}

// ---------------------------------------------------------------- สกินจาก SD
static void scanSkins() {
  skinFiles.clear();
  if (!board_sd_ok()) return;
  DIR* d = opendir("/sdcard/skins");
  if (!d) return;
  struct dirent* e;
  while ((e = readdir(d))) {
    size_t n = strlen(e->d_name);
    if (n > 5 && !strcasecmp(e->d_name + n - 5, ".json")) skinFiles.push_back(e->d_name);
  }
  closedir(d);
}

static char* readFile(const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) return nullptr;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n <= 0 || n > 512 * 1024) { fclose(f); return nullptr; }
  char* b = (char*)heap_caps_malloc(n + 1, MALLOC_CAP_SPIRAM);
  if (b && fread(b, 1, n, f) == (size_t)n) b[n] = 0;
  else { free(b); b = nullptr; }
  fclose(f);
  return b;
}

static void rebuildDots() {
  lv_obj_clean(dots);
  int n = skin::pageCount();
  for (int i = 0; i < n; i++) {
    lv_obj_t* d = lv_obj_create(dots);
    lv_obj_set_size(d, i == curPage ? 22 : 8, 8);
    lv_obj_set_style_radius(d, 4, 0);
    lv_obj_set_style_border_width(d, 0, 0);
    lv_obj_set_style_bg_color(d, i == curPage ? skin::accent() : lv_color_hex(0x45454C), 0);
    lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE);
  }
}

static void loadSkin(const char* name) {
  char err[80] = "";
  int n = 0;
  if (name && name[0] && board_sd_ok()) {
    std::string p = std::string("/sdcard/skins/") + name;
    char* j = readFile(p.c_str());
    if (j) {
      n = skin::build(tv, j, err, sizeof(err));
      free(j);
    } else snprintf(err, sizeof(err), "อ่านไฟล์ไม่ได้");
    if (!n) {
      char m[140];
      snprintf(m, sizeof(m), "สกิน %s ใช้ไม่ได้ (%s) — ใช้สกินในตัวแทน", name, err);
      toast(m, 5000);
    }
  }
  if (!n) n = skin::build(tv, skin::BUILTIN, err, sizeof(err));
  if (curPage >= n) curPage = 0;
  lv_tileview_set_tile_by_index(tv, curPage, 0, LV_ANIM_OFF);
  rebuildDots();
  ESP_LOGI(TAG, "skin '%s' %d pages", name && name[0] ? name : "built-in", n);
}

// ---------------------------------------------------------------- หน้าตั้งค่า
static lv_obj_t* row(lv_obj_t* parent, const char* title, int h = 76) {
  lv_obj_t* r = lv_obj_create(parent);
  lv_obj_set_size(r, lv_pct(100), h);
  lv_obj_set_style_bg_color(r, lv_color_hex(0x16161B), 0);
  lv_obj_set_style_border_width(r, 0, 0);
  lv_obj_set_style_radius(r, 12, 0);
  lv_obj_set_style_pad_hor(r, 20, 0);
  lv_obj_set_style_pad_ver(r, 0, 0);
  lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* l = lv_label_create(r);
  lv_obj_set_style_text_font(l, &f_t28, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(0xEDEDF0), 0);
  lv_label_set_text(l, title);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  return r;
}
static lv_obj_t* button(lv_obj_t* parent, const char* t, lv_event_cb_t cb, lv_color_t c) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_height(b, 56);
  lv_obj_set_style_bg_color(b, c, 0);
  lv_obj_set_style_radius(b, 10, 0);
  lv_obj_set_style_pad_hor(b, 22, 0);
  lv_obj_t* l = lv_label_create(b);
  lv_obj_set_style_text_font(l, &f_t28, 0);
  lv_label_set_text(l, t);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  return b;
}

static void onClose(lv_event_t*) { lv_obj_add_flag(setPanel, LV_OBJ_FLAG_HIDDEN); }
static void onPair(lv_event_t*) {
  if (slink::state() != slink::READY) { toast("ต้องต่อ Superlink อยู่ก่อน"); return; }
  slink::openPairing();
  toast("Superlink เปิดรับจอใหม่ 3 นาที — ไปจับคู่ที่จออื่น/แอปได้เลย", 4000);
}
static void onForget(lv_event_t*) {
  int64_t now = esp_timer_get_time();
  if (!forgetArm || now - forgetArm > 3000000) { forgetArm = now; toast("แตะอีกครั้งภายใน 3 วิ เพื่อลืม Superlink"); return; }
  forgetArm = 0;
  slink::forget();
  toast("ลืมแล้ว — กำลังหา Superlink ที่เปิดโหมดจับคู่");
}
static void onDemo(lv_event_t* e) {
  cfg.demo = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
  vd::setDemo(cfg.demo);
  cfgSave();
}
static void onFps(lv_event_t* e) {
  cfg.fps = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
  if (cfg.fps) lv_sysmon_show_performance(nullptr);
  else lv_sysmon_hide_performance(nullptr);
  cfgSave();
}
static void onBri(lv_event_t* e) {
  cfg.bri = lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
  board_backlight(cfg.bri);
  if (lv_event_get_code(e) == LV_EVENT_RELEASED) cfgSave();
}
static void onSkin(lv_event_t* e) {
  int i = lv_dropdown_get_selected((lv_obj_t*)lv_event_get_target(e));
  const char* name = i == 0 || i > (int)skinFiles.size() ? "" : skinFiles[i - 1].c_str();
  snprintf(cfg.skin, sizeof(cfg.skin), "%s", name);
  cfgSave();
  loadSkin(cfg.skin);
  toast(cfg.skin[0] ? "เปลี่ยนสกินแล้ว" : "ใช้สกินในตัว");
}
static void fillSkinList() {
  scanSkins();
  std::string o = "ในตัว (Didissnap Dark)";
  int sel = 0;
  for (size_t i = 0; i < skinFiles.size(); i++) {
    o += "\n" + skinFiles[i];
    if (skinFiles[i] == cfg.skin) sel = (int)i + 1;
  }
  lv_dropdown_set_options(ddSkin, o.c_str());
  lv_dropdown_set_selected(ddSkin, sel);
}
static void onSaveSkin(lv_event_t*) {
  if (!board_sd_ok()) { toast("ไม่มี SD card"); return; }
  mkdir("/sdcard/skins", 0777);
  FILE* f = fopen("/sdcard/skins/default.json", "wb");
  if (!f) { toast("เขียน SD ไม่ได้"); return; }
  fwrite(skin::BUILTIN, 1, strlen(skin::BUILTIN), f);
  fclose(f);
  fillSkinList();
  toast("บันทึก skins/default.json แล้ว — เอาไปแก้ในคอมได้", 4000);
}

static void buildSettings() {
  setPanel = lv_obj_create(lv_layer_top());
  lv_obj_set_size(setPanel, LCD_W, LCD_H);
  lv_obj_set_pos(setPanel, 0, 0);
  lv_obj_set_style_bg_color(setPanel, lv_color_hex(0x0C0C10), 0);
  lv_obj_set_style_bg_opa(setPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(setPanel, 0, 0);
  lv_obj_set_style_radius(setPanel, 0, 0);
  lv_obj_set_style_pad_all(setPanel, 0, 0);
  lv_obj_remove_flag(setPanel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(setPanel, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* t = lv_label_create(setPanel);
  lv_obj_set_style_text_font(t, &f_t40, 0);
  lv_obj_set_style_text_color(t, skin::accent(), 0);
  lv_label_set_text(t, "ตั้งค่า");
  lv_obj_set_pos(t, 28, 12);
  lv_obj_t* x = button(setPanel, LV_SYMBOL_CLOSE "  ปิด", onClose, lv_color_hex(0x2A2A32));
  lv_obj_align(x, LV_ALIGN_TOP_RIGHT, -20, 12);

  lv_obj_t* list = lv_obj_create(setPanel);
  lv_obj_set_size(list, LCD_W - 40, LCD_H - 90);
  lv_obj_set_pos(list, 20, 84);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 0, 0);
  lv_obj_set_style_pad_row(list, 10, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);

  lv_obj_t* r = row(list, "Superlink", 110);
  stLink = lv_label_create(r);
  lv_obj_set_style_text_font(stLink, &f_t20, 0);
  lv_obj_set_style_text_color(stLink, lv_color_hex(0xB4B4BA), 0);
  lv_obj_set_width(stLink, 620);
  lv_label_set_long_mode(stLink, LV_LABEL_LONG_WRAP);
  lv_obj_align(stLink, LV_ALIGN_RIGHT_MID, 0, 0);

  r = row(list, "จับคู่จอ", 84);
  btnForget = button(r, "ลืม Superlink", onForget, lv_color_hex(0x4A2020));
  lv_obj_align(btnForget, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_t* bp = button(r, "เพิ่มจอใหม่ (3 นาที)", onPair, lv_color_hex(0x2A3A2A));
  lv_obj_align_to(bp, btnForget, LV_ALIGN_OUT_LEFT_MID, -14, 0);

  r = row(list, "โหมดจำลอง (ไม่ต้องต่อรถ)");
  swDemo = lv_switch_create(r);
  lv_obj_set_size(swDemo, 90, 46);
  lv_obj_align(swDemo, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_color(swDemo, skin::accent(), LV_PART_INDICATOR | LV_STATE_CHECKED);
  if (cfg.demo) lv_obj_add_state(swDemo, LV_STATE_CHECKED);
  lv_obj_add_event_cb(swDemo, onDemo, LV_EVENT_VALUE_CHANGED, nullptr);

  r = row(list, "ความสว่าง");
  slBri = lv_slider_create(r);
  lv_obj_set_size(slBri, 480, 22);
  lv_slider_set_range(slBri, 5, 100);
  lv_slider_set_value(slBri, cfg.bri, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(slBri, skin::accent(), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slBri, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
  lv_obj_set_style_pad_all(slBri, 10, LV_PART_KNOB);
  lv_obj_align(slBri, LV_ALIGN_RIGHT_MID, -20, 0);
  lv_obj_add_event_cb(slBri, onBri, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(slBri, onBri, LV_EVENT_RELEASED, nullptr);

  r = row(list, "สกินหน้าจอ");
  ddSkin = lv_dropdown_create(r);
  lv_obj_set_width(ddSkin, 480);
  lv_obj_set_style_text_font(ddSkin, &f_t28, 0);
  lv_obj_set_style_text_font(lv_dropdown_get_list(ddSkin), &f_t28, 0);
  lv_obj_align(ddSkin, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_event_cb(ddSkin, onSkin, LV_EVENT_VALUE_CHANGED, nullptr);

  r = row(list, "SD card", 84);
  stSd = lv_label_create(r);
  lv_obj_set_style_text_font(stSd, &f_t20, 0);
  lv_obj_set_style_text_color(stSd, lv_color_hex(0xB4B4BA), 0);
  lv_obj_align(stSd, LV_ALIGN_LEFT_MID, 150, 0);
  lv_obj_t* bs = button(r, "บันทึกสกินในตัวลง SD", onSaveSkin, lv_color_hex(0x2A2A32));
  lv_obj_align(bs, LV_ALIGN_RIGHT_MID, 0, 0);

  r = row(list, "แสดง FPS");
  swFps = lv_switch_create(r);
  lv_obj_set_size(swFps, 90, 46);
  lv_obj_align(swFps, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_color(swFps, skin::accent(), LV_PART_INDICATOR | LV_STATE_CHECKED);
  if (cfg.fps) lv_obj_add_state(swFps, LV_STATE_CHECKED);
  lv_obj_add_event_cb(swFps, onFps, LV_EVENT_VALUE_CHANGED, nullptr);

  r = row(list, "เวอร์ชัน");
  lv_obj_t* v = lv_label_create(r);
  lv_obj_set_style_text_font(v, &f_t20, 0);
  lv_obj_set_style_text_color(v, lv_color_hex(0x8A8A90), 0);
  lv_label_set_text(v, FW_VER "  ·  ESP-IDF + LVGL 9");
  lv_obj_align(v, LV_ALIGN_RIGHT_MID, 0, 0);
}

static void openSettings() {
  fillSkinList();
  if (board_sd_ok()) lv_label_set_text_fmt(stSd, "ใช้ได้ · สกิน %d ไฟล์ (โฟลเดอร์ skins)", (int)skinFiles.size());
  else lv_label_set_text(stSd, "ไม่มีการ์ด (ใช้สกินในตัว)");
  lv_obj_remove_flag(setPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(setPanel);
  lv_obj_move_foreground(toastL);
}
static void onMenu(lv_event_t*) { openSettings(); }

// ---------------------------------------------------------------- ทุกเฟรม
static void onTile(lv_event_t*) {
  lv_obj_t* t = lv_tileview_get_tile_active(tv);
  int p = t ? lv_obj_get_x(t) / LCD_W : 0;
  if (p != curPage) {
    curPage = p;
    rebuildDots();
    pageChangedAt = esp_timer_get_time();
  }
}

static void tick(lv_timer_t*) {
  static vd::Snap s;
  vd::get(s);
  skin::update(curPage, s);
  int64_t now = esp_timer_get_time();
  static int k = 0;
  if (++k % 8 == 0) {                 // ~4 ครั้ง/วิ: สถานะ
    bool showChip = cfg.demo || !s.live;
    if (showChip) {
      const char* t;
      lv_color_t c;
      if (cfg.demo) { t = "DEMO"; c = lv_color_hex(0x3AA0FF); }
      else if (slink::state() == slink::READY) { t = s.link == 2 ? "รอค่าจากรถ" : vd::info()[0] ? vd::info() : "Superlink ต่อแล้ว — กำลังหารถ"; c = lv_color_hex(0xFFC21A); }
      else { t = slink::status(); c = lv_color_hex(0xFF7A00); }
      lv_label_set_text(chipTxt, t);
      lv_obj_set_style_bg_color(chipDot, c, 0);
      lv_obj_remove_flag(chip, LV_OBJ_FLAG_HIDDEN);
    } else lv_obj_add_flag(chip, LV_OBJ_FLAG_HIDDEN);
    if (!lv_obj_has_flag(setPanel, LV_OBJ_FLAG_HIDDEN)) {
      const char* nm = slink::c3Name();
      lv_label_set_text_fmt(stLink, "%s\n%s%s%s  ·  จอที่จับคู่ %d  ·  %s %u ค่า/วิ", slink::status(), nm[0] ? nm : "(ยังไม่จับคู่)",
                            slink::c3Fw()[0] ? "  ·  " : "", slink::c3Fw(), slink::screens(), s.link == 2 ? "รถ" : "รถ: -", s.rate);
    }
  }
  if (toastUntil && now > toastUntil) { toastUntil = 0; lv_obj_add_flag(toastL, LV_OBJ_FLAG_HIDDEN); }
  if (pageChangedAt && now - pageChangedAt > 3000000) { pageChangedAt = 0; cfg.page = curPage; cfgSave(); }
  static int64_t ka = 0;
  if (now - ka > 2000000) { ka = now; board_keepalive(); }
}

void build() {
  cfgLoad();
  vd::setDemo(cfg.demo);
  curPage = cfg.page;
  lv_obj_t* scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  tv = lv_tileview_create(scr);
  lv_obj_set_size(tv, LCD_W, LCD_H);
  lv_obj_set_style_bg_color(tv, lv_color_hex(0x000000), 0);
  lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_event_cb(tv, onTile, LV_EVENT_VALUE_CHANGED, nullptr);

  // ชิปสถานะ (ซ่อนเมื่อได้ค่าจากรถ)
  chip = lv_obj_create(lv_layer_top());
  lv_obj_set_height(chip, 34);
  lv_obj_set_width(chip, LV_SIZE_CONTENT);
  lv_obj_set_pos(chip, 8, 560);
  lv_obj_set_style_bg_color(chip, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(chip, LV_OPA_80, 0);
  lv_obj_set_style_border_width(chip, 0, 0);
  lv_obj_set_style_radius(chip, 17, 0);
  lv_obj_set_style_pad_hor(chip, 12, 0);
  lv_obj_set_style_pad_ver(chip, 0, 0);
  lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(chip, 8, 0);
  lv_obj_remove_flag(chip, LV_OBJ_FLAG_CLICKABLE);
  chipDot = lv_obj_create(chip);
  lv_obj_set_size(chipDot, 12, 12);
  lv_obj_set_style_radius(chipDot, 6, 0);
  lv_obj_set_style_border_width(chipDot, 0, 0);
  chipTxt = lv_label_create(chip);
  lv_obj_set_style_text_font(chipTxt, &f_t20, 0);
  lv_obj_set_style_text_color(chipTxt, lv_color_hex(0xE0E0E4), 0);
  lv_label_set_text(chipTxt, "");

  // จุดบอกหน้า
  dots = lv_obj_create(lv_layer_top());
  lv_obj_set_size(dots, LV_SIZE_CONTENT, 16);
  lv_obj_align(dots, LV_ALIGN_BOTTOM_MID, 0, -4);
  lv_obj_set_style_bg_opa(dots, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(dots, 0, 0);
  lv_obj_set_style_pad_all(dots, 0, 0);
  lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(dots, 8, 0);
  lv_obj_remove_flag(dots, LV_OBJ_FLAG_CLICKABLE);

  // แตะค้างที่ไหนก็ได้ = ตั้งค่า (ไม่มีปุ่มบังหน้าปัด)
  lv_obj_add_event_cb(tv, onMenu, LV_EVENT_LONG_PRESSED, nullptr);

  toastL = lv_label_create(lv_layer_top());
  lv_obj_set_style_text_font(toastL, &f_t28, 0);
  lv_obj_set_style_text_color(toastL, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_color(toastL, lv_color_hex(0x202026), 0);
  lv_obj_set_style_bg_opa(toastL, LV_OPA_90, 0);
  lv_obj_set_style_radius(toastL, 12, 0);
  lv_obj_set_style_pad_all(toastL, 16, 0);
  lv_obj_set_style_border_color(toastL, lv_color_hex(0xFF7A00), 0);
  lv_obj_set_style_border_width(toastL, 2, 0);
  lv_obj_set_width(toastL, 760);
  lv_label_set_long_mode(toastL, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(toastL, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(toastL, LV_ALIGN_CENTER, 0, 120);
  lv_obj_add_flag(toastL, LV_OBJ_FLAG_HIDDEN);

  loadSkin(cfg.skin);
  buildSettings();
  if (cfg.fps) lv_sysmon_show_performance(nullptr);
  else lv_sysmon_hide_performance(nullptr);
  lv_timer_create(tick, 30, nullptr);
  toast("ปัดซ้าย/ขวา = เปลี่ยนหน้า  ·  แตะค้าง = ตั้งค่า", 4000);
}

int bri() { return cfg.bri; }

}  // namespace ui
