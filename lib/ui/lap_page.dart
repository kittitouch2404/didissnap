import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:math' as math;
import 'dart:ui' show FontFeature;

import 'package:flutter/material.dart' hide Text;
import 'package:path_provider/path_provider.dart';

import '../data/hub.dart';
import '../data/laptimer.dart';
import '../data/native.dart';
import '../i18n.dart';
import 'paint_utils.dart';

const _mono = [FontFeature.tabularFigures()];

// ======================================================================
//  วาดเส้นทาง GPS (ทิศเหนือขึ้นบน) — เส้นเทา = ทั้งหมด, เส้นสี = รอบที่เลือก (สีตามความเร็ว)
// ======================================================================
class TrackPainter extends CustomPainter {
  TrackPainter({
    required this.pts,
    this.from = 0,
    this.to = -1,
    this.line,
    this.pos,
    this.tick = 0,
  });
  final List<LapPt> pts;
  final int from, to; // ช่วงที่ไฮไลต์ (index) — to < 0 = ไม่มี
  final LapLine? line;
  final (double, double)? pos; // ตำแหน่งปัจจุบัน
  final int tick;

  static Color speedColor(double f) =>
      HSVColor.fromAHSV(1, (1 - f.clamp(0.0, 1.0)) * 230, 0.9, 1).toColor();

  @override
  void paint(Canvas c, Size size) {
    if (pts.isEmpty && line == null) return;
    final proj = pts.isNotEmpty ? GeoProj(pts.first.lat, pts.first.lon) : GeoProj(line!.lat, line!.lon);
    final xy = [for (final p in pts) proj.xy(p.lat, p.lon)];
    (Offset, Offset)? le = line?.ends(proj);
    final pp = pos == null ? null : proj.xy(pos!.$1, pos!.$2);

    // ขอบเขต
    var minX = double.infinity, minY = double.infinity, maxX = -double.infinity, maxY = -double.infinity;
    void inc(Offset o) {
      minX = math.min(minX, o.dx);
      maxX = math.max(maxX, o.dx);
      minY = math.min(minY, o.dy);
      maxY = math.max(maxY, o.dy);
    }

    for (final o in xy) {
      inc(o);
    }
    if (le != null) {
      inc(le.$1);
      inc(le.$2);
    }
    if (pp != null) inc(pp);
    final w = math.max(maxX - minX, 60.0), h = math.max(maxY - minY, 60.0);
    const pad = 14.0;
    final sc = math.min((size.width - pad * 2) / w, (size.height - pad * 2) / h);
    final cx = (minX + maxX) / 2, cy = (minY + maxY) / 2;
    Offset sp(Offset o) => Offset(size.width / 2 + (o.dx - cx) * sc, size.height / 2 - (o.dy - cy) * sc);

    // เส้นทางทั้งหมด (เทา)
    if (xy.length > 1) {
      final path = Path()..moveTo(sp(xy.first).dx, sp(xy.first).dy);
      var last = sp(xy.first);
      for (var i = 1; i < xy.length; i++) {
        final s = sp(xy[i]);
        if ((s - last).distanceSquared < 1 && i != xy.length - 1) continue;
        path.lineTo(s.dx, s.dy);
        last = s;
      }
      c.drawPath(
          path,
          Paint()
            ..style = PaintingStyle.stroke
            ..strokeWidth = 2
            ..strokeJoin = StrokeJoin.round
            ..color = Colors.white24);
    }

    // รอบที่ไฮไลต์: สีตามความเร็ว
    if (to > from && from >= 0 && to < pts.length) {
      final a = math.max(0, from - 1), b = math.min(pts.length - 1, to + 1);
      var lo = double.infinity, hi = 0.0;
      for (var i = a; i <= b; i++) {
        lo = math.min(lo, pts[i].spd);
        hi = math.max(hi, pts[i].spd);
      }
      final rng = math.max(hi - lo, 1.0);
      final pa = Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 4
        ..strokeCap = StrokeCap.round;
      for (var i = a; i < b; i++) {
        pa.color = speedColor((pts[i].spd - lo) / rng);
        c.drawLine(sp(xy[i]), sp(xy[i + 1]), pa);
      }
    }

    // เส้นสตาร์ท + ลูกศรทิศวิ่ง
    if (le != null) {
      final a = sp(le.$1), b = sp(le.$2);
      c.drawLine(a, b, Paint()
        ..color = Colors.white
        ..strokeWidth = 4
        ..strokeCap = StrokeCap.round);
      final mid = (a + b) / 2;
      final d = line!.dir;
      final tip = mid + Offset(d.dx, -d.dy) * 16;
      c.drawLine(mid, tip, Paint()
        ..color = kRed
        ..strokeWidth = 3);
      c.drawCircle(tip, 3.5, Paint()..color = kRed);
    }

    // ตำแหน่งปัจจุบัน
    if (pp != null) {
      final s = sp(pp);
      c.drawCircle(s, 9, Paint()..color = kBlue.withAlpha(90));
      c.drawCircle(s, 5, Paint()..color = Colors.white);
    }
  }

  @override
  bool shouldRepaint(TrackPainter o) =>
      o.tick != tick || o.pts.length != pts.length || o.from != from || o.to != to || o.line != line || o.pos != pos;
}

// ======================================================================
//  หน้าจับเวลาสนาม (สด)
// ======================================================================
class LapView extends StatefulWidget {
  const LapView({super.key, required this.hub});
  final DataHub hub;
  @override
  State<LapView> createState() => _LapViewState();
}

class _LapViewState extends State<LapView> {
  Timer? _t;
  LapTimer get lt => widget.hub.lap;

  @override
  void initState() {
    super.initState();
    lt.view(true);
    _t = Timer.periodic(const Duration(milliseconds: 50), (_) {
      if (mounted) setState(() {});
    });
  }

  @override
  void dispose() {
    _t?.cancel();
    lt.view(false);
    super.dispose();
  }

  void _snack(String m) =>
      ScaffoldMessenger.maybeOf(context)?.showSnackBar(SnackBar(content: Text(m), duration: const Duration(seconds: 3)));

  @override
  Widget build(BuildContext context) {
    final s = lt.cur;
    final cm = lt.curMs;
    final blink = (DateTime.now().millisecondsSinceEpoch ~/ 300).isEven;
    final (String msg, Color col) = lt.err.isNotEmpty
        ? (lt.err, kRed)
        : !lt.hasFix
            ? ('รอสัญญาณ GPS...', blink ? kAmber : const Color(0xFF6B5200))
            : !lt.active
                ? ('กด START เพื่อเริ่มบันทึก', kGrey)
                : lt.line == null
                    ? ('ขับไปที่เส้นสตาร์ทแล้วกด "ตั้งเส้น"', kAmber)
                    : cm == null
                        ? ('ผ่านเส้นสตาร์ทเพื่อเริ่มรอบแรก', kBlue)
                        : ('รอบที่ ${lt.lapNo} · ${lt.line!.name}', kAccent);

    // 4 วินาทีแรกหลังผ่านเส้น: โชว์เวลารอบที่เพิ่งจบ
    final showLast = cm != null && cm < 4000 && lt.lastLapMs != null;
    final bigMs = showLast ? lt.lastLapMs : cm;
    final ld = lt.lastLapDelta;
    final bigCol = showLast
        ? (ld == null || ld <= 0 ? kGreen : kRed)
        : (cm == null ? Colors.white38 : Colors.white);
    final delta = showLast ? ld : lt.liveDelta;
    final best = s?.best;
    final hacc = lt.hacc;

    return Container(
      color: Colors.black,
      padding: const EdgeInsets.fromLTRB(16, 40, 16, 14),
      child: Row(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
        // ---------------- ซ้าย ----------------
        Expanded(
          flex: 5,
          child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
            Row(children: [
              Flexible(
                child: Container(
                  padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
                  decoration: BoxDecoration(
                    color: col.withAlpha(40),
                    border: Border.all(color: col, width: 2),
                    borderRadius: BorderRadius.circular(8),
                  ),
                  child: Text(msg,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: TextStyle(color: col, fontSize: 16, fontWeight: FontWeight.w800)),
                ),
              ),
              const SizedBox(width: 10),
              Icon(Icons.satellite_alt, size: 16, color: lt.hasFix ? kGreen : kGrey),
              const SizedBox(width: 4),
              Text(
                  lt.hasFix
                      ? '${lt.hz.toStringAsFixed(0)} Hz${hacc != null && hacc >= 0 ? ' ±${hacc.toStringAsFixed(0)} m' : ''}'
                      : '--',
                  style: const TextStyle(fontSize: 12, color: kGrey)),
            ]),
            Expanded(
              child: FittedBox(
                fit: BoxFit.scaleDown,
                alignment: Alignment.centerLeft,
                child: Text(
                  fmtLap(bigMs ?? 0),
                  style: TextStyle(
                    fontSize: 110,
                    fontWeight: FontWeight.w900,
                    fontStyle: FontStyle.italic,
                    color: bigCol,
                    fontFeatures: _mono,
                  ),
                ),
              ),
            ),
            Row(crossAxisAlignment: CrossAxisAlignment.baseline, textBaseline: TextBaseline.alphabetic, children: [
              Text(delta == null ? '' : fmtDelta(delta),
                  style: TextStyle(
                      fontSize: 30,
                      fontWeight: FontWeight.w900,
                      color: (delta ?? 0) <= 0 ? kGreen : kRed,
                      fontFeatures: _mono)),
              if (delta != null) const SizedBox(width: 18),
              _big(fmtLap(lt.lastLapMs), 'ล่าสุด'),
              const SizedBox(width: 18),
              _big(fmtLap(best?.ms), 'ดีสุด', col: const Color(0xFF8FD3FF)),
            ]),
            const SizedBox(height: 4),
            Row(children: [
              _big(widget.hub.data.speed.toStringAsFixed(0), 'km/h'),
              const SizedBox(width: 18),
              _big('${s?.laps.length ?? 0}', 'รอบ'),
            ]),
            const SizedBox(height: 8),
            Wrap(spacing: 8, runSpacing: 6, crossAxisAlignment: WrapCrossAlignment.center, children: [
              lt.active
                  ? FilledButton.icon(
                      style: FilledButton.styleFrom(backgroundColor: kRed, foregroundColor: Colors.white),
                      onPressed: () async {
                        final n = lt.cur?.laps.length ?? 0;
                        await lt.stopSession();
                        _snack('บันทึกแล้ว ($n รอบ) — ดูได้ที่ ประวัติ');
                      },
                      icon: const Icon(Icons.stop),
                      label: const Text('STOP', style: TextStyle(fontWeight: FontWeight.w900)),
                    )
                  : FilledButton.icon(
                      style: FilledButton.styleFrom(backgroundColor: kGreen, foregroundColor: Colors.black),
                      onPressed: lt.startSession,
                      icon: const Icon(Icons.fiber_manual_record),
                      label: const Text('START', style: TextStyle(fontWeight: FontWeight.w900)),
                    ),
              OutlinedButton.icon(
                icon: const Icon(Icons.flag, size: 18),
                label: const Text('ตั้งเส้น'),
                onPressed: () {
                  final e = lt.setLineHere();
                  _snack(e ?? 'ตั้งเส้นสตาร์ท "${lt.line!.name}" แล้ว');
                },
              ),
              OutlinedButton.icon(
                icon: const Icon(Icons.map_outlined, size: 18),
                label: const Text('สนาม'),
                onPressed: () => showDialog<void>(context: context, builder: (_) => _TracksDialog(lt: lt)),
              ),
              TextButton.icon(
                icon: const Icon(Icons.history, size: 18),
                label: const Text('ประวัติ'),
                onPressed: () => Navigator.of(context)
                    .push(MaterialPageRoute<void>(builder: (_) => const LapHistoryPage())),
              ),
            ]),
          ]),
        ),
        const SizedBox(width: 16),
        // ---------------- ขวา: แผนที่ + รายการรอบ ----------------
        Expanded(
          flex: 6,
          child: Column(children: [
            Expanded(
              flex: 3,
              child: Container(
                decoration: BoxDecoration(
                  color: const Color(0xFF0B0B0B),
                  border: Border.all(color: const Color(0xFF262626)),
                  borderRadius: BorderRadius.circular(10),
                ),
                child: s == null && lt.line == null && !lt.hasFix
                    ? const Center(
                        child: Text('เส้นทาง GPS จะขึ้นตรงนี้', style: TextStyle(color: Colors.white38)))
                    : CustomPaint(
                        size: Size.infinite,
                        painter: TrackPainter(
                          pts: s?.pts ?? const [],
                          from: lt.curFrom,
                          to: (s?.pts.length ?? 0) - 1,
                          line: lt.line,
                          pos: lt.hasFix ? (lt.lat!, lt.lon!) : null,
                          tick: s?.pts.length ?? 0,
                        ),
                      ),
              ),
            ),
            const SizedBox(height: 6),
            Expanded(flex: 2, child: _lapList(s)),
          ]),
        ),
      ]),
    );
  }

  Widget _lapList(LapSession? s) {
    if (s == null || s.laps.isEmpty) {
      return const Center(
        child: Text('วิ่งในสนามหรือพื้นที่ปิดเท่านั้น · GPS ของจอ/มือถือส่วนใหญ่ 1 Hz เวลาคลาดเคลื่อนราว ±0.1 วินาที',
            textAlign: TextAlign.center, style: TextStyle(fontSize: 11, color: Colors.white38)),
      );
    }
    final b = s.best!;
    final laps = s.laps.reversed.toList();
    return ListView(children: [
      for (final l in laps)
        Container(
          padding: const EdgeInsets.symmetric(vertical: 4, horizontal: 8),
          decoration: const BoxDecoration(border: Border(bottom: BorderSide(color: Color(0xFF262626)))),
          child: Row(children: [
            SizedBox(width: 44, child: Text('L${l.n}', style: const TextStyle(color: kGrey, fontSize: 14))),
            Expanded(
              child: Text(fmtLap(l.ms),
                  style: TextStyle(
                      fontSize: 17,
                      fontWeight: FontWeight.w800,
                      color: identical(l, b) ? const Color(0xFF8FD3FF) : Colors.white,
                      fontFeatures: _mono)),
            ),
            Text(identical(l, b) ? 'BEST' : fmtDelta((l.ms - b.ms) / 1000),
                style: TextStyle(
                    fontSize: 14,
                    fontWeight: FontWeight.w700,
                    color: identical(l, b) ? const Color(0xFF8FD3FF) : kRed,
                    fontFeatures: _mono)),
            SizedBox(
                width: 70,
                child: Text('${l.top.toStringAsFixed(0)} km/h',
                    textAlign: TextAlign.right, style: const TextStyle(fontSize: 12, color: kGrey))),
          ]),
        ),
    ]);
  }

  Widget _big(String v, String unit, {Color? col}) => Row(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.baseline,
        textBaseline: TextBaseline.alphabetic,
        children: [
          Text(v,
              style: TextStyle(fontSize: 26, fontWeight: FontWeight.w800, color: col, fontFeatures: _mono)),
          const SizedBox(width: 4),
          Text(unit, style: const TextStyle(fontSize: 12, color: kGrey)),
        ],
      );
}

// ======================================================================
//  จัดการสนามที่บันทึกไว้ (เส้นสตาร์ท)
// ======================================================================
class _TracksDialog extends StatelessWidget {
  const _TracksDialog({required this.lt});
  final LapTimer lt;

  @override
  Widget build(BuildContext context) => AnimatedBuilder(
        animation: lt,
        builder: (context, _) => AlertDialog(
          title: const Text('สนาม (เส้นสตาร์ท)'),
          content: SizedBox(
            width: 460,
            child: lt.tracks.isEmpty
                ? const Text('ยังไม่มีสนาม — ขับไปที่เส้นสตาร์ทแล้วกด "ตั้งเส้น"\n'
                    'ครั้งต่อไปมาสนามเดิม แอปจะเลือกเส้นให้เองอัตโนมัติ (ในระยะ 2 กม.)')
                : ListView(shrinkWrap: true, children: [
                    for (final t in lt.tracks)
                      ListTile(
                        leading: Icon(identical(t, lt.line) ? Icons.radio_button_checked : Icons.radio_button_off,
                            color: identical(t, lt.line) ? kAccent : null),
                        title: Text(t.name),
                        subtitle: Text('${t.lat.toStringAsFixed(5)}, ${t.lon.toStringAsFixed(5)} · ทิศ ${t.hdg.round()}°'),
                        onTap: () => lt.useTrack(t),
                        trailing: Row(mainAxisSize: MainAxisSize.min, children: [
                          IconButton(
                            icon: const Icon(Icons.edit, size: 20),
                            onPressed: () async {
                              final n = await _ask(context, t.name);
                              if (n != null && n.trim().isNotEmpty) lt.renameTrack(t, n.trim());
                            },
                          ),
                          IconButton(
                            icon: const Icon(Icons.delete_outline, size: 20),
                            onPressed: () => lt.deleteTrack(t),
                          ),
                        ]),
                      ),
                  ]),
          ),
          actions: [TextButton(onPressed: () => Navigator.pop(context), child: const Text('ปิด'))],
        ),
      );
}

Future<String?> _ask(BuildContext context, String init) {
  final c = TextEditingController(text: init);
  return showDialog<String>(
    context: context,
    builder: (ctx) => AlertDialog(
      title: const Text('ชื่อสนาม'),
      content: TextField(controller: c, autofocus: true),
      actions: [
        TextButton(onPressed: () => Navigator.pop(ctx), child: const Text('ยกเลิก')),
        FilledButton(onPressed: () => Navigator.pop(ctx, c.text), child: const Text('ตกลง')),
      ],
    ),
  );
}

// ======================================================================
//  ประวัติ session
// ======================================================================
class LapHistoryPage extends StatefulWidget {
  const LapHistoryPage({super.key});
  @override
  State<LapHistoryPage> createState() => _LapHistoryPageState();
}

class _LapHistoryPageState extends State<LapHistoryPage> {
  List<(File, LapSession)>? _list;

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    final out = <(File, LapSession)>[];
    for (final f in await LapTimer.sessions()) {
      final s = await LapTimer.load(f);
      if (s != null) out.add((f, s));
    }
    if (mounted) setState(() => _list = out);
  }

  static String _date(DateTime t) {
    String two(int x) => x.toString().padLeft(2, '0');
    return '${two(t.day)}/${two(t.month)}/${t.year} ${two(t.hour)}:${two(t.minute)}';
  }

  @override
  Widget build(BuildContext context) {
    final l = _list;
    return Scaffold(
      backgroundColor: Colors.black,
      appBar: AppBar(title: const Text('ประวัติจับเวลาสนาม')),
      body: l == null
          ? const Center(child: CircularProgressIndicator())
          : l.isEmpty
              ? const Center(child: Text('ยังไม่มีบันทึก', style: TextStyle(color: Colors.white54)))
              : ListView(children: [
                  for (final (f, s) in l)
                    ListTile(
                      leading: SizedBox(
                        width: 64,
                        height: 48,
                        child: CustomPaint(painter: TrackPainter(pts: s.pts, line: s.line)),
                      ),
                      title: Text('${s.trackName} · ${_date(s.start)}'),
                      subtitle: Text('${s.laps.length} รอบ · ดีสุด ${fmtLap(s.best?.ms)} · '
                          'สูงสุด ${s.top.toStringAsFixed(0)} km/h'),
                      trailing: const Icon(Icons.chevron_right),
                      onTap: () async {
                        await Navigator.of(context)
                            .push(MaterialPageRoute<void>(builder: (_) => LapSessionPage(file: f, s: s)));
                        _load();
                      },
                    ),
                ]),
    );
  }
}

// ======================================================================
//  ดู session ย้อนหลัง: แผนที่ + ตารางรอบ + ส่งออก
// ======================================================================
class LapSessionPage extends StatefulWidget {
  const LapSessionPage({super.key, required this.file, required this.s});
  final File file;
  final LapSession s;
  @override
  State<LapSessionPage> createState() => _LapSessionPageState();
}

class _LapSessionPageState extends State<LapSessionPage> {
  Lap? sel;

  @override
  void initState() {
    super.initState();
    sel = widget.s.best;
  }

  Future<void> _export(bool gpx) async {
    final s = widget.s;
    final base = widget.file.path.split('/').last.replaceAll('.json', '');
    final name = '$base.${gpx ? 'gpx' : 'csv'}';
    final tmp = File('${(await getTemporaryDirectory()).path}/$name');
    await tmp.writeAsString(gpx ? s.toGpx() : s.toCsv());
    final ok = await Native.saveToDownloads(tmp.path, name, gpx ? 'application/gpx+xml' : 'text/csv');
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(
          content: Text(ok ? 'บันทึกไว้ที่ Download/Didissnap/$name' : 'บันทึกไม่สำเร็จ')));
    }
  }

  @override
  Widget build(BuildContext context) {
    final s = widget.s;
    final b = s.best;
    return Scaffold(
      backgroundColor: Colors.black,
      appBar: AppBar(
        title: Text(s.trackName),
        actions: [
          IconButton(
            tooltip: 'แก้ชื่อสนาม',
            icon: const Icon(Icons.edit),
            onPressed: () async {
              final n = await _ask(context, s.line?.name ?? '');
              if (n == null || n.trim().isEmpty) return;
              s.line ??= s.pts.isNotEmpty ? LapLine(s.pts.first.lat, s.pts.first.lon, 0) : null;
              s.line?.name = n.trim();
              await widget.file.writeAsString(_json(s));
              setState(() {});
            },
          ),
          TextButton(onPressed: () => _export(true), child: const Text('GPX')),
          TextButton(onPressed: () => _export(false), child: const Text('CSV')),
          IconButton(
            tooltip: 'ลบ',
            icon: const Icon(Icons.delete_outline),
            onPressed: () async {
              final ok = await showDialog<bool>(
                context: context,
                builder: (c) => AlertDialog(
                  title: const Text('ลบบันทึกนี้?'),
                  actions: [
                    TextButton(onPressed: () => Navigator.pop(c, false), child: const Text('ยกเลิก')),
                    FilledButton(onPressed: () => Navigator.pop(c, true), child: const Text('ลบ')),
                  ],
                ),
              );
              if (ok == true) {
                await widget.file.delete();
                if (context.mounted) Navigator.pop(context);
              }
            },
          ),
        ],
      ),
      body: Padding(
        padding: const EdgeInsets.all(12),
        child: Row(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
          Expanded(
            flex: 6,
            child: Container(
              decoration: BoxDecoration(
                color: const Color(0xFF0B0B0B),
                border: Border.all(color: const Color(0xFF262626)),
                borderRadius: BorderRadius.circular(10),
              ),
              child: Stack(children: [
                Positioned.fill(
                  child: CustomPaint(
                    painter: TrackPainter(
                      pts: s.pts,
                      from: sel?.i0 ?? 0,
                      to: sel?.i1 ?? (s.laps.isEmpty ? s.pts.length - 1 : -1),
                      line: s.line,
                    ),
                  ),
                ),
                Positioned(
                  left: 10,
                  bottom: 8,
                  child: Row(children: [
                    const Text('ช้า ', style: TextStyle(fontSize: 11, color: kGrey)),
                    for (var i = 0; i <= 10; i++)
                      Container(width: 10, height: 6, color: TrackPainter.speedColor(i / 10)),
                    const Text(' เร็ว', style: TextStyle(fontSize: 11, color: kGrey)),
                  ]),
                ),
              ]),
            ),
          ),
          const SizedBox(width: 12),
          Expanded(
            flex: 4,
            child: s.laps.isEmpty
                ? const Center(
                    child: Text('ไม่มีรอบที่วิ่งครบ (แสดงเส้นทางทั้งหมด)', style: TextStyle(color: Colors.white54)))
                : ListView(children: [
                    Padding(
                      padding: const EdgeInsets.only(bottom: 6),
                      child: Text('${s.laps.length} รอบ · ดีสุด ${fmtLap(b?.ms)} · สูงสุด ${s.top.toStringAsFixed(0)} km/h',
                          style: const TextStyle(color: kGrey)),
                    ),
                    for (final l in s.laps)
                      ListTile(
                        dense: true,
                        selected: identical(l, sel),
                        selectedTileColor: const Color(0xFF1A1A1A),
                        onTap: () => setState(() => sel = l),
                        leading: Text('L${l.n}', style: const TextStyle(color: kGrey)),
                        title: Text(fmtLap(l.ms),
                            style: TextStyle(
                                fontSize: 18,
                                fontWeight: FontWeight.w800,
                                color: identical(l, b) ? const Color(0xFF8FD3FF) : Colors.white,
                                fontFeatures: _mono)),
                        trailing: Text(
                            '${identical(l, b) ? 'BEST' : fmtDelta((l.ms - b!.ms) / 1000)}  ${l.top.toStringAsFixed(0)} km/h',
                            style: const TextStyle(fontFeatures: _mono)),
                      ),
                  ]),
          ),
        ]),
      ),
    );
  }
}

String _json(LapSession s) => jsonEncode(s.toJson());
