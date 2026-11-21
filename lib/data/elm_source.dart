import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/services.dart';

import 'car_data.dart';
import 'hub.dart';
import 'native.dart';
import 'pids.dart';

// ======================================================================
//  Transport: ท่อส่งข้อความไป/กลับ ELM327 (Bluetooth หรือ WiFi)
// ======================================================================

abstract class ElmTransport {
  Stream<String> get incoming;
  Future<void> open();
  Future<void> write(String s);
  Future<void> close();
}

class BtDevice {
  const BtDevice(this.name, this.address);
  final String name;
  final String address;
}

/// Bluetooth Classic (SPP) ผ่านโค้ด Kotlin
class BtTransport implements ElmTransport {
  BtTransport(this.address);
  final String address;

  final _ctrl = StreamController<String>.broadcast();
  StreamSubscription<dynamic>? _sub;
  int _gen = -1;

  static Future<bool> ensurePermission() async =>
      (await Native.m.invokeMethod<bool>('ensurePermission')) ?? false;

  /// รายการอุปกรณ์ที่จับคู่ (pair) ไว้แล้วในมือถือ
  static Future<List<BtDevice>> bonded() async {
    if (!await ensurePermission()) {
      throw Exception('ไม่ได้รับสิทธิ์ Bluetooth (อนุญาต "อุปกรณ์ใกล้เคียง" ให้แอป)');
    }
    final raw =
        await Native.m.invokeMethod<List<dynamic>>('bonded') ?? const [];
    return raw
        .whereType<Map>()
        .map((m) => BtDevice('${m['name']}', '${m['address']}'))
        .toList();
  }

  @override
  Stream<String> get incoming => _ctrl.stream;

  @override
  Future<void> open() async {
    if (address.isEmpty) {
      throw Exception('ยังไม่ได้เลือกอุปกรณ์ Bluetooth (MENU → การเชื่อมต่อ)');
    }
    if (!await ensurePermission()) {
      throw Exception('ไม่ได้รับสิทธิ์ Bluetooth');
    }
    _sub = Native.btStream.receiveBroadcastStream().listen((ev) {
      if (ev is! Map) return;
      if (ev['g'] != _gen) return;
      if (ev['t'] == 'data') {
        _ctrl.add(ev['d'] as String);
      } else if (ev['t'] == 'closed') {
        _ctrl.addError(Exception('Bluetooth หลุดการเชื่อมต่อ'));
      }
    });
    try {
      final g = await Native.m.invokeMethod<int>('connect', {'address': address});
      _gen = g ?? -1;
    } on PlatformException catch (e) {
      throw Exception('ต่อ Bluetooth ไม่ได้: ${e.message}');
    }
  }

  @override
  Future<void> write(String s) async {
    try {
      await Native.m.invokeMethod('write', {'data': s});
    } on PlatformException catch (e) {
      throw Exception('ส่งข้อมูล Bluetooth ไม่ได้: ${e.message}');
    }
  }

  @override
  Future<void> close() async {
    await _sub?.cancel();
    _sub = null;
    try {
      await Native.m.invokeMethod('disconnect');
    } catch (_) {}
  }
}

/// ELM327 แบบ WiFi (TCP)
class TcpTransport implements ElmTransport {
  TcpTransport(this.host, this.port);
  final String host;
  final int port;
  Socket? _s;
  final _ctrl = StreamController<String>.broadcast();

  @override
  Stream<String> get incoming => _ctrl.stream;

  @override
  Future<void> open() async {
    await Native.bindWifi();
    final s = await Socket.connect(host, port,
        timeout: const Duration(seconds: 5));
    s.setOption(SocketOption.tcpNoDelay, true);
    _s = s;
    s.listen(
      (d) => _ctrl.add(latin1.decode(d, allowInvalid: true)),
      onError: (Object e) => _ctrl.addError(e),
      onDone: () => _ctrl.addError(Exception('WiFi หลุดการเชื่อมต่อ')),
    );
  }

  @override
  Future<void> write(String s) async {
    final sock = _s;
    if (sock == null) throw Exception('ยังไม่ได้เชื่อมต่อ');
    sock.add(latin1.encode(s));
    await sock.flush();
  }

  @override
  Future<void> close() async {
    final s = _s;
    _s = null;
    s?.destroy();
  }
}

// ======================================================================
//  ElmSession: ส่งคำสั่งทีละคำสั่ง แล้วรอจนเจอ prompt '>'
// ======================================================================

class ElmSession {
  ElmSession(this.t);
  final ElmTransport t;

  final StringBuffer _buf = StringBuffer();
  Completer<String>? _pending;
  StreamSubscription<String>? _sub;
  bool dead = false;
  Object? deadReason;

  Future<void> open() async {
    _sub = t.incoming.listen(_onData, onError: _fail);
    await t.open();
  }

  void _onData(String s) {
    _buf.write(s);
    final str = _buf.toString();
    final i = str.indexOf('>');
    if (i < 0) return;
    _buf.clear();
    _buf.write(str.substring(i + 1));
    final p = _pending;
    _pending = null;
    if (p != null && !p.isCompleted) p.complete(str.substring(0, i));
  }

  void _fail(Object e) {
    dead = true;
    deadReason = e;
    final p = _pending;
    _pending = null;
    if (p != null && !p.isCompleted) p.completeError(e);
  }

  Future<String> cmd(String c,
      {Duration timeout = const Duration(seconds: 3)}) async {
    if (dead) throw deadReason ?? StateError('closed');
    _buf.clear();
    final comp = Completer<String>();
    _pending = comp;
    await t.write('$c\r');
    try {
      final raw = await comp.future.timeout(timeout);
      return _clean(raw, c);
    } on TimeoutException {
      if (identical(_pending, comp)) _pending = null;
      rethrow;
    }
  }

  static String _clean(String raw, String c) {
    final echo = c.replaceAll(' ', '').toUpperCase();
    final s = raw
        .replaceAll('\r', '\n')
        .toUpperCase()
        .replaceAll('SEARCHING...', '');
    return s
        .split('\n')
        .map((l) => l.trim())
        .where((l) => l.isNotEmpty && l.replaceAll(' ', '') != echo)
        .join('\n');
  }

  Future<void> close() async {
    dead = true;
    await _sub?.cancel();
    try {
      await t.close();
    } catch (_) {}
  }
}

// ======================================================================
//  ElmSource: เริ่มต้น ELM327 แล้ววนอ่านค่า PID
// ======================================================================

class ElmSource implements CarSource {
  ElmSource(this.label, this.makeTransport);
  final String label;
  final ElmTransport Function() makeTransport;

  DataHub? _hub;
  bool _running = false;
  ElmSession? _ses;
  bool _isCan = true;
  bool _oneResp = true;

  /// คำสั่งขอหลายค่าในครั้งเดียว (เฉพาะรถ CAN) เช่น "010C0D11040B341"  null = อ่านทีละค่า
  String? _multi;

  /// จำนวนไบต์ข้อมูลของแต่ละ PID (ใช้แยกคำตอบแบบหลายค่า)
  static const Map<int, int> _pidLen = {
    0x04: 1, 0x05: 1, 0x06: 1, 0x0B: 1, 0x0C: 2, 0x0D: 1, 0x0F: 1,
    0x10: 2, 0x11: 1, 0x24: 4, 0x2F: 1, 0x34: 4,
  };

  /// แยกคำตอบหลายค่า เช่น "0:410C1AF80D32\n1:11FF04CC0B9634..." -> {0x0C:[..], 0x0D:[..], ...}
  static Map<int, List<int>> parseMulti(String r) {
    final sb = StringBuffer();
    for (var line in r.split('\n')) {
      line = line.replaceAll(' ', '').trim();
      final c = line.indexOf(':');
      if (c >= 0 && c <= 2) line = line.substring(c + 1);          // "0:" "1:" ของเฟรมต่อเนื่อง
      if (line.length == 3 && int.tryParse(line, radix: 16) != null) continue;   // บรรทัดบอกความยาว
      sb.write(line);
    }
    final h = sb.toString();
    final out = <int, List<int>>{};
    var i = h.indexOf('41');
    if (i < 0) return out;
    int? hx(int at) => at + 2 <= h.length ? int.tryParse(h.substring(at, at + 2), radix: 16) : null;
    while (i + 2 <= h.length) {
      if (hx(i) == 0x41) {                                   // หัว 41 ของแต่ละคำตอบ (ECU ตอบแยกเป็นชุด ๆ)
        i += 2;
        continue;
      }
      final pid = hx(i);
      final n = pid == null ? null : _pidLen[pid];
      if (pid == null || n == null || i + 2 + n * 2 > h.length) break;
      final b = <int>[];
      for (var k = 0; k < n; k++) {
        b.add(hx(i + 2 + k * 2)!);
      }
      out[pid] = b;
      i += 2 + n * 2;
    }
    return out;
  }
  Future<void> _lock = Future<void>.value();

  /// ให้คำสั่งวิ่งทีละคำสั่ง (การอ่านค่ากับการอ่าน DTC จะไม่ชนกัน)
  Future<T> _x<T>(Future<T> Function() f) {
    final prev = _lock;
    final done = Completer<void>();
    _lock = done.future;
    return prev.then((_) => f()).whenComplete(() => done.complete());
  }

  void _set(LinkState s, String m) => _hub?.setLink(this, s, m);

  @override
  void start(DataHub hub) {
    _hub = hub;
    _running = true;
    unawaited(_loop());
  }

  @override
  Future<void> stop() async {
    _running = false;
    await _ses?.close();
  }

  Future<void> _loop() async {
    while (_running) {
      try {
        await _session();
      } catch (e) {
        if (_running) {
          _set(LinkState.error,
              e is TimeoutException ? 'ELM327 ไม่ตอบสนอง (timeout)' : errText(e));
        }
      }
      final s = _ses;
      _ses = null;
      await s?.close();
      if (!_running) break;
      await Future<void>.delayed(const Duration(seconds: 3));
    }
  }

  Future<void> _session() async {
    final hub = _hub!;
    _set(LinkState.connecting, 'กำลังเชื่อมต่อ ELM327 ($label)...');
    final ses = ElmSession(makeTransport());
    _ses = ses;
    await ses.open();
    if (!_running) return;

    _set(LinkState.connecting, 'เริ่มต้น ELM327...');
    try {
      await _x(() => ses.cmd('ATZ', timeout: const Duration(seconds: 5)));
    } on TimeoutException {
      // บางตัวไม่ส่ง prompt หลัง reset ไม่เป็นไร
    }
    await Future<void>.delayed(const Duration(milliseconds: 800));
    for (final c in const ['ATE0', 'ATL0', 'ATS0', 'ATH0', 'ATAT1', 'ATSP0']) {
      await _x(() => ses.cmd(c));
    }

    _set(LinkState.connecting, 'กำลังค้นหาโปรโตคอลของรถ...');
    final r = await _x(
        () => ses.cmd('0100', timeout: const Duration(seconds: 20)));
    if (!r.replaceAll(' ', '').contains('4100')) {
      if (r.contains('UNABLE') || r.contains('NO DATA') || r.contains('ERROR')) {
        throw Exception('ต่อ ECU ไม่ได้ — เปิดสวิตช์กุญแจ (ON) หรือยัง? [$r]');
      }
      throw Exception('ECU ตอบไม่ถูกต้อง: $r');
    }
    // ---- PID ที่รถรองรับ (0100, 0120, 0140 ...) ----
    final sup = hub.data.supportedPids..clear();
    var base = 0;
    var bm = r;
    while (true) {
      final b = parsePid(bm, pidHex(base));
      if (b == null || b.length < 4) break;
      for (var i = 0; i < 32; i++) {
        if ((b[i ~/ 8] >> (7 - i % 8)) & 1 == 1) sup.add(base + i + 1);
      }
      if (!sup.contains(base + 0x20) || base >= 0xC0) break;
      base += 0x20;
      try {
        bm = await _x(() => ses.cmd('01${pidHex(base)}'));
      } on TimeoutException {
        break;
      }
    }

    final dpn = (await _x(() => ses.cmd('ATDPN'))).split('\n').last.trim();
    _isCan = dpn.isNotEmpty && '6789ABC'.contains(dpn[dpn.length - 1]);
    final t = await _x(() => ses.cmd('010C1'));
    _oneResp = t.replaceAll(' ', '').contains('410C');

    // ---- ทดสอบขอหลายค่าในคำสั่งเดียว (CAN เท่านั้น สูงสุด 6 PID) ----
    _multi = null;
    if (_isCan) {
      final want = <int>[0x0C, 0x0D, 0x11, 0x04, 0x0B];
      if (sup.contains(0x34)) {
        want.add(0x34);
      } else if (sup.contains(0x24)) {
        want.add(0x24);
      }
      final pids = want.where((p) => p == 0x0C || p == 0x0D || sup.isEmpty || sup.contains(p)).toList();
      final base = '01${pids.map(pidHex).join()}';
      for (final c in ['${base}1', base]) {
        try {
          final rr = await _x(() => ses.cmd(c, timeout: const Duration(seconds: 2)));
          if (parseMulti(rr).containsKey(0x0C)) {
            _multi = c;
            break;
          }
        } on TimeoutException {
          // ลองแบบถัดไป
        }
      }
    } else {
      // K-line: ให้ ELM จับจังหวะ ECU แบบเร่ง (ตอบเร็วขึ้น)
      try {
        await _x(() => ses.cmd('ATAT2'));
      } catch (_) {}
    }

    _set(LinkState.connected,
        'ELM327 $label · ${_isCan ? (_multi != null ? 'CAN (หลายค่า/คำสั่ง)' : 'CAN') : 'K-Line/อื่น ๆ'}');

    // ---- วนอ่านค่า: รอบ+ความเร็วทุกรอบ, ค่าอื่นสลับกันทีละตัว ----
    final d = hub.data;
    const slowAll = ['05', '0F', 'RV', '04', '11', '0B', '10', '2F', '34', '06'];   // 34 = AFR (lambda), 06 = fuel trim (ส่งให้จอ CYD)
    // ค่าที่มากับคำสั่งหลายค่าแล้ว ไม่ต้องอ่านแยก
    final inMulti = _multi == null
        ? const <String>{}
        : {'11', '04', '0B', '34', '24'}.where((p) => _multi!.contains(p)).toSet();
    final slow = slowAll.where((p) => !inMulti.contains(p)).toList();
    var cyc = 0;
    final miss = <String, int>{};
    var si = 0;
    var timeouts = 0;
    var ai = 0;
    while (_running && !ses.dead) {
      final mode = hub.pollMode;
      final List<String> order;
      final all = hub.data.supportedPids
          .where((p) => !isBitmapPid(p) && p != 0x0C && p != 0x0D)
          .toList()
        ..sort();
      if (mode == PollMode.perf) {
        // จับเวลา: อ่านความเร็วถี่ที่สุด
        order = ['0D', '0D', '0C'];
      } else if (mode == PollMode.allPids && all.isNotEmpty) {
        ai = ai % all.length;
        order = ['0C', '0D', pidHex(all[ai]), pidHex(all[(ai + 1) % all.length])];
        ai += 2;
      } else {
        cyc++;
        // ค่าที่หน้าจอใช้และเปลี่ยนตามเท้า (TPS/Load/MAP/AirFlow/AFR) = กลุ่มเร็ว
        final fast = hub.focusPids
            .where((p) => const {'11', '04', '0B', '10', '34'}.contains(p) &&
                !inMulti.contains(p) && (miss[p] ?? 0) < 4)
            .toList();
        if (_multi != null) {
          order = <String>['MULTI'];                         // รอบ+ความเร็ว+TPS+Load+MAP+AFR ในคำสั่งเดียว
          if (fast.isNotEmpty && cyc.isEven) order.add(fast[(cyc ~/ 2) % fast.length]);
        } else if (cyc.isEven || fast.isEmpty) {
          order = <String>['0C', '0D'];                      // K-line: รอบทุกครั้ง สลับกับ ความเร็ว / ค่ากลุ่มเร็ว
        } else {
          order = <String>['0C', fast[(cyc ~/ 2) % fast.length]];
        }
        // ค่าที่เปลี่ยนช้า (น้ำ/แบต/IAT ...) แทรกทุก 4 รอบ ให้รอบเครื่องได้คิวมากที่สุด
        for (var k = 0; k < slow.length && cyc % 4 == 0; k++) {
          final p = slow[si];
          si = (si + 1) % slow.length;
          if (fast.contains(p)) continue;                    // อยู่กลุ่มเร็วแล้ว
          if ((miss[p] ?? 0) < 4) {
            order.add(p);
            break;
          }
        }
      }
      for (final p in order) {
        if (!_running || ses.dead) break;
        try {
          final ok = await _poll(ses, p, d);
          miss[p] = ok ? 0 : (miss[p] ?? 0) + 1;
          timeouts = 0;
        } on TimeoutException {
          if (++timeouts >= 4) {
            throw Exception('ELM327 ไม่ตอบสนอง — กำลังเชื่อมต่อใหม่');
          }
        }
      }
    }
    if (_running && ses.dead) {
      throw ses.deadReason ?? Exception('หลุดการเชื่อมต่อ');
    }
  }

  Future<bool> _poll(ElmSession ses, String p, CarData d) async {
    if (p == 'MULTI') {
      final r = await _x(() => ses.cmd(_multi!, timeout: const Duration(seconds: 2)));
      final m = parseMulti(r);
      if (!m.containsKey(0x0C)) return false;
      m.forEach((pid, b) => _apply(pidHex(pid), b, d));
      return true;
    }
    if (p == 'RV') {
      final r = await _x(() => ses.cmd('ATRV'));
      final m = RegExp(r'(\d+(?:\.\d+)?)').firstMatch(r);
      if (m == null) return false;
      d.volt = double.parse(m.group(1)!);
      return true;
    }
    final r = await _x(() => ses.cmd('01$p${_oneResp ? '1' : ''}',
        timeout: const Duration(seconds: 2)));
    final b = parsePid(r, p);
    if (b == null || b.isEmpty) return false;
    return _apply(p, b, d);
  }

  bool _apply(String p, List<int> b, CarData d) {
    d.pidBytes[int.parse(p, radix: 16)] = b;
    final a = b[0];
    if ((p == '34' || p == '24') && b.length >= 2) {
      final lam = (a * 256 + b[1]) * 2 / 65536;
      if (lam > 0.5 && lam < 8) d.lambda = lam;              // ตัดค่าเพี้ยน/ตอนตัดน้ำมัน
      return true;
    }
    final bb = b.length > 1 ? b[1] : 0;
    switch (p) {
      case '0C':
        if (b.length < 2) return false;
        d.rpm = (a * 256 + bb) / 4.0;
      case '0D':
        d.speed = a.toDouble();
      case '05':
        d.ect = a - 40.0;
      case '0F':
        d.iat = a - 40.0;
      case '04':
        d.load = a * 100 / 255;
      case '11':
        d.tps = a * 100 / 255;
      case '0B':
        d.map = a.toDouble();
      case '10':
        if (b.length < 2) return false;
        d.maf = (a * 256 + bb) / 100.0;
      case '2F':
        d.fuel = a * 100 / 255;
      default:
        break;
    }
    return true;
  }

  /// ดึงไบต์ข้อมูลจากคำตอบ เช่น "410C1AF8" → [0x1A, 0xF8]
  static List<int>? parsePid(String r, String pid) {
    final key = '41$pid';
    for (final line in r.split('\n')) {
      final l = line.replaceAll(' ', '').trim();
      final i = l.indexOf(key);
      if (i < 0) continue;
      final hex = l.substring(i + key.length);
      final out = <int>[];
      for (var j = 0; j + 1 < hex.length; j += 2) {
        final v = int.tryParse(hex.substring(j, j + 2), radix: 16);
        if (v == null) break;
        out.add(v);
      }
      if (out.isNotEmpty) return out;
    }
    return null;
  }

  /// แปลงคำตอบ Mode 03 เป็นรายการรหัส DTC
  static List<String> parseDtc(String r, bool isCan) {
    final lines = r
        .split('\n')
        .map((l) => l.replaceAll(' ', '').trim())
        .where((l) => l.isNotEmpty)
        .toList();
    final framed = RegExp(r'^[0-9A-F]:');
    final chunks = <String>[];
    final multi = lines.where((l) => framed.hasMatch(l)).toList();
    if (multi.isNotEmpty) {
      var s = multi.map((l) => l.substring(2)).join();
      final i = s.indexOf('43');
      if (i >= 0) {
        s = s.substring(i + 2);
        if (isCan && s.length >= 2) s = s.substring(2);
        chunks.add(s);
      }
    } else {
      for (final l in lines) {
        if (!l.startsWith('43')) continue;
        var s = l.substring(2);
        if (isCan && s.length >= 2) s = s.substring(2);
        chunks.add(s);
      }
    }
    final codes = <String>[];
    for (final s in chunks) {
      for (var j = 0; j + 3 < s.length; j += 4) {
        final c = s.substring(j, j + 4);
        if (c == '0000') continue;
        final n = int.tryParse(c[0], radix: 16);
        if (n == null) continue;
        final code = '${'PCBU'[n >> 2]}${n & 3}${c.substring(1)}';
        if (!codes.contains(code)) codes.add(code);
      }
    }
    return codes;
  }

  @override
  Future<List<String>> readDtc() async {
    final ses = _ses;
    if (ses == null || ses.dead || _hub?.state != LinkState.connected) {
      throw Exception('ยังไม่ได้เชื่อมต่อกับรถ');
    }
    final r =
        await _x(() => ses.cmd('03', timeout: const Duration(seconds: 10)));
    if (r.contains('NO DATA')) return [];
    return parseDtc(r, _isCan);
  }

  @override
  Future<bool> clearDtc() async {
    final ses = _ses;
    if (ses == null || ses.dead || _hub?.state != LinkState.connected) {
      throw Exception('ยังไม่ได้เชื่อมต่อกับรถ');
    }
    final r =
        await _x(() => ses.cmd('04', timeout: const Duration(seconds: 10)));
    return r.replaceAll(' ', '').contains('44');
  }
}
