// =====================================================================
//  DidissnapButtons.h — ปุ่มกด 2 ปุ่ม (สำหรับบอร์ดที่ไม่มีทัชสกรีน)
//
//  ต่อสาย:  IO_A ──[ปุ่ม 1]── GND      IO_B ──[ปุ่ม 2]── GND
//           (ไม่ต้องใช้ตัวต้านทาน ใช้ pull-up ในชิป)
//
//  การทำงาน (ค่าเริ่มต้น):
//    ปุ่ม 1 กดสั้น = หน้าถัดไป      กดค้าง = เข้า/ออกเมนู
//    ปุ่ม 2 กดสั้น = หน้าก่อนหน้า   กดค้าง = ยืนยัน/เลือก (ในเมนู)
//
//  ใช้:  didibtn::begin(PIN_A, PIN_B);  didibtn::onEvent = myHandler;  loop(): didibtn::loop();
//        void myHandler(int button, bool longPress) { ... }   // button = 0 หรือ 1
// =====================================================================
#pragma once
#include <Arduino.h>

#ifndef DIDI_BTN_LONG_MS
#define DIDI_BTN_LONG_MS 700        // กดค้างนานเท่านี้ = กดค้าง
#endif
#ifndef DIDI_BTN_DEBOUNCE_MS
#define DIDI_BTN_DEBOUNCE_MS 30     // กันปุ่มเด้ง
#endif

namespace didibtn {

struct Btn {
  int pin = -1;
  int alt = -1;             // ขาสำรอง (กดได้ทั้งสองขา) ไม่ใช้ = -1
  bool stable = false;      // สถานะหลังกันเด้ง (true = กดอยู่)
  bool raw = false;
  uint32_t rawT = 0;        // เวลาที่สถานะดิบเปลี่ยนล่าสุด
  uint32_t downT = 0;       // เวลาที่เริ่มกด
  bool longFired = false;
};

static Btn b[2];
typedef void (*EventFn)(int button, bool longPress);
static EventFn onEvent = nullptr;

inline void begin(int pinA, int pinB, int altA = -1, int altB = -1) {
  b[0].pin = pinA;
  b[1].pin = pinB;
  b[0].alt = altA;
  b[1].alt = altB;
  for (int i = 0; i < 2; i++) {
    if (b[i].pin >= 0) pinMode(b[i].pin, INPUT_PULLUP);
    if (b[i].alt >= 0) pinMode(b[i].alt, INPUT_PULLUP);
  }
}

inline void loop() {
  uint32_t now = millis();
  for (int i = 0; i < 2; i++) {
    Btn& x = b[i];
    if (x.pin < 0) continue;
    bool r = digitalRead(x.pin) == LOW || (x.alt >= 0 && digitalRead(x.alt) == LOW);
    if (r != x.raw) {
      x.raw = r;
      x.rawT = now;
    }
    if (x.raw != x.stable && now - x.rawT >= DIDI_BTN_DEBOUNCE_MS) {
      x.stable = x.raw;
      if (x.stable) {               // เริ่มกด
        x.downT = now;
        x.longFired = false;
      } else if (!x.longFired) {    // ปล่อยก่อนครบเวลา = กดสั้น
        if (onEvent) onEvent(i, false);
      }
    }
    if (x.stable && !x.longFired && now - x.downT >= DIDI_BTN_LONG_MS) {
      x.longFired = true;           // กดค้าง: ทำงานทันทีไม่ต้องรอปล่อย
      if (onEvent) onEvent(i, true);
    }
  }
}

inline bool held(int i) { return i >= 0 && i < 2 && b[i].stable; }

}  // namespace didibtn
