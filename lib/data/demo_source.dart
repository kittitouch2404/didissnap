import 'dart:async';
import 'dart:math' as math;

import '../settings.dart';
import 'car_data.dart';
import 'hub.dart';

/// โหมดจำลอง: เร่งไล่เกียร์ 1→ท้าย แล้วชะลอ วนไปเรื่อย ๆ
class DemoSource implements CarSource {
  DemoSource(this.s);
  final AppSettings s;

  Timer? _t;
  DataHub? _hub;
  final _rnd = math.Random();
  int _gear = 1;
  bool _accel = true;
  double _rpm = 850, _idle = 1.5, _ect = 45, _time = 0;

  @override
  void start(DataHub hub) {
    _hub = hub;
    hub.setLink(this, LinkState.connected, 'โหมดจำลอง (Demo)');
    _t = Timer.periodic(const Duration(milliseconds: 20), (_) => _tick(0.02));
  }

  @override
  Future<void> stop() async {
    _t?.cancel();
    _t = null;
  }

  void _tick(double dt) {
    final hub = _hub;
    if (hub == null) return;
    final d = hub.data;
    final g = s.gearRatios;
    final n = g.length;
    if (_gear > n) _gear = n;
    final red = s.redline.toDouble();
    _time += dt;

    double tps;
    if (_idle > 0) {
      _idle -= dt;
      _rpm += (850 - _rpm) * math.min(1.0, dt * 3);
      tps = 0;
      if (_idle <= 0) {
        _accel = true;
        _gear = 1;
      }
    } else if (_accel) {
      tps = 85;
      _rpm += (_gear <= 2 ? 2600 : (_gear == 3 ? 1700 : 1100)) * dt;
      if (_rpm >= red + 80) {
        if (_gear < n) {
          _rpm = _rpm * g[_gear - 1] / g[_gear];
          _gear++;
        } else {
          _accel = false;
        }
      }
    } else {
      tps = 0;
      _rpm -= 700 * dt;
      if (_rpm < 1400) {
        if (_gear > 1) {
          _rpm = _rpm * g[_gear - 1] / g[_gear - 2];
          _gear--;
        } else {
          _idle = 2.5;
        }
      }
    }

    final idle = _idle > 0;
    d.rpm = math.min(_rpm, s.maxRpm.toDouble()) + _rnd.nextDouble() * 15;
    d.speed = idle ? 0 : _rpm / 1000 * g[_gear - 1];
    _ect = math.min(92.0, _ect + dt * 1.5);
    d.ect = _ect;
    d.iat = 36 + math.sin(_time / 7) * 2;
    d.volt = 14.1 + math.sin(_time * 1.3) * 0.1;
    d.load = tps > 0 ? 70 + _rnd.nextDouble() * 10 : 18;
    d.tps = tps;
    d.map = tps > 0 ? 165 : 32; // จำลองเครื่องเทอร์โบ (บูสต์ ~0.6 bar)
    d.maf = _rpm / 1000 * (tps > 0 ? 9 : 2.2);
    d.fuel = 67;
    _fillPids(d, tps);
  }

  static const _demoPids = [
    0x01, 0x03, 0x04, 0x05, 0x06, 0x07, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
    0x11, 0x14, 0x15, 0x1C, 0x1F, 0x2F, 0x31, 0x33, 0x3C, 0x42, 0x43, 0x44,
    0x45, 0x46, 0x49, 0x4C, 0x51, 0x5C,
  ];

  /// ข้อมูลจำลองสำหรับหน้าตาราง PID
  void _fillPids(CarData d, double tps) {
    if (d.supportedPids.isEmpty) d.supportedPids.addAll(_demoPids);
    int c(num v) => v.round().clamp(0, 255).toInt();
    List<int> w(num v) {
      final x = v.round().clamp(0, 65535).toInt();
      return [x >> 8, x & 255];
    }

    final rpm = d.rpm, p = d.pidBytes;
    p[0x01] = [0x00, 0x07, 0xE5, 0x00];
    p[0x03] = [2, 0];
    p[0x04] = [c((d.load ?? 0) * 2.55)];
    p[0x05] = [c((d.ect ?? 0) + 40)];
    p[0x06] = [c(128 + math.sin(_time * 3) * 6)];
    p[0x07] = [c(131)];
    p[0x0B] = [c(d.map ?? 0)];
    p[0x0C] = w(rpm * 4);
    p[0x0D] = [c(d.speed)];
    p[0x0E] = [c((12 + tps / 8 + 64) * 2)];
    p[0x0F] = [c((d.iat ?? 0) + 40)];
    p[0x10] = w((d.maf ?? 0) * 100);
    p[0x11] = [c(tps * 2.55)];
    p[0x14] = [c(90 + math.sin(_time * 5) * 80), 128];
    p[0x15] = [c(140), 255];
    p[0x1C] = [6];
    p[0x1F] = w(_time);
    p[0x2F] = [c(67 * 2.55)];
    p[0x31] = w(1234);
    p[0x33] = [101];
    p[0x3C] = w((620 + tps * 2 + 40) * 10);
    p[0x42] = w((d.volt ?? 0) * 1000);
    p[0x43] = w((d.load ?? 0) * 2.55);
    p[0x44] = w(32768 * (tps > 0 ? 0.88 : 1.0));
    p[0x45] = [c(tps * 2.4)];
    p[0x46] = [c(32 + 40)];
    p[0x49] = [c(tps * 2.2 + 30)];
    p[0x4C] = [c(tps * 2.4)];
    p[0x51] = [1];
    p[0x5C] = [c(math.min(98.0, (d.ect ?? 0) + 4) + 40)];
  }

  @override
  Future<List<String>> readDtc() async {
    await Future<void>.delayed(const Duration(milliseconds: 700));
    return const ['P0133', 'P0420'];
  }

  @override
  Future<bool> clearDtc() async {
    await Future<void>.delayed(const Duration(milliseconds: 700));
    return true;
  }
}
