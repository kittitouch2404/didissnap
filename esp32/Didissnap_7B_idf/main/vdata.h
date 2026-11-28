// vdata.h — ค่ารถล่าสุด (จาก Superlink ทาง BLE หรือโหมดจำลอง)
#pragma once
#include <stdint.h>

namespace vd {

// ลำดับต้องตรงกับ KEYS ใน vdata.cpp (ชื่อที่ใช้ในไฟล์สกิน)
enum Key {
  RPM, SPD, ECT, IAT, VOLT, LOAD, TPS, MAP, MAF, FUEL, STFT, AFR, TIMING, OIL,
  CVT, C1, C2, C3, C4, BOOST, GEAR, NKEY
};
extern const char* const KEYS[NKEY];
extern const char* const NAMES_TH[NKEY];   // ชื่อไทยสั้น ๆ (ใช้ในหน้าตั้งค่า)

struct Snap {
  float v[NKEY];
  bool have[NKEY];     // รถรองรับ/มีค่า
  uint8_t link;        // 0 จำลอง · 1 กำลังหา/ต่อ · 2 ได้ค่าจากรถ
  uint8_t proto;
  uint16_t rate;       // คำตอบจากรถต่อวินาที
  bool live;           // ได้ค่าใหม่ภายใน 1.5 วิ
  float pmax[NKEY], pmin[NKEY];   // ค่าสูงสุด/ต่ำสุดตั้งแต่รีเซ็ต (peak hold)
  bool phave[NKEY];
};

int keyOf(const char* name);       // -1 = ไม่รู้จัก
void begin();
void setDemo(bool on);
bool demo();
void feed(const uint8_t* d, int n);  // แพ็กเก็ตจาก Superlink
void get(Snap& s);                   // สำเนาค่าปัจจุบัน (เรียกจากงาน LVGL)
const char* info();                  // ข้อความสถานะจาก Superlink (DN_GWINFO)
void resetPeaks();

// เกียร์จาก รอบ ÷ ความเร็ว: จำอัตราส่วน (rpm ต่อ km/h) ของแต่ละเกียร์ไว้ใน NVS
constexpr int NGEAR = 6;
float gearRatio(int i);              // 0 = ยังไม่ได้ตั้ง
bool learnGear(int i);               // จำอัตราส่วนตอนนี้เป็นเกียร์ i (0-based) · false = ยังไม่มีรอบ/ความเร็ว
void clearGears();
float liveRatio();                   // อัตราส่วนตอนนี้ (0 = ไม่มี)

}  // namespace vd
