import 'dart:async';

import 'package:flutter/foundation.dart';

import '../settings.dart';
import 'c3_cal.dart';
import 'c3_ota.dart';
import 'c3_source.dart';
import 'car_data.dart';
import 'cyd_link.dart';
import 'demo_source.dart';
import 'elm_source.dart';
import 'esp32_source.dart';
import 'perf.dart';
import 'services.dart';
import 'updater.dart';
import 'app_log.dart';

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
    _tripTimer = Timer.periodic(const Duration(milliseconds: 100), (_) => _tick());
    cyd = CydLink(settings, this)..start();
    gps.on; // เริ่ม GPS ถ้าตั้งโหมดอัตโนมัติไว้
    Timer(const Duration(seconds: 15), () => updater.check(auto: true));
  }

  late final AlertEngine alerts = AlertEngine(settings);
  late final GpsCal gps = GpsCal(settings, data);
  /// ค่าปรับ gain/offset ที่เก็บใน C3 (โหมด C3)
  final C3Cal c3cal = C3Cal();
  /// อัปเดตเฟิร์มแวร์ C3 ทาง BLE
  final C3Ota c3ota = C3Ota();
  late final Updater updater = Updater(settings);
  late final DataLogger logger = DataLogger(settings);
  final History history = History();
  DateTime _lastTick = DateTime.now(), _lastFlush = DateTime.now();

  /// ทุก 0.1 วินาที: ทริป / น้ำมัน / เตือน / บันทึก / กราฟ
  void _tick() {
    final now = DateTime.now();
    final dt = (now.difference(_lastTick).inMicroseconds / 1e6).clamp(0.0, 1.0);
    _lastTick = now;
    final conn = state == LinkState.connected;
    if (conn) {
      final km = data.speed * dt / 3600;
      data.trip += km;
      final lph = data.fuelLph;
      final fresh = lph != null && now.difference(data.mafAt).inSeconds < 5;
      settings.addTrip(km, fresh ? lph * dt / 3600 : 0, fresh ? km : 0);
      history.add(data, settings);
    }
    if (now.difference(_lastFlush).inSeconds >= 10) {
      _lastFlush = now;
      settings.flushTrip();
    }
    alerts.tick(data, conn);
    logger.tick(data, conn);
  }

  /// ส่งค่าต่อให้จอ ESP32 CYD ทาง WiFi
  late final CydLink cyd;

  Timer? _tripTimer;

  /// หน้าที่กำลังแสดงอยู่บอกว่าควรอ่านค่าแบบไหน
  PollMode pollMode = PollMode.normal;

  /// PID ที่หน้าจอปัจจุบันโชว์อยู่ (ให้ K-line อ่านถี่กว่าค่าอื่น)  หน้า GIF ตั้งตามช่องที่เลือก
  List<String> focusPids = const ['11', '04'];

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
    if (!settings.unlocked) return; // ยังไม่ใส่รหัสเข้าแอป = ไม่ต่อรถ
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
      SourceType.c3 => C3Source(),
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
    if (s != state || msg != message) AppLog.i('การเชื่อมต่อ: ${s.name} · $msg');
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
    settings.flushTrip();
    logger.close();
    cyd.dispose();
    perf.dispose();
    settings.removeListener(_onSettings);
    _src?.stop();
    super.dispose();
  }
}
