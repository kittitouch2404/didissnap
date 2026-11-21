import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';

import '../settings.dart';
import 'hub.dart';

/// ส่งค่ารถต่อให้จอ ESP32 CYD ทาง WiFi (UDP)
///  - จอ CYD ประกาศตัว "CYD-OBD" ทาง broadcast พอร์ต 35001 ทุก 1 วินาที -> แอปจำ IP ไว้ส่งตรง
///  - ยังไม่เจอจอ = ส่งแบบ broadcast (255.255.255.255) ไปก่อน
///  - ข้อความ: OBD,rpm,kmh,coolant,iat,batt,load,tps,map,maf,lambda,stft  (ช่องว่าง = ไม่มีค่า) ~20 ครั้ง/วินาที
class CydLink extends ChangeNotifier {
  CydLink(this.settings, this.hub);

  static const int dataPort = 35000;
  static const int beaconPort = 35001;

  final AppSettings settings;
  final DataHub hub;

  RawDatagramSocket? _sock;
  Timer? _timer;
  InternetAddress? _cyd;
  DateTime _lastBeacon = DateTime.fromMillisecondsSinceEpoch(0);
  int sent = 0;

  /// สถานะไว้โชว์ในหน้าตั้งค่า
  String get status {
    if (!settings.cydLink) return 'ปิดอยู่';
    if (_sock == null) return 'กำลังเริ่ม...';
    final found = _cyd != null &&
        DateTime.now().difference(_lastBeacon) < const Duration(seconds: 5);
    final sending = hub.state == LinkState.connected &&
        settings.source != SourceType.esp32;
    final who = found ? 'เจอจอ CYD ที่ ${_cyd!.address}' : 'ยังไม่เจอจอ CYD (ส่งแบบ broadcast ${_bcast.length} วง)';
    return sending ? '$who · กำลังส่งค่า' : '$who · รอค่าจากรถ';
  }

  Future<void> start() async {
    try {
      final s = await RawDatagramSocket.bind(InternetAddress.anyIPv4, beaconPort,
          reuseAddress: true);
      s.broadcastEnabled = true;
      s.listen((ev) {
        if (ev != RawSocketEvent.read) return;
        final dg = s.receive();
        if (dg == null) return;
        final t = utf8.decode(dg.data, allowMalformed: true);
        if (t.startsWith('CYD-OBD')) {
          final first = _cyd == null || _cyd!.address != dg.address.address;
          _cyd = dg.address;
          _lastBeacon = DateTime.now();
          if (first) notifyListeners();
        }
      });
      _sock = s;
    } catch (_) {
      // พอร์ตถูกใช้อยู่: ยังส่งได้ (ส่งแบบ broadcast) แค่หา IP จอไม่ได้
      try {
        final s = await RawDatagramSocket.bind(InternetAddress.anyIPv4, 0);
        s.broadcastEnabled = true;
        _sock = s;
      } catch (_) {}
    }
    _timer = Timer.periodic(const Duration(milliseconds: 50), (_) => _tick());
    notifyListeners();
  }

  static String _f(double? v, int dec) =>
      v == null || v.isNaN ? '' : v.toStringAsFixed(dec);

  String _line() {
    final d = hub.data;
    double? lam;
    final l = d.pidBytes[0x34] ?? d.pidBytes[0x24];
    if (l != null && l.length >= 2) lam = (l[0] * 256 + l[1]) * 2 / 65536;
    double? stft;
    final s = d.pidBytes[0x06];
    if (s != null && s.isNotEmpty) stft = (s[0] - 128) * 100 / 128;
    return 'OBD,${d.rpm.round()},${d.speed.round()},${_f(d.ect, 0)},${_f(d.iat, 0)},'
        '${_f(d.volt, 1)},${_f(d.load, 1)},${_f(d.tps, 1)},${_f(d.map, 0)},${_f(d.maf, 1)},'
        '${_f(lam, 3)},${_f(stft, 1)}';
  }

  /// ที่อยู่ broadcast ของทุกวง WiFi ที่เครื่องนี้อยู่ (เช่น ฮอตสปอตที่มือถือปล่อยเอง 192.168.43.255)
  /// มือถือที่เป็นตัวปล่อยฮอตสปอต ส่งไป 255.255.255.255 แล้วมักไปไม่ถึงวงฮอตสปอต จึงต้องส่งไปทีละวง
  List<InternetAddress> _bcast = [InternetAddress('255.255.255.255')];
  DateTime _bcastAt = DateTime.fromMillisecondsSinceEpoch(0);
  bool _bcastBusy = false;

  Future<void> _refreshBcast() async {
    if (_bcastBusy) return;
    _bcastBusy = true;
    try {
      final out = <InternetAddress>[InternetAddress('255.255.255.255')];
      final ifs = await NetworkInterface.list(
          includeLoopback: false, type: InternetAddressType.IPv4);
      for (final i in ifs) {
        for (final a in i.addresses) {
          final p = a.address.split('.');
          if (p.length == 4) out.add(InternetAddress('${p[0]}.${p[1]}.${p[2]}.255'));
        }
      }
      _bcast = out;
    } catch (_) {}
    _bcastAt = DateTime.now();
    _bcastBusy = false;
  }

  void _tick() {
    final s = _sock;
    if (s == null || !settings.cydLink) return;
    // โหมด ESP32: จอ CYD เป็นคนอ่านรถเอง ไม่ต้องส่งกลับไป
    if (hub.state != LinkState.connected || settings.source == SourceType.esp32) return;
    final now = DateTime.now();
    if (now.difference(_bcastAt) > const Duration(seconds: 5)) _refreshBcast();
    final data = utf8.encode(_line());
    try {
      if (_cyd != null && now.difference(_lastBeacon) < const Duration(seconds: 5)) {
        s.send(data, _cyd!, dataPort);                     // รู้ IP จอแล้ว: ส่งตรง
      } else {
        for (final a in _bcast) {
          s.send(data, a, dataPort);                       // ยังไม่รู้: ส่ง broadcast ทุกวง
        }
      }
      sent++;
    } catch (_) {}
  }

  @override
  void dispose() {
    _timer?.cancel();
    _sock?.close();
    super.dispose();
  }
}
