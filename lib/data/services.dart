import 'dart:async';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:path_provider/path_provider.dart';

import '../settings.dart';
import 'car_data.dart';
import 'native.dart';

// ======================================================================
//  ค่าที่เลือกได้ในกราฟ / บันทึก CSV
// ======================================================================
class GMetric {
  const GMetric(this.name, this.unit, this.lo, this.hi, this.dec, this.get);
  final String name, unit;
  final double lo, hi;
  final int dec;
  final double? Function(CarData d, AppSettings s) get;
}

final List<GMetric> kGMetrics = [
  GMetric('RPM', 'rpm', 0, 8000, 0, (d, s) => d.rpm),
  GMetric('SPEED', 'km/h', 0, 200, 0, (d, s) => d.speed),
  GMetric('TPS', '%', 0, 100, 0, (d, s) => d.tps),
  GMetric('LOAD', '%', 0, 100, 0, (d, s) => d.load),
  GMetric('AFR', '', 10, 20, 1, (d, s) => d.lambda == null ? null : d.lambda! * s.afrStoich10 / 10),
  GMetric('BOOST', 'bar', -1, 1, 2, (d, s) => d.map == null ? null : (d.map! - 101.3) / 100),
  GMetric('AIRFLOW', 'g/s', 0, 120, 1, (d, s) => d.maf),
  GMetric('COOLANT', '°C', 40, 130, 0, (d, s) => d.ect),
  GMetric('IAT', '°C', -10, 80, 0, (d, s) => d.iat),
  GMetric('VOLT', 'V', 11, 15, 1, (d, s) => d.volt),
  GMetric('TIMING', '°', -10, 50, 0, (d, s) => d.timing),
  GMetric('F.TRIM', '%', -25, 25, 1, (d, s) {
    final b = d.pidBytes[0x06];
    return b == null || b.isEmpty ? null : (b[0] - 128) * 100 / 128;
  }),
  GMetric('OIL', '°C', 40, 140, 0, (d, s) => d.oil),
  GMetric('FUEL L/h', 'L/h', 0, 20, 1, (d, s) => d.fuelLph),
];

// ======================================================================
//  ประวัติค่าย้อนหลัง (หน้ากราฟ) 10 ครั้ง/วินาที เก็บ 120 วินาที
// ======================================================================
class History {
  static const int hz = 10, keep = 120 * hz;
  final List<List<double>> v = [for (final _ in kGMetrics) <double>[]];
  int n = 0; // จำนวนตัวอย่างทั้งหมดที่เคยเก็บ (ใช้เลื่อนกราฟ)

  void add(CarData d, AppSettings s) {
    for (var i = 0; i < kGMetrics.length; i++) {
      final x = kGMetrics[i].get(d, s);
      final l = v[i];
      l.add(x ?? double.nan);
      if (l.length > keep) l.removeRange(0, l.length - keep);
    }
    n++;
  }
}

// ======================================================================
//  เตือน: เสียง + แถบบนจอ
// ======================================================================
class AlertEngine {
  AlertEngine(this.s);
  final AppSettings s;

  /// ข้อความเตือนที่โชว์อยู่ (null = ไม่มี)  สีแดง/ส้ม
  final ValueNotifier<(String, bool)?> banner = ValueNotifier(null);

  static final _t0 = DateTime.fromMillisecondsSinceEpoch(0);
  DateTime _ectAt = _t0, _voltAt = _t0, _spdAt = _t0;
  DateTime? _connAt;
  bool _shiftArmed = true;
  int _voltLowN = 0;
  bool _oilShown = false;

  void tick(CarData d, bool connected) {
    final now = DateTime.now();
    if (!connected) {
      _connAt = null;
      banner.value = null;
      return;
    }
    _connAt ??= now;
    final settled = now.difference(_connAt!).inSeconds > 8; // เพิ่งต่อติด: ค่ายังไม่นิ่ง
    final vol = s.alertVol;
    (String, bool)? show;

    // น้ำร้อน: เตือนทันที แล้วซ้ำทุก 60 วินาทีถ้ายังร้อน
    final ect = d.ect;
    if (s.alertEct && ect != null && ect >= s.ectWarn) {
      show = ('น้ำร้อน ${ect.round()}°C', true);
      if (now.difference(_ectAt).inSeconds >= 60) {
        _ectAt = now;
        _beeps(1, 3, 350, vol);
      }
    }
    // แบตต่ำ: ต่ำติดกัน ~3 วินาที (กันวูบตอนสตาร์ท)
    final v = d.volt;
    if (s.alertVolt && settled && v != null && v < s.alertVolt10 / 10 && d.rpm > 400) {
      _voltLowN++;
      if (_voltLowN > 30) {
        show ??= ('แบตต่ำ ${v.toStringAsFixed(1)} V', true);
        if (now.difference(_voltAt).inSeconds >= 120) {
          _voltAt = now;
          _beeps(3, 2, 300, vol);
        }
      }
    } else {
      _voltLowN = 0;
    }
    // รอบถึงจุดเปลี่ยนเกียร์: บี๊บครั้งเดียวต่อการเร่ง
    final sr = s.alertShiftRpm > 0 ? s.alertShiftRpm : s.shiftStart;
    if (s.alertShift) {
      if (_shiftArmed && d.rpm >= sr) {
        _shiftArmed = false;
        Native.tone(2, 120, vol);
      } else if (d.rpm < sr - 600) {
        _shiftArmed = true;
      }
    }
    // ขับเร็วเกิน: ซ้ำทุก 6 วินาที
    if (s.alertSpeed && d.speed > s.alertSpeedKmh) {
      show ??= ('เร็วเกิน ${d.speed.round()} km/h', false);
      if (now.difference(_spdAt).inSeconds >= 6) {
        _spdAt = now;
        _beeps(0, 2, 160, vol);
      }
    }
    // ถึงระยะเปลี่ยนน้ำมันเครื่อง: โชว์ 8 วินาทีหลังต่อติด (ครั้งเดียวต่อการเปิดแอป)
    if (!_oilShown && settled && s.oilEvery > 0 && s.live('oilKm') >= s.oilEvery) {
      final t = now.difference(_connAt!).inSeconds;
      if (t < 18) {
        show ??= ('ถึงระยะเปลี่ยนน้ำมันเครื่อง (${s.live('oilKm').round()} km)', false);
      } else {
        _oilShown = true;
      }
      if (t == 9) _beeps(0, 1, 250, vol);
    }
    if (banner.value != show) banner.value = show;
  }

  Future<void> _beeps(int kind, int n, int ms, int vol) async {
    for (var i = 0; i < n; i++) {
      await Native.tone(kind, ms, vol);
      await Future<void>.delayed(Duration(milliseconds: ms + 150));
    }
  }
}

// ======================================================================
//  บันทึกค่าเป็น CSV (เปิดด้วย Excel / Google Sheets ได้)
// ======================================================================
class DataLogger {
  DataLogger(this.s);
  final AppSettings s;
  IOSink? _out;
  String? file;
  DateTime? _t0;
  int rows = 0;
  int _skip = 0;

  static Future<Directory> dir() async {
    final b = await getApplicationDocumentsDirectory();
    final d = Directory('${b.path}/logs');
    await d.create(recursive: true);
    return d;
  }

  bool get recording => _out != null;

  Future<void> _open() async {
    final d = await dir();
    final n = DateTime.now();
    String two(int x) => x.toString().padLeft(2, '0');
    final name = 'log_${n.year}${two(n.month)}${two(n.day)}_${two(n.hour)}${two(n.minute)}${two(n.second)}.csv';
    file = '${d.path}/$name';
    _out = File(file!).openWrite();
    _t0 = n;
    rows = 0;
    final cust = [for (var k = 0; k < AppSettings.nCustom; k++) s.customPid(k)];
    _out!.writeln([
      'time_s', 'clock',
      for (final m in kGMetrics) m.unit.isEmpty ? m.name : '${m.name} (${m.unit})',
      'CVT TEMP',
      for (var k = 0; k < cust.length; k++) cust[k].valid ? cust[k].label : 'CUSTOM ${k + 1}',
    ].join(','));
  }

  Future<void> close() async {
    final o = _out;
    _out = null;
    if (o != null) {
      await o.flush();
      await o.close();
    }
  }

  /// เรียก 10 ครั้ง/วินาที
  Future<void> tick(CarData d, bool connected) async {
    if (!s.logOn || !connected) {
      if (_out != null) await close();
      return;
    }
    final every = (10 / s.logHz.clamp(1, 10)).round();
    if (++_skip < every) return;
    _skip = 0;
    if (_out == null) await _open();
    final o = _out;
    if (o == null) return;
    final now = DateTime.now();
    String f(double? v, int dec) => v == null || v.isNaN ? '' : v.toStringAsFixed(dec);
    String two(int x) => x.toString().padLeft(2, '0');
    o.writeln([
      (now.difference(_t0!).inMilliseconds / 1000).toStringAsFixed(1),
      '${two(now.hour)}:${two(now.minute)}:${two(now.second)}',
      for (final m in kGMetrics) f(m.get(d, s), m.dec + 1),
      f(d.cvtTemp, 0),
      for (var k = 0; k < AppSettings.nCustom; k++) f(d.custNow(k), 2),
    ].join(','));
    rows++;
  }
}
