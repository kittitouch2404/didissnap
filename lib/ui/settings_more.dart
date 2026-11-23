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
        _SpeedCalSection(s: s, hub: hub),
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
        const _Header('เสียงของแต่ละเรื่อง'),
        for (final k in AlertEngine.kinds)
          ListTile(
            dense: true,
            title: Text(k.$2),
            trailing: Row(mainAxisSize: MainAxisSize.min, children: [
              DropdownButton<int>(
                value: AlertEngine.choices.contains(s.alertSound(k.$1)) ? s.alertSound(k.$1) : 1,
                items: [
                  for (final i in AlertEngine.choices)
                    DropdownMenuItem(value: i, child: Text(AlertEngine.sounds[i])),
                ],
                onChanged: (v) {
                  s.setAlertSound(k.$1, v ?? 0);
                  hub.alerts.play(k.$1, 'เตือน ${k.$2}');
                },
              ),
              IconButton(
                tooltip: tr('ลองฟัง'),
                icon: const Icon(Icons.play_circle_outline),
                onPressed: () => hub.alerts.play(k.$1, 'เตือน ${k.$2}'),
              ),
            ]),
          ),
        const _Header('หน้าเตือน'),
        for (final (v, t) in const [
          (2, 'เต็มจอแดงกะพริบ ทุกเรื่อง'),
          (1, 'เต็มจอเฉพาะเรื่องร้ายแรง (น้ำร้อน / แบตต่ำ) ที่เหลือเป็นแถบเล็ก'),
          (0, 'แถบเล็กด้านบนอย่างเดียว'),
        ])
          RadioListTile<int>(
            dense: true,
            title: Text(t),
            value: v,
            groupValue: s.alertStyle,
            onChanged: (x) => s.alertStyle = x ?? 2,
          ),
        const _Hint('หน้าเตือนเต็มจอ: แตะจอ = ปิด และเงียบเรื่องนั้น 2 นาที (ยังมีแถบเล็กค้างจนกว่าจะหาย)'),
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

  /// ลบไฟล์เก่า: กดเองเมื่ออยากล้าง (ไฟล์ที่กำลังบันทึกไม่ถูกลบ)
  Future<void> _clean(BuildContext context) async {
    final keep = await showDialog<int>(
      context: context,
      builder: (ctx) => SimpleDialog(
        title: const Text('ลบไฟล์เก่า'),
        children: [
          for (final k in [1, 5, 10, 20])
            SimpleDialogOption(onPressed: () => Navigator.pop(ctx, k), child: Text('เก็บไว้ $k ไฟล์ล่าสุด ลบที่เหลือ')),
          SimpleDialogOption(onPressed: () => Navigator.pop(ctx, -7), child: const Text('ลบไฟล์ที่เก่ากว่า 7 วัน')),
          SimpleDialogOption(onPressed: () => Navigator.pop(ctx, 0), child: const Text('ลบทั้งหมด', style: TextStyle(color: kRed))),
        ],
      ),
    );
    if (keep == null) return;
    final cur = widget.hub.logger.file;
    var n = 0;
    for (var i = 0; i < _files.length; i++) {
      final f = _files[i];
      if (f.path == cur) continue;
      final old = keep < 0
          ? DateTime.now().difference(f.lastModifiedSync()).inDays >= -keep
          : i >= keep;
      if (old) {
        try {
          f.deleteSync();
          n++;
        } catch (_) {}
      }
    }
    await _load();
    if (context.mounted) _snack(context, 'ลบแล้ว $n ไฟล์');
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
        Row(children: [
          const _Header('ไฟล์ที่บันทึกไว้'),
          const Spacer(),
          if (_files.length > 1)
            Padding(
              padding: const EdgeInsets.only(right: 12, top: 8),
              child: OutlinedButton.icon(
                icon: const Icon(Icons.cleaning_services_outlined),
                label: Text('ลบไฟล์เก่า (${_files.length} ไฟล์ · ${_size(_files.fold<int>(0, (a, f) => a + f.lengthSync()))})'),
                onPressed: () => _clean(context),
              ),
            ),
        ]),
        if (_files.isEmpty) const _Hint('ยังไม่มีไฟล์'),
        for (final f in _files)
          ListTile(
            dense: true,
            leading: const Icon(Icons.table_chart),
            title: Text(f.path.split('/').last),
            subtitle: Text(_size(f.lengthSync())),
            trailing: Row(mainAxisSize: MainAxisSize.min, children: [
              IconButton(
                tooltip: tr('บันทึกลง Download'),
                icon: const Icon(Icons.download),
                onPressed: () async {
                  final ok = await Native.saveToDownloads(f.path, f.path.split('/').last, 'text/csv');
                  if (context.mounted) _snack(context, ok ? 'บันทึกไว้ที่ Download/Didissnap แล้ว' : 'บันทึกไม่สำเร็จ');
                },
              ),
              IconButton(
                tooltip: tr('ลบ'),
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
        content: TextField(controller: c, autofocus: true, decoration: InputDecoration(hintText: tr('เช่น Jazz GK'))),
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
            title: const Text('สำรองทั้งหมด (ค่าตั้ง + รูป GIF / รูปพื้นหลัง / โลโก้ / เสียง)'),
            subtitle: const Text('ได้ไฟล์ .zip ใน Download/Didissnap (รวมทุกคัน)'),
            onTap: () async {
              _snack(context, 'กำลังสำรอง...');
              final r = await _backupZip(s);
              if (context.mounted) _snack(context, r ?? 'สำรองไม่สำเร็จ');
            },
          ),
          ListTile(
            leading: const Icon(Icons.restore),
            title: const Text('คืนค่าจากไฟล์สำรอง'),
            subtitle: const Text('เลือกไฟล์ didissnap_backup_....zip (หรือ .json แบบเก่า)'),
            onTap: () async {
              final p = await Native.pickFile();
              if (p == null) return;
              final r = await _restore(s, p);
              if (context.mounted) _snack(context, r);
            },
          ),
          const _Hint('ไฟล์สำรองอาจใหญ่ตามขนาดรูป GIF · คืนค่าแล้วรูปทุกหน้ากลับมาครบ ไม่ต้องเลือกใหม่'),
        ]);
      },
    );
  }
}


String _pct(int v1000) => '${v1000 >= 1000 ? '+' : ''}${((v1000 - 1000) / 10).toStringAsFixed(1)}%';

/// ชดเชยความเร็ว: ตั้งเอง หรือ อัตโนมัติด้วย GPS (+ ดูค่าเทียบสด ๆ)
class _SpeedCalSection extends StatefulWidget {
  const _SpeedCalSection({required this.s, required this.hub});
  final AppSettings s;
  final DataHub hub;
  @override
  State<_SpeedCalSection> createState() => _SpeedCalSectionState();
}

class _SpeedCalSectionState extends State<_SpeedCalSection> {
  @override
  void initState() {
    super.initState();
    Native.gpsPermission().then((_) => widget.hub.gps.view(true));
  }

  @override
  void dispose() {
    widget.hub.gps.view(false);
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final s = widget.s, g = widget.hub.gps;
    return ListenableBuilder(
      listenable: g,
      builder: (context, _) {
        final est = g.est;
        final gk = g.gpsKmh;
        return Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
            child: SegmentedButton<int>(
              segments: const [
                ButtonSegment(value: 0, label: Text('ตั้งเอง'), icon: Icon(Icons.tune)),
                ButtonSegment(value: 1, label: Text('อัตโนมัติด้วย GPS'), icon: Icon(Icons.satellite_alt)),
              ],
              selected: {s.speedCalMode},
              onSelectionChanged: (v) async {
                if (v.first == 1) await Native.gpsPermission();
                s.speedCalMode = v.first;
              },
            ),
          ),
          if (s.speedCalMode == 0)
            _StepRow('ชดเชยความเร็ว', s.speedCal1000, 850, 1150, 5, (v) => s.speedCal1000 = v, fmt: _pct),
          ListTile(
            dense: true,
            leading: Icon(Icons.gps_fixed, color: gk != null ? kGreen : Colors.white38),
            title: Text('GPS ${gk == null ? '--' : gk.toStringAsFixed(0)} km/h   ·   OBD (ดิบ) ${widget.hub.data.speedRaw.toStringAsFixed(0)} km/h'
                '   ·   ใช้อยู่ ${_pct((s.speedCal * 1000).round())}'),
            subtitle: Text(g.err.isNotEmpty
                ? g.err
                : (est == null
                    ? 'ยังไม่มีข้อมูล — ขับนิ่ง ๆ 40 km/h ขึ้นไปสักพัก (ทางตรง ฟ้าเปิด)'
                    : 'GPS วัดได้ ${_pct((est * 1000).round())}  จาก ${g.n} ครั้ง${s.speedCalMode == 1 && g.n < 20 ? ' (ใช้จริงเมื่อครบ 20 ครั้ง)' : ''}')),
          ),
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16),
            child: Wrap(spacing: 8, children: [
              if (est != null && s.speedCalMode == 0)
                FilledButton.tonal(
                  onPressed: () => s.speedCal1000 = (est * 1000).round(),
                  child: Text('ใช้ค่าที่ GPS วัดได้ (${_pct((est * 1000).round())})'),
                ),
              OutlinedButton(onPressed: g.reset, child: const Text('เริ่มเรียนรู้ใหม่')),
            ]),
          ),
          const _Hint('อัตโนมัติ: เก็บเฉพาะตอนวิ่ง 40+ km/h ความเร็วนิ่ง และ GPS แม่น แล้วเฉลี่ยไปเรื่อย ๆ\n'
              'มีผลกับ ความเร็ว / TRIP / km/L / CVT RATIO / เกียร์ และส่งให้จอ CYD ด้วย'),
        ]);
      },
    );
  }
}


// ---------- สำรอง/คืนค่าแบบ zip (รวมไฟล์รูป/เสียงที่ค่าตั้งอ้างถึง) ----------
final _pathRe = RegExp(r'/(?:data|storage)/[^"\\\s]+');

Future<String?> _backupZip(AppSettings s) async {
  try {
    final text = s.exportAll();
    final a = Archive();
    final map = <String, String>{};
    var i = 0;
    for (final m in _pathRe.allMatches(text)) {
      final p = m.group(0)!;
      if (map.containsKey(p)) continue;
      final f = File(p);
      if (!f.existsSync()) continue;
      final entry = 'media/${i++}_${p.split('/').last}';
      map[p] = entry;
      final b = f.readAsBytesSync();
      a.addFile(ArchiveFile(entry, b.length, b));
    }
    final tb = utf8.encode(text), mb = utf8.encode(jsonEncode(map));
    a.addFile(ArchiveFile('backup.json', tb.length, tb));
    a.addFile(ArchiveFile('media.json', mb.length, mb));
    final out = ZipEncoder().encode(a);
    final d = await getTemporaryDirectory();
    final n = DateTime.now();
    String two(int x) => x.toString().padLeft(2, '0');
    final name = 'didissnap_backup_${n.year}${two(n.month)}${two(n.day)}_${two(n.hour)}${two(n.minute)}.zip';
    final f = File('${d.path}/$name');
    await f.writeAsBytes(out);
    final ok = await Native.saveToDownloads(f.path, name, 'application/zip');
    try {
      f.deleteSync();
    } catch (_) {}
    return ok ? 'สำรองไว้ที่ Download/Didissnap/$name (${map.length} ไฟล์รูป/เสียง)' : null;
  } catch (e) {
    return null;
  }
}

Future<String> _restore(AppSettings s, String path) async {
  try {
    final bytes = File(path).readAsBytesSync();
    String text;
    var nMedia = 0;
    if (bytes.length > 2 && bytes[0] == 0x50 && bytes[1] == 0x4B) {
      final a = ZipDecoder().decodeBytes(bytes);
      ArchiveFile? find(String n) {
        for (final f in a.files) {
          if (f.name == n) return f;
        }
        return null;
      }
      final bj = find('backup.json');
      if (bj == null) return 'ไฟล์ zip ไม่ใช่ไฟล์สำรองของแอปนี้';
      text = utf8.decode(bj.content as List<int>);
      final mj = find('media.json');
      if (mj != null) {
        final map = (jsonDecode(utf8.decode(mj.content as List<int>)) as Map).cast<String, dynamic>();
        final docs = await getApplicationDocumentsDirectory();
        final dir = Directory('${docs.path}/restored');
        await dir.create(recursive: true);
        for (final e in map.entries) {
          final f = find('${e.value}');
          if (f == null) continue;
          final np = '${dir.path}/${'${e.value}'.split('/').last}';
          File(np).writeAsBytesSync(f.content as List<int>);
          text = text.replaceAll(e.key, np);
          nMedia++;
        }
      }
    } else {
      text = utf8.decode(bytes);
    }
    final ok = await s.importAll(text);
    return ok ? 'คืนค่าเรียบร้อย${nMedia > 0 ? ' (รูป/เสียง $nMedia ไฟล์)' : ''}' : 'ไฟล์ไม่ถูกต้อง';
  } catch (e) {
    return 'คืนค่าไม่สำเร็จ';
  } finally {
    try {
      File(path).deleteSync();
    } catch (_) {}
  }
}
