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
        pk = m['pk'] != false,
        mapBtn = m['map'] != false,
        numFont = (m['nf'] as num?)?.toInt() ?? 0,
        numItalic = m['ni'] != false,
        numScale = ((m['ns'] as num?)?.toDouble() ?? 1.0).clamp(0.6, 1.4),
        labFont = (m['lf'] as num?)?.toInt() ?? 0,
        labCol = (m['lc'] as num?)?.toInt() ?? 0,
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

  /// 1 = ภาพค้างตามรอบ, 2 = วนเร็วตามความเร็วรถ, 0 = วนเร็วตามรอบ, 3 = ความเร็วปกติของไฟล์
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

  /// โชว์ MAX / MIN (ค่าสูงสุด/ต่ำสุด) ของแต่ละช่อง + ขีดบนแถบ
  bool pk;

  /// ปุ่มแผนที่มุมขวาบน (แบ่งจอกับ Google Maps)
  bool mapBtn;

  /// ฟอนต์ตัวเลข (kGifNumFonts) · ตัวเอียง · ขนาด (0.6-1.4 เท่า)
  int numFont;
  bool numItalic;
  double numScale;

  /// ฟอนต์ชื่อค่า/หน่วย (kGifLabFonts) · สี: 0 = เดิม (ม่วงอ่อน), 1 = สีเดียวกับตัวเลข, อื่น ๆ = ARGB
  int labFont;
  int labCol;

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
        'pk': pk,
        'map': mapBtn,
        'nf': numFont,
        'ni': numItalic,
        'ns': numScale,
        'lf': labFont,
        'lc': labCol,
        'hue2': hue2,
        'sat2': sat2,
        'rnd': rnd,
      };
}


/// ฟอนต์ตัวเลขหน้า GIF: (ชื่อที่โชว์, family ใน pubspec (null = ฟอนต์ระบบ), เป็นฟอนต์ปรับน้ำหนักได้)
const List<(String, String?, bool)> kGifNumFonts = [
  ('อนิเมะ (เดิม)', null, false),
  ('Russo One', 'RussoOne', false),
  ('Orbitron', 'Orbitron', true),
  ('Audiowide', 'Audiowide', false),
  ('Bebas Neue', 'BebasNeue', false),
  ('Teko', 'Teko', true),
  ('Black Ops One', 'BlackOpsOne', false),
  ('Bungee', 'Bungee', false),
  ('Michroma', 'Michroma', false),
  ('ดิจิทัล 7 ส่วน', 'DSEG7', false),
  ('Share Tech Mono', 'ShareTechMono', false),
  ('Rajdhani', 'Rajdhani', false),
];

/// ฟอนต์ชื่อค่า / หน่วย
const List<(String, String?, bool)> kGifLabFonts = [
  ('ปกติ (เดิม)', null, false),
  ('Rajdhani', 'Rajdhani', false),
  ('Orbitron', 'Orbitron', true),
  ('Teko', 'Teko', true),
  ('Bebas Neue', 'BebasNeue', false),
  ('Russo One', 'RussoOne', false),
  ('Michroma', 'Michroma', false),
  ('Share Tech Mono', 'ShareTechMono', false),
  ('Kanit (ไทย)', 'Kanit', false),
];

/// สีชื่อค่าให้เลือก (0 = เดิม, 1 = ตามตัวเลข)
const List<int> kGifLabColors = [
  0, 1, 0xFFFFFFFF, 0xFFB0B0B0, 0xFFFFD84A, 0xFFFF9A3C, 0xFFFF4A5A, 0xFFFF7AD0, 0xFFB07CFF, 0xFF4AC8FF, 0xFF4AFFB0, 0xFF9CFF4A,
];
