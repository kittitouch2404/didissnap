import 'dart:async';

import 'package:flutter/material.dart' hide Text;
import '../i18n.dart';
import '../data/updater.dart';

import '../data/native.dart';

import '../data/hub.dart';
import '../settings.dart';
import 'live.dart';
import 'gif_pages.dart';
import 'skin_page.dart';
import 'graph_page.dart';
import 'paint_utils.dart';
import 'picture_dash.dart';
import 'pro_pages.dart';
import 'pro_pages2.dart';
import 'perf_page.dart';
import 'pids_page.dart';
import 'race_dash.dart';
import 'round_gauge.dart';
import 'settings_page.dart';

/// หน้าหลัก: ปัดซ้าย/ขวาสลับหน้าปัด + ปุ่ม MENU มุมขวาบน
class DashShell extends StatefulWidget {
  const DashShell({super.key, required this.settings, required this.hub});
  final AppSettings settings;
  final DataHub hub;

  @override
  State<DashShell> createState() => _DashShellState();
}

class _DashShellState extends State<DashShell> {
  late final PageController _pc;
  int _page = 0;
  List<String> _ids = const [];

  @override
  void initState() {
    super.initState();
    _ids = widget.settings.pages;
    final p = widget.settings.startPage;
    _page = (p < 0 || p >= _ids.length) ? 0 : p;
    _pc = PageController(initialPage: _page);
    widget.settings.addListener(_onSettings);
    _updateMode();
    _night();
    _nightT = Timer.periodic(const Duration(seconds: 30), (_) => _night());
  }

  // ---- โหมดกลางคืน: หรี่จอตามเวลา ----
  Timer? _nightT;
  double? _bright;
  void _night() {
    final s = widget.settings;
    final h = DateTime.now().hour;
    final isNight = s.nightOn &&
        (s.nightFrom <= s.nightTo ? (h >= s.nightFrom && h < s.nightTo) : (h >= s.nightFrom || h < s.nightTo));
    final v = isNight ? s.nightLevel / 100.0 : (s.dayLevel > 0 ? s.dayLevel / 100.0 : -1.0);
    if (v != _bright) {
      _bright = v;
      Native.brightness(v);
    }
  }

  /// บอกฝั่งอ่านค่าว่าหน้าที่แสดงอยู่ต้องการข้อมูลแบบไหน
  void _updateMode() {
    final id = _page < _ids.length ? _ids[_page] : '';
    if (!id.startsWith('gif')) widget.hub.focusPids = const ['11', '04'];   // หน้า GIF ตั้งเองตามช่องที่เลือก
    widget.hub.pollMode = switch (id) {
      'perf' => PollMode.perf,
      'pids' => PollMode.allPids,
      _ => PollMode.normal,
    };
  }

  int _col = 0;
  void _onSettings() {
    _night();
    if (_col != widget.settings.menuColor) setState(() => _col = widget.settings.menuColor);
    final ids = widget.settings.pages;
    if (ids.join(',') == _ids.join(',')) return;
    setState(() {
      _ids = ids;
      if (_page >= ids.length) {
        _page = ids.length - 1;
        _pc.jumpToPage(_page);
      }
    });
    _updateMode();
  }

  @override
  void dispose() {
    widget.settings.removeListener(_onSettings);
    _nightT?.cancel();
    _pc.dispose();
    super.dispose();
  }

  void _openMenu() {
    Navigator.of(context).push(MaterialPageRoute<void>(
      builder: (_) => HudMenuPage(settings: widget.settings, hub: widget.hub),
    ));
  }

  Widget _pageFor(String id) {
    final hub = widget.hub;
    Widget paint(CustomPainter Function(Frame f) mk) => LiveBuilder(
          key: ValueKey(id),
          hub: hub,
          builder: (_, f) =>
              CustomPaint(painter: mk(f), child: const SizedBox.expand()),
        );
    return switch (id) {
      'race' => paint((f) => RaceDashPainter(f)),
      'round' => paint((f) => RoundGaugePainter(f)),
      'sweep' => paint((f) => SweepPainter(f)),
      'arc' => paint((f) => ArcPainter(f)),
      'yellow' => paint((f) => YellowPainter(f)),
      'bars' => paint((f) => BarsPainter(f)),
      'redstripe' => paint((f) => RedStripePainter(f)),
      'ecu' => paint((f) => EcuPainter(f)),
      'center' => paint((f) => CenterTachPainter(f)),
      'perf' => PerfPage(key: ValueKey(id), hub: hub),
      'pids' => PidsPage(key: ValueKey(id), hub: hub),
      'graph' => GraphPage(key: ValueKey(id), hub: hub),
      _ when id.startsWith('skin:') => SkinPage(key: ValueKey(id), id: id.substring(5), hub: hub),
      _ when id.startsWith('gif') =>
        GifPage(key: ValueKey(id), index: int.parse(id.substring(3)) - 1, hub: hub),
      _ => LiveBuilder(
          key: ValueKey(id), hub: hub, builder: (_, f) => PictureDash(f: f)),
    };
  }

  @override
  Widget build(BuildContext context) {
    final hub = widget.hub;
    return Scaffold(
      backgroundColor: Colors.black,
      body: Stack(
        children: [
          PageView(
            controller: _pc,
            onPageChanged: (i) {
              setState(() => _page = i);
              widget.settings.startPage = i;
              _updateMode();
            },
            children: [for (final id in _ids) _pageFor(id)],
          ),
          Positioned(top: 6, left: 8, child: StatusChip(hub: hub)),
          // มีแอปเวอร์ชันใหม่
          Positioned(
            bottom: 14,
            right: 10,
            child: ListenableBuilder(
              listenable: hub.updater,
              builder: (context, _) {
                final u = hub.updater;
                if (!u.hasNew || u.dismissed) return const SizedBox();
                return Material(
                  color: const Color(0xE6151515),
                  borderRadius: BorderRadius.circular(12),
                  child: Padding(
                    padding: const EdgeInsets.fromLTRB(14, 4, 4, 4),
                    child: Row(mainAxisSize: MainAxisSize.min, children: [
                      const Icon(Icons.system_update, color: kGreen, size: 20),
                      const SizedBox(width: 8),
                      Text('มีแอปใหม่ ${verName(u.latest)}', style: const TextStyle(fontSize: 14)),
                      TextButton(
                          onPressed: u.busy ? null : u.downloadAndInstall,
                          child: Text(u.progress >= 0 ? '${(u.progress * 100).round()}%' : 'อัปเดต')),
                      IconButton(
                        visualDensity: VisualDensity.compact,
                        icon: const Icon(Icons.close, size: 18),
                        onPressed: u.dismiss,
                      ),
                    ]),
                  ),
                );
              },
            ),
          ),
          // แถบเตือน (น้ำร้อน / แบตต่ำ / เร็วเกิน / เปลี่ยนน้ำมันเครื่อง)
          Positioned(
            top: 6,
            left: 0,
            right: 0,
            child: IgnorePointer(child: Center(child: _AlertBanner(hub: hub))),
          ),
          // หน้าเตือนเต็มจอ (แดงกะพริบ) แตะ = ปิด + เงียบ 2 นาที
          Positioned.fill(child: _AlertFull(hub: hub)),
          Positioned(top: 6, right: 8, child: _MenuButton(onTap: _openMenu, color: Color(widget.settings.menuColor))),
          Positioned(
            bottom: 3,
            left: 0,
            right: 0,
            child: _Dots(count: _ids.length, index: _page, color: Color(widget.settings.menuColor)),
          ),
        ],
      ),
    );
  }
}

class StatusChip extends StatelessWidget {
  const StatusChip({super.key, required this.hub});
  final DataHub hub;

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: hub,
      builder: (context, _) {
        final col = switch (hub.state) {
          LinkState.connected => kGreen,
          LinkState.connecting => kAmber,
          LinkState.error => kRed,
          LinkState.idle => kGrey,
        };
        final label = switch (hub.settings.source) {
          SourceType.demo => 'DEMO',
          SourceType.btElm => 'BT',
          SourceType.wifiElm => 'WiFi',
          SourceType.esp32 => 'ESP32',
          SourceType.c3 => 'C3',
        };
        return GestureDetector(
          onTap: () => ScaffoldMessenger.of(context)
            ..hideCurrentSnackBar()
            ..showSnackBar(SnackBar(
                content: Text(hub.message.isEmpty ? '-' : hub.message))),
          child: Container(
            padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
            decoration: BoxDecoration(
              color: Colors.black54,
              borderRadius: BorderRadius.circular(12),
            ),
            child: Row(mainAxisSize: MainAxisSize.min, children: [
              Container(
                width: 8,
                height: 8,
                decoration: BoxDecoration(color: col, shape: BoxShape.circle),
              ),
              const SizedBox(width: 6),
              Text(label,
                  style: const TextStyle(fontSize: 11, color: Colors.white70)),
            ]),
          ),
        );
      },
    );
  }
}

class _MenuButton extends StatelessWidget {
  const _MenuButton({required this.onTap, required this.color});
  final VoidCallback onTap;
  final Color color;

  @override
  Widget build(BuildContext context) {
    return Material(
      color: Colors.black54,
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(6),
        side: BorderSide(color: color),
      ),
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(6),
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 5),
          child: Text('MENU',
              style: TextStyle(
                  color: color, fontWeight: FontWeight.w800, fontSize: 12)),
        ),
      ),
    );
  }
}

class _Dots extends StatelessWidget {
  const _Dots({required this.count, required this.index, required this.color});
  final int count, index;
  final Color color;

  @override
  Widget build(BuildContext context) {
    return IgnorePointer(
      child: Row(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          for (var i = 0; i < count; i++)
            Container(
              width: i == index ? 14 : 5,
              height: 5,
              margin: const EdgeInsets.symmetric(horizontal: 3),
              decoration: BoxDecoration(
                color: i == index ? color : Colors.white24,
                borderRadius: BorderRadius.circular(3),
              ),
            ),
        ],
      ),
    );
  }
}


class _AlertBanner extends StatefulWidget {
  const _AlertBanner({required this.hub});
  final DataHub hub;
  @override
  State<_AlertBanner> createState() => _AlertBannerState();
}

class _AlertBannerState extends State<_AlertBanner> {
  Timer? _blink;
  bool _on = true;
  @override
  void initState() {
    super.initState();
    _blink = Timer.periodic(const Duration(milliseconds: 450), (_) {
      if (mounted) setState(() => _on = !_on);
    });
  }

  @override
  void dispose() {
    _blink?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final al = widget.hub.alerts;
    return ListenableBuilder(
      listenable: al.changes,
      builder: (_, __) {
        var a = al.banner.value;
        if (a != null && _isFull(widget.hub, a) && !al.snoozed(a.$1)) a = null; // โชว์เต็มจออยู่แล้ว
        final rec = widget.hub.settings.logOn && widget.hub.state == LinkState.connected;
        return Row(mainAxisSize: MainAxisSize.min, children: [
          if (rec)
            Container(
              margin: const EdgeInsets.only(right: 8),
              padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
              decoration: BoxDecoration(color: Colors.black54, borderRadius: BorderRadius.circular(10)),
              child: Row(mainAxisSize: MainAxisSize.min, children: [
                Container(width: 8, height: 8, decoration: const BoxDecoration(color: kRed, shape: BoxShape.circle)),
                const SizedBox(width: 5),
                const Text('REC', style: TextStyle(fontSize: 11, color: Colors.white70, fontWeight: FontWeight.w700)),
              ]),
            ),
          if (a != null)
            AnimatedOpacity(
              opacity: _on ? 1 : 0.55,
              duration: const Duration(milliseconds: 200),
              child: Container(
                padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 6),
                decoration: BoxDecoration(
                  color: a.$3 ? const Color(0xE6D0101E) : const Color(0xE6E07800),
                  borderRadius: BorderRadius.circular(10),
                ),
                child: Row(mainAxisSize: MainAxisSize.min, children: [
                  const Icon(Icons.warning_amber_rounded, color: Colors.white, size: 22),
                  const SizedBox(width: 8),
                  Text(a.$2, style: const TextStyle(fontSize: 20, fontWeight: FontWeight.w900, color: Colors.white)),
                ]),
              ),
            ),
        ]);
      },
    );
  }
}


/// เรื่องนี้ต้องขึ้นเต็มจอไหม
bool _isFull(DataHub hub, (String, String, bool) a) {
  final st = hub.settings.alertStyle;
  return st == 2 || (st == 1 && a.$3);
}

class _AlertFull extends StatefulWidget {
  const _AlertFull({required this.hub});
  final DataHub hub;
  @override
  State<_AlertFull> createState() => _AlertFullState();
}

class _AlertFullState extends State<_AlertFull> {
  Timer? _t;
  bool _on = true;

  @override
  void initState() {
    super.initState();
    _t = Timer.periodic(const Duration(milliseconds: 380), (_) {
      if (mounted && widget.hub.alerts.banner.value != null) setState(() => _on = !_on);
    });
  }

  @override
  void dispose() {
    _t?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final al = widget.hub.alerts;
    return ListenableBuilder(
      listenable: al.changes,
      builder: (context, _) {
        final a = al.banner.value;
        if (a == null || !_isFull(widget.hub, a) || al.snoozed(a.$1)) return const SizedBox.shrink();
        final red = a.$3;
        final base = red ? const Color(0xFFD0101E) : const Color(0xFFE07800);
        final ic = switch (a.$1) {
          'ect' => Icons.thermostat,
          'volt' => Icons.battery_alert,
          'speed' => Icons.speed,
          'oil' => Icons.oil_barrel_outlined,
          _ => Icons.warning_amber_rounded,
        };
        return GestureDetector(
          behavior: HitTestBehavior.opaque,
          onTap: () => al.snooze(a.$1),
          child: AnimatedContainer(
            duration: const Duration(milliseconds: 160),
            color: _on ? base.withValues(alpha: 0.92) : Colors.black.withValues(alpha: 0.88),
            child: Center(
              child: Column(mainAxisSize: MainAxisSize.min, children: [
                Icon(ic, size: 120, color: _on ? Colors.white : base),
                const SizedBox(height: 8),
                Text('เตือน!',
                    style: TextStyle(fontSize: 40, fontWeight: FontWeight.w900, color: _on ? Colors.white : base, letterSpacing: 4)),
                Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 24),
                  child: Text(a.$2,
                      textAlign: TextAlign.center,
                      style: TextStyle(fontSize: 64, fontWeight: FontWeight.w900, color: _on ? Colors.white : base)),
                ),
                const SizedBox(height: 18),
                Text('แตะจอเพื่อปิด (เงียบ 2 นาที)',
                    style: TextStyle(fontSize: 18, color: (_on ? Colors.white : base).withValues(alpha: 0.8))),
              ]),
            ),
          ),
        );
      },
    );
  }
}
