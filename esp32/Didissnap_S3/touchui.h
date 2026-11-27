// touchui.h — จอสัมผัส (Waveshare 7B) ใช้แบบ CYD
//   หน้าปัด: ปัดขวา = หน้าถัดไป · ปัดซ้าย = หน้าก่อน · ปัดขึ้น (หรือแตะมุมขวาบน) = เปิดเมนู · แตะค้าง = ARM/STOP (หน้า Record)
//   เมนู:   แตะช่อง/รายการ = เปิด/เลือก · ON/OFF แตะ = สลับ · แถบเลื่อน แตะตำแหน่ง = ตั้งค่า
//           แตะหัวเมนู / ปัดขวา = ย้อนกลับ · ปัดขึ้น/ลง = เลื่อนรายการ · ตอนแก้ค่า: ปุ่ม − OK + ด้านล่าง
#pragma once
#include "ws7b.h"

namespace tui {

static bool tDown = false, longFired = false;
static int sx = 0, sy = 0, lx = 0, ly = 0;
static uint32_t t0 = 0, tLast = 0;

// พิกัดจอจริง (1024x600) → พิกัดภาพ 480x272 (ภาพขยาย 2 เท่า วางกลางจอ)
inline void toImg(int px, int py, int& x, int& y) {
  x = constrain((px - LCD_OX) / 2, 0, LCD_W - 1);
  y = constrain((py - LCD_OY) / 2, 0, LCD_H - 1);
  if (ws7b::flip) { x = LCD_W - 1 - x; y = LCD_H - 1 - y; }
}

inline void tick() {
  ws7b::loop();
  uint32_t now = millis();
  if (ws7b::touching) {
    int x, y;
    toImg(ws7b::tx, ws7b::ty, x, y);
    if (!tDown) { tDown = true; longFired = false; sx = x; sy = y; t0 = now; }
    lx = x; ly = y; tLast = now;
    if (!longFired && now - t0 > 800 && abs(lx - sx) < 12 && abs(ly - sy) < 12) {
      longFired = true;
      menu::longPress(sx, sy);
    }
    return;
  }
  if (!tDown || now - tLast < 40) return;
  tDown = false;                                   // ปล่อยนิ้ว → ตัดสินว่าแตะหรือปัด
  if (longFired) return;
  int dx = lx - sx, dy = ly - sy;
  if (abs(dx) > 40 && abs(dx) > abs(dy) * 3 / 2) menu::swipeX(dx > 0 ? 1 : -1);
  else if (abs(dy) > 35 && abs(dy) > abs(dx) * 3 / 2) menu::swipeY(dy > 0 ? 1 : -1);
  else if (abs(dx) < 14 && abs(dy) < 14) menu::tap(sx, sy);
}

}  // namespace tui
