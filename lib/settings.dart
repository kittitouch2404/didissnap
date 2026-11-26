import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'data/custom_pid.dart';
import 'data/gif_cfg.dart';

/// แหล่งข้อมูลรถ
enum SourceType { demo, btElm, wifiElm, esp32, c3 }

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
  ('graph', 'กราฟสด (3 ค่า ย้อนหลัง)'),
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
  /// จับคู่ C3: ที่อยู่ / ชื่อ C3 ที่จับคู่ ("" = ยังไม่จับคู่) + กุญแจของแอป (hex 16 ตัว)
  String get c3PairAddr => _s('c3PairAddr', '');
  set c3PairAddr(String v) {
    _p.setString('c3PairAddr', v);
    notifyListeners();
  }

  String get c3PairName => _s('c3PairName', '');
  set c3PairName(String v) {
    _p.setString('c3PairName', v);
    notifyListeners();
  }

  /// ต่อ C3 ทาง WiFi แทน Bluetooth (จอรถที่ไม่มี BLE — C3 กับจอต่อฮอตสปอตเดียวกัน)
  bool get c3Wifi => _b('c3Wifi', false);
  set c3Wifi(bool v) => _setB('c3Wifi', v);
  String get c3WifiSsid => _s('c3WifiSsid', '');
  set c3WifiSsid(String v) => _p.setString('c3WifiSsid', v);

  String get c3Token => _s('c3Token', '');
  set c3Token(String v) => _p.setString('c3Token', v);

  /// แผนที่หน้าสนาม GPS: 0 ไม่มี · 1 มืด · 2 ถนน · 3 ดาวเทียม
  int get lapMap => _i('lapMap', 1);
  set lapMap(int v) => _setI('lapMap', v);

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

  /// ชนิดเกียร์: 0 ธรรมดา · 1 ออโต้ · 2 CVT
  int get gearType => _i('gearType', 0);
  set gearType(int v) => _setI('gearType', v);

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

  // ---------- ไดโน (วัดแรงม้า/แรงบิด) ----------
  String get dynoCfg => _s('dynoCfg', '');
  set dynoCfg(String v) {
    _p.setString('dynoCfg', v);
    notifyListeners();
  }

  String get dynoRuns => _s('dynoRuns', '[]');
  set dynoRuns(String v) => _p.setString('dynoRuns', v);

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
  /// ค่า CVT ที่บันทึกไว้เอง (JSON list: n ชื่อ, h header, r คำสั่ง, b ไบต์, f สูตร)
  String get cvtMine => _s('cvtMine', '[]');
  set cvtMine(String v) {
    _p.setString('cvtMine', v);
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
  // ---------- จำนวนหน้า GIF (เพิ่ม/ลบเองได้ 1-20) ----------
  int get gifPageCount => _i('gifPages', 5).clamp(1, 20);

  /// หน้าทั้งหมด: หน้า GIF ตามจำนวนที่ตั้ง (gif1..gifN) แทรกตรงตำแหน่งหน้า GIF เดิม
  List<(String, String)> get allPages {
    const cydNo = [9, 10, 11, 12, 13];
    final out = <(String, String)>[];
    var done = false;
    for (final p in kPages) {
      if (p.$1.startsWith('gif')) {
        if (done) continue;
        done = true;
        for (var i = 1; i <= gifPageCount; i++) {
          out.add(('gif$i', i <= 5 ? 'GIF $i (แบบหน้า ${cydNo[i - 1]} จอ CYD)' : 'GIF $i (เพิ่มเอง)'));
        }
      } else {
        out.add(p);
      }
    }
    for (final k in skins) {
      out.add(('skin:${k.$1}', 'สกิน: ${k.$2}'));
    }
    return out;
  }

  // ---------- สกินหน้าปัดที่นำเข้า (ออกแบบเองจากเว็บ) ----------
  List<(String, String)> get skins => [
        for (final e in _p.getStringList('skins') ?? const <String>[])
          if (e.contains('|')) (e.substring(0, e.indexOf('|')), e.substring(e.indexOf('|') + 1)),
      ];

  void addSkin(String id, String name) {
    final l = List<String>.from(_p.getStringList('skins') ?? const <String>[]);
    final i = l.indexWhere((e) => e.startsWith('$id|'));
    if (i >= 0) {
      l[i] = '$id|$name';
    } else {
      l.add('$id|$name');
    }
    _p.setStringList('skins', l);
    // ให้หน้าใหม่โผล่ (ไม่อยู่ในรายการที่ปิด)
    final off = List<String>.from(_p.getStringList('pagesOff') ?? const <String>[])..remove('skin:$id');
    _p.setStringList('pagesOff', off);
    skinRev++;
    notifyListeners();
  }

  void removeSkin(String id) {
    final l = List<String>.from(_p.getStringList('skins') ?? const <String>[])..removeWhere((e) => e.startsWith('$id|'));
    _p.setStringList('skins', l);
    skinRev++;
    notifyListeners();
  }

  /// เพิ่มทุกครั้งที่นำเข้า/ลบสกิน (ให้หน้าสกินโหลดไฟล์ใหม่)
  int skinRev = 0;

  /// เพิ่มหน้า GIF ใหม่ (ต่อท้าย) -> คืน index (0-based)
  int addGifPage() {
    final n = gifPageCount;
    if (n >= 20) return -1;
    _p.setInt('gifPages', n + 1);
    _p.remove('gp$n');
    _gifCache.remove(n);
    notifyListeners();
    return n;
  }

  /// ลบหน้า GIF i (0-based): เลื่อนค่าตั้งหน้าที่อยู่ถัดไปขึ้นมา
  void deleteGifPage(int i) {
    final n = gifPageCount;
    if (n <= 1 || i < 0 || i >= n) return;
    for (var k = i; k < n - 1; k++) {
      final nx = _p.getString('gp${k + 1}');
      if (nx != null) {
        _p.setString('gp$k', nx);
      } else {
        _p.remove('gp$k');
      }
    }
    _p.remove('gp${n - 1}');
    _gifCache.clear();
    _p.setInt('gifPages', n - 1);
    notifyListeners();
  }

  List<String> get pages {
    final all = allPages.map((e) => e.$1).toList();
    final off = _p.getStringList('pagesOff') ?? const <String>[];
    final r = all.where((id) => !off.contains(id)).toList();
    return r.isEmpty ? all : r;
  }

  set pages(List<String> v) {
    _p.setStringList('pagesOff',
        allPages.map((e) => e.$1).where((id) => !v.contains(id)).toList());
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

  // ---------- เช็กอัปเดตแอป ----------
  /// ใส่รหัสเข้าแอปถูกแล้วในเครื่องนี้ (ไม่ถูกสำรอง/ไม่ติดไปกับโปรไฟล์)
  bool get unlocked => _b('unlocked', false);
  set unlocked(bool v) => _setB('unlocked', v);

  /// token GitHub (อ่านอย่างเดียว) สำหรับเช็ก/โหลดอัปเดตจาก repo ส่วนตัว — ไม่ถูกสำรองลงไฟล์
  String get ghToken => _s('ghToken', '');
  set ghToken(String v) {
    _p.setString('ghToken', v.trim());
    notifyListeners();
  }

  bool get updateAuto => _b('updateAuto', true);
  set updateAuto(bool v) => _setB('updateAuto', v);
  int get updateLast => _i('updateLast', 0);
  int get updateSeen => _i('updateSeen', 0);
  String get updateUrl => _s('updateUrl', '');
  void setUpdateSeen(int b, String url) {
    _p.setInt('updateLast', DateTime.now().millisecondsSinceEpoch);
    _p.setInt('updateSeen', b);
    _p.setString('updateUrl', url);
  }

  // ---------- ภาษา (ไทย / English) ----------
  bool get langEn => _b('langEn', false);
  set langEn(bool v) => _setB('langEn', v);

  // ---------- สีเมนู (Racing HUD) ----------
  int get menuColor => _i('menuColor', 0xFFFF7A00);
  set menuColor(int v) => _setI('menuColor', v);

  // ---------- ความเร็ว: ชดเชยให้ตรงจริง (x1000 เช่น 1035 = +3.5%) ----------
  int get speedCal1000 => _i('speedCal', 1000);
  set speedCal1000(int v) => _setI('speedCal', v);
  /// 0 = ตั้งเอง, 1 = อัตโนมัติด้วย GPS
  int get speedCalMode => _i('speedCalMode', 0);
  set speedCalMode(int v) => _setI('speedCalMode', v);
  int get speedCalAuto1000 => _i('speedCalAuto', 1000);
  int get speedCalAutoN => _i('speedCalAutoN', 0);
  void setSpeedCalAuto(int v1000, int n) {
    _p.setInt('speedCalAuto', v1000);
    _p.setInt('speedCalAutoN', n);
    notifyListeners();
  }

  /// ค่าที่ใช้จริง: โหมด GPS ใช้ค่าที่เรียนรู้ได้ (เมื่อมีตัวอย่างพอ) ไม่งั้นใช้ค่าตั้งเอง
  double get speedCal =>
      (speedCalMode == 1 && speedCalAutoN >= 20 ? speedCalAuto1000 : speedCal1000) / 1000.0;

  // ---------- ทริป A / B + น้ำมัน (เก็บถาวร ไม่หายตอนปิดแอป) ----------
  double _d(String k) => _p.getDouble(k) ?? 0;
  double get tripA => _d('tripA');
  double get tripB => _d('tripB');
  double get fuelA => _d('fuelA');
  double get fuelB => _d('fuelB');
  double get fuelKmA => _d('fuelKmA');
  double get fuelKmB => _d('fuelKmB');
  double get oilKm => _d('oilKm');

  /// เพิ่มระยะ/น้ำมัน (เรียกจาก DataHub)  save = เขียนลงเครื่อง
  final Map<String, double> _acc = {};
  void addTrip(double km, double fuelL, double fuelKm) {
    for (final k in ['tripA', 'tripB', 'oilKm']) {
      _acc[k] = (_acc[k] ?? _d(k)) + km;
    }
    for (final k in ['fuelA', 'fuelB']) {
      _acc[k] = (_acc[k] ?? _d(k)) + fuelL;
    }
    for (final k in ['fuelKmA', 'fuelKmB']) {
      _acc[k] = (_acc[k] ?? _d(k)) + fuelKm;
    }
  }

  double live(String k) => _acc[k] ?? _d(k);

  void flushTrip() {
    _acc.forEach((k, v) => _p.setDouble(k, v));
    _acc.clear();
  }

  void resetTrip(String ab) {
    for (final k in ['trip$ab', 'fuel$ab', 'fuelKm$ab']) {
      _acc.remove(k);
      _p.setDouble(k, 0);
    }
    notifyListeners();
  }

  // ---------- เตือนเปลี่ยนถ่ายน้ำมันเครื่อง ----------
  int get oilEvery => _i('oilEvery', 5000);
  set oilEvery(int v) => _setI('oilEvery', v);
  void resetOil() {
    _acc.remove('oilKm');
    _p.setDouble('oilKm', 0);
    notifyListeners();
  }

  // ---------- เตือน (เสียง + แถบบนจอ) ----------
  bool get alertEct => _b('alertEct', true);
  set alertEct(bool v) => _setB('alertEct', v);
  bool get alertVolt => _b('alertVolt', true);
  set alertVolt(bool v) => _setB('alertVolt', v);
  int get alertVolt10 => _i('alertVolt10', 118);
  set alertVolt10(int v) => _setI('alertVolt10', v);
  bool get alertShift => _b('alertShift', false);
  set alertShift(bool v) => _setB('alertShift', v);
  int get alertShiftRpm => _i('alertShiftRpm', 0); // 0 = ใช้ค่าไฟ Shift
  set alertShiftRpm(int v) => _setI('alertShiftRpm', v);
  bool get alertSpeed => _b('alertSpeed', false);
  set alertSpeed(bool v) => _setB('alertSpeed', v);
  int get alertSpeedKmh => _i('alertSpeedKmh', 120);
  set alertSpeedKmh(int v) => _setI('alertSpeedKmh', v);
  /// เสียงของเตือนแต่ละชนิด (index ใน AlertEngine.sounds)
  int alertSound(String key) => _i('alertSnd_$key', const {'ect': 2, 'volt': 3, 'shift': 1, 'speed': 0, 'oil': 6}[key] ?? 0);
  void setAlertSound(String key, int v) => _setI('alertSnd_$key', v);
  /// หน้าเตือน: 0 แถบเล็ก, 1 เต็มจอแดงกะพริบเฉพาะเรื่องร้ายแรง (น้ำร้อน/แบตต่ำ), 2 เต็มจอทุกเรื่อง
  int get alertStyle => _i('alertStyle', 2);
  set alertStyle(int v) => _setI('alertStyle', v);
  int get alertVol => _i('alertVol', 80);
  set alertVol(int v) => _setI('alertVol', v);

  // ---------- โหมดกลางคืน (หรี่จอตามเวลา) ----------
  bool get nightOn => _b('nightOn', false);
  set nightOn(bool v) => _setB('nightOn', v);
  int get nightFrom => _i('nightFrom', 18);
  set nightFrom(int v) => _setI('nightFrom', v);
  int get nightTo => _i('nightTo', 6);
  set nightTo(int v) => _setI('nightTo', v);
  int get nightLevel => _i('nightLevel', 35);
  set nightLevel(int v) => _setI('nightLevel', v);
  int get dayLevel => _i('dayLevel', 0); // 0 = ตามระบบ
  set dayLevel(int v) => _setI('dayLevel', v);

  // ---------- บันทึกค่า (CSV) ----------
  bool get logOn => _b('logOn', false);
  set logOn(bool v) => _setB('logOn', v);
  int get logHz => _i('logHz', 5);
  set logHz(int v) => _setI('logHz', v);

  // ---------- หน้ากราฟ: 3 ค่า ----------
  List<int> get graphSel {
    final l = _p.getStringList('graphSel');
    final v = l?.map((e) => int.tryParse(e) ?? 0).toList() ?? [0, 2, 4];
    return v.length >= 1 && v.length <= 6 ? v : [0, 2, 4];
  }

  set graphSel(List<int> v) {
    _p.setStringList('graphSel', v.map((e) => '$e').toList());
    notifyListeners();
  }

  int get graphSec => _i('graphSec', 30);
  set graphSec(int v) => _setI('graphSec', v);

  // ---------- โปรไฟล์รถ + สำรองค่าตั้ง ----------
  static const _noCopy = {'profNames', 'profCur', 'lastProfileSwitch', 'ghToken', 'unlocked'};
  bool _isProfKey(String k) => k.startsWith('prof_') || _noCopy.contains(k);

  /// ค่าตั้งทั้งหมดเป็น JSON (ไม่รวมโปรไฟล์อื่น)
  Map<String, dynamic> snapshot() {
    flushTrip();
    final m = <String, dynamic>{};
    for (final k in _p.getKeys()) {
      if (_isProfKey(k)) continue;
      final v = _p.get(k);
      if (v is List) {
        m[k] = {'l': v.map((e) => '$e').toList()};
      } else if (v is bool) {
        m[k] = {'b': v};
      } else if (v is int) {
        m[k] = {'i': v};
      } else if (v is double) {
        m[k] = {'d': v};
      } else if (v is String) {
        m[k] = {'s': v};
      }
    }
    return m;
  }

  Future<void> applySnapshot(Map<String, dynamic> m) async {
    _acc.clear();
    for (final k in _p.getKeys().toList()) {
      if (!_isProfKey(k)) await _p.remove(k);
    }
    for (final e in m.entries) {
      if (_isProfKey(e.key)) continue;
      final v = e.value;
      if (v is! Map) continue;
      if (v.containsKey('b')) await _p.setBool(e.key, v['b'] == true);
      if (v.containsKey('i')) await _p.setInt(e.key, (v['i'] as num).toInt());
      if (v.containsKey('d')) await _p.setDouble(e.key, (v['d'] as num).toDouble());
      if (v.containsKey('s')) await _p.setString(e.key, '${v['s']}');
      if (v.containsKey('l')) await _p.setStringList(e.key, (v['l'] as List).map((x) => '$x').toList());
    }
    _gifCache.clear();
    _cpCache.clear();
    notifyListeners();
  }

  List<String> get profiles => _p.getStringList('profNames') ?? ['รถคันที่ 1'];
  int get profCur => _i('profCur', 0).clamp(0, profiles.length - 1);

  Future<void> switchProfile(int to) async {
    final names = profiles;
    if (to < 0 || to >= names.length || to == profCur) return;
    await _p.setString('prof_${profCur}', jsonEncode(snapshot()));
    final raw = _p.getString('prof_$to');
    await _p.setInt('profCur', to);
    if (raw != null) {
      try {
        await applySnapshot(jsonDecode(raw) as Map<String, dynamic>);
      } catch (_) {}
    }
    notifyListeners();
  }

  /// เพิ่มรถคันใหม่ (เริ่มจากค่าตั้งของคันปัจจุบัน)
  Future<void> addProfile(String name) async {
    final names = [...profiles, name];
    await _p.setStringList('profNames', names);
    await _p.setString('prof_${names.length - 1}', jsonEncode(snapshot()));
    await switchProfile(names.length - 1);
  }

  Future<void> renameProfile(int i, String name) async {
    final names = [...profiles];
    if (i < 0 || i >= names.length) return;
    names[i] = name;
    await _p.setStringList('profNames', names);
    notifyListeners();
  }

  Future<void> deleteProfile(int i) async {
    final names = [...profiles];
    if (names.length <= 1 || i == profCur) return;
    // เลื่อนข้อมูลโปรไฟล์ที่อยู่หลังขึ้นมา
    for (var k = i; k < names.length - 1; k++) {
      final nx = _p.getString('prof_${k + 1}');
      if (nx != null) {
        await _p.setString('prof_$k', nx);
      } else {
        await _p.remove('prof_$k');
      }
    }
    await _p.remove('prof_${names.length - 1}');
    names.removeAt(i);
    final cur = profCur;
    await _p.setStringList('profNames', names);
    if (i < cur) await _p.setInt('profCur', cur - 1);
    notifyListeners();
  }

  /// สำรองทั้งหมด (ค่าตั้งปัจจุบัน + ทุกโปรไฟล์)
  String exportAll() {
    final profs = <String, dynamic>{};
    for (var i = 0; i < profiles.length; i++) {
      final r = _p.getString('prof_$i');
      if (r != null) profs['$i'] = r;
    }
    return const JsonEncoder.withIndent(' ').convert({
      'app': 'didissnap',
      'ver': 1,
      'current': snapshot(),
      'profNames': profiles,
      'profCur': profCur,
      'profiles': profs,
    });
  }

  Future<bool> importAll(String text) async {
    try {
      final j = jsonDecode(text) as Map<String, dynamic>;
      if (j['app'] != 'didissnap') return false;
      final names = (j['profNames'] as List?)?.map((e) => '$e').toList() ?? ['รถคันที่ 1'];
      for (final k in _p.getKeys().toList()) {
        if (k.startsWith('prof_')) await _p.remove(k);
      }
      await _p.setStringList('profNames', names);
      await _p.setInt('profCur', ((j['profCur'] as num?) ?? 0).toInt().clamp(0, names.length - 1));
      final profs = (j['profiles'] as Map?) ?? {};
      for (final e in profs.entries) {
        await _p.setString('prof_${e.key}', '${e.value}');
      }
      await applySnapshot((j['current'] as Map).cast<String, dynamic>());
      return true;
    } catch (_) {
      return false;
    }
  }
}
