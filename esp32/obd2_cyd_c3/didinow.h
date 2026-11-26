// didinow.h — รูปแบบข้อมูล ESP-NOW ระหว่างบอร์ด S3 (ตัวหลัก ต่อ OBD) กับจอ CYD 2.8" (ตัวรับ)
// ไฟล์นี้ต้องเหมือนกันทุกโปรเจกต์ (Didissnap_S3, obd2_cyd_dashboard, didi_c3_gateway)
// C3 (Super Mini ที่ช่อง OBD) อ่านค่าจากรถ → ส่ง DN_GW ให้ทุกจอ · S3 ปรับค่าแล้วส่งต่อเป็น DN_DATA
#pragma once
#include <stdint.h>

#define DIDINOW_MAGIC 0x31444944u   // "DID1"
#define DIDINOW_CHANNEL 1           // ช่อง WiFi ของ S3 (SoftAP "Didissnap" ตั้งช่อง 1)

enum : uint8_t { DN_DATA = 1, DN_CMD = 2, DN_DTC = 3, DN_GW = 4, DN_GWCMD = 5, DN_GWINFO = 6, DN_RAWREQ = 7, DN_RAWRES = 8,
                 DN_CUSTSET = 9, DN_CUST = 10, DN_CUSTINFO = 11, DN_CUSTRAW = 12, DN_CAL = 13, DN_OTA = 14, DN_OTAST = 15,
                 DN_AUTH = 16, DN_AUTHST = 17, DN_WIFI = 18, DN_WIFIST = 19, DN_BEACON = 20 };
enum : uint8_t { DN_LINK_DEMO = 0, DN_LINK_WAIT = 1, DN_LINK_OK = 2 };
enum : uint8_t { DN_CMD_DTC_READ = 1, DN_CMD_DTC_CLEAR = 2, DN_CMD_MODE_AUTO = 3, DN_CMD_MODE_DEMO = 4,
                 DN_CMD_DYNO = 5 };   // DYNO: แอปส่งทุก 1 วิ ตอนวัดแรงม้า → C3 ถามแค่รอบ/ความเร็ว/TPS ให้ถี่ที่สุด (หมดอายุเองใน 2.5 วิ)
// โปรโตคอลที่ C3 ใช้คุยกับรถ
enum : uint8_t { DN_P_NONE = 0, DN_P_DEMO = 1, DN_P_CAN500 = 2, DN_P_CAN250 = 3, DN_P_ISO9141 = 4, DN_P_KWPFAST = 5, DN_P_KWPSLOW = 6,
                 DN_P_CAN500X = 7, DN_P_CAN250X = 8 };   // X = CAN ID 29 บิต
static const char* const DN_P_NAME[] = {"search", "DEMO", "CAN 500k", "CAN 250k", "K ISO9141", "K KWP fast", "K KWP slow", "CAN 500k 29bit", "CAN 250k 29bit"};
#define DN_P_COUNT (sizeof(DN_P_NAME) / sizeof(DN_P_NAME[0]))
inline bool dnIsCan(uint8_t p) { return p == DN_P_CAN500 || p == DN_P_CAN250 || p == DN_P_CAN500X || p == DN_P_CAN250X; }
inline bool dnIsK(uint8_t p) { return p >= DN_P_ISO9141 && p <= DN_P_KWPSLOW; }

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

// ---------------------------------------------------------------- Custom PID ที่ C3 ถามเอง (โชว์ได้ทุกจอ)
// ช่อง 0-3 = CUSTOM 1-4, ช่อง 4 = CVT TEMP · ตั้งจากแอป (C3 จำไว้ในตัว ใช้ได้แม้ไม่เปิดแอป)
#define DN_NCUST 5
// แอป -> C3: ตั้งค่าช่อง
struct __attribute__((packed)) DidiNowCustSet {
  uint32_t magic;
  uint8_t type;       // DN_CUSTSET
  uint8_t slot;       // 0-4
  uint8_t on;         // 0 = ปิดช่องนี้
  uint8_t len;        // จำนวนไบต์คำสั่ง (1-7)
  uint16_t hdr;       // CAN ID ที่ถาม (0 = เครื่องยนต์)
  uint8_t data[7];    // คำสั่ง เช่น 22 xx xx
  uint8_t dec;        // ทศนิยม
  float lo, hi;       // ช่วงแถบ
  char name[12];
  char unit[6];
  char fml[40];       // สูตรแบบ Torque: A B C ... + - * / ( ) {A:7}
};
// C3 -> ทุกจอ: ค่าของทุกช่อง ~4 ครั้ง/วินาที
struct __attribute__((packed)) DidiNowCust {
  uint32_t magic;
  uint8_t type;       // DN_CUST
  uint8_t have;       // บิต i = ช่อง i มีค่าสด
  uint8_t on;         // บิต i = ช่อง i เปิดใช้
  uint8_t pad;
  float v[DN_NCUST];
};
// C3 -> ทุกจอ: ชื่อ/หน่วย/ช่วงของช่อง (วนทีละช่อง)
struct __attribute__((packed)) DidiNowCustInfo {
  uint32_t magic;
  uint8_t type;       // DN_CUSTINFO
  uint8_t slot, on, dec;
  float lo, hi;
  char name[12];
  char unit[6];
};
// C3 -> ทุกจอ: คำตอบดิบล่าสุดของช่อง (ไว้ทดสอบ PID ในแอป)
struct __attribute__((packed)) DidiNowCustRaw {
  uint32_t magic;
  uint8_t type;       // DN_CUSTRAW
  uint8_t slot;
  int8_t n;           // -1 = ไม่มีใครตอบ · -2 = K-line ใช้ header อื่นไม่ได้
  uint8_t data[48];
};

// ---------------------------------------------------------------- Bluetooth (BLE) แทน ESP-NOW
// C3 เป็นตัวปล่อย (server) ให้จอ S3 / CYD / แอปแอนดรอยด์ ต่อพร้อมกันได้ (สูงสุด 3)
//  DATA: แจ้งเตือน (notify) ทุกแพ็กเก็ตข้างบน (ดูชนิดจาก type)  ·  CMD: เขียนคำสั่ง (DidiNowCmd / DidiNowRaw)
#define DIDIBLE_NAME "Didissnap-C3"
#define DIDIBLE_SVC  "d1d15500-6f0b-4e8b-9c4e-0b5d1d150000"
#define DIDIBLE_DATA "d1d15501-6f0b-4e8b-9c4e-0b5d1d150000"
#define DIDIBLE_CMD  "d1d15502-6f0b-4e8b-9c4e-0b5d1d150000"
#define DIDIBLE_MTU  185

// ---------------------------------------------------------------- ปรับค่า (gain / offset) ที่ C3 — ทุกจอได้ค่าที่ปรับแล้วเหมือนกัน
// ค่าที่ส่ง = ค่าจากรถ x g + o   (TPS: ตัดให้อยู่ 0-100 · AFR narrowband (V) ไม่ปรับ · -999 = ไม่รองรับ ไม่ปรับ)
enum { DC_RPM, DC_SPD, DC_ECT, DC_IAT, DC_VOLT, DC_LOAD, DC_TPS, DC_MAP, DC_MAF, DC_AFR, DC_TIMING, DC_OIL, DC_CVT, DN_NCAL };
struct __attribute__((packed)) DidiNowCal {
  uint32_t magic;
  uint8_t type;       // DN_CAL
  uint8_t set;        // 1 = จอ/แอป -> C3 (ตั้งค่า) · 0 = C3 -> ทุกจอ (ค่าปัจจุบัน ส่งทุก 2 วิ)
  uint16_t ver;       // C3 เพิ่มทุกครั้งที่ค่าเปลี่ยน
  float g[DN_NCAL];
  float o[DN_NCAL];
};
inline void dnCalDefault(DidiNowCal& c) { for (int i = 0; i < DN_NCAL; i++) { c.g[i] = 1; c.o[i] = 0; } }

// ---------------------------------------------------------------- อัปเดตเฟิร์มแวร์ C3 ทาง BLE (จากแอป)
// แอป -> C3 (เขียนแบบรอตอบรับทีละแพ็กเก็ต)
enum { DN_OTA_INFO = 0, DN_OTA_BEGIN = 1, DN_OTA_DATA = 2, DN_OTA_END = 3, DN_OTA_ABORT = 4 };
#define DN_OTA_CHUNK 160
struct __attribute__((packed)) DidiNowOta {
  uint32_t magic;
  uint8_t type;       // DN_OTA
  uint8_t op;         // DN_OTA_*
  uint16_t len;       // ไบต์ใน data
  uint32_t off;       // DATA: ตำแหน่งในไฟล์ · BEGIN: ขนาดไฟล์ทั้งหมด
  uint8_t data[DN_OTA_CHUNK];   // DATA: เนื้อไฟล์ · BEGIN: MD5 (hex 32 ตัว)
};
// C3 -> แอป: สถานะ
enum { DN_OTAS_IDLE = 0, DN_OTAS_RECV = 1, DN_OTAS_DONE = 2, DN_OTAS_ERR = 3 };
struct __attribute__((packed)) DidiNowOtaSt {
  uint32_t magic;
  uint8_t type;       // DN_OTAST
  uint8_t state;      // DN_OTAS_*
  uint8_t err;        // รหัสผิดพลาดของ Update (0 = ไม่มี)
  uint8_t pad;
  uint32_t off;       // รับแล้วกี่ไบต์
  uint32_t size;      // ขนาดไฟล์ (IDLE: พื้นที่ที่ลงได้สูงสุด)
  char msg[40];       // IDLE: เวอร์ชันเฟิร์มแวร์ · ERR: สาเหตุ
};

// ---------------------------------------------------------------- จับคู่จอกับ C3 (กันต่อผิดคันเวลาจอดรวมกัน)
// จอ/แอปมี "กุญแจ" (token 8 ไบต์ สุ่มครั้งแรก จำไว้ในตัว) → ต่อ BLE แล้วต้องส่ง DN_AUTH ภายใน 5 วิ
// C3 รู้จักกุญแจ = ส่งค่าให้ · ไม่รู้จัก: ถ้าเปิดโหมดจับคู่อยู่ = จำกุญแจไว้ (จับคู่) · ไม่เปิด = ตัดสาย
// โหมดจับคู่เปิดเอง: ยังไม่เคยจับคู่ใคร · 2 นาทีแรกหลังเสียบ C3 · จอที่จับคู่แล้วสั่งเปิด 3 นาที (เพิ่มจอใหม่) · กด BOOT ค้าง 6 วิ
// โฆษณา BLE: manufacturer data = FF FF 'D' 'N' flags (bit0 = เปิดโหมดจับคู่อยู่) · ชื่อ DIDI-C3-xxxx (ไม่ซ้ำกันทุกตัว)
#define DN_PAIR_MAX 8
#define DN_PROTO 2      // รุ่นโปรโตคอลของจอ (ส่งใน DidiNowAuth.proto) · 0/1 = รุ่นแรกที่มีจับคู่
enum : uint8_t { DN_AUTH_LOGIN = 0, DN_AUTH_OPEN = 1, DN_AUTH_CLEAR = 2, DN_AUTH_FORGET = 3 };
struct __attribute__((packed)) DidiNowAuth {
  uint32_t magic;
  uint8_t type;       // DN_AUTH
  uint8_t op;         // DN_AUTH_*
  uint8_t proto;      // DN_PROTO ของจอ (จอรุ่นแรกส่ง 0)
  uint8_t pad;
  uint8_t token[8];   // กุญแจของจอนี้
  char name[8];       // ชื่อจอ เช่น "APP" "S3" "CYD"
};
enum : uint8_t { DN_AUTHR_DENIED = 0, DN_AUTHR_OK = 1, DN_AUTHR_NEW = 2 };
struct __attribute__((packed)) DidiNowAuthSt {
  uint32_t magic;
  uint8_t type;       // DN_AUTHST
  uint8_t res;        // DN_AUTHR_*
  uint8_t open;       // โหมดจับคู่เหลืออีกกี่วินาที / 2 (0 = ปิด · 255 = เปิดตลอด เพราะยังไม่เคยจับคู่)
  uint8_t n;          // จำนวนจอที่จับคู่แล้ว
  char c3[16];        // ชื่อ C3 เช่น DIDI-C3-A1B2
  char names[DN_PAIR_MAX][8];   // ชื่อจอที่จับคู่แล้ว
  uint8_t proto[DN_PAIR_MAX];   // รุ่นโปรโตคอลของแต่ละจอ (ล่าสุดที่ต่อ · 0 = ยังไม่รู้/รุ่นแรก)
  uint8_t legacy;     // จอรุ่นเก่า (ไม่มีระบบจับคู่) พยายามต่อใน 10 นาทีล่าสุด กี่ครั้ง
  char fw[16];        // เวอร์ชันเฟิร์มแวร์ C3
};

// ---------------------------------------------------------------- C3 ทาง WiFi (จอแอนดรอยด์ที่ไม่มี BLE)
// C3 ต่อฮอตสปอตมือถือ (ตั้งชื่อ/รหัสจากแอปทาง BLE) · จอที่ต่อฮอตสปอตเดียวกันคุยกับ C3 ทาง UDP
//  C3 ประกาศตัว (DN_BEACON) ทุก 1 วิ ไปที่ broadcast:DN_UDP_APP · จอส่งแพ็กเก็ตเดียวกับ BLE ไปที่ C3:DN_UDP_C3
//  C3 ส่งค่ารถกลับไปที่ ip:พอร์ต ที่จอส่งมา · จอต้องส่งอะไรสักอย่างทุก ~1.5 วิ (ไม่งั้น C3 ลืมใน 6 วิ) · จับคู่เหมือน BLE
#define DN_UDP_C3 35100
#define DN_UDP_APP 35101
enum : uint8_t { DN_WIFI_GET = 0, DN_WIFI_SET = 1, DN_WIFI_OFF = 2 };
struct __attribute__((packed)) DidiNowWifi {    // จอ -> C3 (ต้องจับคู่แล้ว)
  uint32_t magic;
  uint8_t type;       // DN_WIFI
  uint8_t op;         // DN_WIFI_*
  uint8_t pad[2];
  char ssid[33];
  char pass[65];
};
struct __attribute__((packed)) DidiNowWifiSt {  // C3 -> จอ: สถานะ WiFi
  uint32_t magic;
  uint8_t type;       // DN_WIFIST
  uint8_t state;      // 0 ปิด/ยังไม่ตั้ง · 1 กำลังต่อ · 2 ต่อแล้ว
  int8_t rssi;
  uint8_t clients;    // จอที่ต่อทาง WiFi อยู่
  uint8_t ip[4];
  char ssid[33];
};
struct __attribute__((packed)) DidiNowBeacon {  // C3 -> broadcast
  uint32_t magic;
  uint8_t type;       // DN_BEACON
  uint8_t open;       // 1 = เปิดโหมดจับคู่
  uint8_t pad[2];
  char mac[18];       // ที่อยู่ BLE ของ C3 (ใช้จับคู่ตัวเดียวกับทาง BLE)
  char name[16];      // DIDI-C3-xxxx
};
