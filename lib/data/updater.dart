import 'dart:typed_data';
import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:path_provider/path_provider.dart';

import '../settings.dart';
import 'app_log.dart';
import 'native.dart';

/// เลข build ของแอปนี้ (ใส่ตอน build บน GitHub: --dart-define=BUILD=<run_number>)
const int kBuild = int.fromEnvironment('BUILD');

/// เลข build ตัวสุดท้ายก่อนเริ่มนับเวอร์ชันใหม่ (build 161 = 1.0.1)
const int kVerBase = 160;

/// เลข build (ภายใน ใช้เทียบอัปเดต) -> ชื่อเวอร์ชันที่โชว์ เช่น 161 -> "v1.0.1"
String verName(int b) => b <= 0 ? '?' : (b <= kVerBase ? 'build-$b' : 'v1.0.${b - kVerBase}');

/// เวอร์ชันของแอปนี้
String get kVersion => verName(kBuild);

/// เช็ก/โหลดเวอร์ชันใหม่จาก Releases ของ GitHub
///  repo เป็น private ได้: ใส่ token (อ่านอย่างเดียว) ในแอป -> เก็บไว้ในเครื่องนี้เท่านั้น ไม่ได้อยู่ในไฟล์ APK
///  แอปโหลด APK เองแล้วเปิดหน้าติดตั้ง (ไม่มีลิงก์สาธารณะ)
class Updater extends ChangeNotifier {
  Updater(this.s);
  final AppSettings s;
  static const _api = 'https://api.github.com/repos/kittitouch2404/didissnap/releases/latest';

  int latest = 0;
  String assetApi = ''; // URL ของไฟล์ผ่าน API (ใช้ token โหลด)
  String status = '';
  bool busy = false;
  bool dismissed = false;
  double progress = -1; // 0..1 ระหว่างโหลด

  void dismiss() {
    dismissed = true;
    notifyListeners();
  }

  bool get hasNew => kBuild > 0 && latest > kBuild;

  HttpClient _client() => HttpClient()..connectionTimeout = const Duration(seconds: 10);

  void _auth(HttpClientRequest rq) {
    rq.headers.set('User-Agent', 'didissnap-app');
    final t = s.ghToken.trim();
    if (t.isNotEmpty) rq.headers.set('Authorization', 'Bearer $t');
  }

  /// auto = เช็กเองตอนเปิดแอป (ไม่เกินทุก 6 ชม.)
  Future<void> check({bool auto = false}) async {
    if (busy) return;
    if (auto) {
      if (!s.updateAuto) return;
      final last = DateTime.fromMillisecondsSinceEpoch(s.updateLast);
      if (DateTime.now().difference(last).inHours < 6 && s.updateSeen > 0) {
        latest = s.updateSeen;
        assetApi = s.updateUrl;
        notifyListeners();
        return;
      }
    }
    busy = true;
    status = 'กำลังเช็ก...';
    notifyListeners();
    final c = _client();
    try {
      final rq = await c.getUrl(Uri.parse(_api));
      rq.headers.set('Accept', 'application/vnd.github+json');
      _auth(rq);
      final rs = await rq.close().timeout(const Duration(seconds: 12));
      final body = await rs.transform(utf8.decoder).join();
      if (rs.statusCode == 404 || rs.statusCode == 401) {
        status = s.ghToken.trim().isEmpty ? 'repo เป็นส่วนตัว: ใส่ token ก่อน' : 'token ใช้ไม่ได้ / หมดอายุ';
        return;
      }
      if (rs.statusCode != 200) throw Exception('HTTP ${rs.statusCode}');
      final j = jsonDecode(body) as Map<String, dynamic>;
      latest = int.tryParse('${j['tag_name'] ?? ''}'.replaceAll(RegExp(r'[^0-9]'), '')) ?? 0;
      final assets = (j['assets'] as List?) ?? [];
      assetApi = assets.isNotEmpty ? '${assets.first['url']}' : '';
      s.setUpdateSeen(latest, assetApi);
      status = hasNew ? 'มีเวอร์ชันใหม่ ${verName(latest)}' : 'ล่าสุดแล้ว ($kVersion)';
      AppLog.i('เช็กอัปเดต: $status');
    } catch (e) {
      status = 'เช็กไม่ได้ (ไม่มีเน็ต?)';
      AppLog.e('เช็กอัปเดตไม่ได้', e);
    } finally {
      c.close(force: true);
      busy = false;
      notifyListeners();
    }
  }

  // ---------------- เฟิร์มแวร์ C3 ล่าสุด (release c3-latest ที่ CI สร้าง)
  static const _apiRel = 'https://api.github.com/repos/kittitouch2404/didissnap/releases/tags/';
  /// รุ่นของตัวที่ต่ออยู่: 'c3' = ESP32-C3 · 'sl' = ESP32 รุ่นแรก (Bluetooth ธรรมดา) — ใช้ไฟล์คนละตัว
  String fam = 'c3';
  void setFam(String fw) {
    final f = fw.startsWith('sl ') ? 'sl' : (fw.startsWith('c3 ') ? 'c3' : '');
    if (f.isEmpty || f == fam) return;
    fam = f;
    c3Latest = '';
    c3Asset = '';
    checkC3(force: true);
  }
  String c3Latest = ''; // เช่น "c3 2026.11.26b" ("" = ยังไม่รู้)
  String c3Asset = '';
  DateTime _c3At = DateTime.fromMillisecondsSinceEpoch(0);
  bool _c3Busy = false;

  /// เช็กเวอร์ชันเฟิร์มแวร์ C3 ล่าสุด (ไม่เกินทุก 1 ชม. ยกเว้น force)
  Future<void> checkC3({bool force = false}) async {
    if (_c3Busy || (!force && DateTime.now().difference(_c3At).inMinutes < 60)) return;
    _c3Busy = true;
    final c = _client();
    try {
      final rq = await c.getUrl(Uri.parse('$_apiRel$fam-latest'));
      rq.headers.set('Accept', 'application/vnd.github+json');
      _auth(rq);
      final rs = await rq.close().timeout(const Duration(seconds: 12));
      final body = await rs.transform(utf8.decoder).join();
      if (rs.statusCode != 200) throw Exception('HTTP ${rs.statusCode}');
      final j = jsonDecode(body) as Map<String, dynamic>;
      final m = RegExp('FW:\\s*($fam [0-9.]+[a-z]?)').firstMatch('${j['body'] ?? ''}');
      c3Latest = m?.group(1) ?? '';
      c3Asset = '';
      for (final a in (j['assets'] as List? ?? [])) {
        if ('${a['name']}' == (fam == 'sl' ? 'didi_sl_esp32.bin' : 'didi_c3_gateway.bin')) c3Asset = '${a['url']}';
      }
      _c3At = DateTime.now();
      AppLog.i('C3 firmware latest: ${c3Latest.isEmpty ? '?' : c3Latest}');
      notifyListeners();
    } catch (e) {
      AppLog.e('เช็กเฟิร์มแวร์ C3 ไม่ได้', e);
    } finally {
      c.close(force: true);
      _c3Busy = false;
    }
  }

  /// โหลดไฟล์เฟิร์มแวร์ C3 ล่าสุด (สำหรับอัปเดตผ่าน Bluetooth)
  Future<Uint8List?> downloadC3() async {
    if (c3Asset.isEmpty) await checkC3(force: true);
    if (c3Asset.isEmpty) return null;
    final c = _client()..autoUncompress = false;
    try {
      final rq = await c.getUrl(Uri.parse(c3Asset));
      rq.followRedirects = false;
      rq.headers.set('Accept', 'application/octet-stream');
      _auth(rq);
      var rs = await rq.close();
      if (rs.isRedirect || rs.statusCode == 302 || rs.statusCode == 301) {
        final loc = rs.headers.value(HttpHeaders.locationHeader);
        await rs.drain<void>();
        if (loc == null) return null;
        final rq2 = await c.getUrl(Uri.parse(loc));
        rq2.headers.set('User-Agent', 'didissnap-app');
        rs = await rq2.close();
      }
      if (rs.statusCode != 200) return null;
      final b = <int>[];
      await for (final ch in rs) {
        b.addAll(ch);
      }
      return Uint8List.fromList(b);
    } catch (e) {
      AppLog.e('โหลดเฟิร์มแวร์ C3 ไม่ได้', e);
      return null;
    } finally {
      c.close(force: true);
    }
  }

  /// โหลด APK มาไว้ในแอป แล้วเปิดหน้าติดตั้ง
  Future<void> downloadAndInstall() async {
    if (busy || assetApi.isEmpty) return;
    busy = true;
    progress = 0;
    status = 'กำลังโหลด...';
    notifyListeners();
    final c = _client()..autoUncompress = false;
    try {
      // 1) API ของไฟล์ -> ได้ลิงก์ชั่วคราว (redirect)  2) โหลดจากลิงก์นั้นโดยไม่ส่ง token ต่อ
      final rq = await c.getUrl(Uri.parse(assetApi));
      rq.followRedirects = false;
      rq.headers.set('Accept', 'application/octet-stream');
      _auth(rq);
      var rs = await rq.close();
      if (rs.isRedirect || rs.statusCode == 302 || rs.statusCode == 301) {
        final loc = rs.headers.value(HttpHeaders.locationHeader);
        await rs.drain<void>();
        if (loc == null) throw Exception('no redirect');
        final rq2 = await c.getUrl(Uri.parse(loc));
        rq2.headers.set('User-Agent', 'didissnap-app');
        rs = await rq2.close();
      }
      if (rs.statusCode != 200) throw Exception('HTTP ${rs.statusCode}');
      final total = rs.contentLength;
      final d = await getTemporaryDirectory();
      final dir = Directory('${d.path}/updates');
      await dir.create(recursive: true);
      final f = File('${dir.path}/Didissnap.apk');
      final sink = f.openWrite();
      var got = 0;
      await for (final ch in rs) {
        sink.add(ch);
        got += ch.length;
        if (total > 0) {
          progress = got / total;
          notifyListeners();
        }
      }
      await sink.close();
      status = 'โหลดเสร็จ — กด "ติดตั้ง" ในหน้าต่างที่ขึ้นมา';
      progress = -1;
      notifyListeners();
      final ok = await Native.installApk(f.path);
      if (!ok) status = 'เปิดหน้าติดตั้งไม่ได้ — อนุญาต "ติดตั้งแอปที่ไม่รู้จัก" ให้แอปนี้ แล้วกดอีกครั้ง';
    } catch (e) {
      status = 'โหลดไม่สำเร็จ ($e)';
      AppLog.e('โหลดอัปเดตไม่สำเร็จ', e);
    } finally {
      c.close(force: true);
      busy = false;
      progress = -1;
      notifyListeners();
    }
  }
}
