// didinow.h — รูปแบบข้อมูล ESP-NOW ระหว่างบอร์ด S3 (ตัวหลัก ต่อ OBD) กับจอ CYD 2.8" (ตัวรับ)
// ไฟล์นี้ต้องเหมือนกันทุกโปรเจกต์ (Didissnap_S3, obd2_cyd_dashboard, didi_c3_gateway)
// C3 (Super Mini ที่ช่อง OBD) อ่านค่าจากรถ → ส่ง DN_GW ให้ทุกจอ · S3 ปรับค่าแล้วส่งต่อเป็น DN_DATA
#pragma once
#include <stdint.h>

#define DIDINOW_MAGIC 0x31444944u   // "DID1"
#define DIDINOW_CHANNEL 1           // ช่อง WiFi ของ S3 (SoftAP "Didissnap" ตั้งช่อง 1)

enum : uint8_t { DN_DATA = 1, DN_CMD = 2, DN_DTC = 3, DN_GW = 4, DN_GWCMD = 5, DN_GWINFO = 6 };
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
};
// จอ -> C3 ใช้ DidiNowCmd โดย type = DN_GWCMD (คำตอบ DTC กลับมาเป็น DidiNowDtc เลข id เดียวกัน)
// C3 -> จอ: ข้อความสถานะ/ผลตรวจสาย ตอนยังหารถไม่เจอ (โชว์ในเมนู OBD ไม่ต้องต่อคอมดู Serial)
struct __attribute__((packed)) DidiNowInfo {
  uint32_t magic;
  uint8_t type;       // DN_GWINFO
  char text[59];
};
