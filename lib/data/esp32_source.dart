import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'hub.dart';
import 'native.dart';

/// รับค่าจาก ESP32 CYD ผ่าน WiFi (TCP) เป็น JSON ทีละบรรทัด เช่น
/// {"rpm":2350,"spd":42,"ect":88,"iat":36,"volt":14.1,"load":32,"tps":12,"map":40,"maf":6.2,"fuel":67}
/// คำสั่งที่แอปส่งกลับไป:  {"cmd":"dtc_read"}  /  {"cmd":"dtc_clear"}
/// ESP32 ตอบ:            {"dtc":["P0133"]}   /  {"cleared":true}
class Esp32Source implements CarSource {
  Esp32Source(this.host, this.port);
  final String host;
  final int port;

  DataHub? _hub;
  Socket? _s;
  bool _running = false;
  Completer<Map<String, dynamic>>? _reply;

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
    _s?.destroy();
  }

  Future<void> _loop() async {
    while (_running) {
      try {
        await _session();
      } catch (e) {
        if (_running) _set(LinkState.error, 'ESP32: ${errText(e)}');
      }
      _s?.destroy();
      _s = null;
      if (!_running) break;
      await Future<void>.delayed(const Duration(seconds: 3));
    }
  }

  Future<void> _session() async {
    _set(LinkState.connecting, 'กำลังต่อ ESP32 $host:$port ...');
    await Native.bindWifi();
    final s = await Socket.connect(host, port,
        timeout: const Duration(seconds: 5));
    s.setOption(SocketOption.tcpNoDelay, true);
    _s = s;

    final done = Completer<void>();
    var got = false;
    var last = DateTime.now();
    final watch = Timer.periodic(const Duration(seconds: 1), (_) {
      if (DateTime.now().difference(last).inSeconds >= 4 && !done.isCompleted) {
        done.completeError(Exception('ไม่มีข้อมูลจาก ESP32'));
      }
    });
    final sub = s
        .cast<List<int>>()
        .transform(utf8.decoder)
        .transform(const LineSplitter())
        .listen(
      (line) {
        last = DateTime.now();
        if (!got) {
          got = true;
          _set(LinkState.connected, 'ESP32 ($host)');
        }
        _onLine(line);
      },
      onError: (Object e) {
        if (!done.isCompleted) done.completeError(e);
      },
      onDone: () {
        if (!done.isCompleted) {
          done.completeError(Exception('ESP32 ปิดการเชื่อมต่อ'));
        }
      },
      cancelOnError: true,
    );
    try {
      await done.future;
    } finally {
      watch.cancel();
      await sub.cancel();
    }
  }

  void _onLine(String line) {
    final t = line.trim();
    if (t.isEmpty || !t.startsWith('{')) return;
    Map<String, dynamic> m;
    try {
      final j = jsonDecode(t);
      if (j is! Map<String, dynamic>) return;
      m = j;
    } catch (_) {
      return;
    }
    if (m.containsKey('dtc') ||
        m.containsKey('cleared') ||
        m.containsKey('cfg') ||
        m.containsKey('ok')) {
      final r = _reply;
      _reply = null;
      if (r != null && !r.isCompleted) r.complete(m);
      return;
    }
    final d = _hub?.data;
    if (d == null) return;
    double? n(String k) => (m[k] is num) ? (m[k] as num).toDouble() : null;
    d.rpm = n('rpm') ?? d.rpm;
    d.speed = n('spd') ?? d.speed;
    d.ect = n('ect') ?? d.ect;
    d.iat = n('iat') ?? d.iat;
    d.volt = n('volt') ?? d.volt;
    d.load = n('load') ?? d.load;
    d.tps = n('tps') ?? d.tps;
    d.map = n('map') ?? d.map;
    d.maf = n('maf') ?? d.maf;
    d.fuel = n('fuel') ?? d.fuel;
  }

  Future<Map<String, dynamic>> _ask(String cmd,
      [Map<String, dynamic>? extra]) async {
    final s = _s;
    if (s == null || _hub?.state != LinkState.connected) {
      throw Exception('ยังไม่ได้เชื่อมต่อ ESP32');
    }
    final c = Completer<Map<String, dynamic>>();
    _reply = c;
    s.write('${jsonEncode({'cmd': cmd, ...?extra})}\n');
    await s.flush();
    return c.future.timeout(const Duration(seconds: 10),
        onTimeout: () => throw Exception('ESP32 ไม่ตอบคำสั่ง $cmd'));
  }

  // ---------------- ค่าตั้งของจอ ESP32 (DidissnapConfig.h) ----------------
  /// คืน {'cfg': {...}, 'pages': [...]}
  Future<Map<String, dynamic>> getConfig() => _ask('get_cfg');

  Future<Map<String, dynamic>> setConfig(Map<String, num> values) =>
      _ask('set_cfg', {'cfg': values});

  Future<Map<String, dynamic>> setPage(int page) => _ask('page', {'v': page});

  Future<void> resetPeaks() => _ask('peaks_reset');

  Future<void> resetTrip() => _ask('trip_reset');

  @override
  Future<List<String>> readDtc() async {
    final m = await _ask('dtc_read');
    final l = m['dtc'];
    return l is List ? l.map((e) => e.toString()).toList() : <String>[];
  }

  @override
  Future<bool> clearDtc() async {
    final m = await _ask('dtc_clear');
    return m['cleared'] == true;
  }
}
