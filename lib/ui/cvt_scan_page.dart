import 'dart:async';
import 'dart:ui' show FontFeature;

import 'package:flutter/material.dart' hide Text;

import '../data/custom_pid.dart';
import '../data/hub.dart';
import '../i18n.dart';
import 'paint_utils.dart';
import 'settings_page.dart' show kCvtPresets, CvtPreset;

// ======================================================================
//  ค้นหาค่า CVT TEMP: ไล่ถามกล่องเกียร์ทีละ DID (Mode 22) / ทีละหน้า (Mode 21)
//  เก็บเฉพาะที่ตอบ → เฝ้าดูค่าตอนรถร้อนขึ้น → แตะไบต์ที่ขึ้นตามอุณหภูมิ = ใช้เป็น CVT TEMP
// ======================================================================
const _mono = [FontFeature.tabularFigures()];

class _Hit {
  _Hit(this.cmd, this.bytes) : lo = List.of(bytes), hi = List.of(bytes);
  final String cmd;
  List<int> bytes;
  final List<int> lo, hi;
  void update(List<int> b) {
    bytes = b;
    for (var i = 0; i < b.length && i < lo.length; i++) {
      if (b[i] < lo[i]) lo[i] = b[i];
      if (b[i] > hi[i]) hi[i] = b[i];
    }
  }
}

class CvtScanPage extends StatefulWidget {
  const CvtScanPage({super.key, required this.hub});
  final DataHub hub;
  @override
  State<CvtScanPage> createState() => _CvtScanPageState();
}

class _CvtScanPageState extends State<CvtScanPage> {
  static const _hdrs = [
    ('กล่องเกียร์ · Honda 29 บิต', '18DA1EF1'),
    ('กล่องเกียร์ 1D · Honda 29 บิต', '18DA1DF1'),
    ('กล่องเกียร์ · 11 บิต', '7E1'),
    ('เครื่องยนต์ · Honda 29 บิต', '18DA10F1'),
    ('เครื่องยนต์ · 11 บิต', '7E0'),
  ];
  static const _ranges22 = ['2200-22FF', '2300-23FF', '3000-30FF', '0000-00FF', '0100-01FF', '1000-10FF', '1100-11FF', '1F00-1FFF', '2000-20FF', 'F400-F4FF'];

  final _hdr = TextEditingController(text: '18DA1EF1');
  final _from = TextEditingController(text: '0000');
  final _to = TextEditingController(text: '00FF');
  int _mode = 0x22;
  bool _run = false, _watch = false;
  int _done = 0, _total = 0, _neg = 0, _none = 0;
  String _msg = '';
  final _hits = <_Hit>[];

  DataHub get hub => widget.hub;

  @override
  void dispose() {
    _run = false;
    _watch = false;
    super.dispose();
  }

  static List<int>? _bytes(String r) {
    if (r.isEmpty || r.length.isOdd || !RegExp(r'^[0-9A-F]+$').hasMatch(r)) return null;
    return [for (var i = 0; i < r.length; i += 2) int.parse(r.substring(i, i + 2), radix: 16)];
  }

  /// ถาม 1 คำสั่ง → ไบต์ข้อมูลหลัง echo · null = ไม่ตอบ · [] = ปฏิเสธ (7F)
  Future<List<int>?> _ask(int hdr, String cmd) async {
    final r = await hub.rawHdr(hdr, cmd);
    if (r == null) return null;
    final b = _bytes(r);
    if (b == null) return null;
    if (b.isNotEmpty && b[0] == 0x7F) return const [];
    final q = _bytes(cmd)!;
    if (b.length <= q.length || b[0] != q[0] + 0x40) return const [];
    for (var i = 1; i < q.length; i++) {
      if (b[i] != q[i]) return const [];
    }
    return b.sublist(q.length);
  }

  Future<void> _scan() async {
    final hdr = encodeHdr(_hdr.text);
    if (hdr == null) {
      _toast('Header ใช้ไม่ได้ — ใส่ 3 หลัก (7E1) หรือ 8 หลักแบบ 18DAxxF1');
      return;
    }
    if (!hub.canRawHdr) {
      _toast('ใช้ได้เมื่อเชื่อมต่อแบบ C3 เท่านั้น');
      return;
    }
    final a = int.tryParse(CustomPid.cleanHex(_from.text), radix: 16);
    final b = int.tryParse(CustomPid.cleanHex(_to.text), radix: 16);
    final max = _mode == 0x22 ? 0xFFFF : 0xFF;
    if (a == null || b == null || a > b || b > max) {
      _toast('ช่วงไม่ถูกต้อง');
      return;
    }
    setState(() {
      _run = true;
      _watch = false;
      _done = 0;
      _total = b - a + 1;
      _neg = 0;
      _none = 0;
      _msg = '';
    });
    var silentRun = 0;
    for (var id = a; id <= b && _run && mounted; id++) {
      final cmd = _mode == 0x22
          ? '22${id.toRadixString(16).padLeft(4, '0').toUpperCase()}'
          : '21${id.toRadixString(16).padLeft(2, '0').toUpperCase()}';
      final r = await _ask(hdr, cmd);
      if (!mounted) return;
      _done++;
      if (r == null) {
        _none++;
        silentRun++;
        if (silentRun >= 8 && _hits.isEmpty && _neg == 0) {
          _msg = 'กล่องนี้ไม่ตอบเลย — ลอง header อื่น (หรือรถยังไม่ติด / ยังไม่ต่อ C3)';
          break;
        }
      } else {
        silentRun = 0;
        if (r.isEmpty) {
          _neg++;
        } else {
          final i = _hits.indexWhere((h) => h.cmd == cmd && h.cmd.isNotEmpty);
          if (i >= 0) {
            _hits[i].update(r);
          } else {
            _hits.add(_Hit(cmd, r));
          }
        }
      }
      if (_done % 4 == 0 || r != null && r.isNotEmpty) setState(() {});
    }
    if (mounted) {
      setState(() {
        _run = false;
        if (_msg.isEmpty) _msg = 'เสร็จ: ตอบ ${_hits.length} · ปฏิเสธ $_neg · เงียบ $_none';
      });
    }
  }

  Future<void> _watchLoop() async {
    final hdr = encodeHdr(_hdr.text);
    if (hdr == null || _hits.isEmpty) return;
    setState(() => _watch = true);
    while (_watch && mounted) {
      for (final h in List.of(_hits)) {
        if (!_watch || !mounted) break;
        final r = await _ask(hdr, h.cmd);
        if (r != null && r.isNotEmpty) h.update(r);
      }
      if (mounted) setState(() {});
      await Future<void>.delayed(const Duration(milliseconds: 300));
    }
  }

  // ---------------- ลองค่าที่รู้จัก (Honda) ทีละตัว
  bool _known = false;
  final _knownRes = <(CvtPreset, String)>[];

  Future<void> _tryKnown() async {
    setState(() {
      _known = true;
      _knownRes.clear();
    });
    for (final p in kCvtPresets) {
      if (!mounted) return;
      final h = encodeHdr(p.hdr);
      String res;
      if (h == null) {
        res = 'header ผิด';
      } else {
        List<int>? r;
        for (var t = 0; t < 2 && (r == null || r.isEmpty); t++) {
          r = await _ask(h, p.req);
        }
        if (r == null) {
          res = 'ไม่ตอบ';
        } else if (r.isEmpty) {
          res = 'ปฏิเสธ (ไม่รองรับ)';
        } else if (r.length <= p.byte) {
          res = 'ตอบ ${r.length} ไบต์ (สั้นไป)';
        } else {
          final v = r[p.byte] - 40;
          res = v >= -30 && v <= 150 ? 'ได้ $v °C ✓' : 'ได้ $v °C (ค่าแปลก)';
        }
      }
      if (mounted) setState(() => _knownRes.add((p, res)));
    }
    if (mounted) setState(() => _known = false);
  }

  void _useKnown(CvtPreset p) {
    final s = hub.settings;
    s.cvtHdr = p.hdr;
    s.cvtReq = p.req;
    s.cvtByte = p.byte;
    s.cvtFml = p.fml;
    s.cvtTempOn = true;
    _toast('ตั้ง CVT TEMP = ${p.name} แล้ว');
  }

  void _toast(String m) => ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(m)));

  Future<void> _use(_Hit h, int byte) async {
    final v = h.bytes[byte];
    final ok = await showDialog<bool>(
      context: context,
      builder: (c) => AlertDialog(
        title: const Text('ใช้เป็น CVT TEMP?'),
        content: Text('Header ${CustomPid.cleanHex(_hdr.text)} · คำสั่ง ${h.cmd} · ไบต์ที่ $byte\n'
            'สูตร A − 40 → ตอนนี้ ${v - 40} °C\n(เปลี่ยนสูตรทีหลังได้ในหน้า CVT)'),
        actions: [
          TextButton(onPressed: () => Navigator.pop(c, false), child: const Text('ยกเลิก')),
          FilledButton(onPressed: () => Navigator.pop(c, true), child: const Text('ใช้ค่านี้')),
        ],
      ),
    );
    if (ok != true) return;
    final s = hub.settings;
    s.cvtHdr = _hdr.text;
    s.cvtReq = h.cmd;
    s.cvtByte = byte;
    s.cvtFml = 0;
    s.cvtTempOn = true;
    _watch = false;
    if (mounted) {
      _toast('ตั้ง CVT TEMP แล้ว');
      Navigator.pop(context);
    }
  }

  @override
  Widget build(BuildContext context) {
    final ect = hub.data.ect;
    return Scaffold(
      appBar: AppBar(title: const Text('ค้นหาค่า CVT TEMP')),
      body: ListView(padding: const EdgeInsets.fromLTRB(16, 8, 16, 24), children: [
        const Text(
            '1) เลือกกล่อง แล้วกด สแกน  2) ได้รายการที่ตอบแล้ว กด เฝ้าดู ขับไปสักพักให้เกียร์ร้อน\n'
            '3) ไบต์ไหนค่าขึ้นช้า ๆ ตามความร้อน (A−40 ใกล้อุณหภูมิน้ำ) = อุณหภูมิเกียร์ → แตะไบต์นั้น',
            style: TextStyle(fontSize: 13, color: Colors.white70)),
        const SizedBox(height: 10),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(12),
            child: Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
              const Text('ลองค่าที่รู้จักก่อน (Honda CAN: Jazz/City GK, Civic FC/FK, HR-V ...)',
                  style: TextStyle(fontWeight: FontWeight.w500)),
              const Text('ใช้เวลาไม่กี่วิ — ติดเครื่องไว้ ถ้าตัวไหนขึ้น ✓ และใกล้อุณหภูมิน้ำตอนรถร้อน แตะ "ใช้" ได้เลย',
                  style: TextStyle(fontSize: 12, color: Colors.white60)),
              const SizedBox(height: 6),
              Align(
                alignment: Alignment.centerLeft,
                child: FilledButton.tonal(
                  onPressed: _known || _run ? null : _tryKnown,
                  child: Text(_known ? 'กำลังลอง...' : 'ลองค่าที่รู้จัก'),
                ),
              ),
              for (final (p, res) in _knownRes)
                ListTile(
                  dense: true,
                  contentPadding: EdgeInsets.zero,
                  title: Text(p.name),
                  subtitle: Text('${p.hdr} · ${p.req} · ไบต์ ${p.byte} → $res'),
                  trailing: res.contains('°C') ? TextButton(onPressed: () => _useKnown(p), child: const Text('ใช้')) : null,
                ),
            ]),
          ),
        ),
        const SizedBox(height: 10),
        Wrap(spacing: 6, runSpacing: 6, children: [
          for (final h in _hdrs)
            ChoiceChip(
              label: Text(h.$1),
              selected: CustomPid.cleanHex(_hdr.text) == h.$2,
              onSelected: _run ? null : (_) => setState(() => _hdr.text = h.$2),
            ),
        ]),
        const SizedBox(height: 8),
        Row(children: [
          SizedBox(
            width: 150,
            child: TextField(
              controller: _hdr,
              enabled: !_run,
              decoration: const InputDecoration(labelText: 'Header', isDense: true),
              onChanged: (_) => setState(() {}),
            ),
          ),
          const SizedBox(width: 16),
          SegmentedButton<int>(
            segments: const [
              ButtonSegment(value: 0x22, label: Text('Mode 22')),
              ButtonSegment(value: 0x21, label: Text('Mode 21')),
            ],
            selected: {_mode},
            onSelectionChanged: _run
                ? null
                : (v) => setState(() {
                      _mode = v.first;
                      _from.text = _mode == 0x22 ? '0000' : '00';
                      _to.text = _mode == 0x22 ? '00FF' : 'FF';
                    }),
          ),
        ]),
        const SizedBox(height: 8),
        Row(children: [
          SizedBox(
              width: 90,
              child: TextField(
                  controller: _from,
                  enabled: !_run,
                  decoration: const InputDecoration(labelText: 'จาก', isDense: true))),
          const SizedBox(width: 10),
          SizedBox(
              width: 90,
              child: TextField(
                  controller: _to, enabled: !_run, decoration: const InputDecoration(labelText: 'ถึง', isDense: true))),
          const SizedBox(width: 10),
          if (_mode == 0x22)
            Expanded(
              child: Wrap(spacing: 4, runSpacing: 4, children: [
                for (final r in _ranges22)
                  ActionChip(
                    visualDensity: VisualDensity.compact,
                    label: Text(r, style: const TextStyle(fontSize: 12)),
                    onPressed: _run
                        ? null
                        : () => setState(() {
                              _from.text = r.substring(0, 4);
                              _to.text = r.substring(5);
                            }),
                  ),
              ]),
            ),
        ]),
        const SizedBox(height: 12),
        Wrap(spacing: 8, runSpacing: 8, crossAxisAlignment: WrapCrossAlignment.center, children: [
          _run
              ? FilledButton.icon(
                  style: FilledButton.styleFrom(backgroundColor: kRed),
                  onPressed: () => setState(() => _run = false),
                  icon: const Icon(Icons.stop),
                  label: const Text('หยุด'))
              : FilledButton.icon(
                  onPressed: _watch ? null : _scan, icon: const Icon(Icons.search), label: const Text('สแกน')),
          _watch
              ? FilledButton.icon(
                  style: FilledButton.styleFrom(backgroundColor: kRed),
                  onPressed: () => setState(() => _watch = false),
                  icon: const Icon(Icons.stop),
                  label: const Text('หยุดเฝ้าดู'))
              : OutlinedButton.icon(
                  onPressed: _run || _hits.isEmpty ? null : _watchLoop,
                  icon: const Icon(Icons.visibility),
                  label: const Text('เฝ้าดู')),
          TextButton(
              onPressed: _run || _watch || _hits.isEmpty ? null : () => setState(_hits.clear),
              child: const Text('ล้างรายการ')),
          Text('น้ำ (ECT) ตอนนี้ ${ect == null ? '--' : ect.toStringAsFixed(0)} °C',
              style: const TextStyle(color: kAmber, fontWeight: FontWeight.w700)),
        ]),
        if (_run || _total > 0) ...[
          const SizedBox(height: 10),
          LinearProgressIndicator(value: _total == 0 ? 0 : _done / _total),
          const SizedBox(height: 4),
          Text('$_done / $_total · ตอบ ${_hits.length} · ปฏิเสธ $_neg · เงียบ $_none',
              style: const TextStyle(fontSize: 12, color: kGrey)),
        ],
        if (_msg.isNotEmpty)
          Padding(
              padding: const EdgeInsets.only(top: 6),
              child: Text(_msg, style: const TextStyle(color: kAccent, fontWeight: FontWeight.w700))),
        const SizedBox(height: 10),
        for (final h in _hits) _hitRow(h),
        if (_hits.isNotEmpty)
          const Padding(
            padding: EdgeInsets.only(top: 8),
            child: Text(
                'ตัวเลขแต่ละช่อง = ไบต์ (ฐาน 10) · บรรทัดล่าง = ถ้าใช้สูตร A−40 (°C) · สีส้ม = ค่าเคยเปลี่ยน (ต่ำสุด–สูงสุด)\n'
                'ระหว่างสแกน/เฝ้าดู ค่าหลักบนหน้าปัดจะช้าลงชั่วคราว (C3 ถามรถได้ทีละคำถาม)',
                style: TextStyle(fontSize: 11, color: Colors.white38)),
          ),
      ]),
    );
  }

  Widget _hitRow(_Hit h) {
    final n = h.bytes.length.clamp(0, 16);
    return Container(
      padding: const EdgeInsets.symmetric(vertical: 6),
      decoration: const BoxDecoration(border: Border(bottom: BorderSide(color: Color(0xFF262626)))),
      child: Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
        SizedBox(
            width: 80,
            child: Text(h.cmd, style: const TextStyle(fontWeight: FontWeight.w800, fontFeatures: _mono))),
        Expanded(
          child: Wrap(spacing: 4, runSpacing: 4, children: [
            for (var i = 0; i < n; i++)
              InkWell(
                onTap: () => _use(h, i),
                borderRadius: BorderRadius.circular(6),
                child: Container(
                  width: 54,
                  padding: const EdgeInsets.symmetric(vertical: 3),
                  decoration: BoxDecoration(
                    border: Border.all(color: h.lo[i] != h.hi[i] ? kAccent : const Color(0xFF333333)),
                    borderRadius: BorderRadius.circular(6),
                  ),
                  child: Column(children: [
                    Text('${h.bytes[i]}',
                        style: TextStyle(
                            fontSize: 15,
                            fontWeight: FontWeight.w800,
                            color: h.lo[i] != h.hi[i] ? kAccent : Colors.white,
                            fontFeatures: _mono)),
                    Text('${h.bytes[i] - 40}°',
                        style: const TextStyle(fontSize: 11, color: kGrey, fontFeatures: _mono)),
                    if (h.lo[i] != h.hi[i])
                      Text('${h.lo[i]}–${h.hi[i]}', style: const TextStyle(fontSize: 9, color: kAccent)),
                  ]),
                ),
              ),
          ]),
        ),
      ]),
    );
  }
}
