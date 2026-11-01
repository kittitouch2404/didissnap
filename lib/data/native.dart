import 'package:flutter/services.dart';

/// ช่องทางคุยกับโค้ด Kotlin (MainActivity.kt)
class Native {
  static const MethodChannel m = MethodChannel('n1v65/native');
  static const EventChannel btStream = EventChannel('n1v65/bt/stream');

  /// ผูกการเชื่อมต่อเน็ตของแอปเข้ากับ WiFi
  /// (จำเป็นตอนต่อ WiFi ที่ไม่มีอินเทอร์เน็ต เช่น ESP32 / ELM327 WiFi
  /// ไม่งั้น Android อาจส่งข้อมูลออกทางเน็ตมือถือแทน)
  static Future<void> bindWifi() async {
    try {
      await m.invokeMethod('bindWifi', {'on': true});
    } catch (_) {}
  }
}
