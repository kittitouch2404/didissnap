part of 'settings_page.dart';

// ======================================================================
//  เมนูแบบ Racing HUD (แบบ 3) + เลือกสีหลักได้
// ======================================================================
const List<Color> kMenuColors = [
  Color(0xFFFF7A00), Color(0xFFFF3D8B), Color(0xFF20D4FF), Color(0xFF3DFF6E),
  Color(0xFFFFD200), Color(0xFFFF2A3A), Color(0xFFB05CFF), Color(0xFFF2F2F2),
];

ThemeData _menuTheme(Color a) => ThemeData(
      useMaterial3: true,
      brightness: Brightness.dark,
      colorScheme: ColorScheme.fromSeed(seedColor: a, brightness: Brightness.dark, primary: a),
      scaffoldBackgroundColor: const Color(0xFF070707),
      appBarTheme: const AppBarTheme(backgroundColor: Color(0xFF0E0E0E)),
    );

class HudMenuPage extends StatefulWidget {
  const HudMenuPage({super.key, required this.settings, required this.hub});
  final AppSettings settings;
  final DataHub hub;
  @override
  State<HudMenuPage> createState() => _HudMenuPageState();
}

class _HudMenuPageState extends State<HudMenuPage> {
  Timer? _t;
  int _csv = 0;

  @override
  void initState() {
    super.initState();
    _count();
    _t = Timer.periodic(const Duration(seconds: 1), (_) {
      if (mounted) setState(() {});
    });
  }

  Future<void> _count() async {
    try {
      final d = await DataLogger.dir();
      final n = d.listSync().where((f) => f.path.endsWith('.csv')).length;
      if (mounted) setState(() => _csv = n);
    } catch (_) {}
  }

  @override
  void dispose() {
    _t?.cancel();
    super.dispose();
  }

  AppSettings get s => widget.settings;
  DataHub get hub => widget.hub;

  void _open(String title, Widget Function() body) {
    Navigator.of(context).push(MaterialPageRoute<void>(
      builder: (_) => AnimatedBuilder(
        animation: s,
        builder: (_, __) {
          final a = Color(s.menuColor);
          return Theme(
            data: _menuTheme(a),
            child: Scaffold(
              appBar: AppBar(
                toolbarHeight: 56,
                shape: Border(top: BorderSide(color: a, width: 4)),
                title: Text(title.toUpperCase(),
                    style: const TextStyle(fontSize: 22, fontWeight: FontWeight.w900, fontStyle: FontStyle.italic, letterSpacing: 1)),
              ),
              body: body(),
            ),
          );
        },
      ),
    ));
  }

  void _pickColor() {
    showDialog<void>(
      context: context,
      builder: (ctx) => AnimatedBuilder(
        animation: s,
        builder: (ctx, _) {
          final a = Color(s.menuColor);
          final hsv = HSVColor.fromColor(a);
          return Theme(
            data: _menuTheme(a),
            child: AlertDialog(
              title: const Text('สีเมนู'),
              content: SizedBox(
                width: 460,
                child: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.stretch, children: [
                  Wrap(spacing: 12, runSpacing: 12, children: [
                    for (final c in kMenuColors)
                      InkWell(
                        onTap: () => s.menuColor = c.toARGB32(),
                        customBorder: const CircleBorder(),
                        child: Container(
                          width: 46,
                          height: 46,
                          decoration: BoxDecoration(
                            color: c,
                            shape: BoxShape.circle,
                            border: Border.all(color: c.toARGB32() == s.menuColor ? Colors.white : Colors.transparent, width: 3),
                          ),
                        ),
                      ),
                  ]),
                  const SizedBox(height: 16),
                  const Text('เลือกเอง'),
                  Container(
                    height: 14,
                    margin: const EdgeInsets.symmetric(horizontal: 24, vertical: 6),
                    decoration: BoxDecoration(
                      borderRadius: BorderRadius.circular(7),
                      gradient: LinearGradient(colors: [
                        for (var h = 0; h <= 360; h += 60) HSVColor.fromAHSV(1, h % 360.0, 0.9, 1).toColor(),
                      ]),
                    ),
                  ),
                  Slider(
                    min: 0,
                    max: 359,
                    value: hsv.hue.clamp(0, 359),
                    onChanged: (h) => s.menuColor = HSVColor.fromAHSV(1, h, hsv.saturation < 0.15 ? 0.85 : hsv.saturation, 1).toColor().toARGB32(),
                  ),
                  const Text('ความสด (ซ้าย = ขาว)'),
                  Slider(
                    min: 0,
                    max: 1,
                    value: hsv.saturation,
                    onChanged: (v) => s.menuColor = HSVColor.fromAHSV(1, hsv.hue, v, 1).toColor().toARGB32(),
                  ),
                ]),
              ),
              actions: [FilledButton(onPressed: () => Navigator.pop(ctx), child: const Text('เสร็จ'))],
            ),
          );
        },
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: s,
      builder: (context, _) {
        final a = Color(s.menuColor);
        final conn = hub.state == LinkState.connected;
        final src = switch (s.source) {
          SourceType.demo => 'DEMO',
          SourceType.btElm => 'BT',
          SourceType.wifiElm => 'WIFI',
          SourceType.esp32 => 'ESP32',
        };
        final fa = s.live('fuelA'), fk = s.live('fuelKmA');
        final avg = fa > 0.02 && fk > 0.2 ? ' · ${(fk / fa).toStringAsFixed(1)} km/L' : '';
        var nCust = 0;
        for (var k = 0; k < AppSettings.nCustom; k++) {
          if (s.customPid(k).valid) nCust++;
        }
        final rec = s.logOn && conn;
        final cards = <(String, String, IconData, VoidCallback)>[
          ('การเชื่อมต่อ', '$src · ${conn ? 'ต่อแล้ว' : 'ยังไม่ต่อ'}${s.cydLink ? ' · CYD' : ''}', Icons.wifi_tethering,
              () => _open('การเชื่อมต่อ', () => ConnectionTab(settings: s, hub: hub))),
          ('หน้าปัด', 'redline ${s.redline} · ${s.pages.length} หน้า', Icons.speed,
              () => _open('หน้าปัด', () => DisplayTab(settings: s, hub: hub))),
          ('ทริป / เตือน', 'A ${s.live('tripA').toStringAsFixed(1)} km$avg', Icons.warning_amber_rounded,
              () => _open('ทริป / เตือน', () => TripAlertTab(settings: s, hub: hub))),
          ('บันทึก CSV', '$_csv ไฟล์ · ${s.logOn ? '${s.logHz} Hz' : 'ปิด'}', Icons.table_chart_outlined,
              () => _open('บันทึก CSV', () => LogTab(settings: s, hub: hub))),
          ('รถ / สำรอง', '${s.profiles.length} คัน · ใช้ ${s.profiles[s.profCur]}', Icons.directions_car_outlined,
              () => _open('รถ / สำรอง', () => ProfileTab(settings: s))),
          ('Custom PID', nCust == 0 ? 'ยังไม่ได้ตั้ง' : '$nCust ช่องใช้งาน', Icons.tune,
              () => _open('การเชื่อมต่อ', () => ConnectionTab(settings: s, hub: hub))),
          ('เปิดอัตโนมัติ', s.autoWake ? 'จอรถตื่น · เปิด' : (s.autoBoot || s.autoPower || s.autoBt ? 'เปิดอยู่' : 'ปิด'), Icons.power_settings_new,
              () => _open('เปิดอัตโนมัติ', () => AutoStartTab(settings: s))),
          ('โลโก้ / เสียง', '${switch (s.bootMode) { 2 => 'OYT สุ่ม', 1 => 'คลิป', _ => 'ไม่มีโลโก้' }} · ${KeySound.builtIn[s.keySound.clamp(0, KeySound.builtIn.length - 1)].$1}',
              Icons.auto_awesome, () => _open('หน้าปัด', () => DisplayTab(settings: s, hub: hub))),
        ];
        final small = <(String, VoidCallback)>[
          ('รูปพื้นหลัง', () => _open('รูปพื้นหลัง', () => PicturesTab(settings: s, hub: hub))),
          ('เกียร์', () => _open('เกียร์', () => GearTab(settings: s, hub: hub))),
          ('โค้ดเครื่องยนต์', () => _open('โค้ดเครื่องยนต์ (DTC)', () => DtcTab(hub: hub))),
          ('จอ ESP32', () => _open('จอ ESP32', () => Esp32Tab(settings: s, hub: hub))),
          (hub.updater.hasNew ? 'อัปเดต! build-${hub.updater.latest}' : 'อัปเดตแอป', _updateDialog),
        ];
        return Theme(
          data: _menuTheme(a),
          child: Scaffold(
            backgroundColor: const Color(0xFF070707),
            body: Column(children: [
              Container(height: 6, color: a),
              SizedBox(
                height: 84,
                child: Row(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
                  Material(
                    color: const Color(0xFF0E0E0E),
                    child: InkWell(
                      onTap: () => Navigator.of(context).maybePop(),
                      child: const SizedBox(width: 84, child: Icon(Icons.chevron_left, size: 40)),
                    ),
                  ),
                  const VerticalDivider(width: 1, color: Color(0xFF1E1E1E)),
                  const Padding(
                    padding: EdgeInsets.symmetric(horizontal: 24),
                    child: Column(mainAxisAlignment: MainAxisAlignment.center, crossAxisAlignment: CrossAxisAlignment.start, children: [
                      Text('SETTINGS', style: TextStyle(fontSize: 34, fontWeight: FontWeight.w900, fontStyle: FontStyle.italic, letterSpacing: 2, height: 1)),
                      Text('DIDISSNAP · OBD2 GAUGE · build-$kBuild', style: TextStyle(fontSize: 13, color: Color(0xFF8A8A8A), letterSpacing: 1)),
                    ]),
                  ),
                  const Spacer(),
                  _hdr('CAR', s.profiles[s.profCur].toUpperCase(), a,
                      () => _open('รถ / สำรอง', () => ProfileTab(settings: s))),
                  _hdr('LINK', conn ? '$src · OK' : src, conn ? kGreen : kAmber, () {
                    ScaffoldMessenger.of(context)
                      ..hideCurrentSnackBar()
                      ..showSnackBar(SnackBar(content: Text(hub.message.isEmpty ? '-' : hub.message)));
                  }),
                  _hdr('LOG', rec ? '● REC' : (s.logOn ? 'รอต่อรถ' : 'OFF'), rec ? const Color(0xFFFF4A55) : const Color(0xFF8A8A8A),
                      () => s.logOn = !s.logOn),
                  _hdr('สี', '', a, _pickColor, swatch: a),
                ]),
              ),
              const Divider(height: 1, color: Color(0xFF1E1E1E)),
              Expanded(
                child: Padding(
                  padding: const EdgeInsets.fromLTRB(20, 18, 20, 12),
                  child: LayoutBuilder(builder: (context, box) {
                    final cols = box.maxWidth > 900 ? 4 : (box.maxWidth > 560 ? 3 : 2);
                    final rows = (cards.length / cols).ceil();
                    final h = ((box.maxHeight - (rows - 1) * 12) / rows).clamp(120.0, 260.0);
                    return GridView.count(
                      crossAxisCount: cols,
                      mainAxisSpacing: 12,
                      crossAxisSpacing: 12,
                      childAspectRatio: (box.maxWidth - (cols - 1) * 12) / cols / h,
                      children: [
                        for (var i = 0; i < cards.length; i++) _card(i + 1, cards[i], a),
                      ],
                    );
                  }),
                ),
              ),
              Padding(
                padding: const EdgeInsets.fromLTRB(20, 0, 20, 14),
                child: Row(children: [
                  for (final b in small)
                    Expanded(
                      child: Padding(
                        padding: const EdgeInsets.symmetric(horizontal: 5),
                        child: Material(
                          color: const Color(0xFF161616),
                          shape: const RoundedRectangleBorder(
                            borderRadius: BorderRadius.only(topLeft: Radius.circular(4), topRight: Radius.circular(14), bottomLeft: Radius.circular(14), bottomRight: Radius.circular(4)),
                            side: BorderSide(color: Color(0xFF262626)),
                          ),
                          child: InkWell(
                            onTap: b.$2,
                            child: SizedBox(
                              height: 52,
                              child: Center(child: Text(b.$1, style: const TextStyle(fontSize: 17, fontWeight: FontWeight.w600))),
                            ),
                          ),
                        ),
                      ),
                    ),
                ]),
              ),
            ]),
          ),
        );
      },
    );
  }

  void _updateDialog() {
    final u = hub.updater;
    if (!u.busy && u.status.isEmpty) u.check();
    final tok = TextEditingController(text: s.ghToken);
    showDialog<void>(
      context: context,
      builder: (ctx) => Theme(
        data: _menuTheme(Color(s.menuColor)),
        child: ListenableBuilder(
          listenable: Listenable.merge([u, s]),
          builder: (ctx, _) => AlertDialog(
            title: const Text('อัปเดตแอป'),
            content: SizedBox(
              width: 520,
              child: Column(mainAxisSize: MainAxisSize.min, crossAxisAlignment: CrossAxisAlignment.start, children: [
                Text('เวอร์ชันนี้: build-${kBuild > 0 ? kBuild : '?'}'),
                Text(u.latest > 0 ? 'ล่าสุดบน GitHub: build-${u.latest}' : 'ล่าสุดบน GitHub: -'),
                const SizedBox(height: 6),
                Text(u.status, style: TextStyle(color: u.hasNew ? kGreen : Colors.white70, fontWeight: FontWeight.w700)),
                if (u.progress >= 0) Padding(padding: const EdgeInsets.only(top: 8), child: LinearProgressIndicator(value: u.progress)),
                const SizedBox(height: 10),
                TextField(
                  controller: tok,
                  obscureText: true,
                  decoration: const InputDecoration(
                    labelText: 'GitHub token (สำหรับ repo ส่วนตัว)',
                    hintText: 'github_pat_...',
                    isDense: true,
                  ),
                  onChanged: (v) => s.ghToken = v,
                ),
                const SizedBox(height: 4),
                const Text('token เก็บในเครื่องนี้เท่านั้น ไม่อยู่ในไฟล์ APK และไม่ถูกสำรองลงไฟล์ .zip',
                    style: TextStyle(fontSize: 12, color: Colors.white54)),
                SwitchListTile(
                  contentPadding: EdgeInsets.zero,
                  title: const Text('เช็กเองตอนเปิดแอป'),
                  value: s.updateAuto,
                  onChanged: (v) => s.updateAuto = v,
                ),
              ]),
            ),
            actions: [
              TextButton(onPressed: u.busy ? null : () => u.check(), child: const Text('เช็กอีกครั้ง')),
              FilledButton(
                onPressed: u.busy || u.assetApi.isEmpty ? null : u.downloadAndInstall,
                child: Text(u.hasNew ? 'โหลด + ติดตั้ง build-${u.latest}' : 'โหลดตัวล่าสุด'),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _hdr(String k, String v, Color c, VoidCallback onTap, {Color? swatch}) => Row(children: [
        const VerticalDivider(width: 1, color: Color(0xFF1E1E1E)),
        InkWell(
          onTap: onTap,
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: 22),
            child: Column(mainAxisAlignment: MainAxisAlignment.center, crossAxisAlignment: CrossAxisAlignment.start, children: [
              Text(k, style: const TextStyle(fontSize: 12, color: Color(0xFF8A8A8A))),
              if (swatch != null)
                Container(
                  width: 38,
                  height: 26,
                  margin: const EdgeInsets.only(top: 2),
                  decoration: BoxDecoration(color: swatch, borderRadius: BorderRadius.circular(6), border: Border.all(color: Colors.white54)),
                )
              else
                Text(v, style: TextStyle(fontSize: 22, fontWeight: FontWeight.w900, color: c)),
            ]),
          ),
        ),
      ]);

  Widget _card(int n, (String, String, IconData, VoidCallback) c, Color a) {
    const shape = RoundedRectangleBorder(
      borderRadius: BorderRadius.only(topLeft: Radius.circular(4), topRight: Radius.circular(22), bottomLeft: Radius.circular(22), bottomRight: Radius.circular(4)),
      side: BorderSide(color: Color(0xFF242424)),
    );
    return Material(
      color: const Color(0xFF111111),
      shape: shape,
      clipBehavior: Clip.antiAlias,
      child: InkWell(
        onTap: c.$4,
        child: Stack(children: [
          Positioned(
            right: 14,
            top: 2,
            child: Text(n.toString().padLeft(2, '0'),
                style: const TextStyle(fontSize: 42, fontWeight: FontWeight.w900, fontStyle: FontStyle.italic, color: Color(0xFF262626))),
          ),
          Padding(
            padding: const EdgeInsets.fromLTRB(20, 18, 20, 16),
            child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
              Icon(c.$3, color: a, size: 34),
              const Spacer(),
              Text(c.$1, style: const TextStyle(fontSize: 23, fontWeight: FontWeight.w900)),
              Text(c.$2, maxLines: 1, overflow: TextOverflow.ellipsis, style: const TextStyle(fontSize: 14, color: Color(0xFF9A9A9A))),
              const SizedBox(height: 6),
              Container(width: 48, height: 4, decoration: BoxDecoration(color: a, borderRadius: BorderRadius.circular(2))),
            ]),
          ),
        ]),
      ),
    );
  }
}
