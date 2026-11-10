// lcd.h — จอ RGB 480x272 ของ Panlee ZX4D30NE01S (ใช้ esp_lcd ที่มากับ ESP32 core 3.x)
//
// โหมดส่งภาพต่อเนื่อง (stream) + บัฟเฟอร์จอชุดเดียว + บัฟเฟอร์หลังของเราเอง คัดลอกขึ้นจอตอน vsync
//  - แบบนี้ภาพนิ่งสุด (โหมด refresh_on_demand เริ่มเฟรมใหม่ทุกครั้งแล้วภาพเลื่อนไปมา → สั่น/ซ้อน)
//  - ปัญหาเดิมของโหมดนี้: ตอนชิปอ่าน/เขียนแฟลช (LittleFS, Preferences, WiFi) cache/PSRAM ปิดชั่วคราว
//    แต่ DMA ของจอยังดึงภาพจาก PSRAM และ interrupt ของจอยังสั่งเริ่ม DMA ใหม่ → ค้าง → INT WDT รีเซ็ต
//  - แก้: ดักทุกครั้งที่ชิปจะใช้แฟลช (hook start/end ของไดรเวอร์แฟลช) → หยุดจอ+DMA ก่อน
//    ใช้แฟลชเสร็จ → เริ่มส่งภาพใหม่จากบรรทัดบนสุดทันที (จอแค่ค้างภาพเดิมไม่กี่ ms)
#pragma once
#include <Arduino.h>
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "esp_cache.h"
#include "esp_flash.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "rom/ets_sys.h"
#include "soc/gdma_channel.h"
#include "hal/gdma_ll.h"
#include "hal/lcd_ll.h"
#include "esp32s3/rom/cache.h"

#define LCD_W 480
#define LCD_H 272
#define PIN_BL 45
// ความเร็วส่งภาพ: ผู้ผลิตใช้ 14 MHz แต่ตอนวาดทั้งจอ PSRAM ไม่พอ → ภาพเลื่อน/ซ้อน
// 10 MHz = จอรีเฟรช ~44 ครั้ง/วินาที
#ifndef LCD_PCLK_HZ
#define LCD_PCLK_HZ 10000000
#endif

namespace lcd {

static const size_t FB_BYTES = LCD_W * LCD_H * 2;
static const uint32_t DOTS_PER_FRAME = (10 + 80 + LCD_W + 60) * (10 + 20 + LCD_H + 60);   // รวมช่วงว่าง
static esp_lcd_panel_handle_t panel = nullptr;
static uint16_t* fb = nullptr;               // บัฟเฟอร์ที่จออ่าน (PSRAM)
static uint16_t* bb[3] = {nullptr, nullptr, nullptr}; // บัฟเฟอร์หลัง (โหมดปกติใช้ 2 ชุด, โหมด bounce ใช้ 3 ชุดแทน fb)
static int wIdx = 0;
static volatile int pending = -1;
static volatile uint32_t handed = 0, copied = 0;
static SemaphoreHandle_t vsyncSem = nullptr, pendSem = nullptr, doneSem = nullptr;
static volatile uint32_t frames = 0;         // จอส่งภาพไปกี่เฟรมแล้ว (นับ vsync)
static volatile uint32_t flashPauses = 0;    // นับว่าหยุดจอเพราะใช้แฟลชกี่ครั้ง (ดูใน Serial)
static volatile int minVsyncs = 1;           // ขึ้นภาพใหม่ได้ทุกกี่ vsync (2 = ล็อกครึ่งหนึ่งของรีเฟรช)
static int curHz = 0;

// ---------- โหมด bounce แบบไม่มี fb (จอดึงภาพจากบัฟเฟอร์ของเราตรง ๆ) ----------
// เดิม: วาดลงบัฟเฟอร์หลัง → คัดลอก PSRAM→PSRAM ลง fb ทันทีที่จอเริ่มเฟรมใหม่ = ช่วงที่จอกำลังเติม bounce แถวบน ๆ
//   → PSRAM แน่น เติม bounce ไม่ทัน → จอแสดงข้อมูลเก่าจาก 16 แถวก่อน = เส้นขีดแนวนอนสั้น ๆ
// ใหม่: บัฟเฟอร์ 3 ชุด (จอแสดง / รอขึ้นจอ / กำลังวาด) สลับชุดที่จอแสดงตอนเริ่มเฟรม (ภาพไม่ฉีก) ไม่ต้องคัดลอกอีกเลย
static bool noFb = false;
static volatile int front = 0, ready = -1;
static volatile uint32_t lastSwap = 0;
static portMUX_TYPE swapMux = portMUX_INITIALIZER_UNLOCKED;
static bool IRAM_ATTR onBounce(esp_lcd_panel_handle_t, void* buf, int pos, int len, void*) {
  BaseType_t hp = pdFALSE;
  if (pos == 0) {                      // เริ่มเฟรมใหม่ = จุดสลับภาพที่ปลอดภัย
    frames++;
    portENTER_CRITICAL_ISR(&swapMux);
    if (ready >= 0 && (int)(frames - lastSwap) >= minVsyncs) { front = ready; ready = -1; lastSwap = frames; }
    portEXIT_CRITICAL_ISR(&swapMux);
    xSemaphoreGiveFromISR(vsyncSem, &hp);
  }
  const uint32_t* s = (const uint32_t*)((const uint8_t*)bb[front] + (size_t)pos * 2);
  uint32_t* d = (uint32_t*)buf;
  for (int i = len >> 2; i > 0; i--) *d++ = *s++;
  // ดึงช่วงถัดไปเข้า cache ล่วงหน้า (แบบที่ไดรเวอร์ทำเอง) → รอบหน้าคัดลอกเร็วขึ้น
  int nx = pos + len / 2;
  if (nx < LCD_W * LCD_H) Cache_Start_DCache_Preload((uint32_t)((const uint8_t*)bb[front] + (size_t)nx * 2), len, 0);
  return hp == pdTRUE;
}

static bool IRAM_ATTR onVsync(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t*, void*) {
  frames++;
  BaseType_t hp = pdFALSE;
  xSemaphoreGiveFromISR(vsyncSem, &hp);
  return hp == pdTRUE;
}

// ---------- หยุด/เริ่มจอรอบการใช้แฟลช ----------
static int dmaCh = -1;              // ช่อง GDMA ที่ต่อกับจอ
static uint32_t dmaHead = 0;        // ที่อยู่ descriptor แรกของเฟรม
static volatile int flashDepth = 0;
static bool useBounce = false;              // ใช้ bounce buffer ใน RAM ภายใน (จอไม่ต้องรอ PSRAM ตรง ๆ)
static volatile uint32_t recovers = 0;      // นับครั้งที่ปลุกจอที่ค้าง
static esp_flash_os_functions_t myOs;
static const esp_flash_os_functions_t* origOs = nullptr;

static void IRAM_ATTR hwPause() {
  if (dmaCh < 0) return;
  lcd_ll_enable_interrupt(&LCD_CAM, LCD_LL_EVENT_VSYNC_END, false);   // ไม่ให้ไดรเวอร์สั่ง DMA ใหม่
  if (useBounce) gdma_ll_tx_enable_interrupt(&GDMA, dmaCh, GDMA_LL_EVENT_TX_EOF, false);   // ไม่ให้ ISR เติม bounce (อ่าน PSRAM) ตอน cache ปิด
  esp_rom_delay_us(8);                                                 // เผื่อ ISR อีกคอร์กำลังทำงานอยู่
  lcd_ll_stop(&LCD_CAM);
  gdma_ll_tx_stop(&GDMA, dmaCh);
  flashPauses++;
}

static void IRAM_ATTR hwResume() {
  if (dmaCh < 0) return;
  // เหมือนที่ไดรเวอร์ทำตอนเริ่มส่งเฟรม: ล้าง FIFO แล้วเริ่มจากบรรทัดแรก
  gdma_ll_tx_reset_channel(&GDMA, dmaCh);
  lcd_ll_stop(&LCD_CAM);
  lcd_ll_fifo_reset(&LCD_CAM);
  gdma_ll_tx_set_desc_addr(&GDMA, dmaCh, dmaHead);
  gdma_ll_tx_start(&GDMA, dmaCh);
  esp_rom_delay_us(1);
  lcd_ll_start(&LCD_CAM);
  lcd_ll_clear_interrupt_status(&LCD_CAM, LCD_LL_EVENT_VSYNC_END);
  lcd_ll_enable_interrupt(&LCD_CAM, LCD_LL_EVENT_VSYNC_END, true);
  if (useBounce) {
    gdma_ll_tx_clear_interrupt_status(&GDMA, dmaCh, GDMA_LL_EVENT_TX_EOF);
    gdma_ll_tx_enable_interrupt(&GDMA, dmaCh, GDMA_LL_EVENT_TX_EOF, true);
  }
}

static esp_err_t IRAM_ATTR osStart(void* arg) {
  if (__atomic_fetch_add(&flashDepth, 1, __ATOMIC_SEQ_CST) == 0) hwPause();
  return origOs->start(arg);
}

static esp_err_t IRAM_ATTR osEnd(void* arg) {
  esp_err_t r = origOs->end(arg);      // เปิด cache คืนก่อน
  if (__atomic_sub_fetch(&flashDepth, 1, __ATOMIC_SEQ_CST) == 0) {
    hwResume();
    if (useBounce && panel) esp_lcd_rgb_panel_restart(panel);   // ให้ไดรเวอร์เติม bounce ใหม่ตั้งแต่ต้นเฟรมที่ vsync ถัดไป
  }
  return r;
}

static void hookFlash() {
  if (origOs || !esp_flash_default_chip || !esp_flash_default_chip->os_func) return;
  origOs = esp_flash_default_chip->os_func;
  myOs = *origOs;
  myOs.start = osStart;
  myOs.end = osEnd;
  esp_flash_default_chip->os_func = &myOs;
}

static void findDma() {
  for (int i = 0; i < SOC_GDMA_PAIRS_PER_GROUP; i++) {
    if (GDMA.channel[i].out.peri_sel.sel == SOC_GDMA_TRIG_PERIPH_LCD0) {
      dmaHead = GDMA.channel[i].out.link.addr;
      dmaCh = i;
      return;
    }
  }
}

inline void paceCopy(uint16_t* dst, const uint16_t* src, size_t bytes);

// ---------- คุมความเร็วการคัดลอกก้อนใหญ่ใน PSRAM (รวมทุกคอร์/ทุกงาน) ----------
// จอต้องดึงภาพจาก PSRAM ตลอดเวลา ถ้า 2 คอร์ + งานรูป คัดลอกพร้อมกันเร็วเกิน จอดึงไม่ทัน → วูบมืด/ภาพเลื่อน/ซ้อน
// ทุกการคัดลอกก้อนใหญ่ต้อง "จองช่วงเวลา" ก่อน → รวมกันไม่เกิน PSRAM_COPY_MBPS
#ifndef PSRAM_COPY_MBPS
#define PSRAM_COPY_MBPS 20   // ทดสอบบนบอร์ดจริง: 20 ไม่วูบที่ 44 Hz (40 วูบหน้า GIF, 10 ก็วูบ)
#endif
static volatile uint32_t copyMBps = PSRAM_COPY_MBPS;   // ปรับได้จากเมนู COPY SPEED
static portMUX_TYPE gateMux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t gateNext = 0;
// first = ไม่ต้องรอคิว (งานขึ้นจอ ต้องคัดลอกให้ทันจอไล่แสดง) แต่กันเวลาไว้ ให้คนอื่นรอแทน
inline void psramGate(size_t bytes, bool first = false) {
  uint32_t slot = (uint32_t)(bytes / (copyMBps ? copyMBps : 1)) + 1;   // ไมโครวินาที
  portENTER_CRITICAL(&gateMux);
  uint32_t now = (uint32_t)esp_timer_get_time();
  uint32_t start = (!first && (int32_t)(gateNext - now) > 0) ? gateNext : now;
  gateNext = ((int32_t)(gateNext - now) > 0 ? gateNext : now) + slot;
  portEXIT_CRITICAL(&gateMux);
  while ((int32_t)(start - (uint32_t)esp_timer_get_time()) > 0) {}
}

// ---------- คัดลอกเฉพาะส่วนที่เปลี่ยน ----------
// แบ่งจอเป็นแถบเล็กละ 4 แถว (68 แถบ) จำ "ลายนิ้วมือ" (hash) ของแต่ละแถบในบัฟเฟอร์หลังแต่ละชุดและบนจอ
// แถบที่เหมือนเดิมไม่ต้องคัดลอก → ลดงาน PSRAM ซึ่งเป็นคอขวดหลัก (ภาพนิ่ง/ตัวหนังสือที่ไม่เปลี่ยน ไม่ต้องย้ายทุกเฟรม)
static const int BAND_H = 4, NB = LCD_H / BAND_H;
static const size_t BAND_BYTES = (size_t)BAND_H * LCD_W * 2;
static const uint32_t NOHASH = 0x80000000u;          // "ไม่รู้" (hash จริงไม่มีบิตบนสุด)
static uint32_t bbHash[3][NB], fbHash[NB];
static bool bbFull[3] = {false, false, false};
static volatile uint32_t bandsCopied = 0;            // นับแถบที่คัดลอกขึ้นจอจริง (ดูใน Serial)
static void resetHashes() {
  for (int b = 0; b < NB; b++) { bbHash[0][b] = bbHash[1][b] = bbHash[2][b] = fbHash[b] = NOHASH; }
}
static inline uint32_t hashRows(const uint16_t* p, size_t words32, uint32_t seed) {
  const uint32_t* q = (const uint32_t*)p;
  uint32_t h = 2166136261u ^ seed;
  for (size_t i = 0; i < words32; i++) h = (h ^ q[i]) * 16777619u;
  return (h ^ (h >> 15)) & 0x7FFFFFFFu;
}
inline int backIndex() { return wIdx; }
// ใส่แถบ dBand (แถวจริงบนจอ) ลงบัฟเฟอร์หลัง จากแถว 4 แถวในบัฟเฟอร์แถบ (ข้ามถ้าเหมือนของเดิมในบัฟเฟอร์นี้)
inline void putBand(int dBand, const uint16_t* src, bool flip) {
  int w = wIdx;
  uint32_t h = hashRows(src, BAND_BYTES / 4, flip ? 0x5bd1e995u : 0);
  if (bbHash[w][dBand] == h) return;
  uint16_t* d = bb[w] + (size_t)dBand * BAND_H * LCD_W;
  psramGate(BAND_BYTES);
  if (!flip) memcpy(d, src, BAND_BYTES);
  else
    for (int r = 0; r < BAND_H; r++) {      // กลับจอ 180°: แถวกลับหัว + กลับซ้ายขวา
      const uint16_t* s = src + r * LCD_W;
      uint16_t* o = d + (BAND_H - 1 - r) * LCD_W + LCD_W - 1;
      for (int x = 0; x < LCD_W; x++) *o-- = s[x];
    }
  if (!noFb) esp_cache_msync(d, BAND_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  bbHash[w][dBand] = h;
}
// คัดลอกบัฟเฟอร์หลัง p ขึ้นจอ เฉพาะแถบที่ต่างจากบนจอ
static void copyChanged(int p) {
  if (bbFull[p]) {
    paceCopy(fb, bb[p], FB_BYTES);
    for (int b = 0; b < NB; b++) fbHash[b] = NOHASH;
    bandsCopied += NB;
    return;
  }
  int n = 0;
  for (int b = 0; b < NB; b++) {
    if (bbHash[p][b] == fbHash[b]) continue;
    size_t o = (size_t)b * BAND_H * LCD_W;
    psramGate(BAND_BYTES * 2, true);          // อ่าน+เขียน PSRAM (งานขึ้นจอมาก่อน)
    memcpy(fb + o, bb[p] + o, BAND_BYTES);
    esp_cache_msync(fb + o, BAND_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    fbHash[b] = bbHash[p][b];
    if (++n % 2 == 0) esp_rom_delay_us(60);   // จังหวะเดิมที่ทดสอบแล้วภาพนิ่ง
  }
  bandsCopied += n;
}

// งานคัดลอกภาพขึ้นจอ (คอร์ 1): รอ vsync แล้วคัดลอกทั้งจอ ระหว่างนั้นโปรแกรมหลักวาดเฟรมถัดไปลงอีกบัฟเฟอร์ได้เลย
static void presenter(void*) {
  uint32_t lastF = 0;
  for (;;) {
    xSemaphoreTake(pendSem, portMAX_DELAY);
    int p = pending;
    if (p < 0) continue;
    int miss = 0;
    do {
      xSemaphoreTake(vsyncSem, 0);
      if (xSemaphoreTake(vsyncSem, pdMS_TO_TICKS(60)) != pdTRUE && flashDepth == 0 && !useBounce && ++miss >= 3) {
        // ไม่มี vsync มา 180 ms → จอค้าง (DMA หยุด) → ปลุกใหม่
        miss = 0;
        recovers++;
        hwResume();   // (โหมด bounce ไม่ทำ: เริ่ม DMA เองแล้วไดรเวอร์นับตำแหน่ง bounce ผิด → ภาพเลื่อนลง)
      }
    } while ((int)(frames - lastF) < minVsyncs);
    copyChanged(p);
    lastF = frames;
    pending = -1;
    copied++;
    xSemaphoreGive(doneSem);
  }
}

static bool usePresenter = false;

// hz = อัตรารีเฟรช (ตั้งตอนเริ่มเท่านั้น — เปลี่ยนระหว่างทำงานแล้วเสี่ยงดับ), every = ขึ้นภาพทุกกี่ vsync
// presenter = false → คัดลอกขึ้นจอเองในโปรแกรมหลักแบบเดิม (โหมดปลอดภัย)
inline bool begin(int hz = 44, int every = 1, bool presenter = true, bool bounce = false) {
  esp_lcd_rgb_panel_config_t c;
  memset(&c, 0, sizeof(c));
  c.clk_src = LCD_CLK_SRC_DEFAULT;
  c.timings.pclk_hz = hz == 44 ? LCD_PCLK_HZ : (uint32_t)hz * DOTS_PER_FRAME;   // 44 Hz = 10 MHz พอดี (แบบที่ใช้ได้)
  minVsyncs = every < 1 ? 1 : every;
  c.timings.h_res = LCD_W;
  c.timings.v_res = LCD_H;
  c.timings.hsync_pulse_width = 10;
  c.timings.hsync_back_porch = 80;
  c.timings.hsync_front_porch = 60;
  c.timings.vsync_pulse_width = 10;
  c.timings.vsync_back_porch = 20;
  c.timings.vsync_front_porch = 60;
  c.timings.flags.pclk_active_neg = 1;
  c.data_width = 16;
  c.num_fbs = 1;
  c.dma_burst_size = 64;
  c.hsync_gpio_num = 47;
  c.vsync_gpio_num = 48;
  c.de_gpio_num = 40;
  c.pclk_gpio_num = 14;
  c.disp_gpio_num = -1;
  const int data[16] = {9, 10, 11, 12, 13, 16, 17, 18, 8, 3, 46, 4, 5, 6, 7, 15};
  for (int i = 0; i < 16; i++) c.data_gpio_nums[i] = data[i];
  c.flags.fb_in_psram = 1;
  if (bounce) { c.bounce_buffer_size_px = LCD_W * 8; c.flags.no_fb = 1; c.num_fbs = 0; }   // no_fb ห้ามตั้ง num_fbs   // 2 × 8 แถวใน RAM ภายใน (ทดสอบแล้ว: 16 แถว RAM ไม่พอวาด 2 คอร์ + ภาพเลื่อน)
  useBounce = bounce;
  noFb = bounce;
  // บัฟเฟอร์ของเราต้องพร้อมก่อนเริ่มจอ (โหมด bounce จอดึงภาพจากบัฟเฟอร์นี้ทันที)
  for (int i = 0; i < (noFb ? 3 : 2); i++) {
    bb[i] = (uint16_t*)heap_caps_aligned_alloc(64, FB_BYTES, MALLOC_CAP_SPIRAM);
    if (!bb[i]) return false;
    memset(bb[i], 0, FB_BYTES);
  }
  front = 0; ready = -1; wIdx = 1;
  resetHashes();
  vsyncSem = xSemaphoreCreateBinary();
  if (esp_lcd_new_rgb_panel(&c, &panel) != ESP_OK) {
    if (!bounce) return false;
    // เริ่มโหมด bounce ไม่ได้ → ถอยไปโหมดเดิม (ไม่ให้จอดำ)
    Serial.println("LCD bounce/no_fb init failed -> fallback normal mode");
    c.bounce_buffer_size_px = 0; c.flags.no_fb = 0; c.num_fbs = 1;
    bounce = false; useBounce = false; noFb = false;
    heap_caps_free(bb[2]); bb[2] = nullptr;
    if (esp_lcd_new_rgb_panel(&c, &panel) != ESP_OK) return false;
  }
  esp_lcd_rgb_panel_event_callbacks_t cb;
  memset(&cb, 0, sizeof(cb));
  // โหมด bounce: จอดึงภาพจาก fb ล่วงหน้าเป็นช่วง ๆ — รอบสุดท้ายของเฟรมดึงเสร็จ = ปลอดภัยที่จะคัดลอกภาพใหม่ลง fb
  // (ถ้ารอ vsync ไดรเวอร์จะเติม bounce ช่วงบนของเฟรมถัดไปไปก่อนแล้ว → แถวบน ๆ เป็นภาพเก่า เห็นเป็นเส้นรอยต่อ)
  if (bounce) cb.on_bounce_empty = onBounce;
  else cb.on_vsync = onVsync;
  esp_lcd_rgb_panel_register_event_callbacks(panel, &cb, nullptr);
  esp_lcd_panel_reset(panel);
  esp_lcd_panel_init(panel);
  if (!noFb) {
    void* a = nullptr;
    if (esp_lcd_rgb_panel_get_frame_buffer(panel, 1, &a) != ESP_OK || !a) return false;
    fb = (uint16_t*)a;
    memset(fb, 0, FB_BYTES);
    esp_cache_msync(fb, FB_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  }
  ledcAttach(PIN_BL, 5000, 8);
  ledcWrite(PIN_BL, 0);
  delay(30);                 // ให้จอส่งภาพไปสักเฟรมก่อน ค่อยจำตำแหน่ง DMA
  findDma();
  hookFlash();
  curHz = hz;
  Serial.printf("LCD stream %d Hz (every %d), DMA ch %d head 0x%05x, flash hook %s, presenter %s, bounce %s\n", hz, minVsyncs, dmaCh,
                (unsigned)dmaHead, origOs ? "on" : "OFF", presenter ? "on" : "off", bounce ? "on" : "off");
  if (presenter && !noFb) {
    pendSem = xSemaphoreCreateBinary();
    doneSem = xSemaphoreCreateBinary();
    usePresenter = xTaskCreatePinnedToCore(lcd::presenter, "lcd", 4096, nullptr, 6, nullptr, 1) == pdPASS;
  }
  
  return true;
}

inline bool ok() { return noFb ? (bb[0] && bb[1] && bb[2]) : (fb && bb[0] && bb[1]); }

// คัดลอกลง PSRAM ทีละช่วงเล็ก ๆ เว้นจังหวะให้จออ่าน PSRAM ได้ (คัดลอกรวดเดียว → บรรทัดเลื่อน ภาพซ้อน)
inline void paceCopy(uint16_t* dst, const uint16_t* src, size_t bytes) {
  const size_t CH = LCD_W * 2 * 8;
  uint8_t* d = (uint8_t*)dst;
  const uint8_t* s = (const uint8_t*)src;
  for (size_t o = 0; o < bytes; o += CH) {
    size_t n = bytes - o < CH ? bytes - o : CH;
    psramGate(n * 2);
    memcpy(d + o, s + o, n);
    esp_cache_msync(d + o, n, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  }
}
static uint16_t* dummy = nullptr;   // ถ้าจอเริ่มไม่ได้ วาดลงที่นี่แทน (ไม่ค้าง)
inline uint16_t* back() {
  if (!ok()) { if (!dummy) dummy = (uint16_t*)heap_caps_malloc(FB_BYTES, MALLOC_CAP_SPIRAM); return dummy; }
  return bb[wIdx];
}

// ส่งเฟรมที่วาดเสร็จให้งาน lcd ขึ้นจอที่ vsync ถัดไป แล้วสลับไปวาดอีกบัฟเฟอร์
// (ถ้าเฟรมก่อนยังไม่ได้ขึ้นจอ รอก่อน — บัฟเฟอร์ที่จะวาดต่อต้องคัดลอกเสร็จแล้ว)
inline void present() {
  if (!ok()) { delay(16); return; }
  if (noFb) {
    // รอให้เฟรมที่ส่งไปก่อนหน้าขึ้นจอก่อน (ไม่วาดเร็วกว่าจอ) แล้วส่งเฟรมนี้ ไปวาดชุดที่ว่าง
    uint32_t t0 = millis();
    while (ready >= 0 && millis() - t0 < 120) xSemaphoreTake(vsyncSem, pdMS_TO_TICKS(30));
    portENTER_CRITICAL(&swapMux);
    ready = wIdx;
    int nx = 3 - front - wIdx;
    portEXIT_CRITICAL(&swapMux);
    wIdx = nx;
    handed++; copied++;
    bandsCopied += NB;
    return;
  }
  if (!usePresenter) {                       // แบบเดิม: รอ vsync แล้วคัดลอกเอง
    xSemaphoreTake(vsyncSem, 0);
    xSemaphoreTake(vsyncSem, pdMS_TO_TICKS(60));
    copyChanged(wIdx);
    bbFull[wIdx] = false;
    return;
  }
  while (copied != handed) xSemaphoreTake(doneSem, pdMS_TO_TICKS(50));
  pending = wIdx;
  handed++;
  xSemaphoreGive(pendSem);
  wIdx ^= 1;
  bbFull[wIdx] = false;                      // ชุดที่จะวาดต่อ: คัดลอกเสร็จแล้ว ใช้ hash ตามปกติ
}
// ส่งขึ้นจอทั้งภาพ (ใช้เมื่อวาดลง back() ตรง ๆ ไม่ผ่าน putBand เช่น โลโก้/หน้าจอแจ้งปัญหา)
inline void presentFull() {
  if (!ok()) return;
  int w = wIdx;
  bbFull[w] = true;
  for (int b = 0; b < NB; b++) bbHash[w][b] = NOHASH;
  present();
}
// รอจนเฟรมล่าสุดขึ้นจอจริง (ใช้ตอนเปิดเครื่อง/หน้าจอแจ้งปัญหา)
inline void flush() {
  if (!ok()) return;
  if (noFb) { uint32_t t0 = millis(); while (ready >= 0 && millis() - t0 < 200) xSemaphoreTake(vsyncSem, pdMS_TO_TICKS(30)); return; }
  if (!usePresenter) return;
  while (copied != handed) xSemaphoreTake(doneSem, pdMS_TO_TICKS(50));
}

inline void brightness(int pct) { ledcWrite(PIN_BL, constrain(pct, 0, 100) * 255 / 100); }

}  // namespace lcd
