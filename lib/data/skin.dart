import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:archive/archive.dart';
import 'package:flutter/services.dart';
import 'package:path_provider/path_provider.dart';

import '../settings.dart';
import 'app_log.dart';

/// ======================================================================
///  สกินหน้าปัด (ออกแบบเองในเว็บ Didissnap Skin Designer → ไฟล์ .zip → นำเข้าในแอป)
///  ไฟล์ zip: skin.json + รูป / ฟอนต์ที่อ้างถึง (พาธสัมพัทธ์ในไฟล์ zip)
///  พิกัดทั้งหมดเป็นพิกเซลบนจอ 1280 x 720 (แอปย่อ/ขยายให้พอดีจอจริง)
///  มุม: องศา, 0 = 12 นาฬิกา, ตามเข็ม = บวก
/// ======================================================================

/// ค่าที่ผูกกับตัวเลข / แถบ / เข็ม / ไฟได้: key -> (ชื่อ, หน่วย, ทศนิยม, ต่ำสุด, สูงสุด)
const Map<String, (String, String, int, double, double)> kSkinMetrics = {
  'rpm': ('RPM', 'rpm', 0, 0, 8000),
  'speed': ('SPEED', 'km/h', 0, 0, 200),
  'gear': ('GEAR', '', 0, 0, 6),
  'ect': ('COOLANT', '°C', 0, 40, 130),
  'iat': ('IAT', '°C', 0, -10, 80),
  'volt': ('BATT', 'V', 1, 11, 15),
  'load': ('LOAD', '%', 0, 0, 100),
  'tps': ('TPS', '%', 0, 0, 100),
  'map': ('MAP', 'kPa', 0, 20, 250),
  'boost': ('BOOST', 'bar', 2, -1, 1.5),
  'maf': ('MAF', 'g/s', 1, 0, 120),
  'fuel': ('FUEL', '%', 0, 0, 100),
  'afr': ('AFR', '', 1, 10, 20),
  'cvt': ('CVT TEMP', '°C', 0, 30, 120),
  'cvtratio': ('CVT RATIO', '', 2, 0.4, 2.6),
  'oil': ('OIL TEMP', '°C', 0, 40, 140),
  'timing': ('TIMING', '°', 0, -10, 50),
  'fuelnow': ('FUEL NOW', 'km/L', 1, 0, 40),
  'fuelavg': ('FUEL AVG', 'km/L', 1, 0, 30),
  'fuellph': ('FUEL L/H', 'L/h', 1, 0, 20),
  'trip': ('TRIP', 'km', 1, 0, 999),
  'cust1': ('CUSTOM 1', '', 1, 0, 100),
  'cust2': ('CUSTOM 2', '', 1, 0, 100),
  'cust3': ('CUSTOM 3', '', 1, 0, 100),
  'cust4': ('CUSTOM 4', '', 1, 0, 100),
};

/// ฟอนต์ที่มีในแอปอยู่แล้ว (ชื่อ family ในไฟล์สกิน)
const List<String> kSkinBuiltinFonts = [
  'RussoOne', 'Orbitron', 'Audiowide', 'BebasNeue', 'Teko', 'BlackOpsOne', 'Bungee', 'Michroma', 'DSEG7',
  'ShareTechMono', 'Rajdhani', 'Kanit',
];

/// ฟอนต์ปรับน้ำหนักได้ (variable) → ใส่ wght ให้หนา
const Set<String> kSkinVarFonts = {'Orbitron', 'Teko'};

class SkinData {
  SkinData(this.id, this.dir, this.json, this.fonts);
  final String id, dir;
  final Map<String, dynamic> json;

  /// ชื่อฟอนต์ในไฟล์ -> family ที่ลงทะเบียนในแอป
  final Map<String, String> fonts;

  String get name => (json['name'] as String?) ?? id;
  double get w => (json['w'] as num?)?.toDouble() ?? 1280;
  double get h => (json['h'] as num?)?.toDouble() ?? 720;
  List<Map<String, dynamic>> get items =>
      ((json['items'] as List?) ?? const []).whereType<Map>().map((e) => e.cast<String, dynamic>()).toList();

  /// พาธไฟล์ในสกิน (กันพาธหลุดออกนอกโฟลเดอร์)
  String? file(String? rel) {
    if (rel == null || rel.isEmpty || rel.contains('..')) return null;
    final f = File('$dir/$rel');
    return f.existsSync() ? f.path : null;
  }

  String? family(String? f) {
    if (f == null || f.isEmpty) return null;
    return fonts[f] ?? (kSkinBuiltinFonts.contains(f) ? f : null);
  }
}

class SkinStore {
  static final Map<String, Future<SkinData>> _cache = {};
  static int _serial = 0;

  static Future<Directory> root() async {
    final d = Directory('${(await getApplicationDocumentsDirectory()).path}/skins');
    if (!d.existsSync()) d.createSync(recursive: true);
    return d;
  }

  static String _slug(String s) {
    final t = s.toLowerCase().replaceAll(RegExp(r'[^a-z0-9]+'), '-').replaceAll(RegExp(r'^-+|-+$'), '');
    return t.isEmpty ? 'skin${DateTime.now().millisecondsSinceEpoch % 100000}' : (t.length > 24 ? t.substring(0, 24) : t);
  }

  /// นำเข้าไฟล์ .zip → คืนชื่อสกิน (ชื่อซ้ำ = เขียนทับของเดิม)
  static Future<String> importZip(String path, AppSettings s) async {
    final bytes = await File(path).readAsBytes();
    final Archive a;
    try {
      a = ZipDecoder().decodeBytes(bytes);
    } catch (_) {
      throw 'ไม่ใช่ไฟล์ .zip';
    }
    ArchiveFile? sj;
    for (final f in a.files) {
      if (f.isFile && (f.name == 'skin.json' || f.name.endsWith('/skin.json'))) {
        sj = f;
        break;
      }
    }
    if (sj == null) throw 'ไม่มี skin.json ในไฟล์ — ใช้ไฟล์ที่ Export จากเว็บ Skin Designer';
    final prefix = sj.name.substring(0, sj.name.length - 'skin.json'.length);
    final Map<String, dynamic> j;
    try {
      j = (jsonDecode(utf8.decode(sj.content as List<int>)) as Map).cast<String, dynamic>();
    } catch (_) {
      throw 'skin.json อ่านไม่ได้';
    }
    if (j['format'] != 'didissnap-skin') throw 'ไฟล์นี้ไม่ใช่สกินของ Didissnap';
    final name = ((j['name'] as String?) ?? '').trim().isEmpty ? 'My skin' : (j['name'] as String).trim();
    final id = _slug(name);
    final dir = Directory('${(await root()).path}/$id');
    if (dir.existsSync()) dir.deleteSync(recursive: true);
    dir.createSync(recursive: true);
    var n = 0;
    for (final f in a.files) {
      if (!f.isFile || !f.name.startsWith(prefix)) continue;
      final rel = f.name.substring(prefix.length);
      if (rel.isEmpty || rel.contains('..') || rel.startsWith('/')) continue;
      final out = File('${dir.path}/$rel');
      out.parent.createSync(recursive: true);
      out.writeAsBytesSync(f.content as List<int>);
      n++;
    }
    _cache.remove(id);
    s.addSkin(id, name);
    AppLog.i('นำเข้าสกิน "$name" ($n ไฟล์)');
    return name;
  }

  static Future<void> delete(String id, AppSettings s) async {
    final dir = Directory('${(await root()).path}/$id');
    if (dir.existsSync()) dir.deleteSync(recursive: true);
    _cache.remove(id);
    s.removeSkin(id);
  }

  static Future<SkinData> load(String id) => _cache.putIfAbsent(id, () => _load(id));

  static Future<SkinData> _load(String id) async {
    final dir = '${(await root()).path}/$id';
    final f = File('$dir/skin.json');
    if (!f.existsSync()) throw 'ไม่พบไฟล์สกิน (ลองนำเข้าใหม่)';
    final j = (jsonDecode(await f.readAsString()) as Map).cast<String, dynamic>();
    // ฟอนต์ของสกินเอง: ลงทะเบียนชื่อใหม่ทุกครั้ง (นำเข้าทับแล้วได้ฟอนต์ใหม่)
    final fonts = <String, String>{};
    final fl = j['fonts'];
    if (fl is List) {
      final serial = ++_serial;
      for (final e in fl.whereType<Map>()) {
        final fam = e['family'] as String?, file = e['file'] as String?;
        if (fam == null || file == null || file.contains('..')) continue;
        final ff = File('$dir/$file');
        if (!ff.existsSync()) continue;
        final reg = 'sk${serial}_${id}_$fam';
        try {
          final b = await ff.readAsBytes();
          final loader = FontLoader(reg)..addFont(Future.value(ByteData.view(Uint8List.fromList(b).buffer)));
          await loader.load();
          fonts[fam] = reg;
        } catch (e) {
          AppLog.e('โหลดฟอนต์สกิน $fam ไม่ได้', e);
        }
      }
    }
    return SkinData(id, dir, j, fonts);
  }
}
