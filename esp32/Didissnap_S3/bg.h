// bg.h — รูปพื้นหลังของหน้า 9-12: ถอด JPEG (ตัวถอดใน ROM ของ ESP32-S3) / เล่น GIF (ไลบรารี AnimatedGIF)
// ทำงานเป็นงานแยกบนคอร์ 0 จอเลยไม่สะดุดตอนกำลังถอดรูป
#pragma once
#ifdef BG_HOST   // ใช้ตอนทดสอบวาดจอบนคอมเท่านั้น
#include "gfx.h"
namespace bg {
enum Kind { NONE = 0, BI_JPG9, BI_RUN_ANIM, BI_JPG11, FILE_PIC };
static float runSpeed = 0;
static uint16_t* hostImg = nullptr;
inline bool draw() { if (hostImg) { memcpy(gfx::G, hostImg, 480 * 272 * 2); return true; } gfx::clear(0); return false; }
}
#else
#include <Arduino.h>
#include <LittleFS.h>
#include <AnimatedGIF.h>
#include "esp32s3/rom/tjpgd.h"
#include "freertos/semphr.h"
#include "gfx.h"
#include "bimg.h"

namespace bg {

static const int W = 480, H = 272, NPX = W * H;
enum Kind { NONE = 0, BI_JPG9, BI_RUN_ANIM, BI_JPG11, FILE_PIC };

struct Want {
  int kind = NONE;
  char path[40] = "";
  uint32_t ver = 0;          // didipics::version ตอนเลือก (รูปชื่อเดิมแต่ไฟล์ใหม่)
};
static Want want, have;
static float runSpeed = 0;   // ความเร็วรถ (ใช้กับรถวิ่งหน้า 10)

static uint16_t* buf[2] = {nullptr, nullptr};
static volatile int shown = -1;   // บัฟเฟอร์ที่พร้อมแสดง (-1 = ยังไม่มี)
static SemaphoreHandle_t lock;
static uint8_t* fileBuf = nullptr;
static size_t fileLen = 0, fileCap = 0;
static uint16_t* gifCanvas = nullptr;
static AnimatedGIF* gif = nullptr;
static char lastErr[48] = "";

// ---------------------------------------------------------------- JPEG
struct JCtx { const uint8_t* src; size_t len, pos; uint16_t* dst; int ox, oy; };
static uint8_t jwork[4096];
static UINT jin(JDEC* jd, BYTE* b, UINT n) {
  JCtx* c = (JCtx*)jd->device;
  if (c->pos + n > c->len) n = c->len - c->pos;
  if (b) memcpy(b, c->src + c->pos, n);
  c->pos += n;
  return n;
}
static UINT jout(JDEC* jd, void* bmp, JRECT* r) {
  JCtx* c = (JCtx*)jd->device;
  const uint8_t* p = (const uint8_t*)bmp;
  for (int y = r->top; y <= r->bottom; y++) {
    int Y = y + c->oy;
    for (int x = r->left; x <= r->right; x++, p += 3) {
      int X = x + c->ox;
      if ((unsigned)X < (unsigned)W && (unsigned)Y < (unsigned)H)
        c->dst[Y * W + X] = ((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3);
    }
  }
  return 1;
}
// ถอดให้เต็มจอ: รูปใหญ่กว่าจอ = ย่อ (1/2,1/4,1/8) แล้วตัดกลาง, รูปเล็กกว่า = วางกลางพื้นดำ
inline bool decodeJpeg(const uint8_t* src, size_t len, uint16_t* dst) {
  JCtx c{src, len, 0, dst, 0, 0};
  JDEC jd;
  JRESULT r = jd_prepare(&jd, jin, jwork, sizeof(jwork), &c);
  if (r != JDR_OK) { snprintf(lastErr, sizeof(lastErr), "jpeg prepare %d", (int)r); return false; }
  int s = 0;
  while (s < 3 && (int)(jd.width >> (s + 1)) >= W && (int)(jd.height >> (s + 1)) >= H) s++;
  int w = jd.width >> s, h = jd.height >> s;
  c.ox = (W - w) / 2;
  c.oy = (H - h) / 2;
  if (w < W || h < H) memset(dst, 0, NPX * 2);
  r = jd_decomp(&jd, jout, s);
  if (r != JDR_OK) { snprintf(lastErr, sizeof(lastErr), "jpeg decode %d", (int)r); return false; }
  return true;
}

// ---------------------------------------------------------------- GIF
static int gifOx = 0, gifOy = 0, gifPrevDisp = 0, gifPx = 0, gifPy = 0, gifPw = 0, gifPh = 0;
static void gifDraw(GIFDRAW* d) {
  int y = d->iY + d->y + gifOy;
  if ((unsigned)y >= (unsigned)H) return;
  uint16_t* row = gifCanvas + y * W;
  const uint8_t* s = d->pPixels;
  const uint16_t* pal = d->pPalette;
  for (int x = 0; x < d->iWidth; x++) {
    int X = d->iX + x + gifOx;
    uint8_t v = s[x];
    if ((unsigned)X >= (unsigned)W) continue;
    if (d->ucHasTransparency && v == d->ucTransparent) continue;
    row[X] = pal[v];
  }
  if (d->y == 0) { gifPrevDisp = d->ucDisposalMethod; gifPx = d->iX + gifOx; gifPy = d->iY + gifOy; gifPw = d->iWidth; gifPh = d->iHeight; }
}

// ---------------------------------------------------------------- ส่งบัฟเฟอร์ที่ถอดเสร็จขึ้นแสดง
static int backIdx() { return shown == 0 ? 1 : 0; }
static void publish(int idx) {
  xSemaphoreTake(lock, portMAX_DELAY);
  shown = idx;
  xSemaphoreGive(lock);
}

static bool loadFile(const char* path) {
  File f = LittleFS.open(path, "r");
  if (!f) { snprintf(lastErr, sizeof(lastErr), "open %s", path); return false; }
  size_t n = f.size();
  if (n > fileCap) {
    if (fileBuf) free(fileBuf);
    fileCap = n + 4096;
    fileBuf = (uint8_t*)ps_malloc(fileCap);
    if (!fileBuf) { fileCap = 0; f.close(); snprintf(lastErr, sizeof(lastErr), "no psram"); return false; }
  }
  fileLen = f.read(fileBuf, n);
  f.close();
  return fileLen == n;
}

static void task(void*) {
  uint32_t nextT = 0;
  int runIdx = 0;
  bool gifOpen = false;
  for (;;) {
    // เปลี่ยนรูป?
    if (want.kind != have.kind || strcmp(want.path, have.path) != 0 || want.ver != have.ver) {
      have = want;
      if (gifOpen) { gif->close(); gifOpen = false; }
      publish(-1);
      lastErr[0] = 0;
      int b = backIdx();
      if (have.kind == BI_JPG9) { if (decodeJpeg(BI_PIC9, sizeof(BI_PIC9), buf[b])) publish(b); }
      else if (have.kind == BI_JPG11) { if (decodeJpeg(BI_PIC11, sizeof(BI_PIC11), buf[b])) publish(b); }
      else if (have.kind == BI_RUN_ANIM) { runIdx = 0; nextT = 0; }
      else if (have.kind == FILE_PIC && loadFile(have.path)) {
        bool isGif = fileLen > 3 && fileBuf[0] == 'G' && fileBuf[1] == 'I' && fileBuf[2] == 'F';
        if (!isGif) { if (decodeJpeg(fileBuf, fileLen, buf[b])) publish(b); }
        else if (gif->open(fileBuf, fileLen, gifDraw)) {
          gifOpen = true;
          gifOx = (W - gif->getCanvasWidth()) / 2;
          gifOy = (H - gif->getCanvasHeight()) / 2;
          memset(gifCanvas, 0, NPX * 2);
          gifPrevDisp = 0;
          nextT = 0;
        } else snprintf(lastErr, sizeof(lastErr), "gif open");
      }
    }
    uint32_t now = millis();
    if (have.kind == BI_RUN_ANIM && now >= nextT) {
      float sp = runSpeed;
      if (sp > 1 || shown < 0) {
        int b = backIdx();
        if (decodeJpeg(BI_RUN[runIdx], BI_RUN_LEN[runIdx], buf[b])) publish(b);
        runIdx = (runIdx + 1) % BI_RUN_N;
      }
      nextT = now + (uint32_t)constrain(6000.f / fmaxf(sp, 1.f), 40.f, 400.f);   // เร็วขึ้นตามความเร็วรถ
    }
    if (gifOpen && now >= nextT) {
      if (gifPrevDisp == 2) {   // คืนพื้นหลังตรงเฟรมก่อน
        for (int y = max(0, gifPy); y < min(H, gifPy + gifPh); y++)
          for (int x = max(0, gifPx); x < min(W, gifPx + gifPw); x++) gifCanvas[y * W + x] = 0;
      }
      int delayMs = 100;
      int r = gif->playFrame(false, &delayMs);
      int b = backIdx();
      memcpy(buf[b], gifCanvas, NPX * 2);
      publish(b);
      if (r == 0) gif->reset();           // จบรอบ → วนใหม่
      if (r < 0) { snprintf(lastErr, sizeof(lastErr), "gif frame"); gifOpen = false; }
      nextT = now + constrain(delayMs, 20, 2000);
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

inline bool begin() {
  lock = xSemaphoreCreateMutex();
  buf[0] = (uint16_t*)ps_malloc(NPX * 2);
  buf[1] = (uint16_t*)ps_malloc(NPX * 2);
  gifCanvas = (uint16_t*)ps_malloc(NPX * 2);
  gif = new AnimatedGIF();
  if (!buf[0] || !buf[1] || !gifCanvas || !gif) return false;
  gif->begin(GIF_PALETTE_RGB565_LE);
  xTaskCreatePinnedToCore(task, "bg", 8192, nullptr, 2, nullptr, 0);
  return true;
}

// main เรียกทุกเฟรม: บอกว่าตอนนี้อยากได้รูปอะไร
inline void select(int kind, const String& path = String(""), uint32_t ver = 0) {
  if (want.kind == kind && path == want.path && want.ver == ver) return;
  Want w;
  w.kind = kind;
  strlcpy(w.path, path.c_str(), sizeof(w.path));
  w.ver = ver;
  want = w;
}

// วาดรูปพื้นหลังลงเฟรม (ถ้ายังไม่พร้อม = พื้นดำ, คืน false)
inline bool draw() {
  bool ok = false;
  if (!lock) { gfx::clear(0); return false; }   // ยังไม่ได้เริ่มงานรูป (safe mode)
  xSemaphoreTake(lock, portMAX_DELAY);
  if (shown >= 0 && want.kind != NONE) { memcpy(gfx::G, buf[shown], NPX * 2); ok = true; }
  xSemaphoreGive(lock);
  if (!ok) gfx::clear(0);
  return ok;
}

}  // namespace bg
#endif  // BG_HOST
