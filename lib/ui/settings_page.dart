import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:image_picker/image_picker.dart';
import 'package:path_provider/path_provider.dart';

import '../data/car_data.dart';
import '../data/elm_source.dart';
import '../data/hub.dart';
import '../data/native.dart';
import '../settings.dart';
import 'esp32_pics_ui.dart';
import 'paint_utils.dart';

void _snack(BuildContext context, String msg) {
  ScaffoldMessenger.of(context)
    ..hideCurrentSnackBar()
    ..showSnackBar(SnackBar(content: Text(msg)));
}

class SettingsPage extends StatelessWidget {
  const SettingsPage({super.key, required this.settings, required this.hub});
  final AppSettings settings;
  final DataHub hub;

  @override
  Widget build(BuildContext context) {
    return DefaultTabController(
      length: 7,
      child: Scaffold(
        appBar: AppBar(
          toolbarHeight: 40,
          title: const Text('MENU', style: TextStyle(fontSize: 16)),
          bottom: const TabBar(
            isScrollable: true,
            tabs: [
              Tab(height: 36, text: 'การเชื่อมต่อ'),
              Tab(height: 36, text: 'หน้าปัด'),
              Tab(height: 36, text: 'เกียร์'),
              Tab(height: 36, text: 'รูปพื้นหลัง'),
              Tab(height: 36, text: 'DTC'),
              Tab(height: 36, text: 'เปิดอัตโนมัติ'),
              Tab(height: 36, text: 'จอ ESP32'),
            ],
          ),
        ),
        body: TabBarView(children: [
          ConnectionTab(settings: settings, hub: hub),
          DisplayTab(settings: settings, hub: hub),
          GearTab(settings: settings, hub: hub),
          PicturesTab(settings: settings, hub: hub),
          DtcTab(hub: hub),
          AutoStartTab(settings: settings),
          Esp32Tab(settings: settings, hub: hub),
        ]),
      ),
    );
  }
}

// ======================================================================
class _Hint extends StatelessWidget {
  const _Hint(this.text);
  final String text;
  @override
  Widget build(BuildContext context) => Padding(
        padding: const EdgeInsets.fromLTRB(16, 2, 16, 8),
        child: Text(text,
            style: const TextStyle(fontSize: 12, color: Colors.white54)),
      );
}

class _Header extends StatelessWidget {
  const _Header(this.text);
  final String text;
  @override
  Widget build(BuildContext context) => Padding(
        padding: const EdgeInsets.fromLTRB(16, 12, 16, 4),
        child: Text(text,
            style: const TextStyle(
                fontSize: 13, color: kAccent, fontWeight: FontWeight.w700)),
      );
}

// ======================================================================
//  การเชื่อมต่อ
// ======================================================================
class ConnectionTab extends StatefulWidget {
  const ConnectionTab({super.key, required this.settings, required this.hub});
  final AppSettings settings;
  final DataHub hub;
  @override
  State<ConnectionTab> createState() => _ConnectionTabState();
}

class _ConnectionTabState extends State<ConnectionTab> {
  late final _elmH = TextEditingController(text: widget.settings.elmHost);
  late final _elmP =
      TextEditingController(text: '${widget.settings.elmPort}');
  late final _espH = TextEditingController(text: widget.settings.espHost);
  late final _espP =
      TextEditingController(text: '${widget.settings.espPort}');

  @override
  void dispose() {
    _elmH.dispose();
    _elmP.dispose();
    _espH.dispose();
    _espP.dispose();
    super.dispose();
  }

  static String _title(SourceType t) => switch (t) {
        SourceType.demo => 'จำลอง (Demo)',
        SourceType.btElm => 'ELM327 Bluetooth (Vgate iCar Pro)',
        SourceType.wifiElm => 'ELM327 WiFi',
        SourceType.esp32 => 'ESP32 ผ่าน WiFi',
      };

  static String _sub(SourceType t) => switch (t) {
        SourceType.demo => 'ทดสอบหน้าจอโดยไม่ต้องต่อรถ',
        SourceType.btElm => 'มือถือต่อ OBD2 เอง ไม่ต้องใช้ ESP32',
        SourceType.wifiElm => 'ตัว OBD2 แบบปล่อย WiFi',
        SourceType.esp32 => 'ESP32 อ่าน OBD2 แล้วส่งค่าต่อมาที่มือถือ (โชว์พร้อมจอ CYD)',
      };

  Future<void> _pickBt() async {
    try {
      final list = await BtTransport.bonded();
      if (!mounted) return;
      final picked = await showDialog<BtDevice>(
        context: context,
        builder: (ctx) => SimpleDialog(
          title: const Text('เลือกอุปกรณ์ OBD2'),
          children: [
            if (list.isEmpty)
              const Padding(
                padding: EdgeInsets.all(16),
                child: Text('ไม่พบอุปกรณ์ที่จับคู่ไว้ — ไปจับคู่ในตั้งค่า Bluetooth ของมือถือก่อน'),
              ),
            for (final d in list)
              SimpleDialogOption(
                onPressed: () => Navigator.pop(ctx, d),
                child: Text('${d.name}\n${d.address}'),
              ),
          ],
        ),
      );
      if (picked != null) {
        widget.settings.setBtDevice(picked.name, picked.address);
      }
    } catch (e) {
      if (mounted) _snack(context, errText(e));
    }
  }

  Widget _hostPort(TextEditingController h, TextEditingController p,
      void Function(String, int) save) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 16),
      child: Row(children: [
        Expanded(
          child: TextField(
            controller: h,
            decoration: const InputDecoration(labelText: 'IP', isDense: true),
            keyboardType: TextInputType.url,
          ),
        ),
        const SizedBox(width: 12),
        SizedBox(
          width: 90,
          child: TextField(
            controller: p,
            decoration: const InputDecoration(labelText: 'Port', isDense: true),
            keyboardType: TextInputType.number,
          ),
        ),
        const SizedBox(width: 12),
        FilledButton(
          onPressed: () {
            final port = int.tryParse(p.text.trim());
            if (port == null || h.text.trim().isEmpty) {
              _snack(context, 'IP หรือ Port ไม่ถูกต้อง');
              return;
            }
            FocusScope.of(context).unfocus();
            save(h.text.trim(), port);
            _snack(context, 'บันทึกแล้ว กำลังเชื่อมต่อ...');
          },
          child: const Text('บันทึก'),
        ),
      ]),
    );
  }

  @override
  Widget build(BuildContext context) {
    final s = widget.settings;
    final hub = widget.hub;
    return AnimatedBuilder(
      animation: Listenable.merge([s, hub]),
      builder: (context, _) {
        final stCol = switch (hub.state) {
          LinkState.connected => kGreen,
          LinkState.connecting => kAmber,
          LinkState.error => kRed,
          LinkState.idle => kGrey,
        };
        return ListView(
          padding: const EdgeInsets.only(bottom: 24),
          children: [
            ListTile(
              leading: Icon(Icons.circle, color: stCol, size: 14),
              title: Text(hub.message.isEmpty ? '-' : hub.message),
              trailing: FilledButton.tonal(
                onPressed: () => hub.restart(),
                child: const Text('เชื่อมต่อใหม่'),
              ),
            ),
            const _Header('แหล่งข้อมูล'),
            for (final t in SourceType.values)
              RadioListTile<SourceType>(
                dense: true,
                value: t,
                groupValue: s.source,
                onChanged: (v) {
                  if (v != null) s.source = v;
                },
                title: Text(_title(t)),
                subtitle: Text(_sub(t)),
              ),
            if (s.source == SourceType.btElm) ...[
              const _Header('อุปกรณ์ Bluetooth'),
              ListTile(
                leading: const Icon(Icons.bluetooth),
                title: Text(s.btName.isEmpty ? 'ยังไม่ได้เลือกอุปกรณ์' : s.btName),
                subtitle: Text(s.btAddress.isEmpty
                    ? 'เลือกจากอุปกรณ์ที่จับคู่ไว้'
                    : s.btAddress),
                trailing: FilledButton(
                    onPressed: _pickBt, child: const Text('เลือก')),
              ),
              const _Hint(
                  'จับคู่ (Pair) Vgate iCar Pro ในหน้าตั้งค่า Bluetooth ของมือถือก่อน — PIN มักเป็น 1234 หรือ 0000\n'
                  'Vgate ต่อได้ทีละเครื่อง: ถ้า บอร์ด ESP32 ต่ออยู่ มือถือจะต่อไม่ได้ (ใช้โหมด ESP32 แทน)'),
            ],
            if (s.source == SourceType.wifiElm) ...[
              const _Header('ELM327 WiFi'),
              _hostPort(_elmH, _elmP, s.setElm),
              const _Hint('ต่อ WiFi ของตัว OBD2 ก่อน (ค่าทั่วไป 192.168.0.10 : 35000)'),
            ],
            if (s.source == SourceType.esp32) ...[
              const _Header('ESP32 (บอร์ดจอ)'),
              _hostPort(_espH, _espP, s.setEsp),
              const _Hint(
                  'ให้มือถือต่อ WiFi "Didissnap" (รหัส 12345678) ที่ ESP32 ปล่อยออกมา\n'
                  'ถ้ามือถือถามว่า "WiFi นี้ไม่มีอินเทอร์เน็ต" ให้กด "ใช้ต่อ/คงการเชื่อมต่อ"'),
            ],
          ],
        );
      },
    );
  }
}

// ======================================================================
//  หน้าปัด
// ======================================================================
class _StepRow extends StatelessWidget {
  const _StepRow(this.label, this.value, this.min, this.max, this.step,
      this.onChanged,
      {this.unit = ''});
  final String label, unit;
  final int value, min, max, step;
  final ValueChanged<int> onChanged;

  @override
  Widget build(BuildContext context) {
    return ListTile(
      dense: true,
      title: Text(label, style: const TextStyle(fontSize: 14)),
      trailing: Row(mainAxisSize: MainAxisSize.min, children: [
        IconButton(
          icon: const Icon(Icons.remove_circle_outline),
          onPressed: value - step >= min ? () => onChanged(value - step) : null,
        ),
        SizedBox(
          width: 90,
          child: Text('$value$unit',
              textAlign: TextAlign.center,
              style:
                  const TextStyle(fontSize: 17, fontWeight: FontWeight.w700)),
        ),
        IconButton(
          icon: const Icon(Icons.add_circle_outline),
          onPressed: value + step <= max ? () => onChanged(value + step) : null,
        ),
      ]),
    );
  }
}

class DisplayTab extends StatelessWidget {
  const DisplayTab({super.key, required this.settings, required this.hub});
  final AppSettings settings;
  final DataHub hub;

  @override
  Widget build(BuildContext context) {
    final s = settings;
    return AnimatedBuilder(
      animation: s,
      builder: (context, _) => ListView(
        padding: const EdgeInsets.only(bottom: 24),
        children: [
          const _Header('รอบเครื่อง'),
          _StepRow('รอบสูงสุดของหน้าปัด', s.maxRpm, 3000, 12000, 500, (v) {
            s.maxRpm = v;
            if (s.redline > v) s.redline = v;
            if (s.shiftStart >= s.redline) s.shiftStart = s.redline - 500;
          }),
          _StepRow('Redline (กะพริบเตือน)', s.redline, s.shiftStart + 100,
              s.maxRpm, 100, (v) => s.redline = v),
          _StepRow('ไฟ Shift เริ่มติดที่', s.shiftStart, 1000, s.redline - 100,
              100, (v) => s.shiftStart = v),
          const _Header('อื่น ๆ'),
          _StepRow('ความเร็วสูงสุด', s.maxSpeed, 120, 360, 20,
              (v) => s.maxSpeed = v,
              unit: ' km/h'),
          _StepRow('เตือนความร้อนน้ำที่', s.ectWarn, 85, 125, 1,
              (v) => s.ectWarn = v,
              unit: ' °C'),
          SwitchListTile(
            dense: true,
            title: const Text('เปิดหน้าจอค้างไว้ตลอด'),
            value: s.keepScreenOn,
            onChanged: (v) => s.keepScreenOn = v,
          ),
          SwitchListTile(
            dense: true,
            title: const Text('กลับจอ 180° (FLIP)'),
            subtitle: const Text('ใช้เมื่อติดมือถือกลับหัวกับขาจับ'),
            value: s.flip,
            onChanged: (v) => s.flip = v,
          ),
          const _Header('คลิปตอนเปิดแอป'),
          SwitchListTile(
            dense: true,
            title: const Text('เล่นคลิปก่อนเข้าหน้าปัด'),
            subtitle: const Text('เล่นจนจบ → จอดำ → เข้าหน้าปัด (แตะจอเพื่อข้าม)'),
            value: s.bootClip,
            onChanged: (v) => s.bootClip = v,
          ),
          SwitchListTile(
            dense: true,
            title: const Text('เปิดเสียงคลิป'),
            value: s.bootSound,
            onChanged: s.bootClip ? (v) => s.bootSound = v : null,
          ),
          ListTile(
            dense: true,
            title: const Text('TRIP (ระยะทางที่คำนวณจากความเร็ว)'),
            subtitle: Text('${hub.data.trip.toStringAsFixed(1)} km'),
            trailing: FilledButton.tonal(
              onPressed: () {
                hub.data.trip = 0;
                _snack(context, 'รีเซ็ต TRIP แล้ว');
              },
              child: const Text('รีเซ็ต'),
            ),
          ),
          const _Header('หน้าที่แสดง (ปัดซ้าย/ขวา)'),
          for (final p in kPages)
            SwitchListTile(
              dense: true,
              title: Text(p.$2),
              value: s.pages.contains(p.$1),
              onChanged: (on) {
                final cur = s.pages;
                if (!on && cur.length <= 1) {
                  _snack(context, 'ต้องเปิดไว้อย่างน้อย 1 หน้า');
                  return;
                }
                s.pages = kPages
                    .map((e) => e.$1)
                    .where((id) => id == p.$1 ? on : cur.contains(id))
                    .toList();
              },
            ),
        ],
      ),
    );
  }
}

// ======================================================================
//  เกียร์
// ======================================================================
class GearTab extends StatefulWidget {
  const GearTab({super.key, required this.settings, required this.hub});
  final AppSettings settings;
  final DataHub hub;
  @override
  State<GearTab> createState() => _GearTabState();
}

class _GearTabState extends State<GearTab> {
  Timer? _t;

  @override
  void initState() {
    super.initState();
    _t = Timer.periodic(const Duration(milliseconds: 250), (_) {
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
    final s = widget.settings;
    final d = widget.hub.data;
    final ratio = (d.rpm > 400 && d.speed > 2) ? d.speed / (d.rpm / 1000) : null;
    final gears = s.gearRatios;
    void save(List<double> g) {
      s.gearRatios = g;
      setState(() {});
    }

    return ListView(
      padding: const EdgeInsets.only(bottom: 24),
      children: [
        const _Hint(
            'แอปเดาเกียร์จาก "ความเร็ว ÷ รอบ" — ขับคงที่ในแต่ละเกียร์ (ไม่เหยียบคลัตช์) แล้วกด "จำค่า" ของเกียร์นั้น'),
        ListTile(
          dense: true,
          leading: const Icon(Icons.speed),
          title: Text(
              'ตอนนี้: ${fmt(d.rpm)} rpm · ${fmt(d.speed)} km/h · '
              'อัตรา ${ratio == null ? '--' : ratio.toStringAsFixed(2)} · '
              'เกียร์ ${estimateGear(d.rpm, d.speed, gears)}',
              style: const TextStyle(fontSize: 15, fontWeight: FontWeight.w600)),
        ),
        for (var i = 0; i < gears.length; i++)
          ListTile(
            dense: true,
            title: Text('เกียร์ ${i + 1}'),
            subtitle: Text('${gears[i].toStringAsFixed(2)} km/h ต่อ 1000 rpm'),
            trailing: Row(mainAxisSize: MainAxisSize.min, children: [
              IconButton(
                icon: const Icon(Icons.remove),
                onPressed: () {
                  final g = List<double>.of(gears);
                  g[i] = g[i] - 0.1;
                  save(g);
                },
              ),
              IconButton(
                icon: const Icon(Icons.add),
                onPressed: () {
                  final g = List<double>.of(gears);
                  g[i] = g[i] + 0.1;
                  save(g);
                },
              ),
              FilledButton.tonal(
                onPressed: ratio == null
                    ? null
                    : () {
                        final g = List<double>.of(gears);
                        g[i] = ratio;
                        save(g);
                      },
                child: const Text('จำค่า'),
              ),
            ]),
          ),
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 12),
          child: Wrap(spacing: 8, children: [
            TextButton(
              onPressed: gears.length >= 7
                  ? null
                  : () => save([...gears, gears.last + 6]),
              child: const Text('เพิ่มเกียร์'),
            ),
            TextButton(
              onPressed: gears.length <= 3
                  ? null
                  : () => save(gears.sublist(0, gears.length - 1)),
              child: const Text('ลบเกียร์สุดท้าย'),
            ),
            TextButton(
              onPressed: () {
                s.resetGears();
                setState(() {});
              },
              child: const Text('คืนค่าเริ่มต้น'),
            ),
          ]),
        ),
      ],
    );
  }
}

// ======================================================================
//  รูปพื้นหลัง
// ======================================================================
class PicturesTab extends StatefulWidget {
  const PicturesTab({super.key, required this.settings, required this.hub});
  final AppSettings settings;
  final DataHub hub;
  @override
  State<PicturesTab> createState() => _PicturesTabState();
}

class _PicturesTabState extends State<PicturesTab> {
  bool _busy = false;
  Timer? _t;
  String? _showing;

  @override
  void initState() {
    super.initState();
    // อัปเดตป้าย "กำลังแสดง" ตามรอบปัจจุบัน
    _t = Timer.periodic(const Duration(milliseconds: 300), (_) {
      final now = widget.settings.pictureFor(widget.hub.data.rpm);
      if (now != _showing && mounted) setState(() => _showing = now);
    });
  }

  @override
  void dispose() {
    _t?.cancel();
    super.dispose();
  }

  Future<void> _add() async {
    setState(() => _busy = true);
    try {
      final files = await ImagePicker().pickMultiImage();
      if (files.isEmpty) return;
      final docs = await getApplicationDocumentsDirectory();
      final dir = Directory('${docs.path}/backgrounds');
      await dir.create(recursive: true);
      final list = List<String>.of(widget.settings.pictures);
      var n = 0;
      for (final f in files) {
        final dot = f.name.lastIndexOf('.');
        final ext = dot >= 0 ? f.name.substring(dot).toLowerCase() : '.jpg';
        final dst =
            '${dir.path}/bg_${DateTime.now().microsecondsSinceEpoch}_$n$ext';
        n++;
        await File(f.path).copy(dst);
        list.add(dst);
      }
      widget.settings.pictures = list;
    } catch (e) {
      if (mounted) _snack(context, 'เพิ่มรูปไม่ได้: ${errText(e)}');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  void _remove(String p) {
    widget.settings.pictures =
        widget.settings.pictures.where((e) => e != p).toList();
    try {
      File(p).deleteSync();
    } catch (_) {}
  }

  void _clear() {
    final list = widget.settings.pictures;
    widget.settings.pictures = const [];
    for (final p in list) {
      try {
        File(p).deleteSync();
      } catch (_) {}
    }
  }

  Future<void> _typeRpm(String path) async {
    final ctl =
        TextEditingController(text: '${widget.settings.picRpm(path)}');
    final v = await showDialog<int>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('รูปนี้เริ่มแสดงที่รอบ'),
        content: TextField(
          controller: ctl,
          autofocus: true,
          keyboardType: TextInputType.number,
          decoration: const InputDecoration(suffixText: 'rpm'),
        ),
        actions: [
          TextButton(
              onPressed: () => Navigator.pop(ctx),
              child: const Text('ยกเลิก')),
          FilledButton(
              onPressed: () => Navigator.pop(ctx, int.tryParse(ctl.text.trim())),
              child: const Text('ตกลง')),
        ],
      ),
    );
    ctl.dispose();
    if (v != null) widget.settings.setPic(path, rpm: v);
  }

  @override
  Widget build(BuildContext context) {
    final s = widget.settings;
    return AnimatedBuilder(
      animation: s,
      builder: (context, _) {
        final pics = s.pictures;
        final active = s.activePictures();
        // ข้อความช่วงรอบของแต่ละรูปที่เปิดอยู่
        final ranges = <String, String>{};
        for (var i = 0; i < active.length; i++) {
          final from = i == 0 ? 0 : active[i].$2;
          ranges[active[i].$1] = active.length == 1
              ? 'แสดงตลอด (ภาพนิ่ง/GIF)'
              : (i == active.length - 1
                  ? '$from rpm ขึ้นไป'
                  : '$from – ${active[i + 1].$2 - 1} rpm');
        }
        return Column(children: [
          Padding(
            padding: const EdgeInsets.fromLTRB(12, 8, 12, 0),
            child: Row(children: [
              FilledButton.icon(
                onPressed: _busy ? null : _add,
                icon: const Icon(Icons.add_photo_alternate_outlined),
                label: const Text('เพิ่มรูป'),
              ),
              const SizedBox(width: 8),
              if (pics.isNotEmpty)
                TextButton(onPressed: _clear, child: const Text('ลบทั้งหมด')),
              const SizedBox(width: 8),
              const Expanded(
                child: Text(
                  'เปิด/ปิดสวิตช์ = เลือกรูปที่จะแสดง · เปิดไว้รูปเดียว = แสดงตลอด · กดค้างแล้วลากเพื่อเรียงลำดับ · รองรับ GIF',
                  style: TextStyle(fontSize: 11, color: Colors.white54),
                ),
              ),
            ]),
          ),
          SwitchListTile(
            dense: true,
            title: const Text('ฟอนต์พิกเซลแบบเกม 16-bit'),
            subtitle: const Text('ตัวเลข/ตัวหนังสือ และแถบรอบบนหน้ารูปพื้นหลัง'),
            value: s.pixelFont,
            onChanged: (v) => s.pixelFont = v,
          ),
          SwitchListTile(
            dense: true,
            title: const Text('แบ่งช่วงรอบอัตโนมัติ (เท่า ๆ กันถึง redline)'),
            subtitle: Text(s.picAuto
                ? 'ปิดเพื่อกำหนดเองว่าแต่ละรูปเริ่มแสดงที่กี่รอบ'
                : 'กำหนดเอง: แตะตัวเลขเพื่อพิมพ์รอบ หรือกด − / + (ทีละ 100)'),
            value: s.picAuto,
            onChanged: (v) => s.picAuto = v,
          ),
          if (_busy) const LinearProgressIndicator(),
          Expanded(
            child: ReorderableListView.builder(
              itemCount: pics.length,
              onReorder: (from, to) {
                final l = List<String>.of(pics);
                var t = to;
                if (t > from) t--;
                final it = l.removeAt(from);
                l.insert(t, it);
                s.pictures = l;
              },
              itemBuilder: (context, i) {
                final p = pics[i];
                final on = s.picOn(p);
                final showing = p == _showing;
                return ListTile(
                  key: ValueKey(p),
                  dense: true,
                  leading: Opacity(
                    opacity: on ? 1 : 0.35,
                    child: Container(
                      decoration: BoxDecoration(
                        border: Border.all(
                            color: showing ? kAccent : Colors.transparent,
                            width: 2),
                        borderRadius: BorderRadius.circular(6),
                      ),
                      child: ClipRRect(
                        borderRadius: BorderRadius.circular(4),
                        child: Image.file(File(p),
                            width: 80,
                            height: 44,
                            fit: BoxFit.cover,
                            cacheWidth: 160,
                            errorBuilder: (_, __, ___) =>
                                const SizedBox(width: 80, height: 44)),
                      ),
                    ),
                  ),
                  title: Text(
                      'รูป ${i + 1}${showing ? '  · กำลังแสดง' : ''}',
                      style: TextStyle(color: showing ? kAccent : null)),
                  subtitle: Text(on ? (ranges[p] ?? '') : 'ไม่แสดง'),
                  trailing: Row(mainAxisSize: MainAxisSize.min, children: [
                    if (!s.picAuto && on) ...[
                      IconButton(
                        icon: const Icon(Icons.remove_circle_outline),
                        onPressed: () =>
                            s.setPic(p, rpm: s.picRpm(p) - 100),
                      ),
                      InkWell(
                        onTap: () => _typeRpm(p),
                        child: Container(
                          width: 92,
                          padding: const EdgeInsets.symmetric(vertical: 6),
                          decoration: BoxDecoration(
                            border: Border.all(color: Colors.white24),
                            borderRadius: BorderRadius.circular(6),
                          ),
                          child: Text('${s.picRpm(p)}',
                              textAlign: TextAlign.center,
                              style: const TextStyle(
                                  fontSize: 16, fontWeight: FontWeight.w700)),
                        ),
                      ),
                      IconButton(
                        icon: const Icon(Icons.add_circle_outline),
                        onPressed: () =>
                            s.setPic(p, rpm: s.picRpm(p) + 100),
                      ),
                    ],
                    Switch(value: on, onChanged: (v) => s.setPic(p, on: v)),
                    IconButton(
                      icon: const Icon(Icons.delete_outline),
                      onPressed: () => _remove(p),
                    ),
                  ]),
                );
              },
            ),
          ),
        ]);
      },
    );
  }
}

// ======================================================================
//  DTC
// ======================================================================
const _dtcInfo = <String, String>{
  'P0101': 'MAF ค่าผิดปกติ',
  'P0106': 'MAP ค่าผิดปกติ',
  'P0113': 'เซ็นเซอร์ IAT สัญญาณสูง',
  'P0118': 'เซ็นเซอร์อุณหภูมิน้ำ สัญญาณสูง',
  'P0128': 'เครื่องร้อนช้า (เทอร์โมสตัท)',
  'P0133': 'ออกซิเจนเซ็นเซอร์ตอบสนองช้า',
  'P0141': 'ฮีตเตอร์ออกซิเจนเซ็นเซอร์ตัวหลัง',
  'P0171': 'ส่วนผสมบาง',
  'P0172': 'ส่วนผสมหนา',
  'P0300': 'จุดระเบิดผิดพลาดหลายสูบ',
  'P0301': 'จุดระเบิดผิดพลาดสูบ 1',
  'P0302': 'จุดระเบิดผิดพลาดสูบ 2',
  'P0303': 'จุดระเบิดผิดพลาดสูบ 3',
  'P0304': 'จุดระเบิดผิดพลาดสูบ 4',
  'P0335': 'เซ็นเซอร์ข้อเหวี่ยง (CKP)',
  'P0340': 'เซ็นเซอร์แคมชาฟท์ (CMP)',
  'P0420': 'ประสิทธิภาพแคทาไลติกต่ำ',
  'P0455': 'ระบบไอระเหย (EVAP) รั่วมาก',
  'P0500': 'เซ็นเซอร์ความเร็วรถ',
  'P0505': 'ระบบควบคุมรอบเดินเบา',
};

String _dtcDesc(String c) {
  final k = _dtcInfo[c];
  if (k != null) return k;
  if (c.length < 2) return '';
  final sys = switch (c[0]) {
    'P' => 'เครื่องยนต์/เกียร์',
    'C' => 'ช่วงล่าง/เบรก',
    'B' => 'ตัวถัง/ไฟฟ้า',
    _ => 'ระบบสื่อสาร',
  };
  return '$sys · ${c[1] == '0' ? 'รหัสมาตรฐาน' : 'รหัสเฉพาะยี่ห้อ'}';
}

class DtcTab extends StatefulWidget {
  const DtcTab({super.key, required this.hub});
  final DataHub hub;
  @override
  State<DtcTab> createState() => _DtcTabState();
}

class _DtcTabState extends State<DtcTab> {
  List<String>? _codes;
  String? _err;
  bool _busy = false;

  Future<void> _read() async {
    setState(() {
      _busy = true;
      _err = null;
    });
    try {
      final c = await widget.hub.readDtc();
      if (mounted) setState(() => _codes = c);
    } catch (e) {
      if (mounted) setState(() => _err = errText(e));
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _clear() async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('ลบรหัส DTC?'),
        content: const Text(
            'จะล้างรหัสและดับไฟ Check Engine (ข้อมูล Readiness จะถูกรีเซ็ตด้วย)\nแนะนำให้ดับเครื่อง แต่เปิดสวิตช์กุญแจค้างไว้ที่ ON'),
        actions: [
          TextButton(
              onPressed: () => Navigator.pop(ctx, false),
              child: const Text('ยกเลิก')),
          FilledButton(
              onPressed: () => Navigator.pop(ctx, true),
              child: const Text('ลบ')),
        ],
      ),
    );
    if (ok != true || !mounted) return;
    setState(() {
      _busy = true;
      _err = null;
    });
    try {
      final done = await widget.hub.clearDtc();
      if (!mounted) return;
      _snack(context, done ? 'ลบรหัส DTC แล้ว' : 'ECU ไม่ยืนยันการลบ');
      if (done) setState(() => _codes = []);
    } catch (e) {
      if (mounted) setState(() => _err = errText(e));
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final codes = _codes;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Padding(
          padding: const EdgeInsets.all(12),
          child: Row(children: [
            FilledButton.icon(
              onPressed: _busy ? null : _read,
              icon: const Icon(Icons.search),
              label: const Text('อ่าน DTC'),
            ),
            const SizedBox(width: 12),
            OutlinedButton.icon(
              onPressed: _busy ? null : _clear,
              icon: const Icon(Icons.delete_sweep_outlined),
              label: const Text('ลบ DTC'),
            ),
          ]),
        ),
        if (_busy) const LinearProgressIndicator(),
        if (_err != null)
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
            child: Text(_err!, style: const TextStyle(color: kRed)),
          ),
        if (codes != null && codes.isEmpty)
          const Padding(
            padding: EdgeInsets.all(16),
            child: Text('ไม่พบรหัส DTC ✓',
                style: TextStyle(fontSize: 18, color: kGreen)),
          ),
        if (codes != null && codes.isNotEmpty)
          Expanded(
            child: ListView(
              children: [
                for (final c in codes)
                  ListTile(
                    leading: const Icon(Icons.warning_amber_rounded,
                        color: kAmber),
                    title: Text(c,
                        style: const TextStyle(
                            fontSize: 20, fontWeight: FontWeight.w800)),
                    subtitle: Text(_dtcDesc(c)),
                  ),
              ],
            ),
          ),
      ],
    );
  }
}

// ======================================================================
//  เปิดแอปอัตโนมัติ
// ======================================================================
Future<BtDevice?> pickBondedDevice(BuildContext context, String title) async {
  final list = await BtTransport.bonded();
  if (!context.mounted) return null;
  return showDialog<BtDevice>(
    context: context,
    builder: (ctx) => SimpleDialog(
      title: Text(title),
      children: [
        if (list.isEmpty)
          const Padding(
            padding: EdgeInsets.all(16),
            child: Text('ไม่พบอุปกรณ์ที่จับคู่ไว้'),
          ),
        for (final d in list)
          SimpleDialogOption(
            onPressed: () => Navigator.pop(ctx, d),
            child: Text('${d.name}\n${d.address}'),
          ),
      ],
    ),
  );
}

class AutoStartTab extends StatefulWidget {
  const AutoStartTab({super.key, required this.settings});
  final AppSettings settings;
  @override
  State<AutoStartTab> createState() => _AutoStartTabState();
}

class _AutoStartTabState extends State<AutoStartTab>
    with WidgetsBindingObserver {
  bool? _overlay;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    _check();
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    super.dispose();
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    // กลับมาจากหน้าตั้งค่าของ Android → เช็กสิทธิ์ใหม่
    if (state == AppLifecycleState.resumed) _check();
  }

  Future<void> _check() async {
    final ok = await Native.canOverlay();
    if (mounted) setState(() => _overlay = ok);
  }

  bool get _anyOn =>
      widget.settings.autoBoot ||
      widget.settings.autoPower ||
      widget.settings.autoBt;

  Future<void> _toggle(void Function() apply, bool turningOn) async {
    apply();
    await Native.syncAutoStart();
    if (turningOn && _overlay == false && mounted) {
      _snack(context, 'อย่าลืมกด "อนุญาต" สิทธิ์แสดงทับแอปอื่น ด้านบน');
    }
  }

  Future<void> _pickCarBt() async {
    try {
      final d = await pickBondedDevice(context, 'เลือก Bluetooth ของรถ');
      if (d != null) widget.settings.setAutoBtDevice(d.name, d.address);
    } catch (e) {
      if (mounted) _snack(context, errText(e));
    }
  }

  @override
  Widget build(BuildContext context) {
    final s = widget.settings;
    return AnimatedBuilder(
      animation: s,
      builder: (context, _) => ListView(
        padding: const EdgeInsets.only(bottom: 24),
        children: [
          ListTile(
            leading: Icon(
              _overlay == true ? Icons.check_circle : Icons.error_outline,
              color: _overlay == true ? kGreen : kAmber,
            ),
            title: const Text('สิทธิ์ "แสดงทับแอปอื่น"'),
            subtitle: Text(_overlay == true
                ? 'อนุญาตแล้ว — แอปเปิดตัวเองได้'
                : 'จำเป็น: Android ไม่ยอมให้แอปเปิดตัวเองจากเบื้องหลังถ้าไม่มีสิทธิ์นี้'),
            trailing: _overlay == true
                ? null
                : FilledButton(
                    onPressed: Native.requestOverlay,
                    child: const Text('อนุญาต')),
          ),
          const _Header('เปิดแอปเองเมื่อ'),
          SwitchListTile(
            dense: true,
            title: const Text('เปิดมือถือ (บูตเครื่องเสร็จ)'),
            subtitle: const Text('เหมาะกับมือถือ/แท็บเล็ตที่ติดในรถถาวร และติดเครื่องพร้อมรถ'),
            value: s.autoBoot,
            onChanged: (v) => _toggle(() => s.autoBoot = v, v),
          ),
          SwitchListTile(
            dense: true,
            title: const Text('เริ่มชาร์จไฟ (บิดกุญแจแล้วที่ชาร์จในรถจ่ายไฟ)'),
            subtitle: const Text('จะมีแจ้งเตือนเล็ก ๆ ค้างไว้ เพราะ Android ต้องให้แอปเฝ้ารอสัญญาณชาร์จ'),
            value: s.autoPower,
            onChanged: (v) async {
              if (v) await Native.requestNotif();
              await _toggle(() => s.autoPower = v, v);
            },
          ),
          SwitchListTile(
            dense: true,
            title: const Text('มือถือต่อ Bluetooth กับรถ'),
            subtitle: Text(s.autoBtAddress.isEmpty
                ? 'เลือกเครื่องเสียง/ระบบ Bluetooth ของรถก่อน (ไม่ใช่ Vgate)'
                : 'อุปกรณ์: ${s.autoBtName}'),
            value: s.autoBt && s.autoBtAddress.isNotEmpty,
            onChanged: s.autoBtAddress.isEmpty
                ? null
                : (v) => _toggle(() => s.autoBt = v, v),
          ),
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16),
            child: Align(
              alignment: Alignment.centerLeft,
              child: OutlinedButton.icon(
                onPressed: _pickCarBt,
                icon: const Icon(Icons.bluetooth),
                label: Text(s.autoBtAddress.isEmpty
                    ? 'เลือก Bluetooth ของรถ'
                    : 'เปลี่ยนอุปกรณ์'),
              ),
            ),
          ),
          const _Header('ตอนแอปเปิดขึ้นมา'),
          SwitchListTile(
            dense: true,
            title: const Text('แสดงบนหน้าล็อกได้เลย ไม่ต้องปลดล็อก'),
            subtitle: const Text('ปิดแอป/กดย้อนกลับแล้วจะกลับไปที่หน้าล็อกตามปกติ'),
            value: s.showOnLock,
            onChanged: (v) => _toggle(() => s.showOnLock = v, false),
          ),
          if (_anyOn) ...[
            const _Header('ถ้าแอปไม่เด้งขึ้นมาเอง'),
            ListTile(
              dense: true,
              leading: const Icon(Icons.battery_saver),
              title: const Text('ปิดการประหยัดแบตเตอรี่ให้แอปนี้'),
              subtitle: const Text(
                  'Xiaomi/Oppo/Vivo/Realme: เปิด "เริ่มอัตโนมัติ (Autostart)" ให้ Didissnap ในตั้งค่ามือถือด้วย'),
              trailing: OutlinedButton(
                  onPressed: Native.batterySettings,
                  child: const Text('เปิดตั้งค่า')),
            ),
          ],
        ],
      ),
    );
  }
}

// ======================================================================
//  ตั้งค่าจอ ESP32 (DidissnapConfig.h) ผ่าน WiFi
// ======================================================================
class Esp32Tab extends StatefulWidget {
  const Esp32Tab({super.key, required this.settings, required this.hub});
  final AppSettings settings;
  final DataHub hub;
  @override
  State<Esp32Tab> createState() => _Esp32TabState();
}

class _Esp32TabState extends State<Esp32Tab> {
  Map<String, dynamic>? _cfg;
  List<String> _pages = const [];
  List<String> _slots = const []; // ค่าที่เลือกโชว์ในช่องล่างหน้า 13 ได้ (บอร์ดรุ่นใหม่เท่านั้น)
  bool _busy = false;
  String? _err;

  @override
  void initState() {
    super.initState();
    widget.hub.addListener(_onHub);
    _load();
  }

  @override
  void dispose() {
    widget.hub.removeListener(_onHub);
    super.dispose();
  }

  void _onHub() {
    // เพิ่งต่อ ESP32 ติด → โหลดค่าตั้ง
    if (_cfg == null && !_busy && widget.hub.state == LinkState.connected) _load();
    if (mounted) setState(() {});
  }

  void _take(Map<String, dynamic> j) {
    final c = j['cfg'];
    if (c is Map) _cfg = Map<String, dynamic>.from(c);
    final p = j['pages'];
    if (p is List) _pages = p.map((e) => e.toString()).toList();
    final sl = j['slots'];
    if (sl is List) _slots = sl.map((e) => e.toString()).toList();
  }

  Widget _slotRow(String title, String key, int def) {
    final v = _i(key, def).clamp(0, _slots.length - 1);
    return ListTile(
      dense: true,
      title: Text(title),
      trailing: DropdownButton<int>(
        value: v,
        items: [for (var i = 0; i < _slots.length; i++) DropdownMenuItem(value: i, child: Text(_slots[i]))],
        onChanged: (n) {
          if (n == null) return;
          _set(key, n);
        },
      ),
    );
  }

  Future<void> _run(Future<Map<String, dynamic>?> Function() f, [String? ok]) async {
    final esp = widget.hub.esp32;
    if (esp == null) return;
    setState(() {
      _busy = true;
      _err = null;
    });
    try {
      final j = await f();
      if (j != null) _take(j);
      if (ok != null && mounted) _snack(context, ok);
    } catch (e) {
      _err = errText(e);
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _load() async {
    final esp = widget.hub.esp32;
    if (esp == null || widget.hub.state != LinkState.connected) return;
    await _run(() => esp.getConfig());
  }

  Future<void> _set(String key, num v) async {
    final esp = widget.hub.esp32;
    if (esp == null) return;
    setState(() => _cfg?[key] = v); // แสดงทันที แล้วค่อยรอบอร์ดยืนยัน
    await _run(() => esp.setConfig({key: v}));
  }

  int _i(String k, int d) => (_cfg?[k] as num?)?.toInt() ?? d;
  double _d(String k, double d) => (_cfg?[k] as num?)?.toDouble() ?? d;

  @override
  Widget build(BuildContext context) {
    final s = widget.settings;
    final hub = widget.hub;
    if (s.source != SourceType.esp32) {
      return ListView(padding: const EdgeInsets.all(16), children: [
        const Text('ตั้งค่าจอ ESP32 และส่งรูปเข้าจอได้เมื่อใช้แหล่งข้อมูล "ESP32 ผ่าน WiFi"',
            style: TextStyle(fontSize: 15)),
        const SizedBox(height: 12),
        FilledButton(
          onPressed: () => s.source = SourceType.esp32,
          child: const Text('เปลี่ยนเป็นโหมด ESP32'),
        ),
        const SizedBox(height: 12),
        const _Hint('ให้มือถือต่อ WiFi "Didissnap" (รหัส 12345678) ของบอร์ดก่อน'),
      ]);
    }
    final connected = hub.state == LinkState.connected;
    final cfg = _cfg;
    final page = _i('page', 0);
    return ListView(
      padding: const EdgeInsets.only(bottom: 24),
      children: [
        ListTile(
          leading: Icon(Icons.circle, size: 14, color: connected ? kGreen : kAmber),
          title: Text(connected ? 'ต่อบอร์ด ESP32 แล้ว' : 'ยังไม่ได้ต่อบอร์ด ESP32'),
          subtitle: Text('ตั้งในเบราว์เซอร์ก็ได้: http://${s.espHost}'),
          trailing: FilledButton.tonal(
              onPressed: _busy || !connected ? null : _load, child: const Text('โหลดค่า')),
        ),
        if (_busy) const LinearProgressIndicator(),
        if (_err != null)
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16),
            child: Text(_err!, style: const TextStyle(color: kRed)),
          ),
        if (cfg == null && connected && !_busy)
          const _Hint('บอร์ดยังไม่ตอบค่าตั้ง — บอร์ดต้องใช้โค้ดที่มี DidissnapConfig.h'),
        if (cfg != null) ...[
          const _Header('หน้าจอ'),
          ListTile(
            dense: true,
            title: const Text('หน้าที่แสดงบนจอ ESP32'),
            trailing: DropdownButton<int>(
              value: page.clamp(0, _pages.isEmpty ? 0 : _pages.length - 1),
              items: [
                for (var i = 0; i < (_pages.isEmpty ? 1 : _pages.length); i++)
                  DropdownMenuItem(
                      value: i,
                      child: Text('${i + 1}. ${_pages.isEmpty ? '' : _pages[i]}')),
              ],
              onChanged: (v) {
                final esp = hub.esp32;
                if (v == null || esp == null) return;
                setState(() => _cfg?['page'] = v);
                _run(() => esp.setPage(v));
              },
            ),
          ),
          _StepRow('สลับหน้าเองทุก (0 = ปิด)', _i('autoCycle', 0), 0, 120, 5,
              (v) => _set('autoCycle', v),
              unit: ' วิ'),
          ListTile(
            dense: true,
            title: Text('ความสว่าง ${_i('bri', 80)}%'),
            subtitle: Slider(
              value: _i('bri', 80).toDouble(),
              min: 10,
              max: 100,
              divisions: 9,
              onChanged: (v) => setState(() => _cfg?['bri'] = v.round()),
              onChangeEnd: (v) => _set('bri', v.round()),
            ),
          ),
          SwitchListTile(
            dense: true,
            title: const Text('ปรับความสว่างตามแสงอัตโนมัติ'),
            value: _i('briAuto', 0) != 0,
            onChanged: (v) => _set('briAuto', v ? 1 : 0),
          ),
          SwitchListTile(
            dense: true,
            title: const Text('กลับจอ 180°'),
            value: _i('flip', 0) != 0,
            onChanged: (v) => _set('flip', v ? 1 : 0),
          ),
          if (_cfg?.containsKey('p13spd') ?? false)
            SwitchListTile(
              dense: true,
              title: const Text('หน้า 13 โชว์ความเร็ว (มุมขวาบน)'),
              value: _i('p13spd', 1) != 0,
              onChanged: (v) => _set('p13spd', v ? 1 : 0),
            ),
          if (_slots.isNotEmpty) ...[
            _slotRow('หน้า 13 แถวล่าง ช่อง 1', 'p13a', 0),
            _slotRow('หน้า 13 แถวล่าง ช่อง 2', 'p13b', 1),
            _slotRow('หน้า 13 แถวล่าง ช่อง 3', 'p13c', 2),
            _slotRow('หน้า 13 แถวล่าง ช่อง 4', 'p13d', 3),
          ],
          const _Header('รอบ / ความร้อน'),
          _StepRow('Redline', _i('maxRpm', 7500), 2000, 12000, 250,
              (v) => _set('maxRpm', v),
              unit: ' rpm'),
          _StepRow('เตือนน้ำที่', _i('tempAlert', 100), 70, 130, 1,
              (v) => _set('tempAlert', v),
              unit: ' °C'),
          const _Header('เกียร์ (rpm ต่อ 1 km/h)'),
          _StepRow('จำนวนเกียร์', _i('gearCount', 6), 3, 6, 1, (v) => _set('gearCount', v)),
          for (var g = 0; g < _i('gearCount', 6); g++)
            ListTile(
              dense: true,
              title: Text('เกียร์ ${g + 1}'),
              trailing: Row(mainAxisSize: MainAxisSize.min, children: [
                IconButton(
                    icon: const Icon(Icons.remove),
                    onPressed: () => _set('g$g', double.parse((_d('g$g', 50) - 0.5).toStringAsFixed(1)))),
                SizedBox(
                    width: 64,
                    child: Text(_d('g$g', 50).toStringAsFixed(1),
                        textAlign: TextAlign.center,
                        style: const TextStyle(fontSize: 16, fontWeight: FontWeight.w700))),
                IconButton(
                    icon: const Icon(Icons.add),
                    onPressed: () => _set('g$g', double.parse((_d('g$g', 50) + 0.5).toStringAsFixed(1)))),
              ]),
            ),
          Esp32PicsSection(host: s.espHost, redline: _i('maxRpm', 7500)),
          const _Header('รีเซ็ต'),
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16),
            child: Wrap(spacing: 8, children: [
              OutlinedButton(
                onPressed: _busy
                    ? null
                    : () => _run(() async {
                          await hub.esp32?.resetPeaks();
                          return null;
                        }, 'ล้างค่าสูงสุดบนจอ ESP32 แล้ว'),
                child: const Text('ล้างค่าสูงสุด'),
              ),
              OutlinedButton(
                onPressed: _busy
                    ? null
                    : () => _run(() async {
                          await hub.esp32?.resetTrip();
                          return null;
                        }, 'รีเซ็ตทริปบนจอ ESP32 แล้ว'),
                child: const Text('รีเซ็ตทริป'),
              ),
            ]),
          ),
          const _Hint('อ่าน/ลบ DTC ของรถผ่านบอร์ดได้ที่แท็บ DTC'),
        ],
      ],
    );
  }
}
