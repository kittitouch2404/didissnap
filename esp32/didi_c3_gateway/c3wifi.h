// c3wifi.h — C3 ต่อฮอตสปอตมือถือ แล้วส่งค่ารถให้จอแอนดรอยด์ทาง WiFi (UDP) พร้อมกับ BLE
//  ใช้กับจอรถที่ไม่มี Bluetooth BLE ให้แอป (จอจีนหลายรุ่น) · จอ S3 / CYD ยังใช้ BLE ตามเดิม
//  ตั้งชื่อ/รหัส WiFi จากแอปในมือถือ (ทาง BLE) — C3 จำไว้ในตัว
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include "didinow.h"
#include "linkcore.h"

namespace c3wifi {

static Preferences wp;
static WiFiUDP udp;
static String ssid, pass;
static bool started = false, udpOn = false;
static uint32_t lastBeacon = 0, lastSt = 0, lastTry = 0;
static uint8_t lastState = 255;
static char mac[18] = "";
static volatile bool pause = false;   // อัปเดตเฟิร์มแวร์อยู่ → ปิด WiFi ชั่วคราว
static bool paused = false;

static void sendUdp(uint32_t ip, uint16_t port, const uint8_t* d, int n) {
  if (!udpOn) return;
  udp.beginPacket(IPAddress(ip), port);
  udp.write(d, n);
  udp.endPacket();
}

static void connect() {
  if (!ssid.length()) return;
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);            // ต้องเปิด modem sleep เมื่อใช้คู่กับ BLE
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid.c_str(), pass.c_str());
  started = true;
  lastTry = millis();
  Serial.printf("[wifi] connecting to '%s'\n", ssid.c_str());
}

inline void begin() {
  wp.begin("c3wifi", false);
  ssid = wp.getString("ssid", "");
  pass = wp.getString("pass", "");
  strlcpy(mac, c3ble::macStr, sizeof(mac));
  c3ble::udpSend = sendUdp;
  connect();
}

inline uint8_t state() {
  if (!started || !ssid.length()) return 0;
  return WiFi.status() == WL_CONNECTED ? 2 : 1;
}

static void sendStatus() {
  DidiNowWifiSt m = {};
  m.magic = DIDINOW_MAGIC; m.type = DN_WIFIST;
  m.state = state();
  m.rssi = m.state == 2 ? (int8_t)WiFi.RSSI() : 0;
  m.clients = (uint8_t)c3ble::udpClients();
  if (m.state == 2) { IPAddress ip = WiFi.localIP(); for (int i = 0; i < 4; i++) m.ip[i] = ip[i]; }
  strlcpy(m.ssid, ssid.c_str(), sizeof(m.ssid));
  c3ble::send(&m, sizeof(m));
}

// คำสั่งจากจอที่จับคู่แล้ว (เรียกจาก gotCmd — อาจมาจากงาน BLE: เก็บไว้ทำใน loop)
//  แพ็กเก็ตสั้น (8 ไบต์) = ping ของจอทาง WiFi ไม่ต้องทำอะไร
static DidiNowWifi pend;
static volatile bool pendOn = false;
inline void command(const uint8_t* d, int n) {
  if (n < (int)sizeof(DidiNowWifi) || pendOn) return;
  memcpy(&pend, d, sizeof(pend));
  pendOn = true;
}
static void doCommand() {
  DidiNowWifi w = pend;
  pendOn = false;
  w.ssid[32] = 0; w.pass[64] = 0;
  if (w.op == DN_WIFI_GET) { lastState = 255; return; }
  if (w.op == DN_WIFI_SET) {
    ssid = w.ssid; pass = w.pass;
    wp.putString("ssid", ssid);
    wp.putString("pass", pass);
    WiFi.disconnect(true);
    udp.stop(); udpOn = false;
    connect();
  } else if (w.op == DN_WIFI_OFF) {
    ssid = ""; pass = "";
    wp.remove("ssid"); wp.remove("pass");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    udp.stop(); udpOn = false; started = false;
    Serial.println("[wifi] off");
  }
  lastState = 255;   // ส่งสถานะใหม่ทันที
}

// เรียกใน loop()
inline void tick() {
  if (pendOn) doCommand();
  uint32_t now = millis();
  if (pause) {
    if (started && !paused) { paused = true; udp.stop(); udpOn = false; WiFi.disconnect(false); Serial.println("[wifi] paused (OTA)"); }
    return;
  }
  if (paused) { paused = false; connect(); }
  if (!started) {
    if (now - lastSt > 5000) { lastSt = now; sendStatus(); }
    return;
  }
  bool up = WiFi.status() == WL_CONNECTED;
  if (up && !udpOn) {
    udpOn = udp.begin(DN_UDP_C3);
    Serial.printf("[wifi] connected %s · UDP %s\n", WiFi.localIP().toString().c_str(), udpOn ? "ok" : "FAILED");
  } else if (!up && udpOn) {
    udp.stop(); udpOn = false;
    Serial.println("[wifi] lost");
  }
  if (!up && now - lastTry > 5000) { lastTry = now; WiFi.reconnect(); }
  uint8_t st = state();
  if (st != lastState || now - lastSt > 5000) { lastState = st; lastSt = now; sendStatus(); }
  if (!udpOn) return;
  // รับแพ็กเก็ตจากจอ
  static uint8_t buf[256];
  for (int g = 0; g < 8; g++) {
    int sz = udp.parsePacket();
    if (sz <= 0) break;
    int n = udp.read(buf, sizeof(buf));
    if (n < 5 || *(uint32_t*)buf != DIDINOW_MAGIC) continue;
    c3ble::udpIn((uint32_t)udp.remoteIP(), udp.remotePort(), buf, n);
  }
  // ประกาศตัวทุก 1 วิ (จอหา C3 เอง ไม่ต้องใส่ IP)
  if (now - lastBeacon >= 1000) {
    lastBeacon = now;
    DidiNowBeacon b = {};
    b.magic = DIDINOW_MAGIC; b.type = DN_BEACON;
    b.open = c3ble::pairOpen() ? 1 : 0;
    strlcpy(b.mac, mac, sizeof(b.mac));
    strlcpy(b.name, c3ble::devName, sizeof(b.name));
    sendUdp((uint32_t)IPAddress(255, 255, 255, 255), DN_UDP_APP, (const uint8_t*)&b, sizeof(b));
    IPAddress bc = WiFi.broadcastIP();
    if (bc != IPAddress(255, 255, 255, 255)) sendUdp((uint32_t)bc, DN_UDP_APP, (const uint8_t*)&b, sizeof(b));
  }
}

}  // namespace c3wifi
