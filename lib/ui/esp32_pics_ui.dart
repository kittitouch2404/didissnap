import 'dart:async';
import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:image/image.dart' as img;
import 'package:image_picker/image_picker.dart';

import '../data/esp32_pics.dart';
import '../data/hub.dart';
import 'paint_utils.dart';

const int kBoardW = 480, kBoardH = 272;
const int kMaxUpload = 4 * 1024 * 1024; // บอร์ด S3 รับได้ 4 MB (บอร์ดรุ่นเก่าจะตอบ "ใหญ่เกิน" เอง)
// ช่องรูปในบอร์ด: 0 = หน้า 13 (ตอนนี้เป็นหน้าตัวละครที่ฝังในบอร์ด ไม่ใช้รูปที่อัป), 1 = หน้า 14, 2 = หน้า 15
const _slotNames = ['หน้า 13 · Character', 'หน้า 14 · Car run', 'หน้า 15 · Photo'];
const _slotShown = [1, 2]; // ช่องที่เลือกรูปได้

String fmtBytes(int b) =>
    b >= 1048576 ? '${(b / 1048576).toStringAsFixed(1)} MB' : '${(b / 1024).round()} KB';

bool _isGif(Uint8List b) => b.length > 3 && b[0] == 0x47 && b[1] == 0x49 && b[2] == 0x46;

// ============================================================ แปลงรูป (ทำใน isolate แยก ไม่ให้แอปค้าง)
class PicJob {
  PicJob(this.src, this.x, this.y, this.w, this.h);
  final Uint8List src;
  final double x, y, w, h; // ส่วนที่ครอป (0..1 ของภาพ)
}

Uint8List processPic(PicJob j) {
  final gif = _isGif(j.src);
  img.Image? im;
  if (gif) {
    final info = img.GifDecoder().startDecode(j.src);
    if (info == null) throw Exception('อ่านไฟล์ GIF ไม่ได้');
    // GIF ที่ขนาด 480×272 อยู่แล้วและไม่ได้ครอป → ส่งไฟล์เดิมเลย (เข้ารหัสใหม่ทุกเฟรมจะใหญ่ขึ้นหลายเท่า)
    final full = j.x < 0.002 && j.y < 0.002 && j.w > 0.996 && j.h > 0.996;
    if (full && info.width == kBoardW && info.height == kBoardH) return j.src;
    if (info.numFrames * info.width * info.height > 40000000) {
      throw Exception('GIF ใหญ่/ยาวเกินไป (${info.numFrames} เฟรม ${info.width}×${info.height}) '
          '— ลองตัดให้สั้นลงหรือย่อก่อน');
    }
    im = img.decodeGif(j.src);
  } else {
    im = img.decodeImage(j.src);
    if (im != null) im = img.bakeOrientation(im);
  }
  if (im == null) throw Exception('อ่านรูปนี้ไม่ได้ (รองรับ JPG, PNG, WebP, GIF)');
  final x = (j.x * im.width).round().clamp(0, im.width - 1);
  final y = (j.y * im.height).round().clamp(0, im.height - 1);
  final w = (j.w * im.width).round().clamp(1, im.width - x);
  final h = (j.h * im.height).round().clamp(1, im.height - y);
  im = img.copyCrop(im, x: x, y: y, width: w, height: h);
  im = img.copyResize(im,
      width: kBoardW, height: kBoardH, interpolation: img.Interpolation.average);
  if (gif && im.numFrames > 1) return img.encodeGif(im, samplingFactor: 10);
  return img.encodeJpg(im, quality: 85);
}

// ============================================================ หน้าครอป 480×272
class PicCropPage extends StatefulWidget {
  const PicCropPage({super.key, required this.bytes, required this.iw, required this.ih});
  final Uint8List bytes;
  final int iw, ih;
  @override
  State<PicCropPage> createState() => _PicCropPageState();
}

class _PicCropPageState extends State<PicCropPage> {
  final _tc = TransformationController();
  double _cw = 0, _ch = 0, _vw = 0, _vh = 0;
  bool _init = false;

  @override
  void dispose() {
    _tc.dispose();
    super.dispose();
  }

  Rect _crop() {
    final m = _tc.value;
    final sc = m.getMaxScaleOnAxis();
    final tx = m.storage[12], ty = m.storage[13];
    final x = (-tx / sc) / _cw, y = (-ty / sc) / _ch;
    final w = (_vw / sc) / _cw, h = (_vh / sc) / _ch;
    return Rect.fromLTWH(x.clamp(0.0, 1.0), y.clamp(0.0, 1.0), w.clamp(0.01, 1.0), h.clamp(0.01, 1.0));
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.black,
      appBar: AppBar(
        toolbarHeight: 44,
        title: const Text('เลือกส่วนที่จะโชว์บนจอ', style: TextStyle(fontSize: 16)),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context, _crop()),
            child: const Text('ใช้ส่วนนี้', style: TextStyle(fontWeight: FontWeight.w700)),
          ),
        ],
      ),
      body: SafeArea(
        child: LayoutBuilder(builder: (context, box) {
          final maxW = math.min(box.maxWidth - 24, (box.maxHeight - 90) * kBoardW / kBoardH);
          if (!_init) {
            _vw = maxW;
            _vh = maxW * kBoardH / kBoardW;
            final s0 = math.max(_vw / widget.iw, _vh / widget.ih);
            _cw = widget.iw * s0;
            _ch = widget.ih * s0;
            _tc.value = Matrix4.translationValues((_vw - _cw) / 2, (_vh - _ch) / 2, 0);
            _init = true;
          }
          return Column(mainAxisAlignment: MainAxisAlignment.center, children: [
            Container(
              decoration: BoxDecoration(border: Border.all(color: kAccent, width: 2)),
              child: SizedBox(
                width: _vw,
                height: _vh,
                child: ClipRect(
                  child: InteractiveViewer(
                    transformationController: _tc,
                    constrained: false,
                    minScale: 1,
                    maxScale: 6,
                    boundaryMargin: EdgeInsets.zero,
                    child: SizedBox(
                      width: _cw,
                      height: _ch,
                      child: Image.memory(widget.bytes, fit: BoxFit.fill, gaplessPlayback: true),
                    ),
                  ),
                ),
              ),
            ),
            const SizedBox(height: 12),
            const Text('ลากเพื่อเลื่อน · ถ่างสองนิ้วเพื่อซูม\nกรอบนี้ = จอบอร์ด 480×272 พอดี',
                textAlign: TextAlign.center, style: TextStyle(color: Colors.white60, fontSize: 13)),
          ]);
        }),
      ),
    );
  }
}

// ============================================================ ส่วน "รูปบนจอ ESP32" ในแท็บจอ ESP32
class Esp32PicsSection extends StatefulWidget {
  const Esp32PicsSection({super.key, required this.host, required this.redline});
  final String host;
  final int redline;
  @override
  State<Esp32PicsSection> createState() => _Esp32PicsSectionState();
}

class _Esp32PicsSectionState extends State<Esp32PicsSection> {
  late Esp32Pics _api = Esp32Pics(widget.host);
  BoardPics? _p;
  bool _busy = false;
  String? _err, _step;
  double? _prog;
  int _bust = 0; // ให้รูปย่อโหลดใหม่หลังอัปไฟล์ชื่อเดิม

  @override
  void initState() {
    super.initState();
    _run(_api.list);
  }

  @override
  void didUpdateWidget(covariant Esp32PicsSection old) {
    super.didUpdateWidget(old);
    if (old.host != widget.host) {
      _api = Esp32Pics(widget.host);
      _run(_api.list);
    }
  }

  void _snack(String m) => ScaffoldMessenger.of(context)
    ..hideCurrentSnackBar()
    ..showSnackBar(SnackBar(content: Text(m)));

  Future<void> _run(Future<BoardPics> Function() f, [String? ok]) async {
    setState(() {
      _busy = true;
      _err = null;
    });
    try {
      final p = await f();
      if (!mounted) return;
      setState(() => _p = p);
      if (ok != null) _snack(ok);
    } catch (e) {
      if (mounted) setState(() => _err = errText(e));
    } finally {
      if (mounted) {
        setState(() {
          _busy = false;
          _step = null;
          _prog = null;
        });
      }
    }
  }

  Future<void> _add() async {
    final x = await ImagePicker().pickImage(source: ImageSource.gallery, requestFullMetadata: false);
    if (x == null || !mounted) return;
    final src = await x.readAsBytes();
    int iw, ih;
    try {
      final codec = await ui.instantiateImageCodec(src);
      final fr = await codec.getNextFrame();
      iw = fr.image.width;
      ih = fr.image.height;
      fr.image.dispose();
      codec.dispose();
    } catch (_) {
      _snack('เปิดรูปนี้ไม่ได้');
      return;
    }
    if (!mounted) return;
    final crop = await Navigator.push<Rect>(
        context, MaterialPageRoute(builder: (_) => PicCropPage(bytes: src, iw: iw, ih: ih)));
    if (crop == null || !mounted) return;

    setState(() {
      _busy = true;
      _err = null;
      _step = _isGif(src) ? 'กำลังย่อ GIF ทุกเฟรมเป็น 480×272 ...' : 'กำลังย่อรูปเป็น 480×272 ...';
    });
    Uint8List out;
    try {
      out = await compute(processPic, PicJob(src, crop.left, crop.top, crop.width, crop.height));
    } catch (e) {
      if (mounted) {
        setState(() {
          _busy = false;
          _step = null;
          _err = errText(e);
        });
      }
      return;
    }
    if (!mounted) return;
    final gif = _isGif(out);
    final p = _p;
    if (out.length > kMaxUpload) {
      setState(() {
        _busy = false;
        _step = null;
        _err = 'ไฟล์หลังย่อยังใหญ่ ${fmtBytes(out.length)} (เกิน ${fmtBytes(kMaxUpload)}) — ใช้ GIF ที่สั้นลง';
      });
      return;
    }
    if (p != null && p.total > 0 && out.length + 24 * 1024 > p.free) {
      setState(() {
        _busy = false;
        _step = null;
        _err = 'พื้นที่ในบอร์ดเหลือ ${fmtBytes(p.free)} ไม่พอสำหรับไฟล์ ${fmtBytes(out.length)} — ลบรูปเก่าก่อน';
      });
      return;
    }
    final name = 'p${DateTime.now().millisecondsSinceEpoch.toRadixString(36)}${gif ? '.gif' : '.jpg'}';
    setState(() {
      _step = 'กำลังส่ง ${fmtBytes(out.length)} เข้าบอร์ด ...';
      _prog = 0;
    });
    await _run(
        () => _api.upload(name, out, progress: (v) {
              if (mounted) setState(() => _prog = v);
            }));
    if (!mounted || _err != null) return;
    _bust++;
    // ถามว่าจะใช้รูปนี้ที่ไหน
    final where = await showDialog<int>(
      context: context,
      builder: (c) => SimpleDialog(
        title: const Text('ส่งรูปแล้ว ✓ ใช้รูปนี้ที่ไหน?'),
        children: [
          for (final i in _slotShown)
            SimpleDialogOption(onPressed: () => Navigator.pop(c, i), child: Text(_slotNames[i])),
          SimpleDialogOption(
              onPressed: () => Navigator.pop(c, -1),
              child: const Text('หน้า 16 · รูปตามรอบ อย่างเดียว')),
        ],
      ),
    );
    if (where != null && where >= 0) await _run(() => _api.setSlot(where, name), 'ตั้งเป็น${_slotNames[where]}แล้ว');
  }

  Future<void> _delete(BoardPic p) async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (c) => AlertDialog(
        title: const Text('ลบรูปนี้ออกจากบอร์ด?'),
        content: Text(p.name),
        actions: [
          TextButton(onPressed: () => Navigator.pop(c, false), child: const Text('ยกเลิก')),
          FilledButton(onPressed: () => Navigator.pop(c, true), child: const Text('ลบ')),
        ],
      ),
    );
    if (ok == true) await _run(() => _api.delete(p.name), 'ลบแล้ว');
  }

  // ช่วงรอบของแต่ละรูป (แสดงให้ดู)
  Map<String, String> _ranges(BoardPics p) {
    final on = p.pics.where((e) => e.on).toList();
    final r = <String, String>{};
    if (on.isEmpty) return r;
    if (on.length == 1) {
      r[on.first.name] = 'แสดงตลอด';
      return r;
    }
    final List<(String, int)> st = p.auto
        ? [for (var i = 0; i < on.length; i++) (on[i].name, (widget.redline * i / on.length).round())]
        : ([for (final e in on) (e.name, e.rpm)]..sort((a, b) => a.$2.compareTo(b.$2)));
    for (var i = 0; i < st.length; i++) {
      r[st[i].$1] = i == st.length - 1 ? '${st[i].$2} rpm ขึ้นไป' : '${st[i].$2}–${st[i + 1].$2} rpm';
    }
    return r;
  }

  Widget _thumb(String n) => ClipRRect(
        borderRadius: BorderRadius.circular(4),
        child: Image.network('${_api.imageUrl(n)}?v=$_bust',
            width: 84,
            height: 48,
            fit: BoxFit.cover,
            gaplessPlayback: true,
            errorBuilder: (_, __, ___) =>
                Container(width: 84, height: 48, color: Colors.white10, child: const Icon(Icons.image, size: 18))),
      );

  @override
  Widget build(BuildContext context) {
    final p = _p;
    final hdr = (String t) => Padding(
          padding: const EdgeInsets.fromLTRB(16, 14, 16, 4),
          child: Text(t, style: const TextStyle(fontSize: 13, color: kAccent, fontWeight: FontWeight.w700)),
        );
    final rng = p == null ? const <String, String>{} : _ranges(p);
    return Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
      hdr('รูปบนจอ ESP32'),
      if (p != null && p.total > 0)
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
          child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
            Text('ใช้ไป ${fmtBytes(p.used)} / ${fmtBytes(p.total)} · ${p.pics.length} รูป (สูงสุด ${p.maxPics})',
                style: const TextStyle(fontSize: 12, color: Colors.white60)),
            const SizedBox(height: 4),
            LinearProgressIndicator(value: p.total == 0 ? 0 : p.used / p.total, color: kAccent),
          ]),
        ),
      Padding(
        padding: const EdgeInsets.fromLTRB(16, 6, 16, 2),
        child: Row(children: [
          FilledButton.icon(
              onPressed: _busy ? null : _add, icon: const Icon(Icons.add_photo_alternate), label: const Text('เพิ่มรูป')),
          const SizedBox(width: 8),
          OutlinedButton(onPressed: _busy ? null : () => _run(_api.list), child: const Text('โหลดใหม่')),
        ]),
      ),
      const Padding(
        padding: EdgeInsets.fromLTRB(16, 2, 16, 6),
        child: Text('JPG/PNG/GIF จากแกลเลอรี · แอปครอปและย่อเป็น 480×272 ให้ แล้วส่งผ่าน WiFi "Didissnap"',
            style: TextStyle(fontSize: 12, color: Colors.white54)),
      ),
      if (_busy) ...[
        if (_step != null)
          Padding(
              padding: const EdgeInsets.symmetric(horizontal: 16),
              child: Text(_step!, style: const TextStyle(fontSize: 13))),
        Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
            child: LinearProgressIndicator(value: _prog)),
      ],
      if (_err != null)
        Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
            child: Text(_err!, style: const TextStyle(color: kRed))),
      if (p != null) ...[
        for (final i in _slotShown)
          ListTile(
            dense: true,
            leading: p.slots[i].isEmpty ? const SizedBox(width: 84, child: Icon(Icons.photo_outlined)) : _thumb(p.slots[i]),
            title: Text(_slotNames[i]),
            trailing: DropdownButton<String>(
              value: p.pics.any((e) => e.name == p.slots[i]) ? p.slots[i] : '',
              items: [
                const DropdownMenuItem(value: '', child: Text('รูปเดิมในบอร์ด')),
                for (final e in p.pics) DropdownMenuItem(value: e.name, child: Text(e.name)),
              ],
              onChanged: _busy ? null : (v) => _run(() => _api.setSlot(i, v ?? '')),
            ),
          ),
        hdr('หน้า 16 · รูปตามรอบ'),
        SwitchListTile(
          dense: true,
          title: const Text('แบ่งช่วงรอบอัตโนมัติ (เท่า ๆ กันถึง Redline)'),
          subtitle: Text(p.auto ? 'ปิดเพื่อกำหนดรอบเริ่มของแต่ละรูปเอง' : 'กำหนดรอบเริ่มเองทีละรูป'),
          value: p.auto,
          onChanged: _busy ? null : (v) => _run(() => _api.setAuto(v)),
        ),
        if (p.pics.isEmpty)
          const Padding(
              padding: EdgeInsets.all(16),
              child: Text('ยังไม่มีรูปในบอร์ด — กด "เพิ่มรูป"', style: TextStyle(color: Colors.white54))),
        for (var i = 0; i < p.pics.length; i++)
          Builder(builder: (_) {
            final e = p.pics[i];
            return ListTile(
              dense: true,
              leading: Opacity(opacity: e.on ? 1 : .35, child: _thumb(e.name)),
              title: Text('${e.isGif ? 'GIF' : 'รูป'} ${i + 1} · ${fmtBytes(e.size)}'),
              subtitle: !e.on
                  ? const Text('ไม่ใช้ในหน้ารูปตามรอบ')
                  : p.auto
                      ? Text(rng[e.name] ?? '')
                      : Row(children: [
                          const Text('เริ่มที่ '),
                          InkWell(
                              onTap: _busy ? null : () => _run(() => _api.setRpm(e.name, rpm: math.max(0, e.rpm - 250))),
                              child: const Icon(Icons.remove, size: 18)),
                          Padding(
                              padding: const EdgeInsets.symmetric(horizontal: 6),
                              child: Text('${e.rpm}', style: const TextStyle(fontWeight: FontWeight.w700))),
                          InkWell(
                              onTap: _busy ? null : () => _run(() => _api.setRpm(e.name, rpm: e.rpm + 250)),
                              child: const Icon(Icons.add, size: 18)),
                          const Text(' rpm'),
                        ]),
              trailing: Row(mainAxisSize: MainAxisSize.min, children: [
                Switch(value: e.on, onChanged: _busy ? null : (v) => _run(() => _api.setRpm(e.name, on: v))),
                PopupMenuButton<String>(
                  enabled: !_busy,
                  onSelected: (a) {
                    if (a == 'up') _run(() => _api.move(e.name, -1));
                    if (a == 'down') _run(() => _api.move(e.name, 1));
                    if (a == 'del') _delete(e);
                  },
                  itemBuilder: (_) => [
                    if (i > 0) const PopupMenuItem(value: 'up', child: Text('เลื่อนขึ้น')),
                    if (i < p.pics.length - 1) const PopupMenuItem(value: 'down', child: Text('เลื่อนลง')),
                    const PopupMenuItem(value: 'del', child: Text('ลบออกจากบอร์ด')),
                  ],
                ),
              ]),
            );
          }),
      ],
    ]);
  }
}
