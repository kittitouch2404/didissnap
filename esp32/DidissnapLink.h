// =====================================================================
//  DidissnapLink.h  —  ให้ ESP32 CYD ส่งค่า OBD2 ต่อไปที่แอป Android (Didissnap)
//
//  ESP32 ปล่อย WiFi เอง (SoftAP) → มือถือต่อ WiFi นี้ → แอปต่อ TCP มาที่ 192.168.4.1:3333
//  ส่งค่าเป็น JSON ทีละบรรทัด:
//    {"rpm":2350,"spd":42,"ect":88,"iat":36,"volt":14.1,"load":32,"tps":12,"map":40,"maf":6.2,"fuel":67}
//  แอปส่งคำสั่งกลับมา:  {"cmd":"dtc_read"}  /  {"cmd":"dtc_clear"}
//
//  วิธีใช้ในสเก็ตช์เดิม:
//    #include "DidissnapLink.h"
//    setup():  didilink::onDtcRead = myDtcRead;   // (ไม่ใส่ก็ได้)
//              didilink::onDtcClear = myDtcClear; // (ไม่ใส่ก็ได้)
//              didilink::begin();
//    loop():   didilink::loop();
//              didilink::send(rpm, speed, coolant, iat, volt, load, tps, map, maf, fuel);
//              (ค่าที่ไม่มี ให้ส่ง NAN — จะไม่ถูกส่งไป)
//
//  ESP32 core 3.x (ทดสอบตามโค้ด core 3.2.0 / Arduino IDE)
// =====================================================================
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <math.h>

#ifndef DIDI_SSID
#define DIDI_SSID "Didissnap"
#endif
#ifndef DIDI_PASS
#define DIDI_PASS "12345678"   // อย่างน้อย 8 ตัวอักษร
#endif
#ifndef DIDI_PORT
#define DIDI_PORT 3333
#endif
#ifndef DIDI_MAX_STA
#define DIDI_MAX_STA 2         // มือถือที่ต่อ WiFi พร้อมกันได้ (CYD แรมน้อย ใช้ 1)
#endif
#ifndef DIDI_INTERVAL_MS
#define DIDI_INTERVAL_MS 50    // ส่ง 20 ครั้ง/วินาที
#endif

namespace didilink {

// คืนค่าเป็น JSON array เช่น  "[\"P0133\",\"P0420\"]"  หรือ "[]"
typedef String (*DtcReadFn)();
typedef bool (*DtcClearFn)();

static DtcReadFn onDtcRead = nullptr;
static DtcClearFn onDtcClear = nullptr;

// คำสั่งอื่นจากแอป (เช่น ค่าตั้งจาก DidissnapConfig.h) คืน true = จัดการแล้ว
typedef bool (*CmdFn)(const String& line);
static CmdFn onCommand = nullptr;

// ค่าล่าสุดเป็น JSON (ใช้กับหน้าเว็บ /api/live)
static char lastJson[224] = "{}";

static WiFiServer server(DIDI_PORT);
static WiFiClient client;
static String rx;
static uint32_t lastSend = 0;

// คืน true = ปล่อย WiFi สำเร็จ (false = แรมไม่พอ/เริ่ม WiFi ไม่ได้)
inline bool begin() {
  if (!WiFi.mode(WIFI_AP)) return false;
  // ช่อง 1, ไม่ซ่อนชื่อ, ต่อได้สูงสุด DIDI_MAX_STA เครื่อง
  if (!WiFi.softAP(DIDI_SSID, DIDI_PASS, 1, 0, DIDI_MAX_STA)) return false;
#ifdef DIDI_TX_POWER
  WiFi.setTxPower(DIDI_TX_POWER);   // ลดกำลังส่ง = กินไฟน้อยลง (ในรถมือถืออยู่ใกล้)
#endif
  // ห้าม WiFi.setSleep(false) เมื่อใช้ Bluetooth ไปด้วย (ESP32 จะ abort/รีบูต)
  server.begin();
  server.setNoDelay(true);
  return true;
}

inline bool connected() { return client && client.connected(); }

// ปิด WiFi และคืนแรม
inline void end() {
  if (client) client.stop();
  server.end();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
}

// ส่งข้อความกลับไปที่แอป (ต่อท้ายด้วยขึ้นบรรทัดใหม่ให้เอง)
inline void reply(const String& s) {
  if (!connected()) return;
  client.print(s);
  client.print("\n");
}

inline void handleCommand(const String& line) {
  if (onCommand && onCommand(line)) return;
  if (line.indexOf("dtc_read") >= 0) {
    String arr = onDtcRead ? onDtcRead() : String("[]");
    client.print("{\"dtc\":" + arr + "}\n");
  } else if (line.indexOf("dtc_clear") >= 0) {
    bool ok = onDtcClear ? onDtcClear() : false;
    client.print(ok ? "{\"cleared\":true}\n" : "{\"cleared\":false}\n");
  }
}

// เรียกทุกรอบใน loop()
inline void loop() {
  if (server.hasClient()) {
    WiFiClient n = server.accept();
    if (connected()) client.stop();  // รับมือถือได้ทีละเครื่อง (เครื่องใหม่แทนเครื่องเก่า)
    client = n;
    client.setNoDelay(true);
    rx = "";
  }
  if (!connected()) return;
  while (client.available()) {
    char c = (char)client.read();
    if (c == '\n') {
      handleCommand(rx);
      rx = "";
    } else if (rx.length() < 600) {
      rx += c;
    }
  }
}

static void addField(char* buf, size_t cap, size_t& len, const char* key,
                     float v, int dec) {
  if (isnan(v) || len >= cap) return;
  int n = snprintf(buf + len, cap - len, "%s\"%s\":%.*f",
                   len > 1 ? "," : "", key, dec, v);
  if (n > 0) len += (size_t)n;
}

// ส่งค่าล่าสุด (จำกัดความถี่เองตาม DIDI_INTERVAL_MS)
inline void send(float rpm, float spd, float ect, float iat, float volt,
                 float load = NAN, float tps = NAN, float map = NAN,
                 float maf = NAN, float fuel = NAN) {
  uint32_t now = millis();
  if (now - lastSend < DIDI_INTERVAL_MS) return;
  lastSend = now;

  char buf[220];
  size_t len = 0;
  buf[len++] = '{';
  buf[len] = 0;
  addField(buf, sizeof(buf) - 3, len, "rpm", rpm, 0);
  addField(buf, sizeof(buf) - 3, len, "spd", spd, 0);
  addField(buf, sizeof(buf) - 3, len, "ect", ect, 0);
  addField(buf, sizeof(buf) - 3, len, "iat", iat, 0);
  addField(buf, sizeof(buf) - 3, len, "volt", volt, 1);
  addField(buf, sizeof(buf) - 3, len, "load", load, 0);
  addField(buf, sizeof(buf) - 3, len, "tps", tps, 0);
  addField(buf, sizeof(buf) - 3, len, "map", map, 0);
  addField(buf, sizeof(buf) - 3, len, "maf", maf, 1);
  addField(buf, sizeof(buf) - 3, len, "fuel", fuel, 0);
  buf[len++] = '}';
  memcpy(lastJson, buf, len);           // เก็บไว้ให้หน้าเว็บ
  lastJson[len] = 0;
  if (!connected()) return;
  buf[len++] = '\n';
  client.write((const uint8_t*)buf, len);
}

}  // namespace didilink
