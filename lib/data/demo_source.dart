import 'dart:async';
import 'dart:math' as math;

import '../settings.dart';
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
