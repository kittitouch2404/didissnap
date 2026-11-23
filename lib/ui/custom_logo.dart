import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart' hide Text;
import '../i18n.dart';
import 'package:image_picker/image_picker.dart';
import 'package:path_provider/path_provider.dart';
import 'package:video_player/video_player.dart';

import '../settings.dart';

/// โลโก้ของฉัน: เล่นต่อจากโลโก้สุ่ม OYT  (รูป / GIF เคลื่อนไหว / วิดีโอ)  แตะจอ = ข้าม
class CustomLogoPage extends StatefulWidget {
  const CustomLogoPage({super.key, required this.settings, required this.next});
  final AppSettings settings;
  final Widget Function() next;

  static bool isVideo(String p) {
    final l = p.toLowerCase();
    return l.endsWith('.mp4') || l.endsWith('.mov') || l.endsWith('.webm') || l.endsWith('.3gp') || l.endsWith('.mkv');
  }

  /// มีโลโก้ของฉันให้เล่นไหม
  static bool has(AppSettings s) => s.myLogoOn && s.myLogoPath.isNotEmpty && File(s.myLogoPath).existsSync();

  /// เลือกไฟล์ (รูป/GIF/วิดีโอ) แล้วก๊อปเก็บไว้ในแอป
  static Future<bool> pick(AppSettings s) async {
    final x = await ImagePicker().pickMedia();
    if (x == null) return false;
    final docs = await getApplicationDocumentsDirectory();
    final dot = x.name.lastIndexOf('.');
    final ext = dot >= 0 ? x.name.substring(dot).toLowerCase() : '.png';
    final dst = '${docs.path}/mylogo_${DateTime.now().millisecondsSinceEpoch}$ext';
    await File(x.path).copy(dst);
    final old = s.myLogoPath;
    s.myLogoPath = dst;
    s.myLogoOn = true;
    if (old.isNotEmpty && old != dst) {
      try {
        File(old).deleteSync();
      } catch (_) {}
    }
    return true;
  }

  @override
  State<CustomLogoPage> createState() => _CustomLogoPageState();
}

class _CustomLogoPageState extends State<CustomLogoPage> {
  VideoPlayerController? _v;
  bool _show = false, _ending = false;
  Timer? _t;

  @override
  void initState() {
    super.initState();
    final s = widget.settings;
    final p = s.myLogoPath;
    if (CustomLogoPage.isVideo(p)) {
      final c = VideoPlayerController.file(File(p));
      _v = c;
      c.initialize().then((_) async {
        if (!mounted) return;
        await c.setVolume(s.myLogoSound ? 1 : 0);
        setState(() => _show = true);
        await c.play();
        c.addListener(_onVideo);
      }).catchError((_) => _end());
      _t = Timer(const Duration(seconds: 30), _end);         // กันค้าง
    } else {
      // รูป/GIF: ค่อย ๆ ขึ้น ค้างตามเวลาที่ตั้ง แล้วค่อย ๆ หาย
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted) setState(() => _show = true);
      });
      _t = Timer(Duration(milliseconds: s.myLogoSec * 1000 + 300), _end);
    }
  }

  void _onVideo() {
    final v = _v!.value;
    if (v.hasError || (v.duration > Duration.zero && v.position >= v.duration - const Duration(milliseconds: 120))) _end();
  }

  Future<void> _end() async {
    if (_ending || !mounted) return;
    _ending = true;
    _t?.cancel();
    setState(() => _show = false);
    await Future<void>.delayed(const Duration(milliseconds: 350));
    if (!mounted) return;
    Navigator.of(context).pushReplacement(PageRouteBuilder<void>(
      transitionDuration: const Duration(milliseconds: 400),
      pageBuilder: (_, __, ___) => widget.next(),
      transitionsBuilder: (_, a, __, child) => FadeTransition(opacity: a, child: child),
    ));
  }

  @override
  void dispose() {
    _t?.cancel();
    _v?.removeListener(_onVideo);
    _v?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final s = widget.settings;
    final fit = s.myLogoFill ? BoxFit.cover : BoxFit.contain;
    final v = _v;
    Widget child;
    if (v != null) {
      child = v.value.isInitialized
          ? FittedBox(
              fit: fit,
              clipBehavior: Clip.hardEdge,
              child: SizedBox(width: v.value.size.width, height: v.value.size.height, child: VideoPlayer(v)),
            )
          : const SizedBox();
    } else {
      child = Image.file(File(s.myLogoPath), fit: fit, gaplessPlayback: true,
          errorBuilder: (_, __, ___) {
        WidgetsBinding.instance.addPostFrameCallback((_) => _end());
        return const SizedBox();
      });
    }
    return Scaffold(
      backgroundColor: Colors.black,
      body: GestureDetector(
        behavior: HitTestBehavior.opaque,
        onTap: _end,
        child: AnimatedOpacity(
          opacity: _show ? 1 : 0,
          duration: const Duration(milliseconds: 300),
          child: SizedBox.expand(child: child),
        ),
      ),
    );
  }
}
