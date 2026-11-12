// didinow.h — รูปแบบข้อมูล ESP-NOW ระหว่างบอร์ด S3 (ตัวหลัก ต่อ OBD) กับจอ CYD 2.8" (ตัวรับ)
// ไฟล์นี้ต้องเหมือนกันทั้งสองโปรเจกต์ (Didissnap_S3 และ obd2_cyd_dashboard)
#pragma once
#include <stdint.h>

#define DIDINOW_MAGIC 0x31444944u   // "DID1"
#define DIDINOW_CHANNEL 1           // ช่อง WiFi ของ S3 (SoftAP "Didissnap" ตั้งช่อง 1)

enum : uint8_t { DN_DATA = 1, DN_CMD = 2, DN_DTC = 3 };
enum : uint8_t { DN_LINK_DEMO = 0, DN_LINK_WAIT = 1, DN_LINK_OK = 2 };
enum : uint8_t { DN_CMD_DTC_READ = 1, DN_CMD_DTC_CLEAR = 2 };

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
