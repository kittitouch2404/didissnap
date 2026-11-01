import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../data/hub.dart';
import 'live.dart';
import 'paint_utils.dart';

// หน้าดีไซน์เพิ่มอีก 3 แบบ (วาดบนพื้นที่สมมติ 1000x460 เหมือน pro_pages.dart)

Paint _fill(Color c) => Paint()..color = c;
Paint _stroke(Color c, double w) => Paint()
  ..color = c
  ..style = PaintingStyle.stroke
  ..strokeWidth = w;
Offset _pol(Offset c, double a, double r) =>
    c + Offset(math.cos(a), math.sin(a)) * r;
bool _blink(Frame f) => (f.time * 8).floor().isEven;
const _green = Color(0xFF2EC84A);

// ======================================================================
//  Red stripe: เกจรอบวงใหญ่ซ้าย + พื้นแถบแดง + 4 ค่าด้านขวา + ไฟ shift ล่าง
// ======================================================================
class RedStripePainter extends CustomPainter {
  RedStripePainter(this.f);
  final Frame f;

  @override
  void paint(Canvas canvas, Size size) => drawStage(canvas, size, _draw);

  void _draw(Canvas c) {
    final blink = _blink(f);
    final max = f.maxRpm.toDouble();

    // กรอบคาร์บอน
    c.drawRect(const Rect.fromLTWH(0, 0, kVW, kVH), _fill(const Color(0xFF161616)));
    final hatch = _stroke(const Color(0xFF262626), 2);
    for (double i = -kVH; i < kVW; i += 8) {
      c.drawLine(Offset(i, 0), Offset(i + kVH, kVH), hatch);
    }

    const sx = 12.0, sy = 12.0, sw = kVW - 24, sh = kVH - 50;
    c.save();
    c.clipRect(const Rect.fromLTWH(sx, sy, sw, sh));
    for (var i = 0; i < 9; i++) {
      c.drawRect(
          Rect.fromLTWH(sx, sy + i * sh / 9, sw, sh / 9 + 1),
          _fill(i.isOdd ? const Color(0xFFB80E18) : const Color(0xFFD01420)));
    }

    final rows = <(String, String, String, bool)>[
      (fmt(f.iat), '°C', 'INTAKE AIR TEMP', false),
      (fmt(f.ect), '°C', 'WATER TEMP', f.ectHot),
      (fmt(f.volt, 1), 'V', 'BATTERY', false),
      (fmt(f.load), '%', 'ENGINE LOAD', false),
    ];
    const top = sy + 34;
    const rh = (sh - 34) / 4;
    for (var i = 0; i < rows.length; i++) {
      final y = top + i * rh - 8;
      c.drawRect(Rect.fromLTWH(560, y + 4, sw, rh - 6),
          _fill(Color(i.isOdd ? 0x38000000 : 0x14000000)));
      final r = rows[i];
      drawText(c, r.$1, Offset(930, y + 60),
          size: 58,
          weight: FontWeight.w900,
          italic: true,
          anchor: Alignment.bottomRight,
          color: r.$4 && blink ? const Color(0xFFFFE14A) : Colors.white);
      drawText(c, r.$2, Offset(975, y + 30),
          size: 20, weight: FontWeight.w800, anchor: Alignment.centerRight);
      drawText(c, r.$3, Offset(975, y + 68),
          size: 24, weight: FontWeight.w800, anchor: Alignment.topRight);
    }

    // ---- เกจ ----
    const ctr = Offset(265, 232);
    const rad = 225.0;
    const a0 = 120 * math.pi / 180, sweep = 300 * math.pi / 180;
    double ang(double v) => a0 + sweep * clampD(v / max, 0, 1);
    c.drawCircle(ctr, rad, _fill(const Color(0xFF0C0C0C)));
    const n = 100;
    for (var i = 0; i < n; i++) {
      final v = (i + 0.5) / n * max;
      final a = ang(v);
      final lit = v < f.rpm;
      final p = _pol(ctr, a, rad * 0.9);
      c.save();
      c.translate(p.dx, p.dy);
      c.rotate(a);
      c.drawRect(
          const Rect.fromLTWH(-7, -4, 14, 8),
          _fill(lit
              ? (v >= f.redline ? const Color(0xFFFF3A3A) : const Color(0xFFF2F2F2))
              : const Color(0xFF5A5A5A)));
      c.restore();
    }
    for (var k = 0; k * 1000 <= max; k++) {
      final a = ang(k * 1000.0);
      final p = _pol(ctr, a, rad * 0.97);
      c.save();
      c.translate(p.dx, p.dy);
      c.rotate(a);
      c.drawRect(const Rect.fromLTWH(-6, -5, 12, 10), _fill(const Color(0xFFFF2A2A)));
      c.restore();
      drawText(c, '$k', _pol(ctr, a, rad * 0.76),
          size: 34,
          weight: FontWeight.w900,
          italic: true,
          anchor: Alignment.center,
          color: k * 1000 >= f.redline
              ? const Color(0xFFFF6A6A)
              : const Color(0xFFE8E8E8));
    }

    const ri = rad * 0.6;
    final faceRect = Rect.fromCircle(center: ctr, radius: ri);
    c.drawCircle(
        ctr,
        ri,
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = 14
          ..shader = const LinearGradient(
            begin: Alignment.topCenter,
            end: Alignment.bottomCenter,
            colors: [Color(0xFFEEEEEE), Color(0xFF888888), Color(0xFFDDDDDD)],
          ).createShader(faceRect));
    c.drawCircle(
        ctr,
        ri - 7,
        Paint()
          ..shader = const RadialGradient(
            center: Alignment(0, 0.25),
            radius: 0.55,
            colors: [Color(0xFF2A0000), Color(0xFF8A0008), Color(0xFFE0101C)],
            stops: [0, 0.55, 1],
          ).createShader(faceRect));

    // ตัวชี้สามเหลี่ยมเหลือง
    final pa = ang(f.rpm);
    final pp = _pol(ctr, pa, ri + 22);
    c.save();
    c.translate(pp.dx, pp.dy);
    c.rotate(pa);
    c.drawPath(
        Path()
          ..moveTo(-14, 0)
          ..lineTo(10, -12)
          ..lineTo(10, 12)
          ..close(),
        _fill(const Color(0xFFFFD21E)));
    c.restore();

    // กรอบเกียร์ทรงคางหมู
    c.drawPath(
        Path()
          ..moveTo(ctr.dx - 80, ctr.dy - ri + 4)
          ..lineTo(ctr.dx + 80, ctr.dy - ri + 4)
          ..lineTo(ctr.dx + 52, ctr.dy - ri + 70)
          ..lineTo(ctr.dx - 52, ctr.dy - ri + 70)
          ..close(),
        _fill(const Color(0xFFE0141E)));
    drawText(c, f.gear, Offset(ctr.dx, ctr.dy - ri + 38),
        size: 56,
        weight: FontWeight.w900,
        anchor: Alignment.center,
        color: f.atRedline && blink ? const Color(0xFFFFE14A) : Colors.white);
    drawText(c, fmt(f.rpm), Offset(ctr.dx, ctr.dy + 12),
        size: 86, weight: FontWeight.w900, italic: true, anchor: Alignment.center);
    c.drawRect(Rect.fromLTWH(ctr.dx - 100, ctr.dy + 58, 200, 4), _fill(Colors.white));
    drawText(c, fmt(f.speed), Offset(ctr.dx, ctr.dy + 92),
        size: 50, weight: FontWeight.w900, anchor: Alignment.center);
    drawText(c, 'KMH', Offset(ctr.dx, ctr.dy + 124),
        size: 20, weight: FontWeight.w800, anchor: Alignment.center);
    c.restore();

    // ---- แถบเงินล่าง + ไฟ shift ----
    const by = kVH - 34;
    c.drawRect(
        const Rect.fromLTWH(sx, by, sw, 26),
        Paint()
          ..shader = const LinearGradient(
            begin: Alignment.topCenter,
            end: Alignment.bottomCenter,
            colors: [Color(0xFFE8E8E8), Color(0xFF9A9A9A)],
          ).createShader(const Rect.fromLTWH(sx, by, sw, 26)));
    const nl = 10;
    final span = math.max(1, f.redline - f.shiftStart);
    final lit = (clampD((f.rpm - f.shiftStart) / span, 0, 1) * nl).ceil();
    for (var i = 0; i < nl; i++) {
      var col = i < lit
          ? (i < 4 ? kGreen : (i < 7 ? kAmber : kRed))
          : const Color(0xFF3A3A3A);
      if (f.atRedline) col = blink ? kBlue : const Color(0xFF3A3A3A);
      c.drawRRect(
          RRect.fromRectAndRadius(
              Rect.fromLTWH(330.0 + i * 36, by + 5, 28, 16), const Radius.circular(4)),
          _fill(col));
    }
  }

  @override
  bool shouldRepaint(covariant RedStripePainter old) => true;
}

// ======================================================================
//  ECU: รอบตัวใหญ่ + แถบรอบไล่สีด้านบน + เกียร์/ความเร็วตัวใหญ่ + แถวค่าล่าง
// ======================================================================
class EcuPainter extends CustomPainter {
  EcuPainter(this.f);
  final Frame f;
  static const _y = Color(0xFFF5C518);

  @override
  void paint(Canvas canvas, Size size) => drawStage(canvas, size, _draw);

  void _lab(Canvas c, String s, Offset at, Alignment a) =>
      drawText(c, s, at, size: 20, weight: FontWeight.w700, color: _y, anchor: a);

  void _box(Canvas c, double x, double w, String title, double frac, Color col) {
    c.drawRect(Rect.fromLTWH(x, 418, w, 34), _stroke(const Color(0xFF666666), 2));
    c.drawRect(Rect.fromLTWH(x + 2, 420, (w - 4) * clampD(frac, 0, 1), 30), _fill(col));
    drawText(c, title, Offset(x + w / 2, 435),
        size: 16, weight: FontWeight.w800, color: Colors.black, anchor: Alignment.center);
  }

  void _draw(Canvas c) {
    final blink = _blink(f);
    final max = f.maxRpm.toDouble();
    drawText(c, fmt(f.rpm), const Offset(20, 24),
        size: 72,
        weight: FontWeight.w900,
        color: f.atRedline && blink ? kRed : Colors.white);
    drawText(c, 'RPM', const Offset(250, 74),
        size: 24, weight: FontWeight.w700, anchor: Alignment.bottomLeft);

    const bx = 20.0, bw = 960.0, by = 104.0, bh = 30.0;
    c.drawRect(const Rect.fromLTWH(bx, by, bw, bh), _fill(const Color(0xFF1A1A1A)));
    final ss = clampD(f.shiftStart / max, 0, 1);
    final rl = clampD(f.redline / max, ss, 1);
    c.drawRect(
        Rect.fromLTWH(bx, by, bw * clampD(f.rpm / max, 0, 1), bh),
        Paint()
          ..shader = LinearGradient(
            colors: const [
              Color(0xFFFFE600),
              Color(0xFFFFB000),
              Color(0xFFFF5A00),
              Color(0xFFFF1010)
            ],
            stops: [0, ss, rl, 1],
          ).createShader(const Rect.fromLTWH(bx, by, bw, bh)));
    for (var k = 1; k * 1000 <= max; k++) {
      final x = bx + bw * k * 1000 / max;
      c.drawRect(Rect.fromLTWH(x - 1, by, 2, bh), _fill(Colors.black));
      drawText(c, '$k', Offset(x, by + bh + 6),
          size: 18,
          anchor: Alignment.topCenter,
          color: k * 1000 >= f.redline ? kRed : const Color(0xFFDDDDDD));
    }

    _lab(c, 'TRIP km', const Offset(250, 170), Alignment.topRight);
    drawText(c, fmt(f.trip, 1), const Offset(250, 196),
        size: 30, weight: FontWeight.w800, anchor: Alignment.topRight);
    _lab(c, 'FUEL %', const Offset(250, 240), Alignment.topRight);
    drawText(c, fmt(f.fuel), const Offset(250, 266),
        size: 30, weight: FontWeight.w800, anchor: Alignment.topRight);

    _lab(c, 'GEAR', const Offset(430, 170), Alignment.topCenter);
    drawText(c, f.gear, const Offset(430, 268),
        size: 140, weight: FontWeight.w900, anchor: Alignment.center);
    _lab(c, 'SPEED', const Offset(920, 170), Alignment.topRight);
    drawText(c, fmt(f.speed), const Offset(920, 268),
        size: 140, weight: FontWeight.w900, anchor: Alignment.centerRight);
    drawText(c, 'km/h', const Offset(975, 320),
        size: 20, color: const Color(0xFFCCCCCC), anchor: Alignment.bottomRight);

    final cols = <(String, String, bool)>[
      ('WATER °C', fmt(f.ect), f.ectHot),
      ('BATT V', fmt(f.volt, 1), false),
      ('BOOST bar', fmt(f.boost, 2), false),
      ('LOAD %', fmt(f.load), false),
      ('IAT °C', fmt(f.iat), false),
    ];
    for (var i = 0; i < cols.length; i++) {
      final x = 100.0 + i * 200;
      _lab(c, cols[i].$1, Offset(x, 342), Alignment.topCenter);
      drawText(c, cols[i].$2, Offset(x, 368),
          size: i == 2 ? 40 : 34,
          weight: FontWeight.w900,
          anchor: Alignment.topCenter,
          color: cols[i].$3
              ? (blink ? kRed : const Color(0xFFFF8A3A))
              : Colors.white);
    }

    _box(c, 20, 180, 'TPS ${fmt(f.tps)}%', (f.tps ?? 0) / 100, _green);
    _box(c, 220, 360, 'FUEL', (f.fuel ?? 0) / 100, _green);
    for (final v in const [0, 50, 100]) {
      drawText(c, '$v', Offset(220 + 360 * v / 100, 412),
          size: 13, anchor: Alignment.bottomCenter, color: const Color(0xFFAAAAAA));
    }
    _box(c, 600, 180, 'ENGINE', 1,
        f.rpm > 400 ? (f.ectHot ? kRed : _green) : const Color(0xFF555555));
    _box(c, 800, 180, 'LINK', 1, f.link == LinkState.connected ? _green : kAmber);
  }

  @override
  bool shouldRepaint(covariant EcuPainter old) => true;
}

// ======================================================================
//  Center tach: เกจกลางจอ (เหลือง/แดง) + 4 ช่องซ้าย + 4 ช่องขวา
// ======================================================================
class CenterTachPainter extends CustomPainter {
  CenterTachPainter(this.f);
  final Frame f;
  static const _yl = Color(0xFFF2E04A);

  @override
  void paint(Canvas canvas, Size size) => drawStage(canvas, size, _draw);

  void _strip(Canvas c, double x, double y, double h, double frac) {
    const n = 10;
    final on = (clampD(frac, 0, 1) * n).round();
    for (var i = 0; i < n; i++) {
      c.drawRect(
          Rect.fromLTWH(x, y + h - 6 - (i + 1) * (h - 8) / n, 16, (h - 8) / n - 3),
          _fill(i < on ? _yl : const Color(0xFF3A3A2A)));
    }
  }

  void _cells(Canvas c, double x, bool right,
      List<(String, String, double, bool)> list, bool blink) {
    const w = 255.0, h = 100.0;
    for (var i = 0; i < list.length; i++) {
      final y = 14.0 + i * 110;
      final it = list[i];
      final r = Rect.fromLTWH(x, y, w, h);
      c.drawRect(
          r,
          Paint()
            ..shader = LinearGradient(
              colors: right
                  ? const [Color(0xFF222222), Color(0xFF3A3A3A)]
                  : const [Color(0xFF3A3A3A), Color(0xFF222222)],
            ).createShader(r));
      _strip(c, right ? x + w - 24 : x + 8, y, h, it.$3);
      final tx = right ? x + w - 36 : x + 34;
      drawText(c, it.$2, Offset(tx, y + 70),
          size: 58,
          weight: FontWeight.w900,
          anchor: right ? Alignment.bottomRight : Alignment.bottomLeft,
          color: it.$4 && blink ? kRed : _yl);
      drawText(c, it.$1, Offset(tx, y + 84),
          size: 13,
          weight: FontWeight.w700,
          color: const Color(0xFFBBBBBB),
          anchor: right ? Alignment.topRight : Alignment.topLeft);
    }
  }

  void _box(Canvas c, double x, double y, double w, double h, String v, String l) {
    c.drawRect(Rect.fromLTWH(x, y, w, h), _fill(const Color(0xFFE6E6E6)));
    c.drawRect(Rect.fromLTWH(x + 3, y + 3, w - 6, h - 6), _fill(const Color(0xFF1A1A1A)));
    drawText(c, v, Offset(x + w / 2, y + h * 0.42),
        size: h * 0.5, weight: FontWeight.w800, anchor: Alignment.center);
    drawText(c, l, Offset(x + w / 2, y + h - 12),
        size: 11,
        weight: FontWeight.w700,
        color: const Color(0xFFBBBBBB),
        anchor: Alignment.center);
  }

  void _draw(Canvas c) {
    final blink = _blink(f);
    final max = f.maxRpm.toDouble();
    c.drawRect(
        const Rect.fromLTWH(0, 0, kVW, kVH),
        Paint()
          ..shader = const LinearGradient(
            begin: Alignment.topCenter,
            end: Alignment.bottomCenter,
            colors: [Color(0xFF2A2A2A), Color(0xFF141414)],
          ).createShader(const Rect.fromLTWH(0, 0, kVW, kVH)));

    _cells(c, 16, false, [
      ('MANIFOLD PRESSURE', fmt(f.map), (f.map ?? 0) / 250, false),
      ('BOOST', fmt(f.boost, 1), ((f.boost ?? -1) + 1) / 3, false),
      ('THROTTLE POSITION', fmt(f.tps), (f.tps ?? 0) / 100, false),
      ('FUEL LEVEL', fmt(f.fuel), (f.fuel ?? 0) / 100, false),
    ], blink);
    _cells(c, kVW - 271, true, [
      ('COOLANT TEMP', fmt(f.ect), ((f.ect ?? 40) - 40) / 80, f.ectHot),
      ('INTAKE AIR TEMP', fmt(f.iat), (f.iat ?? 0) / 80, false),
      ('BATTERY', fmt(f.volt, 1), ((f.volt ?? 10) - 10) / 6, false),
      ('ENGINE LOAD', fmt(f.load), (f.load ?? 0) / 100, false),
    ], blink);

    const ctr = Offset(500, 232);
    const rad = 220.0;
    const a0 = math.pi / 2, sweep = math.pi * 1.5;
    double ang(double v) => a0 + sweep * clampD(v / max, 0, 1);
    final rect = Rect.fromCircle(center: ctr, radius: rad);
    void sector(double from, double to, Color col) {
      if (to <= from) return;
      c.drawPath(
          Path()
            ..moveTo(ctr.dx, ctr.dy)
            ..arcTo(rect, from, to - from, false)
            ..close(),
          _fill(col));
    }

    c.drawCircle(ctr, rad + 6, _fill(const Color(0xFF6A6A6A)));
    final red = ang(f.redline.toDouble());
    sector(a0, red, const Color(0xFFE8DC5A));
    sector(red, a0 + sweep, const Color(0xFFD41818));
    sector(0, math.pi / 2, const Color(0xFF5A5A5A));
    for (var v = 0; v <= max; v += 250) {
      final a = ang(v.toDouble());
      final maj = v % 1000 == 0;
      c.drawLine(_pol(ctr, a, rad * (maj ? 0.86 : 0.92)), _pol(ctr, a, rad),
          _stroke(const Color(0xFF222222), maj ? 3 : 1.5));
    }
    for (var k = 0; k * 1000 <= max; k++) {
      drawText(c, '$k', _pol(ctr, ang(k * 1000.0), rad * 0.75),
          size: 34,
          weight: FontWeight.w800,
          anchor: Alignment.center,
          color: k * 1000 >= f.redline ? Colors.white : const Color(0xFF222222));
    }
    c.drawCircle(ctr, rad * 0.6, _fill(const Color(0xFF0E0E0E)));
    c.drawCircle(ctr, rad * 0.56, _stroke(_yl, 4));
    drawText(c, 'OYT', Offset(ctr.dx, ctr.dy - 50),
        size: 40,
        weight: FontWeight.w900,
        italic: true,
        color: _yl,
        anchor: Alignment.center);

    _box(c, ctr.dx + 14, ctr.dy + 18, 58, 78, f.gear, 'GEAR');
    _box(c, ctr.dx + 78, ctr.dy + 18, 110, 78, fmt(f.speed), 'SPEED KPH');
    _box(c, ctr.dx + 14, ctr.dy + 102, 174, 58, fmt(f.rpm), 'RPM');

    final pa = ang(f.rpm);
    c.drawLine(
        ctr,
        _pol(ctr, pa, rad * 0.97),
        _stroke(f.atRedline && blink ? Colors.white : const Color(0xFFE01010), 6)
          ..strokeCap = StrokeCap.round);
    c.drawCircle(ctr, 12, _fill(const Color(0xFFE01010)));
  }

  @override
  bool shouldRepaint(covariant CenterTachPainter old) => true;
}
