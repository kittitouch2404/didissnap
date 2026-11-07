/*
  Didissnap — สเก็ตช์ทดสอบ (ไม่ต้องมีจอ) ใช้กับ ESP32 ได้ทุกตัว (ESP32-S3, CYD ฯลฯ)
  ทดสอบก่อนได้บอร์ดจริง:
    - ESP32 ปล่อย WiFi "N1V65-DASH" (รหัส 12345678) ส่งค่าจำลองให้แอป OBD2_Didissnap
    - ตั้งค่าจอจากแอป (MENU → จอ ESP32) หรือหน้าเว็บ http://192.168.4.1
    - ปุ่ม 2 ปุ่ม: ปุ่ม 1 สั้น = หน้าถัดไป, ค้าง = เมนู / ปุ่ม 2 สั้น = หน้าก่อนหน้า, ค้าง = เลือก
    - ดูผลทั้งหมดใน Serial Monitor (115200)

  ไฟล์ที่ต้องอยู่ในโฟลเดอร์เดียวกัน: N1V65Link.h, N1V65Config.h, N1V65Buttons.h
  Board: ESP32S3 Dev Module (PSRAM: OPI PSRAM) หรือ ESP32 Dev Module สำหรับ CYD
*/
#include <Arduino.h>
#include <Preferences.h>
#include "N1V65Link.h"
#include "N1V65Config.h"
#include "N1V65Buttons.h"

// ขาปุ่ม: แก้ให้ตรงกับ schematic ของบอร์ด (ช่อง CN3: IO1 / IO2)
#define PIN_BTN_A 1
#define PIN_BTN_B 2

Preferences prefs;

// รายชื่อหน้า (ตอนทำจอจริงจะตรงกับหน้าที่วาด)
const char* const PAGE_NAMES[] = {"Bar graph", "Yellow box", "Race bar", "Half gauge",
                                  "Round gauge", "Strip", "Race dash", "Gear big",
                                  "Picture", "Car run", "Photo"};
const int N_PAGES = sizeof(PAGE_NAMES) / sizeof(PAGE_NAMES[0]);

bool inMenu = false;
float tripKm = 0, peakRpm = 0;

void applySettings() {
  Serial.printf("[cfg] redline %d, เตือนน้ำ %d, สว่าง %d%%%s, กลับจอ %s, สลับหน้าทุก %d วิ, เกียร์ %d\n",
                n1cfg::cfg.maxRpm, n1cfg::cfg.tempAlert, n1cfg::cfg.bri,
                n1cfg::cfg.briAuto ? " (auto)" : "", n1cfg::cfg.flip ? "ON" : "OFF",
                n1cfg::cfg.autoCycle, n1cfg::cfg.gearCount);
  // จอจริง: lcd.setBrightness(...), lcd.setRotation(...), วาดหน้าใหม่
}

void gotoPage(int p) {
  Serial.printf("[page] -> %d (%s)\n", p + 1, PAGE_NAMES[p]);
  // จอจริง: switchPage(p);
}

void onButton(int button, bool longPress) {
  Serial.printf("[btn] ปุ่ม %d %s\n", button + 1, longPress ? "กดค้าง" : "กดสั้น");
  if (button == 0 && longPress) {               // ปุ่ม 1 ค้าง = เข้า/ออกเมนู
    inMenu = !inMenu;
    Serial.println(inMenu ? "[menu] เปิดเมนู" : "[menu] ปิดเมนู");
    return;
  }
  if (inMenu) {                                  // ในเมนู: ปุ่ม 1 = เลื่อนลง, ปุ่ม 2 สั้น = เลื่อนขึ้น, ค้าง = เลือก
    Serial.println(button == 0 ? "[menu] เลื่อนลง" : (longPress ? "[menu] เลือก" : "[menu] เลื่อนขึ้น"));
    return;
  }
  if (button == 0) n1cfg::setPage(n1cfg::cfg.page + 1);
  else if (!longPress) n1cfg::setPage(n1cfg::cfg.page - 1);
}

String demoDtcRead() { return "[\"P0133\",\"P0420\"]"; }
bool demoDtcClear() { return true; }
void resetPeaks() { peakRpm = 0; Serial.println("[peaks] ล้างแล้ว"); }
void resetTrip() { tripKm = 0; Serial.println("[trip] รีเซ็ตแล้ว"); }

void setup() {
  Serial.begin(115200);
  delay(300);
  prefs.begin("dash", false);

  if (!n1link::begin()) Serial.println("WiFi FAILED");
  Serial.printf("WiFi %s  IP %s  heap %u  psram %u\n", N1LINK_SSID,
                WiFi.softAPIP().toString().c_str(), ESP.getFreeHeap(), ESP.getFreePsram());
  n1link::onDtcRead = demoDtcRead;
  n1link::onDtcClear = demoDtcClear;

  n1cfg::onApply = applySettings;
  n1cfg::onPage = gotoPage;
  n1cfg::onPeaksReset = resetPeaks;
  n1cfg::onTripReset = resetTrip;
  n1cfg::begin(prefs, PAGE_NAMES, N_PAGES);
  applySettings();
  gotoPage(n1cfg::cfg.page);

  n1btn::onEvent = onButton;
  n1btn::begin(PIN_BTN_A, PIN_BTN_B);
}

void loop() {
  n1link::loop();
  n1cfg::loop();
  n1btn::loop();

  // ค่าจำลอง: เร่ง-ผ่อนวนไป
  float t = millis() / 1000.0f;
  float rpm = 1500 + 2500 * (1 + sinf(t * 0.7f));
  float spd = rpm / n1cfg::cfg.gears[2];
  peakRpm = max(peakRpm, rpm);
  tripKm += spd * 0.001f / 3600.0f;
  n1link::send(rpm, spd, 88 + sinf(t * 0.1f) * 3, 36, 14.1f, 40 + 30 * sinf(t), 20 + 20 * sinf(t),
               60 + 50 * sinf(t * 0.7f), rpm / 400.0f, 67);
  delay(1);
}
