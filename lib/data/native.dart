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

  /// แอปถูกเปิดเองเพราะอะไร (boot / wake / acc / power / bluetooth) null = ผู้ใช้เปิดเอง
  static Future<String?> launchReason() async {
    try {
      return await m.invokeMethod<String>('launchReason');
    } catch (_) {
      return null;
    }
  }

  /// เลือกไฟล์เสียงจากเครื่อง -> path ของไฟล์ที่ก๊อปไว้ในแอป (null = ยกเลิก)
  static Future<String?> pickAudio() async {
    try {
      return await m.invokeMethod<String>('pickAudio');
    } catch (_) {
      return null;
    }
  }

  /// เสียงเตือน (0 บี๊บ, 1 เตือนแรง, 2 shift, 3 เตือนยาว)
  static Future<void> tone(int kind, int ms, int vol) async {
    try {
      await m.invokeMethod('tone', {'kind': kind, 'ms': ms, 'vol': vol});
    } catch (_) {}
  }

  /// ความสว่างจอของแอป 0..1  (ติดลบ = ตามระบบ)
  static Future<void> brightness(double v) async {
    try {
      await m.invokeMethod('brightness', {'v': v});
    } catch (_) {}
  }

  static Future<bool> saveToDownloads(String path, String name, String mime) async {
    try {
      return (await m.invokeMethod<bool>('saveToDownloads', {'path': path, 'name': name, 'mime': mime})) ?? false;
    } catch (_) {
      return false;
    }
  }

  static Future<String?> pickText() async {
    try {
      return await m.invokeMethod<String>('pickText');
    } catch (_) {
      return null;
    }
  }

  static Future<void> bindWifi() async {
    try {
      await m.invokeMethod('bindWifi', {'on': true});
    } catch (_) {}
  }
}
