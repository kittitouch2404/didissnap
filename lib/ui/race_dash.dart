import 'dart:math' as math;

import 'package:flutter/material.dart';

import 'live.dart';
import 'paint_utils.dart';

/// หน้า 1: จอดิจิทัลแบบรถแข่ง (shift light + แถบรอบทรงลาด + เกียร์ตัวใหญ่)
class RaceDashPainter extends CustomPainter {
  RaceDashPainter(this.f);
  final Frame f;

  @override
  void paint(Canvas c, Size s) {
    final w = s.width, h = s.height;
    c.drawRect(Offset.zero & s, Paint()..color = Colors.black);
    final blink = (f.time * 8).floor().isEven;
    final redHit = f.atRedline;

    // ---------- shift lights ----------
    const n = 12;
    final lx0 = w * 0.2, lw = w * 0.6 / n, gap = lw * 0.2;
    final ly = h * 0.03, lh = h * 0.07;
    final span = math.max(1, f.redline - f.shiftStart);
    final frac = clampD((f.rpm - f.shiftStart) / span, 0, 1);
    final lit = (frac * n).ceil();
    for (var i = 0; i < n; i++) {
      var col = i < 4 ? kGreen : (i < 8 ? kAmber : kRed);
      var on = i < lit;
      if (redHit) {
        on = blink;
        col = kBlue;
      }
      final r = Rect.fromLTWH(lx0 + i * lw + gap / 2, ly, lw - gap, lh);
      if (on) {
        c.drawRect(
            r.inflate(3),
            Paint()
              ..color = col.withAlpha(110)
              ..maskFilter = const MaskFilter.blur(BlurStyle.normal, 7));
      }
      c.drawRect(r, Paint()..color = on ? col : col.withAlpha(28));
    }

    // ---------- แถบรอบทรงลาด ----------
    final bx = w * 0.03, bw = w * 0.94, by = h * 0.14, bh = h * 0.17;
    Path wedge(double t) => Path()
      ..moveTo(bx, by + bh)
      ..lineTo(bx, by + bh - bh * 0.3)
      ..lineTo(bx + bw * t, by + bh - bh * (0.3 + 0.7 * t))
      ..lineTo(bx + bw * t, by + bh)
      ..close();

    c.drawPath(wedge(1), Paint()..color = const Color(0xFF181818));
    final max = f.maxRpm.toDouble();
    final t = clampD(f.rpm / max, 0, 1);
    final ss = clampD(f.shiftStart / max, 0, 1);
    final rl = clampD(f.redline / max, ss, 1);
    final barPaint = Paint();
    if (redHit && blink) {
      barPaint.color = kRed;
    } else {
      barPaint.shader = LinearGradient(
        colors: const [Color(0xFFE8E8E8), Color(0xFFE8E8E8), kAccent, kRed],
        stops: [0, ss, rl, math.min(1.0, rl + 0.02)],
      ).createShader(Rect.fromLTWH(bx, by, bw, bh));
    }
    c.drawPath(wedge(t), barPaint);

    // แบ่งช่องทุก 250 rpm
    final sep = Paint()
      ..color = Colors.black
      ..strokeWidth = math.max(1.5, w * 0.003);
    for (var v = 250; v < max; v += 250) {
      final x = bx + bw * v / max;
      c.drawLine(Offset(x, by), Offset(x, by + bh), sep);
    }
    // ตัวเลข x1000 + เส้น redline
    for (var k = 1; k * 1000 <= max; k++) {
      final x = bx + bw * k * 1000 / max;
      drawText(c, '$k', Offset(x, by + bh + h * 0.012),
          size: h * 0.045,
          color: k * 1000 >= f.redline ? kRed : kGrey,
          anchor: Alignment.topCenter);
    }
    final rx = bx + bw * rl;
    c.drawPath(
        Path()
          ..moveTo(rx, by - 2)
          ..lineTo(rx - h * 0.02, by - h * 0.035)
          ..lineTo(rx + h * 0.02, by - h * 0.035)
          ..close(),
        Paint()..color = kRed);

    // ---------- เกียร์กลางจอ ----------
    final gearBox = Rect.fromCenter(
        center: Offset(w / 2, h * 0.615), width: h * 0.42, height: h * 0.46);
    c.drawRRect(
        RRect.fromRectAndRadius(gearBox, Radius.circular(h * 0.03)),
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = 2
          ..color = redHit && blink ? kRed : kAccent.withAlpha(160));
    drawText(c, f.gear, gearBox.center,
        size: h * 0.4,
        weight: FontWeight.w900,
        color: redHit && blink ? kRed : Colors.white,
        anchor: Alignment.center);
    drawText(c, 'GEAR', Offset(w / 2, gearBox.bottom + h * 0.012),
        size: h * 0.035, color: kGrey, anchor: Alignment.topCenter);

    // ---------- คอลัมน์ซ้าย/ขวา ----------
    final colTop = h * 0.39, rowH = h * 0.17;
    final hotCol = f.ectHot ? (blink ? kRed : const Color(0xFFFF8A3A)) : Colors.white;
    final voltCol = (f.volt != null && f.volt! < 12.0) ? kAmber : Colors.white;
    _cell(c, Rect.fromLTWH(w * 0.03, colTop, w * 0.3, rowH), 'RPM',
        fmt(f.rpm), Colors.white);
    _cell(c, Rect.fromLTWH(w * 0.03, colTop + rowH, w * 0.3, rowH),
        'COOLANT °C', fmt(f.ect), hotCol);
    _cell(c, Rect.fromLTWH(w * 0.03, colTop + rowH * 2, w * 0.3, rowH),
        'IAT °C', fmt(f.iat), Colors.white);
    _cell(c, Rect.fromLTWH(w * 0.67, colTop, w * 0.3, rowH), 'KM/H',
        fmt(f.speed), Colors.white);
    _cell(c, Rect.fromLTWH(w * 0.67, colTop + rowH, w * 0.3, rowH),
        'BATTERY V', fmt(f.volt, 1), voltCol);
    _cell(c, Rect.fromLTWH(w * 0.67, colTop + rowH * 2, w * 0.3, rowH),
        'LOAD %', fmt(f.load), Colors.white);

    // ---------- แถวล่าง ----------
    final by2 = h * 0.925;
    drawText(c, 'TPS ${fmt(f.tps)}%     MAP ${fmt(f.map)} kPa     MAF ${fmt(f.maf, 1)} g/s',
        Offset(w / 2, by2),
        size: h * 0.04, color: kGrey, anchor: Alignment.topCenter);
  }

  void _cell(Canvas c, Rect r, String label, String value, Color valueColor) {
    c.drawLine(r.topLeft, r.topRight,
        Paint()
          ..color = const Color(0xFF2A2A2A)
          ..strokeWidth = 1);
    drawText(c, label, r.topLeft + Offset(0, r.height * 0.1),
        size: r.height * 0.2, color: kGrey, weight: FontWeight.w600);
    drawText(c, value, r.bottomRight - Offset(0, r.height * 0.06),
        size: r.height * 0.58,
        color: valueColor,
        weight: FontWeight.w800,
        anchor: Alignment.bottomRight);
  }

  @override
  bool shouldRepaint(covariant RaceDashPainter old) => true;
}
