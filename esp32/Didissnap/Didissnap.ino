/*
  Didissnap — สเก็ตช์ทดสอบ (ไม่ต้องมีจอ) ใช้กับ ESP32 ได้ทุกตัว (ESP32-S3, CYD ฯลฯ)
  ทดสอบก่อนได้บอร์ดจริง:
    - ESP32 ปล่อย WiFi "Didissnap" (รหัส 12345678) ส่งค่าจำลองให้แอป OBD2_Didissnap
    - ตั้งค่าจอจากแอป (MENU → จอ ESP32) หรือหน้าเว็บ http://192.168.4.1
    - ปุ่ม 2 ปุ่ม: ปุ่ม 1 สั้น = หน้าถัดไป, ค้าง = เมนู / ปุ่ม 2 สั้น = หน้าก่อนหน้า, ค้าง = เลือก
    - ดูผลทั้งหมดใน Serial Monitor (115200)

    - รับรูปจากแอป/หน้าเว็บ เก็บในแฟลช (LittleFS) ใช้กับหน้า 9-11 และหน้า 12 "รูปตามรอบ"

  ไฟล์ที่ต้องอยู่ในโฟลเดอร์เดียวกัน: DidissnapLink.h, DidissnapConfig.h, DidissnapButtons.h,
                                    DidissnapPics.h, partitions.csv
  Board: ESP32S3 Dev Module · Flash Mode: DIO 80MHz (สำคัญ! QIO = บูตไม่ขึ้น) · Flash Size 16MB
         PSRAM: OPI PSRAM · USB CDC On Boot: Enabled · Partition Scheme: Custom
         (ใช้ partitions.csv ในโฟลเดอร์นี้: แอป 4MB + พื้นที่รูป ~11.9MB)
*/
#include <Arduino.h>
#include <Preferences.h>
#include "DidissnapLink.h"
#include "DidissnapConfig.h"
#include "DidissnapButtons.h"
#include "DidissnapPics.h"

// ขาปุ่ม (ทดสอบกับบอร์ดจริง ZX4D30NE01S-V15): แพด RXD2 = IO1, แพด RXD1 = IO41
// ต่อปุ่มระหว่างแพดกับ GND ไม่ต้องใส่ตัวต้านทาน (ใช้ pull-up ภายใน) · ห้ามใช้ IO38 (มีวงจรอื่นดึง LOW)
#define PIN_BTN_A 1
#define PIN_BTN_B 41

Preferences prefs;

// รายชื่อหน้า (ตอนทำจอจริงจะตรงกับหน้าที่วาด)
const char* const PAGE_NAMES[] = {"Bar graph", "Yellow box", "Race bar", "Half gauge",
                                  "Round gauge", "Strip", "Race dash", "Gear big",
                                  "Picture", "Car run", "Photo", "Pic by rpm"};
const int N_PAGES = sizeof(PAGE_NAMES) / sizeof(PAGE_NAMES[0]);

bool inMenu = false;
float tripKm = 0, peakRpm = 0;

void applySettings() {
  Serial.printf("[cfg] redline %d, เตือนน้ำ %d, สว่าง %d%%%s, กลับจอ %s, สลับหน้าทุก %d วิ, เกียร์ %d\n",
                didicfg::cfg.maxRpm, didicfg::cfg.tempAlert, didicfg::cfg.bri,
                didicfg::cfg.briAuto ? " (auto)" : "", didicfg::cfg.flip ? "ON" : "OFF",
                didicfg::cfg.autoCycle, didicfg::cfg.gearCount);
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
  if (button == 0) didicfg::setPage(didicfg::cfg.page + 1);
  else if (!longPress) didicfg::setPage(didicfg::cfg.page - 1);
}

String demoDtcRead() { return "[\"P0133\",\"P0420\"]"; }
bool demoDtcClear() { return true; }
void resetPeaks() { peakRpm = 0; Serial.println("[peaks] ล้างแล้ว"); }
void resetTrip() { tripKm = 0; Serial.println("[trip] รีเซ็ตแล้ว"); }

void picsChanged() {
  // จอจริง: โหลดรูปของหน้าที่กำลังแสดงใหม่
  Serial.printf("[pics] %d รูป · หน้า9=%s หน้า10=%s หน้า11=%s · ตามรอบ=%s\n", didipics::count,
                didipics::slot[0].length() ? didipics::slot[0].c_str() : "(เดิม)",
                didipics::slot[1].length() ? didipics::slot[1].c_str() : "(เดิม)",
                didipics::slot[2].length() ? didipics::slot[2].c_str() : "(เดิม)",
                didipics::autoSplit ? "แบ่งอัตโนมัติ" : "กำหนดเอง");
}

void setup() {
  Serial.begin(115200);
  delay(300);
  prefs.begin("dash", false);

  if (!didilink::begin()) Serial.println("WiFi FAILED");
  Serial.printf("WiFi %s  IP %s  heap %u  psram %u\n", DIDI_SSID,
                WiFi.softAPIP().toString().c_str(), ESP.getFreeHeap(), ESP.getFreePsram());
  didilink::onDtcRead = demoDtcRead;
  didilink::onDtcClear = demoDtcClear;

  didicfg::onApply = applySettings;
  didicfg::onPage = gotoPage;
  didicfg::onPeaksReset = resetPeaks;
  didicfg::onTripReset = resetTrip;
  didicfg::begin(prefs, PAGE_NAMES, N_PAGES);
  didipics::onChange = picsChanged;
  if (!didipics::begin()) Serial.println("LittleFS FAILED (เช็ค Partition Scheme)");
  picsChanged();
  applySettings();
  gotoPage(didicfg::cfg.page);

  didibtn::onEvent = onButton;
  didibtn::begin(PIN_BTN_A, PIN_BTN_B);
}

void loop() {
  didilink::loop();
  didicfg::loop();
  didibtn::loop();

  // ค่าจำลอง: เร่ง-ผ่อนวนไป
  float t = millis() / 1000.0f;
  float rpm = 1500 + 2500 * (1 + sinf(t * 0.7f));
  float spd = rpm / didicfg::cfg.gears[2];
  peakRpm = max(peakRpm, rpm);
  tripKm += spd * 0.001f / 3600.0f;
  didilink::send(rpm, spd, 88 + sinf(t * 0.1f) * 3, 36, 14.1f, 40 + 30 * sinf(t), 20 + 20 * sinf(t),
               60 + 50 * sinf(t * 0.7f), rpm / 400.0f, 67);
  delay(1);
}
