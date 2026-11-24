import 'dart:async';
import 'dart:io';
import 'dart:math' as math;
import 'dart:ui' show FontFeature;

import 'package:flutter/material.dart' hide Text;
import '../i18n.dart';

import '../data/hub.dart';
import '../data/services.dart';
import 'dyno_page.dart';
import '../settings.dart';

/// หน้ากราฟสด: 1-6 ค่า ย้อนหลัง 10-120 วินาที + เปิดไฟล์ CSV เก่ามาเทียบ (เส้นประ)
///  แตะชื่อค่า = เปลี่ยนค่า, แตะ "30s" = เปลี่ยนช่วงเวลา, + / - = จำนวนเส้น
class GraphPage extends StatefulWidget {
  const GraphPage({super.key, required this.hub});
  final DataHub hub;
  @override
  State<GraphPage> createState() => _GraphPageState();
}

/// ไฟล์เก่าที่เปิดมาเทียบ
class _OldRun {
  _OldRun(this.name, this.cols, this.hz, this.secs);
  final String name;
  final Map<int, List<double>> cols; // index ใน kGMetrics -> ค่า
  final double hz, secs;

  static Future<_OldRun?> load(File f) async {
    final lines = await f.readAsLines();
    if (lines.length < 3) return null;
    final head = lines.first.split(',');
    final map = <int, int>{}; // คอลัมน์ในไฟล์ -> metric
    for (var c = 0; c < head.length; c++) {
      final h = head[c].split(' (').first.trim().toUpperCase();
      for (var i = 0; i < kGMetrics.length; i++) {
        if (kGMetrics[i].name.toUpperCase() == h) map[c] = i;
      }
    }
    final cols = <int, List<double>>{for (final i in map.values) i: <double>[]};
    final ts = <double>[];
    for (final l in lines.skip(1)) {
      final p = l.split(',');
      if (p.isEmpty) continue;
      ts.add(double.tryParse(p[0]) ?? double.nan);
      map.forEach((c, i) => cols[i]!.add(c < p.length ? (double.tryParse(p[c]) ?? double.nan) : double.nan));
    }
    final dur = ts.isEmpty || ts.last.isNaN ? 0.0 : ts.last;
    final hz = dur > 0 ? (ts.length - 1) / dur : 5.0;
    return _OldRun(f.path.split('/').last, cols, hz, dur);
  }
}

class _GraphPageState extends State<GraphPage> {
  Timer? _t;
  static const cols = [
    Color(0xFFFF7A00), Color(0xFF20D4FF), Color(0xFF3DFF6E),
    Color(0xFFFF3D8B), Color(0xFFFFD200), Color(0xFFB05CFF),
  ];
  static const spans = [10, 30, 60, 120];

  bool _pause = false;
  List<List<double>>? _frozen;
  _OldRun? _old;
  double _off = 0; // วินาทีเริ่มของไฟล์เก่า
  bool _follow = false; // ไฟล์เก่าเลื่อนตามเวลา

  @override
  void initState() {
    super.initState();
    _t = Timer.periodic(const Duration(milliseconds: 100), (_) {
      if (!mounted) return;
      if (_follow && _old != null && !_pause) {
        _off = math.min(_off + 0.1, math.max(0.0, _old!.secs - 1));
      }
      setState(() {});
    });
  }

  @override
  void dispose() {
    _t?.cancel();
    super.dispose();
  }

  Future<void> _pickOld() async {
    final d = await DataLogger.dir();
    final files = d.listSync().whereType<File>().where((f) => f.path.endsWith('.csv')).toList()
      ..sort((a, b) => b.path.compareTo(a.path));
    if (!mounted) return;
    final f = await showDialog<File>(
      context: context,
      builder: (ctx) => SimpleDialog(
        title: const Text('เปิดไฟล์เก่ามาเทียบ'),
        children: [
          if (files.isEmpty) const Padding(padding: EdgeInsets.all(16), child: Text('ยังไม่มีไฟล์ CSV (เปิดบันทึกในเมนู บันทึก CSV)')),
          for (final f in files.take(30))
            SimpleDialogOption(
              onPressed: () => Navigator.pop(ctx, f),
              child: Text('${f.path.split('/').last}  ·  ${(f.lengthSync() / 1024).toStringAsFixed(0)} KB'),
            ),
        ],
      ),
    );
    if (f == null) return;
    final r = await _OldRun.load(f);
    if (mounted) {
      setState(() {
        _old = r;
        _off = 0;
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    final s = widget.hub.settings;
    final sel = s.graphSel;
    final h = widget.hub.history;
    final live = _pause ? (_frozen ?? h.v) : h.v;
    final span = s.graphSec;
    final small = sel.length > 3;
    final old = _old;
    return ColoredBox(
      color: Colors.black,
      child: Padding(
        padding: const EdgeInsets.fromLTRB(12, 40, 12, 18),
        child: Column(children: [
          Row(children: [
            for (var i = 0; i < sel.length; i++)
              Expanded(
                child: GestureDetector(
                  onTap: () {
                    final l = [...sel];
                    var m = l[i];
                    for (var k = 0; k < kGMetrics.length; k++) {
                      m = (m + 1) % kGMetrics.length;
                      final cu = kGMetrics[m].custom;
                      if (cu != null && !s.customPid(cu).valid) continue;
                      if (!l.contains(m)) break;
                    }
                    l[i] = m;
                    s.graphSel = l;
                  },
                  child: _Legend(
                    info: gInfo(kGMetrics[sel[i]], s),
                    dec: kGMetrics[sel[i]].dec,
                    v: _last(live[sel[i]]),
                    old: old == null ? null : _oldAt(old, sel[i], _off + span),
                    c: cols[i],
                    small: small,
                  ),
                ),
              ),
          ]),
          const SizedBox(height: 6),
          Expanded(
            child: CustomPaint(
              painter: _GraphPainter(live, sel, span * History.hz, cols, s, old, _off, span.toDouble()),
              child: const SizedBox.expand(),
            ),
          ),
          const SizedBox(height: 6),
          Row(children: [
            _btn('${span}s', () {
              final k = spans.indexOf(span);
              s.graphSec = spans[(k + 1) % spans.length];
            }),
            _btn('−', sel.length <= 1 ? null : () => s.graphSel = sel.sublist(0, sel.length - 1)),
            _btn('+', sel.length >= 6
                ? null
                : () {
                    var m = 0;
                    while (sel.contains(m)) {
                      m++;
                    }
                    s.graphSel = [...sel, m];
                  }),
            _btn(_pause ? '▶ ต่อ' : '❚❚ หยุด', () {
              setState(() {
                _pause = !_pause;
                _frozen = _pause ? [for (final l in h.v) List<double>.of(l)] : null;
              });
            }),
            _btn('ไดโน (แรงม้า)', () => Navigator.of(context).push(MaterialPageRoute<void>(builder: (_) => DynoPage(hub: widget.hub)))),
            _btn(old == null ? 'เทียบไฟล์เก่า' : 'เปลี่ยนไฟล์', _pickOld),
            if (old != null) ...[
              _btn(_follow ? 'หยุดเลื่อน' : 'เลื่อนตาม', () => setState(() => _follow = !_follow)),
              Expanded(
                child: Slider(
                  min: 0,
                  max: math.max(1.0, old.secs - 1),
                  value: _off.clamp(0.0, math.max(1.0, old.secs - 1)),
                  onChanged: (v) => setState(() => _off = v),
                ),
              ),
              Text('${old.name}  ${_off.toStringAsFixed(0)}-${(_off + span).toStringAsFixed(0)}s',
                  style: const TextStyle(fontSize: 12, color: Colors.white60)),
              IconButton(
                tooltip: tr('ปิดไฟล์เก่า'),
                icon: const Icon(Icons.close, size: 18),
                onPressed: () => setState(() => _old = null),
              ),
            ] else
              const Spacer(),
          ]),
        ]),
      ),
    );
  }

  Widget _btn(String t, VoidCallback? on) => Padding(
        padding: const EdgeInsets.only(right: 8),
        child: OutlinedButton(
          style: OutlinedButton.styleFrom(
            padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 6),
            minimumSize: const Size(44, 40),
          ),
          onPressed: on,
          child: Text(t, style: const TextStyle(fontSize: 15, fontWeight: FontWeight.w700)),
        ),
      );

  static double? _last(List<double> l) {
    for (var i = l.length - 1; i >= 0 && i >= l.length - 20; i--) {
      if (!l[i].isNaN) return l[i];
    }
    return null;
  }

  /// ค่าของไฟล์เก่า ณ ขอบขวาของกราฟ
  static double? _oldAt(_OldRun o, int m, double t) {
    final l = o.cols[m];
    if (l == null || l.isEmpty) return null;
    final i = (t * o.hz).round().clamp(0, l.length - 1);
    for (var k = i; k >= 0 && k > i - 10; k--) {
      if (!l[k].isNaN) return l[k];
    }
    return null;
  }
}

class _Legend extends StatelessWidget {
  const _Legend({required this.info, required this.dec, required this.v, required this.old, required this.c, required this.small});
  final (String, String, double, double) info;
  final int dec;
  final double? v, old;
  final Color c;
  final bool small;
  @override
  Widget build(BuildContext context) => Padding(
        padding: const EdgeInsets.only(right: 8),
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
          decoration: BoxDecoration(
            color: c.withValues(alpha: 0.12),
            border: Border(left: BorderSide(color: c, width: 5)),
            borderRadius: BorderRadius.circular(6),
          ),
          child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
            Row(children: [
              Flexible(
                child: Text(info.$1,
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(color: c, fontSize: small ? 12 : 15, fontWeight: FontWeight.w700)),
              ),
              const Spacer(),
              Text(v == null ? '--' : v!.toStringAsFixed(dec),
                  style: TextStyle(
                      fontSize: small ? 22 : 30, fontWeight: FontWeight.w900, fontFeatures: const [FontFeature.tabularFigures()])),
              const SizedBox(width: 3),
              Text(info.$2, style: const TextStyle(fontSize: 11, color: Colors.white54)),
            ]),
            if (old != null)
              Text('เก่า ${old!.toStringAsFixed(dec)}', style: TextStyle(fontSize: 11, color: c.withValues(alpha: 0.7))),
          ]),
        ),
      );
}

class _GraphPainter extends CustomPainter {
  _GraphPainter(this.v, this.sel, this.n, this.cols, this.s, this.old, this.off, this.span);
  final List<List<double>> v;
  final List<int> sel;
  final int n;
  final List<Color> cols;
  final AppSettings s;
  final _OldRun? old;
  final double off, span;

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
    for (var i = 0; i < sel.length; i++) {
      final m = kGMetrics[sel[i]];
      final info = gInfo(m, s);
      var lo = info.$3, hi = info.$4;
      final l = v[sel[i]];
      final st = math.max(0, l.length - n);
      void grow(double x) {
        if (x.isNaN) return;
        if (x < lo) lo = x;
        if (x > hi) hi = x;
      }

      for (var k = st; k < l.length; k++) {
        grow(l[k]);
      }
      // ไฟล์เก่า: ช่วง off .. off+span
      final ol = old?.cols[sel[i]];
      var o0 = 0, o1 = 0;
      if (ol != null && ol.isNotEmpty) {
        o0 = (off * old!.hz).floor().clamp(0, ol.length - 1);
        o1 = ((off + span) * old!.hz).ceil().clamp(0, ol.length - 1);
        for (var k = o0; k <= o1; k++) {
          grow(ol[k]);
        }
      }
      if (hi <= lo) hi = lo + 1;
      double y(double x) => sz.height * (1 - (x - lo) / (hi - lo));

      // เส้นเก่า (จาง + ประ)
      if (ol != null && o1 > o0) {
        final p = Paint()
          ..color = cols[i].withValues(alpha: 0.45)
          ..strokeWidth = 2
          ..style = PaintingStyle.stroke;
        Offset? prev;
        for (var k = o0; k <= o1; k++) {
          final x = ol[k];
          if (x.isNaN) {
            prev = null;
            continue;
          }
          final pt = Offset(sz.width * ((k / old!.hz) - off) / span, y(x));
          if (prev != null && k % 4 < 3) c.drawLine(prev, pt, p); // ประ
          prev = pt;
        }
      }
      // เส้นสด
      final path = Path();
      var pen = false;
      for (var k = st; k < l.length; k++) {
        final x = l[k];
        if (x.isNaN) {
          pen = false;
          continue;
        }
        final px = sz.width * (1 - (l.length - 1 - k) / (n - 1));
        if (pen) {
          path.lineTo(px, y(x));
        } else {
          path.moveTo(px, y(x));
          pen = true;
        }
      }
      c.drawPath(path, Paint()
        ..color = cols[i]
        ..style = PaintingStyle.stroke
        ..strokeWidth = 3
        ..strokeJoin = StrokeJoin.round);
      final tp = TextPainter(
        text: TextSpan(
            text: '${info.$1}  ${hi.toStringAsFixed(m.dec)} / ${lo.toStringAsFixed(m.dec)}',
            style: TextStyle(color: cols[i].withValues(alpha: 0.7), fontSize: 11)),
        textDirection: TextDirection.ltr,
      )..layout();
      tp.paint(c, Offset(4, 4 + i * 14.0));
    }
  }

  @override
  bool shouldRepaint(_GraphPainter o) => true;
}
