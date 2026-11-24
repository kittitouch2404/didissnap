import 'dart:async';
import 'dart:typed_data';

import 'package:flutter/foundation.dart';

/// ค่าปรับ gain / offset ที่เก็บใน C3 (C3 ปรับให้ก่อนส่ง → ทุกจอได้ค่าเดียวกัน)
/// ค่าที่โชว์ = ค่าจากรถ x gain + offset
class C3Cal extends ChangeNotifier {
  static const n = 13;
  static const iRpm = 0, iSpd = 1, iVolt = 4, iTps = 6, iAfr = 9;
  static const names = [
    'รอบเครื่อง', 'ความเร็ว', 'น้ำ (ECT)', 'อากาศเข้า (IAT)', 'แบตเตอรี่', 'โหลด', 'TPS', 'MAP', 'MAF', 'AFR',
    'องศาจุดระเบิด', 'น้ำมันเครื่อง', 'CVT TEMP',
  ];
  static const units = ['rpm', 'km/h', '°C', '°C', 'V', '%', '%', 'kPa', 'g/s', '', '°', '°C', '°C'];

  /// offset เก็บเป็นจำนวนเต็ม x ค่านี้ (แบต / AFR ปรับทีละ 0.1)
  static double ofsDiv(int i) => i == iVolt || i == iAfr || i == 8 ? 10 : 1;
  static int ofsStep(int i) => i == iRpm ? 10 : 1;
  static int ofsMax(int i) => switch (i) { iRpm => 500, iVolt => 30, iAfr => 30, _ => 30 };

  final List<double> g = List<double>.filled(n, 1), o = List<double>.filled(n, 0);
  bool have = false;
  int ver = -1;
  DateTime at = DateTime.fromMillisecondsSinceEpoch(0);

  /// ตั้งโดย C3Source ตอนต่อ BLE อยู่ (null = ยังไม่ได้ต่อ)
  Future<bool> Function(Uint8List)? sender;
  Timer? _deb;
  bool get editing => _deb?.isActive ?? false;
  bool get connected => sender != null;

  bool isDefault() {
    for (var i = 0; i < n; i++) {
      if ((g[i] - 1).abs() > 1e-4 || o[i].abs() > 1e-4) return false;
    }
    return true;
  }

  /// แพ็กเก็ต DN_CAL จาก C3 — คืน true ถ้าเป็นค่าชุดใหม่
  bool fromPacket(ByteData b) {
    at = DateTime.now();
    final v = b.getUint16(6, Endian.little);
    if (editing) return false; // กำลังแก้อยู่ — รอส่งให้ C3 ก่อน
    final isNew = v != ver || !have;
    for (var i = 0; i < n; i++) {
      g[i] = b.getFloat32(8 + i * 4, Endian.little);
      o[i] = b.getFloat32(8 + n * 4 + i * 4, Endian.little);
    }
    ver = v;
    have = true;
    if (isNew) notifyListeners();
    return isNew;
  }

  Uint8List packet() {
    final b = ByteData(8 + n * 8);
    b.setUint32(0, 0x31444944, Endian.little); // "DID1"
    b.setUint8(4, 13); // DN_CAL
    b.setUint8(5, 1); // set
    b.setUint16(6, ver < 0 ? 0 : ver, Endian.little);
    for (var i = 0; i < n; i++) {
      b.setFloat32(8 + i * 4, g[i], Endian.little);
      b.setFloat32(8 + n * 4 + i * 4, o[i], Endian.little);
    }
    return b.buffer.asUint8List();
  }

  void setGain(int i, double v) {
    g[i] = v;
    _changed();
  }

  void setOfs(int i, double v) {
    o[i] = v;
    _changed();
  }

  void reset() {
    for (var i = 0; i < n; i++) {
      g[i] = 1;
      o[i] = 0;
    }
    _changed();
  }

  void _changed() {
    notifyListeners();
    _deb?.cancel();
    _deb = Timer(const Duration(milliseconds: 400), push);
  }

  Future<void> push() async {
    _deb?.cancel();
    final s = sender;
    if (s != null) await s(packet());
  }
}
