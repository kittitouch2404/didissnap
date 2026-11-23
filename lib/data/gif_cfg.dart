/// ค่าตั้งของหน้า GIF แต่ละหน้า (เก็บเป็น JSON ใน AppSettings: คีย์ gp0..gp4)
class GifCfg {
  GifCfg(Map<String, dynamic> m)
      : path = (m['path'] as String?) ?? '',
        style = (m['style'] as num?)?.toInt() ?? 0,
        mode = (m['mode'] as num?)?.toInt() ?? 1,
        lo = (m['lo'] as num?)?.toInt() ?? 0,
        hi = (m['hi'] as num?)?.toInt() ?? 0,
        hue = (m['hue'] as num?)?.toDouble() ?? 322,
        sat = (m['sat'] as num?)?.toDouble() ?? 0.64,
        bz = ((m['bz'] as num?)?.toDouble() ?? 1.0).clamp(1.0, 3.0),
        bx = ((m['bx'] as num?)?.toDouble() ?? 0.0).clamp(-1.0, 1.0),
        by = ((m['by'] as num?)?.toDouble() ?? 0.0).clamp(-1.0, 1.0),
        six = m['six'] == true,
        hue2 = (m['hue2'] as num?)?.toDouble() ?? -1,
        sat2 = (m['sat2'] as num?)?.toDouble() ?? 0.70,
        rnd = m['rnd'] == true,
        slots = _slots(m['slots']);

  static List<int> _slots(Object? v) {
    final l = (v is List) ? v.whereType<num>().map((e) => e.toInt()).toList() : <int>[];
    // 6 ช่องเสมอ: 3 ช่องแรก = โหมด 3 ค่า, โหมด 6 ค่าใช้ครบ
    const def = [0, 1, 2, 3, 4, 8];
    if (!l.every((e) => e >= 0 && e < 19)) return [...def];
    if (l.length == 3) return [...l, ...def.where((e) => !l.contains(e)).take(3)];
    return l.length == 6 ? l : [...def];
  }

  /// ไฟล์ GIF (ก๊อปไว้ในโฟลเดอร์แอป)
  String path;

  /// 0 = แบบ A, 1 = แบบ B (ภาพใหญ่)
  int style;

  /// 1 = ภาพค้างตามรอบ, 2 = วนเร็วตามความเร็วรถ, 0 = วนเร็วตามรอบ
  int mode;

  /// ช่วงรอบของโหมด 1 (hi = 0 -> redline)
  int lo, hi;

  /// สีตัวเลข (0-359) + ความสด (0-1)
  double hue, sat;

  /// แบบ B: ซูม (1-3 เท่า) + ตำแหน่ง (-1..1 ของระยะที่เลื่อนได้, 0 = กลาง)
  double bz, bx, by;

  /// สีโทนที่ 2 (กรอบใน / แถบ / ขอบตัวเลข)  hue2 < 0 = อัตโนมัติ (ม่วงคู่กับชมพูแบบเดิม)
  double hue2, sat2;
  double get hue2Eff => hue2 < 0 ? (hue - 57 + 360) % 360 : hue2;

  /// สุ่มสี 2 โทนใหม่ทุกครั้งที่เปิดแอป
  bool rnd;

  /// ช่องขวา: false = 3 ค่า (ตัวใหญ่), true = 6 ค่า (2 คอลัมน์)
  bool six;

  /// ค่าที่แสดงตอนนี้ (3 หรือ 6 ช่อง)
  List<int> get shown => six ? slots : slots.sublist(0, 3);

  /// ค่าในช่องขวา (เก็บ 6 ช่องเสมอ) (index ของ kGifMetrics)
  List<int> slots;

  Map<String, dynamic> toJson() => {
        'path': path,
        'style': style,
        'mode': mode,
        'lo': lo,
        'hi': hi,
        'hue': hue,
        'sat': sat,
        'bz': bz,
        'bx': bx,
        'by': by,
        'slots': slots,
        'six': six,
        'hue2': hue2,
        'sat2': sat2,
        'rnd': rnd,
      };
}
