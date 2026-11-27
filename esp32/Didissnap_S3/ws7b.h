// ws7b.h — อุปกรณ์บนบอร์ด Waveshare ESP32-S3-Touch-LCD-7B
//  - ชิปขยายขา (CH32V003, I2C 0x24): ไฟจอ (IO2), รีเซ็ตทัช (IO1), รีเซ็ตจอ (IO3), SD CS (IO4), USB/CAN (IO5), VCOM (IO6)
//    ความสว่าง = PWM ของชิปนี้ (ค่า 0 = สว่างสุด ตามตัวอย่างของ Waveshare)
//  - ทัช GT911 (I2C 0x5D/0x14, INT = GPIO4) → touching / tx / ty (ท่าทางแบบ CYD อยู่ใน touchui.h)
#pragma once
#include <Arduino.h>
#include <Wire.h>

namespace ws7b {

static const int SDA = 8, SCL = 9, TP_INT = 4;
static const uint8_t EXP = 0x24;
static uint8_t outv = 0xDF;          // ทุกขา HIGH ยกเว้น IO5 = LOW (ใช้ USB ไม่ใช่ CAN)
static uint8_t tpAddr = 0;
static bool expOk = false;
static volatile bool down[2] = {false, false};   // ครึ่งขวา/ซ้ายที่กดอยู่ (ใช้ตอนเปิดเครื่อง: แตะเพื่อข้ามโลโก้)
static bool touching = false;        // มีนิ้วแตะอยู่
static int tx = 0, ty = 0;           // จุดแตะล่าสุด (พิกัดจอจริง 1024x600)
static bool flip = false;            // จอกลับหัว → สลับซ้าย/ขวา

static bool wr(uint8_t a, uint8_t reg, uint8_t v) {
  Wire.beginTransmission(a);
  Wire.write(reg);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}
static void expOut(int pin, bool v) {
  if (v) outv |= (1 << pin); else outv &= ~(1 << pin);
  wr(EXP, 0x03, outv);
}

// ความสว่าง 0-100 %
inline void brightness(int pct) {
  if (!expOk) return;
  pct = constrain(pct, 0, 100);
  expOut(2, pct > 0);                                  // ปิดไฟจอเมื่อ 0
  int duty = constrain(100 - pct, 0, 97);              // ชิปกลับด้าน: 0 = สว่างสุด
  wr(EXP, 0x05, (uint8_t)(duty * 255 / 100));
}

static bool gtWr(uint16_t reg, uint8_t v) {
  Wire.beginTransmission(tpAddr);
  Wire.write(reg >> 8); Wire.write(reg & 0xFF); Wire.write(v);
  return Wire.endTransmission() == 0;
}
static int gtRd(uint16_t reg, uint8_t* b, int n) {
  Wire.beginTransmission(tpAddr);
  Wire.write(reg >> 8); Wire.write(reg & 0xFF);
  if (Wire.endTransmission(false) != 0) return -1;
  int got = Wire.requestFrom((int)tpAddr, n);
  for (int i = 0; i < got; i++) b[i] = Wire.read();
  return got;
}

// เรียกก่อนเปิดจอ (ต้องเปิด VCOM/ไฟจอผ่านชิปขยายขา)
inline bool begin() {
  Wire.begin(SDA, SCL, 400000);
  expOk = wr(EXP, 0x02, 0xFF);                         // ทุกขาเป็นขาออก
  if (expOk) {
    outv = 0xDF & ~(1 << 1) & ~(1 << 2);               // รีเซ็ตทัช + ไฟจอปิดไว้ก่อน (ไม่เห็นภาพขยะตอนเริ่ม)
    wr(EXP, 0x03, outv);
  }
  pinMode(TP_INT, OUTPUT);
  digitalWrite(TP_INT, LOW);                           // INT ต่ำตอนปล่อยรีเซ็ต = ที่อยู่ 0x5D
  delay(20);
  if (expOk) expOut(1, true);
  delay(60);
  pinMode(TP_INT, INPUT);
  delay(50);
  for (uint8_t a : {(uint8_t)0x5D, (uint8_t)0x14}) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { tpAddr = a; break; }
  }
  Serial.printf("[7B] io-ext %s · touch GT911 %s (0x%02X)\n", expOk ? "ok" : "MISSING", tpAddr ? "ok" : "MISSING", tpAddr);
  return expOk;
}

// อ่านทัช (เรียกใน loop) — ตั้ง down[0]/down[1] ให้ didibtn ใช้แทนปุ่มจริง
inline void loop() {
  if (!tpAddr) return;
  static uint32_t last = 0, lastTouch = 0;
  uint32_t now = millis();
  if (now - last < 15) return;
  last = now;
  uint8_t st = 0;
  if (gtRd(0x814E, &st, 1) != 1) return;
  if (st & 0x80) {
    int n = st & 0x0F;
    if (n > 0) {
      uint8_t p[4];
      if (gtRd(0x8150, p, 4) == 4) {
        int x = p[0] | (p[1] << 8);
        tx = x;
        ty = p[2] | (p[3] << 8);
        touching = true;
        bool right = x >= 512;
        if (flip) right = !right;
        down[0] = right;
        down[1] = !right;
        lastTouch = now;
      }
    } else {
      down[0] = down[1] = false;
      touching = false;
    }
    gtWr(0x814E, 0);
  } else if (now - lastTouch > 150) {
    down[0] = down[1] = false;                         // ไม่มีรายงานจุดสัมผัสนานแล้ว = ปล่อย
    touching = false;
  }
}

inline bool any() { return down[0] || down[1]; }

}  // namespace ws7b
