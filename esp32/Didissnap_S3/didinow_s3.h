// didinow_s3.h — ฝั่ง S3: กระจายค่ารถทาง ESP-NOW ให้จอ CYD (กี่จอก็ได้) + รับคำขอ DTC
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "didinow.h"
#include "data.h"

namespace didinow {

static bool ok = false;
static uint16_t seq = 0;
static uint32_t lastSend = 0;
static volatile uint8_t pendCmd = 0;
static volatile uint16_t pendId = 0;
static volatile uint32_t rxCount = 0;
static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

#if ESP_ARDUINO_VERSION_MAJOR >= 3
static void onRecv(const esp_now_recv_info_t*, const uint8_t* d, int n) {
#else
static void onRecv(const uint8_t*, const uint8_t* d, int n) {
#endif
  if (n < (int)sizeof(DidiNowCmd)) return;
  const DidiNowCmd* c = (const DidiNowCmd*)d;
  if (c->magic != DIDINOW_MAGIC || c->type != DN_CMD) return;
  pendId = c->id;
  pendCmd = c->cmd;
  rxCount++;
}

// เรียกหลังเปิด WiFi (SoftAP) แล้ว
inline bool begin() {
  if (esp_now_init() != ESP_OK) { Serial.println("[now] init FAILED"); return false; }
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, BCAST, 6);
  p.channel = 0;                 // ช่องปัจจุบันของ SoftAP
  p.ifidx = WIFI_IF_AP;
  p.encrypt = false;
  if (esp_now_add_peer(&p) != ESP_OK) { Serial.println("[now] add peer FAILED"); return false; }
  esp_now_register_recv_cb(onRecv);
  ok = true;
  Serial.println("[now] ESP-NOW broadcast ready");
  return true;
}

// เรียกทุกเฟรมจาก loop: ส่งค่ารถ ~30 ครั้ง/วินาที
inline void loop(uint8_t link) {
  if (!ok) return;
  uint32_t now = millis();
  if (now - lastSend >= 33) {
    lastSend = now;
    DidiNowData m;
    m.magic = DIDINOW_MAGIC; m.type = DN_DATA; m.link = link; m.seq = seq++;
    m.rpmT = RAW.rpmT;
    m.rpm = RAW.rpm; m.spd = RAW.spd; m.ect = RAW.ect; m.iat = RAW.iat; m.volt = RAW.volt;
    m.load = RAW.load; m.tps = RAW.tps; m.map = RAW.map; m.maf = RAW.maf; m.fuel = RAW.fuel; m.stft = RAW.stft;
    esp_now_send(BCAST, (const uint8_t*)&m, sizeof(m));
  }
}

// คำขอ DTC ที่ค้างอยู่ (ให้ main ทำแล้วตอบด้วย reply())
inline uint8_t takeCmd(uint16_t* id) {
  uint8_t c = pendCmd;
  if (c) { *id = pendId; pendCmd = 0; }
  return c;
}
// json = แบบ ["P0133","P0420"]
inline void reply(uint8_t cmd, uint16_t id, bool good, const String& json) {
  if (!ok) return;
  DidiNowDtc r = {};
  r.magic = DIDINOW_MAGIC; r.type = DN_DTC; r.ok = good; r.cmd = cmd; r.id = id;
  int i = 0;
  while (r.n < 16 && (i = json.indexOf('"', i)) >= 0) {
    int j = json.indexOf('"', i + 1);
    if (j < 0) break;
    strlcpy(r.codes[r.n++], json.substring(i + 1, j).c_str(), sizeof(r.codes[0]));
    i = j + 1;
  }
  for (int k = 0; k < 3; k++) { esp_now_send(BCAST, (const uint8_t*)&r, sizeof(r)); delay(20); }   // ส่งซ้ำกันหลุด
}

}  // namespace didinow
