import 'package:flutter/services.dart';

/// ช่องทางคุยกับโค้ด Kotlin (MainActivity.kt)
class Native {
  static const MethodChannel m = MethodChannel('didissnap/native');
  static const EventChannel btStream = EventChannel('didissnap/bt/stream');

  /// ผูกการเชื่อมต่อเน็ตของแอปเข้ากับ WiFi
  /// (จำเป็นตอนต่อ WiFi ที่ไม่มีอินเทอร์เน็ต เช่น ESP32 / ELM327 WiFi
  /// ไม่งั้น Android อาจส่งข้อมูลออกทางเน็ตมือถือแทน)
  static Future<bool> canOverlay() async {
    try {
      return (await m.invokeMethod<bool>('canOverlay')) ?? false;
    } catch (_) {
      return false;
    }
  }

  static Future<void> _call(String name) async {
    try {
      await m.invokeMethod(name);
    } catch (_) {}
  }

  static Future<void> requestOverlay() => _call('requestOverlay');
  static Future<void> batterySettings() => _call('batterySettings');
  static Future<void> requestNotif() => _call('requestNotif');

  /// ให้ฝั่ง Android เริ่ม/หยุด service และอัปเดตการแสดงบนหน้าล็อก
  static Future<void> syncAutoStart() => _call('syncAutoStart');

  static Future<void> bindWifi() async {
    try {
      await m.invokeMethod('bindWifi', {'on': true});
    } catch (_) {}
  }
}
