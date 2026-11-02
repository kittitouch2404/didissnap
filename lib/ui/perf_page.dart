import 'dart:async';
import 'dart:ui' show FontFeature;

import 'package:flutter/material.dart';

import '../data/car_data.dart';
import '../data/hub.dart';
import '../data/perf.dart';
import 'paint_utils.dart';

/// หน้า Record: จับเวลาออกตัว 0-60 ft, 0-100 km/h, 1/4 ไมล์ ฯลฯ
class PerfPage extends StatefulWidget {
  const PerfPage({super.key, required this.hub});
  final DataHub hub;

  @override
  State<PerfPage> createState() => _PerfPageState();
}

class _PerfPageState extends State<PerfPage> {
  Timer? _t;

  @override
  void initState() {
    super.initState();
    // อัปเดตความเร็วสด ๆ ตอนยังไม่ได้วิ่ง
    _t = Timer.periodic(const Duration(milliseconds: 100), (_) {
      if (mounted) setState(() {});
    });
  }

  @override
  void dispose() {
    _t?.cancel();
    super.dispose();
  }

  static String _s(double? v) => v == null ? '--' : v.toStringAsFixed(2);

  @override
  Widget build(BuildContext context) {
    final perf = widget.hub.perf;
    return AnimatedBuilder(
      animation: perf,
      builder: (context, _) {
        final d = widget.hub.data;
        final blink = (CarData.clock.elapsedMilliseconds ~/ 250).isEven;
        final (String msg, Color col) = switch (perf.state) {
          PerfState.idle => ('กด ARM เพื่อเริ่มจับเวลา', kGrey),
          PerfState.waitStop => ('หยุดรถให้นิ่งก่อน...', kAmber),
          PerfState.ready => ('พร้อม! ออกตัวได้เลย', blink ? kGreen : const Color(0xFF0E5A26)),
          PerfState.running => ('กำลังจับเวลา', kAccent),
          PerfState.done => ('เสร็จ — กด ARM เพื่อวิ่งรอบใหม่', kBlue),
        };
        final running = perf.state == PerfState.running;
        final showRun = perf.state == PerfState.running || perf.state == PerfState.done;
        return Container(
          color: Colors.black,
          padding: const EdgeInsets.fromLTRB(16, 40, 16, 14),
          child: Row(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
            // ---------------- ซ้าย: สถานะ + ตัวเลขใหญ่ ----------------
            Expanded(
              flex: 5,
              child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                Container(
                  padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
                  decoration: BoxDecoration(
                    color: col.withAlpha(40),
                    border: Border.all(color: col, width: 2),
                    borderRadius: BorderRadius.circular(8),
                  ),
                  child: Text(msg,
                      style: TextStyle(
                          color: col, fontSize: 16, fontWeight: FontWeight.w800)),
                ),
                Expanded(
                  child: FittedBox(
                    fit: BoxFit.scaleDown,
                    alignment: Alignment.centerLeft,
                    child: Text(
                      '${(showRun ? perf.elapsed : 0).toStringAsFixed(2)} s',
                      style: TextStyle(
                        fontSize: 110,
                        fontWeight: FontWeight.w900,
                        fontStyle: FontStyle.italic,
                        color: running ? Colors.white : Colors.white70,
                        fontFeatures: const [FontFeature.tabularFigures()],
                      ),
                    ),
                  ),
                ),
                Row(children: [
                  _big('${d.speed.toStringAsFixed(0)}', 'km/h'),
                  const SizedBox(width: 24),
                  _big(showRun ? perf.dist.toStringAsFixed(0) : '0', 'm'),
                  const SizedBox(width: 24),
                  _big(showRun ? perf.top.toStringAsFixed(0) : '--', 'สูงสุด'),
                ]),
                const SizedBox(height: 10),
                Row(children: [
                  FilledButton.icon(
                    style: FilledButton.styleFrom(
                        backgroundColor: kGreen, foregroundColor: Colors.black),
                    onPressed: running ? null : perf.arm,
                    icon: const Icon(Icons.flag),
                    label: const Text('ARM',
                        style: TextStyle(fontWeight: FontWeight.w900)),
                  ),
                  const SizedBox(width: 10),
                  OutlinedButton(
                    onPressed: perf.state == PerfState.idle ? null : perf.stop,
                    child: const Text('STOP'),
                  ),
                  const SizedBox(width: 10),
                  TextButton(
                    onPressed: perf.history.isEmpty ? null : perf.clearHistory,
                    child: const Text('ล้างสถิติ'),
                  ),
                ]),
              ]),
            ),
            const SizedBox(width: 16),
            // ---------------- ขวา: ตารางผล ----------------
            Expanded(
              flex: 6,
              child: Column(children: [
                _row('รายการ', 'รอบนี้', 'ดีที่สุด', header: true),
                for (final it in kPerfItems)
                  _row(
                    it.$2,
                    perf.times[it.$1] == null
                        ? '--'
                        : '${_s(perf.times[it.$1])} s'
                            '${perf.traps[it.$1] != null ? ' @${perf.traps[it.$1]!.toStringAsFixed(0)}' : ''}',
                    perf.best(it.$1) == null ? '--' : '${_s(perf.best(it.$1))} s',
                    highlight: perf.times[it.$1] != null &&
                        perf.times[it.$1] == perf.best(it.$1),
                  ),
                const Spacer(),
                Text(
                  'ใช้ความเร็วจาก OBD2 (อ่านได้ราว 5–10 ครั้ง/วินาที) เวลาอาจคลาดเคลื่อน ±0.1–0.3 วินาที · วิ่งในสนามหรือพื้นที่ปิดเท่านั้น · ทั้งหมด ${perf.history.length} รอบ',
                  style: const TextStyle(fontSize: 10.5, color: Colors.white38),
                ),
              ]),
            ),
          ]),
        );
      },
    );
  }

  Widget _big(String v, String unit) => Row(
        crossAxisAlignment: CrossAxisAlignment.baseline,
        textBaseline: TextBaseline.alphabetic,
        children: [
          Text(v,
              style: const TextStyle(
                  fontSize: 34,
                  fontWeight: FontWeight.w800,
                  fontFeatures: [FontFeature.tabularFigures()])),
          const SizedBox(width: 4),
          Text(unit, style: const TextStyle(fontSize: 13, color: kGrey)),
        ],
      );

  Widget _row(String a, String b, String c,
      {bool header = false, bool highlight = false}) {
    final st = TextStyle(
      fontSize: header ? 12 : 16,
      fontWeight: header ? FontWeight.w600 : FontWeight.w700,
      color: header ? kGrey : (highlight ? kAccent : Colors.white),
      fontFeatures: const [FontFeature.tabularFigures()],
    );
    return Container(
      padding: const EdgeInsets.symmetric(vertical: 6, horizontal: 8),
      decoration: const BoxDecoration(
          border: Border(bottom: BorderSide(color: Color(0xFF262626)))),
      child: Row(children: [
        Expanded(flex: 5, child: Text(a, style: st.copyWith(fontWeight: FontWeight.w600))),
        Expanded(flex: 5, child: Text(b, style: st, textAlign: TextAlign.right)),
        Expanded(
            flex: 3,
            child: Text(c,
                style: st.copyWith(color: header ? kGrey : const Color(0xFF8FD3FF)),
                textAlign: TextAlign.right)),
      ]),
    );
  }
}
