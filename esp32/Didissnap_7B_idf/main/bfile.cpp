// bfile.cpp — รับไฟล์จากแอปทาง BLE (ดูโปรโตคอลใน bfile.h)
#include "bfile.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "board.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

namespace bfile {

static const char* TAG = "bfile";

// d1d15700-6f0b-4e8b-9c4e-0b5d1d150000 / d1d15701-… (กลับลำดับไบต์)
static const ble_uuid128_t U_SVC = BLE_UUID128_INIT(0x00, 0x00, 0x15, 0x1d, 0x5d, 0x0b, 0x4e, 0x9c, 0x8b, 0x4e, 0x0b, 0x6f, 0x00, 0x57, 0xd1, 0xd1);
static const ble_uuid128_t U_FILE = BLE_UUID128_INIT(0x00, 0x00, 0x15, 0x1d, 0x5d, 0x0b, 0x4e, 0x9c, 0x8b, 0x4e, 0x0b, 0x6f, 0x01, 0x57, 0xd1, 0xd1);

static const char* DIR = "/sdcard/skins";
static const char* TMP = "/sdcard/skins/.upload.tmp";
static const uint32_t MAXSIZE = 4 * 1024 * 1024;

static volatile int64_t winUntil = 0;
static volatile uint16_t conn = BLE_HS_CONN_HANDLE_NONE;
static volatile bool synced = false, advOn = false;
static FILE* fp = nullptr;
static uint32_t fSize = 0, fGot = 0, fCrc = 0;
static char fName[40] = "";
static volatile bool doneFlag = false;
static volatile int prog = -1;

static bool validName(const char* n) {
  size_t l = strlen(n);
  if (l < 5 || l > 32) return false;
  for (size_t i = 0; i < l; i++) {
    char c = n[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
  }
  if (n[0] == '.') return false;
  const char* e = strrchr(n, '.');
  return e && (!strcmp(e, ".jpg") || !strcmp(e, ".png") || !strcmp(e, ".gif") || !strcmp(e, ".json"));
}

static void abortFile() {
  if (fp) { fclose(fp); fp = nullptr; remove(TMP); }
  prog = -1;
}

static int onWrite(const uint8_t* d, int n) {
  if (n < 1) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  switch (d[0]) {
    case 0x01: {                                        // เริ่มไฟล์
      if (n < 6 || n - 5 >= (int)sizeof(fName)) return 0x80;
      uint32_t sz;
      memcpy(&sz, d + 1, 4);
      char nm[40];
      memcpy(nm, d + 5, n - 5);
      nm[n - 5] = 0;
      if (!validName(nm) || sz == 0 || sz > MAXSIZE) return 0x80;
      if (!board_sd_ok()) return 0x82;
      abortFile();
      mkdir(DIR, 0777);
      fp = fopen(TMP, "wb");
      if (!fp) return 0x82;
      setvbuf(fp, nullptr, _IOFBF, 8192);
      fSize = sz;
      fGot = 0;
      fCrc = 0;
      snprintf(fName, sizeof(fName), "%s", nm);
      prog = 0;
      ESP_LOGI(TAG, "begin %s (%u bytes)", fName, (unsigned)sz);
      return 0;
    }
    case 0x02: {                                        // ข้อมูล
      if (!fp) return 0x84;
      if (n < 5) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
      uint32_t off;
      memcpy(&off, d + 1, 4);
      int len = n - 5;
      if (off != fGot) return 0x81;
      if (fGot + len > fSize) return 0x80;
      if (len && fwrite(d + 5, 1, len, fp) != (size_t)len) { abortFile(); return 0x82; }
      fCrc = esp_rom_crc32_le(fCrc, d + 5, len);
      fGot += len;
      prog = (int)((uint64_t)fGot * 100 / fSize);
      winUntil = esp_timer_get_time() + 60000000;       // กำลังส่งอยู่ = ต่อเวลาเปิดรับ
      return 0;
    }
    case 0x03: {                                        // จบไฟล์
      if (!fp) return 0x84;
      if (n < 5) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
      uint32_t crc;
      memcpy(&crc, d + 1, 4);
      fclose(fp);
      fp = nullptr;
      if (fGot != fSize || crc != fCrc) {
        ESP_LOGW(TAG, "%s: got %u/%u crc %08x/%08x", fName, (unsigned)fGot, (unsigned)fSize, (unsigned)fCrc, (unsigned)crc);
        remove(TMP);
        prog = -1;
        return 0x83;
      }
      char path[64];
      snprintf(path, sizeof(path), "%s/%s", DIR, fName);
      remove(path);
      if (rename(TMP, path) != 0) { remove(TMP); prog = -1; return 0x82; }
      ESP_LOGI(TAG, "saved %s", path);
      prog = 100;
      doneFlag = true;
      return 0;
    }
    case 0x04: {                                        // ลบไฟล์
      char nm[40];
      if (n - 1 >= (int)sizeof(nm)) return 0x80;
      memcpy(nm, d + 1, n - 1);
      nm[n - 1] = 0;
      if (!validName(nm)) return 0x80;
      char path[64];
      snprintf(path, sizeof(path), "%s/%s", DIR, nm);
      remove(path);
      snprintf(fName, sizeof(fName), "%s", nm);
      doneFlag = true;
      return 0;
    }
    default:
      return BLE_ATT_ERR_REQ_NOT_SUPPORTED;
  }
}

static int access(uint16_t, uint16_t, ble_gatt_access_ctxt* ctxt, void*) {
  if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
  if (!windowLeft() && !fp) return 0x84;                // ปิดรับแล้ว
  static uint8_t buf[520];
  int n = OS_MBUF_PKTLEN(ctxt->om);
  if (n > (int)sizeof(buf)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  os_mbuf_copydata(ctxt->om, 0, n, buf);
  return onWrite(buf, n);
}

static const ble_gatt_chr_def chrs[] = {
    {.uuid = &U_FILE.u, .access_cb = access, .flags = BLE_GATT_CHR_F_WRITE},
    {},
};
static const ble_gatt_svc_def svcs[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &U_SVC.u, .characteristics = chrs},
    {},
};

static void advStart();
static int gapEvent(ble_gap_event* e, void*) {
  switch (e->type) {
    case BLE_GAP_EVENT_CONNECT:
      advOn = false;
      if (e->connect.status == 0) {
        conn = e->connect.conn_handle;
        ESP_LOGI(TAG, "app connected");
      } else if (windowLeft()) {
        advStart();
      }
      break;
    case BLE_GAP_EVENT_DISCONNECT:
      conn = BLE_HS_CONN_HANDLE_NONE;
      if (fp) abortFile();                              // หลุดกลางทาง = ทิ้งไฟล์ครึ่ง ๆ
      ESP_LOGI(TAG, "app disconnected");
      if (windowLeft()) advStart();
      break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
      advOn = false;
      if (windowLeft() && conn == BLE_HS_CONN_HANDLE_NONE) advStart();
      break;
    default: break;
  }
  return 0;
}

static void advStart() {
  if (!synced || advOn || conn != BLE_HS_CONN_HANDLE_NONE) return;
  uint8_t own;
  if (ble_hs_id_infer_auto(0, &own) != 0) return;
  ble_hs_adv_fields f;
  memset(&f, 0, sizeof(f));
  f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  f.uuids128 = &U_SVC;
  f.num_uuids128 = 1;
  f.uuids128_is_complete = 1;
  if (ble_gap_adv_set_fields(&f) != 0) return;
  ble_hs_adv_fields r;
  memset(&r, 0, sizeof(r));
  static const char* nm = "DIDI-7B";
  r.name = (const uint8_t*)nm;
  r.name_len = strlen(nm);
  r.name_is_complete = 1;
  ble_gap_adv_rsp_set_fields(&r);
  ble_gap_adv_params p;
  memset(&p, 0, sizeof(p));
  p.conn_mode = BLE_GAP_CONN_MODE_UND;
  p.disc_mode = BLE_GAP_DISC_MODE_GEN;
  p.itvl_min = 0x30;
  p.itvl_max = 0x60;
  int ms = windowLeft() * 1000;
  if (ms <= 0) return;
  int rc = ble_gap_adv_start(own, nullptr, ms, &p, gapEvent, nullptr);
  advOn = rc == 0;
  if (rc) ESP_LOGW(TAG, "adv start %d", rc);
}

void init() {
  ble_svc_gap_init();
  ble_svc_gatt_init();
  ble_svc_gap_device_name_set("DIDI-7B");
  int rc = ble_gatts_count_cfg(svcs);
  if (rc == 0) rc = ble_gatts_add_svcs(svcs);
  if (rc) ESP_LOGE(TAG, "gatt svc %d", rc);
}
void onSync() {
  synced = true;
  if (windowLeft()) advStart();
}
void openWindow(int sec) {
  winUntil = esp_timer_get_time() + (int64_t)sec * 1000000;
  if (advOn) { ble_gap_adv_stop(); advOn = false; }
  advStart();
}
void closeWindow() {
  winUntil = 0;
  if (advOn) { ble_gap_adv_stop(); advOn = false; }
  if (conn != BLE_HS_CONN_HANDLE_NONE && !fp) ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
}
int windowLeft() {
  int64_t l = winUntil - esp_timer_get_time();
  return l > 0 ? (int)(l / 1000000) + 1 : 0;
}
bool connected() { return conn != BLE_HS_CONN_HANDLE_NONE; }
int progress() { return prog; }
const char* current() { return fName; }
bool takeDone(char* name, size_t n) {
  if (!doneFlag) return false;
  doneFlag = false;
  snprintf(name, n, "%s", fName);
  return true;
}

}  // namespace bfile
