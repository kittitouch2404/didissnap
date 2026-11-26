// c3bt.h — ESP32 รุ่นแรก (ESP32-WROOM / DevKit): Bluetooth ธรรมดา (SPP) + BLE เปิดพร้อมกัน (Bluedroid)
//  - BLE: จอ S3 / CYD / แอปมือถือ ต่อเหมือน C3 ทุกอย่าง (UUID / แพ็กเก็ต / จับคู่ เดียวกัน)
//  - Bluetooth ธรรมดา: จอแอนดรอยด์จีนที่ไม่มี BLE ให้แอป → จับคู่บลูทูธในเครื่องแล้วใช้แอปได้
//      แพ็กเก็ตเดียวกับ BLE แต่ส่งเป็นบรรทัด "~" + base64 + "\n" (ส่งค่าทุกตัวพร้อมกัน ~30 ครั้ง/วิ เร็วเท่า BLE)
//  ใช้ไลบรารีในตัวของ ESP32 core (BLE + BluetoothSerial) ไม่ต้องลง NimBLE
#pragma once
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <BluetoothSerial.h>
#include "mbedtls/base64.h"
#include "linkcore.h"

namespace c3ble {

static BluetoothSerial SBT;
static BLEServer* srv = nullptr;
static BLECharacteristic* dataC = nullptr;
static BLECharacteristic* cmdC = nullptr;
static volatile int8_t sppEvt = 0;   // 1 = มีเครื่องต่อ SPP · -1 = หลุด (ทำใน loop)
static bool sppUp = false;

static int bleCount() {
  int n = 0;
  for (auto& c : conns) if (c.used && c.kind == K_BLE) n++;
  return n;
}

class SrvCb : public BLEServerCallbacks {
  void onConnect(BLEServer*, esp_ble_gatts_cb_param_t* p) override {
    bleOpen(p->connect.conn_id);
    clients = bleCount();
    Serial.printf("[ble] client connected (%d)\n", (int)clients);
    if (clients < 3) BLEDevice::startAdvertising();   // ให้จออื่นต่อเพิ่มได้
  }
  void onDisconnect(BLEServer*, esp_ble_gatts_cb_param_t* p) override {
    bleClose(p->disconnect.conn_id);
    clients = bleCount();
    Serial.printf("[ble] client left (%d)\n", (int)clients);
    BLEDevice::startAdvertising();
  }
};
class CmdCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* ch, esp_ble_gatts_cb_param_t* p) override {
    String v = ch->getValue();
    bleIn(p->write.conn_id, (const uint8_t*)v.c_str(), (int)v.length());
  }
};

// gatts_if ของเซิร์ฟเวอร์ (จับจากเหตุการณ์ GATT) — ไว้ส่งให้จอทีละเครื่อง (ส่งเฉพาะจอที่จับคู่แล้ว)
static volatile esp_gatt_if_t gattsIf = ESP_GATT_IF_NONE;
static void gattsHook(esp_gatts_cb_event_t ev, esp_gatt_if_t gif, esp_ble_gatts_cb_param_t*) {
  if (ev == ESP_GATTS_REG_EVT || (gattsIf == ESP_GATT_IF_NONE && gif != ESP_GATT_IF_NONE)) gattsIf = gif;
}
static void bdNotify(uint16_t h, const uint8_t* d, int n) {
  if (!dataC || gattsIf == ESP_GATT_IF_NONE) return;
  esp_ble_gatts_send_indicate(gattsIf, h, dataC->getHandle(), n, (uint8_t*)d, false);
}
static void bdDrop(uint16_t h) { if (srv) srv->disconnect(h); }

static void advSet(bool open) {
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  BLEAdvertisementData ad, sd;
  ad.setFlags(0x06);
  ad.setCompleteServices(BLEUUID(DIDIBLE_SVC));
  String m;
  m += (char)0xFF; m += (char)0xFF; m += 'D'; m += 'N'; m += (char)(open ? 1 : 0);
  ad.setManufacturerData(m);
  sd.setName(devName);
  adv->stop();
  adv->setAdvertisementData(ad);
  adv->setScanResponseData(sd);
  adv->start();
}

// ---------------------------------------------------------------- Bluetooth ธรรมดา (SPP)
static void sppCb(esp_spp_cb_event_t ev, esp_spp_cb_param_t*) {
  if (ev == ESP_SPP_SRV_OPEN_EVT) sppEvt = 1;
  else if (ev == ESP_SPP_CLOSE_EVT) sppEvt = -1;
}
static void sppTx(const uint8_t* d, int n) {
  if (!sppUp || n <= 0 || n > 200) return;
  unsigned char b[300];
  size_t o = 0;
  b[0] = '~';
  if (mbedtls_base64_encode(b + 1, sizeof(b) - 3, &o, d, n) != 0) return;
  b[1 + o] = '\n';
  SBT.write(b, o + 2);
}

// เรียกใน loop(): รับบรรทัดจาก SPP → แพ็กเก็ต
inline void sppTick() {
  if (sppEvt) {
    int8_t e = sppEvt;
    sppEvt = 0;
    if (e > 0) { sppUp = true; sppOpen(); Serial.println("[spp] connected"); }
    else { sppUp = false; sppClose(); Serial.println("[spp] disconnected"); }
  }
  static char line[300];
  static int ln = 0;
  while (SBT.available()) {
    int c = SBT.read();
    if (c < 0) break;
    if (c == '\n' || c == '\r') {
      if (ln > 1 && line[0] == '~') {
        uint8_t d[220];
        size_t n = 0;
        if (mbedtls_base64_decode(d, sizeof(d), &n, (const unsigned char*)line + 1, ln - 1) == 0 && n >= 5) sppIn(d, (int)n);
      }
      ln = 0;
    } else if (ln < (int)sizeof(line) - 1) {
      line[ln++] = (char)c;
    } else {
      ln = 0;   // ยาวเกิน = ทิ้ง
    }
  }
}

inline bool begin(bool coldBoot) {
  coreBegin("DIDI-SL", coldBoot);
  // Bluetooth ธรรมดา: จับคู่แบบไม่ต้องใส่รหัส (just works) · ชื่อเดียวกับ BLE
  SBT.enableSSP(false, false);
  SBT.register_callback(sppCb);
  bool okSpp = SBT.begin(String(devName), false, false);
  BLEDevice::setCustomGattsHandler(gattsHook);
  BLEDevice::init(String(devName));
  BLEDevice::setMTU(DIDIBLE_MTU);
  srv = BLEDevice::createServer();
  static SrvCb scb;
  srv->setCallbacks(&scb);
  BLEService* svc = srv->createService(DIDIBLE_SVC);
  dataC = svc->createCharacteristic(DIDIBLE_DATA, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  dataC->addDescriptor(new BLE2902());
  cmdC = svc->createCharacteristic(DIDIBLE_CMD, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  static CmdCb ccb;
  cmdC->setCallbacks(&ccb);
  svc->start();
  bleNotify = bdNotify;
  bleDrop = bdDrop;
  advUpdate = advSet;
  sppSend = sppTx;
  strlcpy(macStr, BLEDevice::getAddress().toString().c_str(), sizeof(macStr));
  for (char* c = macStr; *c; c++) *c = toupper(*c);
  advOpen = pairOpen();
  advSet(advOpen);
  Serial.printf("[bt] %s · SPP %s · BLE ok · paired %d · pairing %s\n", devName, okSpp ? "ok" : "FAILED", nPairs,
                pairOpen() ? "OPEN" : "closed");
  return okSpp;
}

}  // namespace c3ble
