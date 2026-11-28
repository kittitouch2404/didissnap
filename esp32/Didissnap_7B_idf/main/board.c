// board.c — จอ RGB 1024x600 + LVGL (esp_lvgl_port, กันภาพฉีก) + ทัช GT911 + ชิปขยายขา + SD
#include "board.h"
#include <string.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/sdmmc_host.h"
#include "esp_check.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

static const char* TAG = "board";

#define PIN_SDA 8
#define PIN_SCL 9
#define PIN_TP_INT 4
#define EXP_ADDR 0x24
// ขาของชิปขยายขา
#define EX_TP_RST 1
#define EX_DISP 2
#define EX_LCD_RST 3
#define EX_SD_CS 4
#define EX_USB_CAN 5   // 0 = USB · 1 = CAN
#define EX_VCOM 6

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_exp;
static bool s_expOk = false;
static uint8_t s_out = 0xDF;   // ทุกขา HIGH ยกเว้น IO5 (ใช้ USB)
static int s_bri = -1;
static uint8_t s_pwm = 0;
static bool s_sd = false;

static bool exp_wr(uint8_t reg, uint8_t v) {
  uint8_t b[2] = {reg, v};
  return i2c_master_transmit(s_exp, b, 2, 30) == ESP_OK;
}

// ชิปขยายขาเป็น MCU — เสียบไฟใหม่มันบูตช้ากว่า ESP32 → รอ/ลองซ้ำ
static bool exp_init(int waitMs) {
  int t = 0;
  do {
    if (exp_wr(0x02, 0xFF)) return true;   // ทุกขาเป็นขาออก
    vTaskDelay(pdMS_TO_TICKS(25));
    t += 25;
  } while (t < waitMs);
  return false;
}

void board_backlight(int pct) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  s_bri = pct;
  int duty = 100 - pct;                     // ชิปกลับด้าน: 0 = สว่างสุด
  if (duty > 97) duty = 97;
  s_pwm = (uint8_t)(duty * 255 / 100);
  if (pct > 0) s_out |= (1 << EX_DISP); else s_out &= ~(1 << EX_DISP);
  if (!s_expOk) return;
  exp_wr(0x03, s_out);
  exp_wr(0x05, s_pwm);
}

void board_keepalive(void) {
  bool ok = exp_wr(0x02, 0xFF);
  if (ok) {
    exp_wr(0x03, s_out);
    if (s_bri >= 0) exp_wr(0x05, s_pwm);
  }
  if (ok && !s_expOk) ESP_LOGW(TAG, "io-expander came back");
  s_expOk = ok;
}

static int s_mode = 0;
int board_render_mode(void) { return s_mode; }

static esp_lcd_panel_handle_t lcd_new(int nfb) {
  esp_lcd_rgb_panel_config_t c;
  memset(&c, 0, sizeof(c));
  c.clk_src = LCD_CLK_SRC_DEFAULT;
  c.timings.pclk_hz = 30 * 1000 * 1000;
  c.timings.h_res = LCD_W;
  c.timings.v_res = LCD_H;
  c.timings.hsync_pulse_width = 162;
  c.timings.hsync_back_porch = 152;
  c.timings.hsync_front_porch = 48;
  c.timings.vsync_pulse_width = 45;
  c.timings.vsync_back_porch = 13;
  c.timings.vsync_front_porch = 3;
  c.timings.flags.pclk_active_neg = 1;
  c.data_width = 16;
  c.bits_per_pixel = 16;
  c.num_fbs = nfb;                            // 2 ชุด (โหมด 0): LVGL วาดชุดหนึ่ง จอแสดงอีกชุด · 1 ชุด (โหมด 1)
  c.bounce_buffer_size_px = LCD_W * 10;       // จอดึงภาพผ่าน RAM ภายใน (PSRAM ไม่ต้องตอบทันที)
  c.dma_burst_size = 64;
  c.hsync_gpio_num = 46;
  c.vsync_gpio_num = 3;
  c.de_gpio_num = 5;
  c.pclk_gpio_num = 7;
  c.disp_gpio_num = -1;
  // B3-B7, G2-G7, R3-R7
  const int data[16] = {14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40};
  for (int i = 0; i < 16; i++) c.data_gpio_nums[i] = data[i];
  c.flags.fb_in_psram = 1;
  esp_lcd_panel_handle_t p = NULL;
  ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&c, &p));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(p));
  ESP_ERROR_CHECK(esp_lcd_panel_init(p));
  return p;
}

static bool s_touchOk = false;
bool board_touch_ok(void) { return s_touchOk; }

static esp_lcd_touch_handle_t touch_new(void) {
  // รีเซ็ต GT911: INT ต่ำตอนปล่อยรีเซ็ต = ที่อยู่ 0x5D
  gpio_config_t g = {.pin_bit_mask = 1ULL << PIN_TP_INT, .mode = GPIO_MODE_OUTPUT};
  gpio_config(&g);
  gpio_set_level(PIN_TP_INT, 0);
  s_out &= ~(1 << EX_TP_RST);
  if (s_expOk) exp_wr(0x03, s_out);
  vTaskDelay(pdMS_TO_TICKS(20));
  s_out |= (1 << EX_TP_RST);
  if (s_expOk) exp_wr(0x03, s_out);
  vTaskDelay(pdMS_TO_TICKS(60));
  gpio_set_direction(PIN_TP_INT, GPIO_MODE_INPUT);
  vTaskDelay(pdMS_TO_TICKS(50));

  uint8_t addr = 0;
  if (i2c_master_probe(s_bus, 0x5D, 50) == ESP_OK) addr = 0x5D;
  else if (i2c_master_probe(s_bus, 0x14, 50) == ESP_OK) addr = 0x14;
  if (!addr) { ESP_LOGE(TAG, "GT911 not found"); return NULL; }

  esp_lcd_panel_io_handle_t io = NULL;
  esp_lcd_panel_io_i2c_config_t ioc = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
  ioc.dev_addr = addr;
  ioc.scl_speed_hz = 400000;
  if (esp_lcd_new_panel_io_i2c(s_bus, &ioc, &io) != ESP_OK) return NULL;
  esp_lcd_touch_config_t tc;
  memset(&tc, 0, sizeof(tc));
  tc.x_max = LCD_W;
  tc.y_max = LCD_H;
  tc.rst_gpio_num = GPIO_NUM_NC;
  tc.int_gpio_num = GPIO_NUM_NC;
  esp_lcd_touch_handle_t tp = NULL;
  if (esp_lcd_touch_new_i2c_gt911(io, &tc, &tp) != ESP_OK) return NULL;
  ESP_LOGI(TAG, "GT911 ok (0x%02X)", addr);
  return tp;
}

lv_display_t* board_init(int mode) {
  i2c_master_bus_config_t bc;
  memset(&bc, 0, sizeof(bc));
  bc.i2c_port = I2C_NUM_0;
  bc.sda_io_num = PIN_SDA;
  bc.scl_io_num = PIN_SCL;
  bc.clk_source = I2C_CLK_SRC_DEFAULT;
  bc.glitch_ignore_cnt = 7;
  bc.flags.enable_internal_pullup = true;
  ESP_ERROR_CHECK(i2c_new_master_bus(&bc, &s_bus));
  i2c_device_config_t dc;
  memset(&dc, 0, sizeof(dc));
  dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dc.device_address = EXP_ADDR;
  dc.scl_speed_hz = 400000;
  ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &dc, &s_exp));

  s_expOk = exp_init(3000);
  s_out = 0xDF & ~(1 << EX_DISP);             // VCOM เปิด · ไฟจอปิดไว้ก่อน (ไม่เห็นภาพขยะตอนเริ่ม) · USB
  if (s_expOk) exp_wr(0x03, s_out);
  ESP_LOGI(TAG, "io-expander %s", s_expOk ? "ok" : "MISSING");

  esp_lcd_panel_handle_t panel = lcd_new(mode ? 1 : 2);
  esp_lcd_touch_handle_t tp = touch_new();
  s_touchOk = tp != NULL;

  lvgl_port_cfg_t lc = ESP_LVGL_PORT_INIT_CONFIG();
  lc.task_priority = 4;
  lc.task_stack = 12288;
  lc.task_affinity = 1;                       // LVGL คอร์ 1 · Bluetooth คอร์ 0
  lc.timer_period_ms = 5;
  ESP_ERROR_CHECK(lvgl_port_init(&lc));

  lvgl_port_display_cfg_t d;
  memset(&d, 0, sizeof(d));
  d.panel_handle = panel;
  d.hres = LCD_W;
  d.vres = LCD_H;
  d.color_format = LV_COLOR_FORMAT_RGB565;
  d.double_buffer = true;
  lvgl_port_display_rgb_cfg_t rc;
  memset(&rc, 0, sizeof(rc));
  lv_display_t* disp = NULL;
  if (mode) {
    // วาดลงบัฟเฟอร์เล็กใน RAM ภายใน (เร็วกว่า PSRAM หลายเท่า) แล้วก๊อปลงเฟรมบัฟเฟอร์ทีเดียว
    d.buffer_size = LCD_W * 32;
    d.flags.buff_dma = 1;                     // บังคับ RAM ภายใน
    disp = lvgl_port_add_disp_rgb(&d, &rc);
    if (!disp) {                              // RAM ภายในไม่พอ → บัฟเฟอร์เล็กใน PSRAM
      ESP_LOGW(TAG, "no internal RAM for draw buffers, using PSRAM");
      d.flags.buff_dma = 0;
      d.flags.buff_spiram = 1;
      d.buffer_size = LCD_W * 60;
      disp = lvgl_port_add_disp_rgb(&d, &rc);
    }
    s_mode = 1;
  } else {
    d.buffer_size = LCD_W * LCD_H;
    d.flags.buff_spiram = 1;
    d.flags.direct_mode = 1;                  // วาดเฉพาะส่วนที่เปลี่ยน ลงบัฟเฟอร์เต็มจอ
    rc.flags.bb_mode = 1;
    rc.flags.avoid_tearing = 1;
    disp = lvgl_port_add_disp_rgb(&d, &rc);
    s_mode = 0;
  }
  ESP_LOGI(TAG, "render mode %d · internal free %u", s_mode, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

  if (tp) {
    lvgl_port_touch_cfg_t t;
    memset(&t, 0, sizeof(t));
    t.disp = disp;
    t.handle = tp;
    lvgl_port_add_touch(&t);
  }
  return disp;
}

bool board_sd_mount(void) {
  // CS (D3) ของการ์ดต่อกับชิปขยายขา → ดึงขึ้นไว้ แล้วใช้โหมด SDMMC 1 บิต (CLK 12 · CMD 11 · D0 13)
  s_out |= (1 << EX_SD_CS);
  if (s_expOk) exp_wr(0x03, s_out);
  esp_vfs_fat_sdmmc_mount_config_t mc;
  memset(&mc, 0, sizeof(mc));
  mc.format_if_mount_failed = false;
  mc.max_files = 6;
  mc.allocation_unit_size = 16 * 1024;
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
  sdmmc_slot_config_t sc = SDMMC_SLOT_CONFIG_DEFAULT();
  sc.width = 1;
  sc.clk = GPIO_NUM_12;
  sc.cmd = GPIO_NUM_11;
  sc.d0 = GPIO_NUM_13;
  sc.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
  sdmmc_card_t* card = NULL;
  esp_err_t e = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &sc, &mc, &card);
  s_sd = e == ESP_OK;
  if (s_sd) ESP_LOGI(TAG, "SD %llu MB", ((uint64_t)card->csd.capacity) * card->csd.sector_size / (1024 * 1024));
  else ESP_LOGW(TAG, "no SD card (%s)", esp_err_to_name(e));
  return s_sd;
}

bool board_sd_ok(void) { return s_sd; }
