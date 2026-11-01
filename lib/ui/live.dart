import 'dart:math' as math;

import 'package:flutter/scheduler.dart';
import 'package:flutter/widgets.dart';

import '../data/car_data.dart';
import '../data/hub.dart';
import '../settings.dart';

/// สปริงแบบ critically damped: ตัวเลข/เข็มวิ่งนุ่ม ไม่เด้ง ไม่หน่วง
class Smooth {
  double x = 0, v = 0;
  bool _init = false;

  void step(double target, double dt, double omega) {
    if (!_init || !x.isFinite) {
      x = target;
      v = 0;
      _init = true;
      return;
    }
    final f = 1 + 2 * dt * omega;
    final oo = omega * omega;
    final hoo = dt * oo;
    final hhoo = dt * hoo;
    final inv = 1 / (f + hhoo);
    final nx = (f * x + dt * v + hhoo * target) * inv;
    final nv = (v + hoo * (target - x)) * inv;
    x = nx;
    v = nv;
  }
}

/// ข้อมูล 1 เฟรมที่ส่งให้หน้าปัดวาด
class Frame {
  Frame({
    required this.s,
    required this.link,
    required this.time,
    required this.rpm,
    required this.speed,
    required this.gear,
    this.ect,
    this.iat,
    this.volt,
    this.load,
    this.tps,
    this.map,
    this.maf,
    this.fuel,
    this.trip = 0,
  });

  final AppSettings s;
  final LinkState link;
  final double time, rpm, speed;
  final String gear;
  final double? ect, iat, volt, load, tps, map, maf, fuel;
  final double trip;

  /// บูสต์ (bar) คำนวณจาก MAP − ความดันบรรยากาศ
  double? get boost => map == null ? null : (map! - 101.3) / 100;

  int get maxRpm => math.max(1000, s.maxRpm);
  int get redline => s.redline;
  int get shiftStart => s.shiftStart;
  int get maxSpeed => math.max(60, s.maxSpeed);
  int get ectWarn => s.ectWarn;
  bool get atRedline => rpm >= redline;
  bool get ectHot => (ect ?? 0) >= ectWarn;
}

class _LiveValues {
  final rpm = Smooth(), speed = Smooth();
  final ect = Smooth(), iat = Smooth(), volt = Smooth();
  final load = Smooth(), tps = Smooth(), map = Smooth(), maf = Smooth();
  final fuel = Smooth();
  double time = 0;

  void _n(Smooth sm, double? v, double dt, double w) {
    if (v != null) sm.step(v, dt, w);
  }

  void update(CarData d, double dt) {
    time += dt;
    rpm.step(d.rpm, dt, 16);
    speed.step(d.speed, dt, 9);
    _n(ect, d.ect, dt, 4);
    _n(iat, d.iat, dt, 4);
    _n(volt, d.volt, dt, 5);
    _n(load, d.load, dt, 10);
    _n(tps, d.tps, dt, 14);
    _n(map, d.map, dt, 10);
    _n(maf, d.maf, dt, 10);
    _n(fuel, d.fuel, dt, 3);
  }

  Frame frame(DataHub hub) {
    final d = hub.data;
    final s = hub.settings;
    return Frame(
      s: s,
      link: hub.state,
      time: time,
      rpm: math.max(0.0, rpm.x),
      speed: math.max(0.0, speed.x),
      gear: estimateGear(d.rpm, d.speed, s.gearRatios),
      ect: d.ect == null ? null : ect.x,
      iat: d.iat == null ? null : iat.x,
      volt: d.volt == null ? null : volt.x,
      load: d.load == null ? null : load.x,
      tps: d.tps == null ? null : tps.x,
      map: d.map == null ? null : map.x,
      maf: d.maf == null ? null : maf.x,
      fuel: d.fuel == null ? null : fuel.x,
      trip: d.trip,
    );
  }
}

/// วาดใหม่ทุกเฟรม (60/120 fps ตามจอมือถือ)
class LiveBuilder extends StatefulWidget {
  const LiveBuilder({super.key, required this.hub, required this.builder});
  final DataHub hub;
  final Widget Function(BuildContext context, Frame f) builder;

  @override
  State<LiveBuilder> createState() => _LiveBuilderState();
}

class _LiveBuilderState extends State<LiveBuilder>
    with SingleTickerProviderStateMixin {
  late final Ticker _ticker;
  final _v = _LiveValues();
  Duration _last = Duration.zero;

  @override
  void initState() {
    super.initState();
    _ticker = createTicker(_tick)..start();
  }

  void _tick(Duration e) {
    final raw = (e - _last).inMicroseconds / 1e6;
    _last = e;
    final dt = math.min(0.1, math.max(0.0, raw));
    _v.update(widget.hub.data, dt);
    setState(() {});
  }

  @override
  void dispose() {
    _ticker.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) =>
      widget.builder(context, _v.frame(widget.hub));
}
