import 'dart:async';

import 'package:flutter/foundation.dart';

import '../settings.dart';
import 'car_data.dart';
import 'demo_source.dart';
import 'elm_source.dart';
import 'esp32_source.dart';
import 'perf.dart';

enum LinkState { idle, connecting, connected, error }

/// โหมดการอ่านค่า: ปกติ / เน้นความเร็ว (จับเวลา) / อ่านทุก PID (หน้าตาราง)
enum PollMode { normal, perf, allPids }

abstract class CarSource {
  void start(DataHub hub);
  Future<void> stop();
  Future<List<String>> readDtc();
  Future<bool> clearDtc();
}

String errText(Object e) {
  final s = e.toString();
  return s.startsWith('Exception: ') ? s.substring(11) : s;
}

/// ศูนย์กลางข้อมูล: ถือค่ารถล่าสุด + สถานะการเชื่อมต่อ
/// หน้าปัดอ่าน [data] ตรง ๆ ทุกเฟรม (ไม่ notify ทุกค่า เพื่อความลื่น)
class DataHub extends ChangeNotifier {
  DataHub(this.settings) {
    settings.addListener(_onSettings);
    _tripTimer = Timer.periodic(const Duration(milliseconds: 250), (_) {
      if (state == LinkState.connected) data.trip += data.speed * 0.25 / 3600;
    });
  }

  Timer? _tripTimer;

  /// หน้าที่กำลังแสดงอยู่บอกว่าควรอ่านค่าแบบไหน
  PollMode pollMode = PollMode.normal;

  /// ตัวจับเวลาออกตัว
  late final PerfRecorder perf = PerfRecorder(this);

  final AppSettings settings;
  final CarData data = CarData();
  LinkState state = LinkState.idle;
  String message = '';

  CarSource? _src;
  String _sig = '';
  int _gen = 0;
  bool _disposed = false;

  String _signature() => '${settings.source.index}|${settings.btAddress}|'
      '${settings.elmHost}:${settings.elmPort}|'
      '${settings.espHost}:${settings.espPort}';

  void _onSettings() {
    if (_signature() != _sig) restart();
  }

  void start() {
    _sig = _signature();
    data.reset();
    final CarSource s = switch (settings.source) {
      SourceType.demo => DemoSource(settings),
      SourceType.btElm =>
        ElmSource('Bluetooth', () => BtTransport(settings.btAddress)),
      SourceType.wifiElm => ElmSource(
          'WiFi', () => TcpTransport(settings.elmHost, settings.elmPort)),
      SourceType.esp32 => Esp32Source(settings.espHost, settings.espPort),
    };
    _src = s;
    s.start(this);
  }

  Future<void> restart() async {
    final g = ++_gen;
    final old = _src;
    _src = null;
    await old?.stop();
    if (_disposed || g != _gen) return;
    start();
  }

  /// แหล่งข้อมูลเรียกเพื่ออัปเดตสถานะ (ถ้าไม่ใช่แหล่งปัจจุบันจะถูกเมิน)
  void setLink(CarSource from, LinkState s, String msg) {
    if (_disposed || !identical(from, _src)) return;
    state = s;
    message = msg;
    notifyListeners();
  }

  /// แหล่งข้อมูล ESP32 ที่ใช้อยู่ (null = ไม่ได้ใช้โหมด ESP32)
  Esp32Source? get esp32 {
    final s = _src;
    return s is Esp32Source ? s : null;
  }

  Future<List<String>> readDtc() {
    final s = _src;
    if (s == null) return Future.error(Exception('ยังไม่ได้เชื่อมต่อ'));
    return s.readDtc();
  }

  Future<bool> clearDtc() {
    final s = _src;
    if (s == null) return Future.error(Exception('ยังไม่ได้เชื่อมต่อ'));
    return s.clearDtc();
  }

  @override
  void dispose() {
    _disposed = true;
    _tripTimer?.cancel();
    perf.dispose();
    settings.removeListener(_onSettings);
    _src?.stop();
    super.dispose();
  }
}
