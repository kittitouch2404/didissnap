// c3ble.h — C3 (NimBLE) เป็นตัวปล่อยค่ารถทาง Bluetooth BLE ให้จอ S3 / CYD / แอปแอนดรอยด์ ต่อพร้อมกัน (สูงสุด 3)
//  ต้องลงไลบรารี "NimBLE-Arduino" (by h2zero, 2.5.2) จาก Library Manager · ตรรกะจับคู่อยู่ใน linkcore.h
#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>
#include "linkcore.h"

namespace c3ble {

static NimBLEServer* srv = nullptr;
static NimBLECharacteristic* dataC = nullptr;
static NimBLECharacteristic* cmdC = nullptr;

class SrvCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, NimBLEConnInfo& ci) override {
    clients = s->getConnectedCount();
    bleOpen(ci.getConnHandle());
    Serial.printf("[ble] client connected (%d) %s\n", (int)clients, ci.getAddress().toString().c_str());
    s->updateConnParams(ci.getConnHandle(), 12, 24, 0, 300);   // ส่งถี่ 15-30 ms ค่าวิ่งลื่น
    if (clients < 3) NimBLEDevice::startAdvertising();          // ให้จออื่นต่อเพิ่มได้
  }
  void onDisconnect(NimBLEServer* s, NimBLEConnInfo& ci, int reason) override {
    clients = s->getConnectedCount();
    bleClose(ci.getConnHandle());
    Serial.printf("[ble] client left (%d) reason %d\n", (int)clients, reason);
    NimBLEDevice::startAdvertising();
  }
};
class CmdCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* ch, NimBLEConnInfo& ci) override {
    NimBLEAttValue v = ch->getValue();
    bleIn(ci.getConnHandle(), v.data(), (int)v.length());
  }
};

static void nimNotify(uint16_t h, const uint8_t* d, int n) { if (dataC) dataC->notify(d, n, h); }
static void nimDrop(uint16_t h) { if (srv) srv->disconnect(h); }

// โฆษณา: ชื่อไม่ซ้ำ + บอกว่าเปิดโหมดจับคู่อยู่ไหม
static void advSet(bool open) {
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData ad, sd;
  ad.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  ad.addServiceUUID(DIDIBLE_SVC);
  const uint8_t mfg[5] = {0xFF, 0xFF, 'D', 'N', (uint8_t)(open ? 1 : 0)};
  ad.setManufacturerData(mfg, sizeof(mfg));
  sd.setName(devName);
  bool was = adv->isAdvertising();
  if (was) adv->stop();
  adv->setAdvertisementData(ad);
  adv->setScanResponseData(sd);
  if (was || clients < 3) adv->start();
}

inline bool begin(bool coldBoot) {
  coreBegin("DIDI-C3", coldBoot);
  NimBLEDevice::init(devName);
  NimBLEDevice::setMTU(DIDIBLE_MTU);
  NimBLEDevice::setPower(9);
  srv = NimBLEDevice::createServer();
  static SrvCb scb;
  srv->setCallbacks(&scb);
  NimBLEService* svc = srv->createService(DIDIBLE_SVC);
  dataC = svc->createCharacteristic(DIDIBLE_DATA, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  cmdC = svc->createCharacteristic(DIDIBLE_CMD, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  static CmdCb ccb;
  cmdC->setCallbacks(&ccb);
  svc->start();
  bleNotify = nimNotify;
  bleDrop = nimDrop;
  advUpdate = advSet;
  strlcpy(macStr, NimBLEDevice::getAddress().toString().c_str(), sizeof(macStr));
  for (char* c = macStr; *c; c++) *c = toupper(*c);
  advOpen = pairOpen();
  advSet(advOpen);
  bool ok = NimBLEDevice::getAdvertising()->isAdvertising();
  Serial.printf("[ble] %s advertising as %s · paired %d · pairing %s\n", ok ? "OK" : "FAILED", devName, nPairs,
                pairOpen() ? "OPEN" : "closed");
  return ok;
}

}  // namespace c3ble
