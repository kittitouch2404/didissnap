import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter/services.dart';

import 'app_log.dart';
import 'car_data.dart';
import 'c3_cal.dart';
import 'custom_pid.dart';
import 'c3_link.dart';
import 'elm_source.dart';
import 'hub.dart';
import 'native.dart';
import 'pids.dart';

/// แหล่งข้อมูล: ESP32-C3 ที่ช่อง OBD2 (อ่าน CAN / K-line เอง) ต่อทาง Bluetooth BLE
///  C3 ส่งค่ารถมาเอง ~30 ครั้ง/วินาที (DidiNowGw) · DTC / Custom PID / CVT TEMP ฝากให้ C3 ถามรถ
///  รูปแบบแพ็กเก็ตตรงกับ didinow.h (little-endian)
class C3Source implements CarSource {
  late final C3Link _tr; // BLE หรือ WiFi (ตามที่ตั้ง)
  static const int _magic = 0x31444944; // "DID1"
  static const _proto = ['search', 'DEMO', 'CAN 500k', 'CAN 250k', 'K ISO9141', 'K KWP fast', 'K KWP slow', 'CAN 500k 29bit', 'CAN 250k 29bit'];

  DataHub? _hub;
  StreamSubscription<dynamic>? _sub;
  Timer? _watch, _cust, _pidT;
  DateTime _last = DateTime.fromMillisecondsSinceEpoch(0);
  String _bleState = '';
  String _curAddr = '', _pairAddr = '';
  Timer? _authT;
  bool _authed = false;
  int _link = 1, _proto0 = 0, _rate = 0;
  String _info = '';
  int _myId = 0x2000;
  final Map<int, Completer<Uint8List>> _wait = {};
  bool _running = false;

  @override
  void start(DataHub hub) {
    _hub = hub;
    _tr = switch (hub.settings.c3Link) {
      1 => C3UdpLink(),
      2 => C3SppLink(hub.settings.c3SppAddr),
      _ => C3BleLink(),
    };
    hub.c3ota.writer = _tr is C3SppLink ? _tr.write : null; // OTA ทาง Bluetooth ธรรมดา ส่งผ่านช่องเดียวกัน
    _running = true;
    _run();
  }

  Future<void> _run() async {
    final hub = _hub!;
    if (!_tr.isWifi) {
      hub.setLink(this, LinkState.connecting, 'ขอสิทธิ์ Bluetooth...');
      if (!await BtTransport.ensurePermission()) {
        hub.setLink(this, LinkState.error, 'ไม่ได้รับสิทธิ์ Bluetooth (อนุญาต "อุปกรณ์ใกล้เคียง" ให้แอป)');
        return;
      }
    }
    if (!_running) return;
    _sub = _tr.events.listen(_onEvent);
    await _tr.start(hub.settings.c3PairAddr);
    _pairAddr = hub.settings.c3PairAddr;
    hub.setLink(this, LinkState.connecting, 'กำลังหา C3...');
    _watch = Timer.periodic(const Duration(milliseconds: 500), (_) => _status());
    _pidT = Timer.periodic(const Duration(milliseconds: 250), (_) => _pidTick());
    // ส่งค่าตั้ง Custom PID / CVT TEMP ให้ C3 (C3 ถามรถเองแล้วส่งค่าให้ทุกจอ) — ส่งซ้ำทุก 15 วิ กัน C3 รีบูต
    hub.settings.addListener(_cfgChanged);
    _cust = Timer.periodic(const Duration(seconds: 15), (_) => _sendCfg(force: true));
  }

  void _onEvent(dynamic e) {
    if (e is String) {
      if (e.startsWith('addr:')) {
        _curAddr = e.substring(5);
        return;
      }
      if (e != _bleState) AppLog.i('C3 BLE: $e');
      if (e == 'connected' && _bleState != 'connected') {
        // ยืนยันตัวกับ C3 ก่อน (จับคู่) — C3 เฟิร์มแวร์เก่าไม่ตอบใน 3 วิ = ใช้ได้ตามเดิม
        _authed = false;
        final pair = _hub?.c3pair;
        Timer(const Duration(milliseconds: 300), () {
          if (_bleState == 'connected' && pair != null) _write(pair.packet(0));
        });
        _authT?.cancel();
        _authT = Timer(const Duration(milliseconds: 3300), () {
          if (_bleState == 'connected' && !_authed) {
            AppLog.i('C3: no pairing reply (old firmware)');
            _hub?.c3pair.setLegacy();
            _linkUp();
          }
        });
      } else if (e != 'connected') {
        _authT?.cancel();
        _authed = false;
        _hub?.c3cal.sender = null;
        _hub?.c3pair.sender = null;
        _hub?.c3wifi.sender = null;
        _hub?.c3pair.linkChanged();
        if (_bleState == 'connected') _hub?.c3ota.onLink(false);
      }
      _bleState = e;
      _status();
      return;
    }
    if (e is! Uint8List || e.length < 6) return;
    final b = ByteData.sublistView(e);
    if (b.getUint32(0, Endian.little) != _magic) return;
    switch (e[4]) {
      case 4: // DN_GW
        if (e.length >= 64) _gw(b, e.length);
      case 3: // DN_DTC
        if (e.length >= 10) _complete(b.getUint16(8, Endian.little), e);
      case 6: // DN_GWINFO
        final info = String.fromCharCodes(e.sublist(5).takeWhile((c) => c != 0));
        if (info != _info) AppLog.i('C3: $info'); // เก็บทุกข้อความตรวจสาย (ดูย้อนหลังใน "ส่ง log ปัญหา")
        _info = info;
      case 8: // DN_RAWRES
        if (e.length >= 8) _complete(b.getUint16(6, Endian.little), e);
      case 15: // DN_OTAST: สถานะอัปเดตเฟิร์มแวร์ / เวอร์ชัน
        _hub?.c3ota.onStatus(e);
        _hub?.updater.setFam(_hub!.c3ota.fwVer);
      case 13: // DN_CAL: ค่าปรับ gain/offset ปัจจุบันใน C3
        if (e.length >= 112 && e[5] == 0) _calIn(b);
      case 10: // DN_CUST: ค่าของทุกช่อง
        if (e.length >= 28) _custVals(b);
      case 12: // DN_CUSTRAW: คำตอบดิบล่าสุดของช่อง (โชว์ในหน้าทดสอบ)
        if (e.length >= 7) _custRaw(e);
      case 17: // DN_AUTHST: ผลจับคู่
        _authIn(e);
      case 19: // DN_WIFIST: สถานะ WiFi ของ C3
        _hub?.c3wifi.onStatus(e);
    }
  }

  // ---------------- จับคู่ C3
  void _authIn(Uint8List e) {
    final hub = _hub;
    if (hub == null) return;
    final r = hub.c3pair.onStatus(e, _curAddr);
    if (r == 0) {
      // C3 ตัวนี้จับคู่กับจออื่น (คันอื่น) → ตัดแล้วหาตัวอื่น
      AppLog.i('C3 $_curAddr denied (paired with another car)');
      _tr.reject();
      return;
    }
    if (r > 0 && !_authed) {
      AppLog.i('C3 ${hub.c3pair.c3}: ${r == 2 ? 'paired NEW' : 'paired ok'} (${hub.c3pair.screens.join(', ')})');
      _pairAddr = hub.settings.c3PairAddr;
      _linkUp();
    }
    hub.updater.setFam(hub.c3pair.fw);
  }

  void _linkUp() {
    if (_authed) return;
    _authed = true;
    _authT?.cancel();
    Timer(const Duration(milliseconds: 200), () => _sendCfg(force: true));
    _hub?.c3cal.sender = _write;
    _hub?.c3pair.sender = _write;
    _hub?.c3wifi.sender = _write;
    _hub?.c3wifi.viaWifi = _tr.isWifi;
    _hub?.c3wifi.query();
    _hub?.c3pair.linkChanged();
    _hub?.c3ota.onLink(!_tr.isWifi); // อัปเดตเฟิร์มแวร์ได้เฉพาะทาง Bluetooth
    _hub?.updater.checkC3(); // เทียบเวอร์ชันเฟิร์มแวร์ C3 กับตัวล่าสุดบน GitHub
  }

  /// เปลี่ยน/ลืม C3 ที่จับคู่ → บอกตัวต่อ BLE
  void _pairChanged() {
    final a = _hub?.settings.c3PairAddr ?? '';
    if (a == _pairAddr) return;
    _pairAddr = a;
    _tr.setPair(a);
  }

  void _gw(ByteData b, int n) {
    final hub = _hub;
    if (hub == null) return;
    final d = hub.data;
    double f(int o) => b.getFloat32(o, Endian.little);
    final link = b.getUint8(5);
    if (link == 2 && _link != 2) {
      // เพิ่งต่อรถได้ (อาจเป็นรถคันใหม่) → อ่านรายการ PID ใหม่
      d.supportedPids.clear();
      d.pidBytes.clear();
      _pidMaskTry = 0;
    }
    _link = link;
    _proto0 = b.getUint8(6);
    final afrMode = b.getUint8(7);
    _rate = b.getUint16(10, Endian.little);
    _last = DateTime.now();
    if (_link != 2 && _link != 0) return; // ยังไม่ได้ค่ารถ
    // C3 ปรับค่าให้แล้ว (gain/offset) · ความเร็วดิบ = ถอดเกนของ C3 ออก (ไว้เทียบ GPS)
    final cal = hub.c3cal;
    final gs = cal.have && cal.g[C3Cal.iSpd] > 0.2 ? cal.g[C3Cal.iSpd] : 1.0;
    d.rpm = f(16);
    d.speed = f(20);
    d.speedRaw = d.speed / gs;
    d.ect = f(24);
    d.iat = f(28);
    final v = f(32);
    if (v > 1) d.volt = v;
    d.load = f(36);
    d.tps = f(40);
    final map = f(44);
    if (map > 0) d.map = map;
    final maf = f(48);
    if (maf > 0) {
      d.maf = maf;
      d.mafSample(hub.settings.afrStoich10 / 10.0, 745);
    }
    d.fuel = f(52);
    final stft = f(56);
    d.pidBytes[0x06] = [((stft * 128 / 100) + 128).round().clamp(0, 255)];
    final afr = f(60);
    if ((afrMode == 1 || afrMode == 3) && afr > 5) d.lambda = afr / 14.7;
    if (n >= 72) {
      final t = f(64), o = f(68);
      if (t > -100) d.timing = t;
      if (o > -100) d.oil = o;
    }
  }

  void _status() {
    final hub = _hub;
    if (hub == null || !_running) return;
    final fresh = DateTime.now().difference(_last).inMilliseconds < 1500;
    if (hub.dynoActive && _bleState == 'connected') {
      // วัดไดโน: ให้ C3 ถามแค่รอบ/ความเร็ว/TPS (ต่ออายุทุก 1 วิ — C3 หมดอายุเองใน 2.5 วิ)
      final now = DateTime.now();
      if (now.difference(_dynoSent).inMilliseconds >= 900) {
        _dynoSent = now;
        _tr.write(_cmd(5, 0));
      }
    }
    if (hub.c3ota.running) {
      hub.setLink(this, LinkState.connecting, 'กำลังอัปเดตเฟิร์มแวร์ C3 ${(hub.c3ota.progress * 100).round()}%');
    } else if (fresh && (_link == 2 || _link == 0)) {
      final p = _proto0 < _proto.length ? _proto[_proto0] : '?';
      hub.setLink(this, LinkState.connected, _link == 0 ? 'C3 DEMO (จำลอง)' : 'C3 · $p · $_rate/s');
    } else if (fresh) {
      hub.setLink(this, LinkState.connecting, _info.isEmpty ? 'C3 ต่อแล้ว — กำลังหารถ' : 'C3: $_info');
    } else if (_bleState == 'connected') {
      hub.setLink(this, LinkState.connecting, _authed ? 'C3 ต่อแล้ว — รอค่า' : 'C3 ต่อแล้ว — กำลังยืนยันการจับคู่');
    } else {
      final denied = DateTime.now().difference(hub.c3pair.deniedAt).inSeconds < 20;
      hub.setLink(
          this,
          LinkState.connecting,
          _bleState == 'Bluetooth off'
              ? 'เปิด Bluetooth ของเครื่องก่อน'
              : _bleState == 'searching C3 (wifi)'
              ? (hub.settings.c3PairAddr.isEmpty
                  ? 'กำลังหา C3 ทาง WiFi... (จอกับ C3 ต้องต่อฮอตสปอตเดียวกัน · C3 ต้องรอจับคู่)'
                  : 'กำลังหา C3 ทาง WiFi... (จอกับ C3 ต้องต่อฮอตสปอตเดียวกัน)')
              : _bleState == 'wifi error'
              ? 'เปิดช่อง WiFi ไม่ได้ — ปิดแล้วเปิดแอปใหม่'
              : _bleState == 'location off'
              ? 'จอนี้เป็น Android 11 หรือต่ำกว่า: ต้องเปิด "ตำแหน่ง (Location)" ในตั้งค่าเครื่อง ถึงจะหา C3 เจอ'
              : _bleState == 'no location permission'
              ? 'อนุญาต "ตำแหน่ง" ให้แอปนี้ก่อน (Android 11 หรือต่ำกว่าต้องใช้หา Bluetooth BLE) แล้วกด เชื่อมต่อใหม่'
              : _bleState == 'spp no device'
              ? 'ยังไม่ได้เลือกตัว Superlink — จับคู่ "DIDI-SL-..." ในตั้งค่า Bluetooth ของเครื่อง แล้วกด "เลือกอุปกรณ์" ในหน้าการเชื่อมต่อ'
              : _bleState == 'spp connecting'
              ? 'กำลังต่อ Bluetooth ${hub.settings.c3SppName.isEmpty ? hub.settings.c3SppAddr : hub.settings.c3SppName}...'
              : _bleState.startsWith('spp failed')
              ? 'ต่อ Bluetooth ไม่ได้ — เสียบ Superlink อยู่ไหม? (ESP32 รุ่นแรกเท่านั้น · C3 ใช้ Bluetooth ธรรมดาไม่ได้) จะลองใหม่เรื่อยๆ'
              : _bleState == 'no BLE'
              ? 'จอนี้ไม่มี Bluetooth BLE ให้แอปใช้ — ใช้ OBD2 ELM327 WiFi หรือ ESP32-Superlink WiFi แทน'
              : denied
                  ? 'C3 ที่เจอเป็นของคันอื่น — กำลังหาตัวของเรา'
                  : _bleState == 'no C3 in pairing mode'
                      ? 'ไม่เจอ C3 ที่รอจับคู่ — ถอดเสียบ C3 ใหม่ หรือกด "เพิ่มจอใหม่" จากจอที่จับคู่แล้ว'
                      : hub.settings.c3PairAddr.isEmpty
                          ? 'กำลังหา C3 เพื่อจับคู่...'
                          : 'กำลังหา C3 (${hub.settings.c3PairName.isEmpty ? hub.settings.c3PairAddr : hub.settings.c3PairName})...');
    }
  }

  // ---------------- คำขอที่รอคำตอบ (DTC / คำสั่งดิบ)
  void _complete(int id, Uint8List e) {
    final c = _wait.remove(id);
    if (c != null && !c.isCompleted) c.complete(e);
  }

  Future<Uint8List?> _ask(Uint8List pkt, int id, Duration tmo) async {
    final c = Completer<Uint8List>();
    _wait[id] = c;
    final ok = await _tr.write(pkt);
    if (!ok) {
      _wait.remove(id);
      return null;
    }
    try {
      return await c.future.timeout(tmo);
    } on TimeoutException {
      _wait.remove(id);
      return null;
    }
  }

  Uint8List _cmd(int cmd, int id) {
    final b = ByteData(8);
    b.setUint32(0, _magic, Endian.little);
    b.setUint8(4, 5); // DN_GWCMD
    b.setUint8(5, cmd);
    b.setUint16(6, id, Endian.little);
    return b.buffer.asUint8List();
  }

  /// คำสั่งดิบ -> คำตอบเป็นข้อความ hex แบบ ELM (เช่น "620F1078") ให้ parseUds ใช้ต่อ
  Future<String> raw(int hdr, String hex) async {
    final clean = CustomPid.cleanHex(hex);
    final n = (clean.length ~/ 2).clamp(1, 7);
    final id = (++_myId) & 0xFFFF;
    final b = ByteData(17);
    b.setUint32(0, _magic, Endian.little);
    b.setUint8(4, 7); // DN_RAWREQ
    b.setUint8(5, n);
    b.setUint16(6, id, Endian.little);
    b.setUint16(8, hdr, Endian.little);
    for (var i = 0; i < n; i++) {
      b.setUint8(10 + i, int.parse(clean.substring(i * 2, i * 2 + 2), radix: 16));
    }
    final r = await _ask(b.buffer.asUint8List(), id, const Duration(seconds: 3));
    if (r == null || r.length < 8) return 'TIMEOUT';
    final cnt = ByteData.sublistView(r).getInt8(5);
    if (cnt == -2) return 'K-LINE: ใช้ header อื่นไม่ได้';
    if (cnt <= 0) return 'NO DATA';
    return [for (var i = 0; i < cnt && 8 + i < r.length; i++) r[8 + i].toRadixString(16).padLeft(2, '0').toUpperCase()].join();
  }

  // ---------------- หน้า "ค่า OBD2 ทั้งหมด": ถามทุก PID ที่รถรองรับผ่าน C3 (เฉพาะตอนเปิดหน้านั้น ไม่ถ่วงค่าหลัก)
  DateTime _dynoSent = DateTime.fromMillisecondsSinceEpoch(0);
  bool _pidBusy = false;
  int _pidIdx = 0, _pidMaskTry = 0;

  static List<int>? _hexBytes(String r) {
    if (r.isEmpty || r.length.isOdd || !RegExp(r'^[0-9A-F]+$').hasMatch(r)) return null;
    return [for (var i = 0; i < r.length; i += 2) int.parse(r.substring(i, i + 2), radix: 16)];
  }

  Future<void> _pidTick() async {
    final hub = _hub;
    if (hub == null || !_running || _pidBusy || hub.pidPageViews <= 0 || _link != 2) return;
    _pidBusy = true;
    try {
      final d = hub.data;
      if (d.supportedPids.isEmpty) {
        if (_pidMaskTry++ > 5) return; // รถไม่ตอบรายการ PID
        // 0100, 0120, 0140 ... อ่านต่อเมื่อบิตสุดท้ายบอกว่ามีชุดถัดไป
        for (var base = 0; base <= 0xC0; base += 0x20) {
          final b = _hexBytes(await raw(0, '01${base.toRadixString(16).padLeft(2, '0')}'));
          if (b == null || b.length < 6 || b[0] != 0x41 || b[1] != base) break;
          final mask = (b[2] << 24) | (b[3] << 16) | (b[4] << 8) | b[5];
          for (var i = 0; i < 32; i++) {
            if ((mask >> (31 - i)) & 1 == 1) d.supportedPids.add(base + i + 1);
          }
          if (mask & 1 == 0) break;
        }
        return;
      }
      final ids = d.supportedPids.where((p) => !isBitmapPid(p)).toList()..sort();
      if (ids.isEmpty) return;
      for (var k = 0; k < 3 && hub.pidPageViews > 0; k++) {
        final p = ids[_pidIdx++ % ids.length];
        final b = _hexBytes(await raw(0, '01${p.toRadixString(16).padLeft(2, '0')}'));
        if (b != null && b.length >= 3 && b[0] == 0x41 && b[1] == p) d.pidBytes[p] = b.sublist(2);
      }
    } finally {
      _pidBusy = false;
    }
  }

  // ---------------- Custom PID / CVT TEMP: C3 ถามเอง
  String _cfgSig = '';
  Timer? _cfgDeb;
  void _cfgChanged() {
    _pairChanged();
    // พิมพ์ header/คำสั่งทีละตัว → รอให้หยุดพิมพ์ก่อนค่อยส่ง (ไม่ยิง BLE ทุกตัวอักษร)
    _cfgDeb?.cancel();
    _cfgDeb = Timer(const Duration(milliseconds: 600), _sendCfg);
    _speedPush();
  }

  Future<bool> _write(Uint8List p) => _tr.write(p);

  // ---------------- ชดเชยความเร็วของแอป (ตั้งเอง / GPS) = SPEED GAIN ใน C3 (ค่าเดียวกันทุกจอ)
  void _calIn(ByteData b) {
    final hub = _hub;
    if (hub == null) return;
    final cal = hub.c3cal;
    final first = !cal.have;
    if (!cal.fromPacket(b)) return;
    final s = hub.settings;
    final gs = cal.g[C3Cal.iSpd];
    if ((s.speedCal - gs).abs() < 0.0005) return;
    final auto = s.speedCalMode == 1 && s.speedCalAutoN >= 20;
    // C3 ยังไม่เคยตั้ง แต่แอปตั้งไว้ → ย้ายค่าของแอปไป C3 · โหมด GPS อัตโนมัติ → แอปเป็นคนกำหนด
    if (auto || (first && cal.ver == 0 && (s.speedCal - 1).abs() > 0.0005)) {
      _speedPush();
    } else {
      s.speedCal1000 = (gs * 1000).round(); // ปรับจากจอ S3 / หน้าตั้งค่า C3 → ใช้ค่าของ C3
    }
  }

  void _speedPush() {
    final cal = _hub?.c3cal;
    if (cal == null || !cal.have || !cal.connected) return;
    final v = _hub!.settings.speedCal;
    if ((v - cal.g[C3Cal.iSpd]).abs() < 0.0005) return;
    cal.setGain(C3Cal.iSpd, v);
  }

  static String _ascii(String t, int n) {
    final a = t.replaceAll('°', '').replaceAll(RegExp(r'[^\x20-\x7E]'), '').trim();
    return a.length > n ? a.substring(0, n) : a;
  }

  /// สูตร CVT TEMP แบบเลือกจากเมนู -> สูตรตัวอักษร (เลื่อน A/B ตามไบต์ที่เลือก)
  static String _cvtFml(int f, int byte) {
    // ชื่อไบต์แบบ Torque: 0-25 = A-Z · 26-51 = AA-AZ
    String L(int k) => k < 26 ? String.fromCharCode(65 + k) : 'A${String.fromCharCode(65 + k - 26)}';
    final k = byte.clamp(0, 50);
    final a = L(k), bb = L(k + 1);
    return switch (f) {
      1 => a,
      2 => '($a*256+$bb)/10-40',
      3 => '(($a*256+$bb)-2731)/10',
      4 => '$a-50',
      5 => '($a*9/5-58-32)*5/9',
      _ => '$a-40',
    };
  }

  Uint8List _custPkt(int slot, bool on, int hdr, String cmdHex, int dec, double lo, double hi, String name, String unit, String fml) {
    final b = ByteData(84);
    b.setUint32(0, _magic, Endian.little);
    b.setUint8(4, 9); // DN_CUSTSET
    b.setUint8(5, slot);
    final cmd = CustomPid.cleanHex(cmdHex);
    final n = (cmd.length ~/ 2).clamp(0, 7);
    b.setUint8(6, on && n > 0 ? 1 : 0);
    b.setUint8(7, n);
    b.setUint16(8, hdr, Endian.little);
    for (var i = 0; i < n; i++) {
      b.setUint8(10 + i, int.parse(cmd.substring(i * 2, i * 2 + 2), radix: 16));
    }
    b.setUint8(17, dec.clamp(0, 2));
    b.setFloat32(18, lo, Endian.little);
    b.setFloat32(22, hi, Endian.little);
    void str(int off, int len, String t) {
      final u = ascii.encode(_ascii(t, len - 1));
      for (var i = 0; i < u.length; i++) {
        b.setUint8(off + i, u[i]);
      }
    }
    str(26, 12, name);
    str(38, 6, unit);
    str(44, 40, fml.toUpperCase());
    return b.buffer.asUint8List();
  }

  List<Uint8List> _cfgPkts() {
    final s = _hub!.settings;
    final out = <Uint8List>[];
    for (var k = 0; k < 4; k++) {
      final c = s.customPid(k);
      out.add(_custPkt(k, c.valid, encodeHdr(c.hdr) ?? 0, c.cmd, c.dec, c.lo, c.hi,
          c.label, c.unit, c.formula));
    }
    final h = encodeHdr(s.cvtHdr);
    out.add(_custPkt(4, s.cvtTempOn && s.cvtReq.length >= 4 && h != null, h ?? 0, s.cvtReq, 0, 40,
        120, 'CVT TEMP', 'C', _cvtFml(s.cvtFml, s.cvtByte)));
    return out;
  }

  Future<void> _sendCfg({bool force = false}) async {
    if (_hub == null || !_running || _bleState != 'connected') return;
    final pk = _cfgPkts();
    final sig = pk.map((p) => base64.encode(p)).join();
    if (!force && sig == _cfgSig) return;
    _cfgSig = sig;
    for (final p in pk) {
      await _tr.write(p);
      await Future<void>.delayed(const Duration(milliseconds: 40));
    }
  }

  void _custVals(ByteData b) {
    final d = _hub?.data;
    if (d == null) return;
    final have = b.getUint8(5);
    final now = DateTime.now();
    for (var k = 0; k < 5; k++) {
      if ((have >> k) & 1 == 0) continue;
      final v = b.getFloat32(8 + k * 4, Endian.little);
      if (k < 4) {
        d.cust[k] = v;
        d.custAt[k] = now;
      } else {
        d.cvtTemp = v;
        d.cvtAt = now;
      }
    }
  }

  void _custRaw(Uint8List e) {
    final d = _hub?.data;
    if (d == null) return;
    final slot = e[5];
    final n = ByteData.sublistView(e).getInt8(6);
    final txt = n == -2
        ? 'K-LINE: ใช้ header อื่นไม่ได้'
        : n <= 0
            ? 'NO DATA'
            : [for (var i = 0; i < n && 7 + i < e.length; i++) e[7 + i].toRadixString(16).padLeft(2, '0').toUpperCase()].join();
    if (slot < 4) {
      d.custRaw[slot] = txt;
    } else if (slot == 4) {
      d.cvtRaw = txt;
    }
  }

  // ---------------- DTC
  Future<Uint8List?> _dtc(int cmd) => _ask(_cmd(cmd, (++_myId) & 0xFFFF), _myId & 0xFFFF, const Duration(seconds: 12));

  @override
  Future<List<String>> readDtc() async {
    final r = await _dtc(1);
    if (r == null) throw Exception('C3 ไม่ตอบ');
    if (r[5] != 1) throw Exception('อ่านโค้ดไม่สำเร็จ');
    final n = r[6].clamp(0, 16);
    return [
      for (var i = 0; i < n && 10 + i * 6 + 5 <= r.length; i++)
        String.fromCharCodes(r.sublist(10 + i * 6, 10 + i * 6 + 6).takeWhile((c) => c != 0)),
    ];
  }

  @override
  Future<bool> clearDtc() async {
    final r = await _dtc(2);
    return r != null && r[5] == 1;
  }

  bool get _isCanProto => _proto0 == 2 || _proto0 == 3 || _proto0 == 7 || _proto0 == 8;

  @override
  Future<List<String>> readPending() async {
    final r = await raw(0, '07');
    if (!RegExp(r'^47[0-9A-F]*$').hasMatch(r)) return const [];
    return ElmSource.parseDtc(r, _isCanProto, '47');
  }

  @override
  Future<String?> obdRaw(String hex) async {
    final r = await raw(0, hex);
    return RegExp(r'^[0-9A-F]+$').hasMatch(r) ? r : null;
  }

  @override
  Future<void> stop() async {
    _running = false;
    _hub?.settings.removeListener(_cfgChanged);
    _hub?.c3cal.sender = null;
    _hub?.c3ota.onLink(false);
    _watch?.cancel();
    _cust?.cancel();
    _cfgDeb?.cancel();
    _pidT?.cancel();
    _authT?.cancel();
    _hub?.c3pair.sender = null;
    _hub?.c3wifi.sender = null;
    await _sub?.cancel();
    _sub = null;
    await _tr.stop();
  }
}
