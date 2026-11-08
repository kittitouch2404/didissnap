/*
  Didissnap_S3_Test — ทดสอบบอร์ด Panlee ZX4D30NE01S (ESP32-S3 + จอ 4.3" 480x272 RGB)

  ทำอะไร
   1) เปิดจอ: แถบสี แดง/เขียว/น้ำเงิน/ขาว + ข้อความ  → เช็คว่าจอติด สีถูก ภาพไม่เลื่อน
   2) แถบวิ่ง + ตัวเลข FPS → เช็คความลื่น
   3) ตาราง GPIO สด: เอาสายไฟแตะแพดที่จะต่อปุ่ม (TXD2 / RXD2 ฯลฯ) กับ GND
      ตัวไหนเปลี่ยนเป็น LOW (สีแดง) = แพดนั้นคือ GPIO เลขนั้น
   4) ความสว่าง: ถ้า IO0 (BOOT) LOW จะวนความสว่าง 100 → 50 → 15 %

  ต้องลง Library: "GFX Library for Arduino" (moononournation) เวอร์ชัน 1.5 ขึ้นไป
  Arduino IDE: Board = ESP32S3 Dev Module
               Flash Size = 16MB · PSRAM = OPI PSRAM · USB CDC On Boot = Disabled
               (ถ้าเปิด Serial Monitor แล้วเงียบ ลองเปลี่ยน USB CDC On Boot = Enabled)
  ขาจอเอามาจากโค้ดตัวอย่างของผู้ผลิต: github.com/wireless-tag-com/ZX4D30NE01S (main/board.h, screen.c)
*/
#include <Arduino_GFX_Library.h>

#define PIN_BL 45  // ไฟจอ (HIGH = ติด)

// ---- จอ RGB 16 บิต: (DE, VSYNC, HSYNC, PCLK, R0-R4, G0-G5, B0-B4, timing...)
Arduino_ESP32RGBPanel* bus = new Arduino_ESP32RGBPanel(
    40 /* DE */, 48 /* VSYNC */, 47 /* HSYNC */, 14 /* PCLK */,
    4 /* R0 */, 5 /* R1 */, 6 /* R2 */, 7 /* R3 */, 15 /* R4 */,
    16 /* G0 */, 17 /* G1 */, 18 /* G2 */, 8 /* G3 */, 3 /* G4 */, 46 /* G5 */,
    9 /* B0 */, 10 /* B1 */, 11 /* B2 */, 12 /* B3 */, 13 /* B4 */,
    1 /* hsync_polarity */, 60 /* hsync_front_porch */, 10 /* hsync_pulse_width */, 80 /* hsync_back_porch */,
    1 /* vsync_polarity */, 60 /* vsync_front_porch */, 10 /* vsync_pulse_width */, 20 /* vsync_back_porch */,
    1 /* pclk_active_neg */, 14000000 /* pclk 14 MHz */);
Arduino_RGB_Display* gfx = new Arduino_RGB_Display(480, 272, bus, 0 /* rotation */, true /* auto_flush */);

// ---- แพดที่น่าจะว่าง (ไม่ใช้กับจอ/PSRAM/USB) — ทดสอบด้วยการแตะกับ GND
const int PROBE[] = {0, 1, 2, 21, 38, 39, 41, 42};
const int NP = sizeof(PROBE) / sizeof(PROBE[0]);
int last[NP], lows[NP];

const uint16_t C_BLACK = 0x0000, C_WHITE = 0xFFFF, C_RED = 0xF800, C_GREEN = 0x07E0,
               C_BLUE = 0x001F, C_ORANGE = 0xFBE0, C_GREY = 0x7BEF, C_DARK = 0x2104, C_YELLOW = 0xFFE0;

int bri = 255;
void setBri(int v) {
  bri = v;
  ledcWrite(PIN_BL, v);
}

void drawStatic() {
  gfx->fillScreen(C_BLACK);
  // แถบสี: ต้องเห็นเรียงจากซ้าย แดง เขียว น้ำเงิน ขาว
  const uint16_t bars[] = {C_RED, C_GREEN, C_BLUE, C_WHITE};
  for (int i = 0; i < 4; i++) gfx->fillRect(i * 120, 0, 120, 40, bars[i]);
  gfx->drawRect(0, 0, 480, 272, C_ORANGE);   // กรอบขอบจอ: ต้องเห็นครบ 4 ด้าน
  gfx->setTextColor(C_ORANGE);
  gfx->setTextSize(3);
  gfx->setCursor(12, 52);
  gfx->print("Didissnap S3 TEST");
  gfx->setTextSize(1);
  gfx->setTextColor(C_GREY);
  gfx->setCursor(12, 82);
  gfx->printf("480x272  heap %u KB  psram %u KB", ESP.getFreeHeap() / 1024, ESP.getFreePsram() / 1024);
  gfx->setCursor(12, 96);
  gfx->print("Touch a pad to GND -> its GPIO turns RED");
  for (int i = 0; i < NP; i++) {
    int x = 12 + (i % 4) * 116, y = 120 + (i / 4) * 54;
    gfx->drawRect(x, y, 108, 46, C_GREY);
    gfx->setTextSize(2);
    gfx->setTextColor(C_WHITE);
    gfx->setCursor(x + 6, y + 6);
    gfx->printf("IO%d", PROBE[i]);
    last[i] = -1;
  }
}

void drawPin(int i, int v) {
  int x = 12 + (i % 4) * 116, y = 120 + (i / 4) * 54;
  gfx->fillRect(x + 1, y + 26, 106, 19, v ? C_DARK : C_RED);
  gfx->setTextSize(2);
  gfx->setTextColor(C_WHITE);
  gfx->setCursor(x + 6, y + 28);
  gfx->printf("%s %d", v ? "HIGH" : "LOW ", lows[i]);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  ledcAttach(PIN_BL, 5000, 8);
  setBri(255);
  if (!gfx->begin()) Serial.println("gfx->begin() FAILED");
  for (int i = 0; i < NP; i++) {
    pinMode(PROBE[i], INPUT_PULLUP);
    lows[i] = 0;
  }
  drawStatic();
  Serial.printf("Didissnap S3 test  heap %u  psram %u\n", ESP.getFreeHeap(), ESP.getFreePsram());
}

uint32_t frames = 0, fpsT = 0, briT = 0;
int barX = 0;

void loop() {
  // แถบวิ่ง (เช็คความลื่น)
  gfx->fillRect(barX, 236, 60, 26, C_DARK);
  barX = (barX + 4) % 420;
  gfx->fillRect(barX, 236, 60, 26, C_ORANGE);
  frames++;
  if (millis() - fpsT >= 1000) {
    gfx->fillRect(400, 52, 76, 24, C_BLACK);
    gfx->setTextSize(2);
    gfx->setTextColor(C_YELLOW);
    gfx->setCursor(400, 56);
    gfx->printf("%u fps", frames);
    frames = 0;
    fpsT = millis();
  }
  // GPIO สด
  for (int i = 0; i < NP; i++) {
    int v = digitalRead(PROBE[i]);
    if (v != last[i]) {
      if (last[i] == 1 && v == 0) lows[i]++;
      if (last[i] != -1) Serial.printf("IO%d -> %s\n", PROBE[i], v ? "HIGH" : "LOW");
      last[i] = v;
      drawPin(i, v);
    }
  }
  // IO0 (BOOT) ค้าง LOW → วนความสว่าง
  if (digitalRead(0) == LOW && millis() - briT > 700) {
    setBri(bri == 255 ? 128 : bri == 128 ? 40 : 255);
    briT = millis();
    Serial.printf("brightness %d\n", bri);
  }
  delay(10);
}
