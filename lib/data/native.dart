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
  /// เสียงเตือนแบบที่เลือก (ดู AlertEngine.sounds)  text = คำพูดของเสียงพูด
  static Future<void> alertSound(int id, int vol, String text) async {
    try {
      await m.invokeMethod('alertSound', {'id': id, 'vol': vol, 'text': text});
    } catch (_) {}
  }

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

  static const EventChannel gps = EventChannel('didissnap/gps');

  static Future<String> deviceInfo() async {
    try {
      return (await m.invokeMethod<String>('deviceInfo')) ?? '?';
    } catch (_) {
      return '?';
    }
  }

  static Future<bool> installApk(String path) async {
    try {
      return (await m.invokeMethod<bool>('installApk', {'path': path})) ?? false;
    } catch (_) {
      return false;
    }
  }

  static Future<bool> openUrl(String url) async {
    try {
      return (await m.invokeMethod<bool>('openUrl', {'url': url})) ?? false;
    } catch (_) {
      return false;
    }
  }

  /// ขอสิทธิ์ GPS (true = มีสิทธิ์แล้ว)
  static Future<bool> gpsPermission() async {
    try {
      return (await m.invokeMethod<bool>('gpsPermission')) ?? false;
    } catch (_) {
      return false;
    }
  }

  /// เลือกไฟล์ใดก็ได้ -> path ไฟล์ที่ก๊อปไว้ใน cache
  static Future<String?> pickFile({String mime = '*/*'}) async {
    try {
      return await m.invokeMethod<String>('pickFile', {'mime': mime});
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
