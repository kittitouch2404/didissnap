// link.h — ต่อ ESP32-Superlink (C3 / ESP32) ทาง Bluetooth BLE + ระบบจับคู่ (โปรโตคอลเดียวกับจอ S3 / CYD / แอป)
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace slink {

enum State { OFF, SEARCHING, CONNECTING, AUTH, READY };
// สถานะสำหรับข้อความ (หน้าจอแปลเป็นไทย/อังกฤษเอง)
enum Msg { M_START, M_BT_FAIL, M_SEARCH_PAIRED, M_SEARCH_OPEN, M_NONE_OPEN, M_CONNECTING, M_AUTH, M_DENIED, M_READY_NEW, M_READY, M_LOST };

void begin();                     // เริ่ม Bluetooth + งานต่ออัตโนมัติ
State state();
Msg msg();                        // สถานะล่าสุด
const char* c3Name();             // ชื่อ Superlink ที่จับคู่ ("" = ยังไม่จับคู่)
int screens();                    // จำนวนจอที่ Superlink จับคู่ไว้
const char* c3Fw();
bool send(const void* p, size_t n);   // ส่งคำสั่งให้ Superlink (ต้องต่ออยู่)
void forget();                    // ลืมตัวที่จับคู่ → หาตัวที่เปิดโหมดจับคู่ใหม่
void openPairing();               // ให้ Superlink เปิดรับจอใหม่ 3 นาที

}  // namespace slink
