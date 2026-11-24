import 'dart:io';
import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:flutter/material.dart';

import '../data/hub.dart';
import '../data/skin.dart';
import 'live.dart';

/// ค่าของ metric ในสกิน (null = ยังไม่มีค่า)
double? skinVal(Frame f, String k) => switch (k) {
      'rpm' => f.rpm,
      'speed' => f.speed,
      'gear' => double.tryParse(f.gear),
      'ect' => f.ect,
      'iat' => f.iat,
      'volt' => f.volt,
      'load' => f.load,
      'tps' => f.tps,
      'map' => f.map,
      'boost' => f.boost,
      'maf' => f.maf,
      'fuel' => f.fuel,
      'afr' => f.afr,
      'cvt' => f.cvtTemp,
      'cvtratio' => f.cvtRatio,
      'oil' => f.oil,
      'timing' => f.timing,
      'fuelnow' => f.fuelNow,
      'fuelavg' => f.fuelAvg,
      'fuellph' => f.fuelLph,
      'trip' => f.trip,
      'cust1' => f.cust.isNotEmpty ? f.cust[0] : null,
      'cust2' => f.cust.length > 1 ? f.cust[1] : null,
      'cust3' => f.cust.length > 2 ? f.cust[2] : null,
      'cust4' => f.cust.length > 3 ? f.cust[3] : null,
      _ => null,
    };

Color skinColor(Object? v, [Color def = Colors.white]) {
  if (v is! String || !v.startsWith('#')) return def;
  var h = v.substring(1);
  if (h.length == 3) h = h.split('').map((c) => '$c$c').join();
  if (h.length == 6) h = 'FF$h';
  final n = int.tryParse(h, radix: 16);
  return n == null ? def : Color(n);
}

double _d(Map<String, dynamic> m, String k, double def) => (m[k] as num?)?.toDouble() ?? def;
bool _b(Map<String, dynamic> m, String k, [bool def = false]) => m[k] is bool ? m[k] as bool : def;

/// หน้าสกิน: โหลดไฟล์ครั้งเดียว แล้ววาดทุกเฟรมจากค่ารถ
class SkinPage extends StatefulWidget {
  const SkinPage({super.key, required this.id, required this.hub});
  final String id;
  final DataHub hub;
  @override
  State<SkinPage> createState() => _SkinPageState();
}

class _SkinPageState extends State<SkinPage> {
  SkinData? _s;
  String? _err;
  int _rev = -1;

  void _load() {
    _rev = widget.hub.settings.skinRev;
    SkinStore.load(widget.id).then((s) {
      if (mounted) setState(() => _s = s);
    }, onError: (Object e) {
      if (mounted) setState(() => _err = '$e');
    });
  }

  @override
  void initState() {
    super.initState();
    _load();
  }

  @override
  Widget build(BuildContext context) {
    if (_rev != widget.hub.settings.skinRev) _load();
    final s = _s;
    if (_err != null) {
      return Center(child: Text('สกินเปิดไม่ได้: $_err', style: const TextStyle(color: Colors.white70)));
    }
    if (s == null) return const Center(child: CircularProgressIndicator());
    final bgFile = s.file(s.json['bg'] as String?);
    final bgColor = skinColor(s.json['bgColor'], Colors.black);
    return ColoredBox(
      color: Colors.black,
      child: FittedBox(
        fit: BoxFit.contain,
        child: SizedBox(
          width: s.w,
          height: s.h,
          child: Stack(clipBehavior: Clip.hardEdge, children: [
            Positioned.fill(child: ColoredBox(color: bgColor)),
            if (bgFile != null)
              Positioned.fill(child: Image.file(File(bgFile), fit: BoxFit.fill, gaplessPlayback: true)),
            // รูป/GIF ที่ไม่ขึ้นกับค่า วาดครั้งเดียว (GIF เล่นเอง)
            for (final it in s.items)
              if (it['type'] == 'image') _pos(it, _image(s, it)),
            Positioned.fill(
              child: LiveBuilder(
                hub: widget.hub,
                builder: (_, f) => Stack(children: [
                  for (final it in s.items)
                    if (it['type'] != 'image') _pos(it, _item(s, it, f)),
                ]),
              ),
            ),
          ]),
        ),
      ),
    );
  }

  Widget _pos(Map<String, dynamic> it, Widget child) => Positioned(
        left: _d(it, 'x', 0),
        top: _d(it, 'y', 0),
        width: math.max(1, _d(it, 'w', 100)),
        height: math.max(1, _d(it, 'h', 40)),
        child: Transform.rotate(angle: _d(it, 'rot', 0) * math.pi / 180, child: child),
      );

  Widget _image(SkinData s, Map<String, dynamic> it) {
    final p = s.file(it['img'] as String?);
    if (p == null) return const SizedBox();
    return Opacity(
      opacity: _d(it, 'opacity', 1).clamp(0, 1),
      child: Image.file(File(p),
          fit: switch (it['fit']) { 'contain' => BoxFit.contain, 'cover' => BoxFit.cover, _ => BoxFit.fill },
          gaplessPlayback: true),
    );
  }

  Widget _item(SkinData s, Map<String, dynamic> it, Frame f) {
    final metric = (it['metric'] as String?) ?? 'rpm';
    final v = skinVal(f, metric);
    switch (it['type']) {
      case 'value':
        final info = kSkinMetrics[metric];
        final dec = (it['dec'] as num?)?.toInt() ?? info?.$3 ?? 0;
        String txt;
        if (metric == 'gear') {
          txt = f.gear.isEmpty ? '-' : f.gear;
        } else if (v == null) {
          txt = '--';
        } else {
          txt = v.toStringAsFixed(dec);
          if (_b(it, 'sep') && dec == 0 && v.abs() >= 1000) {
            txt = txt.replaceAllMapped(RegExp(r'(\d)(?=(\d{3})+$)'), (m) => '${m[1]},');
          }
        }
        if (_b(it, 'unit') && info != null && info.$2.isNotEmpty) txt = '$txt ${info.$2}';
        return _text(s, it, txt, _warn(it, v, f.time));
      case 'text':
        return _text(s, it, (it['text'] as String?) ?? '', null);
      case 'light':
        return _light(s, it, v, f.time);
      default:
        return CustomPaint(painter: _ShapePainter(it, v, f.time, _needleImg(s, it)), size: Size.infinite);
    }
  }

  /// สีเตือน: เกิน warnAbove / ต่ำกว่า warnBelow (กะพริบได้)
  Color? _warn(Map<String, dynamic> it, double? v, double t) {
    if (v == null) return null;
    final a = (it['warnAbove'] as num?)?.toDouble(), b = (it['warnBelow'] as num?)?.toDouble();
    if (!((a != null && v >= a) || (b != null && v <= b))) return null;
    if (_b(it, 'blink') && (t * 4).floor().isEven) return null;
    return skinColor(it['warnColor'], const Color(0xFFFF3246));
  }

  Widget _text(SkinData s, Map<String, dynamic> it, String txt, Color? warn) {
    final fam = s.family(it['font'] as String?);
    final size = _d(it, 'size', 40);
    final w = (it['weight'] as num?)?.toInt() ?? 700;
    final style = TextStyle(
      fontFamily: fam,
      fontSize: size,
      height: 1.0,
      fontStyle: _b(it, 'italic') ? FontStyle.italic : FontStyle.normal,
      fontWeight: FontWeight.values[((w ~/ 100) - 1).clamp(0, 8)],
      fontVariations: fam != null && kSkinVarFonts.contains(it['font']) ? [FontVariation('wght', w.toDouble())] : null,
      letterSpacing: _d(it, 'spacing', 0),
    );
    final col = warn ?? skinColor(it['color']);
    final align = switch (it['align']) { 'left' => Alignment.centerLeft, 'center' => Alignment.center, _ => Alignment.centerRight };
    final ta = switch (it['align']) { 'left' => TextAlign.left, 'center' => TextAlign.center, _ => TextAlign.right };
    final sw = _d(it, 'strokeW', 0);
    Widget t(TextStyle st) => Text(txt, style: st, textAlign: ta, maxLines: 1, softWrap: false);
    return OverflowBox(
      maxWidth: double.infinity,
      alignment: align,
      child: Align(
        alignment: align,
        child: Stack(children: [
          if (sw > 0)
            t(style.copyWith(
                foreground: Paint()
                  ..style = PaintingStyle.stroke
                  ..strokeWidth = sw
                  ..strokeJoin = StrokeJoin.round
                  ..color = skinColor(it['stroke'], Colors.black))),
          t(style.copyWith(color: col)),
        ]),
      ),
    );
  }

  Widget _light(SkinData s, Map<String, dynamic> it, double? v, double t) {
    final a = (it['above'] as num?)?.toDouble(), b = (it['below'] as num?)?.toDouble();
    var on = v != null && ((a != null && v >= a) || (b != null && v <= b));
    if (on && _b(it, 'blink') && (t * 5).floor().isEven) on = false;
    final img = s.file(it['img'] as String?);
    if (img != null) {
      return Opacity(
        opacity: on ? 1 : _d(it, 'offOpacity', 0.12).clamp(0, 1),
        child: Image.file(File(img), fit: BoxFit.contain, gaplessPlayback: true),
      );
    }
    final off = it['offColor'];
    final col = on ? skinColor(it['color'], const Color(0xFFFF3246)) : (off is String && off.isNotEmpty ? skinColor(off) : Colors.transparent);
    final shape = it['shape'] as String? ?? 'circle';
    return DecoratedBox(
      decoration: BoxDecoration(
        color: col,
        shape: shape == 'circle' ? BoxShape.circle : BoxShape.rectangle,
        borderRadius: shape == 'round' ? BorderRadius.circular(_d(it, 'h', 20) * 0.3) : null,
      ),
    );
  }

  final Map<String, ui.Image?> _imgs = {};
  final Set<String> _imgLoading = {};

  ui.Image? _needleImg(SkinData s, Map<String, dynamic> it) {
    if (it['type'] != 'needle') return null;
    final p = s.file(it['img'] as String?);
    if (p == null) return null;
    if (_imgs.containsKey(p)) return _imgs[p];
    if (_imgLoading.add(p)) {
      () async {
        try {
          final codec = await ui.instantiateImageCodec(await File(p).readAsBytes());
          final fr = await codec.getNextFrame();
          _imgs[p] = fr.image;
        } catch (_) {
          _imgs[p] = null;
        }
      }();
    }
    return null;
  }
}

/// แถบ (bar) / วงโค้ง (arc) / เข็ม (needle)
class _ShapePainter extends CustomPainter {
  _ShapePainter(this.it, this.v, this.t, this.img);
  final Map<String, dynamic> it;
  final double? v;
  final double t;
  final ui.Image? img;

  double get lo => _d(it, 'min', kSkinMetrics[it['metric']]?.$4 ?? 0);
  double get hi => _d(it, 'max', kSkinMetrics[it['metric']]?.$5 ?? 100);
  double get frac => v == null ? 0 : ((v! - lo) / (hi - lo == 0 ? 1 : hi - lo)).clamp(0.0, 1.0);
  double get redFrac {
    final r = (it['redFrom'] as num?)?.toDouble();
    return r == null ? 2 : ((r - lo) / (hi - lo == 0 ? 1 : hi - lo)).clamp(0.0, 1.0);
  }

  Color _grad(double k) => Color.lerp(skinColor(it['color'], const Color(0xFFFF5CC4)), skinColor(it['color2'] ?? it['color'], const Color(0xFFFF5CC4)), k)!;
  Color get offC => skinColor(it['offColor'], const Color(0xFF22162F));
  Color get redC => skinColor(it['redColor'], const Color(0xFFFF3246));

  @override
  void paint(Canvas cv, Size sz) {
    switch (it['type']) {
      case 'bar':
        _bar(cv, sz);
      case 'arc':
        _arc(cv, sz);
      case 'needle':
        _needle(cv, sz);
    }
  }

  void _bar(Canvas cv, Size sz) {
    final vert = it['dir'] == 'v';
    final n = (it['segments'] as num?)?.toInt() ?? 20;
    final style = it['style'] as String? ?? 'slant';
    final f = frac;
    if (n <= 0) {
      // แถบต่อเนื่อง
      cv.drawRect(Offset.zero & sz, Paint()..color = offC);
      final r = vert ? Rect.fromLTRB(0, sz.height * (1 - f), sz.width, sz.height) : Rect.fromLTWH(0, 0, sz.width * f, sz.height);
      final hot = f >= redFrac;
      cv.drawRect(r, Paint()..color = hot ? redC : _grad(f));
      return;
    }
    final gap = _d(it, 'gap', 0.2).clamp(0.0, 0.8);
    final lit = (f * n).round();
    final len = vert ? sz.height : sz.width;
    final pitch = len / n;
    final sk = style == 'slant' ? (vert ? 0.0 : sz.height * 0.5) : 0.0;
    for (var k = 0; k < n; k++) {
      final p0 = k * pitch, seg = pitch * (1 - gap);
      final kk = k / math.max(1, n - 1);
      final on = k < lit;
      final col = on ? ((k + 0.5) / n >= redFrac ? redC : _grad(kk)) : offC;
      final paint = Paint()..color = col;
      if (vert) {
        cv.drawRect(Rect.fromLTWH(0, sz.height - p0 - seg, sz.width, seg), paint);
      } else if (sk > 0) {
        final x = p0 * (sz.width - sk) / sz.width;
        final segW = seg * (sz.width - sk) / sz.width;
        cv.drawPath(
            Path()
              ..moveTo(x + sk, 0)
              ..lineTo(x + segW + sk, 0)
              ..lineTo(x + segW, sz.height)
              ..lineTo(x, sz.height)
              ..close(),
            paint);
      } else {
        cv.drawRect(Rect.fromLTWH(p0, 0, seg, sz.height), paint);
      }
    }
  }

  static double _rad(double deg) => (deg - 90) * math.pi / 180;

  void _arc(Canvas cv, Size sz) {
    final a0 = _d(it, 'a0', -135), a1 = _d(it, 'a1', 135);
    final th = _d(it, 'thick', 20);
    final c = sz.center(Offset.zero);
    final r = math.min(sz.width, sz.height) / 2 - th / 2;
    final rect = Rect.fromCircle(center: c, radius: r);
    final n = (it['segments'] as num?)?.toInt() ?? 0;
    final f = frac;
    final p = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = th;
    if (n <= 0) {
      p.strokeCap = _b(it, 'roundCap') ? StrokeCap.round : StrokeCap.butt;
      cv.drawArc(rect, _rad(a0), (a1 - a0) * math.pi / 180, false, p..color = offC);
      if (f > 0) {
        // ไล่สีทีละช่วงเล็ก ๆ (เหมือนในเว็บออกแบบ)
        const steps = 48;
        final m = (f * steps).ceil();
        p.strokeCap = StrokeCap.butt;
        for (var k = 0; k < m; k++) {
          final s0 = k / steps, s1 = math.min(f, (k + 1) / steps);
          p.color = s0 >= redFrac ? redC : _grad(s0);
          cv.drawArc(rect, _rad(a0 + (a1 - a0) * s0), (a1 - a0) * (s1 - s0) * math.pi / 180 * 1.02, false, p);
        }
      }
      return;
    }
    final gapDeg = _d(it, 'gap', 0.25).clamp(0.0, 0.9) * (a1 - a0) / n;
    final lit = (f * n).round();
    for (var k = 0; k < n; k++) {
      final s = a0 + (a1 - a0) * k / n;
      final on = k < lit;
      p.color = on ? ((k + 0.5) / n >= redFrac ? redC : _grad(k / math.max(1, n - 1))) : offC;
      cv.drawArc(rect, _rad(s), ((a1 - a0) / n - gapDeg) * math.pi / 180, false, p);
    }
  }

  void _needle(Canvas cv, Size sz) {
    final a0 = _d(it, 'a0', -135), a1 = _d(it, 'a1', 135);
    final ang = (a0 + (a1 - a0) * frac) * math.pi / 180;
    final c = sz.center(Offset.zero);
    final r = math.min(sz.width, sz.height) / 2;
    final len = r * _d(it, 'len', 0.9);
    cv.save();
    cv.translate(c.dx, c.dy);
    cv.rotate(ang);
    final im = img;
    if (im != null) {
      // รูปเข็มชี้ขึ้น: จุดหมุนที่ (px, py) ของรูป · ปลายเข็มห่างจุดหมุน = len
      final py = _d(it, 'py', 0.85).clamp(0.05, 1.0), px = _d(it, 'px', 0.5);
      final hgt = len / py, wid = hgt * im.width / im.height;
      cv.drawImageRect(im, Rect.fromLTWH(0, 0, im.width.toDouble(), im.height.toDouble()),
          Rect.fromLTWH(-wid * px, -hgt * py, wid, hgt), Paint()..filterQuality = FilterQuality.medium);
    } else {
      final th = _d(it, 'thick', 6), tail = len * _d(it, 'tail', 0.15);
      cv.drawPath(
          Path()
            ..moveTo(-th / 2, tail)
            ..lineTo(-th * 0.15, -len)
            ..lineTo(th * 0.15, -len)
            ..lineTo(th / 2, tail)
            ..close(),
          Paint()..color = skinColor(it['color'], const Color(0xFFFF3246)));
    }
    cv.restore();
    final cap = _d(it, 'cap', 0);
    if (cap > 0) cv.drawCircle(c, cap, Paint()..color = skinColor(it['capColor'], const Color(0xFF222222)));
  }

  @override
  bool shouldRepaint(covariant _ShapePainter o) => o.v != v || o.t != t || o.img != img;
}
