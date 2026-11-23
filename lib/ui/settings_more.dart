part of 'settings_page.dart';

// ======================================================================
//  ทริป / เตือน / จอกลางคืน / ความเร็ว
// ======================================================================
class TripAlertTab extends StatelessWidget {
  const TripAlertTab({super.key, required this.settings, required this.hub});
  final AppSettings settings;
  final DataHub hub;

  String _avg(AppSettings s, String ab) {
    final f = s.live('fuel$ab'), k = s.live('fuelKm$ab');
    return f > 0.02 && k > 0.2 ? '${(k / f).toStringAsFixed(1)} km/L' : '-- km/L';
  }

  @override
  Widget build(BuildContext context) {
    final s = settings;
    return AnimatedBuilder(
      animation: s,
      builder: (context, _) => ListView(padding: const EdgeInsets.only(bottom: 24), children: [
        const _Header('ทริป (เก็บไว้ ไม่หายตอนปิดแอป)'),
        for (final ab in ['A', 'B'])
          _Live(
            builder: () => ListTile(
              dense: true,
              leading: CircleAvatar(radius: 14, child: Text(ab)),
              title: Text('${s.live('trip$ab').toStringAsFixed(1)} km   ·   ${_avg(s, ab)}   ·   ${s.live('fuel$ab').toStringAsFixed(2)} L'),
              subtitle: Text(ab == 'A' ? 'ทริป A = ค่าที่โชว์ในหน้าปัด (TRIP / FUEL AVG)' : 'ทริป B = ไว้นับระยะยาว เช่น ทั้งเดือน'),
              trailing: FilledButton.tonal(
                onPressed: () {
                  s.resetTrip(ab);
                  if (ab == 'A') hub.data.trip = 0;
                  _snack(context, 'รีเซ็ตทริป $ab แล้ว');
                },
                child: const Text('รีเซ็ต'),
              ),
            ),
          ),
        const _Hint('น้ำมันคำนวณจาก MAF + ค่า stoich (AFR SCALE) — เป็นค่าประมาณ ลองเทียบกับการเติมเต็มถังแล้วปรับ stoich ได้'),
        const _Header('ความเร็วให้ตรงจริง'),
        _StepRow('ชดเชยความเร็ว', s.speedCal1000, 850, 1150, 5, (v) => s.speedCal1000 = v,
            fmt: (v) => '${v >= 1000 ? '+' : ''}${((v - 1000) / 10).toStringAsFixed(1)}%'),
        const _Hint('เทียบกับ GPS (แอปแผนที่) ที่ 80-100 km/h เช่น OBD ขึ้น 100 แต่ GPS 96 -> ตั้ง -4.0%\n'
            'มีผลกับ ความเร็ว / TRIP / km/L / CVT RATIO / เกียร์ และส่งให้จอ CYD ด้วย'),
        const _Header('เปลี่ยนถ่ายน้ำมันเครื่อง'),
        _Live(
          builder: () {
            final km = s.live('oilKm'), left = s.oilEvery - km;
            return ListTile(
              dense: true,
              title: Text('วิ่งไปแล้ว ${km.round()} km  ·  ${left >= 0 ? 'เหลือ ${left.round()} km' : 'เกินมา ${(-left).round()} km'}'),
              subtitle: LinearProgressIndicator(
                value: s.oilEvery <= 0 ? 0 : (km / s.oilEvery).clamp(0.0, 1.0),
                color: left < 0 ? kRed : (left < 500 ? kAmber : kGreen),
              ),
              trailing: FilledButton.tonal(
                onPressed: () {
                  s.resetOil();
                  _snack(context, 'เริ่มนับใหม่แล้ว');
                },
                child: const Text('เพิ่งเปลี่ยน'),
              ),
            );
          },
        ),
        _StepRow('เตือนทุก', s.oilEvery, 0, 20000, 500, (v) => s.oilEvery = v, fmt: (v) => v == 0 ? 'ปิด' : '$v km'),
        const _Header('เสียง/แถบเตือน'),
        SwitchListTile(
          dense: true,
          title: Text('น้ำร้อน (≥ ${s.ectWarn} °C)'),
          subtitle: const Text('ตั้งองศาได้ในแท็บ หน้าปัด -> เตือนความร้อนน้ำที่'),
          value: s.alertEct,
          onChanged: (v) => s.alertEct = v,
        ),
        SwitchListTile(dense: true, title: const Text('แบตต่ำ (ตอนเครื่องติด)'), value: s.alertVolt, onChanged: (v) => s.alertVolt = v),
        if (s.alertVolt)
          _StepRow('ต่ำกว่า', s.alertVolt10, 110, 135, 1, (v) => s.alertVolt10 = v, fmt: (v) => '${(v / 10).toStringAsFixed(1)} V'),
        SwitchListTile(dense: true, title: const Text('บี๊บตอนถึงรอบเปลี่ยนเกียร์'), value: s.alertShift, onChanged: (v) => s.alertShift = v),
        if (s.alertShift)
          _StepRow('ที่รอบ', s.alertShiftRpm, 0, 12000, 100, (v) => s.alertShiftRpm = v,
              fmt: (v) => v == 0 ? 'ไฟ Shift (${s.shiftStart})' : '$v'),
        SwitchListTile(dense: true, title: const Text('ขับเร็วเกิน'), value: s.alertSpeed, onChanged: (v) => s.alertSpeed = v),
        if (s.alertSpeed)
          _StepRow('เกิน', s.alertSpeedKmh, 40, 220, 5, (v) => s.alertSpeedKmh = v, unit: ' km/h'),
        _StepRow('ความดังเสียงเตือน', s.alertVol, 10, 100, 10, (v) => s.alertVol = v, unit: '%'),
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16),
          child: Wrap(spacing: 8, children: [
            OutlinedButton(onPressed: () => Native.tone(1, 350, s.alertVol), child: const Text('ลอง: น้ำร้อน')),
            OutlinedButton(onPressed: () => Native.tone(2, 120, s.alertVol), child: const Text('ลอง: Shift')),
            OutlinedButton(onPressed: () => Native.tone(0, 160, s.alertVol), child: const Text('ลอง: เร็วเกิน')),
          ]),
        ),
        const _Header('โหมดกลางคืน (หรี่จอ)'),
        SwitchListTile(dense: true, title: const Text('หรี่จอตามเวลา'), value: s.nightOn, onChanged: (v) => s.nightOn = v),
        if (s.nightOn) ...[
          _StepRow('เริ่มหรี่', s.nightFrom, 0, 23, 1, (v) => s.nightFrom = v, fmt: (v) => '$v:00 น.'),
          _StepRow('กลับสว่าง', s.nightTo, 0, 23, 1, (v) => s.nightTo = v, fmt: (v) => '$v:00 น.'),
          _StepRow('ความสว่างกลางคืน', s.nightLevel, 5, 100, 5, (v) => s.nightLevel = v, unit: '%'),
        ],
        _StepRow('ความสว่างกลางวัน', s.dayLevel, 0, 100, 10, (v) => s.dayLevel = v, fmt: (v) => v == 0 ? 'ตามระบบ' : '$v%'),
        const _Hint('ปรับเฉพาะตอนแอปนี้อยู่ด้านหน้า — ออกไปแอปอื่นจะกลับเป็นความสว่างของระบบ'),
      ]),
    );
  }
}

/// รีเฟรชค่าสดทุก 1 วินาที
class _Live extends StatefulWidget {
  const _Live({required this.builder});
  final Widget Function() builder;
  @override
  State<_Live> createState() => _LiveState();
}

class _LiveState extends State<_Live> {
  Timer? _t;
  @override
  void initState() {
    super.initState();
    _t = Timer.periodic(const Duration(seconds: 1), (_) {
      if (mounted) setState(() {});
    });
  }

  @override
  void dispose() {
    _t?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => widget.builder();
}

// ======================================================================
//  บันทึกค่า CSV
// ======================================================================
class LogTab extends StatefulWidget {
  const LogTab({super.key, required this.settings, required this.hub});
  final AppSettings settings;
  final DataHub hub;
  @override
  State<LogTab> createState() => _LogTabState();
}

class _LogTabState extends State<LogTab> {
  List<File> _files = [];
  Timer? _t;

  @override
  void initState() {
    super.initState();
    _load();
    _t = Timer.periodic(const Duration(seconds: 2), (_) => _load());
  }

  @override
  void dispose() {
    _t?.cancel();
    super.dispose();
  }

  Future<void> _load() async {
    final d = await DataLogger.dir();
    final l = d.listSync().whereType<File>().where((f) => f.path.endsWith('.csv')).toList()
      ..sort((a, b) => b.path.compareTo(a.path));
    if (mounted) setState(() => _files = l);
  }

  String _size(int b) => b > 1 << 20 ? '${(b / (1 << 20)).toStringAsFixed(1)} MB' : '${(b / 1024).toStringAsFixed(0)} KB';

  @override
  Widget build(BuildContext context) {
    final s = widget.settings, lg = widget.hub.logger;
    return AnimatedBuilder(
      animation: s,
      builder: (context, _) => ListView(padding: const EdgeInsets.only(bottom: 24), children: [
        const _Header('บันทึกค่าลงไฟล์ (CSV)'),
        SwitchListTile(
          title: const Text('บันทึกตอนต่อรถอยู่'),
          subtitle: Text(lg.recording ? 'กำลังบันทึก · ${lg.rows} แถว · ${lg.file?.split('/').last}' : 'ปิดอยู่ / รอต่อรถ'),
          secondary: Icon(Icons.fiber_manual_record, color: lg.recording ? kRed : Colors.white38),
          value: s.logOn,
          onChanged: (v) => s.logOn = v,
        ),
        _StepRow('ความถี่', s.logHz, 1, 10, 1, (v) => s.logHz = v, unit: ' ครั้ง/วินาที'),
        const _Hint('เปิด/ปิดแอปหรือต่อรถใหม่ = ไฟล์ใหม่ · ค่าที่บันทึก: รอบ ความเร็ว TPS LOAD AFR BOOST MAF น้ำ IAT แบต องศาไฟ fuel trim '
            'น้ำมันเครื่อง ลิตร/ชม. CVT TEMP + Custom PID\n'
            'กด "บันทึกลง Download" แล้วเปิดด้วย Excel / Google Sheets ได้ (โฟลเดอร์ Download/Didissnap)'),
        const _Header('ไฟล์ที่บันทึกไว้'),
        if (_files.isEmpty) const _Hint('ยังไม่มีไฟล์'),
        for (final f in _files)
          ListTile(
            dense: true,
            leading: const Icon(Icons.table_chart),
            title: Text(f.path.split('/').last),
            subtitle: Text(_size(f.lengthSync())),
            trailing: Row(mainAxisSize: MainAxisSize.min, children: [
              IconButton(
                tooltip: 'บันทึกลง Download',
                icon: const Icon(Icons.download),
                onPressed: () async {
                  final ok = await Native.saveToDownloads(f.path, f.path.split('/').last, 'text/csv');
                  if (context.mounted) _snack(context, ok ? 'บันทึกไว้ที่ Download/Didissnap แล้ว' : 'บันทึกไม่สำเร็จ');
                },
              ),
              IconButton(
                tooltip: 'ลบ',
                icon: const Icon(Icons.delete_outline),
                onPressed: lg.file == f.path
                    ? null
                    : () async {
                        await f.delete();
                        _load();
                      },
              ),
            ]),
          ),
      ]),
    );
  }
}

// ======================================================================
//  รถหลายคัน + สำรอง/คืนค่า
// ======================================================================
class ProfileTab extends StatelessWidget {
  const ProfileTab({super.key, required this.settings});
  final AppSettings settings;

  Future<String?> _name(BuildContext context, String title, String init) async {
    final c = TextEditingController(text: init);
    return showDialog<String>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text(title),
        content: TextField(controller: c, autofocus: true, decoration: const InputDecoration(hintText: 'เช่น Jazz GK')),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx), child: const Text('ยกเลิก')),
          FilledButton(onPressed: () => Navigator.pop(ctx, c.text.trim()), child: const Text('ตกลง')),
        ],
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final s = settings;
    return AnimatedBuilder(
      animation: s,
      builder: (context, _) {
        final names = s.profiles, cur = s.profCur;
        return ListView(padding: const EdgeInsets.only(bottom: 24), children: [
          const _Header('รถ (โปรไฟล์)'),
          const _Hint('แต่ละคันจำค่าตั้งของตัวเองทั้งหมด: Bluetooth / redline / เกียร์ / AFR / Custom PID / หน้า GIF / ทริป ฯลฯ'),
          for (var i = 0; i < names.length; i++)
            ListTile(
              leading: Icon(i == cur ? Icons.directions_car : Icons.directions_car_outlined, color: i == cur ? kAccent : null),
              title: Text(names[i], style: TextStyle(fontWeight: i == cur ? FontWeight.w800 : null)),
              subtitle: Text(i == cur ? 'ใช้อยู่' : 'แตะเพื่อสลับมาใช้'),
              onTap: i == cur
                  ? null
                  : () async {
                      await s.switchProfile(i);
                      if (context.mounted) _snack(context, 'สลับเป็น ${names[i]} แล้ว');
                    },
              trailing: Row(mainAxisSize: MainAxisSize.min, children: [
                IconButton(
                  icon: const Icon(Icons.edit),
                  onPressed: () async {
                    final n = await _name(context, 'เปลี่ยนชื่อ', names[i]);
                    if (n != null && n.isNotEmpty) s.renameProfile(i, n);
                  },
                ),
                if (i != cur && names.length > 1)
                  IconButton(icon: const Icon(Icons.delete_outline), onPressed: () => s.deleteProfile(i)),
              ]),
            ),
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16),
            child: Align(
              alignment: Alignment.centerLeft,
              child: OutlinedButton.icon(
                icon: const Icon(Icons.add),
                label: const Text('เพิ่มรถอีกคัน (ก๊อปค่าตั้งจากคันนี้)'),
                onPressed: () async {
                  final n = await _name(context, 'ชื่อรถคันใหม่', 'รถคันที่ ${names.length + 1}');
                  if (n != null && n.isNotEmpty) await s.addProfile(n);
                },
              ),
            ),
          ),
          const _Header('สำรอง / คืนค่าตั้ง'),
          ListTile(
            leading: const Icon(Icons.save_alt),
            title: const Text('สำรองค่าตั้งทั้งหมด'),
            subtitle: const Text('ได้ไฟล์ .json ใน Download/Didissnap (รวมทุกคัน)'),
            onTap: () async {
              final d = await getTemporaryDirectory();
              final n = DateTime.now();
              final name = 'didissnap_backup_${n.year}${n.month.toString().padLeft(2, '0')}${n.day.toString().padLeft(2, '0')}.json';
              final f = File('${d.path}/$name');
              await f.writeAsString(s.exportAll());
              final ok = await Native.saveToDownloads(f.path, name, 'application/json');
              if (context.mounted) _snack(context, ok ? 'สำรองไว้ที่ Download/Didissnap/$name' : 'สำรองไม่สำเร็จ');
            },
          ),
          ListTile(
            leading: const Icon(Icons.restore),
            title: const Text('คืนค่าจากไฟล์สำรอง'),
            subtitle: const Text('เลือกไฟล์ didissnap_backup_....json'),
            onTap: () async {
              final t = await Native.pickText();
              if (t == null) return;
              final ok = await s.importAll(t);
              if (context.mounted) _snack(context, ok ? 'คืนค่าเรียบร้อย' : 'ไฟล์ไม่ถูกต้อง');
            },
          ),
          const _Hint('รูป GIF / รูปพื้นหลัง / โลโก้ / เสียงที่เลือกไว้ ไม่ได้อยู่ในไฟล์สำรอง — ลงแอปใหม่แล้วต้องเลือกไฟล์ใหม่'),
        ]);
      },
    );
  }
}
