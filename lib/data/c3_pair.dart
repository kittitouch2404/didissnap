import 'dart:math';
import 'dart:typed_data';

import 'package:flutter/foundation.dart';

import '../settings.dart';

/// จับคู่แอปกับ C3 ตัวเดียว (กันต่อผิดคันตอนจอดรวมกัน) — ดู didinow.h (DN_AUTH / DN_AUTHST)
///  - แอปมี "กุญแจ" 8 ไบต์ (สุ่มครั้งแรก) ส่งให้ C3 ทุกครั้งที่ต่อ
///  - C3 รู้จักกุญแจ = ส่งค่าให้ · ไม่รู้จักและไม่ได้เปิดโหมดจับคู่ = ปฏิเสธ
class C3Pair extends ChangeNotifier {
  C3Pair(this.s);
  final AppSettings s;
  static const _magic = 0x31444944;

  /// ส่งคำสั่งให้ C3 (C3Source ตั้งให้ตอนต่ออยู่)
  Future<bool> Function(Uint8List)? sender;

  int res = -1; // ผลล่าสุด: -1 ยังไม่รู้ · 0 ปฏิเสธ · 1 ผ่าน · 2 จับคู่ใหม่
  bool legacy = false; // C3 เฟิร์มแวร์เก่า (ไม่มีระบบจับคู่)
  String c3 = ''; // ชื่อ C3 ที่ต่ออยู่
  int open = 0; // วินาทีที่โหมดจับคู่เหลือ (-1 = เปิดตลอด เพราะยังไม่เคยจับคู่)
  DateTime openAt = DateTime.fromMillisecondsSinceEpoch(0);
  List<String> screens = const [];
  List<int> screenProto = const [];
  int legacyN = 0; // จอรุ่นเก่า (ไม่มีจับคู่) พยายามต่อใน 10 นาทีล่าสุด
  String fw = ''; // เวอร์ชันเฟิร์มแวร์ C3 (จากผลจับคู่)
  static const proto = 2; // DN_PROTO
  DateTime deniedAt = DateTime.fromMillisecondsSinceEpoch(0);

  bool get paired => s.c3PairAddr.isNotEmpty;
  bool get connected => sender != null;

  int get openLeft {
    if (open < 0) return -1;
    final l = open - DateTime.now().difference(openAt).inSeconds;
    return l > 0 ? l : 0;
  }

  List<int> get token {
    var t = s.c3Token;
    if (t.length != 16) {
      final r = Random.secure();
      t = List.generate(8, (_) => r.nextInt(256).toRadixString(16).padLeft(2, '0')).join();
      s.c3Token = t;
    }
    return [for (var i = 0; i < 16; i += 2) int.parse(t.substring(i, i + 2), radix: 16)];
  }

  Uint8List packet(int op) {
    final b = ByteData(24);
    b.setUint32(0, _magic, Endian.little);
    b.setUint8(4, 16); // DN_AUTH
    b.setUint8(5, op);
    b.setUint8(6, proto);
    final t = token;
    for (var i = 0; i < 8; i++) {
      b.setUint8(8 + i, t[i]);
    }
    const name = 'APP';
    for (var i = 0; i < name.length; i++) {
      b.setUint8(16 + i, name.codeUnitAt(i));
    }
    return b.buffer.asUint8List();
  }

  static String _str(Uint8List e, int off, int len) =>
      String.fromCharCodes(e.sublist(off, min(off + len, e.length)).takeWhile((c) => c != 0));

  /// แพ็กเก็ต DN_AUTHST จาก C3 · คืนผล (0 = ปฏิเสธ)
  int onStatus(Uint8List e, String addr) {
    if (e.length < 24) return -1;
    res = e[5];
    final o = e[6];
    open = o == 255 ? -1 : o * 2;
    openAt = DateTime.now();
    final n = e[7];
    c3 = _str(e, 8, 16);
    screens = [for (var i = 0; i < n && 24 + i * 8 < e.length; i++) _str(e, 24 + i * 8, 8)];
    screenProto = e.length >= 96 ? [for (var i = 0; i < n; i++) e[88 + i]] : const [];
    legacyN = e.length >= 97 ? e[96] : 0;
    if (e.length >= 113) fw = _str(e, 97, 16);
    legacy = false;
    if (res == 0) {
      deniedAt = DateTime.now();
    } else if (addr.isNotEmpty && (s.c3PairAddr.toUpperCase() != addr.toUpperCase() || s.c3PairName != c3)) {
      s.c3PairAddr = addr;
      s.c3PairName = c3;
    }
    notifyListeners();
    return res;
  }

  void setLegacy() {
    legacy = true;
    notifyListeners();
  }

  void linkChanged() => notifyListeners();

  /// ให้ C3 เปิดรับจอใหม่ 3 นาที
  Future<bool> openPairing() async => await sender?.call(packet(1)) ?? false;

  /// ล้างจออื่นทั้งหมดที่จับคู่กับ C3 (เหลือแอปนี้)
  Future<bool> clearOthers() async => await sender?.call(packet(2)) ?? false;

  /// ลืม C3 ตัวนี้ (บอก C3 ด้วยถ้าต่ออยู่) → หา C3 ที่เปิดโหมดจับคู่ใหม่
  Future<void> forget() async {
    await sender?.call(packet(3));
    s.c3PairAddr = '';
    s.c3PairName = '';
    res = -1;
    screens = const [];
    notifyListeners();
  }
}
