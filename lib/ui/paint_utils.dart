import 'dart:math' as math;
import 'dart:ui' show FontFeature;

import 'package:flutter/material.dart';

const kAccent = Color(0xFFFF7A00);
const kRed = Color(0xFFFF2A2A);
const kGreen = Color(0xFF00E05A);
const kAmber = Color(0xFFFFC400);
const kBlue = Color(0xFF2E7BFF);
const kGrey = Color(0xFF8A8A8A);

const _tabular = [FontFeature.tabularFigures()];

double clampD(double v, double lo, double hi) => v < lo ? lo : (v > hi ? hi : v);

String fmt(double? v, [int dec = 0]) =>
    (v == null || v.isNaN) ? '--' : v.toStringAsFixed(dec);

/// วาดตัวหนังสือบน canvas; [anchor] บอกว่าจุด [at] คือมุมไหนของข้อความ
/// [outline] = สีขอบ (เช่น ขอบดำให้อ่านง่ายบนรูป)
Size drawText(
  Canvas c,
  String text,
  Offset at, {
  double size = 14,
  Color color = Colors.white,
  FontWeight weight = FontWeight.w700,
  Alignment anchor = Alignment.topLeft,
  Color? outline,
  double outlineWidth = 0,
  double letterSpacing = 0,
  bool italic = false,
}) {
  TextPainter mk(Paint? fg) => TextPainter(
        text: TextSpan(
          text: text,
          style: TextStyle(
            fontSize: size,
            fontWeight: weight,
            height: 1.0,
            letterSpacing: letterSpacing,
            fontStyle: italic ? FontStyle.italic : FontStyle.normal,
            fontFeatures: _tabular,
            color: fg == null ? color : null,
            foreground: fg,
          ),
        ),
        textDirection: TextDirection.ltr,
      )..layout();

  final tp = mk(null);
  final o = Offset(at.dx - (anchor.x + 1) / 2 * tp.width,
      at.dy - (anchor.y + 1) / 2 * tp.height);
  if (outline != null && outlineWidth > 0) {
    final sp = mk(Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = outlineWidth
      ..strokeJoin = StrokeJoin.round
      ..color = outline);
    sp.paint(c, o);
    sp.dispose();
  }
  tp.paint(c, o);
  final size0 = Size(tp.width, tp.height);
  tp.dispose();
  return size0;
}

/// พื้นที่วาดสมมติ (หน้าดีไซน์ตายตัว) จะถูกย่อ/ขยายให้พอดีจอและจัดกลาง
const double kVW = 1000, kVH = 460;

void drawStage(Canvas c, Size s, void Function(Canvas c) draw) {
  c.drawRect(Offset.zero & s, Paint()..color = Colors.black);
  final k = math.min(s.width / kVW, s.height / kVH);
  c.save();
  c.translate((s.width - kVW * k) / 2, (s.height - kVH * k) / 2);
  c.scale(k);
  draw(c);
  c.restore();
}
