import 'dart:async';
import 'dart:convert';
import 'dart:math' as math;

import 'package:crypto/crypto.dart';
import 'package:flutter/material.dart';

import '../settings.dart';

/// รหัสเข้าแอป: เก็บเป็นค่า hash (SHA-256) ไม่มีตัวเลขรหัสจริงอยู่ในแอป
const String _kPinHash = '276ee9b6a82b9ae63bcf745cb5241e69c1ede7d4889832b859be5a2c4dc9d5b9';
const String _kSalt = 'didissnap-lock-v1:';

bool pinOk(String pin) => sha256.convert(utf8.encode('$_kSalt$pin')).toString() == _kPinHash;

/// หน้าใส่รหัส: ใส่ถูกครั้งเดียวต่อเครื่อง (ลงแอปใหม่ = ถามใหม่)
class LockPage extends StatefulWidget {
  const LockPage({super.key, required this.settings, required this.onUnlock, required this.next});
  final AppSettings settings;
  final VoidCallback onUnlock;
  final Widget Function() next;
  @override
  State<LockPage> createState() => _LockPageState();
}

class _LockPageState extends State<LockPage> {
  /// ปุ่มตัวเลขแบบสุ่มตำแหน่ง (กันคนแอบดูตำแหน่งนิ้ว) สุ่มใหม่ทุกครั้งที่ใส่ผิด
  List<String> _digits = _shuffle();
  static List<String> _shuffle() => [for (var i = 0; i <= 9; i++) '$i']..shuffle(math.Random.secure());
  String _pin = '';
  String _msg = '';
  int _wrong = 0;
  DateTime? _waitUntil;
  Timer? _t;

  @override
  void dispose() {
    _t?.cancel();
    super.dispose();
  }

  bool get _locked => _waitUntil != null && DateTime.now().isBefore(_waitUntil!);

  void _press(String k) {
    if (_locked) return;
    setState(() {
      _msg = '';
      if (k == '<') {
        if (_pin.isNotEmpty) _pin = _pin.substring(0, _pin.length - 1);
      } else if (k == 'OK') {
        _check();
      } else if (_pin.length < 8) {
        _pin += k;
      }
    });
  }

  void _check() {
    if (pinOk(_pin)) {
      widget.settings.unlocked = true;
      widget.onUnlock();
      Navigator.of(context).pushReplacement(MaterialPageRoute<void>(builder: (_) => widget.next()));
      return;
    }
    _wrong++;
    _pin = '';
    _digits = _shuffle();
    if (_wrong >= 5) {
      // ใส่ผิด 5 ครั้ง: รอ 60 วินาที
      _waitUntil = DateTime.now().add(const Duration(seconds: 60));
      _wrong = 0;
      _t?.cancel();
      _t = Timer.periodic(const Duration(seconds: 1), (t) {
        if (!mounted) return;
        if (!_locked) t.cancel();
        setState(() {});
      });
      _msg = 'ใส่ผิดหลายครั้ง รอ 60 วินาที';
    } else {
      _msg = 'รหัสไม่ถูกต้อง';
    }
  }

  @override
  Widget build(BuildContext context) {
    final wait = _locked ? _waitUntil!.difference(DateTime.now()).inSeconds + 1 : 0;
    Widget key(String k) => Padding(
          padding: const EdgeInsets.all(6),
          child: SizedBox(
            width: 96,
            height: 64,
            child: FilledButton.tonal(
              onPressed: _locked ? null : () => _press(k),
              child: k == '<'
                  ? const Icon(Icons.backspace_outlined, size: 28)
                  : Text(k, style: const TextStyle(fontSize: 28, fontWeight: FontWeight.w800)),
            ),
          ),
        );
    return Scaffold(
      backgroundColor: const Color(0xFF070707),
      body: Center(
        child: Row(mainAxisAlignment: MainAxisAlignment.center, children: [
          SizedBox(
            width: 420,
            child: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.start, children: [
              const Icon(Icons.lock_outline, size: 56, color: Color(0xFFFF7A00)),
              const SizedBox(height: 12),
              const Text('DIDISSNAP', style: TextStyle(fontSize: 40, fontWeight: FontWeight.w900, fontStyle: FontStyle.italic, letterSpacing: 2)),
              const Text('ใส่รหัสเพื่อเปิดใช้งานแอปในเครื่องนี้', style: TextStyle(fontSize: 18, color: Colors.white70)),
              const SizedBox(height: 22),
              Row(children: [
                for (var i = 0; i < 8; i++)
                  Container(
                    width: 22,
                    height: 22,
                    margin: const EdgeInsets.only(right: 10),
                    decoration: BoxDecoration(
                      shape: BoxShape.circle,
                      color: i < _pin.length ? const Color(0xFFFF7A00) : Colors.transparent,
                      border: Border.all(color: Colors.white38, width: 2),
                    ),
                  ),
              ]),
              const SizedBox(height: 14),
              Text(_locked ? 'รอ $wait วินาที' : _msg, style: const TextStyle(fontSize: 18, color: Color(0xFFFF4A55))),
            ]),
          ),
          const SizedBox(width: 40),
          Column(mainAxisSize: MainAxisSize.min, children: [
            for (final r in [
              _digits.sublist(0, 3),
              _digits.sublist(3, 6),
              _digits.sublist(6, 9),
              ['<', _digits[9], 'OK'],
            ])
              Row(mainAxisSize: MainAxisSize.min, children: [for (final k in r) key(k)]),
          ]),
        ]),
      ),
    );
  }
}
