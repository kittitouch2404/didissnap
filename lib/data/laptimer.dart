import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:math' as math;

import 'package:flutter/foundation.dart';
import 'package:flutter/painting.dart' show Offset;
import 'package:path_provider/path_provider.dart';

import 'app_log.dart';
import 'hub.dart';
import 'native.dart';

// ======================================================================
//  จับเวลาสนาม (GPS Lap Timer)
//  - ตั้งเส้นสตาร์ท/เส้นชัยด้วย GPS (ตำแหน่ง + ทิศที่วิ่งผ่าน)
//  - ตัดรอบด้วยการหาจุดตัดเส้น แล้วเฉลี่ยเวลาระหว่าง 2 จุด GPS (แม่นกว่าความถี่ GPS)
//  - บันทึกเส้นทางทุกจุด (lat/lon/ความเร็ว/รอบ) → ไฟล์ session ในเครื่อง, ส่งออก GPX/CSV ได้
// ======================================================================

/// แปลง lat/lon → เมตร รอบจุดอ้างอิง (พอสำหรับพื้นที่สนาม ไม่กี่กิโล)
class GeoProj {
  GeoProj(this.lat0, this.lon0) : kx = 111320.0 * math.cos(lat0 * math.pi / 180);
  final double lat0, lon0, kx;
  static const ky = 110540.0;
  Offset xy(double lat, double lon) => Offset((lon - lon0) * kx, (lat - lat0) * ky);
}

/// เส้นสตาร์ท/เส้นชัย: จุดกลาง + ทิศที่วิ่งผ่าน (องศา จากทิศเหนือ ตามเข็ม) + ความกว้าง
class LapLine {
  LapLine(this.lat, this.lon, this.hdg, {this.w = 30, this.name = '', List<LapLine>? splits}) : splits = splits ?? [];
  final double lat, lon, hdg, w;
  String name;
  /// จุดแบ่งช่วง (sector) ตามลำดับที่วิ่งผ่าน — สูงสุด 3 จุด = 4 ช่วง
  final List<LapLine> splits;

  Offset get dir => Offset(math.sin(hdg * math.pi / 180), math.cos(hdg * math.pi / 180));

  /// ปลายเส้นทั้งสองข้าง (เมตร ใน proj)
  (Offset, Offset) ends(GeoProj p) {
    final c = p.xy(lat, lon);
    final d = dir;
    final perp = Offset(d.dy, -d.dx) * (w / 2);
    return (c - perp, c + perp);
  }

  /// วิ่งจาก prev -> cur ตัดเส้นนี้ (ทิศเดียวกัน) ไหม · คืนสัดส่วนตำแหน่งที่ตัด 0..1 (null = ไม่ตัด)
  double? cross(double la0, double lo0, double la1, double lo1) {
    final pr = GeoProj(la0, lo0);
    final b = pr.xy(la1, lo1);
    final (p1, p2) = ends(pr);
    final sv = p2 - p1;
    final den = b.dx * sv.dy - b.dy * sv.dx;
    if (den.abs() < 1e-9) return null;
    final tt = (p1.dx * sv.dy - p1.dy * sv.dx) / den;
    final uu = (p1.dx * b.dy - p1.dy * b.dx) / den;
    if (tt < 0 || tt > 1 || uu < 0 || uu > 1) return null;
    if (b.dx * dir.dx + b.dy * dir.dy <= 0) return null; // วิ่งสวนทาง
    return tt;
  }

  Map<String, dynamic> toJson() => {
        'name': name, 'lat': lat, 'lon': lon, 'hdg': hdg, 'w': w,
        if (splits.isNotEmpty) 'splits': [for (final x in splits) x.toJson()],
      };
  static LapLine fromJson(Map<String, dynamic> j) => LapLine(
      (j['lat'] as num).toDouble(), (j['lon'] as num).toDouble(), (j['hdg'] as num).toDouble(),
      w: (j['w'] as num?)?.toDouble() ?? 30,
      name: '${j['name'] ?? ''}',
      splits: [for (final x in (j['splits'] as List? ?? [])) LapLine.fromJson((x as Map).cast<String, dynamic>())]);
}

/// จุด GPS 1 จุด (t = ms นับจากเริ่ม session)
class LapPt {
  LapPt(this.t, this.lat, this.lon, this.spd, this.rpm);
  final int t;
  final double lat, lon, spd, rpm;
  List<num> toJson() => [t, double.parse(lat.toStringAsFixed(7)), double.parse(lon.toStringAsFixed(7)),
      double.parse(spd.toStringAsFixed(1)), rpm.round()];
  static LapPt fromJson(List<dynamic> a) => LapPt((a[0] as num).toInt(), (a[1] as num).toDouble(),
      (a[2] as num).toDouble(), (a[3] as num).toDouble(), (a[4] as num).toDouble());
}

/// รอบที่วิ่งครบ: เวลาเริ่ม/จบ (ms จาก session, เฉลี่ยตรงจุดตัดเส้น) + ช่วง index ของจุด
class Lap {
  Lap(this.n, this.t0, this.t1, this.i0, this.i1, this.top, [this.splits = const []]);
  final int n, t0, t1, i0, i1;
  final double top;
  /// เวลาที่ผ่านจุดแบ่งแต่ละจุด (ms นับจากเริ่มรอบ) — ครบทุกจุด = มีเวลาแต่ละช่วง
  final List<int> splits;
  int get ms => t1 - t0;

  /// เวลาแต่ละช่วง (ms) — null ถ้าผ่านจุดแบ่งไม่ครบ
  List<int>? sectors(int nSplits) {
    if (nSplits == 0 || splits.length != nSplits) return null;
    final out = <int>[];
    var prev = 0;
    for (final x in splits) {
      out.add(x - prev);
      prev = x;
    }
    out.add(ms - prev);
    return out;
  }

  List<dynamic> toJson() => [n, t0, t1, i0, i1, double.parse(top.toStringAsFixed(1)), if (splits.isNotEmpty) splits];
  static Lap fromJson(List<dynamic> a) => Lap((a[0] as num).toInt(), (a[1] as num).toInt(), (a[2] as num).toInt(),
      (a[3] as num).toInt(), (a[4] as num).toInt(), (a[5] as num).toDouble(),
      a.length > 6 ? [for (final x in a[6] as List) (x as num).toInt()] : const []);
}

/// session = การลงสนาม 1 ครั้ง
class LapSession {
  LapSession(this.start);
  final DateTime start;
  LapLine? line;
  final pts = <LapPt>[];
  final laps = <Lap>[];
  String file = '';

  Lap? get best => laps.isEmpty ? null : laps.reduce((a, b) => a.ms <= b.ms ? a : b);
  int get nSplits => line?.splits.length ?? 0;

  /// เวลาดีสุดของแต่ละช่วง (ทุกรอบที่ผ่านจุดแบ่งครบ)
  List<int>? get bestSectors {
    final n = nSplits;
    if (n == 0) return null;
    List<int>? b;
    for (final l in laps) {
      final sc = l.sectors(n);
      if (sc == null) continue;
      b = b == null ? List.of(sc) : [for (var i = 0; i < sc.length; i++) math.min(b[i], sc[i])];
    }
    return b;
  }

  /// รอบในอุดมคติ = เอาช่วงที่ดีที่สุดของแต่ละช่วงมารวมกัน
  int? get theoretical => bestSectors?.fold<int>(0, (a, b) => a + b);
  double get top => pts.fold(0.0, (m, p) => math.max(m, p.spd));
  String get trackName => line?.name.isNotEmpty == true ? line!.name : 'ไม่มีชื่อสนาม';

  GeoProj? get proj => pts.isNotEmpty
      ? GeoProj(pts.first.lat, pts.first.lon)
      : (line != null ? GeoProj(line!.lat, line!.lon) : null);

  Map<String, dynamic> toJson() => {
        'v': 1,
        'start': start.toIso8601String(),
        if (line != null) 'line': line!.toJson(),
        'laps': [for (final l in laps) l.toJson()],
        'pts': [for (final p in pts) p.toJson()],
      };

  static LapSession fromJson(Map<String, dynamic> j) {
    final s = LapSession(DateTime.tryParse('${j['start']}') ?? DateTime.now());
    if (j['line'] is Map) s.line = LapLine.fromJson((j['line'] as Map).cast<String, dynamic>());
    for (final l in (j['laps'] as List? ?? [])) {
      s.laps.add(Lap.fromJson(l as List));
    }
    for (final p in (j['pts'] as List? ?? [])) {
      s.pts.add(LapPt.fromJson(p as List));
    }
    return s;
  }

  // ---------------- ส่งออก ----------------
  String toGpx() {
    final b = StringBuffer()
      ..writeln('<?xml version="1.0" encoding="UTF-8"?>')
      ..writeln('<gpx version="1.1" creator="Didissnap" xmlns="http://www.topografix.com/GPX/1/1">')
      ..writeln('<trk><name>${_esc(trackName)} ${start.toIso8601String()}</name>');
    var lapIdx = 0;
    b.writeln('<trkseg>');
    for (var i = 0; i < pts.length; i++) {
      // ขึ้น segment ใหม่ทุกครั้งที่เริ่มรอบใหม่
      while (lapIdx < laps.length && i > laps[lapIdx].i1) {
        lapIdx++;
        b.writeln('</trkseg><trkseg>');
      }
      final p = pts[i];
      final tm = start.add(Duration(milliseconds: p.t)).toUtc().toIso8601String();
      b.writeln('<trkpt lat="${p.lat.toStringAsFixed(7)}" lon="${p.lon.toStringAsFixed(7)}"><time>$tm</time>'
          '<extensions><speed>${(p.spd / 3.6).toStringAsFixed(2)}</speed><rpm>${p.rpm.round()}</rpm></extensions></trkpt>');
    }
    b
      ..writeln('</trkseg></trk></gpx>');
    return b.toString();
  }

  String toCsv() {
    final ns = nSplits;
    final b = StringBuffer('lap,time_s,time,top_kmh,best${[for (var i = 0; i <= ns && ns > 0; i++) ',s${i + 1}_s'].join()}\n');
    final bl = best;
    for (final l in laps) {
      final sc = l.sectors(ns);
      b.writeln('${l.n},${(l.ms / 1000).toStringAsFixed(3)},${fmtLap(l.ms)},${l.top.toStringAsFixed(0)},'
          '${identical(l, bl) ? 'best' : ''}${sc == null ? '' : sc.map((x) => ',${(x / 1000).toStringAsFixed(3)}').join()}');
    }
    final th = theoretical;
    if (th != null) b.writeln('theoretical,${(th / 1000).toStringAsFixed(3)},${fmtLap(th)}');
    b.writeln();
    b.writeln('t_s,lat,lon,speed_kmh,rpm');
    for (final p in pts) {
      b.writeln('${(p.t / 1000).toStringAsFixed(2)},${p.lat.toStringAsFixed(7)},${p.lon.toStringAsFixed(7)},'
          '${p.spd.toStringAsFixed(1)},${p.rpm.round()}');
    }
    return b.toString();
  }

  static String _esc(String s) => s.replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;');
}

String fmtLap(int? ms) {
  if (ms == null || ms < 0) return '--:--.--';
  final m = ms ~/ 60000, s = (ms % 60000) / 1000;
  return '$m:${s.toStringAsFixed(2).padLeft(5, '0')}';
}

String fmtDelta(double? s) => s == null ? '' : '${s >= 0 ? '+' : '−'}${s.abs().toStringAsFixed(2)}';

// ======================================================================
class LapTimer extends ChangeNotifier {
  LapTimer(this.hub);
  final DataHub hub;

  /// หน้าจับเวลาอยู่ในโหมดสนาม (จำไว้ระหว่างเปิดแอป)
  bool lapMode = false;

  // ---------------- GPS ----------------
  StreamSubscription<dynamic>? _sub;
  int _views = 0;
  String err = '';
  double? lat, lon, hacc, gpsKmh, heading;
  DateTime lastFix = DateTime.fromMillisecondsSinceEpoch(0);
  final _fixTimes = <int>[];
  int _wallOff = 0; // เวลาเครื่อง − เวลา GPS
  DateTime _lastHdgAt = DateTime.fromMillisecondsSinceEpoch(0);
  double? _hLat, _hLon;

  double get hz {
    final now = DateTime.now().millisecondsSinceEpoch;
    _fixTimes.removeWhere((t) => now - t > 5000);
    return _fixTimes.length / 5;
  }

  bool get hasFix => DateTime.now().difference(lastFix).inSeconds < 3 && lat != null;

  // ---------------- session ----------------
  LapSession? cur;
  bool get active => cur != null;
  LapLine? line;
  final tracks = <LapLine>[];
  int? _curT0; // เวลาเริ่มรอบปัจจุบัน (ms session) — null = ยังไม่ผ่านเส้นแรก (out lap)
  int _curI0 = 0;
  double _curDist = 0, _curTop = 0;
  final _curTrace = <(double, int)>[]; // (ระยะ m, เวลา ms จากเริ่มรอบ)
  List<(double, int)> _bestTrace = [];
  final _curSplits = <int>[]; // เวลาที่ผ่านจุดแบ่งในรอบนี้ (ms จากเริ่มรอบ)
  int? lastLapMs;
  List<int> get curSplits => _curSplits;
  double? lastLapDelta; // เทียบรอบที่ดีที่สุด (ก่อนรอบนี้)
  DateTime _lastCross = DateTime.fromMillisecondsSinceEpoch(0);
  int _gpsT0 = 0; // เวลา GPS ตอนเริ่ม session
  bool beep = true;

  // ---------------- เปิด/ปิด GPS ----------------
  void view(bool open) {
    _views = math.max(0, _views + (open ? 1 : -1));
    _syncGps();
  }

  Future<void> _syncGps() async {
    final want = _views > 0 || active;
    if (want && _sub == null) {
      if (!await Native.gpsPermission()) {
        err = 'ยังไม่ได้อนุญาตตำแหน่ง (GPS) — กดอนุญาตแล้วเปิดหน้านี้ใหม่';
        notifyListeners();
        return;
      }
      if (_sub != null || !(_views > 0 || active)) return;
      _sub = Native.gpsFast.receiveBroadcastStream().listen(_onFix, onError: (Object e) {
        err = '$e';
        notifyListeners();
      });
      if (tracks.isEmpty) _loadTracks();
    } else if (!want && _sub != null) {
      await _sub?.cancel();
      _sub = null;
    }
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
    final now = DateTime.now();
    final la = (e['lat'] as num).toDouble(), lo = (e['lon'] as num).toDouble();
    final t = (e['t'] as num).toInt();
    final ha = (e['hacc'] as num?)?.toDouble() ?? -1;
    final sp = (e['spd'] as num?)?.toDouble() ?? -1;
    final br = (e['brg'] as num?)?.toDouble() ?? -1;
    _fixTimes.add(now.millisecondsSinceEpoch);
    _wallOff = now.millisecondsSinceEpoch - t;
    lastFix = now;
    lat = la;
    lon = lo;
    hacc = ha;
    gpsKmh = sp >= 0 ? sp * 3.6 : null;
    // ทิศ: จาก GPS ถ้าวิ่งเร็วพอ ไม่งั้นคำนวณจากการเคลื่อนที่
    if (sp >= 2 && br >= 0) {
      heading = br;
      _lastHdgAt = now;
    } else if (_hLat != null) {
      final p = GeoProj(_hLat!, _hLon!).xy(la, lo);
      if (p.distance >= 4) {
        heading = (math.atan2(p.dx, p.dy) * 180 / math.pi + 360) % 360;
        _lastHdgAt = now;
      }
    }
    if (_hLat == null || GeoProj(_hLat!, _hLon!).xy(la, lo).distance >= 4) {
      _hLat = la;
      _hLon = lo;
    }
    if (active && (ha < 0 || ha <= 25)) _record(t, la, lo);
    notifyListeners();
  }

  // ---------------- บันทึก + ตัดรอบ ----------------
  void _record(int gpsT, double la, double lo) {
    final s = cur!;
    if (s.pts.isEmpty) _gpsT0 = gpsT;
    final t = gpsT - _gpsT0;
    final d = hub.data;
    // ความเร็ว: OBD ถ้าต่อรถอยู่ ไม่งั้นใช้ GPS
    final spd = hub.state == LinkState.connected ? d.speed : (gpsKmh ?? 0);
    final p = LapPt(t, la, lo, spd, d.rpm);
    // ยังไม่มีเส้น: หาเส้นที่บันทึกไว้ใกล้ ๆ
    if (line == null) _autoLine(la, lo);
    s.line = line;
    final prev = s.pts.isNotEmpty ? s.pts.last : null;
    s.pts.add(p);
    if (prev == null) return;
    final pr = GeoProj(prev.lat, prev.lon);
    final b = pr.xy(la, lo);
    final seg = b.distance;
    if (_curT0 != null) {
      _curDist += seg;
      _curTrace.add((_curDist, t - _curT0!));
      _curTop = math.max(_curTop, p.spd);
    }
    final ln = line;
    if (ln == null) return;
    // จุดแบ่งช่วง: ต้องผ่านตามลำดับ
    if (_curT0 != null && _curSplits.length < ln.splits.length) {
      final f = ln.splits[_curSplits.length].cross(prev.lat, prev.lon, la, lo);
      if (f != null) {
        final st = (prev.t + (t - prev.t) * f).round() - _curT0!;
        if (st > 3000) _curSplits.add(st);
      }
    }
    final tt = ln.cross(prev.lat, prev.lon, la, lo);
    if (tt == null) return;
    if (DateTime.now().difference(_lastCross).inSeconds < 10) return; // กันตัดซ้ำ
    _lastCross = DateTime.now();
    final ct = (prev.t + (t - prev.t) * tt).round();
    final idx = s.pts.length - 1;
    if (_curT0 != null) {
      final ms = ct - _curT0!;
      final bestBefore = s.best;
      final lap = Lap(s.laps.length + 1, _curT0!, ct, _curI0, idx - 1, _curTop, List.of(_curSplits));
      s.laps.add(lap);
      lastLapMs = ms;
      lastLapDelta = bestBefore == null ? null : (ms - bestBefore.ms) / 1000;
      if (bestBefore == null || ms < bestBefore.ms) {
        _bestTrace = List.of(_curTrace);
      }
      if (beep) Native.tone(bestBefore == null || ms < bestBefore.ms ? 3 : 0, 250, 90);
      AppLog.i('Lap ${lap.n}: ${fmtLap(ms)}');
      _save();
    }
    _curT0 = ct;
    _curI0 = idx;
    _curSplits.clear();
    _curDist = b.distance * (1 - tt);
    _curTop = p.spd;
    _curTrace
      ..clear()
      ..add((_curDist, t - ct));
  }

  // ---------------- ค่าสด ----------------
  /// เวลารอบปัจจุบัน (ms) — null = ยังไม่ผ่านเส้นสตาร์ท
  int? get curMs {
    if (!active || _curT0 == null || cur!.pts.isEmpty) return null;
    final nowGps = DateTime.now().millisecondsSinceEpoch - _wallOff - _gpsT0;
    return math.max(0, nowGps - _curT0!);
  }

  /// index เริ่มรอบปัจจุบัน (ไว้ไฮไลต์บนแผนที่)
  int get curFrom => _curT0 != null ? _curI0 : 0;

  int get lapNo => (cur?.laps.length ?? 0) + (_curT0 != null ? 1 : 0);

  /// คาดเวลารอบนี้ = รอบดีสุด + ช้า/เร็วกว่าตอนนี้
  int? get predicted {
    final b = cur?.best, d = liveDelta;
    if (b == null || d == null) return null;
    return b.ms + (d * 1000).round();
  }

  /// ช่วงที่กำลังวิ่งอยู่ (0 = ช่วงแรก)
  int get curSector => _curSplits.length;

  /// index ช่วงของรอบดีสุด (ไว้วาดเส้นเทียบบนแผนที่)
  (int, int)? get bestRange {
    final b = cur?.best;
    return b == null ? null : (b.i0, b.i1);
  }

  /// ต่าง/ช้ากว่ารอบที่ดีที่สุด ณ ระยะเดียวกัน (วินาที, + = ช้ากว่า)
  double? get liveDelta {
    if (_curT0 == null || _bestTrace.length < 2 || _curTrace.isEmpty) return null;
    final (dist, t) = _curTrace.last;
    final bt = _bestTrace;
    if (dist > bt.last.$1) return null;
    var lo = 0, hi = bt.length - 1;
    while (hi - lo > 1) {
      final m = (lo + hi) >> 1;
      if (bt[m].$1 < dist) {
        lo = m;
      } else {
        hi = m;
      }
    }
    final (d0, t0) = bt[lo];
    final (d1, t1) = bt[hi];
    final f = d1 > d0 ? (dist - d0) / (d1 - d0) : 0;
    return (t - (t0 + (t1 - t0) * f)) / 1000;
  }

  // ---------------- คำสั่ง ----------------
  Future<void> startSession() async {
    if (active) return;
    cur = LapSession(DateTime.now());
    _curT0 = null;
    _curTrace.clear();
    _bestTrace = [];
    lastLapMs = null;
    lastLapDelta = null;
    await _syncGps();
    notifyListeners();
  }

  Future<void> stopSession() async {
    final s = cur;
    if (s == null) return;
    if (s.pts.length > 5) await _save();
    cur = null;
    _curT0 = null;
    await _syncGps();
    notifyListeners();
  }

  /// ตั้งเส้นสตาร์ทตรงตำแหน่งปัจจุบัน — คืนข้อความผิดพลาด (null = สำเร็จ)
  String? setLineHere() {
    if (!hasFix) return 'ยังไม่มีสัญญาณ GPS';
    if (heading == null || DateTime.now().difference(_lastHdgAt).inSeconds > 30) {
      return 'ยังไม่รู้ทิศ — ขับผ่านเส้นสตาร์ทช้า ๆ แล้วกดตอนอยู่บนเส้น';
    }
    var n = 1;
    while (tracks.any((t) => t.name == 'สนาม $n')) {
      n++;
    }
    // แทนที่สนามเดิมที่อยู่ใกล้กัน (≤ 300 m)
    final near = _nearest(lat!, lon!, 300);
    final name = near?.name ?? 'สนาม $n';
    if (near != null) tracks.remove(near);
    line = LapLine(lat!, lon!, heading!, name: name, splits: near?.splits);
    tracks.add(line!);
    _saveTracks();
    cur?.line = line;
    _curT0 = null; // เริ่มนับใหม่ตอนผ่านเส้นครั้งถัดไป
    _curSplits.clear();
    _curTrace.clear();
    _bestTrace = [];
    notifyListeners();
    return null;
  }

  /// เพิ่มจุดแบ่งช่วงตรงนี้ (ต้องมีเส้นสตาร์ทก่อน · สูงสุด 3 จุด)
  String? addSplitHere() {
    final ln = line;
    if (ln == null) return 'ตั้งเส้นสตาร์ทก่อน';
    if (ln.splits.length >= 3) return 'จุดแบ่งครบ 3 จุดแล้ว (4 ช่วง)';
    if (!hasFix) return 'ยังไม่มีสัญญาณ GPS';
    if (heading == null || DateTime.now().difference(_lastHdgAt).inSeconds > 30) {
      return 'ยังไม่รู้ทิศ — กดตอนรถกำลังวิ่งผ่านจุดนั้น';
    }
    if (GeoProj(ln.lat, ln.lon).xy(lat!, lon!).distance < 60) return 'ใกล้เส้นสตาร์ทเกินไป';
    ln.splits.add(LapLine(lat!, lon!, heading!));
    _saveTracks();
    notifyListeners();
    return null;
  }

  void clearSplits(LapLine t) {
    t.splits.clear();
    _curSplits.clear();
    _saveTracks();
    notifyListeners();
  }

  void useTrack(LapLine? t) {
    line = t;
    _curSplits.clear();
    cur?.line = t;
    _curT0 = null;
    _curTrace.clear();
    _bestTrace = [];
    notifyListeners();
  }

  void renameTrack(LapLine t, String name) {
    t.name = name;
    _saveTracks();
    notifyListeners();
  }

  void deleteTrack(LapLine t) {
    tracks.remove(t);
    if (identical(line, t)) useTrack(null);
    _saveTracks();
    notifyListeners();
  }

  LapLine? _nearest(double la, double lo, double maxM) {
    LapLine? best;
    var bd = maxM;
    for (final t in tracks) {
      final d = GeoProj(la, lo).xy(t.lat, t.lon).distance;
      if (d <= bd) {
        bd = d;
        best = t;
      }
    }
    return best;
  }

  void _autoLine(double la, double lo) {
    final t = _nearest(la, lo, 2000);
    if (t != null) line = t;
  }

  // ---------------- ไฟล์ ----------------
  static Future<Directory> dir() async {
    final d = Directory('${(await getApplicationDocumentsDirectory()).path}/laps');
    if (!await d.exists()) await d.create(recursive: true);
    return d;
  }

  Future<void> _save() async {
    final s = cur;
    if (s == null) return;
    try {
      final d = await dir();
      if (s.file.isEmpty) {
        String two(int x) => x.toString().padLeft(2, '0');
        final t = s.start;
        s.file = '${d.path}/lap_${t.year}${two(t.month)}${two(t.day)}_${two(t.hour)}${two(t.minute)}${two(t.second)}.json';
      }
      await File(s.file).writeAsString(jsonEncode(s.toJson()));
    } catch (e) {
      AppLog.e('Lap save', e);
    }
  }

  Future<void> _loadTracks() async {
    try {
      final f = File('${(await dir()).path}/tracks.json');
      if (!await f.exists()) return;
      final l = jsonDecode(await f.readAsString()) as List;
      tracks
        ..clear()
        ..addAll(l.map((e) => LapLine.fromJson((e as Map).cast<String, dynamic>())));
      notifyListeners();
    } catch (_) {}
  }

  Future<void> _saveTracks() async {
    try {
      final f = File('${(await dir()).path}/tracks.json');
      await f.writeAsString(jsonEncode([for (final t in tracks) t.toJson()]));
    } catch (_) {}
  }

  /// รายการ session ที่บันทึกไว้ (ใหม่สุดก่อน)
  static Future<List<File>> sessions() async {
    final d = await dir();
    final l = d.listSync().whereType<File>().where((f) => f.path.split('/').last.startsWith('lap_')).toList()
      ..sort((a, b) => b.path.compareTo(a.path));
    return l;
  }

  static Future<LapSession?> load(File f) async {
    try {
      final s = LapSession.fromJson((jsonDecode(await f.readAsString()) as Map).cast<String, dynamic>());
      s.file = f.path;
      return s;
    } catch (_) {
      return null;
    }
  }

  @override
  void dispose() {
    _sub?.cancel();
    super.dispose();
  }
}
