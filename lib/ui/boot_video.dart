import 'dart:async';

import 'package:flutter/material.dart';
import 'package:video_player/video_player.dart';

import '../settings.dart';

/// คลิปตอนเปิดแอป: เล่นจนจบ → จอค่อย ๆ ดำ → เข้าหน้าปัด
/// แตะจอเพื่อข้ามได้
class BootVideoPage extends StatefulWidget {
  const BootVideoPage({super.key, required this.settings, required this.next});
  final AppSettings settings;
  final Widget Function() next;

  @override
  State<BootVideoPage> createState() => _BootVideoPageState();
}

class _BootVideoPageState extends State<BootVideoPage> {
  late final VideoPlayerController _c =
      VideoPlayerController.asset('assets/boot.mp4');
  bool _ready = false;
  bool _ending = false;
  double _black = 0;
  Timer? _safety;

  @override
  void initState() {
    super.initState();
    // กันค้าง: ถ้าคลิปมีปัญหา เข้าหน้าปัดเองภายใน 25 วินาที
    _safety = Timer(const Duration(seconds: 25), _end);
    _start();
  }

  Future<void> _start() async {
    try {
      await _c.initialize();
      await _c.setVolume(widget.settings.bootSound ? 1 : 0);
      _c.addListener(_onTick);
      if (!mounted) return;
      setState(() => _ready = true);
      await _c.play();
    } catch (_) {
      _end();
    }
  }

  void _onTick() {
    final v = _c.value;
    if (v.hasError) {
      _end();
      return;
    }
    if (v.isInitialized &&
        v.duration > Duration.zero &&
        !v.isPlaying &&
        v.position >= v.duration - const Duration(milliseconds: 150)) {
      _end();
    }
  }

  Future<void> _end() async {
    if (_ending || !mounted) return;
    _ending = true;
    _safety?.cancel();
    // จอค่อย ๆ ดำ แล้วค้างไว้แป๊บหนึ่ง
    setState(() => _black = 1);
    await Future<void>.delayed(const Duration(milliseconds: 900));
    if (!mounted) return;
    Navigator.of(context).pushReplacement(PageRouteBuilder<void>(
      transitionDuration: const Duration(milliseconds: 600),
      pageBuilder: (_, __, ___) => widget.next(),
      transitionsBuilder: (_, a, __, child) =>
          FadeTransition(opacity: a, child: child),
    ));
  }

  @override
  void dispose() {
    _safety?.cancel();
    _c.removeListener(_onTick);
    _c.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.black,
      body: GestureDetector(
        behavior: HitTestBehavior.opaque,
        onTap: _end,
        child: Stack(fit: StackFit.expand, children: [
          if (_ready)
            Center(
              child: AspectRatio(
                aspectRatio: _c.value.aspectRatio,
                child: VideoPlayer(_c),
              ),
            ),
          IgnorePointer(
            child: AnimatedOpacity(
              opacity: _black,
              duration: const Duration(milliseconds: 500),
              child: const ColoredBox(color: Colors.black),
            ),
          ),
        ]),
      ),
    );
  }
}
