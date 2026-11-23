// didinow.h — รูปแบบข้อมูล ESP-NOW ระหว่างบอร์ด S3 (ตัวหลัก ต่อ OBD) กับจอ CYD 2.8" (ตัวรับ)
// ไฟล์นี้ต้องเหมือนกันทุกโปรเจกต์ (Didissnap_S3, obd2_cyd_dashboard, didi_c3_gateway)
// C3 (Super Mini ที่ช่อง OBD) อ่านค่าจากรถ → ส่ง DN_GW ให้ทุกจอ · S3 ปรับค่าแล้วส่งต่อเป็น DN_DATA
#pragma once
#include <stdint.h>

#define DIDINOW_MAGIC 0x31444944u   // "DID1"
#define DIDINOW_CHANNEL 1           // ช่อง WiFi ของ S3 (SoftAP "Didissnap" ตั้งช่อง 1)

enum : uint8_t { DN_DATA = 1, DN_CMD = 2, DN_DTC = 3, DN_GW = 4, DN_GWCMD = 5, DN_GWINFO = 6, DN_RAWREQ = 7, DN_RAWRES = 8 };
enum : uint8_t { DN_LINK_DEMO = 0, DN_LINK_WAIT = 1, DN_LINK_OK = 2 };
enum : uint8_t { DN_CMD_DTC_READ = 1, DN_CMD_DTC_CLEAR = 2, DN_CMD_MODE_AUTO = 3, DN_CMD_MODE_DEMO = 4 };
// โปรโตคอลที่ C3 ใช้คุยกับรถ
enum : uint8_t { DN_P_NONE = 0, DN_P_DEMO = 1, DN_P_CAN500 = 2, DN_P_CAN250 = 3, DN_P_ISO9141 = 4, DN_P_KWPFAST = 5, DN_P_KWPSLOW = 6 };
static const char* const DN_P_NAME[] = {"search", "DEMO", "CAN 500k", "CAN 250k", "K ISO9141", "K KWP fast", "K KWP slow"};

// S3 -> จอ: ค่ารถ ~30 ครั้ง/วินาที (ค่าดิบจาก OBD ยังไม่หน่วง — จอหน่วงเอง)
struct __attribute__((packed)) DidiNowData {
  uint32_t magic;
  uint8_t type;       // DN_DATA
  uint8_t link;       // DN_LINK_*: 0 = S3 โหมดจำลอง, 1 = กำลังต่อ OBD, 2 = ได้ค่าจากรถ
  uint16_t seq;
  uint32_t rpmT;      // millis() ฝั่ง S3 ตอนได้ค่ารอบล่าสุด (เปลี่ยน = ค่ารอบใหม่) 0 = โหมดจำลอง
  float rpm, spd, ect, iat, volt, load, tps, map, maf, fuel, stft;
};

// จอ -> S3: ขออ่าน/ลบ DTC
struct __attribute__((packed)) DidiNowCmd {
  uint32_t magic;
  uint8_t type;       // DN_CMD
  uint8_t cmd;        // DN_CMD_*
  uint16_t id;        // เลขคำขอ (คำตอบจะมีเลขเดียวกัน)
};

// S3 -> จอ: ผล DTC
struct __attribute__((packed)) DidiNowDtc {
  uint32_t magic;
  uint8_t type;       // DN_DTC
  uint8_t ok;         // 1 = สำเร็จ
  uint8_t n;          // จำนวนรหัส (สูงสุด 16)
  uint8_t cmd;        // คำขอที่ตอบ
  uint16_t id;
  char codes[16][6];  // เช่น "P0133"
};

// C3 -> ทุกจอ: ค่าดิบจากรถ (ยังไม่ปรับ CALIBRATE) ~30 ครั้ง/วินาที
struct __attribute__((packed)) DidiNowGw {
  uint32_t magic;
  uint8_t type;       // DN_GW
  uint8_t link;       // DN_LINK_*: 0 = โหมดจำลอง, 1 = กำลังหารถ/ต่อ, 2 = ได้ค่าจากรถ
  uint8_t proto;      // DN_P_*
  uint8_t afrMode;    // 0 ไม่มี · 1 wideband · 2 narrowband (V) · 3 ค่าที่ ECU สั่ง
  uint16_t seq;
  uint16_t rate;      // คำตอบจากรถต่อวินาที
  uint32_t rpmT;      // millis() ฝั่ง C3 ตอนได้ค่ารอบล่าสุด
  float rpm, spd, ect, iat, volt, load, tps, map, maf, fuel, stft, afr;
  float timing, oil;  // องศาจุดระเบิด (0E) / น้ำมันเครื่อง (5C) · -999 = รถไม่รองรับ
};
// จอ -> C3 ใช้ DidiNowCmd โดย type = DN_GWCMD (คำตอบ DTC กลับมาเป็น DidiNowDtc เลข id เดียวกัน)
// C3 -> จอ: ข้อความสถานะ/ผลตรวจสาย ตอนยังหารถไม่เจอ (โชว์ในเมนู OBD ไม่ต้องต่อคอมดู Serial)
struct __attribute__((packed)) DidiNowInfo {
  uint32_t magic;
  uint8_t type;       // DN_GWINFO
  char text[59];
};

// จอ/แอป -> C3: ถามรถด้วยคำสั่งเอง (Custom PID / อุณหภูมิเกียร์ ฯลฯ)
struct __attribute__((packed)) DidiNowRaw {
  uint32_t magic;
  uint8_t type;       // DN_RAWREQ
  uint8_t len;        // จำนวนไบต์คำสั่ง (1-7)
  uint16_t id;        // เลขคำขอ
  uint16_t hdr;       // CAN: ID ที่ส่งไป เช่น 0x7E1 กล่องเกียร์ · 0 = เครื่องยนต์ (ปกติ)
  uint8_t data[7];    // เช่น 01 5C / 22 xx xx
};
// C3 -> จอ/แอป: คำตอบดิบ (รวม 7F = ปฏิเสธ)
struct __attribute__((packed)) DidiNowRawRes {
  uint32_t magic;
  uint8_t type;       // DN_RAWRES
  int8_t n;           // จำนวนไบต์คำตอบ · -1 = ไม่มีใครตอบ · -2 = ไม่รองรับ (K-line ใช้ header อื่นไม่ได้)
  uint16_t id;
  uint8_t data[48];   // ไบต์คำตอบ เริ่มที่ SID+0x40 (หรือ 7F)
};

// ---------------------------------------------------------------- Bluetooth (BLE) แทน ESP-NOW
// C3 เป็นตัวปล่อย (server) ให้จอ S3 / CYD / แอปแอนดรอยด์ ต่อพร้อมกันได้ (สูงสุด 3)
//  DATA: แจ้งเตือน (notify) ทุกแพ็กเก็ตข้างบน (ดูชนิดจาก type)  ·  CMD: เขียนคำสั่ง (DidiNowCmd / DidiNowRaw)
#define DIDIBLE_NAME "Didissnap-C3"
#define DIDIBLE_SVC  "d1d15500-6f0b-4e8b-9c4e-0b5d1d150000"
#define DIDIBLE_DATA "d1d15501-6f0b-4e8b-9c4e-0b5d1d150000"
#define DIDIBLE_CMD  "d1d15502-6f0b-4e8b-9c4e-0b5d1d150000"
#define DIDIBLE_MTU  185
