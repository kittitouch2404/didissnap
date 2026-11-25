import 'dart:convert';
import 'dart:async';
import 'dart:io';
import 'dart:typed_data';

import 'package:flutter/material.dart' hide Text;
import '../i18n.dart';
import 'package:archive/archive.dart';
import 'package:image_picker/image_picker.dart';
import 'package:path_provider/path_provider.dart';

import '../data/c3_cal.dart';
import '../data/car_data.dart';
import '../data/custom_pid.dart';
import '../data/skin.dart';
import '../data/torque_import.dart';
import '../data/dtc_th.dart';
import '../data/freeze.dart';
import '../data/dyno.dart';
import '../data/elm_source.dart';
import '../data/key_sound.dart';
import 'custom_logo.dart';
import '../data/hub.dart';
import '../data/native.dart';
import '../data/services.dart';
import '../data/updater.dart';
import '../data/app_log.dart';
import 'help_page.dart';
import '../settings.dart';
import 'esp32_pics_ui.dart';
import 'paint_utils.dart';

part 'settings_more.dart';
part 'menu_hud.dart';

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
      length: 11,
      child: Scaffold(
        appBar: AppBar(
          toolbarHeight: 40,
          title: const Text('MENU', style: TextStyle(fontSize: 16)),
          bottom: TabBar(
            isScrollable: true,
            tabs: [
              Tab(height: 36, text: tr('การเชื่อมต่อ')),
              Tab(height: 36, text: tr('หน้าปัด')),
              Tab(height: 36, text: tr('ทริป/เตือน')),
              Tab(height: 36, text: tr('บันทึก CSV')),
              Tab(height: 36, text: tr('รถ/สำรอง')),
              Tab(height: 36, text: tr('เกียร์')),
              Tab(height: 36, text: tr('ปรับค่า')),
              Tab(height: 36, text: tr('รูปพื้นหลัง')),
              Tab(height: 36, text: tr('DTC')),
              Tab(height: 36, text: tr('เปิดอัตโนมัติ')),
              Tab(height: 36, text: tr('จอ ESP32')),
            ],
          ),
        ),
        body: TabBarView(children: [
          ConnectionTab(settings: settings, hub: hub),
          DisplayTab(settings: settings, hub: hub),
          TripAlertTab(settings: settings, hub: hub),
          LogTab(settings: settings, hub: hub),
          ProfileTab(settings: settings),
          GearTab(settings: settings, hub: hub),
          CalTab(settings: settings, hub: hub),
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
            style: TextStyle(
                fontSize: 13, color: Theme.of(context).colorScheme.primary, fontWeight: FontWeight.w700)),
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
        SourceType.c3 => 'ESP32-C3 ที่ช่อง OBD2 (Bluetooth BLE)  ★',
      };

  static String _sub(SourceType t) => switch (t) {
        SourceType.demo => 'ทดสอบหน้าจอโดยไม่ต้องต่อรถ',
        SourceType.btElm => 'มือถือต่อ OBD2 เอง ไม่ต้องใช้ ESP32',
        SourceType.wifiElm => 'ตัว OBD2 แบบปล่อย WiFi',
        SourceType.esp32 => 'ESP32 อ่าน OBD2 แล้วส่งค่าต่อมาที่มือถือ (โชว์พร้อมจอ CYD)',
        SourceType.c3 => 'C3 อ่าน CAN / K-line เอง แล้วส่งให้แอป + จอ S3 + จอ CYD พร้อมกัน ไม่ต้องใช้ Vgate / WiFi',
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
            const _Header('ส่งค่าให้จอ ESP32 CYD (WiFi)'),
            SwitchListTile(
              dense: true,
              title: const Text('ส่งค่าให้จอ CYD'),
              subtitle: ListenableBuilder(
                listenable: hub.cyd,
                builder: (_, __) => Text(hub.cyd.status),
              ),
              value: s.cydLink,
              onChanged: (v) async {
                s.cydLink = v;
                await Native.syncAutoStart();
              },
            ),
            const _Hint(
                'ให้จอแอนดรอยด์กับจอ CYD ต่อฮอตสปอตมือถือตัวเดียวกัน — แอปต่อ Vgate แล้วส่งค่าให้จอ CYD ราว 20 ครั้ง/วินาที\n'
                'แอปทำงานเบื้องหลังต่อได้ (เปิดยูทูป/แผนที่ทับได้) — ปิด "ประหยัดแบตเตอรี่" ให้แอปนี้ในเมนู เปิดแอปอัตโนมัติ'),
            if (s.source == SourceType.c3) _C3OtaSection(hub: hub),
            _CvtTempSection(s: s, hub: hub),
            _CustomPidSection(s: s, hub: hub),
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
      {this.unit = '', this.fmt});
  final String label, unit;
  final String Function(int)? fmt;
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
          child: Text(fmt != null ? fmt!(value) : '$value$unit',
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
          _StepRow('AFR stoich (เบนซิน 14.7 / E20 13.8 / E85 9.8)', s.afrStoich10, 50, 200, 1,
              (v) => s.afrStoich10 = v,
              fmt: (v) => (v / 10).toStringAsFixed(1)),
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
          const _Header('ตอนเปิดแอป'),
          for (final (v, t, sub) in const [
            (2, 'โลโก้สุ่ม OYT (แบบจอ ESP32-S3 4.3")', 'สุ่มสี 8 สี + ฟอนต์ 8/16/32-bit ทุกครั้ง ~6 วินาที (แตะจอเพื่อข้าม)'),
            (1, 'คลิปวิดีโอ', 'เล่นจนจบ → จอดำ → เข้าหน้าปัด (แตะจอเพื่อข้าม)'),
            (0, 'ไม่มี (เข้าหน้าปัดทันที)', ''),
          ])
            RadioListTile<int>(
              dense: true,
              title: Text(t),
              subtitle: sub.isEmpty ? null : Text(sub),
              value: v,
              groupValue: s.bootMode,
              onChanged: (x) => s.bootMode = x ?? 2,
            ),
          if (s.bootMode == 2) ...[
            SwitchListTile(
              dense: true,
              title: const Text('โลโก้ของฉัน (เล่นต่อจากโลโก้สุ่ม)'),
              subtitle: Text(s.myLogoPath.isEmpty
                  ? 'ยังไม่ได้เลือก — รูป / GIF / วิดีโอ'
                  : '${CustomLogoPage.isVideo(s.myLogoPath) ? 'วิดีโอ' : 'รูป/GIF'}: ${s.myLogoPath.split('/').last}'),
              value: s.myLogoOn && s.myLogoPath.isNotEmpty,
              onChanged: (v) async {
                if (v && s.myLogoPath.isEmpty) {
                  await CustomLogoPage.pick(s);
                } else {
                  s.myLogoOn = v;
                }
              },
            ),
            if (s.myLogoOn && s.myLogoPath.isNotEmpty) ...[
              Padding(
                padding: const EdgeInsets.symmetric(horizontal: 16),
                child: Wrap(spacing: 12, children: [
                  OutlinedButton.icon(
                    onPressed: () => CustomLogoPage.pick(s),
                    icon: const Icon(Icons.image),
                    label: const Text('เปลี่ยนโลโก้'),
                  ),
                  OutlinedButton.icon(
                    onPressed: () => Navigator.of(context).push(MaterialPageRoute<void>(
                        builder: (ctx) => CustomLogoPage(
                            settings: s, next: () => const _PreviewDone()))),
                    icon: const Icon(Icons.play_arrow),
                    label: const Text('ดูตัวอย่าง'),
                  ),
                ]),
              ),
              if (!CustomLogoPage.isVideo(s.myLogoPath))
                _StepRow('แสดงนาน', s.myLogoSec, 1, 15, 1, (v) => s.myLogoSec = v, unit: ' วินาที'),
              SwitchListTile(
                dense: true,
                title: const Text('เต็มจอ (ครอปขอบ)'),
                subtitle: const Text('ปิด = พอดีจอ มีขอบดำ'),
                value: s.myLogoFill,
                onChanged: (v) => s.myLogoFill = v,
              ),
              if (CustomLogoPage.isVideo(s.myLogoPath))
                SwitchListTile(
                  dense: true,
                  title: const Text('เปิดเสียงวิดีโอ'),
                  value: s.myLogoSound,
                  onChanged: (v) => s.myLogoSound = v,
                ),
            ],
          ],
          const _Header('เสียงตอนบิดกุญแจ'),
          ListTile(
            dense: true,
            title: const Text('เสียง'),
            subtitle: Text(s.keySound == 4
                ? (s.keySoundPath.isEmpty ? 'ยังไม่ได้เลือกไฟล์' : s.keySoundPath.split('/').last)
                : 'เล่นตอนแอปเปิดเองเพราะรถติด/จอรถตื่น'),
            trailing: DropdownButton<int>(
              value: s.keySound.clamp(0, KeySound.builtIn.length - 1),
              items: [
                for (var i = 0; i < KeySound.builtIn.length; i++)
                  DropdownMenuItem(value: i, child: Text(KeySound.builtIn[i].$1)),
              ],
              onChanged: (v) async {
                if (v == 4) {
                  final p = await Native.pickAudio();
                  if (p == null) return;
                  s.keySoundPath = p;
                }
                s.keySound = v ?? 0;
                if ((v ?? 0) > 0) KeySound.play(s, force: true);
              },
            ),
          ),
          if (s.keySound > 0) ...[
            _StepRow('ความดัง', s.keySoundVol, 0, 100, 10, (v) => s.keySoundVol = v, unit: '%'),
            SwitchListTile(
              dense: true,
              title: const Text('เล่นทุกครั้งที่เปิดแอป'),
              subtitle: const Text('ปิด = เล่นเฉพาะตอนรถติด (แอปเด้งขึ้นเอง หรือแอปเปิดค้างไว้แล้วจอรถตื่น)'),
              value: s.keySoundAlways,
              onChanged: (v) => s.keySoundAlways = v,
            ),
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 16),
              child: Row(children: [
                OutlinedButton.icon(
                  onPressed: () => KeySound.play(s, force: true),
                  icon: const Icon(Icons.play_arrow),
                  label: const Text('ลองฟัง'),
                ),
                const SizedBox(width: 12),
                if (s.keySound == 4)
                  OutlinedButton.icon(
                    onPressed: () async {
                      final p = await Native.pickAudio();
                      if (p != null) s.keySoundPath = p;
                    },
                    icon: const Icon(Icons.audio_file),
                    label: const Text('เปลี่ยนไฟล์'),
                  ),
              ]),
            ),
          ],
          SwitchListTile(
            dense: true,
            title: const Text('เปิดเสียงคลิป'),
            value: s.bootSound,
            onChanged: s.bootMode == 1 ? (v) => s.bootSound = v : null,
          ),
          ListTile(
            dense: true,
            title: const Text('TRIP (ระยะทางที่คำนวณจากความเร็ว)'),
            subtitle: Text('ทริป A ${s.live('tripA').toStringAsFixed(1)} km (รีเซ็ต/ทริป B ดูที่แท็บ ทริป/เตือน)'),
            trailing: FilledButton.tonal(
              onPressed: () {
                hub.data.trip = 0;
                s.resetTrip('A');
                _snack(context, 'รีเซ็ต TRIP แล้ว');
              },
              child: const Text('รีเซ็ต'),
            ),
          ),
          const _Header('หน้าที่แสดง (ปัดซ้าย/ขวา)'),
          for (final p in s.allPages)
            SwitchListTile(
              dense: true,
              secondary: p.$1.startsWith('skin:')
                  ? IconButton(
                      tooltip: tr('ลบสกินนี้'),
                      icon: const Icon(Icons.delete_outline),
                      onPressed: () async {
                        final ok = await showDialog<bool>(
                          context: context,
                          builder: (ctx) => AlertDialog(
                            title: Text('ลบ ${p.$2}?'),
                            content: const Text('ลบไฟล์สกินออกจากแอป (ไฟล์ .zip ต้นฉบับไม่หาย นำเข้าใหม่ได้)'),
                            actions: [
                              TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('ยกเลิก')),
                              FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('ลบ')),
                            ],
                          ),
                        );
                        if (ok == true) await SkinStore.delete(p.$1.substring(5), s);
                      },
                    )
                  : p.$1.startsWith('gif') && s.gifPageCount > 1
                  ? IconButton(
                      tooltip: tr('ลบหน้านี้'),
                      icon: const Icon(Icons.delete_outline),
                      onPressed: () async {
                        final ok = await showDialog<bool>(
                          context: context,
                          builder: (ctx) => AlertDialog(
                            title: Text('ลบหน้า ${p.$2.split(' (').first}?'),
                            content: const Text('ค่าตั้งของหน้านี้ (GIF / สี / ช่องค่า) จะหายไป'),
                            actions: [
                              TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('ยกเลิก')),
                              FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('ลบ')),
                            ],
                          ),
                        );
                        if (ok == true) s.deleteGifPage(int.parse(p.$1.substring(3)) - 1);
                      },
                    )
                  : null,
              title: Text(p.$2),
              value: s.pages.contains(p.$1),
              onChanged: (on) {
                final cur = s.pages;
                if (!on && cur.length <= 1) {
                  _snack(context, 'ต้องเปิดไว้อย่างน้อย 1 หน้า');
                  return;
                }
                s.pages = s.allPages
                    .map((e) => e.$1)
                    .where((id) => id == p.$1 ? on : cur.contains(id))
                    .toList();
              },
            ),
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 6),
            child: Align(
              alignment: Alignment.centerLeft,
              child: FilledButton.icon(
                icon: const Icon(Icons.add),
                label: Text('เพิ่มหน้า GIF (มี ${s.gifPageCount}/20)'),
                onPressed: s.gifPageCount >= 20
                    ? null
                    : () {
                        final i = s.addGifPage();
                        if (i >= 0) _snack(context, 'เพิ่มหน้า GIF ${i + 1} แล้ว — ปัดไปหน้านั้น แล้วกดค้างที่ตัวเลขเพื่อเลือก GIF');
                      },
              ),
            ),
          ),
          const _Header('สกินหน้าปัด (ออกแบบเอง)'),
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
            child: Align(
              alignment: Alignment.centerLeft,
              child: FilledButton.icon(
                icon: const Icon(Icons.file_open_outlined),
                label: const Text('นำเข้าสกิน (.zip)'),
                onPressed: () async {
                  final path = await Native.pickFile(mime: 'application/zip');
                  if (path == null || !context.mounted) return;
                  try {
                    final name = await SkinStore.importZip(path, s);
                    if (context.mounted) _snack(context, 'นำเข้า "$name" แล้ว — ปัดไปหน้าสุดท้ายเพื่อดู');
                  } catch (e) {
                    if (context.mounted) _snack(context, 'นำเข้าไม่ได้: $e');
                  }
                },
              ),
            ),
          ),
          const _Hint('ออกแบบในเว็บ "Didissnap Skin Designer" (เปิดบนคอม/มือถือ) → Export ได้ไฟล์ .zip → ส่งไฟล์มาที่จอนี้ → กดนำเข้า\n'
              'สกินชื่อเดิม = เขียนทับของเดิม (แก้แล้วนำเข้าใหม่ได้เลย) · ลบได้จากปุ่มถังขยะในรายการหน้าด้านบน'),
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
class DtcTab extends StatefulWidget {
  const DtcTab({super.key, required this.hub});
  final DataHub hub;
  @override
  State<DtcTab> createState() => _DtcTabState();
}

class _DtcTabState extends State<DtcTab> {
  List<String>? _codes, _pending;
  FreezeFrame? _ff;
  bool _ffTried = false;
  String? _err;
  bool _busy = false;
  String _step = '';

  Future<void> _read() async {
    setState(() {
      _busy = true;
      _err = null;
      _ff = null;
      _ffTried = false;
      _step = 'อ่านรหัสที่ทำให้ไฟโชว์...';
    });
    try {
      final c = await widget.hub.readDtc();
      if (!mounted) return;
      setState(() {
        _codes = c;
        _step = 'อ่านรหัสรอยืนยัน...';
      });
      final p = await widget.hub.readPending();
      if (!mounted) return;
      setState(() {
        _pending = p.where((e) => !c.contains(e)).toList();
        _step = 'อ่าน freeze frame (ค่าตอนไฟโชว์ติด)...';
      });
      final ff = await readFreezeFrame(widget.hub);
      if (mounted) setState(() {
        _ff = ff;
        _ffTried = true;
      });
    } catch (e) {
      if (mounted) setState(() => _err = errText(e));
    } finally {
      if (mounted) setState(() {
        _busy = false;
        _step = '';
      });
    }
  }

  Future<void> _clear() async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('ลบรหัส DTC?'),
        content: const Text(
            'จะล้างรหัส + freeze frame และดับไฟ Check Engine (ข้อมูล Readiness จะถูกรีเซ็ตด้วย)\n'
            'แนะนำให้จดรหัส/ถ่ายรูปหน้านี้ไว้ก่อน · ดับเครื่อง แต่เปิดสวิตช์กุญแจค้างไว้ที่ ON'),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('ยกเลิก')),
          FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('ลบ')),
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
      if (done) {
        setState(() {
          _codes = [];
          _pending = [];
          _ff = null;
        });
      }
    } catch (e) {
      if (mounted) setState(() => _err = errText(e));
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Widget _codeTile(String c, {bool pending = false}) {
    final sev = dtcSeverity(c);
    final col = pending ? Colors.white54 : (sev == 2 ? kRed : kAmber);
    // ค้นพร้อมรุ่นรถ (ถ้าเคยเลือกรถในหน้าไดโน)
    final car = widget.hub.settings.dynoCfg.isEmpty ? 'OBD2' : DynoCfg.of(widget.hub.settings).title;
    return ListTile(
      leading: Icon(pending ? Icons.schedule : Icons.warning_amber_rounded, color: col),
      title: Row(children: [
        Text(c, style: const TextStyle(fontSize: 20, fontWeight: FontWeight.w800)),
        if (sev == 2 && !pending) ...[
          const SizedBox(width: 8),
          const Text('ควรรีบตรวจ', style: TextStyle(fontSize: 12, color: kRed)),
        ],
      ]),
      subtitle: Text(dtcThai(c)),
      trailing: IconButton(
        tooltip: tr('ค้นในเว็บ'),
        icon: const Icon(Icons.travel_explore),
        onPressed: () => Native.openUrl('https://www.google.com/search?q=${Uri.encodeComponent('$c $car')}'),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final codes = _codes, pend = _pending, ff = _ff;
    return ListView(
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
            const SizedBox(width: 16),
            if (_step.isNotEmpty) Expanded(child: Text(_step, style: const TextStyle(color: Colors.white60))),
          ]),
        ),
        if (_busy) const LinearProgressIndicator(),
        if (_err != null)
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
            child: Text(_err!, style: const TextStyle(color: kRed)),
          ),
        if (codes != null && codes.isEmpty && (pend == null || pend.isEmpty))
          const Padding(
            padding: EdgeInsets.all(16),
            child: Text('ไม่พบรหัส DTC ✓', style: TextStyle(fontSize: 18, color: kGreen)),
          ),
        if (codes != null && codes.isNotEmpty) ...[
          const _Header('รหัสที่ทำให้ไฟโชว์ติด'),
          for (final c in codes) _codeTile(c),
        ],
        if (pend != null && pend.isNotEmpty) ...[
          const _Header('รหัสรอยืนยัน (เกิดแล้ว แต่ยังไม่ติดไฟโชว์)'),
          for (final c in pend) _codeTile(c, pending: true),
          const _Hint('ถ้าเกิดซ้ำในการขับรอบถัดไป กล่องจะยืนยันแล้วไฟโชว์ติด · ถ้าไม่เกิดอีก จะหายไปเอง'),
        ],
        if (ff != null) ...[
          _Header('Freeze frame — ค่าตอนไฟโชว์ติด${ff.code.isEmpty ? '' : ' (จาก ${ff.code})'}'),
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16),
            child: Wrap(spacing: 10, runSpacing: 10, children: [
              for (final r in ff.rows)
                Container(
                  width: 230,
                  padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                  decoration: BoxDecoration(color: const Color(0xFF15171C), borderRadius: BorderRadius.circular(10), border: Border.all(color: const Color(0xFF262A31))),
                  child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                    Text(r.name, style: const TextStyle(fontSize: 12, color: Colors.white60), maxLines: 1, overflow: TextOverflow.ellipsis),
                    Text('${r.value} ${r.unit}', style: const TextStyle(fontSize: 20, fontWeight: FontWeight.w800)),
                  ]),
                ),
            ]),
          ),
          const _Hint('ดูว่าไฟขึ้นตอนขับแบบไหน เช่น รอบสูง/ต่ำ เครื่องร้อน/เย็น ส่วนผสมหนา/บาง (fuel trim บวก = กล่องเติมน้ำมันเพิ่มเพราะบาง)'),
        ] else if (_ffTried && codes != null && codes.isNotEmpty)
          const _Hint('รถคันนี้ไม่มี freeze frame ให้อ่าน (หรือถูกลบไปแล้ว)'),
        const _Hint('คำอธิบายเป็นสาเหตุที่พบบ่อย ไม่ใช่คำวินิจฉัย — ควรตรวจซ้ำก่อนเปลี่ยนอะไหล่ · ปุ่มแว่นขยาย = ค้นรหัสนี้พร้อมรุ่นรถในเว็บ'),
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
      widget.settings.autoWake ||
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
            title: const Text('จอรถตื่น / ติดเครื่อง (จอแอนดรอยด์ติดรถ)  ★ แนะนำ'),
            subtitle: const Text('จอติดรถส่วนใหญ่ไม่ได้ปิดเครื่องจริง แค่หลับ — แอปจะเด้งขึ้นเมื่อจอตื่น/ได้สัญญาณ ACC ON (รอ 2-8 วินาที)'),
            value: s.autoWake,
            onChanged: (v) async {
              if (v) await Native.requestNotif();
              await _toggle(() => s.autoWake = v, v);
            },
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
          if ((_cfg?.containsKey('pgMask') ?? false) && _pages.isNotEmpty)
            ExpansionTile(
              title: const Text('เปิด/ปิดหน้า (ปิด = กดปุ่มข้ามไป)'),
              subtitle: Text('ใช้อยู่ ${[for (var i = 0; i < _pages.length; i++) if ((_i('pgMask', 0xFFFF) >> i) & 1 == 1) i].length} จาก ${_pages.length} หน้า'),
              children: [
                for (var i = 0; i < _pages.length; i++)
                  CheckboxListTile(
                    dense: true,
                    title: Text('${i + 1}. ${_pages[i]}'),
                    value: (_i('pgMask', 0xFFFF) >> i) & 1 == 1,
                    onChanged: (v) {
                      final m0 = _i('pgMask', 0xFFFF);
                      final m = v == true ? (m0 | (1 << i)) : (m0 & ~(1 << i));
                      if (m == 0) return; // ต้องเหลืออย่างน้อย 1 หน้า
                      _set('pgMask', m);
                    },
                  ),
              ],
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
          if (_cfg?.containsKey('needle') ?? false)
            ListTile(
              dense: true,
              title: const Text('แบบเข็ม (หน้า 5 และ 8)'),
              trailing: DropdownButton<int>(
                value: _i('needle', 0).clamp(0, 5),
                items: const [
                  DropdownMenuItem(value: 0, child: Text('1 CLASSIC')),
                  DropdownMenuItem(value: 1, child: Text('2 TAPER')),
                  DropdownMenuItem(value: 2, child: Text('3 GLOW')),
                  DropdownMenuItem(value: 3, child: Text('4 SPORT')),
                  DropdownMenuItem(value: 4, child: Text('5 ARROW')),
                  DropdownMenuItem(value: 5, child: Text('6 BLADE 3D')),
                ],
                onChanged: (n) {
                  if (n != null) _set('needle', n);
                },
              ),
            ),
          if (_cfg?.containsKey('anim') ?? false)
            SwitchListTile(
              dense: true,
              title: const Text('อนิเมชันเกจ'),
              subtitle: const Text('กวาดเข็มตอนเปิด · เข็มสปริง · ค้างค่าสูงสุด · สีตามรอบ · ไฟ shift'),
              value: _i('anim', 1) != 0,
              onChanged: (v) => _set('anim', v ? 1 : 0),
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
          if (_cfg?.containsKey('picLay') ?? false)
            for (final n in [14, 15, 16])
              ListTile(
                dense: true,
                title: Text('หน้า $n แบบ'),
                trailing: DropdownButton<int>(
                  value: (_i('picLay', 0) >> (n - 14)) & 1,
                  items: const [
                    DropdownMenuItem(value: 0, child: Text('แบบเดิม (แถบบน)')),
                    DropdownMenuItem(value: 1, child: Text('แบบหน้า 13')),
                  ],
                  onChanged: (v) {
                    if (v == null) return;
                    final b = 1 << (n - 14), m = _i('picLay', 0);
                    _set('picLay', v == 1 ? (m | b) : (m & ~b));
                  },
                ),
              ),
          const _Header('รอบ / ความร้อน'),
          _StepRow('Redline', _i('maxRpm', 7500), 2000, 12000, 250,
              (v) => _set('maxRpm', v),
              unit: ' rpm'),
          _StepRow('เตือนน้ำที่', _i('tempAlert', 100), 70, 130, 1,
              (v) => _set('tempAlert', v),
              unit: ' °C'),
          if (_cfg?.containsKey('spdG') ?? false) ...[
            const _Header('ปรับค่าให้ตรง'),
            const _Hint('ค่าที่โชว์ = ค่าจากรถ × เกน% + บวก/ลบ · ปรับแล้วจอ CYD ตรงตามด้วย'),
            _StepRow('ความเร็ว เกน', _i('spdG', 100), 80, 130, 1, (v) => _set('spdG', v), unit: ' %'),
            _StepRow('รอบ เกน', _i('rpmG', 100), 80, 120, 1, (v) => _set('rpmG', v), unit: ' %'),
            _StepRow('AFR เกน', _i('afrG', 100), 80, 120, 1, (v) => _set('afrG', v), unit: ' %'),
            _StepRow('น้ำ บวก/ลบ', _i('ectO', 0), -15, 15, 1, (v) => _set('ectO', v),
                fmt: (v) => '${v > 0 ? '+' : ''}$v °C'),
            _StepRow('อากาศเข้า บวก/ลบ', _i('iatO', 0), -15, 15, 1, (v) => _set('iatO', v),
                fmt: (v) => '${v > 0 ? '+' : ''}$v °C'),
            _StepRow('แบต บวก/ลบ', _i('vOfs', 0), -20, 20, 1, (v) => _set('vOfs', v),
                fmt: (v) => '${v > 0 ? '+' : ''}${(v / 10).toStringAsFixed(1)} V'),
            _StepRow('TPS จุด 0% (ค่าดิบ)', _i('tps0', 0), 0, 60, 1, (v) => _set('tps0', v), unit: ' %'),
            _StepRow('TPS จุด 100% (ค่าดิบ)', _i('tps1', 100), 30, 100, 1, (v) => _set('tps1', v), unit: ' %'),
            if (_cfg?.containsKey('tpsRaw') ?? false)
              _Hint('TPS ดิบตอนกดอ่าน: ${_d('tpsRaw', 0).toStringAsFixed(1)} % (กด "โหลดค่า" เพื่ออ่านใหม่)'),
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
              child: Wrap(spacing: 8, runSpacing: 8, children: [
                OutlinedButton(onPressed: () => _set('tpsSet', 0), child: const Text('ตั้ง TPS ตอนนี้ = 0%')),
                OutlinedButton(onPressed: () => _set('tpsSet', 1), child: const Text('ตั้ง TPS ตอนนี้ = 100%')),
                TextButton(
                  onPressed: () async {
                    final ok = await showDialog<bool>(
                      context: context,
                      builder: (c) => AlertDialog(
                        title: const Text('คืนค่าปรับทั้งหมด?'),
                        actions: [
                          TextButton(onPressed: () => Navigator.pop(c, false), child: const Text('ยกเลิก')),
                          FilledButton(onPressed: () => Navigator.pop(c, true), child: const Text('คืนค่า')),
                        ],
                      ),
                    );
                    if (ok == true) _set('calReset', 1);
                  },
                  child: const Text('คืนค่าเดิม'),
                ),
              ]),
            ),
            const _Hint('ตั้ง TPS 0%: ปล่อยคันเร่ง บิดกุญแจ ON · ตั้ง 100%: ดับเครื่อง บิดกุญแจ ON แล้วกดคันเร่งสุด'),
          ],
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


/// อุณหภูมิน้ำมันเกียร์ CVT (ทดลอง): PID เฉพาะยี่ห้อ ตั้ง header/คำสั่ง/ไบต์/สูตรเองได้ + ดูคำตอบดิบสด ๆ
const List<String> kCvtFormulas = [
  'A - 40',
  'A',
  '(A*256+B)/10 - 40',
  '((A*256+B) - 2731)/10',
  'A - 50',
  'A*9/5-58 (°F) -> °C',
];

class _CvtTempSection extends StatefulWidget {
  const _CvtTempSection({required this.s, required this.hub});
  final AppSettings s;
  final DataHub hub;
  @override
  State<_CvtTempSection> createState() => _CvtTempSectionState();
}

class _CvtTempSectionState extends State<_CvtTempSection> {
  Timer? _t;
  @override
  void initState() {
    super.initState();
    _t = Timer.periodic(const Duration(milliseconds: 700), (_) {
      if (mounted && widget.s.cvtTempOn) setState(() {});
    });
  }

  @override
  void dispose() {
    _t?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final s = widget.s, d = widget.hub.data;
    final fresh = d.cvtTemp != null && DateTime.now().difference(d.cvtAt).inSeconds < 5;
    return Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
      const _Header('อุณหภูมิน้ำมันเกียร์ CVT (ทดลอง)'),
      SwitchListTile(
        dense: true,
        title: const Text('อ่าน CVT TEMP จากกล่องเกียร์'),
        subtitle: Text(!s.cvtTempOn
            ? 'ปิดอยู่'
            : fresh
                ? 'ตอนนี้ ${d.cvtTemp!.toStringAsFixed(0)} °C'
                : 'ยังไม่ได้ค่า'),
        value: s.cvtTempOn,
        onChanged: (v) => s.cvtTempOn = v,
      ),
      if (s.cvtTempOn) ...[
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
          child: Row(children: [
            SizedBox(
              width: 110,
              child: TextFormField(
                initialValue: s.cvtHdr,
                decoration: const InputDecoration(labelText: 'Header', hintText: '7E1'),
                textCapitalization: TextCapitalization.characters,
                onChanged: (v) => s.cvtHdr = v,
              ),
            ),
            const SizedBox(width: 16),
            Expanded(
              child: TextFormField(
                initialValue: s.cvtReq,
                decoration: InputDecoration(labelText: tr('คำสั่ง (Mode 22 / 21)'), hintText: tr('เช่น 22xxxx หรือ 21xx')),
                textCapitalization: TextCapitalization.characters,
                onChanged: (v) => s.cvtReq = v,
              ),
            ),
          ]),
        ),
        _StepRow('ไบต์ A = ไบต์ที่ (หลัง echo คำสั่ง)', s.cvtByte, 0, 60, 1, (v) => s.cvtByte = v),
        ListTile(
          dense: true,
          title: const Text('สูตร'),
          trailing: DropdownButton<int>(
            value: s.cvtFml.clamp(0, kCvtFormulas.length - 1),
            items: [
              for (var i = 0; i < kCvtFormulas.length; i++)
                DropdownMenuItem(value: i, child: Text(kCvtFormulas[i])),
            ],
            onChanged: (v) => s.cvtFml = v ?? 0,
          ),
        ),
        ListTile(
          dense: true,
          title: const Text('คำตอบดิบล่าสุด'),
          subtitle: SelectableText(d.cvtRaw.isEmpty ? tr('(ยังไม่มี — ต่อ OBD ติดก่อน)') : d.cvtRaw,
              style: const TextStyle(fontFamily: 'monospace', fontSize: 13)),
        ),
        const _Hint('ทดลอง: ใช้ได้เฉพาะรถ CAN (Jazz GK) — แอปจะสลับไปถามกล่องเกียร์ราว 1-2 วินาทีครั้ง แล้วกลับมาถามเครื่องยนต์\n'
            'Honda ไม่เปิดเผย PID นี้ ต้องลองเอง: ถ้าคำตอบขึ้น 7F ... = กล่องไม่รองรับคำสั่งนั้น, NO DATA = ไม่มีใครตอบ\n'
            'ค่าที่ได้ส่งต่อให้จอ CYD ด้วย (เลือก CVT TEMP ในช่องขวาของหน้า GIF)'),
      ],
    ]);
  }
}


/// Custom PID 4 ช่อง (แบบ Torque): ตั้งชื่อ/คำสั่ง/สูตรเอง แล้วเลือกโชว์ในช่องขวาของหน้า GIF (ส่งให้จอ CYD ด้วย)
class _CustomPidSection extends StatefulWidget {
  const _CustomPidSection({required this.s, required this.hub});
  final AppSettings s;
  final DataHub hub;
  @override
  State<_CustomPidSection> createState() => _CustomPidSectionState();
}

class _CustomPidSectionState extends State<_CustomPidSection> {
  Timer? _t;
  @override
  void initState() {
    super.initState();
    _t = Timer.periodic(const Duration(milliseconds: 700), (_) {
      if (mounted) setState(() {});
    });
  }

  @override
  void dispose() {
    _t?.cancel();
    super.dispose();
  }

  String _now(int k) {
    final v = widget.hub.data.custNow(k);
    final c = widget.s.customPid(k);
    return v == null ? '--' : '${v.toStringAsFixed(c.dec.clamp(0, 2))} ${c.unit}';
  }

  @override
  Widget build(BuildContext context) {
    final s = widget.s;
    return Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
      const _Header('Custom PID (ตั้งค่าเอง แบบ Torque)'),
      for (var k = 0; k < AppSettings.nCustom; k++)
        Builder(builder: (_) {
          final c = s.customPid(k);
          return ListTile(
            dense: true,
            leading: Icon(c.valid ? Icons.tune : Icons.add_circle_outline),
            title: Text(c.name.trim().isEmpty ? 'ช่อง ${k + 1}: ยังไม่ได้ตั้ง' : 'ช่อง ${k + 1}: ${c.label}'),
            subtitle: Text(c.cmd.isEmpty
                ? 'แตะเพื่อตั้งค่า'
                : '${c.hdr.isEmpty ? '7DF' : c.hdr} · ${c.cmd} · ${c.formula}${c.on ? '' : ' (ปิด)'}'),
            trailing: Text(c.valid ? _now(k) : '',
                style: const TextStyle(fontSize: 16, fontWeight: FontWeight.w700)),
            onTap: () => _edit(k),
          );
        }),
      Padding(
        padding: const EdgeInsets.fromLTRB(16, 4, 16, 0),
        child: Align(
          alignment: Alignment.centerLeft,
          child: OutlinedButton.icon(
            icon: const Icon(Icons.file_open_outlined),
            label: const Text('นำเข้าจากไฟล์ Torque (.csv)'),
            onPressed: _importTorque,
          ),
        ),
      ),
      const _Hint('เลือกโชว์ได้ในช่องขวาของหน้า GIF (CUSTOM 1-4) และส่งต่อให้จอ CYD ด้วย\n'
          'ตัวอย่าง: น้ำมันเครื่อง 015C สูตร A-40 · องศาไฟ 010E สูตร A/2-64 · แรงดันเซนเซอร์ A/F 0134 สูตร (C*256+D)/256-128\n'
          'ไฟล์ Torque: หาได้จากเว็บ/กลุ่มรถรุ่นนั้น (ค้น "torque pid csv" + รุ่นรถ) → เลือกค่าที่อยากได้ใส่ช่อง 1-4'),
    ]);
  }

  /// นำเข้า PID จากไฟล์ .csv ของแอป Torque แล้วเลือกใส่ช่อง 1-4
  Future<void> _importTorque() async {
    final path = await Native.pickFile(mime: '*/*');
    if (path == null || !mounted) return;
    List<TorquePid> list;
    try {
      final bytes = await File(path).readAsBytes();
      list = parseTorqueCsv(utf8.decode(bytes, allowMalformed: true));
    } catch (e) {
      if (mounted) _snack(context, 'อ่านไฟล์ไม่ได้: $e');
      return;
    }
    if (!mounted) return;
    if (list.isEmpty) {
      _snack(context, 'ไม่พบรายการ PID ในไฟล์นี้ (ต้องเป็นไฟล์ .csv ของ Torque)');
      return;
    }
    final q = TextEditingController();
    await showDialog<void>(
      context: context,
      builder: (ctx) => StatefulBuilder(builder: (ctx, set) {
        final f = q.text.trim().toLowerCase();
        final shown = list.where((p) => f.isEmpty || '${p.name} ${p.short} ${p.cmd}'.toLowerCase().contains(f)).toList();
        final okN = list.where((p) => p.problem == null).length;
        return AlertDialog(
          title: Text('PID จากไฟล์ (${list.length} รายการ · ใช้ได้ $okN)'),
          content: SizedBox(
            width: 720,
            height: 460,
            child: Column(children: [
              TextField(
                controller: q,
                decoration: InputDecoration(prefixIcon: const Icon(Icons.search), hintText: tr('ค้นชื่อ / คำสั่ง'), isDense: true),
                onChanged: (_) => set(() {}),
              ),
              const SizedBox(height: 6),
              Expanded(
                child: ListView.builder(
                  itemCount: shown.length,
                  itemBuilder: (_, i) {
                    final p = shown[i];
                    return ListTile(
                      dense: true,
                      title: Text(p.name),
                      subtitle: Text(
                          '${p.hdr.isEmpty ? '7DF' : p.hdr} · ${p.cmd} · ${p.formula}${p.unit.isEmpty ? '' : ' (${p.unit})'}'
                          '${p.problem == null ? '' : '\n⚠ ${p.problem}'}',
                          style: TextStyle(color: p.problem == null ? null : Colors.orangeAccent)),
                      trailing: p.problem != null
                          ? null
                          : PopupMenuButton<int>(
                              tooltip: tr('ใส่ช่อง'),
                              icon: const Icon(Icons.add_circle_outline),
                              itemBuilder: (_) => [
                                for (var k = 0; k < AppSettings.nCustom; k++)
                                  PopupMenuItem(
                                    value: k,
                                    child: Text('ใส่ช่อง ${k + 1}'
                                        '${widget.s.customPid(k).valid ? ' (แทน ${widget.s.customPid(k).label})' : ''}'),
                                  ),
                              ],
                              onSelected: (k) {
                                widget.s.setCustomPid(k, p.toCustom());
                                set(() {});
                                ScaffoldMessenger.of(ctx).showSnackBar(
                                    SnackBar(content: Text('ใส่ "${p.label}" ในช่อง ${k + 1} แล้ว')));
                              },
                            ),
                    );
                  },
                ),
              ),
            ]),
          ),
          actions: [FilledButton(onPressed: () => Navigator.pop(ctx), child: const Text('เสร็จ'))],
        );
      }),
    );
    if (mounted) setState(() {});
  }

  Future<void> _edit(int k) async {
    final s = widget.s;
    final o = s.customPid(k);
    final c = CustomPid(o.toJson());
    final name = TextEditingController(text: c.name);
    final unit = TextEditingController(text: c.unit);
    final hdr = TextEditingController(text: c.hdr);
    final cmd = TextEditingController(text: c.cmd);
    final fml = TextEditingController(text: c.formula);
    final lo = TextEditingController(text: _num(c.lo));
    final hi = TextEditingController(text: _num(c.hi));
    var on = c.on || c.cmd.isEmpty;
    var dec = c.dec.clamp(0, 2);
    Timer? live;
    void save() {
      c
        ..on = on
        ..name = name.text.trim()
        ..unit = unit.text.trim()
        ..hdr = CustomPid.cleanHex(hdr.text)
        ..cmd = CustomPid.cleanHex(cmd.text)
        ..formula = fml.text.trim().isEmpty ? 'A' : fml.text.trim()
        ..lo = double.tryParse(lo.text) ?? 0
        ..hi = double.tryParse(hi.text) ?? 100
        ..dec = dec;
      if (c.hi <= c.lo) c.hi = c.lo + 1;
      s.setCustomPid(k, CustomPid(c.toJson()));
    }

    await showDialog<void>(
      context: context,
      builder: (ctx) => StatefulBuilder(builder: (ctx, set) {
        live ??= Timer.periodic(const Duration(milliseconds: 600), (_) {
          if (ctx.mounted) set(() {});
        });
        final d = widget.hub.data;
        final raw = d.custRaw[k];
        String test = '';
        final b = ElmSource.parseUds(raw, CustomPid.cleanHex(cmd.text));
        if (b != null) {
          final v = evalFormula(fml.text.trim().isEmpty ? 'A' : fml.text, b);
          final abc = [for (var i = 0; i < b.length && i < 8; i++) '${String.fromCharCode(65 + i)}=${b[i]}'].join(' ');
          test = '$abc\n= ${v == null ? 'สูตรผิด / ไบต์ไม่พอ' : v.toStringAsFixed(dec)}';
        }
        Widget tf(TextEditingController t, String label, {String? hint, bool caps = false, double? w}) {
          final f = TextField(
            controller: t,
            decoration: InputDecoration(labelText: label, hintText: hint, isDense: true),
            textCapitalization: caps ? TextCapitalization.characters : TextCapitalization.none,
          );
          return w == null ? f : SizedBox(width: w, child: f);
        }

        return AlertDialog(
          title: Text('Custom PID ช่อง ${k + 1}'),
          content: SingleChildScrollView(
            child: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.stretch, children: [
              SwitchListTile(
                contentPadding: EdgeInsets.zero,
                dense: true,
                title: const Text('เปิดใช้'),
                value: on,
                onChanged: (v) => set(() => on = v),
              ),
              Row(children: [
                Expanded(child: tf(name, 'ชื่อ', hint: 'OIL TEMP')),
                const SizedBox(width: 12),
                tf(unit, 'หน่วย', hint: '°C', w: 70),
              ]),
              Row(children: [
                tf(hdr, 'Header', hint: '7DF', caps: true, w: 80),
                const SizedBox(width: 12),
                Expanded(child: tf(cmd, 'คำสั่ง (hex)', hint: '015C / 221234 / 2101', caps: true)),
              ]),
              tf(fml, 'สูตร', hint: 'A-40   (A*256+B)/4   {A:7}', caps: true),
              Row(children: [
                Expanded(child: tf(lo, 'แถบต่ำสุด')),
                const SizedBox(width: 12),
                Expanded(child: tf(hi, 'แถบสูงสุด')),
                const SizedBox(width: 12),
                DropdownButton<int>(
                  value: dec,
                  items: const [
                    DropdownMenuItem(value: 0, child: Text('0')),
                    DropdownMenuItem(value: 1, child: Text('0.0')),
                    DropdownMenuItem(value: 2, child: Text('0.00')),
                  ],
                  onChanged: (v) => set(() => dec = v ?? 1),
                ),
              ]),
              const SizedBox(height: 12),
              const Text('คำตอบดิบล่าสุด (กดบันทึกก่อน แล้วรอสักครู่)', style: TextStyle(fontSize: 12)),
              SelectableText(raw.isEmpty ? tr('(ยังไม่มี)') : raw,
                  style: const TextStyle(fontFamily: 'monospace', fontSize: 12)),
              if (test.isNotEmpty)
                Text(test, style: const TextStyle(fontFamily: 'monospace', fontSize: 12, fontWeight: FontWeight.w700)),
              const SizedBox(height: 6),
              const Text(
                  'A B C D = ไบต์ข้อมูลหลังหัวคำตอบ (เช่น คำสั่ง 015C ตอบ 41 5C 7B -> A = 0x7B)\n'
                  'Header อื่นที่ไม่ใช่ 7DF (เช่น 7E1 กล่องเกียร์) ใช้ได้เฉพาะรถ CAN\n'
                  '7F ... = กล่องไม่รองรับคำสั่ง · NO DATA = ไม่มีใครตอบ',
                  style: TextStyle(fontSize: 11)),
            ]),
          ),
          actions: [
            TextButton(
              onPressed: () {
                s.setCustomPid(k, CustomPid(<String, dynamic>{}));
                Navigator.pop(ctx);
              },
              child: const Text('ลบ'),
            ),
            TextButton(onPressed: () => save(), child: const Text('บันทึก + ทดสอบ')),
            FilledButton(
              onPressed: () {
                save();
                Navigator.pop(ctx);
              },
              child: const Text('เสร็จ'),
            ),
          ],
        );
      }),
    );
    live?.cancel();
  }

  static String _num(double v) => v == v.roundToDouble() ? v.toStringAsFixed(0) : v.toString();
}


/// หน้าเปล่าหลังดูตัวอย่างโลโก้ -> กลับไปหน้าตั้งค่า
class _PreviewDone extends StatelessWidget {
  const _PreviewDone();
  @override
  Widget build(BuildContext context) {
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (Navigator.of(context).canPop()) Navigator.of(context).pop();
    });
    return const ColoredBox(color: Colors.black);
  }
}



/// แท็บ "ปรับค่า" ในเมนูหน้าปัด — gain / offset ที่ C3
class CalTab extends StatelessWidget {
  const CalTab({super.key, required this.settings, required this.hub});
  final AppSettings settings;
  final DataHub hub;

  @override
  Widget build(BuildContext context) => AnimatedBuilder(
        animation: settings,
        builder: (context, _) => ListView(
          padding: const EdgeInsets.only(bottom: 24),
          children: [
            if (settings.source == SourceType.c3)
              _C3CalSection(hub: hub)
            else ...[
              const _Header('ปรับค่าให้ตรง (gain / offset)'),
              const _Hint('ใช้ได้เมื่อเชื่อมต่อแบบ C3 (ESP32-C3 ผ่าน Bluetooth) — เลือกได้ที่เมนู การเชื่อมต่อ'),
            ],
          ],
        ),
      );
}

// ======================================================================
//  ปรับค่า (gain / offset) ที่ C3 — มีผลกับทุกจอ (S3 / CYD / แอป)
// ======================================================================
class _C3CalSection extends StatelessWidget {
  const _C3CalSection({required this.hub});
  final DataHub hub;

  static String _pct(int v) => 'x${(v / 1000).toStringAsFixed(3)}';

  @override
  Widget build(BuildContext context) {
    final cal = hub.c3cal;
    return ListenableBuilder(
      listenable: cal,
      builder: (context, _) {
        final rows = <Widget>[
          const _Header('ปรับค่าให้ตรง (gain / offset) ที่ C3'),
        ];
        if (!cal.have) {
          rows.add(const _Hint('ยังไม่ได้รับค่าจาก C3 — ต่อ C3 ให้ได้ก่อน (C3 ต้องเป็นเฟิร์มแวร์รุ่นใหม่)'));
          return Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: rows);
        }
        for (var i = 0; i < C3Cal.n; i++) {
          final div = C3Cal.ofsDiv(i), st = C3Cal.ofsStep(i), mx = C3Cal.ofsMax(i);
          final gi = (cal.g[i] * 1000).round(), oi = (cal.o[i] * div).round();
          final u = C3Cal.units[i];
          final changed = gi != 1000 || oi != 0;
          final undo = changed
              ? IconButton(
                  tooltip: 'คืนค่าเดิม',
                  icon: const Icon(Icons.undo, size: 20),
                  onPressed: () {
                    cal.g[i] = 1;
                    cal.setOfs(i, 0);
                  },
                )
              : const SizedBox(width: 40);
          final name = Text(C3Cal.names[i],
              style: TextStyle(fontSize: 15, color: changed ? kGreen : null, fontWeight: changed ? FontWeight.w700 : FontWeight.w600));
          final gain = Row(mainAxisSize: MainAxisSize.min, children: [
            const Text('gain', style: TextStyle(fontSize: 12, color: Colors.white54)),
            _mini(gi, 500, 2000, 5, (v) => cal.setGain(i, v / 1000), _pct),
          ]);
          final ofs = Row(mainAxisSize: MainAxisSize.min, children: [
            const Text('offset', style: TextStyle(fontSize: 12, color: Colors.white54)),
            _mini(oi, -mx, mx, st, (v) => cal.setOfs(i, v / div),
                (v) => '${v >= 0 ? '+' : ''}${div == 1 ? v : (v / div).toStringAsFixed(1)} $u'),
          ]);
          rows.add(LayoutBuilder(builder: (context, c) {
            final wide = c.maxWidth >= 760;
            return Padding(
              padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 2),
              child: wide
                  ? Row(children: [
                      SizedBox(width: 190, child: name),
                      gain,
                      const SizedBox(width: 16),
                      ofs,
                      undo,
                    ])
                  : Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                      Padding(padding: const EdgeInsets.only(top: 6), child: name),
                      Wrap(crossAxisAlignment: WrapCrossAlignment.center, spacing: 8, children: [gain, ofs, undo]),
                      const Divider(height: 8),
                    ]),
            );
          }));
        }
        rows.add(Padding(
          padding: const EdgeInsets.fromLTRB(16, 6, 16, 0),
          child: Wrap(spacing: 8, children: [
            OutlinedButton.icon(
              icon: const Icon(Icons.restart_alt),
              label: const Text('คืนค่าเดิมทั้งหมด'),
              onPressed: () async {
                final ok = await showDialog<bool>(
                  context: context,
                  builder: (c) => AlertDialog(
                    title: const Text('คืนค่าปรับทั้งหมด?'),
                    content: const Text('ทุกค่ากลับเป็น gain x1.000 / offset 0 (มีผลกับทุกจอ)'),
                    actions: [
                      TextButton(onPressed: () => Navigator.pop(c, false), child: const Text('ยกเลิก')),
                      FilledButton(onPressed: () => Navigator.pop(c, true), child: const Text('คืนค่า')),
                    ],
                  ),
                );
                if (ok == true) {
                  if (hub.settings.speedCalMode == 0) hub.settings.speedCal1000 = 1000;
                  cal.reset();
                }
              },
            ),
          ]),
        ));
        rows.add(_Hint('ค่าที่โชว์ = ค่าจากรถ x gain + offset · C3 จำไว้ในตัว ทุกจอ (S3 / CYD / แอป) ได้ค่าที่ปรับแล้วเหมือนกัน\n'
            'ความเร็ว = ค่าเดียวกับ "ชดเชยความเร็ว" ของแอป และ SPEED GAIN ของจอ S3 (แก้ที่ไหนก็ได้)'
            '${hub.settings.speedCalMode == 1 ? '\nตอนนี้ตั้งชดเชยความเร็วด้วย GPS อัตโนมัติ — แอปจะเขียนค่าความเร็วทับเมื่อเรียนรู้ครบ' : ''}\n'
            'AFR แบบ narrowband (โวลต์) C3 ไม่ปรับ · ${cal.connected ? 'ต่อ C3 อยู่ (ชุดค่าที่ ${cal.ver})' : 'ยังไม่ได้ต่อ C3 — แก้ค่าแล้วจะส่งไม่ได้'}'));
        return Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: rows);
      },
    );
  }

  static Widget _mini(int v, int mn, int mx, int st, ValueChanged<int> on, String Function(int) fmt) =>
      Row(mainAxisSize: MainAxisSize.min, children: [
        IconButton(
          visualDensity: VisualDensity.compact,
          icon: const Icon(Icons.remove_circle_outline, size: 22),
          onPressed: v - st >= mn ? () => on(v - st) : null,
        ),
        SizedBox(
          width: 92,
          child: Text(fmt(v), textAlign: TextAlign.center, style: const TextStyle(fontSize: 15, fontWeight: FontWeight.w700)),
        ),
        IconButton(
          visualDensity: VisualDensity.compact,
          icon: const Icon(Icons.add_circle_outline, size: 22),
          onPressed: v + st <= mx ? () => on(v + st) : null,
        ),
      ]);
}


// ======================================================================
//  อัปเดตเฟิร์มแวร์ C3 ทาง Bluetooth
// ======================================================================
class _C3OtaSection extends StatelessWidget {
  const _C3OtaSection({required this.hub});
  final DataHub hub;

  Future<void> _start(BuildContext context) async {
    final ota = hub.c3ota;
    final path = await Native.pickFile(mime: 'application/octet-stream');
    if (path == null || !context.mounted) return;
    Uint8List f;
    try {
      f = await File(path).readAsBytes();
    } catch (e) {
      if (context.mounted) _snack(context, 'อ่านไฟล์ไม่ได้: $e');
      return;
    }
    final err = ota.check(f);
    if (!context.mounted) return;
    if (err != null) {
      await showDialog<void>(
        context: context,
        builder: (c) => AlertDialog(
          title: const Text('ใช้ไฟล์นี้ไม่ได้'),
          content: Text(err),
          actions: [FilledButton(onPressed: () => Navigator.pop(c), child: const Text('ตกลง'))],
        ),
      );
      return;
    }
    final name = path.split('/').last;
    final go = await showDialog<bool>(
      context: context,
      builder: (c) => AlertDialog(
        title: const Text('อัปเดตเฟิร์มแวร์ C3?'),
        content: Text('ไฟล์: $name\nขนาด: ${(f.length / 1024).toStringAsFixed(0)} KB\n'
            'เฟิร์มแวร์ตอนนี้: ${ota.fwVer.isEmpty ? '-' : ota.fwVer}\n\n'
            'ใช้เวลาราว 1-3 นาที · ระหว่างนี้จอทุกจอจะไม่มีค่า\n'
            'ห้ามดับเครื่อง/ถอด C3 · ถ้าหลุดกลางทาง เฟิร์มแวร์เดิมยังอยู่ ลองใหม่ได้'),
        actions: [
          TextButton(onPressed: () => Navigator.pop(c, false), child: const Text('ยกเลิก')),
          FilledButton(onPressed: () => Navigator.pop(c, true), child: const Text('เริ่มอัปเดต')),
        ],
      ),
    );
    if (go == true) await ota.run(f);
  }

  @override
  Widget build(BuildContext context) {
    final ota = hub.c3ota;
    return ListenableBuilder(
      listenable: ota,
      builder: (context, _) {
        final ver = ota.fwVer.isEmpty
            ? (ota.linked ? 'ไม่ทราบ (เฟิร์มแวร์เก่า ยังไม่รองรับ — แฟลชผ่าน USB ครั้งแรกก่อน)' : 'ยังไม่ได้ต่อ C3')
            : '${ota.fwVer}  ·  ลงได้สูงสุด ${(ota.freeSpace / 1024).round()} KB';
        return Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
          const _Header('อัปเดตเฟิร์มแวร์ C3 (ทาง Bluetooth)'),
          ListTile(
            dense: true,
            leading: const Icon(Icons.memory),
            title: Text('เฟิร์มแวร์ใน C3: $ver'),
            trailing: ota.running
                ? OutlinedButton(onPressed: ota.cancel, child: const Text('ยกเลิก'))
                : FilledButton.icon(
                    icon: const Icon(Icons.system_update_alt),
                    label: const Text('เลือกไฟล์ .bin'),
                    onPressed: ota.linked && ota.fwVer.isNotEmpty ? () => _start(context) : null,
                  ),
          ),
          if (ota.running || ota.msg.isNotEmpty)
            Padding(
              padding: const EdgeInsets.fromLTRB(16, 0, 16, 6),
              child: Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
                if (ota.running) LinearProgressIndicator(value: ota.progress, minHeight: 8),
                const SizedBox(height: 4),
                Text(
                  ota.running && ota.kbs > 0
                      ? '${ota.msg}  ·  ${ota.kbs.toStringAsFixed(1)} KB/s  ·  เหลือ ~${ota.etaS} วิ'
                      : ota.msg,
                  style: TextStyle(
                      fontSize: 13,
                      color: ota.running ? null : (ota.ok ? kGreen : Colors.orangeAccent)),
                ),
              ]),
            ),
          const _Hint('ไฟล์: Arduino IDE → Sketch → Export Compiled Binary → ใช้ไฟล์ didi_c3_gateway.ino.bin (ไม่ใช่ .merged.bin)\n'
              'แอปตรวจไฟล์ให้ก่อน (ต้องเป็นของ ESP32-C3) · C3 ตรวจ MD5 ก่อนเปลี่ยน ไฟล์เสีย/หลุดกลางทาง = ใช้เฟิร์มแวร์เดิมต่อ'),
        ]);
      },
    );
  }
}
