import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:math' as math;
import 'dart:typed_data';

import 'native.dart';

/// รูป 1 ไฟล์ในบอร์ด
class BoardPic {
  BoardPic(this.name, this.size, this.on, this.rpm);
  final String name;
  final int size;
  final bool on;
  final int rpm;
  bool get isGif => name.endsWith('.gif');
}

/// สถานะรูปทั้งหมดในบอร์ด (ตอบจาก /api/pics)
class BoardPics {
  BoardPics(this.total, this.used, this.maxPics, this.auto, this.slots, this.pics);
  final int total, used, maxPics;
  final bool auto;
  final List<String> slots; // หน้า 9, 10, 11 ('' = รูปเดิม)
  final List<BoardPic> pics;
  int get free => math.max(0, total - used);

  static BoardPics fromJson(Map<String, dynamic> j) {
    final ps = (j['pics'] as List? ?? const [])
        .whereType<Map>()
        .map((p) => BoardPic('${p['n']}', (p['s'] as num?)?.toInt() ?? 0, (p['on'] ?? 1) != 0,
            (p['rpm'] as num?)?.toInt() ?? 0))
        .toList();
    final sl = (j['slots'] as List? ?? const []).map((e) => '$e').toList();
    while (sl.length < 3) {
      sl.add('');
    }
    return BoardPics((j['total'] as num?)?.toInt() ?? 0, (j['used'] as num?)?.toInt() ?? 0,
        (j['max'] as num?)?.toInt() ?? 40, (j['auto'] ?? 1) != 0, sl, ps);
  }
}

String _err(String code) => const {
      'nospace': 'พื้นที่ในบอร์ดไม่พอ — ลบรูปเก่าก่อน',
      'toobig': 'ไฟล์ใหญ่เกิน 1.5 MB',
      'full': 'บอร์ดเก็บรูปได้สูงสุด 40 รูป',
      'badfile': 'ไฟล์เสีย หรือไม่ใช่ JPG/GIF',
      'badname': 'ชื่อไฟล์ไม่ถูกต้อง',
      'nofs': 'บอร์ดยังไม่มีพื้นที่เก็บรูป (เช็ค Partition Scheme ตอนแฟลช)',
      'nofile': 'ไม่พบรูปนี้ในบอร์ด',
    }[code] ??
    'บอร์ดตอบว่า $code';

/// คุยกับ DidissnapPics.h บนบอร์ดผ่าน HTTP (พอร์ต 80 ตัวเดียวกับหน้าเว็บตั้งค่า)
class Esp32Pics {
  Esp32Pics(this.host);
  final String host;

  Uri _u(String path, [Map<String, String>? q]) =>
      Uri(scheme: 'http', host: host, port: 80, path: path, queryParameters: q);
  String imageUrl(String name) => 'http://$host/pics/$name';

  HttpClient _client() => HttpClient()..connectionTimeout = const Duration(seconds: 5);

  Future<BoardPics> _read(HttpClientResponse r) async {
    final body = await r.transform(utf8.decoder).join();
    Map<String, dynamic> j;
    try {
      j = jsonDecode(body) as Map<String, dynamic>;
    } catch (_) {
      throw Exception(r.statusCode == 404
          ? 'บอร์ดยังไม่รองรับรูป (ต้องแฟลชโค้ดที่มี DidissnapPics.h)'
          : 'บอร์ดตอบไม่ถูกต้อง (${r.statusCode})');
    }
    if (j['ok'] != true) throw Exception(_err('${j['err']}'));
    return BoardPics.fromJson(j);
  }

  Future<BoardPics> _get(String path, [Map<String, String>? q]) async {
    await Native.bindWifi();
    final c = _client();
    try {
      final rq = await c.getUrl(_u(path, q));
      final r = await rq.close().timeout(const Duration(seconds: 8));
      return await _read(r);
    } finally {
      c.close();
    }
  }

  Future<BoardPics> list() => _get('/api/pics');
  Future<BoardPics> delete(String n) => _get('/api/pic_del', {'name': n});
  Future<BoardPics> setSlot(int slot, String n) =>
      _get('/api/pic_slot', {'slot': '$slot', 'name': n});
  Future<BoardPics> setRpm(String n, {bool? on, int? rpm}) => _get('/api/pic_rpm', {
        'name': n,
        if (on != null) 'on': on ? '1' : '0',
        if (rpm != null) 'rpm': '$rpm',
      });
  Future<BoardPics> setAuto(bool v) => _get('/api/pic_auto', {'v': v ? '1' : '0'});
  Future<BoardPics> move(String n, int dir) => _get('/api/pic_move', {'name': n, 'dir': '$dir'});

  /// ส่งไฟล์ (multipart) พร้อมแจ้งความคืบหน้า 0..1
  Future<BoardPics> upload(String name, Uint8List data, {void Function(double)? progress}) async {
    await Native.bindWifi();
    final c = _client();
    try {
      final rq = await c.postUrl(_u('/api/pic_up', {'name': name}));
      final b = 'didi${DateTime.now().microsecondsSinceEpoch}';
      final type = name.endsWith('.gif') ? 'image/gif' : 'image/jpeg';
      final head = utf8.encode('--$b\r\nContent-Disposition: form-data; name="f"; filename="$name"\r\n'
          'Content-Type: $type\r\n\r\n');
      final tail = utf8.encode('\r\n--$b--\r\n');
      rq.headers.contentType = ContentType('multipart', 'form-data', parameters: {'boundary': b});
      rq.contentLength = head.length + data.length + tail.length;
      rq.add(head);
      const chunk = 8192;
      for (var o = 0; o < data.length; o += chunk) {
        rq.add(data.sublist(o, math.min(o + chunk, data.length)));
        await rq.flush();
        progress?.call(math.min(1.0, (o + chunk) / data.length));
      }
      rq.add(tail);
      final r = await rq.close().timeout(const Duration(seconds: 30));
      return await _read(r);
    } finally {
      c.close();
    }
  }
}
