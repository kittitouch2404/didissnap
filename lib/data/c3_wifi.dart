import 'dart:typed_data';

import 'package:flutter/foundation.dart';

/// สถานะ/ตั้งค่า WiFi ของ C3 (ให้ C3 ต่อฮอตสปอตมือถือ → จอรถที่ไม่มี BLE ต่อ C3 ทาง WiFi ได้)
class C3Wifi extends ChangeNotifier {
  static const _magic = 0x31444944;
  Future<bool> Function(Uint8List)? sender;
  bool viaWifi = false; // แอปนี้ต่อ C3 ทาง WiFi อยู่
  bool have = false;
  int state = 0; // 0 ปิด · 1 กำลังต่อ · 2 ต่อแล้ว
  int rssi = 0, clients = 0;
  String ip = '', ssid = '';

  bool get connected => sender != null;

  void onStatus(Uint8List e) {
    if (e.length < 45) return;
    have = true;
    state = e[5];
    rssi = ByteData.sublistView(e).getInt8(6);
    clients = e[7];
    ip = state == 2 ? '${e[8]}.${e[9]}.${e[10]}.${e[11]}' : '';
    final b = <int>[];
    for (var i = 12; i < 45 && e[i] != 0; i++) {
      b.add(e[i]);
    }
    ssid = String.fromCharCodes(b);
    notifyListeners();
  }

  Uint8List _pkt(int op, String ssid, String pass) {
    final b = ByteData(106);
    b.setUint32(0, _magic, Endian.little);
    b.setUint8(4, 18); // DN_WIFI
    b.setUint8(5, op);
    final s = ssid.codeUnits.take(32).toList(), p = pass.codeUnits.take(64).toList();
    for (var i = 0; i < s.length; i++) {
      b.setUint8(8 + i, s[i] & 0xFF);
    }
    for (var i = 0; i < p.length; i++) {
      b.setUint8(41 + i, p[i] & 0xFF);
    }
    return b.buffer.asUint8List();
  }

  Future<bool> query() async => await sender?.call(_pkt(0, '', '')) ?? false;
  Future<bool> set(String ssid, String pass) async => await sender?.call(_pkt(1, ssid, pass)) ?? false;
  Future<bool> off() async => await sender?.call(_pkt(2, '', '')) ?? false;
}
