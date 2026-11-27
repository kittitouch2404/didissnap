// custeval.h — คิดสูตรแบบ Torque (A B C ... = ไบต์ข้อมูล, + - * / ( ), {A:7} = บิต,
//   SIGNED(x) = มีเครื่องหมาย 8 บิต, S16(x) = มีเครื่องหมาย 16 บิต) บน C3
#pragma once
#include <Arduino.h>

namespace custeval {
struct P {
  const char* s;
  const uint8_t* b;
  int n;
  bool err;
  char c() { while (*s == ' ') s++; return *s; }
};
static float expr(P& p);
static float atom(P& p) {
  char c = p.c();
  if (!c) { p.err = true; return 0; }
  if (c == '(') { p.s++; float v = expr(p); if (p.c() != ')') p.err = true; else p.s++; return v; }
  if (c == '{') {                                        // {A:7}
    p.s++;
    char L = toupper(p.c()); p.s++;
    if (p.c() != ':') { p.err = true; return 0; }
    p.s++;
    int bit = strtol(p.s, (char**)&p.s, 10);
    if (p.c() != '}') { p.err = true; return 0; }
    p.s++;
    int k = L - 'A';
    if (k < 0 || k >= p.n || bit < 0 || bit > 7) { p.err = true; return 0; }
    return (p.b[k] >> bit) & 1;
  }
  if (isalpha((unsigned char)c) && isalpha((unsigned char)p.s[1])) {   // ฟังก์ชัน: SIGNED( ) / S16( )
    char name[8] = {0};
    int L = 0;
    while (isalnum((unsigned char)*p.s) && L < 7) name[L++] = toupper(*p.s++);
    if (p.c() != '(') {
      // ไบต์ที่ 27 ขึ้นไปแบบ Torque: AA = ไบต์ 26, AB = 27 ... AZ = 51
      if (L == 2 && name[0] == 'A' && name[1] >= 'A' && name[1] <= 'Z') {
        int k = 26 + (name[1] - 'A');
        if (k >= p.n) { p.err = true; return 0; }
        return p.b[k];
      }
      p.err = true;
      return 0;
    }
    p.s++;
    float v = expr(p);
    if (p.c() != ')') { p.err = true; return 0; }
    p.s++;
    if (!strcmp(name, "SIGNED")) return v >= 128 ? v - 256 : v;
    if (!strcmp(name, "S16")) return v >= 32768 ? v - 65536 : v;
    p.err = true;
    return 0;
  }
  if (isalpha((unsigned char)c)) {
    int k = toupper(c) - 'A';
    p.s++;
    if (k < 0 || k >= p.n) { p.err = true; return 0; }
    return p.b[k];
  }
  if (isdigit((unsigned char)c) || c == '.') return strtof(p.s, (char**)&p.s);
  p.err = true;
  return 0;
}
static float unary(P& p) {
  char c = p.c();
  if (c == '-') { p.s++; return -unary(p); }
  if (c == '+') { p.s++; return unary(p); }
  return atom(p);
}
static float term(P& p) {
  float v = unary(p);
  for (;;) {
    char c = p.c();
    if (c == '*') { p.s++; v *= unary(p); }
    else if (c == '/') { p.s++; float d = unary(p); v = d != 0 ? v / d : NAN; }
    else return v;
  }
}
static float expr(P& p) {
  float v = term(p);
  for (;;) {
    char c = p.c();
    if (c == '+') { p.s++; v += term(p); }
    else if (c == '-') { p.s++; v -= term(p); }
    else return v;
  }
}
// คืน true + ค่าใน *out ถ้าสูตรถูกและไบต์พอ
inline bool eval(const char* f, const uint8_t* b, int n, float* out) {
  P p{f, b, n, false};
  float v = expr(p);
  if (p.err || p.c() != 0 || isnan(v) || isinf(v)) return false;
  *out = v;
  return true;
}
}  // namespace custeval
