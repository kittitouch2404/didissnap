import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:flutter/services.dart';

import 'native.dart';

/// ทางเชื่อมแอป ↔ C3: Bluetooth BLE (ปกติ) หรือ WiFi (ฮอตสปอตเดียวกัน — จอรถที่ไม่มี BLE)
/// events: String = สถานะ ('connected' / 'disconnected' / 'addr:XX' / ...) · Uint8List = แพ็กเก็ตจาก C3
abstract class C3Link {
  Stream<dynamic> get events;
  Future<void> start(String pairAddr);
  Future<void> stop();
  Future<bool> write(Uint8List p);
  void reject();
  void setPair(String addr);
  bool get isWifi;
}

class C3BleLink implements C3Link {
  static const _ev = EventChannel('didissnap/c3');
  @override
  Stream<dynamic> get events => _ev.receiveBroadcastStream();
  @override
  Future<void> start(String pairAddr) async => Native.m.invokeMethod('c3Start', {'addr': pairAddr});
  @override
  Future<void> stop() async {
    try {
      await Native.m.invokeMethod('c3Stop');
    } catch (_) {}
  }

  @override
  Future<bool> write(Uint8List p) async => await Native.m.invokeMethod<bool>('c3Write', {'b': p}) ?? false;
  @override
  void reject() => Native.m.invokeMethod('c3Reject');
  @override
  void setPair(String addr) => Native.m.invokeMethod('c3SetPair', {'addr': addr});
  @override
  bool get isWifi => false;
}

/// WiFi: C3 ประกาศตัว (DN_BEACON) ทุก 1 วิ ที่พอร์ต 35101 → เลือก C3 ของเรา → ส่ง/รับแพ็กเก็ตทาง UDP
class C3UdpLink implements C3Link {
  static const portC3 = 35100, portApp = 35101;
  static const _magic = 0x31444944;
  final _ctrl = StreamController<dynamic>.broadcast();
  RawDatagramSocket? _s;
  InternetAddress? _peer;
  String _peerMac = '', _pair = '';
  DateTime _lastRx = DateTime.fromMillisecondsSinceEpoch(0);
  final _rejected = <String, DateTime>{};
  Timer? _t;
  DateTime _lastBeacon = DateTime.fromMillisecondsSinceEpoch(0);
  bool _run = false;

  @override
  bool get isWifi => true;
  @override
  Stream<dynamic> get events => _ctrl.stream;

  @override
  Future<void> start(String pairAddr) async {
    _pair = pairAddr.toUpperCase();
    _run = true;
    try {
      await Native.m.invokeMethod('wifiLock', {'on': true}); // ให้เครื่องรับ broadcast ได้ (บางจอกรองทิ้ง)
    } catch (_) {}
    await _bind();
    _t = Timer.periodic(const Duration(milliseconds: 1500), (_) => _tick());
  }

  Future<void> _bind() async {
    try {
      final s = await RawDatagramSocket.bind(InternetAddress.anyIPv4, portApp, reuseAddress: true);
      s.broadcastEnabled = true;
      s.listen((ev) {
        if (ev != RawSocketEvent.read) return;
        Datagram? d;
        while ((d = s.receive()) != null) {
          _in(d!);
        }
      });
      _s = s;
      _ctrl.add('searching C3 (wifi)');
    } catch (e) {
      _ctrl.add('wifi error');
    }
  }

  static String _str(Uint8List b, int off, int len) {
    final out = <int>[];
    for (var i = off; i < off + len && i < b.length && b[i] != 0; i++) {
      out.add(b[i]);
    }
    return String.fromCharCodes(out);
  }

  void _in(Datagram d) {
    final b = d.data;
    if (b.length < 6 || ByteData.sublistView(b).getUint32(0, Endian.little) != _magic) return;
    if (b[4] == 20) {
      // DN_BEACON: open(5) pad(6,7) mac[18](8) name[16](26)
      _lastBeacon = DateTime.now();
      if (_peer != null) return;
      final open = b[5] == 1;
      final mac = _str(b, 8, 18).toUpperCase();
      final rj = _rejected[mac];
      if (rj != null && DateTime.now().difference(rj).inSeconds < 60) return;
      if (_pair.isNotEmpty ? mac != _pair : !open) {
        if (_pair.isEmpty) _ctrl.add('no C3 in pairing mode');
        return;
      }
      _peer = d.address;
      _peerMac = mac;
      _lastRx = DateTime.now();
      _ctrl.add('addr:$mac');
      _ctrl.add('connected');
      return;
    }
    if (_peer != null && d.address == _peer) {
      _lastRx = DateTime.now();
      _ctrl.add(Uint8List.fromList(b));
    }
  }

  void _tick() {
    if (!_run) return;
    if (_s == null) {
      _bind();
      return;
    }
    if (_peer != null) {
      if (DateTime.now().difference(_lastRx).inSeconds > 4) {
        _drop();
        return;
      }
      // ping (DN_WIFI แบบสั้น) ให้ C3 รู้ว่ายังอยู่
      final p = ByteData(8)
        ..setUint32(0, _magic, Endian.little)
        ..setUint8(4, 18);
      _send(p.buffer.asUint8List());
    } else if (DateTime.now().difference(_lastBeacon).inSeconds > 4) {
      _ctrl.add('searching C3 (wifi)');
    }
  }

  void _drop() {
    if (_peer == null) return;
    _peer = null;
    _peerMac = '';
    _ctrl.add('disconnected');
  }

  bool _send(Uint8List p) {
    final s = _s, a = _peer;
    if (s == null || a == null) return false;
    try {
      return s.send(p, a, portC3) > 0;
    } catch (_) {
      return false;
    }
  }

  @override
  Future<bool> write(Uint8List p) async => _send(p);

  @override
  void reject() {
    if (_peerMac.isNotEmpty) _rejected[_peerMac] = DateTime.now();
    _drop();
  }

  @override
  void setPair(String addr) {
    _pair = addr.toUpperCase();
    if (_peer != null && _pair.isNotEmpty && _peerMac != _pair) _drop();
    if (_pair.isEmpty) _drop();
  }

  @override
  Future<void> stop() async {
    _run = false;
    _t?.cancel();
    _drop();
    _s?.close();
    _s = null;
    try {
      await Native.m.invokeMethod('wifiLock', {'on': false});
    } catch (_) {}
  }
}

/// Bluetooth ธรรมดา (SPP) — ESP32-Superlink รุ่น ESP32 (จอแอนดรอยด์จีนที่ไม่มี BLE ให้แอป)
/// แพ็กเก็ตเดียวกับ BLE ส่งเป็นบรรทัด "~" + base64 + "\n" ทั้งสองทาง
/// ต้องจับคู่ "DIDI-SL-xxxx" ในตั้งค่า Bluetooth ของเครื่องก่อน แล้วเลือกตัวนั้นในแอป
class C3SppLink implements C3Link {
  C3SppLink(this.address);
  final String address;
  final _ctrl = StreamController<dynamic>.broadcast();
  StreamSubscription<dynamic>? _sub;
  int _gen = -1;
  bool _run = false, _up = false, _busy = false;
  String _buf = '';
  DateTime _retryAt = DateTime.fromMillisecondsSinceEpoch(0);
  Timer? _t;

  @override
  bool get isWifi => false;
  @override
  Stream<dynamic> get events => _ctrl.stream;

  @override
  Future<void> start(String pairAddr) async {
    _run = true;
    if (address.isEmpty) {
      Timer(const Duration(milliseconds: 50), () => _ctrl.add('spp no device'));
      return;
    }
    _sub = const EventChannel('didissnap/bt/stream').receiveBroadcastStream().listen(_onNative);
    _t = Timer.periodic(const Duration(seconds: 1), (_) => _tick());
    _tick();
  }

  void _onNative(dynamic ev) {
    if (ev is! Map || ev['g'] != _gen) return;
    if (ev['t'] == 'data') {
      _buf += ev['d'] as String;
      var i = _buf.indexOf('\n');
      while (i >= 0) {
        final line = _buf.substring(0, i).trim();
        _buf = _buf.substring(i + 1);
        if (line.length > 1 && line.codeUnitAt(0) == 0x7E) {
          try {
            _ctrl.add(Uint8List.fromList(base64.decode(line.substring(1))));
          } catch (_) {}
        }
        i = _buf.indexOf('\n');
      }
      if (_buf.length > 2000) _buf = '';
    } else if (ev['t'] == 'closed') {
      _lost(2);
    }
  }

  void _lost(int waitS) {
    _gen = -1;
    _buf = '';
    _retryAt = DateTime.now().add(Duration(seconds: waitS));
    if (_up) {
      _up = false;
      _ctrl.add('disconnected');
    }
  }

  Future<void> _tick() async {
    if (!_run || _up || _busy || DateTime.now().isBefore(_retryAt)) return;
    _busy = true;
    _ctrl.add('spp connecting');
    try {
      final g = await Native.m.invokeMethod<int>('connect', {'address': address});
      if (!_run) {
        await Native.m.invokeMethod('disconnect');
        return;
      }
      _gen = g ?? -1;
      _up = true;
      _ctrl.add('addr:${address.toUpperCase()}');
      _ctrl.add('connected');
    } on PlatformException catch (e) {
      _ctrl.add('spp failed:${e.message ?? ''}');
      _retryAt = DateTime.now().add(const Duration(seconds: 3));
    } catch (_) {
      _retryAt = DateTime.now().add(const Duration(seconds: 3));
    } finally {
      _busy = false;
    }
  }

  @override
  Future<bool> write(Uint8List p) async {
    if (!_up) return false;
    try {
      return await Native.m.invokeMethod<bool>('write', {'data': '~${base64.encode(p)}\n'}) ?? false;
    } catch (_) {
      return false;
    }
  }

  @override
  void reject() {
    // Superlink ตัวนี้จับคู่กับคันอื่น → ตัดแล้วรอ 10 วิค่อยลองใหม่ (อาจเปิดโหมดจับคู่แล้ว)
    try {
      Native.m.invokeMethod('disconnect');
    } catch (_) {}
    _lost(10);
  }

  @override
  void setPair(String addr) {}

  @override
  Future<void> stop() async {
    _run = false;
    _t?.cancel();
    await _sub?.cancel();
    _sub = null;
    _up = false;
    _gen = -1;
    try {
      await Native.m.invokeMethod('disconnect');
    } catch (_) {}
  }
}
