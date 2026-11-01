import 'dart:math' as math;

import 'package:flutter/material.dart';

import 'live.dart';
import 'paint_utils.dart';

/// หน้า 2: เกจกลมมีเข็ม (วัดรอบใหญ่ซ้าย + เกจความร้อนเล็กขวา)
class RoundGaugePainter extends CustomPainter {
  RoundGaugePainter(this.f);
  final Frame f;

  static const _a0 = math.pi * 0.75; // 135°
  static const _sweep = math.pi * 1.5; // 270°

  Offset _dir(double a) => Offset(math.cos(a), math.sin(a));

  @override
  void paint(Canvas c, Size s) {
    final w = s.width, h = s.height;
    c.drawRect(Offset.zero & s, Paint()..color = Colors.black);
    final blink = (f.time * 8).floor().isEven;

    // ================= วัดรอบ =================
    final ctr = Offset(w * 0.34, h * 0.53);
    final R = math.min(w * 0.3, h * 0.45);
    final max = f.maxRpm.toDouble();

    c.drawCircle(
        ctr,
        R,
        Paint()
          ..shader = const RadialGradient(
                  colors: [Color(0xFF1E1E1E), Color(0xFF050505)])
              .createShader(Rect.fromCircle(center: ctr, radius: R)));
    c.drawCircle(
        ctr,
        R,
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = R * 0.03
          ..color = f.atRedline && blink ? kRed : const Color(0xFF3A3A3A));

    // โซนแดง
    final rz = _a0 + _sweep * clampD(f.redline / max, 0, 1);
    c.drawArc(
        Rect.fromCircle(center: ctr, radius: R * 0.885),
        rz,
        _a0 + _sweep - rz,
        false,
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = R * 0.07
          ..color = kRed.withAlpha(200));

    // ขีด + ตัวเลข
    for (var v = 0; v <= max; v += 250) {
      final a = _a0 + _sweep * v / max;
      final major = v % 1000 == 0;
      final p = Paint()
        ..color = v >= f.redline ? kRed : Colors.white
        ..strokeWidth = major ? R * 0.022 : R * 0.01;
      c.drawLine(ctr + _dir(a) * (R * 0.93), ctr + _dir(a) * (R * (major ? 0.78 : 0.86)), p);
      if (major) {
        drawText(c, '${v ~/ 1000}', ctr + _dir(a) * (R * 0.64),
            size: R * 0.13,
            color: v >= f.redline ? kRed : Colors.white,
            anchor: Alignment.center);
      }
    }

    // ข้อความกลางเกจ
    drawText(c, f.gear, ctr + Offset(0, -R * 0.36),
        size: R * 0.2,
        weight: FontWeight.w900,
        color: kAccent,
        anchor: Alignment.center);
    drawText(c, 'x1000 r/min', ctr + Offset(0, -R * 0.2),
        size: R * 0.065, color: kGrey, anchor: Alignment.center);
    drawText(c, fmt(f.speed), ctr + Offset(0, R * 0.42),
        size: R * 0.26, weight: FontWeight.w800, anchor: Alignment.center);
    drawText(c, 'km/h', ctr + Offset(0, R * 0.6),
        size: R * 0.07, color: kGrey, anchor: Alignment.center);
    drawText(c, '${fmt(f.rpm)} rpm', ctr + Offset(0, R * 0.78),
        size: R * 0.075, color: kGrey, anchor: Alignment.center);

    // เข็ม
    _needle(c, ctr, R * 0.9, _a0 + _sweep * clampD(f.rpm / max, 0, 1),
        R * 0.035, R * 0.16, kAccent);
    c.drawCircle(ctr, R * 0.08, Paint()..color = const Color(0xFF222222));
    c.drawCircle(
        ctr,
        R * 0.08,
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = R * 0.015
          ..color = kAccent);

    // ================= เกจความร้อน (ครึ่งวง) =================
    final ec = Offset(w * 0.8, h * 0.36);
    final er = math.min(w * 0.15, h * 0.26);
    const lo = 40.0, hi = 130.0;
    double ang(double t) => math.pi + math.pi * clampD((t - lo) / (hi - lo), 0, 1);
    c.drawArc(
        Rect.fromCircle(center: ec, radius: er),
        math.pi,
        math.pi,
        false,
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = er * 0.1
          ..color = const Color(0xFF262626));
    final wa = ang(f.ectWarn.toDouble());
    c.drawArc(
        Rect.fromCircle(center: ec, radius: er),
        wa,
        2 * math.pi - wa,
        false,
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = er * 0.1
          ..color = kRed);
    for (var t = 40; t <= 130; t += 10) {
      final a = ang(t.toDouble());
      c.drawLine(ec + _dir(a) * (er * 0.8), ec + _dir(a) * (er * 0.92),
          Paint()
            ..color = Colors.white70
            ..strokeWidth = 1.5);
    }
    drawText(c, 'C', ec + Offset(-er * 0.95, er * 0.12),
        size: er * 0.16, color: kBlue, anchor: Alignment.topCenter);
    drawText(c, 'H', ec + Offset(er * 0.95, er * 0.12),
        size: er * 0.16, color: kRed, anchor: Alignment.topCenter);
    if (f.ect != null) {
      _needle(c, ec, er * 0.85, ang(f.ect!), er * 0.05, er * 0.12,
          f.ectHot ? kRed : Colors.white);
    }
    c.drawCircle(ec, er * 0.08, Paint()..color = const Color(0xFF333333));
    drawText(c, '${fmt(f.ect)}°C', ec + Offset(0, er * 0.3),
        size: er * 0.24,
        weight: FontWeight.w800,
        color: f.ectHot && blink ? kRed : Colors.white,
        anchor: Alignment.topCenter);
    drawText(c, 'COOLANT', ec + Offset(0, er * 0.6),
        size: er * 0.11, color: kGrey, anchor: Alignment.topCenter);

    // ================= ค่าอื่น ๆ =================
    final rows = <(String, String)>[
      ('IAT', '${fmt(f.iat)} °C'),
      ('BATTERY', '${fmt(f.volt, 1)} V'),
      ('LOAD', '${fmt(f.load)} %'),
      ('TPS', '${fmt(f.tps)} %'),
    ];
    final x0 = w * 0.66, x1 = w * 0.95;
    var y = h * 0.62;
    final rh = h * 0.085;
    for (final r in rows) {
      c.drawLine(Offset(x0, y), Offset(x1, y),
          Paint()..color = const Color(0xFF2A2A2A));
      drawText(c, r.$1, Offset(x0, y + rh * 0.5),
          size: rh * 0.38, color: kGrey, anchor: Alignment.centerLeft);
      drawText(c, r.$2, Offset(x1, y + rh * 0.5),
          size: rh * 0.5, anchor: Alignment.centerRight);
      y += rh;
    }
  }

  void _needle(Canvas c, Offset ctr, double len, double a, double baseW,
      double tail, Color col) {
    final d = _dir(a);
    final nrm = Offset(-d.dy, d.dx);
    final path = Path()
      ..moveTo((ctr + d * len).dx, (ctr + d * len).dy)
      ..lineTo((ctr + nrm * baseW - d * tail).dx, (ctr + nrm * baseW - d * tail).dy)
      ..lineTo((ctr - nrm * baseW - d * tail).dx, (ctr - nrm * baseW - d * tail).dy)
      ..close();
    c.drawPath(
        path,
        Paint()
          ..color = col.withAlpha(120)
          ..maskFilter = const MaskFilter.blur(BlurStyle.normal, 6));
    c.drawPath(path, Paint()..color = col);
  }

  @override
  bool shouldRepaint(covariant RoundGaugePainter old) => true;
}
