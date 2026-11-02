/// ตารางชื่อ/สูตรของ OBD2 Mode 01 PID มาตรฐาน (SAE J1979)
class PidDef {
  const PidDef(this.name, this.unit, this.fmt);
  final String name;
  final String unit;
  final String Function(List<int> b) fmt;
}

int _a(List<int> b) => b.isNotEmpty ? b[0] : 0;
int _b(List<int> b) => b.length > 1 ? b[1] : 0;
int _ab(List<int> b) => _a(b) * 256 + _b(b);
String _f(num v, [int d = 0]) => v.toStringAsFixed(d);
String _pct(List<int> b) => _f(_a(b) * 100 / 255, 1);
String _temp(List<int> b) => _f(_a(b) - 40);
String _trim(List<int> b) => _f((_a(b) - 128) * 100 / 128, 1);
String _lambda(List<int> b) => _f(2 / 65536 * _ab(b), 3);

const _fuelSys = {
  1: 'Open loop (เครื่องเย็น)',
  2: 'Closed loop',
  4: 'Open loop (โหลด/ลดความเร็ว)',
  8: 'Open loop (ระบบผิดปกติ)',
  16: 'Closed loop (เซ็นเซอร์ผิดปกติ)',
};
const _fuelType = {
  1: 'เบนซิน',
  2: 'เมทานอล',
  3: 'เอทานอล',
  4: 'ดีเซล',
  5: 'LPG',
  6: 'CNG',
  8: 'ไฟฟ้า',
  9: 'Bifuel เบนซิน',
  17: 'Hybrid เบนซิน',
};

final Map<int, PidDef> kPidDefs = _build();

Map<int, PidDef> _build() {
  final m = <int, PidDef>{
    0x01: PidDef('สถานะไฟ MIL / จำนวน DTC', '', (b) {
      final a = _a(b);
      return '${(a & 0x80) != 0 ? 'MIL ติด' : 'MIL ดับ'} · ${a & 0x7F} รหัส';
    }),
    0x03: PidDef('ระบบน้ำมัน', '', (b) => _fuelSys[_a(b)] ?? '${_a(b)}'),
    0x04: PidDef('Engine load', '%', _pct),
    0x05: PidDef('อุณหภูมิน้ำ (Coolant)', '°C', _temp),
    0x06: PidDef('Short fuel trim B1', '%', _trim),
    0x07: PidDef('Long fuel trim B1', '%', _trim),
    0x08: PidDef('Short fuel trim B2', '%', _trim),
    0x09: PidDef('Long fuel trim B2', '%', _trim),
    0x0A: PidDef('แรงดันน้ำมัน', 'kPa', (b) => _f(_a(b) * 3)),
    0x0B: PidDef('MAP (แรงดันท่อไอดี)', 'kPa', (b) => _f(_a(b))),
    0x0C: PidDef('รอบเครื่อง', 'rpm', (b) => _f(_ab(b) / 4)),
    0x0D: PidDef('ความเร็ว', 'km/h', (b) => _f(_a(b))),
    0x0E: PidDef('องศาจุดระเบิด', '°', (b) => _f(_a(b) / 2 - 64, 1)),
    0x0F: PidDef('อุณหภูมิอากาศเข้า (IAT)', '°C', _temp),
    0x10: PidDef('MAF (ปริมาณอากาศ)', 'g/s', (b) => _f(_ab(b) / 100, 2)),
    0x11: PidDef('ลิ้นเร่ง (TPS)', '%', _pct),
    0x1C: PidDef('มาตรฐาน OBD', '', (b) => '${_a(b)}'),
    0x1F: PidDef('เวลาตั้งแต่ติดเครื่อง', 's', (b) => _f(_ab(b))),
    0x21: PidDef('ระยะทางที่ไฟ MIL ติด', 'km', (b) => _f(_ab(b))),
    0x22: PidDef('แรงดันรางหัวฉีด (เทียบสุญญากาศ)', 'kPa', (b) => _f(_ab(b) * 0.079, 1)),
    0x23: PidDef('แรงดันรางหัวฉีด', 'kPa', (b) => _f(_ab(b) * 10)),
    0x2C: PidDef('EGR ที่สั่ง', '%', _pct),
    0x2D: PidDef('EGR error', '%', _trim),
    0x2E: PidDef('Evap purge ที่สั่ง', '%', _pct),
    0x2F: PidDef('ระดับน้ำมัน', '%', _pct),
    0x30: PidDef('จำนวนวอร์มเครื่องตั้งแต่ลบโค้ด', 'ครั้ง', (b) => '${_a(b)}'),
    0x31: PidDef('ระยะทางตั้งแต่ลบโค้ด', 'km', (b) => _f(_ab(b))),
    0x32: PidDef('แรงดันไอระเหย Evap', 'Pa', (b) {
      var v = _ab(b);
      if (v > 32767) v -= 65536;
      return _f(v / 4, 1);
    }),
    0x33: PidDef('ความดันบรรยากาศ', 'kPa', (b) => _f(_a(b))),
    0x42: PidDef('แรงดันไฟกล่อง ECU', 'V', (b) => _f(_ab(b) / 1000, 2)),
    0x43: PidDef('Absolute load', '%', (b) => _f(_ab(b) * 100 / 255, 1)),
    0x44: PidDef('AFR ที่สั่ง (λ)', 'λ', _lambda),
    0x45: PidDef('ลิ้นเร่งสัมพัทธ์', '%', _pct),
    0x46: PidDef('อุณหภูมิอากาศภายนอก', '°C', _temp),
    0x47: PidDef('ลิ้นเร่ง B', '%', _pct),
    0x48: PidDef('ลิ้นเร่ง C', '%', _pct),
    0x49: PidDef('คันเร่ง D', '%', _pct),
    0x4A: PidDef('คันเร่ง E', '%', _pct),
    0x4B: PidDef('คันเร่ง F', '%', _pct),
    0x4C: PidDef('มอเตอร์ลิ้นเร่งที่สั่ง', '%', _pct),
    0x4D: PidDef('เวลาที่ไฟ MIL ติด', 'นาที', (b) => _f(_ab(b))),
    0x4E: PidDef('เวลาตั้งแต่ลบโค้ด', 'นาที', (b) => _f(_ab(b))),
    0x51: PidDef('ชนิดเชื้อเพลิง', '', (b) => _fuelType[_a(b)] ?? '${_a(b)}'),
    0x52: PidDef('เอทานอลในน้ำมัน', '%', _pct),
    0x59: PidDef('แรงดันรางหัวฉีด (absolute)', 'kPa', (b) => _f(_ab(b) * 10)),
    0x5A: PidDef('ตำแหน่งคันเร่งสัมพัทธ์', '%', _pct),
    0x5B: PidDef('แบตไฮบริดคงเหลือ', '%', _pct),
    0x5C: PidDef('อุณหภูมิน้ำมันเครื่อง', '°C', _temp),
    0x5D: PidDef('จังหวะฉีดน้ำมัน', '°', (b) => _f(_ab(b) / 128 - 210, 1)),
    0x5E: PidDef('อัตราการกินน้ำมัน', 'L/h', (b) => _f(_ab(b) / 20, 2)),
    0x61: PidDef('แรงบิดที่คนขับต้องการ', '%', (b) => _f(_a(b) - 125)),
    0x62: PidDef('แรงบิดจริง', '%', (b) => _f(_a(b) - 125)),
    0x63: PidDef('แรงบิดอ้างอิง', 'Nm', (b) => _f(_ab(b))),
    0xA6: PidDef('เลขไมล์ (Odometer)', 'km', (b) {
      if (b.length < 4) return '--';
      final v = (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
      return _f(v / 10, 1);
    }),
  };
  const pos = ['B1S1', 'B1S2', 'B1S3', 'B1S4', 'B2S1', 'B2S2', 'B2S3', 'B2S4'];
  for (var i = 0; i < 8; i++) {
    m[0x14 + i] = PidDef('ออกซิเจนเซ็นเซอร์ ${pos[i]}', 'V', (b) => _f(_a(b) / 200, 3));
    m[0x24 + i] = PidDef('Wideband O2 ${pos[i]} (λ)', 'λ', _lambda);
    m[0x34 + i] = PidDef('Wideband O2 ${pos[i]} (λ, กระแส)', 'λ', _lambda);
  }
  const cat = ['B1S1', 'B2S1', 'B1S2', 'B2S2'];
  for (var i = 0; i < 4; i++) {
    m[0x3C + i] = PidDef('อุณหภูมิแคท ${cat[i]}', '°C', (b) => _f(_ab(b) / 10 - 40));
  }
  return m;
}

/// PID ที่เป็นแค่ "รายการ PID ที่รองรับ" ไม่ใช่ค่าจริง
bool isBitmapPid(int pid) => pid % 0x20 == 0;

String pidHex(int pid) => pid.toRadixString(16).padLeft(2, '0').toUpperCase();
