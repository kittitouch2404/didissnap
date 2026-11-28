// skin.h — หน้าจอแบบ "สกิน" (ไฟล์ JSON): หน้าไหนมีเกจ/ช่องค่าอะไร ตรงไหน สีอะไร — ไม่ต้องแก้โค้ด
//  ไฟล์สกินอยู่ใน SD: /sdcard/skins/*.json (ไม่มีการ์ด = ใช้สกินในตัว) · รูปพื้นหลังอ้างเป็น "S:skins/ชื่อไฟล์.jpg"
#pragma once
#include "lvgl.h"
#include "vdata.h"

namespace skin {

extern const char* const BUILTIN;   // สกินในตัว (JSON)

// สร้างทุกหน้าใน tileview (ลบของเดิมก่อน) · คืนจำนวนหน้า (0 = JSON ผิด)
int build(lv_obj_t* tileview, const char* json, char* err, size_t errLen, bool english = false);
// อัปเดตค่าของหน้าที่แสดงอยู่ (เรียกทุกเฟรมจากงาน LVGL)
void update(int page, const vd::Snap& s);
// หน้าที่แสดงอยู่: วาดส่วนที่ไม่ขยับ (พื้น สเกล ป้าย กรอบ) เป็นรูปเดียวไว้ → ทุกเฟรมวาดใหม่แค่ค่า/เข็ม (เร็วหลายเท่า)
// หน้าอื่นคืนเป็นวิดเจ็ตปกติ (ประหยัด PSRAM: รูปละ 1.2 MB)
void setActive(int page);
// ซ่อนหน้า: รายชื่อแบบ "|ECU|OBD2|" (ใช้ตอน build ครั้งถัดไป)
void setHidden(const char* list);
const char* hiddenList();
int allCount();                 // ทุกหน้าในสกิน (รวมที่ซ่อน)
const char* allName(int i);
bool isHidden(int i);
void setHiddenAt(int i, bool hide);
int pageCount();
const char* pageName(int i);
lv_color_t accent();

}  // namespace skin
