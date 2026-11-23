import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';

import '../settings.dart';

/// เลข build ของแอปนี้ (ใส่ตอน build บน GitHub: --dart-define=BUILD=<run_number>)
const int kBuild = int.fromEnvironment('BUILD');

/// เช็กเวอร์ชันใหม่จากหน้า Releases ของ GitHub
class Updater extends ChangeNotifier {
  Updater(this.s);
  final AppSettings s;
  static const _api = 'https://api.github.com/repos/kittitouch2404/didissnap/releases/latest';

  int latest = 0;
  String url = '';
  String status = '';
  bool busy = false;
  bool dismissed = false;

  void dismiss() {
    dismissed = true;
    notifyListeners();
  }

  bool get hasNew => kBuild > 0 && latest > kBuild;

  /// auto = เช็กเองตอนเปิดแอป (ไม่เกินทุก 6 ชม.)
  Future<void> check({bool auto = false}) async {
    if (busy) return;
    if (auto) {
      if (!s.updateAuto) return;
      final last = DateTime.fromMillisecondsSinceEpoch(s.updateLast);
      if (DateTime.now().difference(last).inHours < 6 && s.updateSeen > 0) {
        latest = s.updateSeen;
        url = s.updateUrl;
        notifyListeners();
        return;
      }
    }
    busy = true;
    status = 'กำลังเช็ก...';
    notifyListeners();
    final c = HttpClient()..connectionTimeout = const Duration(seconds: 8);
    try {
      final rq = await c.getUrl(Uri.parse(_api));
      rq.headers.set('Accept', 'application/vnd.github+json');
      rq.headers.set('User-Agent', 'didissnap-app');
      final rs = await rq.close().timeout(const Duration(seconds: 10));
      final body = await rs.transform(utf8.decoder).join();
      if (rs.statusCode != 200) throw Exception('HTTP ${rs.statusCode}');
      final j = jsonDecode(body) as Map<String, dynamic>;
      final tag = '${j['tag_name'] ?? ''}';
      latest = int.tryParse(tag.replaceAll(RegExp(r'[^0-9]'), '')) ?? 0;
      final assets = (j['assets'] as List?) ?? [];
      url = assets.isNotEmpty ? '${assets.first['browser_download_url']}' : '${j['html_url'] ?? ''}';
      s.setUpdateSeen(latest, url);
      status = hasNew ? 'มีเวอร์ชันใหม่ build-$latest' : 'ล่าสุดแล้ว (build-${kBuild > 0 ? kBuild : '?'})';
    } catch (e) {
      status = 'เช็กไม่ได้ (ไม่มีเน็ต?)';
    } finally {
      c.close(force: true);
      busy = false;
      notifyListeners();
    }
  }
}
