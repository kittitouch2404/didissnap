// =====================================================================
//  N1V65Buttons.h — ปุ่มกด 2 ปุ่ม (สำหรับบอร์ดที่ไม่มีทัชสกรีน)
//
//  ต่อสาย:  IO_A ──[ปุ่ม 1]── GND      IO_B ──[ปุ่ม 2]── GND
//           (ไม่ต้องใช้ตัวต้านทาน ใช้ pull-up ในชิป)
//
//  การทำงาน (ค่าเริ่มต้น):
//    ปุ่ม 1 กดสั้น = หน้าถัดไป      กดค้าง = เข้า/ออกเมนู
//    ปุ่ม 2 กดสั้น = หน้าก่อนหน้า   กดค้าง = ยืนยัน/เลือก (ในเมนู)
//
//  ใช้:  n1btn::begin(PIN_A, PIN_B);  n1btn::onEvent = myHandler;  loop(): n1btn::loop();
//        void myHandler(int button, bool longPress) { ... }   // button = 0 หรือ 1
// =====================================================================
#pragma once
#include <Arduino.h>

#ifndef N1BTN_LONG_MS
#define N1BTN_LONG_MS 700        // กดค้างนานเท่านี้ = กดค้าง
#endif
#ifndef N1BTN_DEBOUNCE_MS
#define N1BTN_DEBOUNCE_MS 30     // กันปุ่มเด้ง
#endif

namespace n1btn {

struct Btn {
  int pin = -1;
  bool stable = false;      // สถานะหลังกันเด้ง (true = กดอยู่)
  bool raw = false;
  uint32_t rawT = 0;        // เวลาที่สถานะดิบเปลี่ยนล่าสุด
  uint32_t downT = 0;       // เวลาที่เริ่มกด
  bool longFired = false;
};

static Btn b[2];
typedef void (*EventFn)(int button, bool longPress);
static EventFn onEvent = nullptr;

inline void begin(int pinA, int pinB) {
  b[0].pin = pinA;
  b[1].pin = pinB;
  for (int i = 0; i < 2; i++) {
    if (b[i].pin >= 0) pinMode(b[i].pin, INPUT_PULLUP);
  }
}

inline void loop() {
  uint32_t now = millis();
  for (int i = 0; i < 2; i++) {
    Btn& x = b[i];
    if (x.pin < 0) continue;
    bool r = digitalRead(x.pin) == LOW;
    if (r != x.raw) {
      x.raw = r;
      x.rawT = now;
    }
    if (x.raw != x.stable && now - x.rawT >= N1BTN_DEBOUNCE_MS) {
      x.stable = x.raw;
      if (x.stable) {               // เริ่มกด
        x.downT = now;
        x.longFired = false;
      } else if (!x.longFired) {    // ปล่อยก่อนครบเวลา = กดสั้น
        if (onEvent) onEvent(i, false);
      }
    }
    if (x.stable && !x.longFired && now - x.downT >= N1BTN_LONG_MS) {
      x.longFired = true;           // กดค้าง: ทำงานทันทีไม่ต้องรอปล่อย
      if (onEvent) onEvent(i, true);
    }
  }
}

inline bool held(int i) { return i >= 0 && i < 2 && b[i].stable; }

}  // namespace n1btn
