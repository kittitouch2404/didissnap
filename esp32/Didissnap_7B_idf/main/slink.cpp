// link.cpp — NimBLE central: สแกน → เลือก Superlink ของเรา → ต่อ → เปิดแจ้งเตือน → ยืนยันตัว (DN_AUTH) → รับค่า
#include "slink.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "didinow.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs.h"
#include "vdata.h"
#include "bfile.h"

namespace slink {

static const char* TAG = "link";

// d1d15500-6f0b-4e8b-9c4e-0b5d1d150000 (กลับลำดับไบต์)
static const ble_uuid128_t U_SVC = BLE_UUID128_INIT(0x00, 0x00, 0x15, 0x1d, 0x5d, 0x0b, 0x4e, 0x9c, 0x8b, 0x4e, 0x0b, 0x6f, 0x00, 0x55, 0xd1, 0xd1);
static const ble_uuid128_t U_DATA = BLE_UUID128_INIT(0x00, 0x00, 0x15, 0x1d, 0x5d, 0x0b, 0x4e, 0x9c, 0x8b, 0x4e, 0x0b, 0x6f, 0x01, 0x55, 0xd1, 0xd1);
static const ble_uuid128_t U_CMD = BLE_UUID128_INIT(0x00, 0x00, 0x15, 0x1d, 0x5d, 0x0b, 0x4e, 0x9c, 0x8b, 0x4e, 0x0b, 0x6f, 0x02, 0x55, 0xd1, 0xd1);

enum : EventBits_t { EV_SYNC = 1, EV_SCAN_DONE = 2, EV_CONN = 4, EV_FAIL = 8, EV_DISC = 16, EV_READY = 32, EV_AUTH = 64 };
static EventGroupHandle_t ev;
static uint8_t ownType;
static volatile State st = OFF;
static volatile uint16_t conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t svcStart, svcEnd, hData, hCmd;
static volatile Msg msgCode = M_START;

// ---------------------------------------------------------------- จับคู่ (เหมือน c3pair.h)
static uint8_t tok[8];
static ble_addr_t saved;
static bool havePair = false;
static char c3name[16] = "";
static char c3fw[16] = "";
static volatile int8_t authRes = -1;
static volatile uint8_t nScreens = 0;
static volatile bool wantForget = false, wantOpen = false;
struct Rej { ble_addr_t a; int64_t t; };
static Rej rej[4];

static void nvsLoad() {
  nvs_handle_t h;
  if (nvs_open("c3pair", NVS_READWRITE, &h) != ESP_OK) return;
  size_t n = 8;
  if (nvs_get_blob(h, "tok", tok, &n) != ESP_OK || n != 8) {
    esp_fill_random(tok, 8);
    nvs_set_blob(h, "tok", tok, 8);
  }
  n = sizeof(saved);
  havePair = nvs_get_blob(h, "addr", &saved, &n) == ESP_OK && n == sizeof(saved);
  n = sizeof(c3name);
  if (nvs_get_str(h, "name", c3name, &n) != ESP_OK) c3name[0] = 0;
  nvs_commit(h);
  nvs_close(h);
}
static void nvsSavePair() {
  nvs_handle_t h;
  if (nvs_open("c3pair", NVS_READWRITE, &h) != ESP_OK) return;
  if (havePair) { nvs_set_blob(h, "addr", &saved, sizeof(saved)); nvs_set_str(h, "name", c3name); }
  else { nvs_erase_key(h, "addr"); nvs_erase_key(h, "name"); }
  nvs_commit(h);
  nvs_close(h);
}

static bool sameAddr(const ble_addr_t& a, const ble_addr_t& b) { return ble_addr_cmp(&a, &b) == 0; }
static bool rejected(const ble_addr_t& a) {
  int64_t now = esp_timer_get_time();
  for (auto& r : rej) if (r.t && sameAddr(r.a, a) && now - r.t < 60000000) return true;
  return false;
}
static void reject(const ble_addr_t& a) {
  int k = 0;
  for (int i = 1; i < 4; i++) if (rej[i].t < rej[k].t) k = i;
  rej[k].a = a;
  rej[k].t = esp_timer_get_time();
}

// ---------------------------------------------------------------- สแกน
struct Cand { ble_addr_t a; int rssi; int open; bool didi; };
static Cand cands[12];
static int nCand = 0;

static Cand* candOf(const ble_addr_t& a) {
  for (int i = 0; i < nCand; i++) if (sameAddr(cands[i].a, a)) return &cands[i];
  if (nCand >= 12) return nullptr;
  Cand* c = &cands[nCand++];
  memset(c, 0, sizeof(*c));
  c->a = a;
  c->open = -1;
  return c;
}

static void onAdv(const ble_gap_disc_desc& d) {
  ble_hs_adv_fields f;
  if (ble_hs_adv_parse_fields(&f, d.data, d.length_data) != 0) return;
  bool didi = false;
  int open = -1;
  for (int i = 0; i < f.num_uuids128; i++) if (ble_uuid_cmp(&f.uuids128[i].u, &U_SVC.u) == 0) didi = true;
  if (f.mfg_data && f.mfg_data_len >= 5 && f.mfg_data[0] == 0xFF && f.mfg_data[1] == 0xFF && f.mfg_data[2] == 'D' && f.mfg_data[3] == 'N') {
    didi = true;
    open = f.mfg_data[4] & 1;
  }
  if (f.name && f.name_len >= 7 && (!memcmp(f.name, "DIDI-C3", 7) || !memcmp(f.name, "DIDI-SL", 7) || (f.name_len == 12 && !memcmp(f.name, "Didissnap-C3", 12)))) didi = true;
  Cand* c = candOf(d.addr);
  if (!c) return;
  c->rssi = d.rssi;
  if (didi) c->didi = true;
  if (open >= 0) c->open = open;
}

// เลือก: จับคู่แล้ว = ตัวเดิมเท่านั้น · ยังไม่จับคู่ = ตัวที่เปิดโหมดจับคู่ สัญญาณแรงสุด
static Cand* pick() {
  Cand* best = nullptr;
  for (int i = 0; i < nCand; i++) {
    Cand& c = cands[i];
    if (!c.didi) continue;
    if (havePair) { if (sameAddr(c.a, saved)) return &c; continue; }
    if (rejected(c.a)) continue;
    if (c.open == 1 && (!best || c.rssi > best->rssi)) best = &c;
  }
  return best;
}

// ---------------------------------------------------------------- GATT
static int onWrite(uint16_t, const ble_gatt_error* e, ble_gatt_attr*, void*) {
  if (e->status != 0) ESP_LOGW(TAG, "write status %d", e->status);
  return 0;
}
static int onChr(uint16_t c, const ble_gatt_error* e, const ble_gatt_chr* chr, void*) {
  if (e->status == 0 && chr) {
    if (ble_uuid_cmp(&chr->uuid.u, &U_DATA.u) == 0) hData = chr->val_handle;
    else if (ble_uuid_cmp(&chr->uuid.u, &U_CMD.u) == 0) hCmd = chr->val_handle;
    return 0;
  }
  if (e->status == BLE_HS_EDONE && hData && hCmd) {
    static const uint8_t on[2] = {1, 0};                  // เปิดแจ้งเตือน (CCCD อยู่ถัดจากค่า)
    if (ble_gattc_write_flat(c, hData + 1, on, 2, onWrite, nullptr) == 0) { xEventGroupSetBits(ev, EV_READY); return 0; }
  }
  xEventGroupSetBits(ev, EV_FAIL);
  return 0;
}
static int onSvc(uint16_t c, const ble_gatt_error* e, const ble_gatt_svc* s, void*) {
  if (e->status == 0 && s) { svcStart = s->start_handle; svcEnd = s->end_handle; return 0; }
  if (e->status == BLE_HS_EDONE && svcStart) {
    if (ble_gattc_disc_all_chrs(c, svcStart, svcEnd, onChr, nullptr) == 0) return 0;
  }
  xEventGroupSetBits(ev, EV_FAIL);
  return 0;
}
static int onMtu(uint16_t, const ble_gatt_error*, uint16_t mtu, void*) {
  ESP_LOGI(TAG, "MTU %u", mtu);
  return 0;
}

static void onPacket(const uint8_t* d, int n) {
  if (n >= 6 && d[4] == DN_AUTHST && n >= (int)offsetof(DidiNowAuthSt, proto)) {
    DidiNowAuthSt a;
    memset(&a, 0, sizeof(a));
    memcpy(&a, d, (size_t)n < sizeof(a) ? (size_t)n : sizeof(a));
    a.c3[15] = 0;
    a.fw[15] = 0;
    if (a.c3[0]) strncpy(c3name, a.c3, sizeof(c3name) - 1);
    if (n >= (int)sizeof(a)) strncpy(c3fw, a.fw, sizeof(c3fw) - 1);
    nScreens = a.n;
    authRes = a.res;
    xEventGroupSetBits(ev, EV_AUTH);
    return;
  }
  vd::feed(d, n);
}

static int gapEvent(ble_gap_event* e, void*) {
  switch (e->type) {
    case BLE_GAP_EVENT_DISC: onAdv(e->disc); break;
    case BLE_GAP_EVENT_DISC_COMPLETE: xEventGroupSetBits(ev, EV_SCAN_DONE); break;
    case BLE_GAP_EVENT_CONNECT:
      if (e->connect.status == 0) {
        conn = e->connect.conn_handle;
        svcStart = svcEnd = hData = hCmd = 0;
        ble_gattc_exchange_mtu(conn, onMtu, nullptr);
        if (ble_gattc_disc_svc_by_uuid(conn, &U_SVC.u, onSvc, nullptr) != 0) xEventGroupSetBits(ev, EV_FAIL);
        xEventGroupSetBits(ev, EV_CONN);
      } else {
        xEventGroupSetBits(ev, EV_FAIL);
      }
      break;
    case BLE_GAP_EVENT_DISCONNECT:
      conn = BLE_HS_CONN_HANDLE_NONE;
      hData = hCmd = 0;
      xEventGroupSetBits(ev, EV_DISC | EV_FAIL);
      break;
    case BLE_GAP_EVENT_NOTIFY_RX: {
      static uint8_t buf[256];
      int n = OS_MBUF_PKTLEN(e->notify_rx.om);
      if (n > (int)sizeof(buf)) n = sizeof(buf);
      os_mbuf_copydata(e->notify_rx.om, 0, n, buf);
      onPacket(buf, n);
      break;
    }
    default: break;
  }
  return 0;
}

static void onSync() {
  ble_hs_util_ensure_addr(0);
  ble_hs_id_infer_auto(0, &ownType);
  xEventGroupSetBits(ev, EV_SYNC);
  bfile::onSync();
}
static void onReset(int reason) { ESP_LOGW(TAG, "host reset %d", reason); }
static void hostTask(void*) {
  nimble_port_run();
  nimble_port_freertos_deinit();
}

static bool sendAuth(uint8_t op) {
  if (conn == BLE_HS_CONN_HANDLE_NONE || !hCmd) return false;
  DidiNowAuth a;
  memset(&a, 0, sizeof(a));
  a.magic = DIDINOW_MAGIC;
  a.type = DN_AUTH;
  a.op = op;
  a.proto = DN_PROTO;
  memcpy(a.token, tok, 8);
  strncpy(a.name, "7B", sizeof(a.name) - 1);
  return ble_gattc_write_flat(conn, hCmd, &a, sizeof(a), onWrite, nullptr) == 0;
}

static void setStatus(State s, Msg m) {
  st = s;
  msgCode = m;
}

// งานหลัก: หา → ต่อ → ยืนยันตัว → เฝ้าดู
static void task(void*) {
  xEventGroupWaitBits(ev, EV_SYNC, pdFALSE, pdTRUE, portMAX_DELAY);
  for (;;) {
    if (wantForget && conn == BLE_HS_CONN_HANDLE_NONE) {
      wantForget = false;
      havePair = false;
      c3name[0] = 0;
      nvsSavePair();
    }
    // ---- สแกน
    setStatus(SEARCHING, havePair ? M_SEARCH_PAIRED : M_SEARCH_OPEN);
    nCand = 0;
    xEventGroupClearBits(ev, EV_SCAN_DONE);
    ble_gap_disc_params dp;
    memset(&dp, 0, sizeof(dp));
    dp.itvl = 0x60;
    dp.window = 0x50;
    dp.filter_duplicates = 1;
    if (ble_gap_disc(ownType, 3500, &dp, gapEvent, nullptr) != 0) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
    xEventGroupWaitBits(ev, EV_SCAN_DONE, pdTRUE, pdTRUE, pdMS_TO_TICKS(5000));
    Cand* c = pick();
    if (!c) {
      if (!havePair) setStatus(SEARCHING, M_NONE_OPEN);
      vTaskDelay(pdMS_TO_TICKS(800));
      continue;
    }
    // ---- ต่อ
    setStatus(CONNECTING, M_CONNECTING);
    ble_addr_t target = c->a;
    xEventGroupClearBits(ev, EV_CONN | EV_FAIL | EV_READY | EV_DISC | EV_AUTH);
    if (ble_gap_connect(ownType, &target, 5000, nullptr, gapEvent, nullptr) != 0) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
    EventBits_t b = xEventGroupWaitBits(ev, EV_READY | EV_FAIL, pdFALSE, pdFALSE, pdMS_TO_TICKS(9000));
    if (!(b & EV_READY) || (b & EV_FAIL)) {
      if (conn != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
      vTaskDelay(pdMS_TO_TICKS(1200));
      continue;
    }
    // ---- ยืนยันตัว
    setStatus(AUTH, M_AUTH);
    vTaskDelay(pdMS_TO_TICKS(150));
    authRes = -1;
    xEventGroupClearBits(ev, EV_AUTH);
    sendAuth(DN_AUTH_LOGIN);
    b = xEventGroupWaitBits(ev, EV_AUTH | EV_DISC, pdFALSE, pdFALSE, pdMS_TO_TICKS(2500));
    if (b & EV_DISC) continue;
    if (authRes == DN_AUTHR_DENIED) {
      reject(target);
      setStatus(SEARCHING, M_DENIED);
      ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
      vTaskDelay(pdMS_TO_TICKS(800));
      continue;
    }
    if (authRes >= 0 && (!havePair || !sameAddr(saved, target))) {
      saved = target;
      havePair = true;
      nvsSavePair();
    }
    setStatus(READY, authRes == DN_AUTHR_NEW ? M_READY_NEW : M_READY);
    ESP_LOGI(TAG, "ready: %s (res %d, %u screens)", c3name, authRes, nScreens);
    // ---- เฝ้าดู
    int64_t tAuth = esp_timer_get_time();
    while (conn != BLE_HS_CONN_HANDLE_NONE) {
      if (wantOpen) { wantOpen = false; sendAuth(DN_AUTH_OPEN); }
      if (wantForget) {
        sendAuth(DN_AUTH_FORGET);
        vTaskDelay(pdMS_TO_TICKS(300));
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
        break;
      }
      if (esp_timer_get_time() - tAuth > 20000000) { tAuth = esp_timer_get_time(); sendAuth(DN_AUTH_LOGIN); }   // ขอสถานะ (จำนวนจอ) เป็นระยะ
      vTaskDelay(pdMS_TO_TICKS(100));
    }
    xEventGroupWaitBits(ev, EV_DISC, pdTRUE, pdTRUE, pdMS_TO_TICKS(2000));
    setStatus(SEARCHING, M_LOST);
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

void begin() {
  ev = xEventGroupCreate();
  nvsLoad();
  if (nimble_port_init() != ESP_OK) { setStatus(OFF, M_BT_FAIL); return; }
  ble_hs_cfg.sync_cb = onSync;
  ble_hs_cfg.reset_cb = onReset;
  bfile::init();                     // รับไฟล์จากแอป (จอเป็นอุปกรณ์ต่อพ่วงด้วย · เปิดรับจากเมนูเท่านั้น)
  nimble_port_freertos_init(hostTask);
  xTaskCreatePinnedToCore(task, "link", 4096, nullptr, 3, nullptr, 0);
}

State state() { return st; }
Msg msg() { return msgCode; }
const char* c3Name() { return havePair ? c3name : ""; }
const char* c3Fw() { return c3fw; }
int screens() { return nScreens; }
bool send(const void* p, size_t n) {
  if (st != READY || conn == BLE_HS_CONN_HANDLE_NONE || !hCmd) return false;
  return ble_gattc_write_no_rsp_flat(conn, hCmd, p, n) == 0;
}
void forget() { wantForget = true; }
void openPairing() { wantOpen = true; }

}  // namespace slink
