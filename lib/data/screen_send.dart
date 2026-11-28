import 'package:flutter/foundation.dart';

import 'native.dart';

/// ส่งไฟล์เข้าจอ 7B ทาง Bluetooth BLE (ต่อจอตรง ครั้งเดียวแล้วตัด) — ฝั่ง Android อยู่ใน ScreenFile.kt
class ScreenSend {
  /// 0-100 ระหว่างส่ง · -1 ไม่ได้ส่ง
  static final ValueNotifier<int> progress = ValueNotifier<int>(-1);

  /// คืน null = สำเร็จ · ไม่งั้นเป็นข้อความผิดพลาด
  static Future<String?> send(String name, Uint8List bytes) async {
    progress.value = 0;
    try {
      final r = await Native.m.invokeMethod<String>('screenSend', {'name': name, 'b': bytes});
      return r == 'ok' ? null : (r ?? 'ส่งไม่สำเร็จ');
    } catch (e) {
      return '$e';
    } finally {
      progress.value = -1;
    }
  }
}
