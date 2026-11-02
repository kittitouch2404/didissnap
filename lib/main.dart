import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:wakelock_plus/wakelock_plus.dart';

import 'data/hub.dart';
import 'settings.dart';
import 'ui/boot_video.dart';
import 'ui/dash_shell.dart';
import 'ui/paint_utils.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  final settings = await AppSettings.load();
  final hub = DataHub(settings);
  runApp(DashApp(settings: settings, hub: hub));
}

class DashApp extends StatefulWidget {
  const DashApp({super.key, required this.settings, required this.hub});
  final AppSettings settings;
  final DataHub hub;

  @override
  State<DashApp> createState() => _DashAppState();
}

class _DashAppState extends State<DashApp> {
  bool? _flip;
  bool? _keepOn;

  @override
  void initState() {
    super.initState();
    widget.settings.addListener(_applySystem);
    SystemChrome.setEnabledSystemUIMode(SystemUiMode.immersiveSticky);
    _applySystem();
    widget.hub.start();
  }

  void _applySystem() {
    final s = widget.settings;
    if (_flip != s.flip) {
      _flip = s.flip;
      SystemChrome.setPreferredOrientations(s.flip
          ? [DeviceOrientation.landscapeRight]
          : [DeviceOrientation.landscapeLeft]);
    }
    if (_keepOn != s.keepScreenOn) {
      _keepOn = s.keepScreenOn;
      WakelockPlus.toggle(enable: s.keepScreenOn);
    }
  }

  @override
  void dispose() {
    widget.settings.removeListener(_applySystem);
    widget.hub.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'OBD2_Didissnap',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        useMaterial3: true,
        brightness: Brightness.dark,
        colorScheme: ColorScheme.fromSeed(
          seedColor: kAccent,
          brightness: Brightness.dark,
        ),
        scaffoldBackgroundColor: const Color(0xFF0B0B0B),
      ),
      home: widget.settings.bootClip
          ? BootVideoPage(
              settings: widget.settings,
              next: () => DashShell(settings: widget.settings, hub: widget.hub),
            )
          : DashShell(settings: widget.settings, hub: widget.hub),
    );
  }
}
