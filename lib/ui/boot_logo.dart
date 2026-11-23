import 'dart:async';
import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart' hide Text;
import '../i18n.dart';
import 'package:flutter/scheduler.dart';

import '../settings.dart';

/// โลโก้ตอนเปิดแอป แบบเดียวกับจอ ESP32-S3 4.3" (bootanim.h)
///  OYT (ตัวใหญ่ สีสุ่ม) / OBD2 GAUGE / DIDISSNAP.
///  - สุ่มฟอนต์ทุกครั้ง: 8-bit 60%, 16-bit 20%, 32-bit 20%
///  - สุ่มสีทุกครั้ง (8 สี ไม่ซ้ำกับครั้งก่อน)
///  - เด้งขึ้น + วงแหวน, แสงกวาด, พิมพ์ทีละตัว, กลิตช์, ประกายไฟ, แถบโหลด  ~6 วินาที  แตะจอ = ข้าม
///  วาดลงเฟรม 480x272 เหมือนจอจริง แล้วขยายเต็มจอแบบพิกเซลคม
class BootLogoPage extends StatefulWidget {
  const BootLogoPage({super.key, required this.settings, required this.next});
  final AppSettings settings;
  final Widget Function() next;

  @override
  State<BootLogoPage> createState() => _BootLogoPageState();
}

class _BootLogoPageState extends State<BootLogoPage> with SingleTickerProviderStateMixin {
  late final Ticker _tk;
  late final _Anim _a;
  ui.Image? _img;
  bool _busy = false, _ending = false;

  @override
  void initState() {
    super.initState();
    final s = widget.settings;
    final rnd = math.Random();
    final r = rnd.nextInt(10);
    final font = r < 6 ? 8 : (r < 8 ? 16 : 32);
    final last = s.bootLastColor;
    int ci;
    do {
      ci = rnd.nextInt(8);
    } while (ci == last);
    s.bootLastColor = ci;
    _a = _Anim(font, ci, rnd);
    _tk = createTicker(_tick)..start();
  }

  void _tick(Duration e) {
    final t = e.inMicroseconds / 1e6;
    if (t > _Anim.end) {
      _finish();
      return;
    }
    if (_busy) return;
    _busy = true;
    final px = _a.render(t);
    ui.decodeImageFromPixels(px, _Anim.w, _Anim.h, ui.PixelFormat.rgba8888, (img) {
      _busy = false;
      if (!mounted) {
        img.dispose();
        return;
      }
      final old = _img;
      setState(() => _img = img);
      old?.dispose();
    });
  }

  void _finish() {
    if (_ending || !mounted) return;
    _ending = true;
    _tk.stop();
    Navigator.of(context).pushReplacement(PageRouteBuilder<void>(
      transitionDuration: const Duration(milliseconds: 400),
      pageBuilder: (_, __, ___) => widget.next(),
      transitionsBuilder: (_, a, __, child) => FadeTransition(opacity: a, child: child),
    ));
  }

  @override
  void dispose() {
    _tk.dispose();
    _img?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.black,
      body: GestureDetector(
        behavior: HitTestBehavior.opaque,
        onTap: _finish,
        child: SizedBox.expand(child: CustomPaint(painter: _FbPainter(_img))),
      ),
    );
  }
}

class _FbPainter extends CustomPainter {
  _FbPainter(this.img);
  final ui.Image? img;
  @override
  void paint(Canvas c, Size s) {
    c.drawRect(Offset.zero & s, Paint()..color = Colors.black);
    final i = img;
    if (i == null) return;
    final k = math.min(s.width / i.width, s.height / i.height);
    final w = i.width * k, h = i.height * k;
    c.drawImageRect(
      i,
      Rect.fromLTWH(0, 0, i.width.toDouble(), i.height.toDouble()),
      Rect.fromLTWH((s.width - w) / 2, (s.height - h) / 2, w, h),
      Paint()..filterQuality = FilterQuality.none,
    );
  }

  @override
  bool shouldRepaint(_FbPainter o) => o.img != img;
}

// ======================================================================
//  พอร์ตจาก bootanim.h (Didissnap_S3) — วาดลงบัฟเฟอร์พิกเซลแบบเดียวกัน
// ======================================================================
class _C {
  const _C(this.r, this.g, this.b);
  final int r, g, b;
}

int _l8(int a, int b, double k) => (a + ((b - a) * k)).toInt().clamp(0, 255);
_C _mix(_C c, double k, int to) => _C(_l8(c.r, to, k), _l8(c.g, to, k), _l8(c.b, to, k));
_C _lerp(_C a, _C b, double k) => _C(_l8(a.r, b.r, k), _l8(a.g, b.g, k), _l8(a.b, b.b, k));
_C _scale(_C c, double k) => _mix(c, 1.0 - k, 0);
int _pk(_C c) => 0xFF000000 | (c.b << 16) | (c.g << 8) | c.r; // RGBA8888 (little-endian)
_C _un(int p) => _C(p & 0xFF, (p >> 8) & 0xFF, (p >> 16) & 0xFF);
double _c01(double v) => v < 0 ? 0 : (v > 1 ? 1 : v);
double _easeBack(double t) {
  const s = 1.70158;
  t -= 1;
  return t * t * ((s + 1) * t + s) + 1;
}

double _easeOut(double t) => 1 - math.pow(1 - t, 3).toDouble();

const List<_C> _pal = [
  _C(0xFF, 0xD2, 0x00), // yellow
  _C(0xFF, 0x3D, 0x8B), // pink
  _C(0x20, 0xD4, 0xFF), // cyan
  _C(0x3D, 0xFF, 0x6E), // green
  _C(0xFF, 0x7A, 0x1A), // orange
  _C(0xFF, 0x2A, 0x3A), // red
  _C(0xB0, 0x5C, 0xFF), // purple
  _C(0xF2, 0xF2, 0xF2), // white
];
const _white = _C(255, 255, 255);
const _grey = _C(0x99, 0x99, 0x99);
const _outl = _C(12, 8, 8);

const Map<String, List<int>> _font = {
  'A': [0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11],
  'B': [0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E],
  'C': [0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E],
  'D': [0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E],
  'E': [0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F],
  'G': [0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F],
  'I': [0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E],
  'N': [0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11],
  'O': [0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E],
  'P': [0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10],
  'R': [0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11],
  'S': [0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E],
  'T': [0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04],
  'U': [0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E],
  'Y': [0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04],
  '2': [0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F],
  '.': [0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C],
  ' ': [0, 0, 0, 0, 0, 0, 0],
};

class _Mask {
  _Mask(this.w, this.h) : m = Uint8List(w * h);
  final int w, h;
  final Uint8List m;
  int g(int x, int y) => (x < 0 || y < 0 || x >= w || y >= h) ? 0 : m[y * w + x];
}

_Mask _buildL0(String s, bool bold) {
  final n = s.length, cw = bold ? 6 : 5, adv = cw + 1;
  final k = _Mask(n * adv - 1, 7);
  for (var i = 0; i < n; i++) {
    final g = _font[s[i]] ?? _font[' ']!;
    for (var y = 0; y < 7; y++) {
      for (var x = 0; x < 5; x++) {
        if (g[y] & (0x10 >> x) != 0) {
          k.m[y * k.w + i * adv + x] = 1;
          if (bold) k.m[y * k.w + i * adv + x + 1] = 1;
        }
      }
    }
  }
  return k;
}

_Mask _epx(_Mask s) {
  final d = _Mask(s.w * 2, s.h * 2);
  for (var y = 0; y < s.h; y++) {
    for (var x = 0; x < s.w; x++) {
      final p = s.g(x, y), a = s.g(x, y - 1), b = s.g(x + 1, y), c = s.g(x - 1, y), dd = s.g(x, y + 1);
      var p1 = p, p2 = p, p3 = p, p4 = p;
      if (p == 0) {
        if (c != 0 && a != 0 && dd == 0 && b == 0) p1 = 1;
        if (a != 0 && b != 0 && c == 0 && dd == 0) p2 = 1;
        if (dd != 0 && c != 0 && b == 0 && a == 0) p3 = 1;
        if (b != 0 && dd != 0 && a == 0 && c == 0) p4 = 1;
      }
      d.m[(2 * y) * d.w + 2 * x] = p1;
      d.m[(2 * y) * d.w + 2 * x + 1] = p2;
      d.m[(2 * y + 1) * d.w + 2 * x] = p3;
      d.m[(2 * y + 1) * d.w + 2 * x + 1] = p4;
    }
  }
  return d;
}

_Mask _dilate(_Mask s, int e) {
  final d = _Mask(s.w + 2 * e, s.h + 2 * e);
  for (var y = 0; y < s.h; y++) {
    for (var x = 0; x < s.w; x++) {
      if (s.m[y * s.w + x] == 0) continue;
      for (var dy = 0; dy <= 2 * e; dy++) {
        for (var dx = 0; dx <= 2 * e; dx++) {
          d.m[(y + dy) * d.w + x + dx] = 1;
        }
      }
    }
  }
  return d;
}

class _Text {
  _Text(String s, bool bold, this.style, double targetH, this.col) : n = s.length {
    final level = (style == 8 || style == 0) ? 0 : (style == 16 ? 1 : 2);
    var k = _buildL0(s, bold);
    for (var i = 0; i < level; i++) {
      k = _epx(k);
    }
    m = k;
    cell = targetH / m.h;
    outE = style == 16 ? 1 : (style == 32 ? 2 : 0);
    out = outE > 0 ? _dilate(m, outE) : null;
  }
  late final _Mask m;
  _Mask? out;
  late final double cell;
  final int style, n;
  final _C col;
  late final int outE;
}

class _Spark {
  _Spark(this.x, this.y, this.vx, this.vy);
  double x, y, vx, vy, life = 1;
}

class _Anim {
  _Anim(this.f, int ci, this.rnd) : y = _pal[ci] {
    q = f == 8 ? 4 : (f == 16 ? 2 : 1);
    logo = _Text('OYT', true, f, 84, y);
    final rowH = f == 8 ? 21.0 : 28.0;
    r1 = _Text('OBD2 GAUGE', false, f, rowH, _white);
    r2 = _Text('DIDISSNAP.', false, f, rowH, _white);
    final lst = f == 32 ? 16 : 0;
    l1 = _Text('CONNECTING OBD2...', false, lst, 14, _grey);
    l2 = _Text('READY', false, lst, 14, _grey);
  }

  static const int w = 480, h = 272;
  static const double end = 6.0;
  final int f;
  final math.Random rnd;
  final _C y;
  late final int q;
  late final _Text logo, r1, r2, l1, l2;
  final Uint32List fb = Uint32List(w * h);
  final List<_Spark> sparks = [];
  double _lastT = 0, _acc = 0;

  double fr() => rnd.nextDouble();

  void fill(int x, int yy, int ww, int hh, int c) {
    if (x < 0) {
      ww += x;
      x = 0;
    }
    if (yy < 0) {
      hh += yy;
      yy = 0;
    }
    if (x + ww > w) ww = w - x;
    if (yy + hh > h) hh = h - yy;
    if (ww <= 0 || hh <= 0) return;
    for (var j = 0; j < hh; j++) {
      final o = (yy + j) * w + x;
      fb.fillRange(o, o + ww, c);
    }
  }

  void blend(int x, int yy, int ww, int hh, _C c, double a) {
    if (x < 0) {
      ww += x;
      x = 0;
    }
    if (yy < 0) {
      hh += yy;
      yy = 0;
    }
    if (x + ww > w) ww = w - x;
    if (yy + hh > h) hh = h - yy;
    if (ww <= 0 || hh <= 0) return;
    for (var j = 0; j < hh; j++) {
      for (var i = 0; i < ww; i++) {
        final o = (yy + j) * w + x + i;
        fb[o] = _pk(_lerp(_un(fb[o]), c, a));
      }
    }
  }

  int drawText(_Text t, double cx, double cy, double s, double reveal, double sweep, double fade) {
    final cs = t.cell * s;
    if (cs <= 0.05) return cx.toInt();
    final tw = t.m.w * cs, th = t.m.h * cs, x0 = cx - tw / 2, y0 = cy - th / 2;
    var cols = (t.m.w * reveal + 0.5).toInt();
    if (reveal < 1.0) {
      final adv = (t.m.w + 1) ~/ t.n;
      final nc = (reveal * t.n + 0.0001).toInt();
      cols = math.min(nc * adv, t.m.w);
    }
    int X(int i) => (x0 + i * cs).floor();
    int Y(int j) => (y0 + j * cs).floor();
    final base = _scale(t.col, fade);
    if (t.style == 8) {
      final sh = _pk(_scale(_mix(t.col, 0.65, 0), fade));
      for (var j = 0; j < t.m.h; j++) {
        for (var i = 0; i < cols; i++) {
          if (t.m.m[j * t.m.w + i] != 0) fill(X(i + 1), Y(j + 1), X(i + 2) - X(i + 1), Y(j + 2) - Y(j + 1), sh);
        }
      }
    } else if (t.outE > 0) {
      final oc = _pk(_outl), e = t.outE, ocols = cols + 2 * e, o = t.out!;
      for (var j = 0; j < o.h; j++) {
        for (var i = 0; i < ocols && i < o.w; i++) {
          if (o.m[j * o.w + i] != 0) fill(X(i - e), Y(j - e), X(i - e + 1) - X(i - e), Y(j - e + 1) - Y(j - e), oc);
        }
      }
    }
    final hi16 = _mix(base, 0.35, 255), lo16 = _mix(base, 0.30, 0);
    final g0 = _mix(base, 0.6, 255), g2 = _mix(base, 0.25, 0), g3 = _mix(base, 0.10, 255);
    for (var j = 0; j < t.m.h; j++) {
      final rf = (j + 0.5) / t.m.h;
      var c = base;
      if (t.style == 16) {
        c = rf < 0.42 ? hi16 : (rf > 0.60 ? lo16 : base);
      } else if (t.style == 32) {
        if (rf < 0.45) {
          c = _lerp(g0, base, rf / 0.45);
        } else if (rf < 0.55) {
          c = g2;
        } else {
          c = _lerp(g2, g3, (rf - 0.55) / 0.45);
        }
        if (rf > 0.04 && rf < 0.16) c = _lerp(c, _white, 0.35);
      }
      final c32 = _pk(c);
      int? swc;
      for (var i = 0; i < cols; i++) {
        if (t.m.m[j * t.m.w + i] == 0) continue;
        final xa = X(i), ya = Y(j), ww = X(i + 1) - xa, hh = Y(j + 1) - ya;
        var cc = c32;
        if (sweep > -999) {
          final bx = sweep - ya * 0.4, mx = xa + ww * 0.5;
          if (mx >= bx && mx <= bx + 34) cc = swc ??= _pk(_lerp(c, _white, 0.85));
        }
        fill(xa, ya, ww, hh, cc);
      }
    }
    return X(cols);
  }

  void ring(int cx, int cy, double r, _C c, double a, int blk) {
    final col = _pk(_scale(c, a));
    final steps = (r * 6.3).toInt() + 8;
    for (var th = 0; th < 3; th++) {
      final rr = r + th;
      for (var k = 0; k < steps; k++) {
        final an = k * 6.2831853 / steps;
        var x = cx + (math.cos(an) * rr).toInt(), yy = cy + (math.sin(an) * rr).toInt();
        if (blk > 1) {
          x = x ~/ blk * blk;
          yy = yy ~/ blk * blk;
          fill(x, yy, blk, blk, col);
        } else if (x >= 0 && x < w && yy >= 0 && yy < h) {
          fb[yy * w + x] = col;
        }
      }
      if (blk > 1) break;
    }
  }

  final Uint32List _row = Uint32List(w);
  void glitch(double t) {
    for (var yy = 0; yy < h;) {
      final hh = 4 + ((yy * 7) % 10);
      var dx = ((fr() - 0.5) * 30).toInt();
      if (q > 1) dx = dx ~/ q * q;
      for (var j = yy; j < yy + hh && j < h; j++) {
        final o = j * w;
        _row.setRange(0, w, fb, o);
        for (var x = 0; x < w; x++) {
          final sx = x - dx;
          fb[o + x] = (sx >= 0 && sx < w) ? _row[sx] : 0xFF000000;
        }
        if (dx > 6) {
          for (var x = 0; x < w - 4; x++) {
            fb[o + x] |= _row[x + 4] & 0xFF; // แยกสีแดงเหลื่อม
          }
        }
      }
      yy += hh + 8 + ((yy * 13) % 11);
    }
    final ly = (t * 900).toInt() % h;
    blend(0, ly, w, 2, const _C(0xFF, 0x00, 0x4C), 0.5);
  }

  /// ขยับประกายไฟเท่าจอจริง (~30 ครั้ง/วินาที) ไม่ว่ามือถือจะวาดกี่ fps
  void _stepSparks(double t, double sw) {
    _acc += (t - _lastT).clamp(0.0, 0.2);
    _lastT = t;
    while (_acc >= 1 / 30) {
      _acc -= 1 / 30;
      if (t > 0.55 && t < 0.75 && sparks.length < 40) {
        for (var i = 0; i < 8; i++) {
          final a = fr() * 6.2831853, v = 2 + fr() * 4;
          sparks.add(_Spark(240, 90, math.cos(a) * v, math.sin(a) * v - 1));
        }
      }
      if (sw > 0 && sw < 1 && fr() < 0.6 && sparks.length < 96) {
        sparks.add(_Spark(140 + sw * 200, 44 + fr() * 90, (fr() - 0.5) * 2, -fr() * 2));
      }
      for (final k in sparks) {
        k.x += k.vx;
        k.y += k.vy;
        k.vy += 0.12;
        k.life -= 0.025;
      }
      sparks.removeWhere((k) => k.life <= 0);
    }
  }

  Uint8List render(double t) {
    const logoY = 90.0, r1Y = 162.0, r2Y = 200.0;
    final fade = t > 5.4 ? 1 - _c01((t - 5.4) / 0.6) : 1.0;
    fb.fillRange(0, fb.length, 0xFF000000);

    final r = _c01((t - 0.45) / 0.9);
    if (r > 0 && r < 1) ring(240, logoY.toInt(), 46 + r * 200, y, fade * (1 - r) * 0.7, q > 1 ? 4 : 1);

    final p = _c01(t / 0.8), s = p > 0 ? _easeBack(p) : 0.0;
    final sw = _c01((t - 1.0) / 0.7);
    var sweepX = (sw > 0 && sw < 1) ? (-80 + sw * 640) : -10000.0;
    if (q > 1 && sweepX > -999) sweepX = (sweepX / q).floorToDouble() * q;
    if (s > 0.01) drawText(logo, 240, logoY, s, 1, sweepX, fade);

    const rs = [1.3, 2.1], ry = [r1Y, r2Y];
    final rt = [r1, r2];
    for (var k = 0; k < 2; k++) {
      if (t < rs[k]) continue;
      final frv = _c01((t - rs[k]) / 0.8);
      final ex = drawText(rt[k], 240, ry[k], 1, frv, -10000, fade);
      if (frv < 1 && (t * 8).toInt() % 2 == 0) {
        final cx = q > 1 ? ex ~/ q * q : ex;
        fill(cx, ry[k].toInt() - 8, f == 8 ? 8 : 10, 16, _pk(_scale(y, fade)));
      }
    }

    final bb = _easeOut(_c01((t - 3.0) / 1.6));
    if (t > 2.9) {
      fill(150, 236, 180, 4, _pk(_scale(const _C(0x2A, 0x2A, 0x2A), fade)));
      var bw = (180 * bb).toInt();
      if (f == 8) {
        bw = bw ~/ 12 * 12;
      } else if (f == 16) {
        bw = bw ~/ 6 * 6;
      }
      fill(150, 236, bw, 4, _pk(_scale(y, fade)));
      drawText(bb < 1 ? l1 : l2, 240, 254, 1, 1, -10000, fade);
    }

    _stepSparks(t, sw);
    final hiC = _mix(y, 0.7, 255);
    final z = f == 8 ? 4 : 2;
    for (final k in sparks) {
      final c = _pk(_scale(k.life > 0.5 ? hiC : y, fade * k.life));
      fill((k.x / z).floor() * z, (k.y / z).floor() * z, z, z, c);
    }

    if (t < 0.35 || (t > 2.05 && t < 2.2) || (t > 3.4 && t < 3.5)) glitch(t);
    return fb.buffer.asUint8List();
  }
}
