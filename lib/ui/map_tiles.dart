import 'dart:async';
import 'dart:collection';
import 'dart:io';
import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:flutter/foundation.dart';
import 'package:path_provider/path_provider.dart';

// ======================================================================
//  แผนที่พื้นหลัง (tile 256px, Web Mercator) — โหลดจากเน็ตแล้วเก็บไว้ในเครื่อง
//  ไปสนามที่เคยเปิดแผนที่ไว้แล้ว ไม่มีเน็ตก็ยังเห็นแผนที่
// ======================================================================
class MapStyle {
  const MapStyle(this.name, this.url, this.attr, this.maxZ, {this.subs = const []});
  final String name, url, attr;
  final int maxZ;
  final List<String> subs;

  static const none = MapStyle('ไม่มี', '', '', 0);
  static const all = [
    none,
    MapStyle('มืด', 'https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}.png', '© OpenStreetMap © CARTO', 19,
        subs: ['a', 'b', 'c', 'd']),
    MapStyle('ถนน', 'https://tile.openstreetmap.org/{z}/{x}/{y}.png', '© OpenStreetMap', 19),
    MapStyle('ดาวเทียม',
        'https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}', '© Esri', 19),
  ];
  static MapStyle of(int i) => all[i.clamp(0, all.length - 1)];
  bool get isNone => url.isEmpty;
}

/// Web Mercator: lat/lon → พิกัดโลกที่ zoom 0 (0..256)
ui.Offset merc(double lat, double lon) {
  final la = lat.clamp(-85.0, 85.0) * math.pi / 180;
  return ui.Offset((lon + 180) / 360 * 256, (1 - math.log(math.tan(la) + 1 / math.cos(la)) / math.pi) / 2 * 256);
}

/// เมตรต่อ 1 หน่วยโลก (zoom 0) ที่ละติจูดนี้
double mercMeters(double lat) => 40075016.686 * math.cos(lat * math.pi / 180) / 256;

class TileCache extends ChangeNotifier {
  TileCache._();
  static final i = TileCache._();

  final _mem = LinkedHashMap<String, ui.Image>();
  final _loading = <String>{};
  final _failed = <String, DateTime>{};
  Directory? _dir;
  HttpClient? _http;
  int _active = 0;
  final _queue = Queue<(MapStyle, int, int, int, String)>();

  /// คืนรูป tile ถ้ามีในหน่วยความจำ ไม่งั้นสั่งโหลด (เสร็จแล้วจะ notify ให้วาดใหม่)
  ui.Image? get(MapStyle st, int z, int x, int y) {
    final n = 1 << z;
    if (y < 0 || y >= n) return null;
    x = ((x % n) + n) % n;
    final k = '${st.name}/$z/$x/$y';
    final img = _mem.remove(k);
    if (img != null) {
      _mem[k] = img; // LRU: ย้ายไปท้าย
      return img;
    }
    final f = _failed[k];
    if (f != null && DateTime.now().difference(f).inSeconds < 30) return null;
    if (_loading.add(k)) {
      _queue.add((st, z, x, y, k));
      _pump();
    }
    return null;
  }

  void _pump() {
    while (_active < 6 && _queue.isNotEmpty) {
      final j = _queue.removeLast(); // ล่าสุดก่อน (จอปัจจุบัน)
      _active++;
      _load(j.$1, j.$2, j.$3, j.$4, j.$5).whenComplete(() {
        _active--;
        _pump();
      });
    }
  }

  Future<void> _load(MapStyle st, int z, int x, int y, String k) async {
    try {
      _dir ??= Directory('${(await getApplicationDocumentsDirectory()).path}/tiles');
      final f = File('${_dir!.path}/$k.img');
      Uint8List? bytes;
      if (await f.exists()) {
        bytes = await f.readAsBytes();
      } else {
        final sub = st.subs.isEmpty ? '' : st.subs[(x + y) % st.subs.length];
        final url = st.url
            .replaceAll('{s}', sub)
            .replaceAll('{z}', '$z')
            .replaceAll('{x}', '$x')
            .replaceAll('{y}', '$y');
        _http ??= HttpClient()
          ..connectionTimeout = const Duration(seconds: 8)
          ..userAgent = 'Didissnap/1.0 (personal car dashboard; lap timer)';
        final rq = await _http!.getUrl(Uri.parse(url));
        final rs = await rq.close().timeout(const Duration(seconds: 12));
        if (rs.statusCode != 200) throw 'HTTP ${rs.statusCode}';
        bytes = await consolidateHttpClientResponseBytes(rs);
        await f.parent.create(recursive: true);
        await f.writeAsBytes(bytes, flush: false);
      }
      final codec = await ui.instantiateImageCodec(bytes);
      final fr = await codec.getNextFrame();
      _mem[k] = fr.image;
      while (_mem.length > 160) {
        final old = _mem.keys.first;
        _mem.remove(old)?.dispose();
      }
      _failed.remove(k);
      notifyListeners();
    } catch (_) {
      _failed[k] = DateTime.now();
    } finally {
      _loading.remove(k);
    }
  }

  /// ขนาดแคชแผนที่ในเครื่อง (MB)
  Future<double> diskMb() async {
    try {
      _dir ??= Directory('${(await getApplicationDocumentsDirectory()).path}/tiles');
      if (!await _dir!.exists()) return 0;
      var n = 0;
      await for (final e in _dir!.list(recursive: true)) {
        if (e is File) n += await e.length();
      }
      return n / 1048576;
    } catch (_) {
      return 0;
    }
  }

  Future<void> clearDisk() async {
    try {
      if (_dir != null && await _dir!.exists()) await _dir!.delete(recursive: true);
    } catch (_) {}
  }
}

/// วาด tile ให้เต็มกรอบ: center = จุดกลาง (พิกัดโลก z0), sc = พิกเซลจอต่อ 1 หน่วยโลก
void paintTiles(ui.Canvas c, ui.Size size, MapStyle st, ui.Offset center, double sc) {
  if (st.isNone) return;
  final zf = math.log(sc) / math.ln2;
  final z = zf.round().clamp(1, st.maxZ);
  final n = 1 << z;
  final tw = 256.0 / n; // ขนาด tile ในหน่วยโลก
  final left = center.dx - size.width / 2 / sc, top = center.dy - size.height / 2 / sc;
  final right = center.dx + size.width / 2 / sc, bottom = center.dy + size.height / 2 / sc;
  final x0 = (left / tw).floor(), x1 = (right / tw).floor();
  final y0 = (top / tw).floor(), y1 = (bottom / tw).floor();
  if ((x1 - x0 + 1) * (y1 - y0 + 1) > 64) return;
  final p = ui.Paint()..filterQuality = ui.FilterQuality.medium;
  for (var ty = y0; ty <= y1; ty++) {
    for (var tx = x0; tx <= x1; tx++) {
      final dst = ui.Rect.fromLTWH(
        size.width / 2 + (tx * tw - center.dx) * sc,
        size.height / 2 + (ty * tw - center.dy) * sc,
        tw * sc + 0.5,
        tw * sc + 0.5,
      );
      final img = TileCache.i.get(st, z, tx, ty);
      if (img != null) {
        c.drawImageRect(img, ui.Rect.fromLTWH(0, 0, img.width.toDouble(), img.height.toDouble()), dst, p);
      } else {
        // ยังไม่มี: ลองใช้ tile zoom ต่ำกว่าที่โหลดไว้แล้วมาขยายแทน
        for (var dz = 1; dz <= 3 && z - dz >= 1; dz++) {
          final pz = z - dz, f = 1 << dz;
          final px = tx ~/ f, py = ty ~/ f;
          final k = TileCache.i._mem['${st.name}/$pz/${((px % (1 << pz)) + (1 << pz)) % (1 << pz)}/$py'];
          if (k == null) continue;
          final s = k.width / f;
          final src = ui.Rect.fromLTWH((tx - px * f) * s, (ty - py * f) * s, s, s);
          c.drawImageRect(k, src, dst, p);
          break;
        }
      }
    }
  }
}
