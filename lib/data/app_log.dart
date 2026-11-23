import 'dart:async';
import 'dart:io';

import 'package:path_provider/path_provider.dart';

/// บันทึกเหตุการณ์ / error ของแอป ไว้ส่งให้ช่วยดูปัญหา
///  เก็บในหน่วยความจำ + ไฟล์ (ไฟล์เกิน ~400 KB จะตัดครึ่งเก่าทิ้ง)
class AppLog {
  static final List<String> _mem = [];
  static File? _f;
  static final List<String> _pending = [];
  static Timer? _flushT;

  static Future<void> init() async {
    try {
      final d = await getApplicationDocumentsDirectory();
      _f = File('${d.path}/app_log.txt');
      if (_f!.existsSync() && _f!.lengthSync() > 400 * 1024) {
        final t = _f!.readAsStringSync();
        _f!.writeAsStringSync(t.substring(t.length ~/ 2));
      }
    } catch (_) {}
  }

  static String _ts() {
    final n = DateTime.now();
    String two(int x) => x.toString().padLeft(2, '0');
    return '${n.year}-${two(n.month)}-${two(n.day)} ${two(n.hour)}:${two(n.minute)}:${two(n.second)}';
  }

  /// เหตุการณ์ทั่วไป (สั้น ๆ)
  static void i(String msg) => _add('I', msg);

  /// error (เก็บ stack ด้วย)
  static void e(String msg, [Object? err, StackTrace? st]) {
    final s = StringBuffer(msg);
    if (err != null) s.write(' | $err');
    if (st != null) s.write('\n${st.toString().split('\n').take(12).join('\n')}');
    _add('E', s.toString());
  }

  static void _add(String lv, String msg) {
    final line = '${_ts()} $lv $msg';
    _mem.add(line);
    if (_mem.length > 400) _mem.removeRange(0, _mem.length - 400);
    _pending.add(line);
    _flushT ??= Timer(const Duration(seconds: 2), _flush);
  }

  static void _flush() {
    _flushT = null;
    final f = _f;
    if (f == null || _pending.isEmpty) return;
    try {
      f.writeAsStringSync('${_pending.join('\n')}\n', mode: FileMode.append);
    } catch (_) {}
    _pending.clear();
  }

  static List<String> get recent => List.unmodifiable(_mem);

  /// ข้อความทั้งหมดในไฟล์ (รวมครั้งก่อน ๆ)
  static String all() {
    _flush();
    try {
      return _f?.readAsStringSync() ?? _mem.join('\n');
    } catch (_) {
      return _mem.join('\n');
    }
  }

  static void clear() {
    _mem.clear();
    _pending.clear();
    try {
      _f?.writeAsStringSync('');
    } catch (_) {}
  }
}
