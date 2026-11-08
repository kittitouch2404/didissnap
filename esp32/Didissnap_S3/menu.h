// menu.h — เมนูบนจอ ใช้ปุ่ม 2 ปุ่ม (เหมือนในพรีวิว)
//  หน้าปกติ: ปุ่ม1 สั้น = หน้าถัดไป · ปุ่ม2 สั้น = หน้าก่อน · ปุ่ม1 ค้าง = เปิดเมนู · ปุ่ม2 ค้าง = ARM/STOP (หน้า Record)
//  ในเมนู : ปุ่ม1 = ลง · ปุ่ม2 = ขึ้น · ปุ่ม2 ค้าง = เลือก/แก้ค่า · ปุ่ม1 ค้าง = ออก
//  แก้ค่า : ปุ่ม1 = เพิ่ม · ปุ่ม2 = ลด · ปุ่ม2 ค้าง = ตกลง
#pragma once
#include "gfx.h"
#include "pages.h"
#include "DidissnapConfig.h"

namespace menu {
using namespace gfx;

struct Item { const char* label; const char* key; int mn, mx, st; };
static const Item ITEMS[] = {
    {"PAGE", "page", 0, pages::N - 1, 1},
    {"BRIGHTNESS", "bri", 10, 100, 10},
    {"AUTO BRIGHT", "briAuto", 0, 1, 1},
    {"REDLINE", "maxRpm", 2000, 12000, 250},
    {"TEMP ALERT", "tempAlert", 70, 130, 1},
    {"AUTO CYCLE", "autoCycle", 0, 120, 5},
    {"FLIP 180", "flip", 0, 1, 1},
    {"GEARS", "gearCount", 3, 6, 1},
    {"REFRESH", "lcdMode", 0, 3, 1},
    {"DRAW CORES", "cores", 1, 2, 1},
    {"PEAK RESET", nullptr, 0, 0, 0},
    {"TRIP RESET", nullptr, 0, 0, 0},
    {"WIFI", nullptr, 0, 0, 0},
    {"EXIT", nullptr, 0, 0, 0},
};
static const int N = sizeof(ITEMS) / sizeof(ITEMS[0]);
static bool open = false, edit = false;
static int sel = 0;
static void (*onPeaks)() = nullptr;
static void (*onTrip)() = nullptr;
static void (*onHold2)() = nullptr;
// อัตรารีเฟรชจอ: 0 = 44 Hz · 1 = 30 Hz · 2 = 60 Hz ล็อก 30 fps · 3 = 60 Hz
static int lcdMode = 0;
static const char* const LCD_MODE_NAME[] = {"44 Hz", "30 Hz (30 fps)", "60 Hz lock 30 fps", "60 Hz"};
static void (*onLcdMode)(int) = nullptr;
// ทดสอบความเร็ว: วาดกี่คอร์ / อ่านตัวอักษรจาก PSRAM หรือแฟลช
static int drawCores = 1, fontMem = 0;
static void (*onPerfOpt)() = nullptr;   // ปุ่ม 2 ค้างตอนอยู่หน้าปกติ (หน้า Record = ARM/STOP)

static char toastMsg[32] = "";
static uint32_t toastUntil = 0;
inline void toast(const char* m) { strlcpy(toastMsg, m, sizeof(toastMsg)); toastUntil = millis() + 1400; }

inline int value(const char* k) {
  const auto& c = didicfg::cfg;
  if (!strcmp(k, "page")) return c.page;
  if (!strcmp(k, "bri")) return c.bri;
  if (!strcmp(k, "briAuto")) return c.briAuto;
  if (!strcmp(k, "maxRpm")) return c.maxRpm;
  if (!strcmp(k, "tempAlert")) return c.tempAlert;
  if (!strcmp(k, "autoCycle")) return c.autoCycle;
  if (!strcmp(k, "flip")) return c.flip;
  if (!strcmp(k, "gearCount")) return c.gearCount;
  if (!strcmp(k, "lcdMode")) return lcdMode;
  if (!strcmp(k, "cores")) return drawCores;
  if (!strcmp(k, "fontMem")) return fontMem;
  return 0;
}
inline void fmt(const Item& it, char* b, size_t n) {
  if (!it.key) {
    if (!strcmp(it.label, "WIFI")) snprintf(b, n, "Didissnap  192.168.4.1");
    else snprintf(b, n, ">");
    return;
  }
  int v = value(it.key);
  if (!strcmp(it.key, "page")) snprintf(b, n, "%d %s", v + 1, pages::NAMES[v]);
  else if (!strcmp(it.key, "bri")) snprintf(b, n, "%d%%", v);
  else if (!strcmp(it.key, "maxRpm")) snprintf(b, n, "%d rpm", v);
  else if (!strcmp(it.key, "tempAlert")) snprintf(b, n, "%d C", v);
  else if (!strcmp(it.key, "autoCycle")) { if (v) snprintf(b, n, "%d s", v); else snprintf(b, n, "OFF"); }
  else if (!strcmp(it.key, "fontMem")) snprintf(b, n, v ? "FLASH" : "PSRAM");
  else if (!strcmp(it.key, "lcdMode")) snprintf(b, n, "%s", LCD_MODE_NAME[v & 3]);
  else if (!strcmp(it.key, "briAuto") || !strcmp(it.key, "flip")) snprintf(b, n, v ? "ON" : "OFF");
  else snprintf(b, n, "%d", v);
}
inline void set(const char* k, int v) {
  if (!strcmp(k, "lcdMode")) { lcdMode = v & 3; if (onLcdMode) onLcdMode(lcdMode); return; }
  if (!strcmp(k, "cores")) { drawCores = v; if (onPerfOpt) onPerfOpt(); return; }
  if (!strcmp(k, "fontMem")) { fontMem = v; if (onPerfOpt) onPerfOpt(); return; }
  int old = didicfg::cfg.page;
  didicfg::setKV(String(k), (float)v);
  didicfg::changed(old);
}

inline void draw() {
  using pages::ORG;
  clear(rgb(0x0A0A0A));
  fill(0, 0, W, 28, rgb(0x1A1512));
  text("MENU", 12, 20, 16, ORG, LEFT, 900);
  text("Didissnap", 468, 20, 12, rgb(0x999999), RIGHT, 600);
  const int rows = 9;
  int top = max(0, min(sel - 4, N - rows));
  bool bl = (nowMs() / 250) % 2 == 0;
  for (int r = 0; r < rows; r++) {
    int j = top + r;
    if (j >= N) break;
    int y = 32 + r * 23;
    bool s = j == sel;
    if (s) rrect(6, y, 468, 21, 4, edit ? rgb(0x5A2E00) : rgb(0x2A2320), edit ? ORG : rgb(0x8A4A10), 1.5f);
    text(ITEMS[j].label, 16, y + 16, 14, s ? rgb(0xFFB37A) : rgb(0xDDDDDD), LEFT, 700);
    char v[40];
    fmt(ITEMS[j], v, sizeof(v));
    text(v, 464, y + 16, 13, s && edit ? (bl ? 0xFFFF : ORG) : rgb(0xAAAAAA), RIGHT, 600);
  }
  fill(0, H - 22, W, 22, rgb(0x1A1512));
  text(edit ? "BTN1 = +    BTN2 = -    HOLD BTN2 = OK" : "BTN1 = DOWN   BTN2 = UP   HOLD BTN2 = SELECT   HOLD BTN1 = EXIT",
       W / 2, H - 7, 11, rgb(0xBBBBBB), CENTER, 600);
}

inline void drawToast() {
  if (nowMs() >= toastUntil) return;
  rrect(W / 2 - 90, H / 2 - 18, 180, 36, 8, rgb(0x101010), pages::ORG, 1.5f);
  text(toastMsg, W / 2, H / 2 + 6, 16, pages::ORG, CENTER, 800);
}

// ปุ่ม: b = 0 (ปุ่ม 1) / 1 (ปุ่ม 2)
inline void button(int b, bool lng) {
  if (!open) {
    if (b == 0 && lng) { open = true; sel = 0; edit = false; return; }
    if (lng) { if (onHold2) onHold2(); return; }
    int p = didicfg::cfg.page + (b == 0 ? 1 : -1);
    didicfg::setPage(((p % pages::N) + pages::N) % pages::N);
    return;
  }
  const Item& it = ITEMS[sel];
  if (edit) {
    if (b == 1 && lng) { edit = false; return; }
    if (lng) return;
    int v = value(it.key) + (b == 0 ? it.st : -it.st);
    if (!strcmp(it.key, "page")) v = (v + pages::N) % pages::N;
    else v = constrain(v, it.mn, it.mx);
    set(it.key, v);
    return;
  }
  if (b == 0 && lng) { open = false; return; }
  if (b == 0) { sel = (sel + 1) % N; return; }
  if (!lng) { sel = (sel + N - 1) % N; return; }
  // ปุ่ม 2 ค้าง = เลือก
  if (it.key) edit = true;
  else if (!strcmp(it.label, "PEAK RESET")) { if (onPeaks) onPeaks(); toast("PEAK RESET"); }
  else if (!strcmp(it.label, "TRIP RESET")) { if (onTrip) onTrip(); toast("TRIP RESET"); }
  else if (!strcmp(it.label, "EXIT")) open = false;
}

}  // namespace menu
