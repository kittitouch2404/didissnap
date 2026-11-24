import 'dart:async';
import 'dart:typed_data';

import 'package:crypto/crypto.dart';
import 'package:flutter/foundation.dart';

import 'app_log.dart';
import 'native.dart';

/// อัปเดตเฟิร์มแวร์ C3 ทาง BLE: เลือกไฟล์ .bin (Arduino: Sketch → Export Compiled Binary → ไฟล์ .ino.bin)
/// ส่งทีละ 160 ไบต์แบบรอ C3 ตอบรับ → C3 ตรวจ MD5 → รีบูตเข้าเฟิร์มแวร์ใหม่
class C3Ota extends ChangeNotifier {
  static const _magic = 0x31444944;
  static const chunk = 160;

  bool linked = false;
  String fwVer = ''; // ว่าง = C3 ยังไม่ตอบ (เฟิร์มแวร์เก่า ไม่รองรับ)
  int freeSpace = 0;

  bool running = false, ok = false;
  double progress = 0;
  String msg = '';
  double kbs = 0;
  int etaS = 0;

  int _st = -1, _off = 0;
  String _stMsg = '';
  Completer<void>? _stWait;
  bool _cancel = false;

  /// แพ็กเก็ต DN_OTAST จาก C3
  void onStatus(Uint8List e) {
    if (e.length < 56) return;
    final b = ByteData.sublistView(e);
    _st = e[5];
    _off = b.getUint32(8, Endian.little);
    final size = b.getUint32(12, Endian.little);
    final end = e.indexOf(0, 16);
    _stMsg = String.fromCharCodes(e.sublist(16, end < 0 || end > 56 ? 56 : end));
    if (_st == 0) {
      fwVer = _stMsg;
      freeSpace = size;
      notifyListeners();
    }
    if (_st == 3 && running) AppLog.e('C3 OTA error: $_stMsg');
    final w = _stWait;
    if (w != null && !w.isCompleted) w.complete();
  }

  void onLink(bool on) {
    linked = on;
    if (on) {
      Timer(const Duration(milliseconds: 900), query);
    } else if (!running) {
      // ยังจำเวอร์ชันเดิมไว้โชว์ จนกว่าจะต่อใหม่
    }
    notifyListeners();
  }

  static Uint8List _pkt(int op, int off, [List<int> data = const []]) {
    final b = ByteData(12 + data.length);
    b.setUint32(0, _magic, Endian.little);
    b.setUint8(4, 14); // DN_OTA
    b.setUint8(5, op);
    b.setUint16(6, data.length, Endian.little);
    b.setUint32(8, off, Endian.little);
    final u = b.buffer.asUint8List();
    u.setRange(12, 12 + data.length, data);
    return u;
  }

  static Future<bool> _w(Uint8List p) async {
    try {
      return await Native.m.invokeMethod<bool>('c3WriteWait', {'b': p}) ?? false;
    } catch (_) {
      return false;
    }
  }

  Future<void> query() async {
    if (!linked || running) return;
    await _w(_pkt(0, 0));
  }

  /// ตรวจไฟล์ก่อนส่ง — คืนข้อความผิดพลาด หรือ null ถ้าใช้ได้
  String? check(Uint8List f) {
    if (f.length < 4096) return 'ไฟล์เล็กเกินไป — ไม่ใช่ไฟล์เฟิร์มแวร์';
    if (f[0] != 0xE9) return 'ไม่ใช่ไฟล์เฟิร์มแวร์ ESP32 (.bin)';
    // ไฟล์ merged (มี bootloader + partition table) ใช้ไม่ได้
    if (f.length > 0x8010 && f[0x8000] == 0xAA && f[0x8001] == 0x50) {
      return 'นี่คือไฟล์ .merged.bin — ให้ใช้ไฟล์ .ino.bin (ไฟล์ที่ไม่มีคำว่า merged / bootloader / partitions)';
    }
    final chip = f[12] | (f[13] << 8);
    if (chip != 5) {
      const names = {0: 'ESP32', 2: 'ESP32-S2', 9: 'ESP32-S3', 12: 'ESP32-C2', 13: 'ESP32-C6', 16: 'ESP32-H2'};
      return 'ไฟล์นี้สำหรับ ${names[chip] ?? 'ชิปอื่น ($chip)'} ไม่ใช่ ESP32-C3 — เลือกไฟล์ของ didi_c3_gateway';
    }
    if (freeSpace > 0 && f.length > freeSpace) {
      return 'ไฟล์ใหญ่ ${(f.length / 1024).round()} KB เกินพื้นที่ใน C3 (${(freeSpace / 1024).round()} KB) — ตั้ง Partition Scheme ให้มี OTA';
    }
    return null;
  }

  void cancel() => _cancel = true;

  Future<bool> _waitSt(Set<int> want, Duration t) async {
    final end = DateTime.now().add(t);
    while (DateTime.now().isBefore(end)) {
      if (want.contains(_st)) return true;
      _stWait = Completer<void>();
      await _stWait!.future.timeout(end.difference(DateTime.now()), onTimeout: () {});
    }
    return want.contains(_st);
  }

  void _set(String m, {double? p}) {
    msg = m;
    if (p != null) progress = p;
    notifyListeners();
  }

  Future<void> run(Uint8List f) async {
    if (running) return;
    running = true;
    ok = false;
    _cancel = false;
    progress = 0;
    kbs = 0;
    etaS = 0;
    _st = -1;
    try {
      if (!linked) throw 'ยังไม่ได้ต่อ C3';
      final md5hex = md5.convert(f).toString();
      AppLog.i('C3 OTA start ${f.length} B md5 $md5hex');
      _set('เริ่ม…');
      if (!await _w(_pkt(1, f.length, md5hex.codeUnits))) throw 'ส่งคำสั่งเริ่มไม่ได้';
      if (!await _waitSt({1, 3}, const Duration(seconds: 6))) throw 'C3 ไม่ตอบ — เฟิร์มแวร์ใน C3 ยังไม่รองรับ (ต้องแฟลชผ่าน USB ครั้งแรก)';
      if (_st == 3) throw 'C3: $_stMsg';
      final sw = Stopwatch()..start();
      var lastUi = 0;
      for (var off = 0; off < f.length; off += chunk) {
        if (_cancel) throw 'ยกเลิกแล้ว';
        if (_st == 3) throw 'C3: $_stMsg';
        final n = (f.length - off) < chunk ? f.length - off : chunk;
        final p = _pkt(2, off, Uint8List.sublistView(f, off, off + n));
        var sent = false;
        for (var t = 0; t < 3 && !sent; t++) {
          sent = await _w(p);
          if (!sent) await Future<void>.delayed(const Duration(milliseconds: 150));
        }
        if (!sent) throw 'ส่งไม่ผ่านที่ ${(off / 1024).round()} KB (สัญญาณหลุด?)';
        final ms = sw.elapsedMilliseconds;
        if (ms - lastUi > 250) {
          lastUi = ms;
          final done = off + n;
          kbs = ms > 0 ? done / 1.024 / ms : 0;
          etaS = kbs > 0 ? ((f.length - done) / 1024 / kbs).round() : 0;
          _set('กำลังส่ง ${(done / 1024).round()} / ${(f.length / 1024).round()} KB', p: done / f.length);
        }
      }
      _set('ตรวจไฟล์ (MD5)…', p: 1);
      if (!await _w(_pkt(3, f.length))) throw 'ส่งคำสั่งจบไม่ได้';
      if (!await _waitSt({2, 3}, const Duration(seconds: 15))) throw 'C3 ไม่ตอบหลังส่งครบ';
      if (_st == 3) throw 'C3: $_stMsg';
      ok = true;
      fwVer = '';
      AppLog.i('C3 OTA ok in ${(sw.elapsedMilliseconds / 1000).toStringAsFixed(0)} s');
      _set('สำเร็จ — C3 กำลังรีบูตเข้าเฟิร์มแวร์ใหม่ (ต่อกลับเองใน ~10 วิ)');
    } catch (e) {
      if (_st == 1) await _w(_pkt(4, 0)); // ยกเลิกฝั่ง C3 (เฟิร์มแวร์เดิมยังอยู่ ใช้ต่อได้)
      AppLog.e('C3 OTA fail: $e');
      _set('ไม่สำเร็จ: $e\nเฟิร์มแวร์เดิมใน C3 ยังอยู่ ใช้งานต่อได้');
    } finally {
      running = false;
      notifyListeners();
    }
  }
}
