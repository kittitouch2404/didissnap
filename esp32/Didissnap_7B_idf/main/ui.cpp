// ui.cpp — หน้าปัด (tileview จากสกิน) · ชิปสถานะ · จุดบอกหน้า · เมนูแบบแอป (Racing HUD) ไทย/อังกฤษ
#include <math.h>
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
#include "esp_system.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "nvs.h"
#include "skin.h"
#include "slink.h"
#include "vdata.h"

extern "C" {
LV_FONT_DECLARE(f_num60)
LV_FONT_DECLARE(f_t40)
LV_FONT_DECLARE(f_t28)
LV_FONT_DECLARE(f_t20)
}

#define FW_VER "7B 2026.11.28j"

namespace ui {

static const char* TAG = "ui";

// ---------------------------------------------------------------- ค่าตั้ง (NVS "ui")
struct Cfg { int bri = 85; int demo = 0; int fps = 0; int page = 0; int en = 0; int color = 0; char skin[48] = ""; };
static Cfg cfg;
static void cfgLoad() {
  nvs_handle_t h;
  if (nvs_open("ui", NVS_READONLY, &h) != ESP_OK) return;
  int32_t v;
  if (nvs_get_i32(h, "bri", &v) == ESP_OK) cfg.bri = v;
  if (nvs_get_i32(h, "demo", &v) == ESP_OK) cfg.demo = v;
  if (nvs_get_i32(h, "fps", &v) == ESP_OK) cfg.fps = v;
  if (nvs_get_i32(h, "page", &v) == ESP_OK) cfg.page = v;
  if (nvs_get_i32(h, "en", &v) == ESP_OK) cfg.en = v;
  if (nvs_get_i32(h, "color", &v) == ESP_OK) cfg.color = v;
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
  nvs_set_i32(h, "en", cfg.en);
  nvs_set_i32(h, "color", cfg.color);
  nvs_set_str(h, "skin", cfg.skin);
  nvs_commit(h);
  nvs_close(h);
}

// ไทย / อังกฤษ
static inline const char* T(const char* th, const char* en) { return cfg.en ? en : th; }

// สีหลักของเมนู (ชุดเดียวกับในแอป)
static const uint32_t COLORS[8] = {0xFF7A00, 0xFF3D8B, 0x20D4FF, 0x3DFF6E, 0xFFD200, 0xFF2A3A, 0xB05CFF, 0xF2F2F2};
static lv_color_t accent() { return lv_color_hex(COLORS[cfg.color & 7]); }

static const char* linkText(slink::Msg m) {
  switch (m) {
    case slink::M_BT_FAIL: return T("เปิด Bluetooth ไม่ได้", "Bluetooth failed");
    case slink::M_SEARCH_PAIRED: return T("กำลังหา Superlink ที่จับคู่ไว้...", "Searching paired Superlink...");
    case slink::M_SEARCH_OPEN: return T("กำลังหา Superlink ที่เปิดโหมดจับคู่...", "Searching Superlink in pairing mode...");
    case slink::M_NONE_OPEN: return T("ไม่เจอ Superlink ที่เปิดโหมดจับคู่ — ถอดเสียบใหม่ หรือกด \"เพิ่มจอใหม่\" ในแอป",
                                      "No Superlink in pairing mode — replug it or tap \"Add screen\" in the app");
    case slink::M_CONNECTING: return T("กำลังต่อ Superlink...", "Connecting Superlink...");
    case slink::M_AUTH: return T("กำลังยืนยันการจับคู่...", "Checking pairing...");
    case slink::M_DENIED: return T("Superlink ที่เจอเป็นของคันอื่น — กำลังหาตัวของเรา", "That Superlink belongs to another car — searching ours");
    case slink::M_READY_NEW: return T("จับคู่ใหม่แล้ว", "Paired (new)");
    case slink::M_READY: return T("ต่อ Superlink แล้ว", "Superlink connected");
    case slink::M_LOST: return T("Superlink หลุด — กำลังต่อใหม่...", "Superlink lost — reconnecting...");
    default: return T("เริ่ม Bluetooth...", "Starting Bluetooth...");
  }
}

// ---------------------------------------------------------------- ตัวแปรหน้าจอ
static lv_obj_t* tv;
static lv_obj_t* chip;
static lv_obj_t* chipDot;
static lv_obj_t* chipTxt;
static lv_obj_t* dots;
static lv_obj_t* toastL;
static lv_obj_t* fpsL = nullptr;     // ตัวนับ FPS มุมขวาล่าง
static lv_obj_t* menu = nullptr;      // หน้าเมนูหลัก (สร้างใหม่ทุกครั้งที่เปิด/เปลี่ยนภาษา/สี)
static lv_obj_t* sub = nullptr;       // หน้าย่อย
static int curPage = 0;
static int64_t pageChangedAt = 0;
static int64_t toastUntil = 0;
static int64_t forgetArm = 0;
static std::vector<std::string> skinFiles;
static vd::Snap snap;
static lv_obj_t* hdrLink = nullptr;
static lv_obj_t* cardSub[6] = {};
static lv_obj_t* subLink = nullptr;
static lv_obj_t* subGearL = nullptr;   // อัตราส่วนสด ในหน้าสอนเกียร์

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
    lv_obj_set_style_bg_color(d, i == curPage ? accent() : lv_color_hex(0x45454C), 0);
    lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE);
  }
}
static void loadSkin(const char* name) {
  char err[80] = "";
  int n = 0;
  if (name && name[0] && board_sd_ok()) {
    std::string p = std::string("/sdcard/skins/") + name;
    char* j = readFile(p.c_str());
    if (j) { n = skin::build(tv, j, err, sizeof(err), true); free(j); }   // หน้าปัดเป็นภาษาอังกฤษเสมอ
    else snprintf(err, sizeof(err), "%s", T("อ่านไฟล์ไม่ได้", "cannot read file"));
    if (!n) {
      char m[160];
      snprintf(m, sizeof(m), T("สกิน %s ใช้ไม่ได้ (%s) — ใช้สกินในตัวแทน", "Skin %s failed (%s) — using built-in"), name, err);
      toast(m, 5000);
    }
  }
  if (!n) n = skin::build(tv, skin::BUILTIN, err, sizeof(err), true);
  if (curPage >= n) curPage = 0;
  lv_tileview_set_tile_by_index(tv, curPage, 0, LV_ANIM_OFF);
  rebuildDots();
  skin::setActive(curPage);
  ESP_LOGI(TAG, "skin '%s' %d pages", name && name[0] ? name : "built-in", n);
}

// ---------------------------------------------------------------- ชิ้นส่วนเมนู
static lv_obj_t* box(lv_obj_t* p, int x, int y, int w, int h, uint32_t bg, int radius = 0) {
  lv_obj_t* o = lv_obj_create(p);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}
static lv_obj_t* text(lv_obj_t* p, const char* t, const lv_font_t* f, lv_color_t c, int x, int y, int w = 0,
                      lv_text_align_t al = LV_TEXT_ALIGN_LEFT) {
  lv_obj_t* l = lv_label_create(p);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, c, 0);
  lv_obj_set_style_text_align(l, al, 0);
  if (w > 0) { lv_obj_set_size(l, w, f->line_height); lv_label_set_long_mode(l, LV_LABEL_LONG_DOT); }   // บรรทัดเดียว ยาวเกินใส่ ...
  lv_obj_set_pos(l, x, y);
  lv_label_set_text(l, t);
  lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
  return l;
}
static void pressFx(lv_obj_t* o) {   // กดแล้วสว่างขึ้นนิดหนึ่ง
  lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(o, lv_color_hex(0x24242A), LV_STATE_PRESSED);
}
static lv_obj_t* btn(lv_obj_t* p, const char* t, lv_event_cb_t cb, uint32_t bg) {
  lv_obj_t* b = lv_button_create(p);
  lv_obj_set_height(b, 58);
  lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
  lv_obj_set_style_radius(b, 10, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_pad_hor(b, 22, 0);
  lv_obj_t* l = lv_label_create(b);
  lv_obj_set_style_text_font(l, &f_t28, 0);
  lv_label_set_text(l, t);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  return b;
}

static void buildMenu();
static void closeSub() {
  if (sub) { lv_obj_delete(sub); sub = nullptr; subLink = nullptr; subGearL = nullptr; }
}
static void closeAll() {
  closeSub();
  if (menu) { lv_obj_delete(menu); menu = nullptr; hdrLink = nullptr; memset(cardSub, 0, sizeof(cardSub)); }
}

// หน้าย่อย: แถบสีบน + หัวข้อ + ปุ่มย้อนกลับ · คืนพื้นที่เนื้อหา (เลื่อนขึ้นลงได้)
static void onSubBack(lv_event_t*) { closeSub(); }
static lv_obj_t* openSub(const char* title) {
  closeSub();
  sub = box(lv_layer_top(), 0, 0, LCD_W, LCD_H, 0x070707);
  lv_obj_t* bar = box(sub, 0, 0, LCD_W, 4, 0);
  lv_obj_set_style_bg_color(bar, accent(), 0);
  lv_obj_t* hd = box(sub, 0, 4, LCD_W, 64, 0x0E0E0E);
  lv_obj_t* bk = box(hd, 0, 0, 72, 64, 0x0E0E0E);
  pressFx(bk);
  lv_obj_add_event_cb(bk, onSubBack, LV_EVENT_CLICKED, nullptr);
  text(bk, LV_SYMBOL_LEFT, &lv_font_montserrat_28, lv_color_hex(0xFFFFFF), 0, 16, 72, LV_TEXT_ALIGN_CENTER);
  text(hd, title, &f_t40, lv_color_hex(0xFFFFFF), 92, 6);
  lv_obj_t* body = lv_obj_create(sub);
  lv_obj_set_pos(body, 20, 80);
  lv_obj_set_size(body, LCD_W - 40, LCD_H - 92);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(body, 0, 0);
  lv_obj_set_style_pad_all(body, 0, 0);
  lv_obj_set_style_pad_row(body, 10, 0);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(body, LV_DIR_VER);
  lv_obj_move_foreground(toastL);
  return body;
}
static lv_obj_t* row(lv_obj_t* body, const char* title, int h = 80) {
  lv_obj_t* r = lv_obj_create(body);
  lv_obj_set_size(r, lv_pct(100), h);
  lv_obj_set_style_bg_color(r, lv_color_hex(0x111111), 0);
  lv_obj_set_style_border_color(r, lv_color_hex(0x242424), 0);
  lv_obj_set_style_border_width(r, 1, 0);
  lv_obj_set_style_radius(r, 14, 0);
  lv_obj_set_style_pad_hor(r, 20, 0);
  lv_obj_set_style_pad_ver(r, 0, 0);
  lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* l = text(r, title, &f_t28, lv_color_hex(0xEDEDF0), 0, 0);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  return r;
}
static lv_obj_t* sw(lv_obj_t* r, bool on, lv_event_cb_t cb) {
  lv_obj_t* s = lv_switch_create(r);
  lv_obj_set_size(s, 92, 46);
  lv_obj_align(s, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_color(s, accent(), LV_PART_INDICATOR | LV_STATE_CHECKED);
  if (on) lv_obj_add_state(s, LV_STATE_CHECKED);
  lv_obj_add_event_cb(s, cb, LV_EVENT_VALUE_CHANGED, nullptr);
  return s;
}

// ---------------------------------------------------------------- การทำงานของปุ่ม
static void onPair(lv_event_t*) {
  if (slink::state() != slink::READY) { toast(T("ต้องต่อ Superlink อยู่ก่อน", "Connect to the Superlink first")); return; }
  slink::openPairing();
  toast(T("Superlink เปิดรับจอใหม่ 3 นาที — ไปจับคู่ที่จออื่น/แอปได้เลย", "Superlink accepts new screens for 3 min — pair the other screen/app now"), 4000);
}
static void onForget(lv_event_t*) {
  int64_t now = esp_timer_get_time();
  if (!forgetArm || now - forgetArm > 3000000) { forgetArm = now; toast(T("แตะอีกครั้งภายใน 3 วิ เพื่อลืม Superlink", "Tap again within 3 s to forget the Superlink")); return; }
  forgetArm = 0;
  slink::forget();
  toast(T("ลืมแล้ว — กำลังหา Superlink ที่เปิดโหมดจับคู่", "Forgotten — searching a Superlink in pairing mode"));
}
static void onDemoSw(lv_event_t* e) {
  cfg.demo = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
  vd::setDemo(cfg.demo);
  cfgSave();
}
static void onFps(lv_event_t* e) {
  cfg.fps = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
  if (fpsL) { if (cfg.fps) lv_obj_remove_flag(fpsL, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(fpsL, LV_OBJ_FLAG_HIDDEN); }
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
  toast(cfg.skin[0] ? T("เปลี่ยนสกินแล้ว", "Skin changed") : T("ใช้สกินในตัว", "Using built-in skin"));
}
static void onSaveSkin(lv_event_t*) {
  if (!board_sd_ok()) { toast(T("ไม่มี SD card", "No SD card")); return; }
  mkdir("/sdcard/skins", 0777);
  FILE* f = fopen("/sdcard/skins/default.json", "wb");
  if (!f) { toast(T("เขียน SD ไม่ได้", "Cannot write SD")); return; }
  fwrite(skin::BUILTIN, 1, strlen(skin::BUILTIN), f);
  fclose(f);
  toast(T("บันทึก skins/default.json แล้ว — เอาไปแก้ในคอมได้", "Saved skins/default.json — edit it on a PC"), 4000);
}
static void onRestart(lv_event_t*) { esp_restart(); }
static void onGoPage(lv_event_t* e) {
  int p = (int)(intptr_t)lv_event_get_user_data(e);
  curPage = p;
  lv_tileview_set_tile_by_index(tv, p, 0, LV_ANIM_OFF);
  rebuildDots();
  skin::setActive(p);
  pageChangedAt = esp_timer_get_time();
  closeAll();
}

// ---------------------------------------------------------------- หน้าย่อย
static void subConnection() {
  lv_obj_t* b = openSub(T("การเชื่อมต่อ", "CONNECTION"));
  lv_obj_t* r = row(b, "Superlink", 130);
  subLink = text(r, "", &f_t20, lv_color_hex(0xB4B4BA), 0, 0, 640);
  lv_label_set_long_mode(subLink, LV_LABEL_LONG_WRAP);
  lv_obj_set_height(subLink, LV_SIZE_CONTENT);
  lv_obj_align(subLink, LV_ALIGN_RIGHT_MID, 0, 0);
  r = row(b, T("จับคู่จอ", "Pairing"), 90);
  lv_obj_t* bf = btn(r, T("ลืม Superlink", "Forget Superlink"), onForget, 0x4A2020);
  lv_obj_align(bf, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_t* bp = btn(r, T("เพิ่มจอใหม่ (3 นาที)", "Add screen (3 min)"), onPair, 0x23402A);
  lv_obj_align_to(bp, bf, LV_ALIGN_OUT_LEFT_MID, -14, 0);
  lv_obj_t* hint = text(b, T("จับคู่ครั้งแรก: ถอดเสียบ Superlink ใหม่ (เปิดรับ 2 นาที) หรือกด \"เพิ่มจอใหม่\" จากแอป/จอที่จับคู่แล้ว",
                             "First pairing: replug the Superlink (2 min window) or tap \"Add screen\" on the app / a paired screen"),
                        &f_t20, lv_color_hex(0x8A8A90), 0, 0, LCD_W - 60);
  lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_height(hint, LV_SIZE_CONTENT);
}
static void subDisplay() {
  lv_obj_t* b = openSub(T("หน้าปัด", "DISPLAY"));
  lv_obj_t* r = row(b, T("ความสว่าง", "Brightness"));
  lv_obj_t* sl = lv_slider_create(r);
  lv_obj_set_size(sl, 520, 22);
  lv_slider_set_range(sl, 5, 100);
  lv_slider_set_value(sl, cfg.bri, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(sl, accent(), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(sl, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
  lv_obj_set_style_pad_all(sl, 10, LV_PART_KNOB);
  lv_obj_align(sl, LV_ALIGN_RIGHT_MID, -20, 0);
  lv_obj_add_event_cb(sl, onBri, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(sl, onBri, LV_EVENT_RELEASED, nullptr);
  r = row(b, T("สกินหน้าจอ", "Skin"));
  scanSkins();
  std::string o = T("ในตัว (Didissnap Dark)", "Built-in (Didissnap Dark)");
  int sel = 0;
  for (size_t i = 0; i < skinFiles.size(); i++) {
    o += "\n" + skinFiles[i];
    if (skinFiles[i] == cfg.skin) sel = (int)i + 1;
  }
  lv_obj_t* dd = lv_dropdown_create(r);
  lv_obj_set_width(dd, 520);
  lv_obj_set_style_text_font(dd, &f_t28, 0);
  lv_obj_set_style_text_font(lv_dropdown_get_list(dd), &f_t28, 0);
  lv_dropdown_set_options(dd, o.c_str());
  lv_dropdown_set_selected(dd, sel);
  lv_obj_align(dd, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_event_cb(dd, onSkin, LV_EVENT_VALUE_CHANGED, nullptr);
  r = row(b, T("โหมดจำลอง (ไม่ต้องต่อรถ)", "Demo mode (no car needed)"));
  sw(r, cfg.demo, onDemoSw);
}
static void subPages() {
  lv_obj_t* b = openSub(T("หน้า", "PAGES"));
  for (int i = 0; i < skin::pageCount(); i++) {
    char t[48];
    snprintf(t, sizeof(t), "%02d   %s", i + 1, skin::pageName(i));
    lv_obj_t* r = row(b, t, 76);
    pressFx(r);
    lv_obj_add_event_cb(r, onGoPage, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    if (i == curPage) {
      lv_obj_t* l = text(r, T("กำลังแสดง", "showing"), &f_t20, accent(), 0, 0);
      lv_obj_align(l, LV_ALIGN_RIGHT_MID, 0, 0);
    }
  }
}
static void subSd() {
  lv_obj_t* b = openSub("SD CARD");
  scanSkins();
  lv_obj_t* r = row(b, T("สถานะ", "Status"));
  char t[96];
  if (board_sd_ok()) snprintf(t, sizeof(t), T("ใช้ได้ · สกิน %d ไฟล์ (โฟลเดอร์ skins)", "OK · %d skin files (skins folder)"), (int)skinFiles.size());
  else snprintf(t, sizeof(t), "%s", T("ไม่มีการ์ด (ใช้สกินในตัว)", "No card (using built-in skin)"));
  lv_obj_t* l = text(r, t, &f_t20, lv_color_hex(0xB4B4BA), 0, 0);
  lv_obj_align(l, LV_ALIGN_RIGHT_MID, 0, 0);
  r = row(b, T("สกินตัวอย่าง", "Sample skin"), 90);
  lv_obj_t* bs = btn(r, T("บันทึกสกินในตัวลง SD", "Save built-in skin to SD"), onSaveSkin, 0x2A2A32);
  lv_obj_align(bs, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_t* hint = text(b, T("แก้สกินในคอม: ก๊อป skins/default.json เป็นชื่อใหม่ → แก้ → ใส่การ์ดกลับ → หน้าปัด → สกินหน้าจอ",
                             "Edit on a PC: copy skins/default.json to a new name → edit → put the card back → Display → Skin"),
                        &f_t20, lv_color_hex(0x8A8A90), 0, 0, LCD_W - 60);
  lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_height(hint, LV_SIZE_CONTENT);
}
static void onRmode(lv_event_t* e) {
  int on = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
  nvs_handle_t h;
  if (nvs_open("ui", NVS_READWRITE, &h) == ESP_OK) { nvs_set_i32(h, "rmode", on); nvs_commit(h); nvs_close(h); }
  toast(T("กด รีสตาร์ท ด้านล่างเพื่อใช้โหมดใหม่", "Tap Restart below to apply"));
}
// ---- เกียร์จาก รอบ / ความเร็ว: ขับในเกียร์นั้นนิ่ง ๆ แล้วแตะเลขเกียร์ = จำไว้
static void subGear();
static void onGearLearn(lv_event_t* e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (!vd::learnGear(i)) { toast(T("ยังไม่มีรอบ/ความเร็ว (ต้องวิ่ง > 5 km/h)", "No rpm/speed yet (drive above 5 km/h)")); return; }
  char t[64];
  snprintf(t, sizeof(t), T("จำเกียร์ %d แล้ว (%.1f)", "Gear %d saved (%.1f)"), i + 1, (double)vd::gearRatio(i));
  subGear();
  toast(t);
}
static void onGearClear(lv_event_t*) {
  vd::clearGears();
  subGear();
  toast(T("ล้างเกียร์แล้ว", "Gears cleared"));
}
static void onPeakReset(lv_event_t*) {
  vd::resetPeaks();
  toast(T("รีเซ็ตค่าสูงสุดแล้ว", "Peaks reset"));
}
static void subGear() {
  lv_obj_t* b = openSub(T("เกียร์ (รอบ / ความเร็ว)", "GEARS (RPM / SPEED)"));
  lv_obj_t* r = row(b, T("ตอนนี้", "Now"), 70);
  subGearL = text(r, "-", &f_t28, accent(), 0, 0, 560, LV_TEXT_ALIGN_RIGHT);
  lv_obj_align(subGearL, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_t* g = lv_obj_create(b);
  lv_obj_set_size(g, lv_pct(100), 120);
  lv_obj_set_style_bg_opa(g, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(g, 0, 0);
  lv_obj_set_style_pad_all(g, 0, 0);
  lv_obj_set_style_pad_column(g, 10, 0);
  lv_obj_set_flex_flow(g, LV_FLEX_FLOW_ROW);
  lv_obj_remove_flag(g, LV_OBJ_FLAG_SCROLLABLE);
  for (int i = 0; i < vd::NGEAR; i++) {
    lv_obj_t* bt = lv_button_create(g);
    lv_obj_set_size(bt, 150, 116);
    float rr = vd::gearRatio(i);
    lv_obj_set_style_bg_color(bt, rr > 0 ? lv_color_hex(0x1E2A1E) : lv_color_hex(0x22222A), 0);
    lv_obj_set_style_radius(bt, 12, 0);
    lv_obj_set_style_shadow_width(bt, 0, 0);
    lv_obj_t* l = lv_label_create(bt);
    lv_obj_set_style_text_font(l, &f_t28, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    char t[32];
    if (rr > 0) snprintf(t, sizeof(t), "%d\n%.1f", i + 1, (double)rr);
    else snprintf(t, sizeof(t), "%d\n-", i + 1);
    lv_label_set_text(l, t);
    lv_obj_center(l);
    lv_obj_add_event_cb(bt, onGearLearn, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }
  lv_obj_t* h = text(b, T("วิธีสอน: ขับในเกียร์นั้นด้วยความเร็วคงที่ (เกิน 20 km/h ไม่เหยียบคลัตช์) แล้วแตะเลขเกียร์ — ทำให้ครบทุกเกียร์ "
                          "· จอด = N · เหยียบคลัตช์/ไม่ตรงเกียร์ไหน = - · ตัวเลขคือ รอบต่อ 1 km/h",
                          "How: drive in that gear at a steady speed (above 20 km/h, clutch out) and tap the gear number — do every gear "
                          "· stopped = N · clutch in / no match = - · the number is rpm per 1 km/h"),
                  &f_t20, lv_color_hex(0x8A8A90), 0, 0, LCD_W - 60);
  lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
  lv_obj_set_height(h, LV_SIZE_CONTENT);
  r = row(b, T("ล้างทุกเกียร์", "Clear all gears"), 90);
  lv_obj_t* bc = btn(r, T("ล้าง", "Clear"), onGearClear, 0x4A2020);
  lv_obj_align(bc, LV_ALIGN_RIGHT_MID, 0, 0);
}
static void onOpenGear(lv_event_t*) { subGear(); }

static void subSystem() {
  lv_obj_t* b = openSub(T("ระบบ", "SYSTEM"));
  lv_obj_t* r = row(b, T("แสดง FPS", "Show FPS"));
  sw(r, cfg.fps, onFps);
  r = row(b, T("วาดเร็ว (RAM ภายใน) · ปิด = ไม่ฉีกแต่ช้า", "Fast render (internal RAM) · off = no tearing, slower"));
  sw(r, board_render_mode(), onRmode);
  r = row(b, T("เกียร์จากรอบ / ความเร็ว", "Gear from rpm / speed"), 90);
  lv_obj_t* bg = btn(r, T("สอนเกียร์", "Teach gears"), onOpenGear, 0x2A2A32);
  lv_obj_align(bg, LV_ALIGN_RIGHT_MID, 0, 0);
  r = row(b, T("ค่าสูงสุด (หน้า PEAK)", "Peak values (PEAK page)"), 90);
  lv_obj_t* bp = btn(r, T("รีเซ็ต", "Reset"), onPeakReset, 0x2A2A32);
  lv_obj_align(bp, LV_ALIGN_RIGHT_MID, 0, 0);
  r = row(b, T("เวอร์ชัน", "Version"));
  lv_obj_t* v = text(r, FW_VER "  ·  ESP-IDF + LVGL 9", &f_t20, lv_color_hex(0x8A8A90), 0, 0);
  lv_obj_align(v, LV_ALIGN_RIGHT_MID, 0, 0);
  r = row(b, T("เริ่มจอใหม่", "Restart screen"), 90);
  lv_obj_t* bt = btn(r, T("รีสตาร์ท", "Restart"), onRestart, 0x4A2020);
  lv_obj_align(bt, LV_ALIGN_RIGHT_MID, 0, 0);
}
static void subHelp() {
  lv_obj_t* b = openSub(T("วิธีใช้", "HELP"));
  lv_obj_t* l = text(b, T("• ปัดซ้าย/ขวา = เปลี่ยนหน้า\n• แตะค้างที่ไหนก็ได้ = เมนูนี้\n• เมนู → หน้า = กระโดดไปหน้าที่ต้องการ\n"
                          "• เมนู → หน้าปัด = ความสว่าง / สกิน / โหมดจำลอง\n• หัวเมนู: แตะ ภาษา = ไทย/EN · แตะ สี = เปลี่ยนสีเมนู\n"
                          "• หน้าจอทั้งหมดมาจากไฟล์สกิน (SD card โฟลเดอร์ skins) แก้ในคอมได้",
                          "• Swipe left/right = change page\n• Long-press anywhere = this menu\n• Menu → Pages = jump to a page\n"
                          "• Menu → Display = brightness / skin / demo\n• Menu header: tap LANG = Thai/EN · tap COLOR = menu colour\n"
                          "• All pages come from skin files (SD card, skins folder) — edit them on a PC"),
                        &f_t28, lv_color_hex(0xE0E0E4), 0, 0, LCD_W - 60);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_height(l, LV_SIZE_CONTENT);
}

// ---------------------------------------------------------------- เมนูหลัก (แบบ Racing HUD เหมือนแอป)
static void onBack(lv_event_t*) { closeAll(); }
static void onLang(lv_event_t*) {
  cfg.en = !cfg.en;
  cfgSave();
  buildMenu();                 // (หน้าปัดเป็นภาษาอังกฤษเสมอ — เปลี่ยนแค่เมนู)
}
static void onColor(lv_event_t*) {
  cfg.color = (cfg.color + 1) & 7;
  cfgSave();
  rebuildDots();
  buildMenu();
}
static void onHdrLink(lv_event_t*) { toast(linkText(slink::msg())); }
static void onCard(lv_event_t* e) {
  switch ((int)(intptr_t)lv_event_get_user_data(e)) {
    case 0: subConnection(); break;
    case 1: subDisplay(); break;
    case 2:
      cfg.demo = !cfg.demo;
      vd::setDemo(cfg.demo);
      cfgSave();
      toast(cfg.demo ? T("โหมดจำลอง: เปิด", "Demo: ON") : T("โหมดจำลอง: ปิด", "Demo: OFF"));
      break;
    case 3: subPages(); break;
    case 4: subSd(); break;
    case 5: subSystem(); break;
  }
}

static lv_obj_t* hdrItem(lv_obj_t* hd, int x, int w, const char* k, const char* v, lv_color_t c, lv_event_cb_t cb, bool swatch = false) {
  lv_obj_t* it = box(hd, x, 0, w, 84, 0x070707);
  pressFx(it);
  lv_obj_add_event_cb(it, cb, LV_EVENT_CLICKED, nullptr);
  box(it, 0, 14, 1, 56, 0x1E1E1E);
  text(it, k, &f_t20, lv_color_hex(0x8A8A8A), 22, 12);
  if (swatch) {
    lv_obj_t* s = box(it, 22, 42, 40, 26, 0, 6);
    lv_obj_set_style_bg_color(s, c, 0);
    lv_obj_set_style_border_color(s, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_border_width(s, 1, 0);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_CLICKABLE);
    return s;
  }
  return text(it, v, &f_t28, c, 22, 38, w - 30);
}

static void card(lv_obj_t* p, int i, int x, int y, int w, int h, const char* icon, const char* title) {
  lv_obj_t* c = box(p, x, y, w, h, 0x111111, 20);
  lv_obj_set_style_border_color(c, lv_color_hex(0x242424), 0);
  lv_obj_set_style_border_width(c, 1, 0);
  pressFx(c);
  lv_obj_add_event_cb(c, onCard, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  char n[4];
  snprintf(n, sizeof(n), "%02d", i + 1);
  text(c, n, &f_num60, lv_color_hex(0x262626), w - 112, -6, 96, LV_TEXT_ALIGN_RIGHT);
  text(c, icon, &lv_font_montserrat_28, accent(), 22, 22);
  text(c, title, &f_t28, lv_color_hex(0xFFFFFF), 22, h - 92, w - 40);
  cardSub[i] = text(c, "", &f_t20, lv_color_hex(0x9A9A9A), 22, h - 52, w - 40);
  lv_obj_t* u = box(c, 22, h - 18, 48, 4, 0, 2);
  lv_obj_set_style_bg_color(u, accent(), 0);
}

static void smallBtn(lv_obj_t* p, int x, int w, const char* t, lv_event_cb_t cb) {
  lv_obj_t* b = box(p, x, 530, w, 54, 0x161616, 14);
  lv_obj_set_style_border_color(b, lv_color_hex(0x262626), 0);
  lv_obj_set_style_border_width(b, 1, 0);
  pressFx(b);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* l = text(b, t, &f_t20, lv_color_hex(0xE8E8E8), 0, 0, w - 10, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(l);
}
static void onHelp(lv_event_t*) { subHelp(); }

static void updateMenuLive();
static void buildMenu() {
  closeAll();
  lv_color_t a = accent();
  menu = box(lv_layer_top(), 0, 0, LCD_W, LCD_H, 0x070707);
  lv_obj_t* top = box(menu, 0, 0, LCD_W, 6, 0);
  lv_obj_set_style_bg_color(top, a, 0);
  lv_obj_t* hd = box(menu, 0, 6, LCD_W, 84, 0x070707);
  lv_obj_t* bk = box(hd, 0, 0, 84, 84, 0x0E0E0E);
  pressFx(bk);
  lv_obj_add_event_cb(bk, onBack, LV_EVENT_CLICKED, nullptr);
  text(bk, LV_SYMBOL_LEFT, &lv_font_montserrat_28, lv_color_hex(0xFFFFFF), 0, 26, 84, LV_TEXT_ALIGN_CENTER);
  box(hd, 84, 0, 1, 84, 0x1E1E1E);
  text(hd, "SETTINGS", &f_t40, lv_color_hex(0xFFFFFF), 108, 8);
  text(hd, "DIDISSNAP · 7\" DASH · " FW_VER, &f_t20, lv_color_hex(0x8A8A8A), 110, 54);
  // ด้านขวา: LINK · ภาษา · สี
  hdrLink = hdrItem(hd, LCD_W - 140 - 120 - 250, 250, "LINK", "", lv_color_hex(0xFFC21A), onHdrLink);
  hdrItem(hd, LCD_W - 140 - 120, 120, T("ภาษา", "LANG"), cfg.en ? "EN" : "ไทย", lv_color_hex(0xFFFFFF), onLang);
  hdrItem(hd, LCD_W - 140, 140, T("สี", "COLOR"), "", a, onColor, true);
  box(menu, 0, 90, LCD_W, 1, 0x1E1E1E);
  // การ์ด 6 ใบ (3 x 2)
  const int x0 = 20, y0 = 106, gap = 12, cw = (LCD_W - 40 - 2 * gap) / 3, ch = 200;
  const char* icons[6] = {LV_SYMBOL_BLUETOOTH, LV_SYMBOL_IMAGE, LV_SYMBOL_PLAY, LV_SYMBOL_LIST, LV_SYMBOL_SD_CARD, LV_SYMBOL_SETTINGS};
  const char* titles[6] = {T("การเชื่อมต่อ", "Connection"), T("หน้าปัด", "Display"), T("โหมดจำลอง", "Demo mode"),
                           T("หน้า", "Pages"), "SD card", T("ระบบ", "System")};
  for (int i = 0; i < 6; i++) card(menu, i, x0 + (i % 3) * (cw + gap), y0 + (i / 3) * (ch + gap), cw, ch, icons[i], titles[i]);
  // ปุ่มเล็กด้านล่าง
  int bw = (LCD_W - 40 - 2 * 10) / 3;
  smallBtn(menu, 20, bw, T("วิธีใช้", "Help"), onHelp);
  smallBtn(menu, 20 + bw + 10, bw, T("รีสตาร์ทจอ", "Restart screen"), onRestart);
  smallBtn(menu, 20 + 2 * (bw + 10), bw, T("ปิดเมนู", "Close menu"), onBack);
  lv_obj_move_foreground(toastL);
  updateMenuLive();
}

static void updateMenuLive() {
  if (!menu) return;
  bool ok = slink::state() == slink::READY;
  if (hdrLink) {
    lv_label_set_text(hdrLink, cfg.demo ? "DEMO" : ok ? (snap.live ? "SUPERLINK · OK" : "SUPERLINK") : T("กำลังหา", "SEARCHING"));
    lv_obj_set_style_text_color(hdrLink, cfg.demo ? lv_color_hex(0x3AA0FF) : ok ? lv_color_hex(0x2BD46A) : lv_color_hex(0xFFC21A), 0);
  }
  char t[96];
  const char* nm = slink::c3Name();
  if (cardSub[0]) {
    if (ok) snprintf(t, sizeof(t), "%s · %s %d", nm[0] ? nm : "Superlink", T("จอ", "screens"), slink::screens());
    else snprintf(t, sizeof(t), "%s", linkText(slink::msg()));
    lv_label_set_text(cardSub[0], t);
  }
  if (cardSub[1]) {
    snprintf(t, sizeof(t), "%s · %d%%", cfg.skin[0] ? cfg.skin : T("สกินในตัว", "built-in skin"), cfg.bri);
    lv_label_set_text(cardSub[1], t);
  }
  if (cardSub[2]) lv_label_set_text(cardSub[2], cfg.demo ? T("เปิด · แตะเพื่อปิด", "ON · tap to turn off") : T("ปิด · แตะเพื่อเปิด", "OFF · tap to turn on"));
  if (cardSub[3]) {
    snprintf(t, sizeof(t), T("%d หน้า · ตอนนี้ %s", "%d pages · now %s"), skin::pageCount(), skin::pageName(curPage));
    lv_label_set_text(cardSub[3], t);
  }
  if (cardSub[4]) lv_label_set_text(cardSub[4], board_sd_ok() ? T("ใช้ได้ · โฟลเดอร์ skins", "OK · skins folder") : T("ไม่มีการ์ด", "no card"));
  if (cardSub[5]) lv_label_set_text(cardSub[5], FW_VER);
}

// ตัวนับ FPS ของเราเอง (ตัววัดในตัวของ LVGL นับเวลารอจอเป็น CPU เต็ม และขึ้น 0 FPS)
static volatile int fpsFrames = 0;
static volatile int64_t fpsRenderUs = 0, fpsRenderStart = 0;
static void onDispEv(lv_event_t* e) {
  lv_event_code_t c = lv_event_get_code(e);
  if (c == LV_EVENT_RENDER_START) fpsRenderStart = esp_timer_get_time();
  else if (c == LV_EVENT_RENDER_READY) {
    fpsFrames = fpsFrames + 1;
    if (fpsRenderStart) fpsRenderUs = fpsRenderUs + (esp_timer_get_time() - fpsRenderStart);
  }
}
static void fpsTick(int64_t now) {
  static int64_t t0 = 0;
  if (!t0) { t0 = now; return; }
  if (now - t0 < 1000000) return;
  int n = fpsFrames;
  int64_t r = fpsRenderUs;
  fpsFrames = 0;
  fpsRenderUs = 0;
  float sec = (now - t0) / 1e6f;
  t0 = now;
  if (fpsL && cfg.fps) lv_label_set_text_fmt(fpsL, "%d FPS  ·  draw %d ms", (int)lroundf(n / sec), n ? (int)(r / n / 1000) : 0);
}

// แตะค้างที่ไหนก็ได้ = เมนู · อ่านจากทัชตรง ๆ (อีเวนต์ LONG_PRESSED ไปตกที่หน้า/วิดเจ็ตลูก ไม่ถึง tileview)
static void pollLongPress() {
  static lv_indev_t* in = nullptr;
  static int64_t downAt = 0;
  static lv_point_t p0;
  static bool fired = false;
  if (!in) {
    for (lv_indev_t* i = lv_indev_get_next(nullptr); i; i = lv_indev_get_next(i))
      if (lv_indev_get_type(i) == LV_INDEV_TYPE_POINTER) { in = i; break; }
    if (!in) return;
  }
  lv_point_t p;
  lv_indev_get_point(in, &p);
  bool down = lv_indev_get_state(in) == LV_INDEV_STATE_PRESSED;
  int64_t now = esp_timer_get_time();
  if (!down) { downAt = 0; fired = false; return; }
  if (!downAt) { downAt = now; p0 = p; return; }
  if (fired || menu || sub) return;
  if (LV_ABS(p.x - p0.x) > 24 || LV_ABS(p.y - p0.y) > 24) { downAt = now; p0 = p; return; }   // กำลังปัด
  if (now - downAt > 700000) { fired = true; buildMenu(); }
}

// ---------------------------------------------------------------- ทุกเฟรม
static void onTile(lv_event_t*) {
  lv_obj_t* t = lv_tileview_get_tile_active(tv);
  int p = t ? lv_obj_get_x(t) / LCD_W : 0;
  if (p != curPage) {
    curPage = p;
    rebuildDots();
    skin::setActive(curPage);
    pageChangedAt = esp_timer_get_time();
  }
}

static void tick(lv_timer_t*) {
  pollLongPress();
  vd::get(snap);
  if (!menu && !sub) skin::update(curPage, snap);
  int64_t now = esp_timer_get_time();
  fpsTick(now);
  static int k = 0;
  if (++k % 8 == 0) {                 // ~4 ครั้ง/วิ: สถานะ
    bool showChip = !menu && !sub && (cfg.demo || !snap.live);
    if (showChip) {
      int langSave = cfg.en;
      cfg.en = 1;                     // ป้ายบนหน้าปัดเป็นภาษาอังกฤษเสมอ
      const char* t;
      lv_color_t c;
      if (cfg.demo) { t = "DEMO"; c = lv_color_hex(0x3AA0FF); }
      else if (slink::state() == slink::READY) {
        t = snap.link == 2 ? T("รอค่าจากรถ", "Waiting for car data") : vd::info()[0] ? vd::info() : T("Superlink ต่อแล้ว — กำลังหารถ", "Superlink connected — finding the car");
        c = lv_color_hex(0xFFC21A);
      } else { t = linkText(slink::msg()); c = accent(); }
      if (strcmp(lv_label_get_text(chipTxt), t)) lv_label_set_text(chipTxt, t);
      lv_obj_set_style_bg_color(chipDot, c, 0);
      lv_obj_remove_flag(chip, LV_OBJ_FLAG_HIDDEN);
      cfg.en = langSave;
    } else lv_obj_add_flag(chip, LV_OBJ_FLAG_HIDDEN);
    updateMenuLive();
    if (subGearL) {
      float lr = vd::liveRatio();
      if (lr > 0) {
        if (snap.have[vd::GEAR]) lv_label_set_text_fmt(subGearL, T("%.1f รอบ/km/h · เกียร์ %s", "%.1f rpm per km/h · gear %s"), (double)lr,
                                                       snap.v[vd::GEAR] < 0.5f ? "N" : snap.v[vd::GEAR] < 1.5f ? "1" : snap.v[vd::GEAR] < 2.5f ? "2" : snap.v[vd::GEAR] < 3.5f ? "3" : snap.v[vd::GEAR] < 4.5f ? "4" : snap.v[vd::GEAR] < 5.5f ? "5" : "6");
        else lv_label_set_text_fmt(subGearL, T("%.1f รอบ/km/h", "%.1f rpm per km/h"), (double)lr);
      } else lv_label_set_text(subGearL, T("รอรอบ + ความเร็ว (> 5 km/h)", "Waiting for rpm + speed (> 5 km/h)"));
    }
    if (subLink) {
      const char* nm = slink::c3Name();
      lv_label_set_text_fmt(subLink, "%s\n%s%s%s  ·  %s %d  ·  %u %s", linkText(slink::msg()), nm[0] ? nm : T("(ยังไม่จับคู่)", "(not paired)"),
                            slink::c3Fw()[0] ? "  ·  " : "", slink::c3Fw(), T("จอที่จับคู่", "paired screens"), slink::screens(),
                            (unsigned)snap.rate, T("ค่า/วิ", "values/s"));
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

  fpsL = lv_label_create(lv_layer_top());
  lv_obj_set_style_text_font(fpsL, &f_t20, 0);
  lv_obj_set_style_text_color(fpsL, lv_color_hex(0x7CFC9A), 0);
  lv_obj_set_style_bg_color(fpsL, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(fpsL, LV_OPA_70, 0);
  lv_obj_set_style_pad_hor(fpsL, 8, 0);
  lv_obj_align(fpsL, LV_ALIGN_BOTTOM_RIGHT, -6, -6);
  lv_label_set_text(fpsL, "-- FPS");
  lv_display_add_event_cb(lv_display_get_default(), onDispEv, LV_EVENT_RENDER_START, nullptr);
  lv_display_add_event_cb(lv_display_get_default(), onDispEv, LV_EVENT_RENDER_READY, nullptr);

  loadSkin(cfg.skin);
  if (fpsL) { if (cfg.fps) lv_obj_remove_flag(fpsL, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(fpsL, LV_OBJ_FLAG_HIDDEN); }
  lv_timer_create(tick, 16, nullptr);
  toast(board_touch_ok() ? "Swipe left/right = page  ·  Long-press = menu" : "Touch not found (GT911) - check / replug", 5000);
}

int bri() { return cfg.bri; }

// สำหรับทำภาพตัวอย่างบนคอม
void previewMenu(bool english) {
  cfg.en = english;
  lv_obj_add_flag(toastL, LV_OBJ_FLAG_HIDDEN);
  buildMenu();
}
void previewGear() {
  lv_obj_add_flag(toastL, LV_OBJ_FLAG_HIDDEN);
  subSystem();
  subGear();
}

}  // namespace ui
