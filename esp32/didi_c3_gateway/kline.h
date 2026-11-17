// kline.h — OBD2 ผ่าน K-line (ISO 9141-2 / ISO 14230 KWP2000) ด้วย UART1 + โมดูล K-line (L9637 ฯลฯ)
//  สาย K-line เส้นเดียว: ส่งอะไรออกไป ขา RX จะได้ยินตัวเองด้วย (echo) → ตัดทิ้งก่อนอ่านคำตอบ
#pragma once
#include <Arduino.h>

namespace kline {

enum Proto : uint8_t { NONE = 0, ISO9141 = 1, KWP = 2 };
static Proto proto = NONE;
static int pinRx = -1, pinTx = -1;
static HardwareSerial& K = Serial1;
static uint32_t lastRx = 0;          // เวลาจบคำตอบล่าสุด (เว้น P3 ≥ 55 ms ก่อนถามต่อ)
static char info[48] = "";

inline void begin(int rx, int tx) { pinRx = rx; pinTx = tx; }

static void uartOn() { K.begin(10400, SERIAL_8N1, pinRx, pinTx); K.setTimeout(5); }
static void uartOff() { K.end(); pinMode(pinTx, OUTPUT); digitalWrite(pinTx, HIGH); }
static int readByte(uint32_t ms) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms) { if (K.available()) return K.read(); delayMicroseconds(200); }
  return -1;
}
static void flushRx() { while (K.available()) K.read(); }

inline void stop() { if (proto != NONE) { K.end(); proto = NONE; } }

// ---- เริ่มคุยแบบช้า (5 baud, ที่อยู่ 0x33) — ใช้ได้ทั้ง ISO 9141-2 และ KWP2000 แบบ slow init
inline bool slowInit() {
  stop();
  uartOff();
  delay(2600);                                   // สายต้องว่าง ≥ 2.6 วิ ก่อนเริ่มใหม่ (W0/W5)
  const uint8_t addr = 0x33;
  digitalWrite(pinTx, LOW); delay(200);          // start bit
  for (int i = 0; i < 8; i++) { digitalWrite(pinTx, (addr >> i) & 1); delay(200); }
  digitalWrite(pinTx, HIGH);                     // stop bit
  uartOn();
  delay(200);
  flushRx();                                     // echo ของตอนส่ง 5 baud (ถ้ามี)
  int sync = readByte(400);
  if (sync != 0x55) { snprintf(info, sizeof(info), "slow: no 0x55 (%d)", sync); stop(); proto = NONE; K.end(); return false; }
  int kb1 = readByte(60), kb2 = readByte(60);
  if (kb1 < 0 || kb2 < 0) { snprintf(info, sizeof(info), "slow: no keywords"); K.end(); return false; }
  delay(30);
  uint8_t inv = ~(uint8_t)kb2;
  K.write(inv);
  K.flush();
  int e = readByte(30);                          // echo ของเราเอง
  int ack = (e == inv) ? readByte(80) : e;
  if (ack != (uint8_t)~addr) { snprintf(info, sizeof(info), "slow: kb %02X %02X ack %d", kb1, kb2, ack); K.end(); return false; }
  proto = (kb2 == 0x8F) ? KWP : ISO9141;
  snprintf(info, sizeof(info), "kb %02X %02X", kb1, kb2);
  lastRx = millis();
  return true;
}

// ---- เริ่มคุยแบบเร็ว (KWP2000 fast init: ดึงสายลง 25 ms ขึ้น 25 ms แล้วส่ง StartCommunication)
static uint8_t cks(const uint8_t* b, int n) { uint8_t s = 0; for (int i = 0; i < n; i++) s += b[i]; return s; }
static int readMsg(uint8_t* buf, int maxLen, uint32_t firstMs);
inline bool fastInit() {
  stop();
  uartOff();
  delay(350);                                    // สายว่างก่อน (W5 ≥ 300 ms)
  digitalWrite(pinTx, LOW); delay(25);
  digitalWrite(pinTx, HIGH); delay(25);
  uartOn();
  uint8_t m[5] = {0xC1, 0x33, 0xF1, 0x81, 0};
  m[4] = cks(m, 4);
  flushRx();
  K.write(m, 5);
  K.flush();
  uint8_t r[16];
  proto = KWP;                                   // ให้ readMsg แยกหัวแบบ KWP
  int n = readMsg(r, sizeof(r), 120);
  // ตัด echo (5 ไบต์ที่เราส่ง) ออกถ้าโมดูลสะท้อนกลับมา
  if (n >= 5 && !memcmp(r, m, 5)) { memmove(r, r + 5, n - 5); n -= 5; if (n <= 0) n = readMsg(r, sizeof(r), 120); }
  for (int i = 0; i < n; i++) if (r[i] == 0xC1) { snprintf(info, sizeof(info), "fast ok"); lastRx = millis(); return true; }
  snprintf(info, sizeof(info), "fast: no reply (%d)", n);
  proto = NONE;
  K.end();
  return false;
}

// อ่านไบต์ต่อเนื่องจนเงียบเกิน 25 ms (จบข้อความ)
static int readMsg(uint8_t* buf, int maxLen, uint32_t firstMs) {
  int n = 0;
  int b = readByte(firstMs);
  while (b >= 0 && n < maxLen) { buf[n++] = (uint8_t)b; b = readByte(25); }
  return n;
}

// ถาม 1 ข้อความ: data = [mode, pid, ...] → คำตอบ (ตัดหัว/ท้ายแล้ว เริ่มที่ mode+0x40) คืนจำนวนไบต์ หรือ -1
//  multi = รวมทุกข้อความที่ตอบมา (DTC บางรถตอบหลายข้อความ)
inline int request(const uint8_t* data, int dl, uint8_t* resp, int maxLen, uint32_t timeoutMs, bool multi = false) {
  if (proto == NONE || dl > 7) return -1;
  uint32_t gap = millis() - lastRx;
  if (gap < 60) delay(60 - gap);                 // P3: เว้นอย่างน้อย 55 ms หลังคำตอบก่อนหน้า
  uint8_t m[12];
  int n = 0;
  if (proto == ISO9141) { m[n++] = 0x68; m[n++] = 0x6A; m[n++] = 0xF1; }
  else { m[n++] = 0xC0 | dl; m[n++] = 0x33; m[n++] = 0xF1; }
  memcpy(m + n, data, dl); n += dl;
  m[n] = cks(m, n); n++;
  flushRx();
  K.write(m, n);
  K.flush();
  // ตัด echo
  uint8_t raw[96];
  int rn = readMsg(raw, sizeof(raw), timeoutMs);
  int off = 0;
  if (rn >= n && !memcmp(raw, m, n)) off = n;
  if (rn - off <= 0) { int k = readMsg(raw, sizeof(raw), timeoutMs); rn = k; off = 0; }
  lastRx = millis();
  // แยกข้อความ: ISO9141 = 48 6B xx data.. cs (ยาวไม่บอก → ใช้ช่วงเงียบแบ่ง: ทั้งก้อนคือ 1 ข้อความหรือหลายข้อความติดกัน)
  //            KWP     = fmt tgt src [len] data.. cs (fmt & 0x3F = ความยาว)
  int out = 0, i = off;
  const uint8_t want = data[0] + 0x40;
  while (i < rn) {
    int hdr, len;
    if (proto == KWP) {
      uint8_t fmt = raw[i];
      if ((fmt & 0xC0) != 0x80 && (fmt & 0xC0) != 0xC0) { i++; continue; }
      hdr = 3; len = fmt & 0x3F;
      if (!len) { if (i + 3 >= rn) break; len = raw[i + 3]; hdr = 4; }
    } else {
      if (raw[i] != 0x48 && raw[i] != 0x41) { i++; continue; }
      hdr = 3;
      // ยาวไม่บอก: ISO9141 คำตอบ OBD ไม่เกิน 7 ไบต์ข้อมูล — หาไบต์ที่ทำ checksum ตรง
      len = -1;
      for (int L = 1; L <= 7 && i + hdr + L < rn + 0; L++) if (cks(raw + i, hdr + L) == raw[i + hdr + L]) { len = L; break; }
      if (len < 0) { len = rn - i - hdr - 1; if (len <= 0) break; }
    }
    if (i + hdr + len > rn) break;
    const uint8_t* d = raw + i + hdr;
    if (d[0] == want && (dl < 2 || data[0] != 0x01 || d[1] == data[1])) {
      int k = min(len, maxLen - out);
      if (!multi) { memcpy(resp, d, k); return k; }
      if (out == 0) { memcpy(resp, d, k); out = k; }
      else if (len > 1) { k = min(len - 1, maxLen - out); memcpy(resp + out, d + 1, k); out += k; }   // ต่อท้าย ไม่เอา 0x43 ซ้ำ
    }
    i += hdr + len + 1;
  }
  return out > 0 ? out : -1;
}

}  // namespace kline
