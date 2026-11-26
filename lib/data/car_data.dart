/// ค่าจากรถล่าสุด (null = ยังไม่มีค่า / รถไม่รองรับ)
class CarData {
  /// นาฬิกากลางสำหรับประทับเวลาค่าความเร็ว (ใช้จับเวลาออกตัว)
  static final Stopwatch clock = Stopwatch()..start();

  double rpm = 0;
  double _speed = 0;

  /// เวลา (µs) และลำดับ ของค่าความเร็วล่าสุดที่ได้จากรถ
  int speedT = 0;
  int speedSeq = 0;

  double get speed => _speed;
  set speed(double v) {
    _speed = v;
    speedT = clock.elapsedMicroseconds;
    speedSeq++;
  }

  /// PID ที่รถรองรับ (Mode 01) และไบต์ดิบล่าสุดของแต่ละ PID
  final Set<int> supportedPids = {};
  final Map<int, List<int>> pidBytes = {};
  double? ect; // น้ำหล่อเย็น °C
  double? iat; // อากาศเข้า °C
  double? volt; // แบตเตอรี่ V
  double? load; // engine load %
  double? tps; // ลิ้นเร่ง %
  double? map; // kPa
  double? maf; // g/s
  double? fuel; // น้ำมัน %
  double? lambda; // lambda จากเซนเซอร์ A/F (PID 34/24) -> AFR = lambda x stoich
  double? cvtTemp; // อุณหภูมิน้ำมันเกียร์ CVT °C (ทดลอง)
  /// Custom PID 4 ช่อง: ค่าล่าสุด / เวลาที่ได้ / คำตอบดิบ
  final List<double?> cust = List<double?>.filled(4, null);
  final List<DateTime> custAt = List<DateTime>.filled(4, DateTime.fromMillisecondsSinceEpoch(0));
  final List<String> custRaw = List<String>.filled(4, '');
  double? custNow(int k) =>
      cust[k] != null && DateTime.now().difference(custAt[k]).inSeconds < 5 ? cust[k] : null;
  double speedRaw = 0; // ความเร็วดิบจาก OBD (ก่อนชดเชย) ใช้เทียบกับ GPS
  double? oil; // อุณหภูมิน้ำมันเครื่อง °C (PID 5C บางรุ่นมี)
  double? timing; // องศาจุดระเบิด ° ก่อนศูนย์ตายบน (PID 0E)
  /// อัตราสิ้นเปลือง: ลิตร/ชม. จาก MAF ล่าสุด + ตัวนับสำหรับค่าเฉลี่ย (นับเฉพาะช่วงที่อ่าน MAF ได้ต่อเนื่อง)
  double? fuelLph;
  double fuelUsedL = 0, fuelKm = 0;
  DateTime mafAt = DateTime.fromMillisecondsSinceEpoch(0);

  /// เรียกทุกครั้งที่ได้ MAF ใหม่  stoich = AFR ของน้ำมัน, dens = ความหนาแน่น g/L
  void mafSample(double stoich, double dens) {
    final m = maf;
    if (m == null) return;
    final now = DateTime.now();
    final lph = m * 3600 / (stoich * dens);
    final dt = now.difference(mafAt).inMilliseconds / 1000.0;
    if (dt > 0 && dt < 10) {
      fuelUsedL += lph * dt / 3600;
      fuelKm += speed * dt / 3600;
    }
    fuelLph = lph;
    mafAt = now;
  }
  String cvtRaw = ''; // คำตอบดิบล่าสุดจากกล่องเกียร์ (ไว้ทดสอบ PID)
  DateTime cvtAt = DateTime.fromMillisecondsSinceEpoch(0);
  double trip = 0; // km (คำนวณจากความเร็ว ไม่ถูกล้างตอนต่อใหม่)

  void reset() {
    rpm = 0;
    speed = 0;
    ect = null;
    iat = null;
    volt = null;
    load = null;
    tps = null;
    map = null;
    maf = null;
    fuel = null;
    lambda = null;
    cvtTemp = null;
    oil = null;
    for (var k = 0; k < 4; k++) {
      cust[k] = null;
    }
    timing = null;
    fuelLph = null;
    supportedPids.clear();
    pidBytes.clear();
  }
}

/// เดาเกียร์จากอัตราส่วน ความเร็ว / (รอบ/1000)
/// type: 0 = ธรรมดา · 1 = ออโต้ (มีทอร์คคอนเวอร์เตอร์ ลื่นได้ → เผื่อคลาดมากขึ้น) · 2 = CVT (ไม่มีเกียร์ตายตัว → โชว์ D)
String estimateGear(double rpm, double speed, List<double> ratios, {int type = 0}) {
  if (type == 2) return speed < 3 || rpm < 400 ? '-' : 'D';
  if (speed < 3 || rpm < 400) return type == 1 ? '-' : 'N';
  final r = speed / (rpm / 1000.0);
  var best = -1;
  var bestErr = 1e9;
  for (var i = 0; i < ratios.length; i++) {
    if (ratios[i] <= 0) continue;
    final e = (r - ratios[i]).abs() / ratios[i];
    if (e < bestErr) {
      bestErr = e;
      best = i;
    }
  }
  if (best < 0 || bestErr > (type == 1 ? 0.18 : 0.12)) return type == 1 ? 'D' : '-';
  return '${best + 1}';
}

/// พรีเซ็ตเกียร์: อัตราทดแต่ละเกียร์ + เฟืองท้าย + ขนาดยางเดิม → km/h ต่อ 1000 rpm
class GearPreset {
  const GearPreset(this.name, this.type, this.gears, this.fd, this.tire);
  final String name;
  final int type; // 0 ธรรมดา · 1 ออโต้ · 2 CVT
  final List<double> gears;
  final double fd;
  final String tire; // เช่น 175/65R14

  /// เส้นรอบวงยาง (เมตร) จาก 175/65R14
  static double circ(String t) {
    final m = RegExp(r'(\d+)/(\d+)R(\d+)').firstMatch(t);
    if (m == null) return 1.9;
    final w = double.parse(m[1]!), a = double.parse(m[2]!), r = double.parse(m[3]!);
    return 3.14159265 * (r * 25.4 + 2 * w * a / 100) / 1000;
  }

  List<double> get kmh => [for (final g in gears) circ(tire) * 60 / (g * fd)];
}

const kGearPresets = [
  GearPreset('Jazz / City GD · 5MT', 0, [3.462, 1.870, 1.321, 0.970, 0.757], 4.294, '175/65R14'),
  GearPreset('Jazz / City GE · 5MT', 0, [3.308, 1.870, 1.303, 0.949, 0.727], 4.62, '175/65R15'),
  GearPreset('Jazz GK RS · 6MT', 0, [3.461, 1.869, 1.235, 0.948, 0.809, 0.727], 4.62, '185/55R16'),
  GearPreset('Jazz GE · 5AT', 1, [2.996, 1.679, 1.067, 0.756, 0.550], 4.56, '175/65R15'),
  GearPreset('เกียร์ธรรมดา 5 สปีด (ทั่วไป)', 0, [3.4, 1.9, 1.3, 0.97, 0.76], 4.3, '185/60R15'),
  GearPreset('เกียร์ธรรมดา 6 สปีด (ทั่วไป)', 0, [3.4, 1.9, 1.3, 1.0, 0.82, 0.70], 4.3, '205/55R16'),
  GearPreset('CVT (GE / GK / FC / FK / City ฯลฯ)', 2, [], 1, ''),
];
