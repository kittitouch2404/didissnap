// DidissnapPics.h — เก็บรูปที่ส่งมาจากแอป/หน้าเว็บ ไว้ในแฟลชของบอร์ด (LittleFS)
//
//  - หน้า 9, 10, 11 เลือกได้ว่าจะใช้รูปไหน (slot 0..2)  ไม่เลือก = ใช้รูปเดิมที่ฝังในโค้ด
//  - หน้า "รูปตามรอบ" (Pic by rpm) สลับรูปตามรอบเครื่อง: แบ่งเท่า ๆ กันถึง redline หรือกำหนดรอบเองทีละรูป
//
//  API (ต่อจาก DidissnapConfig.h บนพอร์ต 80 ตัวเดียวกัน)
//   GET  /api/pics                         รายการรูป + พื้นที่ + การใช้งาน
//   POST /api/pic_up?name=p123.jpg         อัปโหลด (multipart, ไฟล์ JPG หรือ GIF)
//   GET  /api/pic_del?name=..              ลบรูป
//   GET  /api/pic_slot?slot=0&name=..      ตั้งรูปของหน้า 9/10/11 (name ว่าง = รูปเดิม)
//   GET  /api/pic_rpm?name=..&on=1&rpm=..  เปิด/ปิด และรอบเริ่มของรูปในหน้ารูปตามรอบ
//   GET  /api/pic_auto?v=1                 1 = แบ่งช่วงรอบอัตโนมัติ
//   GET  /api/pic_move?name=..&dir=-1      เลื่อนลำดับรูป
//   GET  /pics/<name>                      ตัวไฟล์ (แอปใช้ทำรูปย่อ)
//
//  Partition: ใช้ partitions.csv ในโฟลเดอร์สเก็ตช์ (แอป 4MB + พื้นที่รูป ~11.9MB)
#pragma once
#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include "DidissnapConfig.h"

namespace didipics {

static const int SLOTS = 3;                 // หน้า 9, 10, 11
static const int MAX_PICS = 40;
#ifndef DIDI_MAX_FILE
#define DIDI_MAX_FILE (1536 * 1024)
#endif
static const size_t MAX_FILE = DIDI_MAX_FILE; // ต่อไฟล์ (GIF ยาว ๆ) — บอร์ดที่มี PSRAM เยอะตั้งให้ใหญ่ขึ้นได้
static const size_t KEEP_FREE = 24 * 1024;  // เผื่อพื้นที่ให้ LittleFS
static const char* PIC_DIR = "/pics";
static const char* META = "/pics/_meta.txt";
static const char* TMP = "/pics/_up.tmp";

struct Pic { String name; int on = 1; int rpm = 0; };
static Pic list[MAX_PICS];
static int count = 0;
static String slot[SLOTS];
static int autoSplit = 1;
static uint32_t version = 0;               // เปลี่ยนทุกครั้งที่มีการแก้ → ตัววาดจอโหลดรูปใหม่
static void (*onChange)() = nullptr;
static bool fsOk = false;

// ---------------------------------------------------------------- ส่วนที่ไม่ยุ่งกับไฟล์ (ทดสอบบนคอมได้)
// ชื่อไฟล์: a-z 0-9 _ - ยาวไม่เกิน 24 ตัว และลงท้าย .jpg หรือ .gif
inline bool validName(const String& n) {
  int L = n.length();
  if (L < 5 || L > 28) return false;
  String ext = n.substring(L - 4);
  if (!(ext == ".jpg" || ext == ".gif")) return false;
  for (int i = 0; i < L - 4; i++) {
    char c = n[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
  }
  return true;
}
inline bool isGif(const String& n) { return n.length() > 4 && n.substring(n.length() - 4) == ".gif"; }
inline int find(const String& n) {
  for (int i = 0; i < count; i++) if (list[i].name == n) return i;
  return -1;
}

inline String metaText() {
  String s;
  for (int i = 0; i < SLOTS; i++) { s += "s"; s += i; s += "="; s += slot[i]; s += "\n"; }
  s += "auto="; s += autoSplit; s += "\n";
  for (int i = 0; i < count; i++) { s += "r="; s += list[i].name; s += ","; s += list[i].on; s += ","; s += list[i].rpm; s += "\n"; }
  return s;
}

inline void parseMeta(const String& t) {
  count = 0;
  for (int i = 0; i < SLOTS; i++) slot[i] = "";
  autoSplit = 1;
  int p = 0, L = t.length();
  while (p < L) {
    int e = t.indexOf('\n', p);
    if (e < 0) e = L;
    String ln = t.substring(p, e);
    p = e + 1;
    if (ln.length() >= 3 && ln[0] == 's' && ln[2] == '=' && ln[1] >= '0' && ln[1] < '0' + SLOTS) {
      String n = ln.substring(3);
      slot[ln[1] - '0'] = validName(n) ? n : String("");
    } else if (ln.length() > 5 && ln.substring(0, 5) == "auto=") {
      autoSplit = ln.substring(5).toInt() ? 1 : 0;
    } else if (ln.length() > 2 && ln.substring(0, 2) == "r=" && count < MAX_PICS) {
      int a = ln.indexOf(',', 2), b = a < 0 ? -1 : ln.indexOf(',', a + 1);
      if (a < 0 || b < 0) continue;
      String n = ln.substring(2, a);
      if (!validName(n) || find(n) >= 0) continue;
      list[count].name = n;
      list[count].on = ln.substring(a + 1, b).toInt() ? 1 : 0;
      list[count].rpm = constrain((int)ln.substring(b + 1).toInt(), 0, 20000);
      count++;
    }
  }
}

// รูปของหน้ารูปตามรอบ ณ รอบนี้ (ว่าง = ยังไม่มีรูปที่เปิดไว้)
inline String forRpm(float rpm, int redline) {
  int on[MAX_PICS], n = 0;
  for (int i = 0; i < count; i++) if (list[i].on) on[n++] = i;
  if (n == 0) return String("");
  if (autoSplit) {
    int k = (int)(rpm * n / (float)(redline > 0 ? redline : 7000));
    return list[on[constrain(k, 0, n - 1)]].name;
  }
  int best = on[0], bestRpm = -1;     // รูปที่ "รอบเริ่ม" สูงสุดที่ยังไม่เกินรอบตอนนี้
  for (int j = 0; j < n; j++) {
    int r = list[on[j]].rpm;
    if (r <= rpm && r > bestRpm) { bestRpm = r; best = on[j]; }
  }
  if (bestRpm < 0) {                   // ยังไม่ถึงรอบเริ่มของรูปใดเลย → ใช้รูปที่รอบเริ่มต่ำสุด
    for (int j = 0; j < n; j++) if (list[on[j]].rpm < list[best].rpm) best = on[j];
  }
  return list[best].name;
}

inline void addName(const String& n) {
  if (find(n) >= 0 || count >= MAX_PICS) return;
  list[count].name = n; list[count].on = 1; list[count].rpm = 0;
  count++;
}
inline void removeName(const String& n) {
  int i = find(n);
  if (i >= 0) { for (int j = i; j < count - 1; j++) list[j] = list[j + 1]; count--; }
  for (int s = 0; s < SLOTS; s++) if (slot[s] == n) slot[s] = "";
}
inline void move(const String& n, int dir) {
  int i = find(n), j = i + (dir < 0 ? -1 : 1);
  if (i < 0 || j < 0 || j >= count) return;
  Pic t = list[i]; list[i] = list[j]; list[j] = t;
}

// ---------------------------------------------------------------- ไฟล์
inline String path(const String& n) { return String(PIC_DIR) + "/" + n; }
inline String slotPath(int s) { return (s >= 0 && s < SLOTS && slot[s].length()) ? path(slot[s]) : String(""); }

inline void save() {
  if (!fsOk) return;
  File f = LittleFS.open(META, "w");
  if (f) { String t = metaText(); f.print(t); f.close(); }
}
inline void changed() { save(); version++; if (onChange) onChange(); }

inline void load() {
  String t;
  File f = LittleFS.open(META, "r");
  if (f) { t = f.readString(); f.close(); }
  parseMeta(t);
  // ตัดชื่อที่ไม่มีไฟล์จริงทิ้ง แล้วเพิ่มไฟล์ที่มีแต่ไม่อยู่ในรายการ
  for (int i = count - 1; i >= 0; i--) if (!LittleFS.exists(path(list[i].name))) removeName(list[i].name);
  for (int s = 0; s < SLOTS; s++) if (slot[s].length() && !LittleFS.exists(path(slot[s]))) slot[s] = "";
  File d = LittleFS.open(PIC_DIR);
  if (d && d.isDirectory()) {
    for (File e = d.openNextFile(); e; e = d.openNextFile()) {
      String n = e.name();
      int sl = n.lastIndexOf('/');
      if (sl >= 0) n = n.substring(sl + 1);
      if (validName(n)) addName(n);
    }
  }
}

inline String json() {
  String s;
  s.reserve(160 + count * 64);
  s += "{\"ok\":true,\"total\":"; s += (unsigned long)(fsOk ? LittleFS.totalBytes() : 0);
  s += ",\"used\":"; s += (unsigned long)(fsOk ? LittleFS.usedBytes() : 0);
  s += ",\"max\":"; s += MAX_PICS;
  s += ",\"auto\":"; s += autoSplit;
  s += ",\"slots\":[";
  for (int i = 0; i < SLOTS; i++) { if (i) s += ","; s += "\""; s += slot[i]; s += "\""; }
  s += "],\"pics\":[";
  for (int i = 0; i < count; i++) {
    File f = LittleFS.open(path(list[i].name), "r");
    unsigned long sz = f ? (unsigned long)f.size() : 0;
    if (f) f.close();
    if (i) s += ",";
    s += "{\"n\":\""; s += list[i].name; s += "\",\"s\":"; s += sz;
    s += ",\"on\":"; s += list[i].on; s += ",\"rpm\":"; s += list[i].rpm; s += "}";
  }
  s += "]}";
  return s;
}

inline void reply() { didicfg::web.send(200, "application/json", json()); }
inline void fail(const char* why) {
  didicfg::web.send(400, "application/json", String("{\"ok\":false,\"err\":\"") + why + "\"}");
}

// ---------------------------------------------------------------- อัปโหลด (มาทีละก้อน ไม่ต้องใช้แรมเยอะ)
static File upF;
static String upName, upErr;
static size_t upSize = 0;
static uint8_t upHead[4];

inline void onUpload() {
  HTTPUpload& u = didicfg::web.upload();
  if (u.status == UPLOAD_FILE_START) {
    upName = didicfg::web.arg("name");
    upErr = "";
    upSize = 0;
    if (!fsOk) upErr = "nofs";
    else if (!validName(upName)) upErr = "badname";
    else if (find(upName) < 0 && count >= MAX_PICS) upErr = "full";
    else {
      LittleFS.remove(TMP);
      upF = LittleFS.open(TMP, "w");
      if (!upF) upErr = "open";
    }
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (upErr.length()) return;
    if (upSize < 4) for (size_t i = 0; i < u.currentSize && upSize + i < 4; i++) upHead[upSize + i] = u.buf[i];
    size_t freeB = LittleFS.totalBytes() - LittleFS.usedBytes();
    if (upSize + u.currentSize > MAX_FILE) upErr = "toobig";
    else if (freeB < u.currentSize + KEEP_FREE) upErr = "nospace";
    else if (upF.write(u.buf, u.currentSize) != u.currentSize) upErr = "write";
    upSize += u.currentSize;
  } else if (u.status == UPLOAD_FILE_END) {
    if (upF) upF.close();
    if (!upErr.length()) {
      bool jpg = upSize >= 4 && upHead[0] == 0xFF && upHead[1] == 0xD8;
      bool gif = upSize >= 4 && upHead[0] == 'G' && upHead[1] == 'I' && upHead[2] == 'F';
      if (isGif(upName) ? !gif : !jpg) upErr = "badfile";
    }
    if (upErr.length()) { LittleFS.remove(TMP); return; }
    LittleFS.remove(path(upName));
    if (!LittleFS.rename(TMP, path(upName))) { upErr = "rename"; LittleFS.remove(TMP); return; }
    addName(upName);
    changed();
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    if (upF) upF.close();
    LittleFS.remove(TMP);
    upErr = "aborted";
  }
}
inline void onUploadDone() {
  if (upErr.length()) fail(upErr.c_str());
  else reply();
}

inline void setupWeb() {
  WebServer& w = didicfg::web;
  w.on("/api/pics", HTTP_GET, []() { reply(); });
  w.on("/api/pic_up", HTTP_POST, onUploadDone, onUpload);
  w.on("/api/pic_del", HTTP_GET, []() {
    String n = didicfg::web.arg("name");
    if (!validName(n)) return fail("badname");
    LittleFS.remove(path(n));
    removeName(n);
    changed();
    reply();
  });
  w.on("/api/pic_slot", HTTP_GET, []() {
    int s = didicfg::web.arg("slot").toInt();
    String n = didicfg::web.arg("name");
    if (s < 0 || s >= SLOTS) return fail("badslot");
    if (n.length() && find(n) < 0) return fail("nofile");
    slot[s] = n;
    changed();
    reply();
  });
  w.on("/api/pic_rpm", HTTP_GET, []() {
    int i = find(didicfg::web.arg("name"));
    if (i < 0) return fail("nofile");
    if (didicfg::web.hasArg("on")) list[i].on = didicfg::web.arg("on").toInt() ? 1 : 0;
    if (didicfg::web.hasArg("rpm")) list[i].rpm = constrain((int)didicfg::web.arg("rpm").toInt(), 0, 20000);
    changed();
    reply();
  });
  w.on("/api/pic_auto", HTTP_GET, []() {
    autoSplit = didicfg::web.arg("v").toInt() ? 1 : 0;
    changed();
    reply();
  });
  w.on("/api/pic_move", HTTP_GET, []() {
    move(didicfg::web.arg("name"), didicfg::web.arg("dir").toInt());
    changed();
    reply();
  });
  w.serveStatic("/pics/", LittleFS, "/pics/", "max-age=600");
}

// เรียกใน setup() หลัง didicfg::begin()
inline bool begin() {
  fsOk = LittleFS.begin(true);   // true = ฟอร์แมตให้ถ้ายังไม่เคยใช้
  if (fsOk) {
    if (!LittleFS.exists(PIC_DIR)) LittleFS.mkdir(PIC_DIR);
    LittleFS.remove(TMP);
    load();
  }
  setupWeb();
  return fsOk;
}

}  // namespace didipics
