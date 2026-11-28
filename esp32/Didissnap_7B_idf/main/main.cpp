// main.cpp — Didissnap 7B: จอ 7" 1024x600 (Waveshare ESP32-S3-Touch-LCD-7B) รับค่ารถจาก ESP32-Superlink ทาง BLE
#include <stdio.h>
#include <sys/stat.h>
#include "board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "skin.h"
#include "slink.h"
#include "ui.h"
#include "vdata.h"

extern "C" void app_main(void) {
  esp_err_t e = nvs_flash_init();
  if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }
  vd::begin();
  int rmode = 1;                      // โหมดวาด (เมนู → ระบบ) · ค่าเริ่มต้น = เร็ว
  nvs_handle_t h;
  if (nvs_open("ui", NVS_READONLY, &h) == ESP_OK) {
    int32_t v;
    if (nvs_get_i32(h, "rmode", &v) == ESP_OK) rmode = v;
    nvs_close(h);
  }
  board_init(rmode);
  if (board_sd_mount()) {
    mkdir("/sdcard/skins", 0777);
    struct stat st;
    if (stat("/sdcard/skins/default.json", &st) != 0) {   // ใส่สกินตัวอย่างไว้ให้แก้ในคอม
      FILE* f = fopen("/sdcard/skins/default.json", "wb");
      if (f) { fputs(skin::BUILTIN, f); fclose(f); }
    }
  }
  if (lvgl_port_lock(0)) {
    ui::build();
    lvgl_port_unlock();
  }
  vTaskDelay(pdMS_TO_TICKS(120));   // ให้ภาพแรกขึ้นก่อนค่อยเปิดไฟจอ
  board_backlight(ui::bri());
  slink::begin();
  ESP_LOGI("main", "ready · free internal %u · psram %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
