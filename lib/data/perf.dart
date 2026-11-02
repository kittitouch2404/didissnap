import 'dart:async';
import 'dart:math' as math;

import 'package:flutter/foundation.dart';

import 'car_data.dart';
import 'hub.dart';

enum PerfState { idle, waitStop, ready, running, done }

/// รอบวิ่งที่บันทึกไว้
class PerfRun {
  PerfRun(this.at, this.times, this.traps, this.top, this.peakRpm);
  final DateTime at;
  final Map<String, double> times;
  final Map<String, double> traps;
  final double top;
  final double peakRpm;
}

/// รายการที่จับเวลา (key, ชื่อที่แสดง)
const List<(String, String)> kPerfItems = [
  ('60ft', '0 – 60 ft (18 m)'),
  ('0-60', '0 – 60 km/h'),
  ('0-100', '0 – 100 km/h'),
  ('60-100', '60 – 100 km/h'),
  ('100-150', '100 – 150 km/h'),
  ('201m', '1/8 ไมล์ (201 m)'),
  ('402m', '1/4 ไมล์ (402 m)'),
];

/// จับเวลาออกตัวจากค่าความเร็วของรถ (ใช้เวลาที่ประทับไว้ตอนได้ค่า + คำนวณจุดตัดแบบเส้นตรง)
class PerfRecorder extends ChangeNotifier {
  PerfRecorder(this.hub);
  final DataHub hub;

  static const _speedTargets = {'0-60': 60.0, '0-100': 100.0, '0-150': 150.0};
  static const _distTargets = {'60ft': 18.288, '201m': 201.168, '402m': 402.336};

  PerfState state = PerfState.idle;
  final Map<String, double> times = {};
  final Map<String, double> traps = {};
  final List<PerfRun> history = [];
  double dist = 0, elapsed = 0, top = 0, peakRpm = 0;

  Timer? _timer;
  int _seq = -1;
  double _t0 = 0, _lastT = 0, _lastV = 0, _zeroT = 0;

  static double _now() => CarData.clock.elapsedMicroseconds / 1e6;

  /// เริ่มรอ: หยุดรถให้นิ่ง แล้วออกตัวได้เลย
  void arm() {
    times.clear();
    traps.clear();
    dist = 0;
    elapsed = 0;
    top = 0;
    peakRpm = 0;
    final d = hub.data;
    _seq = d.speedSeq;
    _zeroT = d.speedT / 1e6;
    state = d.speed < 0.5 ? PerfState.ready : PerfState.waitStop;
    _timer?.cancel();
    _timer = Timer.periodic(const Duration(milliseconds: 10), (_) => _tick());
    notifyListeners();
  }

  void stop() {
    if (state == PerfState.running) {
      _finish();
    } else {
      _timer?.cancel();
      state = PerfState.idle;
      notifyListeners();
    }
  }

  void clearHistory() {
    history.clear();
    notifyListeners();
  }

  /// เวลาดีที่สุดของแต่ละรายการ
  double? best(String key) {
    double? b;
    for (final r in history) {
      final v = r.times[key];
      if (v != null && (b == null || v < b)) b = v;
    }
    return b;
  }

  void _tick() {
    final d = hub.data;
    if (d.speedSeq == _seq) {
      if (state == PerfState.running) {
        elapsed = _now() - _t0;
        if (elapsed > 90) _finish();
        notifyListeners();
      }
      return;
    }
    _seq = d.speedSeq;
    final t = d.speedT / 1e6;
    final v = d.speed;
    switch (state) {
      case PerfState.waitStop:
        if (v < 0.5) {
          _zeroT = t;
          state = PerfState.ready;
          notifyListeners();
        }
      case PerfState.ready:
        if (v < 0.5) {
          _zeroT = t;
        } else {
          // ออกตัว: นับเวลาจากค่าความเร็ว 0 ครั้งสุดท้าย
          _t0 = _zeroT;
          _lastT = _t0;
          _lastV = 0;
          dist = 0;
          state = PerfState.running;
          _process(t, v);
        }
      case PerfState.running:
        _process(t, v);
      case PerfState.idle:
      case PerfState.done:
        break;
    }
  }

  void _process(double t, double v) {
    final dt = t - _lastT;
    if (dt <= 0) return;
    final d0 = dist;
    dist += (_lastV + v) / 2 / 3.6 * dt;
    _speedTargets.forEach((key, target) {
      if (!times.containsKey(key) && v >= target && _lastV < target) {
        final tc = _lastT + dt * (target - _lastV) / (v - _lastV);
        times[key] = tc - _t0;
      }
    });
    _distTargets.forEach((key, target) {
      if (!times.containsKey(key) && dist >= target && d0 < target) {
        final f = (target - d0) / (dist - d0);
        times[key] = _lastT + dt * f - _t0;
        traps[key] = _lastV + (v - _lastV) * f;
      }
    });
    top = math.max(top, v);
    peakRpm = math.max(peakRpm, hub.data.rpm);
    _lastT = t;
    _lastV = v;
    elapsed = t - _t0;
    final allDone = times.containsKey('402m') && times.containsKey('0-100');
    if (v < 0.5 || elapsed > 90 || allDone) {
      _finish();
    } else {
      notifyListeners();
    }
  }

  void _finish() {
    _timer?.cancel();
    final a = times['0-60'], b = times['0-100'], c = times['0-150'];
    if (a != null && b != null) times['60-100'] = b - a;
    if (b != null && c != null) times['100-150'] = c - b;
    if (times.isNotEmpty) {
      history.insert(
          0,
          PerfRun(DateTime.now(), Map.of(times), Map.of(traps), top, peakRpm));
      if (history.length > 30) history.removeLast();
    }
    state = PerfState.done;
    notifyListeners();
  }

  @override
  void dispose() {
    _timer?.cancel();
    super.dispose();
  }
}
