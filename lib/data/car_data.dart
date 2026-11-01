/// ค่าจากรถล่าสุด (null = ยังไม่มีค่า / รถไม่รองรับ)
class CarData {
  double rpm = 0;
  double speed = 0;
  double? ect; // น้ำหล่อเย็น °C
  double? iat; // อากาศเข้า °C
  double? volt; // แบตเตอรี่ V
  double? load; // engine load %
  double? tps; // ลิ้นเร่ง %
  double? map; // kPa
  double? maf; // g/s
  double? fuel; // น้ำมัน %
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
  }
}

/// เดาเกียร์จากอัตราส่วน ความเร็ว / (รอบ/1000)
String estimateGear(double rpm, double speed, List<double> ratios) {
  if (speed < 3 || rpm < 400) return 'N';
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
  if (best < 0 || bestErr > 0.12) return '-';
  return '${best + 1}';
}
