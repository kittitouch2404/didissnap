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
        slots = _slots(m['slots']);

  static List<int> _slots(Object? v) {
    final l = (v is List) ? v.whereType<num>().map((e) => e.toInt()).toList() : <int>[];
    return l.length == 3 && l.every((e) => e >= 0 && e < 19) ? l : [0, 1, 2];
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

  /// ค่าใน 3 ช่องขวา (index ของ kGifMetrics)
  List<int> slots;

  Map<String, dynamic> toJson() => {
        'path': path,
        'style': style,
        'mode': mode,
        'lo': lo,
        'hi': hi,
        'hue': hue,
        'sat': sat,
        'slots': slots,
      };
}
