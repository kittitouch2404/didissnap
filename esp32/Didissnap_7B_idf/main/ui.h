// ui.h — หน้าจอหลัก: หน้าปัดจากสกิน (ปัดซ้าย/ขวา) + แถบสถานะ + หน้าตั้งค่า
#pragma once

namespace ui {
void build();   // เรียกในล็อก LVGL ครั้งเดียว
int bri();      // ความสว่างที่ตั้งไว้ (%)
void previewMenu(bool english);   // (ใช้ทำภาพตัวอย่าง)
void previewGear();
}
