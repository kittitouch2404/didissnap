/// Custom PID แบบ Torque: ผู้ใช้ตั้งคำสั่ง + สูตรเอง (ใช้ได้กับ Mode 01 / 21 / 22 และ header ของกล่องอื่น)
class CustomPid {
  CustomPid(Map<String, dynamic> m)
      : on = m['on'] == true,
        name = (m['name'] as String?) ?? '',
        unit = (m['unit'] as String?) ?? '',
        hdr = (m['hdr'] as String?) ?? '',
        cmd = (m['cmd'] as String?) ?? '',
        formula = (m['f'] as String?) ?? 'A',
        lo = (m['lo'] as num?)?.toDouble() ?? 0,
        hi = (m['hi'] as num?)?.toDouble() ?? 100,
        dec = (m['dec'] as num?)?.toInt() ?? 1;

  bool on;
  String name, unit;

  /// header (CAN 11 บิต) เช่น 7E1 = กล่องเกียร์  ว่าง = 7DF (ถามทุกกล่อง/เครื่องยนต์)
  String hdr;

  /// คำสั่งเต็ม hex เช่น 015C, 221234, 2101
  String cmd;

  /// สูตร: A B C D ... = ไบต์ข้อมูลหลัง echo ของคำสั่ง, ใช้ + - * / ( ) ได้, {A:7} = บิตที่ 7 ของ A
  String formula;
  double lo, hi;
  int dec;

  bool get valid => on && name.trim().isNotEmpty && cleanHex(cmd).length >= 4;
  String get label => name.trim().isEmpty ? 'CUSTOM' : name.trim().toUpperCase();

  Map<String, dynamic> toJson() => {
        'on': on, 'name': name, 'unit': unit, 'hdr': hdr, 'cmd': cmd,
        'f': formula, 'lo': lo, 'hi': hi, 'dec': dec,
      };

  static String cleanHex(String s) => s.toUpperCase().replaceAll(RegExp(r'[^0-9A-F]'), '');
}

/// คิดสูตรแบบ Torque  คืน null ถ้าสูตรผิดหรือไบต์ไม่พอ
double? evalFormula(String f, List<int> b) {
  try {
    final p = _Parser(f.toUpperCase().replaceAll(' ', ''), b);
    final v = p.expr();
    if (p.i != p.s.length || v.isNaN || v.isInfinite) return null;
    return v;
  } catch (_) {
    return null;
  }
}

class _Parser {
  _Parser(this.s, this.b);
  final String s;
  final List<int> b;
  int i = 0;

  String? get _c => i < s.length ? s[i] : null;

  double expr() {
    var v = term();
    while (_c == '+' || _c == '-') {
      final op = s[i++];
      final r = term();
      v = op == '+' ? v + r : v - r;
    }
    return v;
  }

  double term() {
    var v = unary();
    while (_c == '*' || _c == '/') {
      final op = s[i++];
      final r = unary();
      v = op == '*' ? v * r : v / r;
    }
    return v;
  }

  double unary() {
    if (_c == '-') {
      i++;
      return -unary();
    }
    if (_c == '+') {
      i++;
      return unary();
    }
    return atom();
  }

  int _byte(String ch) {
    final k = ch.codeUnitAt(0) - 65;
    if (k < 0 || k >= b.length) throw const FormatException('byte');
    return b[k];
  }

  double atom() {
    final c = _c;
    if (c == null) throw const FormatException('end');
    if (c == '(') {
      i++;
      final v = expr();
      if (_c != ')') throw const FormatException(')');
      i++;
      return v;
    }
    if (c == '{') {
      // {A:7} = บิต
      final e = s.indexOf('}', i);
      if (e < 0) throw const FormatException('}');
      final inner = s.substring(i + 1, e).split(':');
      i = e + 1;
      final bit = int.parse(inner[1]);
      return ((_byte(inner[0]) >> bit) & 1).toDouble();
    }
    if (RegExp(r'[A-Z]').hasMatch(c)) {
      i++;
      return _byte(c).toDouble();
    }
    final m = RegExp(r'\d+(\.\d+)?').matchAsPrefix(s, i);
    if (m == null) throw const FormatException('num');
    i = m.end;
    return double.parse(m.group(0)!);
  }
}
