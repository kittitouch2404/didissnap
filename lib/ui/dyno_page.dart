import 'dart:async';
import 'dart:math' as math;

import 'package:flutter/material.dart' hide Text;
import '../i18n.dart';

import '../data/dyno.dart';
import '../data/hub.dart';

enum _St { idle, armed, rec, done }

/// หน้าไดโนเสมือน: กดพร้อม → กดคันเร่งสุด → ได้กราฟแรงม้า/แรงบิด บันทึกและเทียบกันได้
class DynoPage extends StatefulWidget {
  const DynoPage({super.key, required this.hub});
  final DataHub hub;
  @override
  State<DynoPage> createState() => _DynoPageState();
}

class _DynoPageState extends State<DynoPage> {
  static const cPs = Color(0xFFFF7A00), cNm = Color(0xFF21C8FF);
  static const cmpCols = [Color(0xFFB07CFF), Color(0xFF4AFFB0), Color(0xFFFFD84A), Color(0xFFFF4A9A)];

  _St st = _St.idle;
  final List<DynoSample> _buf = [];
  final List<DynoSample> _pre = []; // 1 วินาทีก่อนเริ่ม (ไม่ให้ช่วงต้นหาย)
  final Stopwatch _sw = Stopwatch()..start();
  Timer? _t;
  String msg = '';
  double _recStart = 0, _lowTps = -1, _maxRpm = 0, _maxKmh = 0;
  List<DynoResult> runs = [];
  int? main; // index ใน runs ที่โชว์เป็นเส้นหลัก
  final Set<int> cmp = {};

  DataHub get hub => widget.hub;
  DynoCfg get cfg => DynoCfg.of(hub.settings);

  @override
  void initState() {
    super.initState();
    runs = DynoResult.load(hub.settings);
    if (runs.isNotEmpty) main = runs.length - 1;
    _t = Timer.periodic(const Duration(milliseconds: 20), (_) => _tick());
  }

  @override
  void dispose() {
    _t?.cancel();
    hub.dynoActive = false;
    super.dispose();
  }

  double get _now => _sw.elapsedMicroseconds / 1e6;

  void _tick() {
    final d = hub.data;
    final t = _now;
    final smp = DynoSample(t, d.rpm, d.speed);
    if (st == _St.armed) {
      _pre.add(smp);
      while (_pre.isNotEmpty && t - _pre.first.t > 1.0) {
        _pre.removeAt(0);
      }
      final tps = d.tps;
      final fast = _pre.length > 10 && d.speed - _pre.first.kmh >= 4 && d.rpm > 1500;
      if ((tps != null && tps >= cfg.trigger && d.speed >= 5) || (tps == null && fast)) {
        _buf
          ..clear()
          ..addAll(_pre);
        _recStart = t;
        _lowTps = -1;
        _maxRpm = d.rpm;
        _maxKmh = d.speed;
        st = _St.rec;
      }
    } else if (st == _St.rec) {
      _buf.add(smp);
      _maxRpm = math.max(_maxRpm, d.rpm);
      _maxKmh = math.max(_maxKmh, d.speed);
      final tps = d.tps;
      if (tps != null && tps < cfg.trigger - 25) {
        if (_lowTps < 0) _lowTps = t;
      } else {
        _lowTps = -1;
      }
      final lift = _lowTps >= 0 && t - _lowTps > 0.3;
      final shift = cfg.trans == 'MT' && d.rpm < _maxRpm - 900; // เปลี่ยนเกียร์ = จบช่วง
      final slow = _maxKmh - d.speed >= 3;
      if (lift || shift || slow || t - _recStart > 45) _finish(shift ? 'ตัดตอนเปลี่ยนเกียร์' : '');
    }
    if (mounted) setState(() {});
  }

  void _arm() {
    _pre.clear();
    _buf.clear();
    msg = '';
    st = _St.armed;
    hub.dynoActive = true;
  }

  void _cancel() {
    st = runs.isEmpty ? _St.idle : _St.done;
    hub.dynoActive = false;
  }

  void _finish(String why) {
    hub.dynoActive = false;
    // ตัดช่วงท้ายที่ถอนคันเร่ง/รอบตกออก
    while (_buf.length > 5 && (_buf.last.rpm < _maxRpm - 300 && _buf.last.kmh < _maxKmh)) {
      _buf.removeLast();
    }
    final (r, err) = computeDyno(List.of(_buf), cfg);
    if (r == null) {
      msg = err;
      st = runs.isEmpty ? _St.idle : _St.done;
      return;
    }
    runs.add(r);
    DynoResult.saveAll(hub.settings, runs);
    runs = DynoResult.load(hub.settings);
    main = runs.length - 1;
    msg = why;
    st = _St.done;
  }

  // ---------------------------------------------------------------- ตั้งค่ารถ
  Future<void> _carSheet() async {
    final c = DynoCfg(cfg.toJson());
    final q = TextEditingController();
    await showModalBottomSheet<void>(
      context: context,
      isScrollControlled: true,
      backgroundColor: const Color(0xFF121418),
      builder: (ctx) => StatefulBuilder(builder: (ctx, set) {
        final f = q.text.trim().toLowerCase();
        final list = kCarPresets.where((p) => f.isEmpty || p.title.toLowerCase().contains(f)).toList();
        Widget num(String label, double v, double lo, double hi, double step, String Function(double) fmt, ValueChanged<double> on) =>
            Row(children: [
              SizedBox(width: 150, child: Text(label)),
              IconButton(onPressed: v - step >= lo - 1e-9 ? () => set(() => on(v - step)) : null, icon: const Icon(Icons.remove_circle_outline)),
              SizedBox(width: 90, child: Text(fmt(v), textAlign: TextAlign.center, style: const TextStyle(fontWeight: FontWeight.w700, fontSize: 16))),
              IconButton(onPressed: v + step <= hi + 1e-9 ? () => set(() => on(v + step)) : null, icon: const Icon(Icons.add_circle_outline)),
            ]);
        return SizedBox(
          height: MediaQuery.of(ctx).size.height * 0.92,
          child: Padding(
            padding: const EdgeInsets.fromLTRB(16, 12, 16, 8),
            child: Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
              // รายชื่อรถ
              Expanded(
                child: Column(children: [
                  TextField(
                    controller: q,
                    decoration: InputDecoration(prefixIcon: const Icon(Icons.search), hintText: tr('ค้นรุ่นรถ เช่น civic, jazz, revo'), isDense: true),
                    onChanged: (_) => set(() {}),
                  ),
                  Expanded(
                    child: ListView.builder(
                      itemCount: list.length,
                      itemBuilder: (_, i) {
                        final p = list[i];
                        return ListTile(
                          dense: true,
                          selected: p.id == c.car,
                          selectedColor: cPs,
                          title: Text(p.title),
                          subtitle: Text('${p.mass.round()} กก. · ${p.trans}${p.diesel ? ' · ดีเซล' : ''} · ${p.cc} cc · ${p.ps} PS'),
                          onTap: () => set(() => c.applyPreset(p)),
                        );
                      },
                    ),
                  ),
                ]),
              ),
              const SizedBox(width: 16),
              // ค่าของรถคันนี้
              SizedBox(
                width: 470,
                child: ListView(children: [
                  Text(c.title, style: const TextStyle(fontSize: 20, fontWeight: FontWeight.w800)),
                  const SizedBox(height: 4),
                  const Text('ค่าประมาณของรุ่นมาตรฐาน — แก้ให้ตรงรถจริงได้ (น้ำหนักสำคัญที่สุด)',
                      style: TextStyle(fontSize: 12, color: Colors.white54)),
                  const SizedBox(height: 8),
                  num('น้ำหนักรถ', c.mass, 600, 3500, 10, (v) => '${v.round()} กก.', (v) => c.mass = v),
                  num('คน + ของในรถ', c.people, 0, 500, 5, (v) => '${v.round()} กก.', (v) => c.people = v),
                  Row(children: [
                    const SizedBox(width: 150, child: Text('เกียร์')),
                    SegmentedButton<String>(
                      segments: const [
                        ButtonSegment(value: 'MT', label: Text('ธรรมดา')),
                        ButtonSegment(value: 'AT', label: Text('ออโต้')),
                        ButtonSegment(value: 'CVT', label: Text('CVT')),
                      ],
                      selected: {c.trans},
                      onSelectionChanged: (v) => set(() {
                        c.trans = v.first;
                        c.loss = defaultLoss(c.trans);
                      }),
                    ),
                  ]),
                  num('สูญเสียในเกียร์', c.loss * 100, 5, 35, 1, (v) => '${v.round()} %', (v) => c.loss = v / 100),
                  num('ค่าต้านลม (Cd)', c.cd, 0.2, 0.6, 0.01, (v) => v.toStringAsFixed(2), (v) => c.cd = v),
                  num('หน้าตัดรถ', c.area, 1.6, 3.6, 0.05, (v) => '${v.toStringAsFixed(2)} m²', (v) => c.area = v),
                  num('มวลหมุน (ล้อ/เกียร์)', c.rot, 1.0, 1.2, 0.01, (v) => 'x${v.toStringAsFixed(2)}', (v) => c.rot = v),
                  num('เริ่มวัดเมื่อ TPS ≥', c.trigger, 40, 95, 5, (v) => '${v.round()} %', (v) => c.trigger = v),
                  num('ขนาดเครื่อง', c.cc.toDouble(), 600, 5000, 50, (v) => '${v.round()} cc', (v) => c.cc = v.round()),
                  SwitchListTile(
                    contentPadding: EdgeInsets.zero,
                    dense: true,
                    title: const Text('เครื่องดีเซล'),
                    subtitle: const Text('ดีเซลไม่คิดแรงม้าขณะขับจากอากาศเข้า (โหมดไดโนใช้ได้ปกติ)'),
                    value: c.diesel,
                    onChanged: (v) => set(() => c.diesel = v),
                  ),
                  const SizedBox(height: 8),
                  FilledButton(
                    onPressed: () {
                      c.save(hub.settings);
                      Navigator.pop(ctx);
                    },
                    child: const Text('ใช้ค่านี้'),
                  ),
                ]),
              ),
            ]),
          ),
        );
      }),
    );
    if (mounted) setState(() {});
  }

  // ---------------------------------------------------------------- หน้าจอ
  @override
  Widget build(BuildContext context) {
    final d = hub.data;
    final c = cfg;
    final r = main != null && main! < runs.length ? runs[main!] : null;
    return Scaffold(
      backgroundColor: const Color(0xFF07080A),
      appBar: AppBar(
        backgroundColor: const Color(0xFF0E0F12),
        title: const Text('ไดโน (แรงม้า / แรงบิด)', style: TextStyle(fontWeight: FontWeight.w900, fontStyle: FontStyle.italic)),
        actions: [
          TextButton.icon(onPressed: _carSheet, icon: const Icon(Icons.directions_car), label: Text(c.title)),
          const SizedBox(width: 8),
        ],
      ),
      body: Row(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
        SizedBox(
          width: 360,
          child: ListView(padding: const EdgeInsets.all(12), children: [
            _stateCard(d.rpm, d.speed, d.tps),
            const SizedBox(height: 10),
            Text('${c.title} · ${(c.mass + c.people).round()} กก. · ${c.trans} · สูญเสีย ${(c.loss * 100).round()}%',
                style: const TextStyle(fontSize: 12, color: Colors.white54)),
            const SizedBox(height: 10),
            if (runs.isNotEmpty) const Text('ผลที่บันทึกไว้ (แตะ = ดู · ช่องติ๊ก = เทียบ)', style: TextStyle(fontWeight: FontWeight.w700)),
            for (var i = runs.length - 1; i >= 0; i--) _runTile(i),
            const SizedBox(height: 10),
            const Text(
                'วิธีวัด: กด "พร้อมวัด" → เกียร์ธรรมดาใส่เกียร์ 3 ที่ ~2,000 รอบ แล้วกดคันเร่งสุดจนเกือบตัด / CVT-ออโต้ กดสุดจาก ~30 ถึง 110 km/h\n'
                'ถอนคันเร่งเมื่อพอ แอปหยุดวัดเอง · วัดไป-กลับถนนเดิมแล้วดูค่าเฉลี่ย (หักผลของลม/ทางลาด)\n'
                'ค่าที่ได้เป็นแรงม้าที่เครื่องโดยประมาณ (±5-10%) เหมาะเทียบก่อน-หลังแต่ง\n'
                '⚠ วัดในที่ปลอดภัยเท่านั้น ให้คนข้าง ๆ เป็นคนกดหน้าจอ',
                style: TextStyle(fontSize: 12, color: Colors.white54)),
          ]),
        ),
        const VerticalDivider(width: 1, color: Color(0xFF1E2026)),
        Expanded(
          child: Padding(
            padding: const EdgeInsets.fromLTRB(8, 12, 16, 12),
            child: r == null
                ? const Center(child: Text('ยังไม่มีผล — กด "พร้อมวัด" แล้ววัดสักรอบ', style: TextStyle(color: Colors.white54, fontSize: 18)))
                : CustomPaint(
                    painter: _DynoPainter(
                      r,
                      [for (final i in cmp) if (i < runs.length && i != main && runs[i].byRpm == r.byRpm) runs[i]],
                      c.ps,
                    ),
                    child: const SizedBox.expand(),
                  ),
          ),
        ),
      ]),
    );
  }

  Widget _stateCard(double rpm, double kmh, double? tps) {
    final live = '${rpm.round()} rpm · ${kmh.round()} km/h · TPS ${tps == null ? '--' : tps.round()}%';
    late final String head;
    late final Widget btn;
    switch (st) {
      case _St.idle:
      case _St.done:
        head = msg.isNotEmpty && st == _St.idle ? msg : (st == _St.done ? (msg.isEmpty ? 'วัดเสร็จแล้ว' : 'วัดเสร็จ · $msg') : 'พร้อมเมื่อคุณพร้อม');
        btn = FilledButton.icon(
          style: FilledButton.styleFrom(backgroundColor: cPs, foregroundColor: Colors.black, minimumSize: const Size.fromHeight(56)),
          onPressed: _arm,
          icon: const Icon(Icons.speed),
          label: Text(st == _St.done ? 'วัดอีกครั้ง' : 'พร้อมวัด', style: const TextStyle(fontSize: 18, fontWeight: FontWeight.w800)),
        );
      case _St.armed:
        head = tps == null ? 'กดคันเร่งสุดได้เลย (เริ่มเมื่อรถเร่งแรง)' : 'กดคันเร่งสุดได้เลย (เริ่มเมื่อ TPS ≥ ${cfg.trigger.round()}%)';
        btn = OutlinedButton(style: OutlinedButton.styleFrom(minimumSize: const Size.fromHeight(56)), onPressed: _cancel, child: const Text('ยกเลิก'));
      case _St.rec:
        final secs = _now - _recStart;
        head = 'กำลังวัด ${secs.toStringAsFixed(1)} วิ · ${_buf.isEmpty ? 0 : _buf.first.kmh.round()} → ${kmh.round()} km/h';
        btn = FilledButton(
          style: FilledButton.styleFrom(backgroundColor: const Color(0xFFFF3246), minimumSize: const Size.fromHeight(56)),
          onPressed: () => _finish(''),
          child: const Text('หยุดวัด', style: TextStyle(fontSize: 18, fontWeight: FontWeight.w800)),
        );
    }
    final r = main != null && main! < runs.length ? runs[main!] : null;
    return Container(
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: const Color(0xFF121418),
        borderRadius: BorderRadius.circular(14),
        border: Border.all(color: st == _St.rec ? const Color(0xFFFF3246) : (st == _St.armed ? cPs : const Color(0xFF262A31)), width: st == _St.idle || st == _St.done ? 1 : 2),
      ),
      child: Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
        Text(head, style: const TextStyle(fontSize: 15, fontWeight: FontWeight.w700)),
        const SizedBox(height: 4),
        Text(live, style: const TextStyle(fontSize: 13, color: Colors.white60)),
        if (r != null && (st == _St.done || st == _St.idle)) ...[
          const SizedBox(height: 10),
          Row(children: [
            _peak('${r.peakPs.ps.toStringAsFixed(0)} PS', '@ ${_x(r, r.peakPs.x)}', cPs),
            const SizedBox(width: 12),
            _peak('${r.peakNm.nm.toStringAsFixed(0)} Nm', '@ ${_x(r, r.peakNm.x)}', cNm),
          ]),
        ],
        const SizedBox(height: 12),
        btn,
      ]),
    );
  }

  static String _x(DynoResult r, double x) => r.byRpm ? '${x.round()} rpm' : '${x.round()} km/h';

  Widget _peak(String big, String sub, Color col) => Expanded(
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Text(big, style: TextStyle(fontSize: 30, fontWeight: FontWeight.w900, fontStyle: FontStyle.italic, color: col, height: 1)),
          Text(sub, style: const TextStyle(fontSize: 12, color: Colors.white60)),
        ]),
      );

  Widget _runTile(int i) {
    final r = runs[i];
    final w = r.when;
    String two(int n) => n.toString().padLeft(2, '0');
    final ci = cmp.toList().indexOf(i);
    return ListTile(
      dense: true,
      contentPadding: EdgeInsets.zero,
      selected: i == main,
      selectedTileColor: const Color(0xFF1A1D23),
      leading: Checkbox(
        value: cmp.contains(i),
        activeColor: ci >= 0 ? cmpCols[ci % cmpCols.length] : null,
        onChanged: i == main ? null : (v) => setState(() => v == true ? cmp.add(i) : cmp.remove(i)),
      ),
      title: Text('${r.peakPs.ps.toStringAsFixed(0)} PS · ${r.peakNm.nm.toStringAsFixed(0)} Nm'),
      subtitle: Text('${two(w.day)}/${two(w.month)} ${two(w.hour)}:${two(w.minute)} · ${r.car}\n${r.note}', style: const TextStyle(fontSize: 11)),
      isThreeLine: true,
      onTap: () => setState(() {
        main = i;
        cmp.remove(i);
      }),
      trailing: IconButton(
        icon: const Icon(Icons.delete_outline, size: 20),
        onPressed: () => setState(() {
          runs.removeAt(i);
          DynoResult.saveAll(hub.settings, runs);
          cmp.clear();
          main = runs.isEmpty ? null : runs.length - 1;
        }),
      ),
    );
  }
}

/// กราฟใบไดโน: แรงม้า (ส้ม แกนซ้าย) + แรงบิด (ฟ้า แกนขวา) · เส้นบาง = ผลที่เอามาเทียบ
class _DynoPainter extends CustomPainter {
  _DynoPainter(this.r, this.cmp, this.stockPs);
  final DynoResult r;
  final List<DynoResult> cmp;
  final int stockPs;

  void _txt(Canvas cv, String s, Offset at, {double size = 12, Color color = Colors.white60, bool right = false, bool center = false}) {
    final tp = TextPainter(text: TextSpan(text: s, style: TextStyle(fontSize: size, color: color)), textDirection: TextDirection.ltr)..layout();
    tp.paint(cv, at - Offset(right ? tp.width : (center ? tp.width / 2 : 0), tp.height / 2));
  }

  @override
  void paint(Canvas cv, Size sz) {
    final all = [r, ...cmp];
    double minX = double.infinity, maxX = 0, maxPs = 0, maxNm = 0;
    for (final e in all) {
      for (final p in e.pts) {
        minX = math.min(minX, p.x);
        maxX = math.max(maxX, p.x);
        maxPs = math.max(maxPs, p.ps);
        maxNm = math.max(maxNm, p.nm);
      }
    }
    if (stockPs > 0) maxPs = math.max(maxPs, stockPs.toDouble());
    final stepX = r.byRpm ? 1000.0 : 10.0;
    minX = (minX / stepX).floor() * stepX;
    maxX = math.max(minX + stepX, (maxX / stepX).ceil() * stepX);
    double nice(double v) {
      final st = v <= 60 ? 10.0 : (v <= 160 ? 20.0 : (v <= 400 ? 50.0 : 100.0));
      return (v * 1.1 / st).ceil() * st;
    }
    final topPs = nice(maxPs), topNm = nice(maxNm);
    final plot = Rect.fromLTRB(56, 16, sz.width - 60, sz.height - 40);
    double px(double x) => plot.left + (x - minX) / (maxX - minX) * plot.width;
    double py(double v, double top) => plot.bottom - v / top * plot.height;
    final grid = Paint()
      ..color = const Color(0xFF22262E)
      ..strokeWidth = 1;
    for (var i = 0; i <= 5; i++) {
      final y = plot.bottom - plot.height * i / 5;
      cv.drawLine(Offset(plot.left, y), Offset(plot.right, y), grid);
      _txt(cv, (topPs * i / 5).round().toString(), Offset(plot.left - 8, y), right: true, color: _DynoPageState.cPs);
      _txt(cv, (topNm * i / 5).round().toString(), Offset(plot.right + 8, y), color: _DynoPageState.cNm);
    }
    for (var x = minX; x <= maxX + 1e-6; x += stepX) {
      cv.drawLine(Offset(px(x), plot.top), Offset(px(x), plot.bottom), grid);
      _txt(cv, r.byRpm ? '${(x / 1000).toStringAsFixed(0)}k' : x.round().toString(), Offset(px(x), plot.bottom + 14), center: true);
    }
    _txt(cv, 'PS', Offset(plot.left - 8, plot.top - 6), right: true, color: _DynoPageState.cPs, size: 13);
    _txt(cv, 'Nm', Offset(plot.right + 8, plot.top - 6), color: _DynoPageState.cNm, size: 13);
    _txt(cv, r.byRpm ? 'รอบเครื่อง (rpm)' : 'ความเร็ว (km/h) — CVT รอบนิ่ง', Offset(plot.center.dx, sz.height - 8), center: true);
    // แรงม้าโรงงาน
    if (stockPs > 0) {
      final y = py(stockPs.toDouble(), topPs);
      final p = Paint()
        ..color = Colors.white24
        ..strokeWidth = 1;
      for (var x = plot.left; x < plot.right; x += 12) {
        cv.drawLine(Offset(x, y), Offset(math.min(x + 6, plot.right), y), p);
      }
      _txt(cv, 'โรงงาน $stockPs PS', Offset(plot.left + 6, y - 10), color: Colors.white38);
    }
    void line(DynoResult e, bool ps, Color col, double w) {
      final path = Path();
      for (var i = 0; i < e.pts.length; i++) {
        final p = e.pts[i];
        final o = Offset(px(p.x), py(ps ? p.ps : p.nm, ps ? topPs : topNm));
        i == 0 ? path.moveTo(o.dx, o.dy) : path.lineTo(o.dx, o.dy);
      }
      cv.drawPath(
          path,
          Paint()
            ..style = PaintingStyle.stroke
            ..strokeWidth = w
            ..strokeJoin = StrokeJoin.round
            ..color = col);
    }
    for (var i = 0; i < cmp.length; i++) {
      final col = _DynoPageState.cmpCols[i % _DynoPageState.cmpCols.length];
      line(cmp[i], true, col.withValues(alpha: 0.85), 2);
      line(cmp[i], false, col.withValues(alpha: 0.5), 1.5);
    }
    line(r, false, _DynoPageState.cNm, 3);
    line(r, true, _DynoPageState.cPs, 3.5);
    // จุดสูงสุด
    final pp = r.peakPs, pn = r.peakNm;
    cv.drawCircle(Offset(px(pp.x), py(pp.ps, topPs)), 5, Paint()..color = _DynoPageState.cPs);
    cv.drawCircle(Offset(px(pn.x), py(pn.nm, topNm)), 5, Paint()..color = _DynoPageState.cNm);
    _txt(cv, '${pp.ps.toStringAsFixed(0)} PS', Offset(px(pp.x), py(pp.ps, topPs) - 16), center: true, color: _DynoPageState.cPs, size: 14);
    _txt(cv, '${pn.nm.toStringAsFixed(0)} Nm', Offset(px(pn.x), py(pn.nm, topNm) + 16), center: true, color: _DynoPageState.cNm, size: 14);
  }

  @override
  bool shouldRepaint(covariant _DynoPainter o) => true;
}
