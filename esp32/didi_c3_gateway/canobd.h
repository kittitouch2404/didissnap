// canobd.h — OBD2 ผ่าน CAN (ISO 15765-4) ด้วย TWAI ในชิป ESP32-C3 + SN65HVD230
#pragma once
#include <Arduino.h>
#include "driver/twai.h"

namespace canobd {

static bool on = false;
static uint32_t ecuRx = 0x7E8;     // ID ที่ ECU เครื่องตอบ (หาได้จากคำตอบแรก)
static bool physical = false;      // true = ถามตรง ECU (ecuRx - 8) แทนถามทุกกล่อง (0x7DF) → ตอบไวกว่า

inline void stop() {
  if (!on) return;
  twai_stop();
  twai_driver_uninstall();
  on = false;
}

inline bool start(int pinTx, int pinRx, uint32_t bitrate, bool listenOnly) {
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
  return true;
}

// ฟังเฉย ๆ (ไม่ส่งอะไรออกบัส): ถ้าความเร็วบัสถูก จะเห็นเฟรมวิ่ง และไม่มี error
inline bool sniff(uint32_t ms) {
  uint32_t t0 = millis();
  int n = 0;
  twai_message_t m;
  while (millis() - t0 < ms) if (twai_receive(&m, pdMS_TO_TICKS(20)) == ESP_OK) n++;
  twai_status_info_t st;
  twai_get_status_info(&st);
  return n >= 3 && st.rx_error_counter < 16;
}

static void drain() {
  twai_message_t m;
  while (twai_receive(&m, 0) == ESP_OK) {}
}
static bool sendFrame(uint32_t id, const uint8_t* d) {
  twai_message_t m = {};
  m.identifier = id;
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
  if (!sendFrame(physical ? ecuRx - 8 : 0x7DF, d)) return -1;
  uint32_t t0 = millis();
  int total = -1, got = 0;
  uint32_t from = 0;
  twai_message_t m;
  while (millis() - t0 < timeoutMs) {
    if (twai_receive(&m, pdMS_TO_TICKS(10)) != ESP_OK) continue;
    if (m.identifier < 0x7E8 || m.identifier > 0x7EF || m.data_length_code < 2) continue;
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
      sendFrame(from - 8, fc);
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

}  // namespace canobd
