import 'dart:async';
import 'dart:math' as math;
import 'dart:ui' show FontFeature;

import 'package:flutter/material.dart';

import '../data/hub.dart';
import '../data/services.dart';

/// หน้ากราฟสด: 3 ค่า ย้อนหลัง 10-120 วินาที  แตะชื่อค่าเพื่อเปลี่ยน, แตะ "30s" เพื่อเปลี่ยนช่วงเวลา
class GraphPage extends StatefulWidget {
  const GraphPage({super.key, required this.hub});
  final DataHub hub;
  @override
  State<GraphPage> createState() => _GraphPageState();
}

class _GraphPageState extends State<GraphPage> {
  Timer? _t;
  static const cols = [Color(0xFFFF7A00), Color(0xFF20D4FF), Color(0xFF3DFF6E)];
  static const spans = [10, 30, 60, 120];

  @override
  void initState() {
    super.initState();
    _t = Timer.periodic(const Duration(milliseconds: 100), (_) {
      if (mounted) setState(() {});
    });
  }

  @override
  void dispose() {
    _t?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final s = widget.hub.settings;
    final sel = s.graphSel;
    final h = widget.hub.history;
    return ColoredBox(
      color: Colors.black,
      child: Padding(
        padding: const EdgeInsets.fromLTRB(12, 40, 12, 18),
        child: Column(children: [
          Row(children: [
            for (var i = 0; i < 3; i++)
              Expanded(
                child: GestureDetector(
                  onTap: () {
                    final l = [...sel];
                    var m = l[i];
                    do {
                      m = (m + 1) % kGMetrics.length;
                    } while (l.contains(m));
                    l[i] = m;
                    s.graphSel = l;
                  },
                  child: _Legend(m: kGMetrics[sel[i]], v: _last(h.v[sel[i]]), c: cols[i]),
                ),
              ),
            GestureDetector(
              onTap: () {
                final k = spans.indexOf(s.graphSec);
                s.graphSec = spans[(k + 1) % spans.length];
              },
              child: Container(
                padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                decoration: BoxDecoration(border: Border.all(color: Colors.white24), borderRadius: BorderRadius.circular(8)),
                child: Text('${s.graphSec}s', style: const TextStyle(fontSize: 18, fontWeight: FontWeight.w800)),
              ),
            ),
          ]),
          const SizedBox(height: 8),
          Expanded(
            child: CustomPaint(
              painter: _GraphPainter(h, sel, s.graphSec * History.hz, cols),
              child: const SizedBox.expand(),
            ),
          ),
        ]),
      ),
    );
  }

  static double? _last(List<double> l) {
    for (var i = l.length - 1; i >= 0 && i >= l.length - 20; i--) {
      if (!l[i].isNaN) return l[i];
    }
    return null;
  }
}

class _Legend extends StatelessWidget {
  const _Legend({required this.m, required this.v, required this.c});
  final GMetric m;
  final double? v;
  final Color c;
  @override
  Widget build(BuildContext context) => Padding(
        padding: const EdgeInsets.only(right: 10),
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
          decoration: BoxDecoration(
            color: c.withValues(alpha: 0.12),
            border: Border(left: BorderSide(color: c, width: 5)),
            borderRadius: BorderRadius.circular(6),
          ),
          child: Row(children: [
            Text(m.name, style: TextStyle(color: c, fontSize: 15, fontWeight: FontWeight.w700)),
            const Spacer(),
            Text(v == null ? '--' : v!.toStringAsFixed(m.dec),
                style: const TextStyle(fontSize: 30, fontWeight: FontWeight.w900, fontFeatures: [FontFeature.tabularFigures()])),
            const SizedBox(width: 4),
            Text(m.unit, style: const TextStyle(fontSize: 13, color: Colors.white54)),
          ]),
        ),
      );
}

class _GraphPainter extends CustomPainter {
  _GraphPainter(this.h, this.sel, this.n, this.cols);
  final History h;
  final List<int> sel;
  final int n;
  final List<Color> cols;

  @override
  void paint(Canvas c, Size sz) {
    final grid = Paint()
      ..color = Colors.white10
      ..strokeWidth = 1;
    for (var k = 0; k <= 4; k++) {
      final y = sz.height * k / 4;
      c.drawLine(Offset(0, y), Offset(sz.width, y), grid);
    }
    for (var k = 0; k <= 6; k++) {
      final x = sz.width * k / 6;
      c.drawLine(Offset(x, 0), Offset(x, sz.height), grid);
    }
    for (var i = 0; i < 3; i++) {
      final m = kGMetrics[sel[i]];
      final l = h.v[sel[i]];
      if (l.isEmpty) continue;
      // สเกล: ใช้ช่วงมาตรฐาน แต่ขยายตามค่าจริงถ้าเกิน
      var lo = m.lo, hi = m.hi;
      final st = math.max(0, l.length - n);
      for (var k = st; k < l.length; k++) {
        final v = l[k];
        if (v.isNaN) continue;
        if (v < lo) lo = v;
        if (v > hi) hi = v;
      }
      final p = Path();
      var pen = false;
      for (var k = st; k < l.length; k++) {
        final v = l[k];
        if (v.isNaN) {
          pen = false;
          continue;
        }
        final x = sz.width * (1 - (l.length - 1 - k) / (n - 1));
        final y = sz.height * (1 - (v - lo) / (hi - lo));
        if (pen) {
          p.lineTo(x, y);
        } else {
          p.moveTo(x, y);
          pen = true;
        }
      }
      c.drawPath(p, Paint()
        ..color = cols[i]
        ..style = PaintingStyle.stroke
        ..strokeWidth = 3
        ..strokeJoin = StrokeJoin.round);
      // ป้ายสเกลซ้าย (บน/ล่าง)
      final tp = TextPainter(
        text: TextSpan(
            text: '${hi.toStringAsFixed(m.dec)} / ${lo.toStringAsFixed(m.dec)}',
            style: TextStyle(color: cols[i].withValues(alpha: 0.7), fontSize: 11)),
        textDirection: TextDirection.ltr,
      )..layout();
      tp.paint(c, Offset(4, 4 + i * 14.0));
    }
  }

  @override
  bool shouldRepaint(_GraphPainter o) => true;
}
