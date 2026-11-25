// canobd.h — OBD2 ผ่าน CAN (ISO 15765-4) ด้วย TWAI ในชิป ESP32-C3 + SN65HVD230
#pragma once
#include <Arduino.h>
#include "driver/twai.h"

namespace canobd {

static bool on = false;
static uint32_t ecuRx = 0x7E8;     // ID ที่ ECU เครื่องตอบ (หาได้จากคำตอบแรก)
static bool physical = false;      // true = ถามตรง ECU (ecuRx - 8) แทนถามทุกกล่อง (0x7DF) → ตอบไวกว่า
static bool ext = false;           // true = CAN ID 29 บิต (ถามทุกกล่อง 18DB33F1 · ตอบ 18DAF1xx · ถามตรง 18DAxxF1)

// ID ที่ใช้ถามทุกกล่อง / ถามตรงกล่องที่ตอบมาด้วย rx / เป็นคำตอบ OBD ไหม
inline uint32_t funcId() { return ext ? 0x18DB33F1 : 0x7DF; }
inline uint32_t physOf(uint32_t rx) { return ext ? (0x18DA00F1 | ((rx & 0xFF) << 8)) : rx - 8; }
inline uint32_t defTx() { return physical ? physOf(ecuRx) : funcId(); }
inline bool isResp(const twai_message_t& m) {
  return ext ? (m.extd && (m.identifier & 0xFFFFFF00) == 0x18DAF100) : (!m.extd && m.identifier >= 0x7E8 && m.identifier <= 0x7EF);
}
// header 11 บิตที่ตั้งในแอป (เช่น 7E1 กล่องเกียร์) → ID 29 บิตที่ตรงกัน (7E0 = 0x10 เครื่อง, 7E1 = 0x18 เกียร์ ...)
inline uint32_t mapHdr(uint32_t h) {
  if (!ext || h > 0x7FF) return h;
  if (h == 0x7DF) return 0x18DB33F1;
  if (h >= 0x7E0 && h <= 0x7E7) return 0x18DA00F1 | ((0x10 + (h - 0x7E0) * 8) << 8);
  return h;
}

inline void stop() {
  if (!on) return;
  twai_stop();
  twai_driver_uninstall();
  on = false;
}

inline bool start(int pinTx, int pinRx, uint32_t bitrate, bool listenOnly, bool extId = false) {
  stop();
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)pinTx, (gpio_num_t)pinRx,
                                                        listenOnly ? TWAI_MODE_LISTEN_ONLY : TWAI_MODE_NORMAL);
  g.rx_queue_len = 32;
  g.tx_queue_len = 4;
  twai_timing_config_t t = bitrate == 250000 ? (twai_timing_config_t)TWAI_TIMING_CONFIG_250KBITS()
                                             : (twai_timing_config_t)TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&g, &t, &f) != ESP_OK) return false;
  if (twai_start() != ESP_OK) { twai_driver_uninstall(); return false; }
  on = true;
  physical = false;
  ext = extId;
  ecuRx = ext ? 0x18DAF110 : 0x7E8;
  return true;
}

// ฟังเฉย ๆ (ไม่ส่งอะไรออกบัส): ถ้าความเร็วบัสถูก จะเห็นเฟรมวิ่ง และไม่มี error
static int sniffRx = 0, sniffErr = 0;   // ผลการฟังล่าสุด (ไว้บอกสาเหตุ)
inline bool sniff(uint32_t ms) {
  uint32_t t0 = millis();
  int n = 0;
  twai_message_t m;
  while (millis() - t0 < ms) if (twai_receive(&m, pdMS_TO_TICKS(20)) == ESP_OK) n++;
  twai_status_info_t st;
  twai_get_status_info(&st);
  sniffRx = n;
  sniffErr = st.rx_error_counter;
  return n >= 3 && st.rx_error_counter < 16;
}
// สถานะการส่ง (หลังถามแล้วไม่มีคำตอบ): tx error สูง = ไม่มีใครรับ (ACK) → สาย/ไฟโมดูล
inline void txStatus(int* txErr, int* busOff) {
  twai_status_info_t st = {};
  twai_get_status_info(&st);
  *txErr = st.tx_error_counter;
  *busOff = st.state == TWAI_STATE_BUS_OFF ? 1 : 0;
}

static void drain() {
  twai_message_t m;
  while (twai_receive(&m, 0) == ESP_OK) {}
}
static bool sendFrame(uint32_t id, const uint8_t* d) {
  twai_message_t m = {};
  m.identifier = id;
  m.extd = id > 0x7FF ? 1 : 0;
  m.data_length_code = 8;
  memcpy(m.data, d, 8);
  return twai_transmit(&m, pdMS_TO_TICKS(30)) == ESP_OK;
}

// ส่งคำถาม (ไม่เกิน 7 ไบต์) แล้วรับคำตอบ ISO-TP (รองรับหลายเฟรม เช่น DTC) คืนจำนวนไบต์ หรือ -1
inline int request(const uint8_t* req, int rl, uint8_t* resp, int maxLen, uint32_t timeoutMs) {
  if (!on || rl > 7) return -1;
  drain();
  uint8_t d[8] = {(uint8_t)rl, 0, 0, 0, 0, 0, 0, 0};
  memcpy(d + 1, req, rl);
  if (!sendFrame(defTx(), d)) return -1;
  uint32_t t0 = millis(), tStart = t0;
  int total = -1, got = 0;
  uint32_t from = 0;
  twai_message_t m;
  while (millis() - t0 < timeoutMs && millis() - tStart < timeoutMs + 800) {   // "รอสักครู่" (7F..78) ต่อเวลาได้ไม่เกิน 0.8 วิ
    if (twai_receive(&m, pdMS_TO_TICKS(10)) != ESP_OK) continue;
    if (!isResp(m) || m.data_length_code < 2) continue;
    if (from && m.identifier != from) continue;
    uint8_t pci = m.data[0] >> 4;
    if (pci == 0) {                                   // เฟรมเดียวจบ
      int len = m.data[0] & 15;
      if (len < 1 || len > 7) continue;
      if (m.data[1] == 0x7F) continue;                // ปฏิเสธ (ECU อื่น/ไม่รองรับ) — รอคำตอบอื่นต่อ
      if (m.data[1] != (uint8_t)(req[0] + 0x40)) continue;
      if (rl >= 2 && req[0] == 0x01 && m.data[2] != req[1]) continue;
      len = min(len, maxLen);
      memcpy(resp, m.data + 1, len);
      ecuRx = m.identifier;
      return len;
    }
    if (pci == 1 && total < 0) {                      // เฟรมแรกของหลายเฟรม → ขอส่วนที่เหลือ (flow control)
      total = ((m.data[0] & 15) << 8) | m.data[1];
      if (m.data[2] != (uint8_t)(req[0] + 0x40)) { total = -1; continue; }
      from = m.identifier;
      got = min(6, min(total, maxLen));
      memcpy(resp, m.data + 2, got);
      uint8_t fc[8] = {0x30, 0, 0, 0, 0, 0, 0, 0};
      sendFrame(physOf(from), fc);
      t0 = millis();
      continue;
    }
    if (pci == 2 && total > 0 && m.identifier == from) {   // เฟรมต่อ
      int k = min(7, total - got);
      if (got + k > maxLen) k = maxLen - got;
      if (k > 0) { memcpy(resp + got, m.data + 1, k); got += k; }
      if (got >= total || got >= maxLen) { ecuRx = from; return got; }
    }
  }
  return total > 0 ? got : -1;
}

// ถามด้วย ID ที่เลือกเอง (Custom PID เช่น 0x7E1 กล่องเกียร์) — คืนคำตอบดิบรวม 7F (ปฏิเสธ) · ไม่เปลี่ยน ecuRx
inline int requestTo(uint32_t tx, const uint8_t* req, int rl, uint8_t* resp, int maxLen, uint32_t timeoutMs) {
  if (!on || rl < 1 || rl > 7) return -1;
  tx = mapHdr(tx);
  drain();
  uint8_t d[8] = {(uint8_t)rl, 0, 0, 0, 0, 0, 0, 0};
  memcpy(d + 1, req, rl);
  if (!sendFrame(tx, d)) return -1;
  // ถามตรงกล่อง → รอคำตอบจาก ID คู่กัน (11 บิต: +8 · 29 บิต: 18DAF1xx) · ถามทุกกล่อง → รับจากกล่องไหนก็ได้
  uint32_t want = tx == funcId() ? 0 : (tx > 0x7FF ? (0x18DAF100 | ((tx >> 8) & 0xFF)) : tx + 8);
  uint32_t t0 = millis(), tStart = t0;
  int total = -1, got = 0;
  uint32_t from = 0;
  twai_message_t m;
  while (millis() - t0 < timeoutMs && millis() - tStart < timeoutMs + 800) {
    if (twai_receive(&m, pdMS_TO_TICKS(10)) != ESP_OK) continue;
    if (m.data_length_code < 2) continue;
    if (want ? m.identifier != want : !isResp(m)) continue;
    if (from && m.identifier != from) continue;
    uint8_t pci = m.data[0] >> 4;
    if (pci == 0) {
      int len = m.data[0] & 15;
      if (len < 1 || len > 7) continue;
      if (m.data[1] == 0x7F && len >= 3 && m.data[3] == 0x78) { t0 = millis(); continue; }   // รอสักครู่ (กำลังทำ)
      if (m.data[1] != 0x7F && m.data[1] != (uint8_t)(req[0] + 0x40)) continue;
      len = min(len, maxLen);
      memcpy(resp, m.data + 1, len);
      return len;
    }
    if (pci == 1 && total < 0) {
      total = ((m.data[0] & 15) << 8) | m.data[1];
      if (m.data[2] != (uint8_t)(req[0] + 0x40)) { total = -1; continue; }
      from = m.identifier;
      got = min(6, min(total, maxLen));
      memcpy(resp, m.data + 2, got);
      uint8_t fc[8] = {0x30, 0, 0, 0, 0, 0, 0, 0};
      sendFrame(physOf(from), fc);
      t0 = millis();
      continue;
    }
    if (pci == 2 && total > 0 && m.identifier == from) {
      int k = min(7, total - got);
      if (got + k > maxLen) k = maxLen - got;
      if (k > 0) { memcpy(resp + got, m.data + 1, k); got += k; }
      if (got >= total || got >= maxLen) return got;
    }
  }
  return total > 0 ? got : -1;
}

}  // namespace canobd
