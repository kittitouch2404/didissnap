import 'dart:math' as math;

import 'package:flutter/material.dart';

import 'live.dart';
import 'paint_utils.dart';

// หน้าดีไซน์จอ dash 4 แบบ (ตามภาพตัวอย่าง) วาดบนพื้นที่สมมติ 1000x460
// แล้วย่อ/ขยายให้พอดีจอ (drawStage)

const _lbl = Color(0xFF9DB7CF);
const _blue = Color(0xFF1E90FF);

Paint _fill(Color c) => Paint()..color = c;
Paint _stroke(Color c, double w) => Paint()
  ..color = c
  ..style = PaintingStyle.stroke
  ..strokeWidth = w;
Paint _glow(Color c, double w, [double blur = 6]) => Paint()
  ..color = c.withAlpha(150)
  ..style = PaintingStyle.stroke
  ..strokeWidth = w
  ..maskFilter = MaskFilter.blur(BlurStyle.normal, blur);
Offset _pol(Offset c, double a, double r) =>
    c + Offset(math.cos(a), math.sin(a)) * r;
void _rr(Canvas c, Rect r, double rad, Paint p) =>
    c.drawRRect(RRect.fromRectAndRadius(r, Radius.circular(rad)), p);
bool _blink(Frame f) => (f.time * 8).floor().isEven;
Color _hotCol(Frame f) =>
    f.ectHot ? (_blink(f) ? kRed : const Color(0xFFFF8A3A)) : Colors.white;

// ======================================================================
//  Sweep: แถบรอบโค้งจากซ้ายล่างขึ้นไปด้านบน + ตาราง 6 ช่อง
// ======================================================================
class SweepPainter extends CustomPainter {
  SweepPainter(this.f);
  final Frame f;

  @override
  void paint(Canvas canvas, Size size) => drawStage(canvas, size, _draw);

  void _draw(Canvas c) {
    final blink = _blink(f);
    const cx = 340.0, cy = 410.0, r1 = 225.0, r2 = 320.0, xEnd = 990.0;
    const ctr = Offset(cx, cy);
    const la = math.pi / 2 * (r1 + r2) / 2;
    const ls = xEnd - cx;
    const total = la + ls;
    final max = f.maxRpm.toDouble();
    final outer = Rect.fromCircle(center: ctr, radius: r2);
    final inner = Rect.fromCircle(center: ctr, radius: r1);

    void piece(double s0, double s1, Paint p) {
      if (s1 <= la) {
        final a0 = math.pi + s0 / la * math.pi / 2;
        final a1 = math.pi + s1 / la * math.pi / 2;
        final path = Path()
          ..arcTo(outer, a0, a1 - a0, true)
          ..arcTo(inner, a1, a0 - a1, false)
          ..close();
        c.drawPath(path, p);
      } else {
        c.drawRect(
            Rect.fromLTRB(cx + (s0 - la), cy - r2, cx + (s1 - la), cy - r1), p);
      }
    }

    final n = (max / 200).round();
    for (var i = 0; i < n; i++) {
      final v = i * max / n;
      final s0 = total * i / n;
      final s1 = total * (i + 1) / n - 4;
      Color on, off;
      if (v >= f.redline) {
        on = const Color(0xFFF03A3A);
        off = const Color(0xFF3A1012);
      } else if (v >= f.shiftStart) {
        on = const Color(0xFFE2E23A);
        off = const Color(0xFF34340E);
      } else {
        on = const Color(0xFFEDEDED);
        off = const Color(0xFF333333);
      }
      final lit = v < f.rpm;
      final p = _fill(lit
          ? ((f.atRedline && blink && v >= f.redline) ? Colors.white : on)
          : off);
      if (s0 < la && s1 > la) {
        piece(s0, la, p);
        piece(la, s1, p);
      } else {
        piece(s0, s1, p);
      }
    }

    // เส้นฟ้าเรืองแสง
    final blue = Path()
      ..arcTo(Rect.fromCircle(center: ctr, radius: r1 - 10), math.pi,
          math.pi / 2, true)
      ..lineTo(xEnd + 10, cy - r1 + 10);
    c.drawPath(blue, _glow(_blue, 8, 8));
    c.drawPath(blue, _stroke(_blue, 4));

    for (var k = 0; k * 1000 <= max; k++) {
      final s = total * k * 1000 / max;
      Offset p;
      if (s <= la) {
        p = _pol(ctr, math.pi + s / la * math.pi / 2, r1 - 34);
      } else {
        p = Offset(cx + (s - la), cy - r1 + 24);
      }
      drawText(c, '$k', p,
          size: 30,
          italic: true,
          weight: FontWeight.w900,
          anchor: Alignment.center,
          color: k * 1000 >= f.redline ? kRed : Colors.white);
    }
    drawText(c, 'x1000 RPM', const Offset(12, 440),
        size: 14, italic: true, color: kGrey, anchor: Alignment.centerLeft);

    drawText(c, fmt(f.speed), const Offset(355, 300),
        size: 112, weight: FontWeight.w900, anchor: Alignment.center);
    drawText(c, 'KM/H', const Offset(355, 362),
        size: 22, weight: FontWeight.w800, anchor: Alignment.center);
    const gb = Color(0xFF4FB3FF);
    drawText(c, f.gear, const Offset(355, 408),
        size: 46, weight: FontWeight.w900, color: gb, anchor: Alignment.center);
    drawText(c, 'GEAR', const Offset(355, 442),
        size: 13, color: gb, anchor: Alignment.center);

    final cells = <(String, String, String, Color)>[
      ('WATER', '°C', fmt(f.ect), _hotCol(f)),
      ('RPM', '', fmt(f.rpm), Colors.white),
      ('BATT', 'V', fmt(f.volt, 1), Colors.white),
      ('TPS', '%', fmt(f.tps), Colors.white),
      ('BOOST', 'bar', fmt(f.boost, 1), Colors.white),
      ('TRIP', 'km', fmt(f.trip, 1), Colors.white),
    ];
    const gx = [560.0, 785.0];
    const cw = 205.0, gy0 = 240.0, rh = 73.0;
    for (var i = 0; i < cells.length; i++) {
      final x = gx[i % 2];
      final y = gy0 + (i ~/ 2) * rh;
      final cell = cells[i];
      drawText(c, cell.$1, Offset(x, y + 6),
          size: 15, color: _lbl, weight: FontWeight.w600);
      drawText(c, cell.$2, Offset(x + cw, y + 6),
          size: 15,
          color: _lbl,
          weight: FontWeight.w600,
          anchor: Alignment.topRight);
      drawText(c, cell.$3, Offset(x + cw, y + rh - 8),
          size: 46,
          weight: FontWeight.w800,
          color: cell.$4,
          anchor: Alignment.bottomRight);
      if (i % 2 == 1) {
        final p0 = Offset(540, y + rh - 1), p1 = Offset(kVW, y + rh - 1);
        c.drawLine(p0, p1, _glow(_blue, 4));
        c.drawLine(p0, p1, _stroke(_blue, 2));
      }
    }
  }

  @override
  bool shouldRepaint(covariant SweepPainter old) => true;
}

// ======================================================================
//  Arc: วัดรอบวงกลมซ้าย (แถบเหลือง) + บูสต์ + ความเร็วกล่องใหญ่ + แบต/น้ำ
// ======================================================================
class ArcPainter extends CustomPainter {
  ArcPainter(this.f);
  final Frame f;

  @override
  void paint(Canvas canvas, Size size) => drawStage(canvas, size, _draw);

  void _draw(Canvas c) {
    final blink = _blink(f);
    const ctr = Offset(255, 250);
    const rad = 225.0;
    const a0 = 75 * math.pi / 180, sw = 235 * math.pi / 180;
    final max = f.maxRpm.toDouble();
    final red = f.redline.toDouble();
    double ang(double v) => a0 + sw * clampD(v / max, 0, 1);
    void arcS(double r, double from, double to, Color col, double w) {
      if (to <= from) return;
      c.drawArc(Rect.fromCircle(center: ctr, radius: r), from, to - from,
          false, _stroke(col, w));
    }

    c.save();
    c.clipRect(const Rect.fromLTWH(0, 0, kVW, kVH));
    arcS(rad * 0.97, a0, a0 + sw, const Color(0xFF8E8E8E), 10);
    arcS(rad * 0.97, ang(red), a0 + sw, const Color(0xFFE02828), 13);
    for (var v = 0; v <= max; v += 500) {
      final a = ang(v.toDouble());
      final maj = v % 1000 == 0;
      c.drawLine(_pol(ctr, a, rad * 0.92), _pol(ctr, a, rad * (maj ? 1.01 : 0.98)),
          _stroke(Colors.white, maj ? 3 : 1.5));
    }
    void band(double v0, double v1, Color col) =>
        arcS(rad * 0.75, ang(v0), ang(v1), col, rad * 0.27);
    band(0, red, const Color(0xFF183A57));
    band(red, max, const Color(0xFF3E0E14));
    final r = math.min(f.rpm, max);
    band(0, math.min(r, red), const Color(0xFFF6D21E));
    if (r > red) {
      band(red, r, blink ? const Color(0xFFFF3030) : const Color(0xFFFF7070));
    }
    final pa = ang(r);
    c.drawLine(_pol(ctr, pa, rad * 0.6), _pol(ctr, pa, rad * 0.9), _glow(kRed, 10));
    c.drawLine(_pol(ctr, pa, rad * 0.6), _pol(ctr, pa, rad * 0.9), _stroke(kRed, 7));
    for (var k = 0; k * 1000 <= max; k++) {
      drawText(c, '$k', _pol(ctr, ang(k * 1000.0), rad * 0.47),
          size: 36,
          italic: true,
          weight: FontWeight.w900,
          anchor: Alignment.center,
          color: k * 1000 >= f.redline ? kRed : Colors.white);
    }
    c.restore();

    drawText(c, f.gear, ctr + const Offset(30, -8),
        size: 104, weight: FontWeight.w900, anchor: Alignment.center);
    drawText(c, 'GEAR', ctr + const Offset(30, 52),
        size: 16, color: _lbl, anchor: Alignment.center);
    drawText(c, 'x1000 RPM', const Offset(12, 440),
        size: 14, color: kGrey, anchor: Alignment.centerLeft);

    // ---- ขวา ----
    drawText(c, 'BOOST', const Offset(600, 22),
        size: 20, color: _lbl, weight: FontWeight.w600);
    drawText(c, 'bar', const Offset(690, 22),
        size: 20, color: _lbl, weight: FontWeight.w400);
    drawText(c, fmt(f.boost, 1), const Offset(690, 86),
        size: 58, weight: FontWeight.w800, anchor: Alignment.center);

    const box = Rect.fromLTWH(520, 128, 470, 205);
    _rr(c, box, 14, _fill(const Color(0xFF0A1520)));
    _rr(c, box, 14, _stroke(const Color(0xFF2E6DA4), 2.5));
    drawText(c, fmt(f.speed), const Offset(905, 322),
        size: 180, weight: FontWeight.w900, anchor: Alignment.bottomRight);
    drawText(c, 'KM/H', const Offset(978, 322),
        size: 22, weight: FontWeight.w800, anchor: Alignment.bottomRight);

    c.drawLine(const Offset(520, 347), const Offset(990, 347),
        _stroke(const Color(0xFF2E6DA4), 2));
    drawText(c, 'BATT  V', const Offset(530, 357),
        size: 18, color: _lbl, weight: FontWeight.w600);
    drawText(c, fmt(f.volt, 1), const Offset(540, 456),
        size: 70,
        weight: FontWeight.w800,
        anchor: Alignment.bottomLeft,
        color: (f.volt ?? 13) < 12 ? kAmber : Colors.white);
    drawText(c, 'WATER', const Offset(770, 357),
        size: 18, color: _lbl, weight: FontWeight.w600);
    drawText(c, fmt(f.ect), const Offset(985, 357),
        size: 20,
        anchor: Alignment.topRight,
        color: f.ectHot ? kRed : Colors.white);

    const bx = 770.0, bw = 215.0;
    const marks = [0, 60, 120];
    for (var i = 0; i < marks.length; i++) {
      drawText(c, '${marks[i]}', Offset(bx + bw * marks[i] / 120, 387),
          size: 14,
          anchor: Alignment.bottomCenter,
          color: i == 2 ? kRed : const Color(0xFFDDDDDD));
    }
    for (var v = 0; v <= 120; v += 10) {
      final x = bx + bw * v / 120;
      c.drawLine(Offset(x, 392), Offset(x, v % 60 == 0 ? 404 : 399),
          _stroke(const Color(0xFFDDDDDD), 1));
    }
    c.drawRect(const Rect.fromLTWH(bx, 410, bw, 40),
        _fill(const Color(0xFF2A2A2A)));
    c.drawRect(
        Rect.fromLTWH(bx, 410, bw * clampD((f.ect ?? 0) / 120, 0, 1), 40),
        _fill(f.ectHot
            ? (blink ? kRed : const Color(0xFFFF8A3A))
            : const Color(0xFF2E8CE6)));
  }

  @override
  bool shouldRepaint(covariant ArcPainter old) => true;
}

// ======================================================================
//  Yellow: ช่องสีเหลือง + เกียร์กล่องฟ้ากลางจอ
// ======================================================================
class YellowPainter extends CustomPainter {
  YellowPainter(this.f);
  final Frame f;

  @override
  void paint(Canvas canvas, Size size) => drawStage(canvas, size, _draw);

  void _cell(Canvas c, Rect r, String label, String value, {bool warn = false}) {
    final blink = _blink(f);
    _rr(c, r, 7,
        _fill(warn && blink ? const Color(0xFFFF4A3A) : const Color(0xFFF6C21C)));
    _rr(c, r, 7, _stroke(Colors.black, 2));
    drawText(c, label, r.topLeft + const Offset(9, 7),
        size: 16, color: const Color(0xFF111111), weight: FontWeight.w600);
    drawText(c, value, r.bottomRight - const Offset(10, 8),
        size: math.min(r.height * 0.56, 62),
        weight: FontWeight.w900,
        color: Colors.black,
        anchor: Alignment.bottomRight);
  }

  void _draw(Canvas c) {
    final blink = _blink(f);
    final max = f.maxRpm.toDouble();
    const bx = 95.0, bw = 680.0, by = 34.0, bh = 32.0;
    for (var k = 1; k * 1000 <= max; k++) {
      drawText(c, '$k', Offset(bx + bw * k * 1000 / max, 6),
          size: 24,
          anchor: Alignment.topCenter,
          color: k * 1000 >= f.redline ? kRed : Colors.white);
    }
    final rl = bx + bw * clampD(f.redline / max, 0, 1);
    c.drawRect(Rect.fromLTRB(bx, by, rl, by + bh), _fill(const Color(0xFF1C2732)));
    c.drawRect(Rect.fromLTRB(rl, by, bx + bw, by + bh), _fill(const Color(0xFF3A1015)));
    c.drawRect(Rect.fromLTWH(bx, by, bw * clampD(f.rpm / max, 0, 1), bh),
        _fill(f.atRedline && blink ? kRed : kBlue));
    c.drawRect(const Rect.fromLTWH(bx, by, bw, bh),
        _stroke(const Color(0xFF666666), 1.5));

    const sp = Rect.fromLTWH(330, 78, 310, 64);
    _rr(c, sp, 7, _fill(const Color(0xFFF6C21C)));
    _rr(c, sp, 7, _stroke(Colors.black, 2));
    drawText(c, fmt(f.speed), sp.center,
        size: 58,
        weight: FontWeight.w900,
        italic: true,
        color: Colors.black,
        anchor: Alignment.center);
    drawText(c, 'Km/h', const Offset(652, 110),
        size: 24, weight: FontWeight.w500, anchor: Alignment.centerLeft);
    drawText(c, 'GEAR', const Offset(485, 149),
        size: 15,
        anchor: Alignment.topCenter,
        color: const Color(0xFFDDDDDD));

    const gbox = Rect.fromLTWH(395, 168, 180, 287);
    _rr(c, gbox, 10, _fill(kBlue));
    _rr(c, gbox, 10, _stroke(const Color(0xFFCFE3FF), 2));
    drawText(c, f.gear, gbox.center,
        size: 230, weight: FontWeight.w900, anchor: Alignment.center);

    const r1 = 168.0, h1 = 103.0, r2 = 281.0, r3 = 394.0, h3 = 61.0;
    _cell(c, const Rect.fromLTWH(20, r1, 180, h1), 'WATER T', fmt(f.ect),
        warn: f.ectHot);
    _cell(c, const Rect.fromLTWH(205, r1, 180, h1), 'AIR T', fmt(f.iat));
    _cell(c, const Rect.fromLTWH(20, r2, 180, h1), 'ENG LOAD', fmt(f.load));
    _cell(c, const Rect.fromLTWH(205, r2, 180, h1), 'AIRFLOW', fmt(f.maf, 1));
    _cell(c, const Rect.fromLTWH(20, r3, 365, h3), 'FUEL %', fmt(f.fuel));
    _cell(c, const Rect.fromLTWH(585, r1, 395, h1), 'r/min', fmt(f.rpm),
        warn: f.atRedline);
    _cell(c, const Rect.fromLTWH(585, r2, 195, h1), 'BATT V', fmt(f.volt, 1));
    _cell(c, const Rect.fromLTWH(785, r2, 195, h1), 'BOOST', fmt(f.boost, 1));
    _cell(c, const Rect.fromLTWH(585, r3, 395, h3), 'TRIP km', fmt(f.trip, 1));
  }

  @override
  bool shouldRepaint(covariant YellowPainter old) => true;
}

// ======================================================================
//  Bars: กราฟแท่งรอบทรงลาด + 8 ช่องมีช่วงค่า + เกียร์กลาง
// ======================================================================
class BarsPainter extends CustomPainter {
  BarsPainter(this.f);
  final Frame f;

  @override
  void paint(Canvas canvas, Size size) => drawStage(canvas, size, _draw);

  void _cell(Canvas c, double x, double y, String min, String max,
      String value, String name, bool orange) {
    const w = 180.0;
    const border = Color(0xFF4A86D0);
    drawText(c, min, Offset(x + 2, y),
        size: 14, color: const Color(0xFFDDDDDD), weight: FontWeight.w500);
    drawText(c, max, Offset(x + w - 2, y),
        size: 14,
        color: const Color(0xFFDDDDDD),
        weight: FontWeight.w500,
        anchor: Alignment.topRight);
    final vb = Rect.fromLTWH(x, y + 18, w, 58);
    c.drawRect(vb, _fill(orange ? const Color(0xFFF5A623) : Colors.black));
    c.drawRect(vb, _stroke(border, 2.5));
    drawText(c, value, Offset(x + w - 10, y + 70),
        size: 44,
        weight: FontWeight.w900,
        anchor: Alignment.bottomRight,
        color: orange ? Colors.black : Colors.white);
    final nb = Rect.fromLTWH(x, y + 82, w, 30);
    c.drawRect(nb, _fill(const Color(0xFF0E2A4A)));
    c.drawRect(nb, _stroke(border, 2.5));
    drawText(c, name, nb.center,
        size: 16, weight: FontWeight.w600, anchor: Alignment.center);
  }

  void _draw(Canvas c) {
    final blink = _blink(f);
    final max = f.maxRpm.toDouble();
    drawText(c, fmt(f.speed), const Offset(20, 38),
        size: 84, weight: FontWeight.w900, italic: true);
    drawText(c, 'Km/h', const Offset(24, 124),
        size: 18, weight: FontWeight.w500);

    const x0 = 240.0, x1 = 905.0, base = 150.0, top = 48.0;
    const n = 50;
    double prof(double u) => u < 0.3 ? 0.22 + 0.78 * u / 0.3 : 1.0;
    final barW = (x1 - x0) / n * 0.62;
    for (var i = 0; i < n; i++) {
      final u = (i + 0.5) / n;
      final x = x0 + (x1 - x0) * i / n;
      final hh = (base - top) * prof(u);
      final lit = u * max < f.rpm;
      c.drawRect(
          Rect.fromLTWH(x, base - hh, barW, hh),
          _fill(lit
              ? (f.atRedline && blink ? kRed : const Color(0xFFF5A623))
              : const Color(0xFFD9D9D9)));
    }
    c.drawLine(const Offset(x0 - 10, base + 2), const Offset(x1, base + 2),
        _stroke(Colors.white, 2));
    for (var k = 1; k * 1000 <= max; k++) {
      final u = k * 1000 / max;
      drawText(c, '$k',
          Offset(x0 + (x1 - x0) * u, base - (base - top) * prof(u) - 6),
          size: 22,
          weight: FontWeight.w500,
          anchor: Alignment.bottomCenter,
          color: k * 1000 >= f.redline ? kRed : Colors.white);
    }
    drawText(c, 'RPM', const Offset(x1, base + 8),
        size: 15, color: kRed, anchor: Alignment.topRight);
    drawText(c, fmt(f.rpm), const Offset(572, 194),
        size: 60,
        weight: FontWeight.w900,
        italic: true,
        anchor: Alignment.center,
        color: f.atRedline && blink ? kRed : Colors.white);

    const gbox = Rect.fromLTWH(404, 232, 192, 224);
    _rr(c, gbox, 12, _fill(const Color(0xFF5DA9E9)));
    _rr(c, gbox, 12, _stroke(Colors.white, 2));
    drawText(c, f.gear, gbox.center,
        size: 190, weight: FontWeight.w900, anchor: Alignment.center);

    _cell(c, 20, 230, '40', '120', fmt(f.ect), 'WATER', f.ectHot && blink);
    _cell(c, 212, 230, '10', '60', fmt(f.iat), 'AIR.T', true);
    _cell(c, 608, 230, '0.0', '2.5', fmt(f.boost, 1), 'BOOST.P', true);
    _cell(c, 800, 230, '0', '655', fmt(f.maf), 'AIR.FLOW', true);
    _cell(c, 20, 346, '0', '100', fmt(f.load, 1), 'CALC.LOAD', false);
    _cell(c, 212, 346, '0', '100', fmt(f.tps, 1), 'THRO.P', true);
    _cell(c, 608, 346, '0', '100', fmt(f.fuel), 'FUEL.L', false);
    _cell(c, 800, 346, '10.0', '16.0', fmt(f.volt, 1), 'BAT', true);
  }

  @override
  bool shouldRepaint(covariant BarsPainter old) => true;
}
