import 'dart:async';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:path_provider/path_provider.dart';

import '../settings.dart';
import 'car_data.dart';
import 'app_log.dart';
import 'native.dart';

// ======================================================================
//  ค่าที่เลือกได้ในกราฟ / บันทึก CSV
// ======================================================================
class GMetric {
  const GMetric(this.name, this.unit, this.lo, this.hi, this.dec, this.get, {this.custom});
  final String name, unit;
  final int? custom;
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
  GMetric('FUEL km/L', 'km/L', 0, 30, 1, (d, s) {
    final l = d.fuelLph;
    return l == null || d.speed < 3 ? null : (d.speed / (l < 0.2 ? 0.2 : l)).clamp(0, 99.9).toDouble();
  }),
  GMetric('LAMBDA', '', 0.7, 1.3, 2, (d, s) => d.lambda),
  GMetric('CVT RATIO', '', 0.4, 2.6, 2, (d, s) {
    if (d.speed < 8 || d.rpm < 500) return null;
    final v = d.rpm / (d.speed * 1000 / 60 / 1.91 * 4.989);
    return v < 0.3 || v > 3.2 ? null : v;
  }),
  GMetric('CVT TEMP', '°C', 40, 120, 0, (d, s) =>
      d.cvtTemp != null && DateTime.now().difference(d.cvtAt).inSeconds < 5 ? d.cvtTemp : null),
  for (var k = 0; k < 4; k++)
    GMetric('CUSTOM ${k + 1}', '', 0, 100, 1, (d, s) => d.custNow(k), custom: k),
];

/// ชื่อที่โชว์ (ช่อง Custom ใช้ชื่อ/หน่วย/ช่วงที่ตั้งไว้)
(String, String, double, double) gInfo(GMetric m, AppSettings s) {
  final k = m.custom;
  if (k == null) return (m.name, m.unit, m.lo, m.hi);
  final c = s.customPid(k);
  return c.valid ? (c.label, c.unit, c.lo, c.hi) : (m.name, '', m.lo, m.hi);
}

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

  /// เสียงเตือนที่เลือกได้ (id ตรงกับฝั่ง Kotlin playAlert)
  static const List<String> sounds = [
    'บี๊บสั้น', 'บี๊บคู่', 'ไซเรนแหลม', 'เตือนภัยยาว', 'ติ๊ด ๆ เร็ว', 'เสียงผิดพลาด', 'ติ๊ง-ต่อง',
    'ระฆังเตือน', 'ไซเรนรถแข่ง', '8-bit alarm', '', 'ปิดเสียง',
  ];

  /// id ที่เลือกได้ในเมนู (10 = เดิมเป็นเสียงพูด เอาออกแล้ว)
  static const List<int> choices = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11];

  /// ประเภทเตือน: (key, ชื่อ, เสียงเริ่มต้น)
  static const List<(String, String, int)> kinds = [
    ('ect', 'น้ำร้อน', 2),
    ('volt', 'แบตต่ำ', 3),
    ('shift', 'ถึงรอบเปลี่ยนเกียร์', 1),
    ('speed', 'ขับเร็วเกิน', 0),
    ('oil', 'ถึงระยะเปลี่ยนน้ำมันเครื่อง', 6),
  ];

  /// เล่นเสียงของเตือนชนิดนี้ (text = คำพูดถ้าเลือกเสียงพูด)
  Future<void> play(String key, String text, {int times = 1}) async {
    var id = s.alertSound(key);
    if (id >= 11) return;
    if (id == 10) id = 1; // เคยตั้งเป็นเสียงพูด -> บี๊บคู่
    for (var i = 0; i < times; i++) {
      await Native.alertSound(id, s.alertVol, text);
      if (i < times - 1) await Future<void>.delayed(Duration(milliseconds: id >= 7 ? 1700 : 1000));
    }
  }

  /// ข้อความเตือนที่โชว์อยู่ (key, ข้อความ, ร้ายแรง)  null = ไม่มี
  final ValueNotifier<(String, String, bool)?> banner = ValueNotifier(null);

  /// แตะปิดหน้าเตือน = เงียบเรื่องนั้น 2 นาที (ยังเห็นแถบเล็ก)
  final Map<String, DateTime> _snooze = {};
  void snooze(String key) {
    _snooze[key] = DateTime.now().add(const Duration(minutes: 2));
    _bump.value++;
  }

  final ValueNotifier<int> _bump = ValueNotifier(0);
  Listenable get changes => Listenable.merge([banner, _bump]);
  bool snoozed(String key) {
    final t = _snooze[key];
    return t != null && DateTime.now().isBefore(t);
  }

  static final _t0 = DateTime.fromMillisecondsSinceEpoch(0);
  DateTime _ectAt = _t0, _voltAt = _t0, _spdAt = _t0;
  DateTime? _connAt;
  bool _shiftArmed = true;
  int _voltLowN = 0;
  bool _oilShown = false, _oilBeeped = false;

  void tick(CarData d, bool connected) {
    final now = DateTime.now();
    if (!connected) {
      _connAt = null;
      banner.value = null;
      return;
    }
    _connAt ??= now;
    final settled = now.difference(_connAt!).inSeconds > 8; // เพิ่งต่อติด: ค่ายังไม่นิ่ง
    (String, String, bool)? show;

    // น้ำร้อน: เตือนทันที แล้วซ้ำทุก 60 วินาทีถ้ายังร้อน
    final ect = d.ect;
    if (s.alertEct && ect != null && ect >= s.ectWarn) {
      show = ('ect', 'น้ำร้อน ${ect.round()}°C', true);
      if (now.difference(_ectAt).inSeconds >= 60 && !snoozed('ect')) {
        _ectAt = now;
        play('ect', 'เตือน น้ำร้อน ${ect.round()} องศา', times: 3);
      }
    }
    // แบตต่ำ: ต่ำติดกัน ~3 วินาที (กันวูบตอนสตาร์ท)
    final v = d.volt;
    if (s.alertVolt && settled && v != null && v < s.alertVolt10 / 10 && d.rpm > 400) {
      _voltLowN++;
      if (_voltLowN > 30) {
        show ??= ('volt', 'แบตต่ำ ${v.toStringAsFixed(1)} V', true);
        if (now.difference(_voltAt).inSeconds >= 120 && !snoozed('volt')) {
          _voltAt = now;
          play('volt', 'เตือน แบตเตอรี่ต่ำ ${v.toStringAsFixed(1)} โวลต์', times: 2);
        }
      }
    } else {
      _voltLowN = 0;
    }
    // รอบถึงจุดเปลี่ยนเกียร์: เสียงครั้งเดียวต่อการเร่ง (ไม่ขึ้นหน้าเตือน)
    final sr = s.alertShiftRpm > 0 ? s.alertShiftRpm : s.shiftStart;
    if (s.alertShift) {
      if (_shiftArmed && d.rpm >= sr) {
        _shiftArmed = false;
        play('shift', 'เปลี่ยนเกียร์');
      } else if (d.rpm < sr - 600) {
        _shiftArmed = true;
      }
    }
    // ขับเร็วเกิน: ซ้ำทุก 6 วินาที
    if (s.alertSpeed && d.speed > s.alertSpeedKmh) {
      show ??= ('speed', 'เร็วเกิน ${d.speed.round()} km/h', false);
      if (now.difference(_spdAt).inSeconds >= 6 && !snoozed('speed')) {
        _spdAt = now;
        play('speed', 'ขับเร็วเกิน', times: 2);
      }
    }
    // ถึงระยะเปลี่ยนน้ำมันเครื่อง: โชว์ ~10 วินาทีหลังต่อติด (ครั้งเดียวต่อการเปิดแอป)
    if (!_oilShown && settled && s.oilEvery > 0 && s.live('oilKm') >= s.oilEvery) {
      final t = now.difference(_connAt!).inSeconds;
      if (t < 20 && !snoozed('oil')) {
        show ??= ('oil', 'ถึงระยะเปลี่ยนน้ำมันเครื่อง (${s.live('oilKm').round()} km)', false);
        if (!_oilBeeped) {
          _oilBeeped = true;
          play('oil', 'ถึงระยะเปลี่ยนน้ำมันเครื่องแล้ว');
        }
      } else {
        _oilShown = true;
      }
    }
    if (banner.value != show) {
      if (show != null && banner.value?.$1 != show.$1) AppLog.i('เตือน: ${show.$2}');
      banner.value = show;
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
    String head(GMetric m) {
      if (m.custom != null) {
        final c = s.customPid(m.custom!);
        return c.valid ? '${m.name} (${c.label.replaceAll(',', ' ')}${c.unit.isEmpty ? '' : ' ${c.unit}'})' : m.name;
      }
      return m.unit.isEmpty ? m.name : '${m.name} (${m.unit})';
    }
    _out!.writeln(['time_s', 'clock', for (final m in kGMetrics) head(m)].join(','));
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
    ].join(','));
    rows++;
  }
}


// ======================================================================
//  ปรับความเร็วอัตโนมัติด้วย GPS: เทียบความเร็ว GPS กับ OBD ตอนวิ่งนิ่ง ๆ
// ======================================================================
class GpsCal extends ChangeNotifier {
  GpsCal(this.s, this.d) {
    s.addListener(_sync);
    _sync();
  }
  final AppSettings s;
  final CarData d;
  StreamSubscription<dynamic>? _sub;
  int _views = 0; // หน้าตั้งค่าเปิดดูอยู่ = เปิด GPS แม้โหมดตั้งเอง
  double? gpsKmh, sacc;
  String err = '';
  double? _prev;
  double? est; // อัตราส่วนที่เรียนรู้ได้ (GPS / OBD)
  int n = 0;
  DateTime last = DateTime.fromMillisecondsSinceEpoch(0);

  bool get on => s.speedCalMode == 1 || _views > 0;

  void view(bool open) {
    _views += open ? 1 : -1;
    if (_views < 0) _views = 0;
    _sync();
  }

  void _sync() {
    if (on && _sub == null) {
      est = s.speedCalAutoN > 0 ? s.speedCalAuto1000 / 1000 : null;
      n = s.speedCalAutoN;
      _sub = Native.gps.receiveBroadcastStream().listen(_onFix, onError: (Object e) {
        err = '$e';
        notifyListeners();
      });
    } else if (!on && _sub != null) {
      _sub?.cancel();
      _sub = null;
      gpsKmh = null;
    }
  }

  void reset() {
    est = null;
    n = 0;
    s.setSpeedCalAuto(1000, 0);
    notifyListeners();
  }

  void _onFix(dynamic e) {
    if (e is! Map) return;
    if (e['err'] != null) {
      err = e['err'] == 'perm' ? 'ยังไม่ได้อนุญาตตำแหน่ง (GPS)' : '${e['err']}';
      _sub?.cancel();
      _sub = null;
      notifyListeners();
      return;
    }
    err = '';
    last = DateTime.now();
    final spd = (e['spd'] as num?)?.toDouble() ?? -1;
    final sa = (e['sacc'] as num?)?.toDouble() ?? -1;
    sacc = sa;
    if (spd < 0) {
      gpsKmh = null;
      notifyListeners();
      return;
    }
    final g = spd * 3.6;
    gpsKmh = g;
    final obd = d.speedRaw;
    final steady = _prev != null && (g - _prev!).abs() < 1.5;
    _prev = g;
    // เก็บเฉพาะตอนวิ่ง 40+ km/h ความเร็วนิ่ง และ GPS แม่น
    if (steady && g >= 40 && obd >= 35 && (sa < 0 || sa < 1.0)) {
      final r = g / obd;
      if (r > 0.85 && r < 1.15) {
        n++;
        est = est == null ? r : est! + (r - est!) * (n < 10 ? 1 / n : 0.05);
        if (n % 5 == 0 || n == 20) s.setSpeedCalAuto((est! * 1000).round(), n);
      }
    }
    notifyListeners();
  }

  @override
  void dispose() {
    _sub?.cancel();
    s.removeListener(_sync);
    super.dispose();
  }
}
