import 'dart:convert';
import 'dart:math' as math;

import '../settings.dart';

/// ======================================================================
///  ไดโนเสมือน: คิดแรงม้า/แรงบิดจากความเร่งของรถ (ความเร็ว + รอบจาก OBD)
///  แรงที่ล้อ = มวล x ความเร่ง + ลมต้าน + ล้อต้าน → กำลังที่ล้อ → หารการสูญเสีย = กำลังที่เครื่อง
///  แรงบิดเครื่อง = กำลัง / ความเร็วเชิงมุมของเครื่อง (ใช้ได้ทั้งเกียร์ธรรมดา / AT / CVT)
/// ======================================================================

/// ข้อมูลรถ (ค่าประมาณของรุ่นมาตรฐานในไทย — แก้ได้ในหน้าไดโน)
class CarPreset {
  const CarPreset(this.id, this.brand, this.name, this.mass, this.cd, this.area, this.trans, this.diesel, this.cc, this.ps);
  final String id, brand, name;

  /// น้ำหนักรถเปล่า (กก.) ยังไม่รวมคนขับ
  final double mass;
  final double cd, area;

  /// 'MT' / 'AT' / 'CVT'
  final String trans;
  final bool diesel;
  final int cc;

  /// แรงม้าโรงงาน (PS) ไว้เทียบ
  final int ps;
  String get title => '$brand $name';
}

const List<CarPreset> kCarPresets = [
  // ---- Honda
  CarPreset('jazz-gd-vtec', 'Honda', 'Jazz GD 1.5 VTEC (03-07)', 1050, .33, 2.15, 'CVT', false, 1497, 110),
  CarPreset('jazz-gd-idsi', 'Honda', 'Jazz GD 1.5 i-DSI (03-07)', 1040, .33, 2.15, 'CVT', false, 1497, 88),
  CarPreset('city-zx', 'Honda', 'City ZX 1.5 (05-07)', 1080, .32, 2.10, 'CVT', false, 1497, 110),
  CarPreset('jazz-ge', 'Honda', 'Jazz GE 1.5 (08-13)', 1100, .33, 2.20, 'AT', false, 1497, 120),
  CarPreset('city-gm2', 'Honda', 'City GM2 1.5 (08-13)', 1130, .31, 2.15, 'AT', false, 1497, 120),
  CarPreset('jazz-gk', 'Honda', 'Jazz GK 1.5 (14-22)', 1090, .32, 2.20, 'CVT', false, 1497, 117),
  CarPreset('city-gm6', 'Honda', 'City GM6 1.5 (14-19)', 1110, .31, 2.18, 'CVT', false, 1497, 117),
  CarPreset('city-gn', 'Honda', 'City GN 1.0 Turbo (20-)', 1100, .30, 2.20, 'CVT', false, 988, 122),
  CarPreset('brio', 'Honda', 'Brio / Amaze 1.2', 950, .33, 2.05, 'CVT', false, 1198, 90),
  CarPreset('mobilio', 'Honda', 'Mobilio 1.5', 1200, .35, 2.40, 'CVT', false, 1497, 120),
  CarPreset('brv', 'Honda', 'BR-V 1.5', 1250, .36, 2.50, 'CVT', false, 1497, 120),
  CarPreset('hrv', 'Honda', 'HR-V 1.8 (14-21)', 1250, .35, 2.50, 'CVT', false, 1799, 141),
  CarPreset('civic-fd18', 'Honda', 'Civic FD 1.8 (06-11)', 1260, .31, 2.20, 'AT', false, 1799, 140),
  CarPreset('civic-fd20', 'Honda', 'Civic FD 2.0 (06-11)', 1300, .31, 2.20, 'AT', false, 1998, 155),
  CarPreset('civic-fb', 'Honda', 'Civic FB 1.8 (12-15)', 1250, .30, 2.20, 'AT', false, 1799, 141),
  CarPreset('civic-fc18', 'Honda', 'Civic FC 1.8 (16-21)', 1250, .29, 2.20, 'CVT', false, 1799, 141),
  CarPreset('civic-fc15t', 'Honda', 'Civic FC 1.5 Turbo (16-21)', 1300, .29, 2.20, 'CVT', false, 1498, 173),
  CarPreset('civic-fk', 'Honda', 'Civic FK 1.5 Turbo Hatch (17-21)', 1330, .30, 2.20, 'CVT', false, 1498, 173),
  CarPreset('civic-fk8', 'Honda', 'Civic Type R FK8 2.0T', 1380, .30, 2.25, 'MT', false, 1996, 320),
  CarPreset('civic-fe', 'Honda', 'Civic FE 1.5 Turbo (21-)', 1330, .28, 2.20, 'CVT', false, 1498, 178),
  CarPreset('civic-fl5', 'Honda', 'Civic Type R FL5 2.0T', 1430, .30, 2.25, 'MT', false, 1996, 330),
  CarPreset('accord-g9', 'Honda', 'Accord G9 2.0 (13-18)', 1450, .29, 2.30, 'CVT', false, 1997, 155),
  CarPreset('accord-g10', 'Honda', 'Accord G10 1.5 Turbo', 1500, .28, 2.30, 'CVT', false, 1498, 190),
  CarPreset('crv-24', 'Honda', 'CR-V 2.4 (13-17)', 1550, .35, 2.60, 'AT', false, 2356, 190),
  CarPreset('crv-16d', 'Honda', 'CR-V 1.6 ดีเซล (17-)', 1700, .35, 2.60, 'AT', true, 1597, 160),
  // ---- Toyota
  CarPreset('vios-ncp93', 'Toyota', 'Vios 1.5 (07-13)', 1100, .30, 2.10, 'AT', false, 1497, 109),
  CarPreset('vios-nsp151', 'Toyota', 'Vios 1.5 (13-22)', 1100, .30, 2.10, 'CVT', false, 1496, 109),
  CarPreset('yaris-nsp152', 'Toyota', 'Yaris 1.2 (17-)', 1050, .31, 2.10, 'CVT', false, 1197, 86),
  CarPreset('yaris-ativ', 'Toyota', 'Yaris Ativ 1.2', 1060, .29, 2.10, 'CVT', false, 1197, 92),
  CarPreset('yaris-cross', 'Toyota', 'Yaris Cross 1.5', 1200, .34, 2.40, 'CVT', false, 1496, 106),
  CarPreset('altis-zze', 'Toyota', 'Altis 1.6 (01-07)', 1150, .30, 2.10, 'AT', false, 1598, 110),
  CarPreset('altis-zre172', 'Toyota', 'Altis 1.8 (14-18)', 1280, .29, 2.20, 'CVT', false, 1798, 140),
  CarPreset('altis-zre210', 'Toyota', 'Altis 1.8 (19-)', 1330, .28, 2.20, 'CVT', false, 1798, 140),
  CarPreset('camry-acv50', 'Toyota', 'Camry 2.5 (12-18)', 1500, .28, 2.35, 'AT', false, 2494, 181),
  CarPreset('chr', 'Toyota', 'C-HR 1.8', 1400, .32, 2.40, 'CVT', false, 1798, 140),
  CarPreset('innova', 'Toyota', 'Innova 2.0', 1600, .35, 2.70, 'AT', false, 1998, 136),
  CarPreset('vigo-25', 'Toyota', 'Hilux Vigo 2.5 ดีเซล', 1700, .45, 2.85, 'MT', true, 2494, 102),
  CarPreset('vigo-30', 'Toyota', 'Hilux Vigo 3.0 ดีเซล', 1850, .45, 2.85, 'AT', true, 2982, 163),
  CarPreset('revo-24', 'Toyota', 'Hilux Revo 2.4 ดีเซล', 1800, .44, 2.90, 'MT', true, 2393, 150),
  CarPreset('revo-28', 'Toyota', 'Hilux Revo 2.8 ดีเซล', 1950, .44, 2.90, 'AT', true, 2755, 204),
  CarPreset('fortuner-28', 'Toyota', 'Fortuner 2.8 ดีเซล', 2100, .40, 2.95, 'AT', true, 2755, 204),
  // ---- Nissan
  CarPreset('march', 'Nissan', 'March 1.2', 950, .33, 2.10, 'CVT', false, 1198, 79),
  CarPreset('almera-n17', 'Nissan', 'Almera 1.2 (11-19)', 1000, .30, 2.10, 'CVT', false, 1198, 79),
  CarPreset('almera-n18', 'Nissan', 'Almera 1.0 Turbo (20-)', 1050, .29, 2.10, 'CVT', false, 999, 100),
  CarPreset('note', 'Nissan', 'Note 1.2', 1050, .32, 2.20, 'CVT', false, 1198, 79),
  CarPreset('sylphy', 'Nissan', 'Sylphy 1.6', 1250, .29, 2.20, 'CVT', false, 1598, 116),
  CarPreset('navara-np300', 'Nissan', 'Navara NP300 2.5 ดีเซล', 1900, .43, 2.90, 'AT', true, 2488, 190),
  // ---- Mazda
  CarPreset('mazda2-13', 'Mazda', 'Mazda2 1.3 (15-)', 1050, .31, 2.10, 'AT', false, 1298, 93),
  CarPreset('mazda2-15d', 'Mazda', 'Mazda2 1.5 ดีเซล', 1100, .31, 2.10, 'AT', true, 1499, 105),
  CarPreset('mazda3-20', 'Mazda', 'Mazda3 2.0', 1300, .29, 2.20, 'AT', false, 1998, 165),
  CarPreset('cx3', 'Mazda', 'CX-3 2.0', 1300, .34, 2.40, 'AT', false, 1998, 156),
  CarPreset('cx5-20', 'Mazda', 'CX-5 2.0', 1500, .33, 2.60, 'AT', false, 1998, 165),
  CarPreset('cx5-22d', 'Mazda', 'CX-5 2.2 ดีเซล', 1650, .33, 2.60, 'AT', true, 2191, 175),
  CarPreset('bt50', 'Mazda', 'BT-50 3.0 ดีเซล (20-)', 1900, .44, 2.90, 'AT', true, 2999, 190),
  // ---- Mitsubishi
  CarPreset('mirage', 'Mitsubishi', 'Mirage 1.2', 870, .30, 2.05, 'CVT', false, 1193, 78),
  CarPreset('attrage', 'Mitsubishi', 'Attrage 1.2', 900, .30, 2.05, 'CVT', false, 1193, 78),
  CarPreset('lancer-ex', 'Mitsubishi', 'Lancer EX 1.8', 1300, .30, 2.20, 'CVT', false, 1798, 143),
  CarPreset('xpander', 'Mitsubishi', 'Xpander 1.5', 1250, .37, 2.55, 'AT', false, 1499, 105),
  CarPreset('triton-24', 'Mitsubishi', 'Triton 2.4 ดีเซล (15-)', 1850, .44, 2.90, 'MT', true, 2442, 181),
  CarPreset('pajero-sport', 'Mitsubishi', 'Pajero Sport 2.4 ดีเซล', 2050, .42, 2.95, 'AT', true, 2442, 181),
  // ---- Isuzu
  CarPreset('dmax-25', 'Isuzu', 'D-Max 2.5 ดีเซล (12-19)', 1750, .45, 2.90, 'MT', true, 2499, 136),
  CarPreset('dmax-19', 'Isuzu', 'D-Max 1.9 Blue Power', 1750, .45, 2.90, 'MT', true, 1898, 150),
  CarPreset('dmax-30', 'Isuzu', 'D-Max 3.0 ดีเซล', 1900, .45, 2.90, 'AT', true, 2999, 190),
  CarPreset('mux-30', 'Isuzu', 'MU-X 3.0 ดีเซล', 2000, .41, 2.90, 'AT', true, 2999, 190),
  // ---- Suzuki
  CarPreset('swift-12a', 'Suzuki', 'Swift 1.2 (12-17)', 950, .32, 2.10, 'CVT', false, 1197, 91),
  CarPreset('swift-12b', 'Suzuki', 'Swift 1.2 (18-)', 880, .31, 2.10, 'CVT', false, 1197, 83),
  CarPreset('ciaz', 'Suzuki', 'Ciaz 1.2', 950, .30, 2.10, 'CVT', false, 1242, 91),
  CarPreset('celerio', 'Suzuki', 'Celerio 1.0', 830, .33, 2.00, 'CVT', false, 998, 68),
  // ---- Ford / Chevrolet / MG
  CarPreset('fiesta', 'Ford', 'Fiesta 1.5', 1100, .32, 2.10, 'AT', false, 1499, 112),
  CarPreset('focus-20', 'Ford', 'Focus 2.0', 1350, .30, 2.20, 'AT', false, 1999, 170),
  CarPreset('ranger-22', 'Ford', 'Ranger 2.2 ดีเซล', 1950, .44, 2.95, 'MT', true, 2198, 160),
  CarPreset('ranger-20bt', 'Ford', 'Ranger 2.0 Bi-Turbo ดีเซล', 2100, .44, 2.95, 'AT', true, 1996, 213),
  CarPreset('everest-20', 'Ford', 'Everest 2.0 ดีเซล', 2200, .40, 3.00, 'AT', true, 1996, 180),
  CarPreset('cruze', 'Chevrolet', 'Cruze 1.8', 1400, .30, 2.20, 'AT', false, 1796, 141),
  CarPreset('sonic', 'Chevrolet', 'Sonic 1.4', 1150, .33, 2.20, 'AT', false, 1398, 100),
  CarPreset('colorado', 'Chevrolet', 'Colorado 2.5 ดีเซล', 1900, .44, 2.90, 'AT', true, 2499, 180),
  CarPreset('mg3', 'MG', 'MG3 1.5', 1150, .32, 2.20, 'AT', false, 1498, 112),
  CarPreset('mg5', 'MG', 'MG5 1.5', 1200, .30, 2.20, 'CVT', false, 1498, 114),
  CarPreset('mgzs', 'MG', 'ZS 1.5', 1250, .35, 2.40, 'CVT', false, 1498, 114),
];

CarPreset? presetById(String id) {
  for (final p in kCarPresets) {
    if (p.id == id) return p;
  }
  return null;
}

/// การสูญเสียในระบบส่งกำลัง (ล้อ → เครื่อง)
double defaultLoss(String trans) => switch (trans) { 'MT' => 0.13, 'AT' => 0.18, _ => 0.20 };

/// ค่าตั้งไดโนของรถที่ใช้อยู่ (เก็บเป็น JSON ใน AppSettings)
class DynoCfg {
  DynoCfg(Map<String, dynamic> m)
      : car = (m['car'] as String?) ?? 'jazz-gk',
        mass = (m['mass'] as num?)?.toDouble() ?? 1090,
        people = (m['people'] as num?)?.toDouble() ?? 75,
        cd = (m['cd'] as num?)?.toDouble() ?? .32,
        area = (m['area'] as num?)?.toDouble() ?? 2.2,
        trans = (m['trans'] as String?) ?? 'CVT',
        loss = (m['loss'] as num?)?.toDouble() ?? .20,
        diesel = m['diesel'] == true,
        cc = (m['cc'] as num?)?.toInt() ?? 1497,
        ps = (m['ps'] as num?)?.toInt() ?? 0,
        rot = (m['rot'] as num?)?.toDouble() ?? 1.06,
        trigger = (m['trig'] as num?)?.toDouble() ?? 70;

  String car, trans;
  double mass, people, cd, area, loss, rot, trigger;
  bool diesel;
  int cc, ps;

  Map<String, dynamic> toJson() => {
        'car': car, 'mass': mass, 'people': people, 'cd': cd, 'area': area, 'trans': trans, 'loss': loss,
        'diesel': diesel, 'cc': cc, 'ps': ps, 'rot': rot, 'trig': trigger,
      };

  void applyPreset(CarPreset p) {
    car = p.id;
    mass = p.mass;
    cd = p.cd;
    area = p.area;
    trans = p.trans;
    loss = defaultLoss(p.trans);
    diesel = p.diesel;
    cc = p.cc;
    ps = p.ps;
  }

  String get title => presetById(car)?.title ?? 'รถที่ตั้งเอง';

  static DynoCfg? _cur;
  static String _curSrc = '';
  static DynoCfg of(AppSettings s) {
    final src = s.dynoCfg;
    if (_cur == null || src != _curSrc) {
      _curSrc = src;
      Map<String, dynamic> m = {};
      try {
        final j = jsonDecode(src.isEmpty ? '{}' : src);
        if (j is Map<String, dynamic>) m = j;
      } catch (_) {}
      _cur = DynoCfg(m);
      if (src.isEmpty) _cur!.applyPreset(presetById('jazz-gk')!);
    }
    return _cur!;
  }

  void save(AppSettings s) {
    _cur = this;
    _curSrc = jsonEncode(toJson());
    s.dynoCfg = _curSrc;
  }

  /// แรงม้าโดยประมาณจากอากาศเข้า (เบนซินเท่านั้น): ราว 0.93 kW ต่อ 1 g/s
  /// ไม่มี MAF → คิดจาก MAP + รอบ + ขนาดเครื่อง (VE 0.85)
  double? livePs(double? maf, double? map, double rpm, double? iat) {
    if (diesel) return null;
    var m = maf;
    if ((m == null || m <= 0) && map != null && rpm > 300) {
      final t = (iat ?? 30) + 273.15;
      final rho = map * 1000 / (287.05 * t); // kg/m3
      m = rpm / 120 * (cc / 1e6) * 0.85 * rho * 1000; // g/s
    }
    if (m == null || m <= 0) return null;
    return m * 0.93 * 1000 / 735.5;
  }
}

/// ตัวอย่างที่เก็บระหว่างวัด
class DynoSample {
  DynoSample(this.t, this.rpm, this.kmh);
  final double t, rpm, kmh;
}

/// จุดบนกราฟผล: x = รอบ (หรือ km/h สำหรับ CVT ที่รอบนิ่ง)
class DynoPoint {
  DynoPoint(this.x, this.ps, this.nm);
  final double x, ps, nm;
}

class DynoResult {
  DynoResult(this.when, this.car, this.byRpm, this.pts, this.note);
  final DateTime when;
  final String car, note;

  /// true = แกน x เป็นรอบ · false = km/h
  final bool byRpm;
  final List<DynoPoint> pts;

  DynoPoint get peakPs => pts.reduce((a, b) => b.ps > a.ps ? b : a);
  DynoPoint get peakNm => pts.reduce((a, b) => b.nm > a.nm ? b : a);

  Map<String, dynamic> toJson() => {
        't': when.millisecondsSinceEpoch, 'car': car, 'rpm': byRpm, 'note': note,
        'p': [for (final p in pts) [p.x.round(), double.parse(p.ps.toStringAsFixed(1)), double.parse(p.nm.toStringAsFixed(1))]],
      };

  static DynoResult? fromJson(Object? j) {
    if (j is! Map) return null;
    try {
      final pts = [for (final e in (j['p'] as List)) DynoPoint((e[0] as num).toDouble(), (e[1] as num).toDouble(), (e[2] as num).toDouble())];
      if (pts.isEmpty) return null;
      return DynoResult(DateTime.fromMillisecondsSinceEpoch((j['t'] as num).toInt()), (j['car'] as String?) ?? '', j['rpm'] != false, pts,
          (j['note'] as String?) ?? '');
    } catch (_) {
      return null;
    }
  }

  static List<DynoResult> load(AppSettings s) {
    try {
      return [for (final e in (jsonDecode(s.dynoRuns) as List)) DynoResult.fromJson(e)].whereType<DynoResult>().toList();
    } catch (_) {
      return [];
    }
  }

  static void saveAll(AppSettings s, List<DynoResult> l) {
    final keep = l.length > 30 ? l.sublist(l.length - 30) : l;
    s.dynoRuns = jsonEncode([for (final r in keep) r.toJson()]);
  }
}

/// ค่าความเร็ว (m/s) และความเร่ง (m/s²) ณ เวลา tc: ฟิตเส้นโค้งกำลังสองในหน้าต่าง ±w วินาที
(double, double)? _fit(List<DynoSample> s, double tc, double w, double Function(DynoSample) y) {
  double n = 0, sx = 0, sx2 = 0, sx3 = 0, sx4 = 0, sy = 0, sxy = 0, sx2y = 0;
  for (final p in s) {
    final x = p.t - tc;
    if (x < -w || x > w) continue;
    final v = y(p);
    final x2 = x * x;
    n++;
    sx += x;
    sx2 += x2;
    sx3 += x2 * x;
    sx4 += x2 * x2;
    sy += v;
    sxy += x * v;
    sx2y += x2 * v;
  }
  if (n < 5) return null;
  // แก้สมการ [n sx sx2; sx sx2 sx3; sx2 sx3 sx4] [c b a] = [sy sxy sx2y]
  final m = [
    [n, sx, sx2, sy],
    [sx, sx2, sx3, sxy],
    [sx2, sx3, sx4, sx2y],
  ];
  for (var i = 0; i < 3; i++) {
    var p = i;
    for (var r = i + 1; r < 3; r++) {
      if (m[r][i].abs() > m[p][i].abs()) p = r;
    }
    final t = m[i];
    m[i] = m[p];
    m[p] = t;
    if (m[i][i].abs() < 1e-12) return null;
    for (var r = 0; r < 3; r++) {
      if (r == i) continue;
      final f = m[r][i] / m[i][i];
      for (var c = i; c < 4; c++) {
        m[r][c] -= f * m[i][c];
      }
    }
  }
  final c0 = m[0][3] / m[0][0], b = m[1][3] / m[1][1];
  return (c0, b); // ค่าที่ tc, อนุพันธ์ที่ tc
}

/// คิดผลจากข้อมูลที่เก็บได้ (คืน null + ข้อความถ้าข้อมูลไม่พอ)
(DynoResult?, String) computeDyno(List<DynoSample> raw, DynoCfg c) {
  final s = raw.where((e) => e.rpm > 300).toList();
  if (s.length < 20) return (null, 'ข้อมูลน้อยไป (ต้องกดยาวกว่านี้)');
  final t0 = s.first.t, t1 = s.last.t;
  if (t1 - t0 < 2.0) return (null, 'สั้นเกินไป — กดคันเร่งค้างอย่างน้อย 3-4 วินาที');
  final dv = s.last.kmh - s.first.kmh;
  if (dv < 12) return (null, 'ความเร็วเพิ่มน้อยไป (${dv.toStringAsFixed(0)} km/h) — ต้องเร่งให้ได้อย่างน้อย 15 km/h');
  // เกียร์ล็อก (ธรรมดา/AT ในเกียร์เดียว): ความเร็ว ∝ รอบ → ใช้รอบซึ่งละเอียดกว่าแทนความเร็ว
  final ratios = [for (final e in s) if (e.kmh > 10) e.kmh / e.rpm]..sort();
  var locked = false;
  double k = 0;
  if (ratios.length > 10) {
    k = ratios[ratios.length ~/ 2];
    final lo = ratios[(ratios.length * .1).floor()], hi = ratios[(ratios.length * .9).floor()];
    locked = (hi - lo) / k < 0.06;
  }
  final rpmSpan = s.map((e) => e.rpm).reduce(math.max) - s.map((e) => e.rpm).reduce(math.min);
  final byRpm = rpmSpan > 1500;
  const rhoAir = 1.18, g = 9.81, crr = 0.013;
  final m = c.mass + c.people;
  final mEff = m * c.rot;
  final cda = c.cd * c.area;
  // คิดทุก 0.05 วินาที
  final bins = <int, List<(double, double)>>{};
  const w = 0.6;
  for (var t = t0 + w; t <= t1 - w; t += 0.05) {
    final fr = _fit(s, t, w, (e) => e.rpm);
    final fv = locked ? fr == null ? null : (fr.$1 * k, fr.$2 * k) : _fit(s, t, w, (e) => e.kmh);
    if (fr == null || fv == null) continue;
    final rpm = fr.$1;
    final v = fv.$1 / 3.6, a = fv.$2 / 3.6;
    if (v < 2 || rpm < 800 || a <= 0) continue;
    final pWheel = (mEff * a + 0.5 * rhoAir * cda * v * v + crr * m * g) * v;
    final pEng = pWheel / (1 - c.loss.clamp(0.0, 0.5));
    final nm = pEng / (rpm * 2 * math.pi / 60);
    final x = byRpm ? rpm : v * 3.6;
    final key = byRpm ? (x / 100).round() * 100 : (x / 2).round() * 2;
    bins.putIfAbsent(key, () => []).add((pEng / 735.5, nm));
  }
  if (bins.length < 4) return (null, 'คำนวณไม่ได้ — ลองวัดใหม่ (กดสุดค้างไว้ ถนนเรียบ)');
  final keys = bins.keys.toList()..sort();
  double med(List<double> l) => (l..sort())[l.length ~/ 2];
  var pts = [
    for (final kx in keys)
      DynoPoint(kx.toDouble(), med([for (final e in bins[kx]!) e.$1]), med([for (final e in bins[kx]!) e.$2])),
  ];
  // เกลี่ยเส้น 3 จุด
  pts = [
    for (var i = 0; i < pts.length; i++)
      DynoPoint(
        pts[i].x,
        [for (var j = math.max(0, i - 1); j <= math.min(pts.length - 1, i + 1); j++) pts[j].ps].reduce((a, b) => a + b) /
            (math.min(pts.length - 1, i + 1) - math.max(0, i - 1) + 1),
        [for (var j = math.max(0, i - 1); j <= math.min(pts.length - 1, i + 1); j++) pts[j].nm].reduce((a, b) => a + b) /
            (math.min(pts.length - 1, i + 1) - math.max(0, i - 1) + 1),
      ),
  ];
  final note = '${locked ? 'เกียร์เดียว (ใช้รอบ)' : 'ใช้ความเร็ว'} · ${s.first.kmh.round()}-${s.last.kmh.round()} km/h · '
      '${(t1 - t0).toStringAsFixed(1)} วิ';
  return (DynoResult(DateTime.now(), c.title, byRpm, pts, note), '');
}
