// c3link.h — จอ S3 ต่อ ESP32-C3 (ตัวอ่าน OBD ที่ช่อง OBD2) ทาง Bluetooth (BLE)
//  C3 ปล่อยค่ารถ (DidiNowGw) ให้หลายจอพร้อมกัน → ส่งเข้า didinow::gwFeed() เหมือนตอนรับทาง ESP-NOW
//  ใช้ไลบรารี "NimBLE-Arduino" ตัวเดียวกับ obdble.h
#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>
#include "didinow.h"
#include "c3pair.h"

namespace c3link {

static NimBLEClient* cli = nullptr;
static NimBLERemoteCharacteristic* cmdC = nullptr;
static volatile bool up = false;
static void (*feed)(const uint8_t* d, int n) = nullptr;

static void onNotify(NimBLERemoteCharacteristic*, uint8_t* d, size_t n, bool) {
  if (c3pair::onPacket(d, (int)n)) return;   // ผลจับคู่
  if (feed) feed(d, (int)n);
}
static volatile bool wantForget = false, wantOpen = false;

class CliCb : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient*, int reason) override {
    up = false;
    cmdC = nullptr;
    Serial.printf("[c3] BLE disconnected (%d)\n", reason);
  }
};

// หา C3 แล้วต่อ (บล็อกได้หลายวินาที — เรียกจากงานแยก)
static bool connectOnce() {
  NimBLEScan* sc = NimBLEDevice::getScan();
  sc->setActiveScan(true);
  sc->setInterval(60);
  sc->setWindow(50);
  NimBLEScanResults res = sc->getResults(4000, false);
  const NimBLEAdvertisedDevice* found = c3pair::pick(res);   // C3 ของเราเท่านั้น (ดู c3pair.h)
  if (!found) return false;
  if (!cli) {
    cli = NimBLEDevice::createClient();
    static CliCb cb;
    cli->setClientCallbacks(&cb, false);
    cli->setConnectionParams(12, 24, 0, 300);
    cli->setConnectTimeout(5000);
  }
  if (!cli->connect(found)) return false;
  NimBLERemoteService* s = cli->getService(DIDIBLE_SVC);
  NimBLERemoteCharacteristic* dc = s ? s->getCharacteristic(DIDIBLE_DATA) : nullptr;
  cmdC = s ? s->getCharacteristic(DIDIBLE_CMD) : nullptr;
  if (!dc || !cmdC || !dc->subscribe(true, onNotify)) { cli->disconnect(); return false; }
  if (!c3pair::login(cmdC, found->getAddress())) { cli->disconnect(); cmdC = nullptr; return false; }
  up = true;
  Serial.printf("[c3] BLE connected %s (MTU %u)\n", found->getAddress().toString().c_str(), cli->getMTU());
  return true;
}

static void task(void*) {
  for (;;) {
    if (!up) {
      if (!connectOnce()) vTaskDelay(pdMS_TO_TICKS(1500));
    } else {
      if (wantOpen) { wantOpen = false; c3pair::openPairing(cmdC); }
      if (wantForget) { wantForget = false; c3pair::forget(cmdC); vTaskDelay(pdMS_TO_TICKS(300)); if (cli) cli->disconnect(); }
      vTaskDelay(pdMS_TO_TICKS(200));
    }
    if (!up && wantForget) { wantForget = false; c3pair::forget(nullptr); }
  }
}

// ส่งคำสั่ง (DTC / โหมด / Custom PID) ให้ C3
inline bool send(const void* p, size_t n) {
  if (!up || !cmdC) return false;
  return cmdC->writeValue((const uint8_t*)p, n, false);
}

inline void begin(void (*onData)(const uint8_t*, int)) {
  feed = onData;
  NimBLEDevice::init("Didissnap-S3");
  c3pair::begin("S3");
  NimBLEDevice::setMTU(DIDIBLE_MTU);
  xTaskCreatePinnedToCore(task, "c3link", 6144, nullptr, 1, nullptr, 0);
}

inline bool connected() { return up; }
// เมนู: ลืม C3 (หาตัวใหม่) / ให้ C3 เปิดโหมดจับคู่จอใหม่ 3 นาที
inline void forgetC3() { wantForget = true; }
inline void openPairing() { wantOpen = true; }

}  // namespace c3link
