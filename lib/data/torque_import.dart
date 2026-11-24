import 'custom_pid.dart';

/// ======================================================================
///  นำเข้ารายการ PID จากไฟล์ของแอป Torque (.csv)
///  คอลัมน์: Name, ShortName, ModeAndPID, Equation, Min Value, Max Value, Units, Header
///  (มีแถวหัวตารางหรือไม่ก็ได้ · คอลัมน์เกินมาไม่เป็นไร)
/// ======================================================================
class TorquePid {
  TorquePid(this.name, this.short, this.cmd, this.formula, this.unit, this.hdr, this.lo, this.hi, this.problem);
  final String name, short, cmd, formula, unit, hdr;
  final double lo, hi;

  /// ข้อความบอกว่าทำไมใช้ไม่ได้ (null = ใช้ได้)
  final String? problem;

  String get label {
    final s = short.trim().isNotEmpty && short.trim().length <= 11 ? short.trim() : name.trim();
    final a = s.replaceAll(RegExp(r'[^\x20-\x7E]'), '').trim();
    final b = a.isEmpty ? 'CUSTOM' : a;
    return b.length > 11 ? b.substring(0, 11) : b;
  }

  CustomPid toCustom() => CustomPid({
        'on': true,
        'name': label,
        'unit': unit.replaceAll(RegExp(r'[^\x20-\x7E°]'), '').trim(),
        'hdr': hdr,
        'cmd': cmd,
        'f': formula,
        'lo': lo,
        'hi': hi,
        'dec': formula.contains('/') || (hi - lo) <= 20 ? 1 : 0,
      });
}

/// แยกแถว CSV (รองรับ "ข้อความ, มีจุลภาค" และ "" ซ้อน)
List<List<String>> _csv(String text) {
  final rows = <List<String>>[];
  var row = <String>[];
  final cell = StringBuffer();
  var q = false;
  for (var i = 0; i < text.length; i++) {
    final c = text[i];
    if (q) {
      if (c == '"') {
        if (i + 1 < text.length && text[i + 1] == '"') {
          cell.write('"');
          i++;
        } else {
          q = false;
        }
      } else {
        cell.write(c);
      }
    } else if (c == '"') {
      q = true;
    } else if (c == ',') {
      row.add(cell.toString());
      cell.clear();
    } else if (c == '\n' || c == '\r') {
      if (c == '\r' && i + 1 < text.length && text[i + 1] == '\n') i++;
      row.add(cell.toString());
      cell.clear();
      if (row.any((e) => e.trim().isNotEmpty)) rows.add(row);
      row = <String>[];
    } else {
      cell.write(c);
    }
  }
  row.add(cell.toString());
  if (row.any((e) => e.trim().isNotEmpty)) rows.add(row);
  return rows;
}

/// สูตร Torque -> สูตรของแอป/C3 (คืน null = แปลงไม่ได้)
String? convertTorqueFormula(String f) {
  var s = f.toUpperCase().replaceAll(' ', '');
  if (s.isEmpty) return null;
  s = s.replaceAllMapped(RegExp(r'INT16\(([A-Z]):([A-Z])\)'), (m) => 'S16(${m[1]}*256+${m[2]})');
  // ทดสอบด้วยไบต์สมมติ: ต้องคิดได้ (ตัวอักษร 1 ตัว = 1 ไบต์, AA/AB... ใช้ไม่ได้)
  if (evalFormula(s, List<int>.filled(26, 100)) == null) return null;
  if (s.length > 39) return null; // ช่องสูตรใน C3 ยาวได้ 39 ตัว
  return s;
}

String _hdr(String h) {
  final c = CustomPid.cleanHex(h);
  if (c.isEmpty) return '';
  if (c.length == 3) return c;
  // header แบบ 29 บิต DAxxF1 -> 7E0 / 7E1 (เครื่อง 0x10, เกียร์ 0x18)
  final m = RegExp(r'^(?:18)?DA([0-9A-F]{2})F1$').firstMatch(c);
  if (m != null) {
    final a = int.parse(m[1]!, radix: 16);
    if (a >= 0x10 && a <= 0x48 && (a - 0x10) % 8 == 0) return (0x7E0 + (a - 0x10) ~/ 8).toRadixString(16).toUpperCase();
  }
  return '?$c';
}

List<TorquePid> parseTorqueCsv(String text) {
  final rows = _csv(text);
  if (rows.isEmpty) return const [];
  // หาคอลัมน์จากหัวตาราง (ถ้ามี)
  var idx = {'name': 0, 'short': 1, 'pid': 2, 'eq': 3, 'min': 4, 'max': 5, 'unit': 6, 'hdr': 7};
  var start = 0;
  final h = rows.first.map((e) => e.toLowerCase().replaceAll(RegExp(r'[^a-z]'), '')).toList();
  if (h.contains('modeandpid') || h.contains('equation')) {
    int f(List<String> keys, int def) {
      for (var i = 0; i < h.length; i++) {
        if (keys.contains(h[i])) return i;
      }
      return def;
    }
    idx = {
      'name': f(['name'], 0),
      'short': f(['shortname'], 1),
      'pid': f(['modeandpid', 'pid'], 2),
      'eq': f(['equation'], 3),
      'min': f(['minvalue', 'min'], 4),
      'max': f(['maxvalue', 'max'], 5),
      'unit': f(['units', 'unit'], 6),
      'hdr': f(['header'], 7),
    };
    start = 1;
  }
  String g(List<String> r, String k) => idx[k]! < r.length ? r[idx[k]!].trim() : '';
  final out = <TorquePid>[];
  for (final r in rows.skip(start)) {
    final name = g(r, 'name');
    if (name.isEmpty || name.startsWith('#')) continue;
    var cmd = CustomPid.cleanHex(g(r, 'pid').replaceFirst(RegExp(r'^0[xX]'), ''));
    final eq = g(r, 'eq');
    final fml = convertTorqueFormula(eq);
    final hdr = _hdr(g(r, 'hdr'));
    String? problem;
    if (cmd.length < 4 || cmd.length > 14 || cmd.length.isOdd) {
      problem = 'คำสั่ง "${g(r, 'pid')}" ไม่ถูก';
    } else if (fml == null) {
      problem = 'สูตร "$eq" ยังใช้ไม่ได้';
    } else if (hdr.startsWith('?')) {
      problem = 'header ${hdr.substring(1)} ยังไม่รองรับ';
    }
    out.add(TorquePid(name, g(r, 'short'), cmd, fml ?? eq, g(r, 'unit'), hdr.startsWith('?') ? '' : hdr,
        double.tryParse(g(r, 'min')) ?? 0, double.tryParse(g(r, 'max')) ?? 100, problem));
  }
  return out;
}
