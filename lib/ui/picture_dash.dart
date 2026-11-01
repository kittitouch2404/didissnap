import 'dart:io';

import 'package:flutter/material.dart';

import 'live.dart';
import 'paint_utils.dart';
import 'pixel_font.dart';

/// หน้า 3: รูปพื้นหลังของตัวเอง (รองรับ GIF)
/// 1 รูป = ภาพนิ่ง, หลายรูป = เปลี่ยนรูปตามช่วงรอบ (แบ่งเท่า ๆ กันถึง redline)
class PictureDash extends StatelessWidget {
  const PictureDash({super.key, required this.f});
  final Frame f;

  @override
  Widget build(BuildContext context) {
    final path = f.s.pictureFor(f.rpm);
    Widget bg;
    if (path == null) {
      bg = const DecoratedBox(
        decoration: BoxDecoration(
          gradient: LinearGradient(
            begin: Alignment.topLeft,
            end: Alignment.bottomRight,
            colors: [Color(0xFF2A1240), Color(0xFF06030C)],
          ),
        ),
      );
    } else {
      bg = Image.file(
        File(path),
        fit: BoxFit.cover,
        gaplessPlayback: true,
        errorBuilder: (_, __, ___) => const ColoredBox(color: Colors.black),
      );
    }
    return Stack(
      fit: StackFit.expand,
      children: [
        bg,
        CustomPaint(painter: _OverlayPainter(f, hasPics: path != null)),
      ],
    );
  }
}

class _OverlayPainter extends CustomPainter {
  _OverlayPainter(this.f, {required this.hasPics});
  final Frame f;
  final bool hasPics;

  static double _px(double size) {
    final v = (size / 7).roundToDouble();
    return v < 1 ? 1 : v;
  }

  void _val(Canvas c, String label, String value, Offset at, double size,
      Color col) {
    if (f.s.pixelFont) {
      final lp = _px(size * 0.3);
      drawPixelText(c, label, at,
          px: lp, color: const Color(0xFFFFE08A));
      drawPixelText(c, value, at + Offset(0, lp * 7 + lp * 3),
          px: _px(size * 0.875), color: col);
      return;
    }
    drawText(c, label, at,
        size: size * 0.32,
        color: Colors.white70,
        outline: Colors.black,
        outlineWidth: 3);
    drawText(c, value, at + Offset(0, size * 0.36),
        size: size,
        weight: FontWeight.w900,
        color: col,
        outline: Colors.black,
        outlineWidth: size * 0.12);
  }

  @override
  void paint(Canvas c, Size s) {
    final w = s.width, h = s.height;
    final blink = (f.time * 8).floor().isEven;
    final x = w * 0.04;

    _val(c, 'RPM', fmt(f.rpm), Offset(x, h * 0.11), h * 0.15,
        f.atRedline && blink ? kRed : Colors.white);
    _val(c, 'KM/H', fmt(f.speed), Offset(x, h * 0.34), h * 0.12, Colors.white);
    _val(c, 'COOLANT °C', fmt(f.ect), Offset(x, h * 0.54), h * 0.09,
        f.ectHot ? (blink ? kRed : const Color(0xFFFF8A3A)) : Colors.white);
    _val(c, 'IAT °C', fmt(f.iat), Offset(x, h * 0.70), h * 0.09, Colors.white);

    // เกียร์มุมขวาล่าง
    if (f.s.pixelFont) {
      drawPixelText(c, f.gear, Offset(w * 0.96, h * 0.9),
          px: _px(h * 0.2), color: kAccent, anchor: Alignment.bottomRight);
    } else {
    drawText(c, f.gear, Offset(w * 0.96, h * 0.9),
        size: h * 0.2,
        weight: FontWeight.w900,
        color: kAccent,
        outline: Colors.black,
        outlineWidth: h * 0.02,
        anchor: Alignment.bottomRight);
    }

    // แถบรอบด้านล่าง
    final y = h * 0.955, x0 = w * 0.03, x1 = w * 0.97;
    final t = clampD(f.rpm / f.maxRpm, 0, 1);
    if (f.s.pixelFont) {
      _pixelBar(c, x0, x1, y, h, t, blink);
    } else {
    c.drawLine(Offset(x0, y), Offset(x1, y),
        Paint()
          ..color = Colors.black54
          ..strokeWidth = h * 0.025
          ..strokeCap = StrokeCap.round);
    final col = f.atRedline
        ? (blink ? kRed : Colors.white)
        : (f.rpm >= f.shiftStart ? kAccent : Colors.white);
    final xe = x0 + (x1 - x0) * t;
    if (t > 0) {
      c.drawLine(Offset(x0, y), Offset(xe, y),
          Paint()
            ..color = col.withAlpha(140)
            ..strokeWidth = h * 0.03
            ..strokeCap = StrokeCap.round
            ..maskFilter = const MaskFilter.blur(BlurStyle.normal, 5));
      c.drawLine(Offset(x0, y), Offset(xe, y),
          Paint()
            ..color = col
            ..strokeWidth = h * 0.014
            ..strokeCap = StrokeCap.round);
    }
    }

    if (!hasPics) {
      drawText(c, 'MENU → รูปพื้นหลัง เพื่อเพิ่มรูปของคุณ (JPG/PNG/GIF)',
          Offset(w * 0.62, h * 0.45),
          size: h * 0.04, color: Colors.white60, anchor: Alignment.center);
    }
  }

  /// แถบรอบแบบบล็อก (เขียว → เหลือง → แดง) สไตล์เกม 16-bit
  void _pixelBar(Canvas c, double x0, double x1, double y, double h, double t,
      bool blink) {
    final b = clampD((h * 0.032).roundToDouble(), 4, 40);
    final gap = clampD((b / 4).roundToDouble(), 1, 10);
    final n = ((x1 - x0) / (b + gap)).floor();
    if (n <= 0) return;
    final lit = (t * n).ceil();
    final max = f.maxRpm.toDouble();
    final shade = Paint()..color = Colors.black;
    final off = Paint()..color = const Color(0x88000000);
    for (var i = 0; i < n; i++) {
      final r = Rect.fromLTWH(x0 + i * (b + gap), y - b / 2, b, b);
      c.drawRect(r.inflate(gap / 2 + 1), shade);
      if (i >= lit) {
        c.drawRect(r, off);
        continue;
      }
      final v = (i + 0.5) / n * max;
      Color col = v >= f.redline
          ? kRed
          : (v >= f.shiftStart ? const Color(0xFFFFD23F) : const Color(0xFF3CE05A));
      if (f.atRedline && blink) col = Colors.white;
      c.drawRect(r, Paint()..color = col);
      // ไฮไลต์มุมบน ให้ดูเป็นบล็อกเกม
      c.drawRect(Rect.fromLTWH(r.left, r.top, b, gap),
          Paint()..color = Colors.white.withAlpha(90));
    }
  }

  @override
  bool shouldRepaint(covariant _OverlayPainter old) => true;
}
