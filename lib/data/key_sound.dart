import 'dart:async';
import 'dart:io';

import 'package:video_player/video_player.dart';

import '../settings.dart';

/// เสียงตอนบิดกุญแจ (แอปเปิดเอง / จอรถตื่น)  ใช้ video_player เล่นไฟล์เสียง (m4a/mp3/wav ได้หมด)
class KeySound {
  static const List<(String, String)> builtIn = [
    ('ปิด', ''),
    ('8-bit (เข้ากับโลโก้ OYT)', 'assets/boot_8bit.m4a'),
    ('Whoosh + ตูม', 'assets/boot_whoosh.m4a'),
    ('เสียงเครื่องยนต์เร่ง', 'assets/boot_engine.m4a'),
    ('ไฟล์ของฉัน', ''),
  ];

  static DateTime _last = DateTime.fromMillisecondsSinceEpoch(0);
  static VideoPlayerController? _c;

  /// force = กดทดสอบ (ไม่สนการกันเล่นซ้ำ)
  static Future<void> play(AppSettings s, {bool force = false, int? choice}) async {
    final k = choice ?? s.keySound;
    if (k <= 0 || k >= builtIn.length) return;
    final now = DateTime.now();
    if (!force && now.difference(_last).inSeconds < 20) return;   // รถติด + จอตื่น มาพร้อมกัน = เล่นครั้งเดียว
    _last = now;
    VideoPlayerController c;
    if (k == 4) {
      final p = s.keySoundPath;
      if (p.isEmpty || !File(p).existsSync()) return;
      c = VideoPlayerController.file(File(p));
    } else {
      c = VideoPlayerController.asset(builtIn[k].$2);
    }
    await _stop();
    _c = c;
    try {
      await c.initialize();
      await c.setVolume(s.keySoundVol.clamp(0, 100) / 100.0);
      await c.play();
      // ปล่อยตัวเล่นเมื่อจบ (ไฟล์ของฉันยาวสุด 15 วินาที)
      var waited = 0;
      while (_c == c && waited < 15000) {
        await Future<void>.delayed(const Duration(milliseconds: 250));
        waited += 250;
        final v = c.value;
        if (v.hasError || (v.duration > Duration.zero && v.position >= v.duration)) break;
      }
    } catch (_) {}
    if (_c == c) await _stop();
  }

  static Future<void> _stop() async {
    final c = _c;
    _c = null;
    if (c == null) return;
    try {
      await c.pause();
      await c.dispose();
    } catch (_) {}
  }
}
