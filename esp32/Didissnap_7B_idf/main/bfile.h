// bfile.h — รับไฟล์ (รูป / GIF / สกิน) จากแอปทาง Bluetooth BLE → เก็บลง SD card โฟลเดอร์ skins
//  จอเป็น "อุปกรณ์ต่อพ่วง" ให้แอปต่อเข้ามาตรง ๆ (พร้อมกับที่ยังต่อ Superlink อยู่)
//  ปลอดภัย: เปิดรับเฉพาะตอนกด "รับไฟล์จากแอป" ในเมนู (3 นาที) — นอกนั้นจอไม่ประกาศตัว ไม่รับการเชื่อมต่อ
//
//  โปรโตคอล (เขียนแบบรอตอบรับ ลงคาแรกเตอร์ FILE d1d15701-…):
//    01 size:u32 name…   เริ่มไฟล์ (ชื่อ a-z 0-9 _ - . ยาวไม่เกิน 32 · .jpg .png .gif .json)
//    02 off:u32 data…    ข้อมูลต่อจากเดิม (off ต้องตรงกับที่รับแล้ว)
//    03 crc32:u32        จบไฟล์: ตรวจขนาด + CRC32 แล้วแทนที่ไฟล์เดิม
//    04 name…            ลบไฟล์
//  ผิดพลาด = ตอบ ATT error: 0x80 ชื่อ/ขนาดผิด · 0x81 ตำแหน่งข้อมูลไม่ต่อกัน · 0x82 เขียน SD ไม่ได้ · 0x83 CRC ไม่ตรง · 0x84 ยังไม่ได้เริ่มไฟล์
#pragma once
#include <stddef.h>

namespace bfile {

void init();                 // เรียกหลัง nimble_port_init() ก่อนเริ่มงาน host
void onSync();               // เรียกจาก sync_cb ของ NimBLE
void openWindow(int sec);    // เปิดรับ (ประกาศตัว) sec วินาที
void closeWindow();
int windowLeft();            // วินาทีที่เหลือ (0 = ปิด)
bool connected();            // แอปต่ออยู่
int progress();              // 0-100 ระหว่างรับ · -1 ไม่ได้รับ
const char* current();       // ชื่อไฟล์ที่กำลังรับ / รับล่าสุด
bool takeDone(char* name, size_t n);   // มีไฟล์รับครบใหม่ (ครั้งเดียว)

}  // namespace bfile
