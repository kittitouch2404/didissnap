import 'dart:async';
import 'dart:typed_data';

import 'package:flutter/services.dart';

import 'app_log.dart';
import 'car_data.dart';
import 'custom_pid.dart';
import 'elm_source.dart';
import 'hub.dart';
import 'native.dart';

/// แหล่งข้อมูล: ESP32-C3 ที่ช่อง OBD2 (อ่าน CAN / K-line เอง) ต่อทาง Bluetooth BLE
///  C3 ส่งค่ารถมาเอง ~30 ครั้ง/วินาที (DidiNowGw) · DTC / Custom PID / CVT TEMP ฝากให้ C3 ถามรถ
///  รูปแบบแพ็กเก็ตตรงกับ didinow.h (little-endian)
class C3Source implements CarSource {
  static const _ev = EventChannel('didissnap/c3');
  static const int _magic = 0x31444944; // "DID1"
  static const _proto = ['search', 'DEMO', 'CAN 500k', 'CAN 250k', 'K ISO9141', 'K KWP fast', 'K KWP slow'];

  DataHub? _hub;
  StreamSubscription<dynamic>? _sub;
  Timer? _watch, _cust;
  DateTime _last = DateTime.fromMillisecondsSinceEpoch(0);
  String _bleState = '';
  int _link = 1, _proto0 = 0, _rate = 0;
  String _info = '';
  int _myId = 0x2000;
  final Map<int, Completer<Uint8List>> _wait = {};
  bool _running = false;

  @override
  void start(DataHub hub) {
    _hub = hub;
    _running = true;
    _run();
  }

  Future<void> _run() async {
    final hub = _hub!;
    hub.setLink(this, LinkState.connecting, 'ขอสิทธิ์ Bluetooth...');
    if (!await ElmSource.ensurePermission()) {
      hub.setLink(this, LinkState.error, 'ไม่ได้รับสิทธิ์ Bluetooth (อนุญาต "อุปกรณ์ใกล้เคียง" ให้แอป)');
      return;
    }
    if (!_running) return;
    _sub = _ev.receiveBroadcastStream().listen(_onEvent);
    await Native.m.invokeMethod('c3Start');
    hub.setLink(this, LinkState.connecting, 'กำลังหา C3...');
    _watch = Timer.periodic(const Duration(milliseconds: 500), (_) => _status());
    _cust = Timer.periodic(const Duration(milliseconds: 700), (_) => _pollCustom());
  }

  void _onEvent(dynamic e) {
    if (e is String) {
      if (e != _bleState) AppLog.i('C3 BLE: $e');
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
        _info = String.fromCharCodes(e.sublist(5).takeWhile((c) => c != 0));
      case 8: // DN_RAWRES
        if (e.length >= 8) _complete(b.getUint16(6, Endian.little), e);
    }
  }

  void _gw(ByteData b, int n) {
    final hub = _hub;
    if (hub == null) return;
    final d = hub.data;
    double f(int o) => b.getFloat32(o, Endian.little);
    _link = b.getUint8(5);
    _proto0 = b.getUint8(6);
    final afrMode = b.getUint8(7);
    _rate = b.getUint16(10, Endian.little);
    _last = DateTime.now();
    if (_link != 2 && _link != 0) return; // ยังไม่ได้ค่ารถ
    final cal = hub.settings.speedCal;
    d.rpm = f(16);
    d.speedRaw = f(20);
    d.speed = d.speedRaw * cal;
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
    if (fresh && (_link == 2 || _link == 0)) {
      final p = _proto0 < _proto.length ? _proto[_proto0] : '?';
      hub.setLink(this, LinkState.connected, _link == 0 ? 'C3 DEMO (จำลอง)' : 'C3 · $p · $_rate/s');
    } else if (fresh) {
      hub.setLink(this, LinkState.connecting, _info.isEmpty ? 'C3 ต่อแล้ว — กำลังหารถ' : 'C3: $_info');
    } else if (_bleState == 'connected') {
      hub.setLink(this, LinkState.connecting, 'C3 ต่อแล้ว — รอค่า');
    } else {
      hub.setLink(this, LinkState.connecting, _bleState == 'Bluetooth off' ? 'เปิด Bluetooth ของเครื่องก่อน' : 'กำลังหา C3...');
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
    final ok = await Native.m.invokeMethod<bool>('c3Write', {'b': pkt}) ?? false;
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

  // ---------------- Custom PID / CVT TEMP: วนถามทีละช่อง
  int _ci = 0;
  bool _busy = false;
  Future<void> _pollCustom() async {
    final hub = _hub;
    if (hub == null || _busy || hub.state != LinkState.connected) return;
    final s = hub.settings, d = hub.data;
    final jobs = <(int, CustomPid?)>[];
    for (var k = 0; k < 4; k++) {
      final c = s.customPid(k);
      if (c.valid) jobs.add((k, c));
    }
    if (s.cvtTempOn && s.cvtReq.length >= 4) jobs.add((-1, null));
    if (jobs.isEmpty) return;
    _busy = true;
    try {
      final (k, c) = jobs[_ci++ % jobs.length];
      if (c != null) {
        final h = CustomPid.cleanHex(c.hdr);
        final cmd = CustomPid.cleanHex(c.cmd);
        final r = await raw(h.length == 3 && h != '7DF' ? int.parse(h, radix: 16) : 0, cmd);
        d.custRaw[k] = r;
        final bytes = ElmSource.parseUds(r, cmd);
        final v = bytes == null ? null : evalFormula(c.formula, bytes);
        if (v != null) {
          d.cust[k] = v;
          d.custAt[k] = DateTime.now();
        }
      } else {
        final h = s.cvtHdr;
        final r = await raw(h.length == 3 ? int.parse(h, radix: 16) : 0, s.cvtReq);
        d.cvtRaw = r;
        final b = ElmSource.parseUds(r, s.cvtReq);
        if (b != null && b.length > s.cvtByte) {
          final i = s.cvtByte;
          final v = ElmSource.cvtFormula(s.cvtFml, b[i], i + 1 < b.length ? b[i + 1] : 0);
          if (v > -40 && v < 200) {
            d.cvtTemp = v;
            d.cvtAt = DateTime.now();
          }
        }
      }
    } catch (e) {
      AppLog.e('C3 custom PID', e);
    } finally {
      _busy = false;
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

  @override
  Future<void> stop() async {
    _running = false;
    _watch?.cancel();
    _cust?.cancel();
    await _sub?.cancel();
    _sub = null;
    try {
      await Native.m.invokeMethod('c3Stop');
    } catch (_) {}
  }
}
