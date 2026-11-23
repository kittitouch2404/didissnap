// c3ble.h — C3 เป็นตัวปล่อยค่ารถทาง Bluetooth (BLE) ให้จอ S3 / CYD / แอปแอนดรอยด์ ต่อพร้อมกัน (สูงสุด 3)
//  ต้องลงไลบรารี "NimBLE-Arduino" (by h2zero, 2.x) จาก Library Manager
#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>
#include "didinow.h"

namespace c3ble {

static NimBLEServer* srv = nullptr;
static NimBLECharacteristic* dataC = nullptr;
static NimBLECharacteristic* cmdC = nullptr;
static volatile int clients = 0;
// คำสั่งที่เข้ามา (ให้ไฟล์หลักเอาไปทำ)
static void (*onCmd)(const uint8_t* d, int n) = nullptr;

class SrvCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, NimBLEConnInfo& ci) override {
    clients = s->getConnectedCount();
    Serial.printf("[ble] client connected (%d)\n", (int)clients);
    // ช่วงส่งข้อมูลถี่ ๆ (15-30 ms) ค่าวิ่งลื่น
    s->updateConnParams(ci.getConnHandle(), 12, 24, 0, 300);
    if (clients < 3) NimBLEDevice::startAdvertising();   // ให้จออื่นต่อเพิ่มได้
  }
  void onDisconnect(NimBLEServer* s, NimBLEConnInfo&, int reason) override {
    clients = s->getConnectedCount();
    Serial.printf("[ble] client left (%d) reason %d\n", (int)clients, reason);
    NimBLEDevice::startAdvertising();
  }
};
class CmdCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    NimBLEAttValue v = c->getValue();
    if (onCmd) onCmd(v.data(), (int)v.length());
  }
};

inline bool begin() {
  NimBLEDevice::init(DIDIBLE_NAME);
  NimBLEDevice::setMTU(DIDIBLE_MTU);
  NimBLEDevice::setPower(9);                       // แรงสุด (dBm) ในรถระยะใกล้ก็พอ
  srv = NimBLEDevice::createServer();
  static SrvCb scb;
  srv->setCallbacks(&scb);
  NimBLEService* svc = srv->createService(DIDIBLE_SVC);
  dataC = svc->createCharacteristic(DIDIBLE_DATA, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  cmdC = svc->createCharacteristic(DIDIBLE_CMD, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  static CmdCb ccb;
  cmdC->setCallbacks(&ccb);
  svc->start();
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setName(DIDIBLE_NAME);
  adv->addServiceUUID(DIDIBLE_SVC);
  adv->enableScanResponse(true);
  bool ok = adv->start();
  Serial.printf("[ble] %s advertising as %s\n", ok ? "OK" : "FAILED", DIDIBLE_NAME);
  return ok;
}

// ส่งแพ็กเก็ตให้ทุกจอที่ต่ออยู่
inline void send(const void* p, size_t n) {
  if (!dataC || !clients) return;
  dataC->setValue((const uint8_t*)p, n);
  dataC->notify();
}

}  // namespace c3ble
