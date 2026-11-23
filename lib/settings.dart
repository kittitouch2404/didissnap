import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'data/custom_pid.dart';
import 'data/gif_cfg.dart';

/// แหล่งข้อมูลรถ
enum SourceType { demo, btElm, wifiElm, esp32 }

/// หน้าปัดทั้งหมดตามลำดับ (id, ชื่อที่แสดงในเมนู)
const List<(String, String)> kPages = [
  ('race', 'จอดิจิทัลรถแข่ง'),
  ('round', 'เกจกลมมีเข็ม'),
  ('sweep', 'Sweep (แถบรอบโค้ง + 6 ช่อง)'),
  ('arc', 'Arc (วงรอบเหลือง + ความเร็วกล่องใหญ่)'),
  ('yellow', 'Yellow box (ช่องเหลือง + เกียร์ฟ้า)'),
  ('bars', 'Bar graph (กราฟแท่ง + 8 ช่อง)'),
  ('redstripe', 'Red stripe (เกจวงใหญ่ + พื้นแถบแดง)'),
  ('ecu', 'ECU (รอบตัวใหญ่ + เกียร์/ความเร็วกลาง)'),
  ('center', 'Center tach (เกจกลางจอ + 8 ช่อง)'),
  ('perf', 'Record (จับเวลาออกตัว 0-100, 1/4 ไมล์)'),
  ('pids', 'ตารางค่า OBD2 ทั้งหมด'),
  ('picture', 'รูปพื้นหลังของคุณ'),
  ('gif1', 'GIF 1 (แบบหน้า 9 จอ CYD)'),
  ('gif2', 'GIF 2 (แบบหน้า 10 จอ CYD)'),
  ('gif3', 'GIF 3 (แบบหน้า 11 จอ CYD)'),
  ('gif4', 'GIF 4 (แบบหน้า 12 จอ CYD)'),
  ('gif5', 'GIF 5 (แบบหน้า 13 จอ CYD)'),
];

const List<double> kDefaultGears = [7.6, 13.4, 19.6, 25.9, 32.4];

/// ค่าตั้งทั้งหมดของแอป เก็บถาวรด้วย SharedPreferences
class AppSettings extends ChangeNotifier {
  AppSettings._(this._p);
  final SharedPreferences _p;

  static Future<AppSettings> load() async =>
      AppSettings._(await SharedPreferences.getInstance());

  int _i(String k, int d) => _p.getInt(k) ?? d;
  String _s(String k, String d) => _p.getString(k) ?? d;
  bool _b(String k, bool d) => _p.getBool(k) ?? d;

  void _setI(String k, int v) {
    _p.setInt(k, v);
    notifyListeners();
  }

  void _setB(String k, bool v) {
    _p.setBool(k, v);
    notifyListeners();
  }

  // ---------- การเชื่อมต่อ ----------
  SourceType get source {
    final i = _i('source', 0);
    return (i >= 0 && i < SourceType.values.length)
        ? SourceType.values[i]
        : SourceType.demo;
  }

  set source(SourceType v) => _setI('source', v.index);

  String get btName => _s('btName', '');
  String get btAddress => _s('btAddress', '');
  void setBtDevice(String name, String address) {
    _p.setString('btName', name);
    _p.setString('btAddress', address);
    notifyListeners();
  }

  String get elmHost => _s('elmHost', '192.168.0.10');
  int get elmPort => _i('elmPort', 35000);
  void setElm(String host, int port) {
    _p.setString('elmHost', host);
    _p.setInt('elmPort', port);
    notifyListeners();
  }

  String get espHost => _s('espHost', '192.168.4.1');
  int get espPort => _i('espPort', 3333);
  void setEsp(String host, int port) {
    _p.setString('espHost', host);
    _p.setInt('espPort', port);
    notifyListeners();
  }

  // ---------- หน้าปัด ----------
  int get maxRpm => _i('maxRpm', 8000);
  set maxRpm(int v) => _setI('maxRpm', v);

  int get redline => _i('redline', 7000);
  set redline(int v) => _setI('redline', v);

  int get shiftStart => _i('shiftStart', 5000);
  set shiftStart(int v) => _setI('shiftStart', v);

  int get maxSpeed => _i('maxSpeed', 240);
  set maxSpeed(int v) => _setI('maxSpeed', v);

  /// AFR = lambda x stoich (14.7 เบนซิน, E20 13.8, E85 9.8, ดีเซล 14.5) เก็บ x10
  int get afrStoich10 => _i('afrStoich10', 147);
  set afrStoich10(int v) => _setI('afrStoich10', v);

  int get ectWarn => _i('ectWarn', 105);
  set ectWarn(int v) => _setI('ectWarn', v);

  bool get keepScreenOn => _b('keepScreenOn', true);
  set keepScreenOn(bool v) => _setB('keepScreenOn', v);

  bool get flip => _b('flip', false);
  set flip(bool v) => _setB('flip', v);

  /// หน้าที่เปิดค้างไว้ล่าสุด (ไม่ notify เพื่อไม่ให้ทั้งแอป rebuild)
  int get startPage => _i('startPage', 0);
  set startPage(int v) {
    _p.setInt('startPage', v);
  }

  // ---------- เกียร์ (km/h ต่อ 1000 rpm) ----------
  List<double> get gearRatios {
    final l = _p.getStringList('gears');
    if (l == null || l.isEmpty) return List<double>.of(kDefaultGears);
    return l.map((e) => double.tryParse(e) ?? 10.0).toList();
  }

  set gearRatios(List<double> v) {
    _p.setStringList(
        'gears', v.map((e) => (e < 1 ? 1.0 : e).toStringAsFixed(2)).toList());
    notifyListeners();
  }

  void resetGears() {
    _p.remove('gears');
    notifyListeners();
  }

  // ---------- หน้า GIF 1-5 ----------
  final Map<int, GifCfg> _gifCache = {};
  GifCfg gifCfg(int i) {
    final c = _gifCache[i];
    if (c != null) return c;
    GifCfg g;
    try {
      final j = jsonDecode(_s('gp$i', '{}'));
      g = GifCfg(j is Map<String, dynamic> ? j : <String, dynamic>{});
    } catch (_) {
      g = GifCfg(<String, dynamic>{});
    }
    return _gifCache[i] = g;
  }

  void setGifCfg(int i, GifCfg c) {
    _gifCache[i] = c;
    _p.setString('gp$i', jsonEncode(c.toJson()));
    notifyListeners();
  }

  // ---------- ส่งค่าให้จอ ESP32 CYD ทาง WiFi (โค้ด Kotlin อ่านค่านี้ด้วย: เปิด service ให้แอปทำงานเบื้องหลัง) ----------
  bool get cydLink => _b('cydLink', true);
  set cydLink(bool v) => _setB('cydLink', v);

  // ---------- Custom PID 4 ช่อง (แบบ Torque) ----------
  static const int nCustom = 4;
  final Map<int, CustomPid> _cpCache = {};
  CustomPid customPid(int i) {
    final c = _cpCache[i];
    if (c != null) return c;
    CustomPid g;
    try {
      final j = jsonDecode(_s('cp$i', '{}'));
      g = CustomPid(j is Map<String, dynamic> ? j : <String, dynamic>{});
    } catch (_) {
      g = CustomPid(<String, dynamic>{});
    }
    return _cpCache[i] = g;
  }

  void setCustomPid(int i, CustomPid c) {
    _cpCache[i] = c;
    _p.setString('cp$i', jsonEncode(c.toJson()));
    notifyListeners();
  }

  // ---------- อุณหภูมิน้ำมันเกียร์ CVT (ทดลอง: PID เฉพาะยี่ห้อ ส่งไปที่กล่องเกียร์) ----------
  bool get cvtTempOn => _b('cvtTempOn', false);
  set cvtTempOn(bool v) => _setB('cvtTempOn', v);
  /// header ของกล่องเกียร์ (CAN 11 บิต) ปกติ 7E1 -> ตอบกลับ 7E9
  String get cvtHdr => _s('cvtHdr', '7E1');
  set cvtHdr(String v) {
    _p.setString('cvtHdr', v.toUpperCase().replaceAll(RegExp(r'[^0-9A-F]'), ''));
    notifyListeners();
  }
  /// คำสั่งเต็ม เช่น 22xxxx (UDS) หรือ 21xx (KWP)
  String get cvtReq => _s('cvtReq', '');
  set cvtReq(String v) {
    _p.setString('cvtReq', v.toUpperCase().replaceAll(RegExp(r'[^0-9A-F]'), ''));
    notifyListeners();
  }
  /// ไบต์ที่เป็น A (นับจาก 0 หลัง echo ของคำสั่ง)
  int get cvtByte => _i('cvtByte', 0);
  set cvtByte(int v) => _setI('cvtByte', v);
  /// สูตร (ดู kCvtFormulas)
  int get cvtFml => _i('cvtFml', 0);
  set cvtFml(int v) => _setI('cvtFml', v);

  // ---------- เปิดแอปอัตโนมัติ (โค้ด Kotlin อ่านค่าเหล่านี้โดยตรง) ----------
  bool get autoBoot => _b('autoBoot', false);
  set autoBoot(bool v) => _setB('autoBoot', v);
  bool get autoWake => _b('autoWake', false);
  set autoWake(bool v) => _setB('autoWake', v);
  bool get autoPower => _b('autoPower', false);
  set autoPower(bool v) => _setB('autoPower', v);
  bool get autoBt => _b('autoBt', false);
  set autoBt(bool v) => _setB('autoBt', v);
  String get autoBtName => _s('autoBtName', '');
  String get autoBtAddress => _s('autoBtAddress', '');
  void setAutoBtDevice(String name, String address) {
    _p.setString('autoBtName', name);
    _p.setString('autoBtAddress', address);
    notifyListeners();
  }

  bool get showOnLock => _b('showOnLock', false);
  set showOnLock(bool v) => _setB('showOnLock', v);

  // ---------- คลิปตอนเปิดแอป ----------
  /// ตอนเปิดแอป: 0 = เข้าหน้าปัดเลย, 1 = คลิปวิดีโอ, 2 = โลโก้สุ่มแบบจอ ESP32-S3 (ค่าเริ่มต้น)
  int get bootMode => _i('bootMode', 2);
  set bootMode(int v) => _setI('bootMode', v);
  /// เสียงตอนบิดกุญแจ: 0 ปิด, 1 8-bit, 2 Whoosh, 3 เครื่องยนต์, 4 ไฟล์ของฉัน
  int get keySound => _i('keySound', 1);
  set keySound(int v) => _setI('keySound', v);
  String get keySoundPath => _s('keySoundPath', '');
  set keySoundPath(String v) {
    _p.setString('keySoundPath', v);
    notifyListeners();
  }
  int get keySoundVol => _i('keySoundVol', 100);
  set keySoundVol(int v) => _setI('keySoundVol', v);
  /// เล่นทุกครั้งที่เปิดแอป (ปิด = เฉพาะตอนแอปเปิดเองเพราะรถติด/จอตื่น)
  bool get keySoundAlways => _b('keySoundAlways', false);
  set keySoundAlways(bool v) => _setB('keySoundAlways', v);
  /// โลโก้ของฉัน (เล่นต่อจากโลโก้สุ่ม OYT): รูป / GIF / วิดีโอ
  bool get myLogoOn => _b('myLogoOn', false);
  set myLogoOn(bool v) => _setB('myLogoOn', v);
  String get myLogoPath => _s('myLogoPath', '');
  set myLogoPath(String v) {
    _p.setString('myLogoPath', v);
    notifyListeners();
  }
  /// รูป/GIF: ค้างกี่วินาที
  int get myLogoSec => _i('myLogoSec', 3);
  set myLogoSec(int v) => _setI('myLogoSec', v);
  /// true = เต็มจอ (ครอปขอบ), false = พอดีจอ (มีขอบดำ)
  bool get myLogoFill => _b('myLogoFill', false);
  set myLogoFill(bool v) => _setB('myLogoFill', v);
  /// วิดีโอ: เปิดเสียงของคลิป
  bool get myLogoSound => _b('myLogoSound', false);
  set myLogoSound(bool v) => _setB('myLogoSound', v);
  int get bootLastColor => _i('bootLastColor', -1);
  set bootLastColor(int v) => _p.setInt('bootLastColor', v);
  bool get bootClip => _b('bootClip', true);
  set bootClip(bool v) => _setB('bootClip', v);
  bool get bootSound => _b('bootSound', true);
  set bootSound(bool v) => _setB('bootSound', v);

  // ---------- หน้าที่เปิดใช้ ----------
  // เก็บเป็น "หน้าที่ปิดไว้" เพื่อให้หน้าใหม่ในเวอร์ชันถัดไปโผล่ขึ้นมาเอง
  List<String> get pages {
    final all = kPages.map((e) => e.$1).toList();
    final off = _p.getStringList('pagesOff') ?? const <String>[];
    final r = all.where((id) => !off.contains(id)).toList();
    return r.isEmpty ? all : r;
  }

  set pages(List<String> v) {
    _p.setStringList('pagesOff',
        kPages.map((e) => e.$1).where((id) => !v.contains(id)).toList());
    notifyListeners();
  }

  // ---------- รูปพื้นหลัง ----------
  List<String> get pictures => _p.getStringList('pictures') ?? const <String>[];
  set pictures(List<String> v) {
    _p.setStringList('pictures', v);
    // ล้างค่าตั้งของรูปที่ถูกลบไปแล้ว
    final cfg = _picCfg();
    cfg.removeWhere((k, _) => !v.contains(k));
    _p.setString('picCfg', jsonEncode(cfg));
    _cfgCache = cfg;
    notifyListeners();
  }

  /// หน้ารูปพื้นหลังใช้ฟอนต์พิกเซลแบบ 16-bit
  bool get pixelFont => _b('pixelFont', true);
  set pixelFont(bool v) => _setB('pixelFont', v);

  /// true = แบ่งช่วงรอบเท่า ๆ กันอัตโนมัติ, false = กำหนดรอบเริ่มของแต่ละรูปเอง
  bool get picAuto => _b('picAuto', true);
  set picAuto(bool v) {
    if (!v && picAuto) {
      // ตอนเปลี่ยนเป็นกำหนดเอง เริ่มจากค่าที่แบ่งอัตโนมัติไว้
      final cfg = _picCfg();
      for (final e in activePictures()) {
        cfg[e.$1] = {...(cfg[e.$1] as Map? ?? const {}), 'rpm': e.$2};
      }
      _p.setString('picCfg', jsonEncode(cfg));
      _cfgCache = cfg;
    }
    _setB('picAuto', v);
  }

  Map<String, dynamic>? _cfgCache;
  Map<String, dynamic> _picCfg() {
    final c = _cfgCache;
    if (c != null) return Map<String, dynamic>.of(c);
    try {
      final j = jsonDecode(_s('picCfg', '{}'));
      _cfgCache = j is Map<String, dynamic> ? j : <String, dynamic>{};
    } catch (_) {
      _cfgCache = <String, dynamic>{};
    }
    return Map<String, dynamic>.of(_cfgCache!);
  }

  /// แสดงรูปนี้ไหม
  bool picOn(String path) {
    final m = _picCfg()[path];
    return m is Map && m['on'] is bool ? m['on'] as bool : true;
  }

  /// รอบเริ่มของรูป (ใช้ตอนกำหนดเอง)
  int picRpm(String path) {
    final m = _picCfg()[path];
    return m is Map && m['rpm'] is num ? (m['rpm'] as num).toInt() : 0;
  }

  void setPic(String path, {bool? on, int? rpm}) {
    final cfg = _picCfg();
    final m = <String, dynamic>{...(cfg[path] as Map? ?? const {})};
    if (on != null) m['on'] = on;
    if (rpm != null) m['rpm'] = rpm < 0 ? 0 : rpm;
    cfg[path] = m;
    _p.setString('picCfg', jsonEncode(cfg));
    _cfgCache = cfg;
    notifyListeners();
  }

  List<(String, int)>? _activeCache;

  /// รูปที่เปิดแสดง พร้อมรอบเริ่ม เรียงจากรอบน้อยไปมาก
  List<(String, int)> activePictures() {
    final cached = _activeCache;
    if (cached != null) return cached;
    final pics = pictures.where(picOn).toList();
    List<(String, int)> out;
    if (pics.isEmpty) {
      out = const [];
    } else if (picAuto) {
      final band = redline / pics.length;
      out = [for (var i = 0; i < pics.length; i++) (pics[i], (band * i).round())];
    } else {
      out = [for (final p in pics) (p, picRpm(p))]
        ..sort((a, b) => a.$2.compareTo(b.$2));
    }
    _activeCache = out;
    return out;
  }

  /// เลือกรูปที่จะแสดงตามรอบ (null = ไม่มีรูปที่เปิดไว้)
  String? pictureFor(double rpm) {
    final l = activePictures();
    if (l.isEmpty) return null;
    var pick = l.first.$1;
    for (final e in l) {
      if (rpm >= e.$2) pick = e.$1;
    }
    return pick;
  }

  @override
  void notifyListeners() {
    _activeCache = null;
    super.notifyListeners();
  }
}
