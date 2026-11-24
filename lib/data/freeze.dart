import 'hub.dart';
import 'pids.dart';

/// ======================================================================
///  Freeze frame (OBD Mode 02): ค่าของรถ ณ ตอนที่ไฟโชว์ติด
/// ======================================================================
class FreezeRow {
  FreezeRow(this.pid, this.name, this.value, this.unit);
  final int pid;
  final String name, value, unit;
}

class FreezeFrame {
  FreezeFrame(this.code, this.rows);

  /// รหัสที่ทำให้เก็บ freeze frame ('' = ไม่ทราบ)
  final String code;
  final List<FreezeRow> rows;
}

/// ค่าที่อ่าน (เรียงตามความสำคัญ)
const List<int> kFreezePids = [0x0C, 0x0D, 0x05, 0x04, 0x11, 0x0B, 0x10, 0x0F, 0x06, 0x07, 0x0E, 0x03, 0x42, 0x2F, 0x1F, 0x21];

/// ไบต์ข้อมูลหลัง "42 PID 00" จากคำตอบ (รับได้ทั้งแบบ ELM หลายบรรทัด และแบบ hex ล้วนจาก C3)
List<int>? freezeBytes(String? r, int pid) {
  if (r == null) return null;
  final lines = r.toUpperCase().split(RegExp(r'[\r\n]')).map((l) => l.replaceAll(' ', '').trim()).where((l) => l.isNotEmpty).toList();
  final framed = RegExp(r'^[0-9A-F]:');
  final fl = lines.where((l) => framed.hasMatch(l)).toList();
  final all = fl.isNotEmpty ? fl.map((l) => l.substring(2)).join() : lines.join();
  final key = '42${pid.toRadixString(16).padLeft(2, '0').toUpperCase()}00';
  final i = all.indexOf(key);
  if (i < 0) return null;
  final out = <int>[];
  final hex = all.substring(i + key.length);
  for (var j = 0; j + 1 < hex.length && out.length < 4; j += 2) {
    final v = int.tryParse(hex.substring(j, j + 2), radix: 16);
    if (v == null) break;
    out.add(v);
  }
  return out.isEmpty ? null : out;
}

String _code(List<int> b) {
  if (b.length < 2 || (b[0] == 0 && b[1] == 0)) return '';
  final n = b[0] >> 4;
  return '${'PCBU'[n >> 2]}${n & 3}${(b[0] & 15).toRadixString(16)}${b[1].toRadixString(16).padLeft(2, '0')}'.toUpperCase();
}

/// อ่าน freeze frame (null = ไม่มี / รถไม่รองรับ / แหล่งข้อมูลนี้อ่านไม่ได้)
Future<FreezeFrame?> readFreezeFrame(DataHub hub) async {
  // PID ที่มีใน freeze frame: 020000 (01-20), 022000 (21-40), 024000 (41-60)
  final sup = <int>{};
  for (final base in [0x00, 0x20, 0x40]) {
    final b = freezeBytes(await hub.obdRaw('02${base.toRadixString(16).padLeft(2, '0')}00'), base);
    if (b == null || b.length < 4) break;
    final mask = (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
    for (var i = 0; i < 32; i++) {
      if ((mask >> (31 - i)) & 1 == 1) sup.add(base + i + 1);
    }
    if (mask & 1 == 0) break;
  }
  if (sup.isEmpty) return null;
  var code = '';
  if (sup.contains(0x02)) {
    final b = freezeBytes(await hub.obdRaw('020200'), 0x02);
    if (b != null) code = _code(b);
    if (code.isEmpty) return null; // ไม่มี freeze frame เก็บไว้
  }
  final rows = <FreezeRow>[];
  for (final p in kFreezePids) {
    if (!sup.contains(p)) continue;
    final b = freezeBytes(await hub.obdRaw('02${p.toRadixString(16).padLeft(2, '0')}00'), p);
    if (b == null) continue;
    final d = kPidDefs[p];
    rows.add(FreezeRow(p, d?.name ?? 'PID ${pidHex(p)}', d?.fmt(b) ?? b.map((e) => e.toRadixString(16)).join(' '), d?.unit ?? ''));
  }
  return rows.isEmpty && code.isEmpty ? null : FreezeFrame(code, rows);
}
