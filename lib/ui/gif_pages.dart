import 'dart:io';
import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:flutter/material.dart' hide Text;
import '../i18n.dart';
import 'package:image_picker/image_picker.dart';
import 'package:path_provider/path_provider.dart';

import '../data/gif_cfg.dart';
import '../data/hub.dart';
import '../settings.dart';
import 'live.dart';
import '../data/native.dart';

/// กว้าง/สูง ต่ำกว่านี้ = จอแคบ (แบ่งจอกับแผนที่) → ใช้หน้าแนวตั้ง
const double gifNarrowRatio = 1.25;

/// หน้า GIF 1-5 (พอร์ตจากหน้า 9-13 ของจอ CYD)
///  แบบ A: กรอบ GIF + แถบรอบ + ความเร็ว/รอบ + ค่า 3 ช่องขวา (แตะช่องเพื่อสลับค่า)
///  แบบ B: GIF ใหญ่ด้านบน + รอบซ้ายล่าง + ความเร็วขวาล่าง (ไม่มีแถบ)
///  ตัวเลขแบบไตเติลอนิเมะ: ไล่สี -> ขาว, ขอบขาว + ขอบเข้ม, เงาเยื้อง  (กดค้างที่ตัวเลข = ตั้งค่าหน้า)
///  โหมดภาพ: 1 = ภาพค้างตามรอบ, 2 = เล่นวนเร็วตามความเร็วรถ, 0 = เล่นวนเร็วตามรอบ

/// ค่าที่เลือกใส่ช่องขวาได้
const List<(String, String, int)> kGifMetrics = [
  // (ชื่อ, หน่วย, ทศนิยม)
  ('COOLANT', '°C', 0),
  ('IAT', '°C', 0),
  ('VOLT', 'V', 1),
  ('LOAD', '%', 0),
  ('TPS', '%', 0),
  ('BOOST', 'bar', 2),
  ('AIRFLOW', 'g/s', 1),
  ('FUEL', '%', 0),
  ('AFR', '', 1),
  ('CVT RATIO', '', 2),
  ('CVT TEMP', '°C', 0),
  ('OIL TEMP', '°C', 0),
  ('TIMING', '°', 0),
  ('FUEL NOW', 'km/L', 1),
  ('FUEL AVG', 'km/L', 1),
  ('CUSTOM 1', '', 1),
  ('CUSTOM 2', '', 1),
  ('CUSTOM 3', '', 1),
  ('CUSTOM 4', '', 1),
];

/// ช่อง Custom PID เริ่มที่ index นี้
const int kGifCustom0 = 15;

/// ชื่อ/หน่วย/ทศนิยม (ช่อง Custom ใช้ที่ตั้งไว้ในหน้าตั้งค่า)
(String, String, int) gifMetricInfo(AppSettings s, int m) {
  if (m >= kGifCustom0) {
    final c = s.customPid(m - kGifCustom0);
    return (c.label, c.unit, c.dec.clamp(0, 2));
  }
  return kGifMetrics[m];
}

/// PID ที่ต้องอ่านสำหรับแต่ละค่า (ให้ K-line อ่านค่าที่โชว์อยู่ถี่ขึ้น)
const List<String> kGifMetricPid = ['05', '0F', 'RV', '04', '11', '0B', '10', '2F', '34', '0D', 'CT', '5C', '0E', '10', '10', 'X', 'X', 'X', 'X'];

double? _metricVal(Frame f, int m) => switch (m) {
      0 => f.ect,
      1 => f.iat,
      2 => f.volt,
      3 => f.load,
      4 => f.tps,
      5 => f.boost,
      6 => f.maf,
      7 => f.fuel,
      9 => f.cvtRatio,
      10 => f.cvtTemp,
      11 => f.oil,
      12 => f.timing,
      13 => f.fuelNow,
      14 => f.fuelAvg,
      >= 15 && <= 18 => f.cust[m - 15],
      _ => f.afr,
    };

(double, double) _metricRange(Frame f, int m) => switch (m) {
      0 => (40, 130),
      1 => (-10, 80),
      2 => (11, 15),
      5 => (-1, 1),
      6 => (0, 120),
      8 => (10, 20),
      9 => (0.4, 2.6),
      10 => (40, 120),
      11 => (40, 140),
      12 => (-10, 50),
      13 || 14 => (0, 30),
      >= 15 && <= 18 => (f.s.customPid(m - 15).lo, f.s.customPid(m - 15).hi),
      _ => (0, 100),
    };

/// เฟรมของ GIF ที่ถอดไว้แล้ว
class _Gif {
  _Gif(this.frames, this.ms);
  final List<ui.Image> frames;
  final List<int> ms;
  void dispose() {
    for (final f in frames) {
      f.dispose();
    }
  }
}

Future<_Gif?> _loadGif(String path, int targetW) async {
  final file = File(path);
  if (!await file.exists()) return null;
  final bytes = await file.readAsBytes();
  final probe = await ui.instantiateImageCodec(bytes);
  final first = await probe.getNextFrame();
  final w = first.image.width;
  first.image.dispose();
  final n = math.min(probe.frameCount, 150);
  probe.dispose();
  final codec = await ui.instantiateImageCodec(bytes,
      targetWidth: w > targetW ? targetW : null);
  final frames = <ui.Image>[];
  final ms = <int>[];
  for (var i = 0; i < n; i++) {
    final fr = await codec.getNextFrame();
    frames.add(fr.image);
    final d = fr.duration.inMilliseconds;
    ms.add(d <= 10 ? 80 : d);
  }
  codec.dispose();
  return _Gif(frames, ms);
}

Color _hsv(double h, double s, double v) =>
    HSVColor.fromAHSV(1, ((h % 360) + 360) % 360, s.clamp(0.0, 1.0), v.clamp(0.0, 1.0))
        .toColor();

class GifPage extends StatefulWidget {
  const GifPage({super.key, required this.index, required this.hub});
  final int index;
  final DataHub hub;

  @override
  State<GifPage> createState() => _GifPageState();
}

class _GifPageState extends State<GifPage> {
  _Gif? _gif;
  String _loaded = '';
  int _loadedW = 0;
  bool _loading = false;

  // สถานะเล่นภาพ
  int _idx = 0;
  double _acc = 0;
  double _lastT = -1;

  // สุ่มสี 2 โทน (สุ่มครั้งเดียวต่อการเปิดแอป/เปิดหน้า)
  GifCfg? _rc;
  String _rcSrc = '';
  GifCfg _rndCfg(GifCfg c) {
    final key = c.toJson().toString();
    if (_rc != null && _rcSrc == key) return _rc!;
    _rcSrc = key;
    final r = math.Random();
    final h = r.nextDouble() * 360;
    _rc = GifCfg(c.toJson())
      ..hue = h
      ..sat = 0.5 + r.nextDouble() * 0.4
      ..hue2 = (h + 40 + r.nextDouble() * 140) % 360   // โทน 2 ต่างจากโทน 1 พอให้ตัด
      ..sat2 = 0.6 + r.nextDouble() * 0.35;
    return _rc!;
  }

  // MAX / MIN ของแต่ละค่า (ตั้งแต่เปิดหน้า)
  final Map<int, double> _pk = {};

  AppSettings get s => widget.hub.settings;
  GifCfg get cfg => s.gifCfg(widget.index);

  @override
  void dispose() {
    _gif?.dispose();
    super.dispose();
  }

  void _ensureLoaded(GifCfg c) {
    final tw = c.style == 1 ? 1100 : 760;
    if (_loading || (c.path == _loaded && tw == _loadedW)) return;
    _loading = true;
    final path = c.path;
    () async {
      _Gif? g;
      try {
        if (path.isNotEmpty) g = await _loadGif(path, tw);
      } catch (_) {
        g = null;
      }
      if (!mounted) {
        g?.dispose();
        return;
      }
      setState(() {
        _gif?.dispose();
        _gif = g;
        _idx = 0;
        _loaded = path;
        _loadedW = tw;
        _loading = false;
      });
    }();
  }

  /// เลือกเฟรมที่จะแสดงตามโหมด
  void _advance(Frame f, GifCfg c) {
    final g = _gif;
    if (g == null || g.frames.isEmpty) return;
    final n = g.frames.length;
    final t = f.time;
    final dtMs = _lastT < 0 ? 0.0 : (t - _lastT) * 1000;
    _lastT = t;
    if (c.mode == 1) {
      final hi = c.hi > 0 ? c.hi : f.redline;
      final span = math.max(500, hi - c.lo);
      final pos = ((f.rpm - c.lo) / span).clamp(0.0, 1.0) * (n - 1);
      var tg = _idx;
      if (pos > _idx + 0.6) tg = (pos + 0.4).floor();
      if (pos < _idx - 0.6) tg = (pos - 0.4).ceil();
      _idx = tg.clamp(0, n - 1);
      return;
    }
    double rate;
    if (c.mode == 3) {
      rate = 1.0; // ความเร็วปกติของไฟล์ GIF (ไม่ขึ้นกับรถ)
    } else if (c.mode == 2) {
      rate = f.speed < 1 ? 0 : (f.speed / 60).clamp(0.0, 3.0);
    } else {
      final r = (f.rpm / math.max(1000, f.redline)).clamp(0.0, 1.2);
      rate = 0.6 + 1.8 * r;
    }
    _acc += dtMs * rate;
    var guard = 0;
    while (_acc >= g.ms[_idx] && guard < n) {
      _acc -= g.ms[_idx];
      _idx = (_idx + 1) % n;
      guard++;
    }
    if (guard >= n) _acc = 0;
  }

  void _trackPeaks(Frame f, GifCfg c) {
    for (final m in c.shown) {
      final v = _metricVal(f, m);
      if (v == null) continue;
      final isMin = m == 2 || m == 9;
      final o = _pk[m];
      if (o == null || (isMin ? v < o : v > o)) _pk[m] = v;
    }
  }

  void _tapAt(Offset p, Size size) {
    final c = cfg;
    if (c.style != 0) return;
    if (size.width < size.height * gifNarrowRatio) {
      for (final (i, r) in _GifDashPainter.narrowCells(size, c.six)) {
        if (r.contains(p)) {
          _cycleSlot(c, i);
          return;
        }
      }
      return;
    }
    if (p.dx < size.width * 0.63) return;
    final top = size.height * 0.09, h = size.height * 0.86 / 3;
    final row = ((p.dy - top) / h).floor();
    if (row < 0 || row > 2) return;
    // โหมด 6 ค่า: คอลัมน์ซ้าย = ช่อง 0-2, ขวา = ช่อง 3-5
    final right = c.six && p.dx > size.width * (0.645 + 0.985) / 2;
    _cycleSlot(c, row + (right ? 3 : 0));
  }

  void _cycleSlot(GifCfg c, int i) {
    var m = c.slots[i];
    for (var k = 0; k < kGifMetrics.length; k++) {
      m = (m + 1) % kGifMetrics.length;
      if (m >= kGifCustom0 && !s.customPid(m - kGifCustom0).valid) continue;   // ช่อง Custom ที่ยังไม่ได้ตั้ง = ข้าม
      if (!c.shown.contains(m)) break;
    }
    c.slots[i] = m;
    s.setGifCfg(widget.index, c);
  }

  /// ปุ่มแผนที่: จอเต็ม → แบ่งจอกับ Google Maps · แบ่งจออยู่ → กลับเต็มจอ
  Future<void> _mapTap(bool narrow) async {
    final r = await Native.mapSplit(exit: narrow);
    if (!mounted) return;
    final msg = switch (r) {
      'nomaps' => 'ไม่พบแอป Google Maps ในเครื่อง',
      'full' => 'จอนี้แบ่งจอให้ไม่ได้ — เปิดแผนที่เต็มจอแทน (เปิด "ตัวช่วยแบ่งจอ" ในตั้งค่าหน้านี้ แล้วลองใหม่)',
      'noexit' => 'ออกจากแบ่งจอ: ลากเส้นกลางจอไปสุดขอบ (หรือเปิด "ตัวช่วยแบ่งจอ" ให้กดปุ่มนี้ได้)',
      _ => '',
    };
    if (msg.isNotEmpty) {
      ScaffoldMessenger.of(context)
        ..hideCurrentSnackBar()
        ..showSnackBar(SnackBar(content: Text(msg), duration: const Duration(seconds: 5)));
    }
  }

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(builder: (context, box) {
      final size = Size(box.maxWidth, box.maxHeight);
      return GestureDetector(
        behavior: HitTestBehavior.opaque,
        onTapUp: (d) => _tapAt(d.localPosition, size),
        onLongPressStart: (d) {
          // กดค้างที่ตัวเลข (ครึ่งล่าง) = ตั้งค่าหน้านี้
          if (d.localPosition.dy > size.height * 0.5) {
            showGifSettings(context, s, widget.index);
          }
        },
        child: LiveBuilder(
          hub: widget.hub,
          builder: (_, f) {
            final c = cfg;
            _ensureLoaded(c);
            // ให้ ELM อ่านค่าที่โชว์ในหน้านี้ถี่ขึ้น (แบบ B ไม่มีช่องค่า)
            widget.hub.focusPids = c.style == 1
                ? const <String>[]
                : [for (final m in c.shown) kGifMetricPid[m]];
            _advance(f, c);
            _trackPeaks(f, c);
            final g = _gif;
            final img = (g != null && g.frames.isNotEmpty)
                ? g.frames[_idx.clamp(0, g.frames.length - 1)]
                : null;
            final paint = CustomPaint(
              painter: _GifDashPainter(f, c.rnd ? _rndCfg(c) : c, img, Map.of(_pk), _loading,
                  widget.index),
              child: const SizedBox.expand(),
            );
            if (!c.mapBtn) return paint;
            final narrow = size.width < size.height * gifNarrowRatio;
            // ขนาด/แบบเดียวกับปุ่ม MENU (มุมขวาบน) วางถัดไปทางซ้าย
            final col = Color(s.menuColor);
            return Stack(children: [
              paint,
              Positioned(
                right: 80,
                top: 6,
                child: Material(
                  color: Colors.black54,
                  shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(6), side: BorderSide(color: col)),
                  child: InkWell(
                    borderRadius: BorderRadius.circular(6),
                    onTap: () => _mapTap(narrow),
                    child: Padding(
                      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 5),
                      child: Row(mainAxisSize: MainAxisSize.min, children: [
                        Icon(narrow ? Icons.fullscreen : Icons.map_outlined, color: col, size: 15),
                        const SizedBox(width: 4),
                        Text(narrow ? 'FULL' : 'MAP',
                            style: TextStyle(color: col, fontWeight: FontWeight.w800, fontSize: 12)),
                      ]),
                    ),
                  ),
                ),
              ),
            ]);
          },
        ),
      );
    });
  }
}

class _GifDashPainter extends CustomPainter {
  _GifDashPainter(this.f, this.c, this.img, this.pk, this.loading, this.page);
  final Frame f;
  final GifCfg c;
  final ui.Image? img;
  final Map<int, double> pk;
  final bool loading;
  final int page;

  static const pink = Color(0xFFFF5CC4);
  static const vio = Color(0xFF8C46E6);
  static const outC = Color(0xFF4A1670);
  static const off = Color(0xFF22162F);
  static const lab = Color(0xFFC4B0DC);
  static const red = Color(0xFFFF3246);
  static const white = Color(0xFFFFF4FC);

  Color get top => _hsv(c.hue, c.sat, 1);
  /// โทนที่ 2: ขอบตัวเลข / เงา / กรอบใน / ต้นแถบ
  Color get edge => _hsv(c.hue2Eff + 10, math.max(0.5, c.sat2), c.sat < 0.08 ? 0.3 : 0.44);
  Color get shade => _hsv(c.hue2Eff + 10, 0.85, 0.17);
  Color get tone2 => _hsv(c.hue2Eff, c.sat2, 0.9);
  /// เส้นในของกรอบรูป (สีตามตัวเลข: ค่าเริ่มต้นชมพู -> ม่วง)
  Color get frameIn => tone2;

  @override
  void paint(Canvas cv, Size sz) {
    cv.drawRect(Offset.zero & sz, Paint()..color = Colors.black);
    final w = sz.width, h = sz.height;
    if (w < h * gifNarrowRatio) {   // แบ่งจอกับแผนที่ (จอแคบ) → จัดหน้าแนวตั้ง
      _paintNarrow(cv, w, h);
      return;
    }
    if (c.style == 1) {
      final fr = Rect.fromLTWH(w * 0.015, h * 0.09, w * 0.97, h * 0.66);
      _frame(cv, fr);
      _img(cv, fr.deflate(h * 0.012), big: true);
      final fs = h * 0.19;
      _anime(cv, _rpmTxt(), Offset(w * 0.40, h * 0.965), fs, f.atRedline, TextAlign.right);
      _label(cv, 'RPM', Offset(w * 0.41, h * 0.90), h * 0.045);
      _anime(cv, f.speed.round().toString(), Offset(w * 0.86, h * 0.965), fs, false, TextAlign.right);
      _label(cv, 'km/h', Offset(w * 0.87, h * 0.90), h * 0.045);
      return;
    }
    // ---- แบบ A ----
    final fr = Rect.fromLTWH(w * 0.015, h * 0.09, w * 0.60, h * 0.60);
    _frame(cv, fr);
    _img(cv, fr.deflate(h * 0.012), big: true);   // แบบ A ก็ซูม/เลื่อนภาพได้
    // แถบรอบเฉียง
    final barR = Rect.fromLTWH(fr.left, fr.bottom + h * 0.025, fr.width, h * 0.045);
    final maxR = (f.maxRpm).toDouble();
    final lit = (f.rpm / maxR).clamp(0.0, 1.0);
    final redAt = (f.redline / maxR).clamp(0.0, 1.0);
    _segBar(cv, barR, 40, lit, redAt, flash: f.atRedline && (f.time * 9).floor().isEven);
    // ความเร็ว + รอบ
    final fs = h * 0.17;
    _anime(cv, f.speed.round().toString(), Offset(w * 0.22, h * 0.965), fs, false, TextAlign.right);
    _label(cv, 'km/h', Offset(w * 0.23, h * 0.90), h * 0.04);
    _anime(cv, _rpmTxt(), Offset(w * 0.53, h * 0.965), fs, f.atRedline, TextAlign.right);
    _label(cv, 'RPM', Offset(w * 0.54, h * 0.90), h * 0.04);
    // ค่าช่องขวา: 3 ค่า (ตัวใหญ่) หรือ 6 ค่า (2 คอลัมน์)
    final xa = w * 0.645, xb = w * 0.985, top0 = h * 0.09, bh = h * 0.86 / 3;
    final nCol = c.six ? 2 : 1, gap = w * 0.02;
    final cw = (xb - xa - gap * (nCol - 1)) / nCol;
    for (var col = 0; col < nCol; col++) {
      final x0 = xa + col * (cw + gap), x1 = x0 + cw;
      final k = c.six ? 0.72 : 1.0; // ตัวเล็กลงในโหมด 6 ค่า
      for (var i = 0; i < 3; i++) {
        _slotCell(cv, c.slots[i + col * 3], x0, x1, top0 + bh * i, bh, k, c.six, h, w);
      }
    }
  }

  /// ช่องค่า 1 ช่อง (ชื่อ / MAX-MIN / ตัวเลข / หน่วย / แถบ)  u, uw = ขนาดอ้างอิง (ความสูง/กว้างจอแบบเต็ม)
  void _slotCell(Canvas cv, int m, double x0, double x1, double y, double bh, double k, bool small, double u, double uw) {
    final (name, unit, dec) = gifMetricInfo(f.s, m);
    _label(cv, name, Offset(x0, y), u * 0.045 * (small ? 0.8 : 1));
    final v = _metricVal(f, m);
    final isMin = m == 2 || m == 9;
    final p = pk[m];
    final pkTxt = '${isMin ? 'MIN' : 'MAX'} ${p == null ? '--' : p.toStringAsFixed(dec)}';
    if (c.pk && small) {
      _label(cv, pkTxt, Offset(x0, y + bh * 0.80 - u * 0.042), u * 0.026, color: top);
    } else if (c.pk) {
      _label(cv, pkTxt, Offset(x1, y + u * 0.006), u * 0.032, color: top, right: true);
    }
    final warn = (m == 0 && v != null && v >= f.ectWarn && (f.time * 3).floor().isEven) ||
        (m == 2 && v != null && v < 11.8);
    _anime(cv, v == null ? '--' : v.toStringAsFixed(dec),
        Offset(x1 - uw * 0.045 * k, y + bh * 0.70), u * 0.13 * k, warn, TextAlign.right);
    _label(cv, unit, Offset(x1 - uw * 0.04 * k, y + bh * 0.42), u * 0.035 * (small ? 0.85 : 1));
    final (lo, hi) = _metricRange(f, m);
    final t = v == null ? 0.0 : ((v - lo) / (hi - lo)).clamp(0.0, 1.0);
    final pt = p == null || !c.pk ? -1.0 : ((p - lo) / (hi - lo)).clamp(0.0, 1.0);
    _segBar(cv, Rect.fromLTWH(x0, y + bh * 0.80, x1 - x0, u * 0.035), small ? 10 : 18, t, 2, peak: pt);
  }

  /// จอแคบ (แบ่งจอกับแผนที่): รูปบน · ความเร็ว/รอบกลาง · ช่องค่าเรียง 3 คอลัมน์ล่าง
  void _paintNarrow(Canvas cv, double w, double h) {
    if (c.style == 1) {
      final fr = Rect.fromLTWH(w * 0.03, h * 0.04, w * 0.94, h * 0.62);
      _frame(cv, fr);
      _img(cv, fr.deflate(6), big: true);
      final fs = math.min(h * 0.15, w * 0.2);
      _anime(cv, f.speed.round().toString(), Offset(w * 0.36, h * 0.93), fs, false, TextAlign.right);
      _label(cv, 'km/h', Offset(w * 0.37, h * 0.86), fs * 0.25);
      _anime(cv, _rpmTxt(), Offset(w * 0.84, h * 0.93), fs, f.atRedline, TextAlign.right);
      _label(cv, 'RPM', Offset(w * 0.85, h * 0.86), fs * 0.25);
      return;
    }
    final fr = Rect.fromLTWH(w * 0.03, h * 0.03, w * 0.94, h * 0.37);
    _frame(cv, fr);
    _img(cv, fr.deflate(6), big: true);
    final maxR = (f.maxRpm).toDouble();
    _segBar(cv, Rect.fromLTWH(fr.left, fr.bottom + h * 0.018, fr.width, h * 0.026), 30,
        (f.rpm / maxR).clamp(0.0, 1.0), (f.redline / maxR).clamp(0.0, 1.0),
        flash: f.atRedline && (f.time * 9).floor().isEven);
    final fs = math.min(h * 0.11, w * 0.15);
    _anime(cv, f.speed.round().toString(), Offset(w * 0.36, h * 0.585), fs, false, TextAlign.right);
    _label(cv, 'km/h', Offset(w * 0.37, h * 0.535), fs * 0.26);
    _anime(cv, _rpmTxt(), Offset(w * 0.84, h * 0.585), fs, f.atRedline, TextAlign.right);
    _label(cv, 'RPM', Offset(w * 0.85, h * 0.535), fs * 0.26);
    for (final g in narrowCells(Size(w, h), c.six)) {
      final (i, r) = g;
      final u = r.height / 0.287, uw = r.width / 0.34;
      _slotCell(cv, c.slots[i], r.left, r.right, r.top, r.height, c.six ? 0.85 : 1.0, true, math.min(u, uw * 0.56), uw);
    }
  }

  /// ตำแหน่งช่องค่าในโหมดจอแคบ (ใช้ทั้งวาดและแตะ)
  static List<(int, Rect)> narrowCells(Size s, bool six) {
    final w = s.width, h = s.height;
    final rows = six ? 2 : 1;
    final y0 = h * 0.63, hh = h * 0.355, gap = w * 0.025;
    final cw = (w * 0.94 - gap * 2) / 3, rh = hh / rows;
    return [
      for (var r = 0; r < rows; r++)
        for (var col = 0; col < 3; col++)
          (r == 0 ? col : 3 + col, Rect.fromLTWH(w * 0.03 + col * (cw + gap), y0 + r * rh, cw, rh * 0.92)),
    ];
  }

  String _rpmTxt() => ((f.rpm / 10).round() * 10).toString();

  void _frame(Canvas cv, Rect r) {
    final rr = RRect.fromRectAndRadius(r, const Radius.circular(14));
    cv.drawRRect(rr, Paint()..color = const Color(0xFF12081C));
    cv.drawRRect(rr, Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 3
      ..color = top);
    cv.drawRRect(rr.deflate(3), Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2
      ..color = frameIn);
    final wp = Paint()
      ..color = white
      ..strokeWidth = 3;
    cv.drawLine(r.topLeft + const Offset(18, 0), r.topLeft + const Offset(58, 0), wp);
    cv.drawLine(r.topLeft + const Offset(0, 18), r.topLeft + const Offset(0, 48), wp);
    cv.drawLine(r.bottomRight - const Offset(18, 0), r.bottomRight - const Offset(58, 0), wp);
    cv.drawLine(r.bottomRight - const Offset(0, 18), r.bottomRight - const Offset(0, 48), wp);
  }

  void _img(Canvas cv, Rect r, {bool big = false}) {
    final i = img;
    if (i == null) {
      _label(cv, loading ? 'กำลังโหลด GIF...' : 'กดค้างที่ตัวเลข -> เลือก GIF',
          r.center - Offset(r.width * 0.25, 10), r.height * 0.07);
      return;
    }
    // ครอปให้เต็มกรอบ (cover)
    final iw = i.width.toDouble(), ih = i.height.toDouble();
    final sc = math.max(r.width / iw, r.height / ih) * (big ? c.bz : 1.0);
    final cw = r.width / sc, ch = r.height / sc;
    // แบบ B: เลื่อนตำแหน่งภาพในส่วนที่เกินกรอบ (bx/by = -1 ซ้าย/บน .. 1 ขวา/ล่าง)
    final ox = big ? c.bx : 0.0, oy = big ? c.by : 0.0;
    final src = Rect.fromLTWH((iw - cw) / 2 * (1 + ox), (ih - ch) / 2 * (1 + oy), cw, ch);
    cv.save();
    cv.clipRRect(RRect.fromRectAndRadius(r, const Radius.circular(10)));
    cv.drawImageRect(i, src, r, Paint()..filterQuality = FilterQuality.medium);
    cv.restore();
  }

  Color _segCol(double t) => t < 0.55
      ? Color.lerp(tone2, top, t / 0.55)!
      : Color.lerp(top, white, (t - 0.55) / 0.45)!;

  void _segBar(Canvas cv, Rect r, int n, double lit, double redAt,
      {bool flash = false, double peak = -1}) {
    final pitch = r.width / n, sk = r.height * 0.5;
    final litN = (lit * n).round();
    final pkN = peak < 0 ? -1 : (peak * n).round() - 1;
    for (var k = 0; k < n; k++) {
      final x = r.left + k * pitch;
      final on = k < litN;
      final isRed = k / n >= redAt;
      final col = on
          ? (flash || isRed ? red : _segCol(k / (n - 1)))
          : (k == pkN ? lab : (isRed ? const Color(0xFF3C0E16) : off));
      final p = Path()
        ..moveTo(x + sk, r.top)
        ..lineTo(x + pitch * 0.82 + sk, r.top)
        ..lineTo(x + pitch * 0.82, r.bottom)
        ..lineTo(x, r.bottom)
        ..close();
      cv.drawPath(p, Paint()..color = col);
    }
  }

  /// สีชื่อค่า/หน่วย (ตั้งได้ในหน้าตั้งค่า)
  Color get labC => c.labCol == 0 ? lab : (c.labCol == 1 ? top : Color(c.labCol));

  void _label(Canvas cv, String t, Offset at, double fs,
      {Color? color, bool right = false}) {
    final lf = kGifLabFonts[c.labFont.clamp(0, kGifLabFonts.length - 1)];
    final tp = TextPainter(
      text: TextSpan(
          text: tr(t),
          style: TextStyle(
              color: color ?? labC,
              fontSize: fs,
              fontFamily: lf.$2,
              fontVariations: lf.$3 ? const [FontVariation('wght', 700)] : null,
              fontWeight: FontWeight.w600)),
      textDirection: TextDirection.ltr,
    )..layout();
    tp.paint(cv, right ? at - Offset(tp.width, 0) : at);
  }

  /// ตัวเลขไตเติลอนิเมะ: เงาเยื้อง -> ขอบเข้ม -> ขอบขาว -> เนื้อไล่สี   (at = จุดเส้นฐาน)
  void _anime(Canvas cv, String t, Offset at, double fs0, bool hot, TextAlign align) {
    final nf = kGifNumFonts[c.numFont.clamp(0, kGifNumFonts.length - 1)];
    final fs = fs0 * c.numScale * (nf.$2 == 'DSEG7' ? 0.8 : 1);
    if (nf.$2 == 'DSEG7') t = t.replaceAll(',', '');   // ฟอนต์ 7 ส่วนไม่มีลูกน้ำ
    TextPainter mk(Paint p) => TextPainter(
          text: TextSpan(
            text: t,
            style: TextStyle(
                fontSize: fs,
                fontFamily: nf.$2,
                fontVariations: nf.$3 ? const [FontVariation('wght', 900)] : null,
                fontWeight: FontWeight.w900,
                fontStyle: c.numItalic && nf.$2 != 'DSEG7' ? FontStyle.italic : FontStyle.normal,
                letterSpacing: -fs * 0.02,
                foreground: p),
          ),
          textDirection: TextDirection.ltr,
        )..layout();
    final probe = mk(Paint());
    final base = probe.computeDistanceToActualBaseline(TextBaseline.alphabetic);
    final x = align == TextAlign.right ? at.dx - probe.width : at.dx;
    final o = Offset(x, at.dy - base);
    final c0 = hot ? red : top;
    final c1 = hot ? const Color(0xFFFFDCC8) : white;
    final sh = Offset(fs * 0.06, fs * 0.06);
    mk(Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = fs * 0.16
          ..strokeJoin = StrokeJoin.round
          ..color = shade)
        .paint(cv, o + sh);
    mk(Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = fs * 0.16
          ..strokeJoin = StrokeJoin.round
          ..color = edge)
        .paint(cv, o);
    mk(Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = fs * 0.07
          ..strokeJoin = StrokeJoin.round
          ..color = white)
        .paint(cv, o);
    final grad = ui.Gradient.linear(Offset(0, o.dy + fs * 0.25),
        Offset(0, o.dy + base), [c0, c1]);
    mk(Paint()..shader = grad).paint(cv, o);
  }

  @override
  bool shouldRepaint(covariant CustomPainter oldDelegate) => true;
}

// ======================================================================
//  แผงตั้งค่าของหน้า GIF (กดค้างที่ตัวเลข)
// ======================================================================
Future<void> showGifSettings(BuildContext context, AppSettings s, int index) {
  return showModalBottomSheet<void>(
    context: context,
    isScrollControlled: true,
    backgroundColor: const Color(0xFF14101C),
    builder: (ctx) => StatefulBuilder(builder: (ctx, setSt) {
      final c = s.gifCfg(index);
      void save() {
        s.setGifCfg(index, c);
        setSt(() {});
      }

      Future<void> pick() async {
        final x = await ImagePicker().pickImage(source: ImageSource.gallery);
        if (x == null) return;
        final docs = await getApplicationDocumentsDirectory();
        final dir = Directory('${docs.path}/gifpages');
        await dir.create(recursive: true);
        final dot = x.name.lastIndexOf('.');
        final ext = dot >= 0 ? x.name.substring(dot).toLowerCase() : '.gif';
        final dst = '${dir.path}/gif${index}_${DateTime.now().millisecondsSinceEpoch}$ext';
        await File(x.path).copy(dst);
        final old = c.path;
        c.path = dst;
        save();
        if (old.isNotEmpty && old != dst) {
          try {
            File(old).deleteSync();
          } catch (_) {}
        }
      }

      final red = s.redline.toDouble();
      final hi = c.hi > 0 ? c.hi.toDouble() : red;
      return SafeArea(
        child: Padding(
          padding: const EdgeInsets.fromLTRB(20, 12, 20, 12),
          child: SingleChildScrollView(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text('ตั้งค่าหน้า GIF ${index + 1}',
                    style: const TextStyle(fontSize: 20, fontWeight: FontWeight.bold)),
                SwitchListTile(
                  contentPadding: EdgeInsets.zero,
                  dense: true,
                  secondary: const Icon(Icons.map_outlined),
                  title: const Text('ปุ่มแผนที่ (แบ่งจอกับ Google Maps)'),
                  subtitle: const Text('ปุ่มมุมขวาบน: กด = แผนที่ครึ่งจอ หน้านี้อีกครึ่ง · กดอีกที = กลับเต็มจอ'),
                  value: c.mapBtn,
                  onChanged: (v) {
                    c.mapBtn = v;
                    save();
                  },
                ),
                if (c.mapBtn)
                  FutureBuilder<bool>(
                    future: Native.splitHelperOn(),
                    builder: (_, snap) {
                      final on = snap.data ?? false;
                      return ListTile(
                        contentPadding: EdgeInsets.zero,
                        dense: true,
                        leading: Icon(on ? Icons.check_circle : Icons.info_outline,
                            color: on ? const Color(0xFF00E05A) : Colors.orangeAccent),
                        title: Text(on ? 'ตัวช่วยแบ่งจอ: เปิดอยู่' : 'ตัวช่วยแบ่งจอ: ปิดอยู่'),
                        subtitle: const Text('จอรถส่วนใหญ่ต้องเปิดตัวนี้ แอปถึงสั่งแบ่งจอเองได้ '
                            '(ตั้งค่า → การช่วยเหลือพิเศษ / Accessibility → Didissnap แบ่งจอ → เปิด)'),
                        trailing: OutlinedButton(
                          onPressed: () async {
                            await Native.openAccessibility();
                            setSt(() {});
                          },
                          child: Text(on ? 'ตั้งค่า' : 'เปิด'),
                        ),
                      );
                    },
                  ),
                const SizedBox(height: 8),
                Row(children: [
                  FilledButton.icon(
                    onPressed: pick,
                    icon: const Icon(Icons.gif_box),
                    label: Text(c.path.isEmpty ? 'เลือก GIF' : 'เปลี่ยน GIF'),
                  ),
                  const SizedBox(width: 16),
                  SegmentedButton<int>(
                    segments: const [
                      ButtonSegment(value: 0, label: Text('แบบ A')),
                      ButtonSegment(value: 1, label: Text('แบบ B (ภาพใหญ่)')),
                    ],
                    selected: {c.style},
                    onSelectionChanged: (v) {
                      c.style = v.first;
                      save();
                    },
                  ),
                ]),
                // ---- ตัวอักษร: ฟอนต์ตัวเลข / ชื่อค่า + สีชื่อค่า
                const SizedBox(height: 10),
                const Text('ตัวเลข', style: TextStyle(fontWeight: FontWeight.bold)),
                SizedBox(
                  height: 58,
                  child: ListView(scrollDirection: Axis.horizontal, children: [
                    for (var i = 0; i < kGifNumFonts.length; i++)
                      _fontChip(kGifNumFonts[i], '123', c.numFont == i, _hsv(c.hue, c.sat, 1), () {
                        c.numFont = i;
                        save();
                      }, italic: c.numItalic, big: true),
                  ]),
                ),
                Row(children: [
                  const Text('ตัวเอียง'),
                  Switch(
                      value: c.numItalic,
                      onChanged: (v) {
                        c.numItalic = v;
                        save();
                      }),
                  const SizedBox(width: 12),
                  const Text('ขนาด'),
                  Expanded(
                    child: Slider(
                      min: 0.6,
                      max: 1.4,
                      divisions: 16,
                      value: c.numScale,
                      label: '${(c.numScale * 100).round()}%',
                      onChanged: (v) {
                        c.numScale = v;
                        save();
                      },
                    ),
                  ),
                  Text('${(c.numScale * 100).round()}%'),
                ]),
                const Text('ชื่อค่า / หน่วย', style: TextStyle(fontWeight: FontWeight.bold)),
                SizedBox(
                  height: 50,
                  child: ListView(scrollDirection: Axis.horizontal, children: [
                    for (var i = 0; i < kGifLabFonts.length; i++)
                      _fontChip(kGifLabFonts[i], 'COOLANT', c.labFont == i,
                          c.labCol == 0 ? const Color(0xFFC4B0DC) : (c.labCol == 1 ? _hsv(c.hue, c.sat, 1) : Color(c.labCol)), () {
                        c.labFont = i;
                        save();
                      }),
                  ]),
                ),
                const SizedBox(height: 6),
                Wrap(spacing: 8, runSpacing: 8, crossAxisAlignment: WrapCrossAlignment.center, children: [
                  const Text('สีชื่อค่า'),
                  for (final v in kGifLabColors)
                    InkWell(
                      onTap: () {
                        c.labCol = v;
                        save();
                      },
                      customBorder: const CircleBorder(),
                      child: Container(
                        width: 34,
                        height: 34,
                        alignment: Alignment.center,
                        decoration: BoxDecoration(
                          shape: BoxShape.circle,
                          color: v == 0 ? const Color(0xFFC4B0DC) : (v == 1 ? _hsv(c.hue, c.sat, 1) : Color(v)),
                          border: Border.all(color: c.labCol == v ? Colors.white : Colors.white24, width: c.labCol == v ? 3 : 1),
                        ),
                        child: v == 0
                            ? const Text('เดิม', style: TextStyle(fontSize: 10, color: Colors.black87))
                            : (v == 1 ? const Icon(Icons.link, size: 16, color: Colors.black87) : null),
                      ),
                    ),
                ]),
                const Text('ปุ่ม "เดิม" = ม่วงอ่อนแบบเดิม · ปุ่มรูปโซ่ = สีเดียวกับตัวเลข',
                    style: TextStyle(fontSize: 12, color: Colors.white54)),
                if (c.style == 0) ...[
                  const SizedBox(height: 8),
                  Row(children: [
                    const Text('ช่องค่าด้านขวา'),
                    const SizedBox(width: 12),
                    SegmentedButton<bool>(
                      segments: const [
                        ButtonSegment(value: false, label: Text('3 ค่า (ตัวใหญ่)')),
                        ButtonSegment(value: true, label: Text('6 ค่า (2 คอลัมน์)')),
                      ],
                      selected: {c.six},
                      onSelectionChanged: (v) {
                        c.six = v.first;
                        save();
                      },
                    ),
                  ]),
                  SwitchListTile(
                    contentPadding: EdgeInsets.zero,
                    dense: true,
                    title: const Text('โชว์ค่า MAX / MIN (peak)'),
                    subtitle: const Text('ปิด = เหลือแค่ตัวเลขกับแถบ ไม่มีค่าสูงสุด/ต่ำสุด และขีด peak บนแถบ'),
                    value: c.pk,
                    onChanged: (v) {
                      c.pk = v;
                      save();
                    },
                  ),
                ],
                ...[
                  const SizedBox(height: 8),
                  Row(children: [
                    const Text('ตำแหน่งภาพ (ซูม / เลื่อน)'),
                    const Spacer(),
                    TextButton(
                        onPressed: () {
                          c.bz = 1;
                          c.bx = 0;
                          c.by = 0;
                          save();
                        },
                        child: const Text('กลับค่าเดิม')),
                  ]),
                  _slRow('ซูม ${(c.bz * 100).round()}%', c.bz, 1, 3, (v) {
                    c.bz = v;
                    save();
                  }),
                  _slRow('ซ้าย ↔ ขวา', c.bx, -1, 1, (v) {
                    c.bx = v;
                    save();
                  }),
                  _slRow('บน ↕ ล่าง', c.by, -1, 1, (v) {
                    c.by = v;
                    save();
                  }),
                  const Text('ภาพเต็มกรอบอยู่แล้ว: ซูมเพิ่มก่อน ถึงจะเลื่อนซ้าย/ขวา/บน/ล่างได้',
                      style: TextStyle(fontSize: 12, color: Colors.white54)),
                ],
                const SizedBox(height: 12),
                const Text('การเล่นภาพ'),
                SegmentedButton<int>(
                  segments: const [
                    ButtonSegment(value: 1, label: Text('ค้างตามรอบ')),
                    ButtonSegment(value: 2, label: Text('วนเร็วตามความเร็วรถ')),
                    ButtonSegment(value: 0, label: Text('วนเร็วตามรอบ')),
                    ButtonSegment(value: 3, label: Text('ความเร็วปกติ')),
                  ],
                  selected: {c.mode},
                  onSelectionChanged: (v) {
                    c.mode = v.first;
                    save();
                  },
                ),
                if (c.mode == 1) ...[
                  const SizedBox(height: 8),
                  Text('ช่วงรอบ: ${c.lo} → ${hi.round()} rpm (ต่ำกว่า = เฟรมแรก, สูงกว่า = เฟรมสุดท้าย)'),
                  RangeSlider(
                    min: 0,
                    max: red,
                    divisions: math.max(1, (red / 100).round()),
                    values: RangeValues(math.min(c.lo.toDouble(), hi).clamp(0.0, red), hi.clamp(0.0, red)),
                    onChanged: (v) {
                      c.lo = v.start.round();
                      c.hi = v.end.round() >= red ? 0 : v.end.round();
                      save();
                    },
                  ),
                ],
                const SizedBox(height: 8),
                SwitchListTile(
                  contentPadding: EdgeInsets.zero,
                  dense: true,
                  title: const Text('สุ่มสี 2 โทนทุกครั้งที่เปิดแอป'),
                  subtitle: const Text('เปิดไว้ = ไม่ใช้สีที่เลือกด้านล่าง'),
                  value: c.rnd,
                  onChanged: (v) {
                    c.rnd = v;
                    save();
                  },
                ),
                Row(children: [
                  const Text('สีตัวเลข'),
                  const SizedBox(width: 12),
                  Container(
                    width: 60,
                    height: 24,
                    decoration: BoxDecoration(
                      borderRadius: BorderRadius.circular(6),
                      gradient: LinearGradient(
                          begin: Alignment.topCenter,
                          end: Alignment.bottomCenter,
                          colors: [_hsv(c.hue, c.sat, 1), Colors.white]),
                    ),
                  ),
                  const Spacer(),
                  TextButton(
                      onPressed: () {
                        c.hue = 322;
                        c.sat = 0.64;
                        c.hue2 = -1;
                        c.sat2 = 0.70;
                        save();
                      },
                      child: const Text('ค่าเริ่มต้น (ชมพู/ม่วง)')),
                  TextButton.icon(
                      onPressed: () {
                        final r = math.Random();
                        c.hue = r.nextDouble() * 360;
                        c.sat = 0.5 + r.nextDouble() * 0.4;
                        c.hue2 = (c.hue + 40 + r.nextDouble() * 140) % 360;
                        c.sat2 = 0.6 + r.nextDouble() * 0.35;
                        c.rnd = false;
                        save();
                      },
                      icon: const Icon(Icons.casino_outlined),
                      label: const Text('สุ่มเลย')),
                ]),
                const Text('โทน 1: ตัวเลข / กรอบนอก / ปลายแถบ'),
                Slider(
                  min: 0,
                  max: 359,
                  value: c.hue.clamp(0, 359),
                  activeColor: _hsv(c.hue, 0.9, 1),
                  onChanged: (v) {
                    c.hue = v;
                    save();
                  },
                ),
                const Text('ความสด (ซ้าย = ขาว)'),
                Slider(
                  min: 0,
                  max: 1,
                  value: c.sat.clamp(0, 1),
                  activeColor: _hsv(c.hue, c.sat, 1),
                  onChanged: (v) {
                    c.sat = v;
                    save();
                  },
                ),
                Row(children: [
                  const Text('โทน 2: กรอบใน / ขอบตัวเลข / ต้นแถบ'),
                  const SizedBox(width: 12),
                  Container(
                    width: 40,
                    height: 20,
                    decoration: BoxDecoration(
                      color: _hsv(c.hue2Eff, c.sat2, 0.9),
                      borderRadius: BorderRadius.circular(5),
                    ),
                  ),
                  const Spacer(),
                  if (c.hue2 >= 0)
                    TextButton(
                        onPressed: () {
                          c.hue2 = -1;
                          c.sat2 = 0.70;
                          save();
                        },
                        child: const Text('อัตโนมัติ (คู่กับโทน 1)')),
                ]),
                Slider(
                  min: 0,
                  max: 359,
                  value: c.hue2Eff.clamp(0, 359),
                  activeColor: _hsv(c.hue2Eff, 0.9, 1),
                  onChanged: (v) {
                    c.hue2 = v;
                    save();
                  },
                ),
                Slider(
                  min: 0,
                  max: 1,
                  value: c.sat2.clamp(0, 1),
                  activeColor: _hsv(c.hue2Eff, c.sat2, 0.9),
                  onChanged: (v) {
                    if (c.hue2 < 0) c.hue2 = c.hue2Eff;
                    c.sat2 = v;
                    save();
                  },
                ),
                Align(
                  alignment: Alignment.centerRight,
                  child: FilledButton(
                      onPressed: () => Navigator.pop(ctx), child: const Text('เสร็จ')),
                ),
              ],
            ),
          ),
        ),
      );
    }),
  );
}


Widget _slRow(String label, double v, double lo, double hi, ValueChanged<double> on) => Row(children: [
      SizedBox(width: 110, child: Text(label, style: const TextStyle(fontSize: 13))),
      Expanded(child: Slider(min: lo, max: hi, value: v.clamp(lo, hi), onChanged: on)),
    ]);


/// ปุ่มเลือกฟอนต์ (โชว์ตัวอย่างด้วยฟอนต์นั้น)
Widget _fontChip((String, String?, bool) f, String sample, bool sel, Color col, VoidCallback on,
    {bool italic = false, bool big = false}) {
  return Padding(
    padding: const EdgeInsets.only(right: 8),
    child: Material(
      color: sel ? const Color(0xFF2A2236) : const Color(0xFF1B1724),
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(10),
        side: BorderSide(color: sel ? col : Colors.white12, width: sel ? 2 : 1),
      ),
      child: InkWell(
        borderRadius: BorderRadius.circular(10),
        onTap: on,
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
          child: Column(mainAxisAlignment: MainAxisAlignment.center, children: [
            Text(f.$2 == 'DSEG7' ? sample.replaceAll(',', '') : sample,
                style: TextStyle(
                  fontFamily: f.$2,
                  fontVariations: f.$3 ? const [FontVariation('wght', 900)] : null,
                  fontWeight: FontWeight.w900,
                  fontStyle: italic && f.$2 != 'DSEG7' ? FontStyle.italic : FontStyle.normal,
                  fontSize: big ? 24 : 16,
                  height: 1.1,
                  color: col,
                )),
            Text(f.$1, style: const TextStyle(fontSize: 10, color: Colors.white60)),
          ]),
        ),
      ),
    ),
  );
}
