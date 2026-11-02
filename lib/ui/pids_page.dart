import 'dart:async';
import 'dart:ui' show FontFeature;

import 'package:flutter/material.dart';

import '../data/hub.dart';
import '../data/pids.dart';
import '../settings.dart';
import 'paint_utils.dart';

/// หน้าตาราง: แสดงทุกค่าที่รถรองรับผ่าน OBD2 (Mode 01)
class PidsPage extends StatefulWidget {
  const PidsPage({super.key, required this.hub});
  final DataHub hub;

  @override
  State<PidsPage> createState() => _PidsPageState();
}

class _PidsPageState extends State<PidsPage> {
  Timer? _t;

  @override
  void initState() {
    super.initState();
    _t = Timer.periodic(const Duration(milliseconds: 300), (_) {
      if (mounted) setState(() {});
    });
  }

  @override
  void dispose() {
    _t?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final hub = widget.hub;
    final d = hub.data;
    final rows = <(String, String, String, String)>[];
    final ids = d.supportedPids.where((p) => !isBitmapPid(p)).toList()..sort();
    for (final p in ids) {
      final def = kPidDefs[p];
      final b = d.pidBytes[p];
      final v = b == null
          ? '…'
          : (def != null
              ? def.fmt(b)
              : b.map((e) => e.toRadixString(16).padLeft(2, '0').toUpperCase()).join(' '));
      rows.add((pidHex(p), def?.name ?? 'PID ${pidHex(p)} (ไม่รู้จัก)', v, def?.unit ?? 'hex'));
    }
    String note;
    if (rows.isEmpty) {
      // แหล่งข้อมูลที่ไม่ได้อ่าน PID เอง (ESP32) → แสดงค่าที่มี
      note = hub.settings.source == SourceType.esp32
          ? 'โหมด ESP32 ส่งมาเฉพาะค่าหลัก — ถ้าต้องการทุก PID ให้ต่อ ELM327 ตรงจากมือถือ'
          : (hub.state == LinkState.connected
              ? 'กำลังอ่านรายการ PID ที่รถรองรับ...'
              : 'ยังไม่ได้เชื่อมต่อกับรถ');
      rows.addAll([
        ('0C', 'รอบเครื่อง', fmt(d.rpm), 'rpm'),
        ('0D', 'ความเร็ว', fmt(d.speed), 'km/h'),
        ('05', 'อุณหภูมิน้ำ (Coolant)', fmt(d.ect), '°C'),
        ('0F', 'อุณหภูมิอากาศเข้า (IAT)', fmt(d.iat), '°C'),
        ('RV', 'แบตเตอรี่', fmt(d.volt, 1), 'V'),
        ('04', 'Engine load', fmt(d.load, 1), '%'),
        ('11', 'ลิ้นเร่ง (TPS)', fmt(d.tps, 1), '%'),
        ('0B', 'MAP', fmt(d.map), 'kPa'),
        ('10', 'MAF', fmt(d.maf, 2), 'g/s'),
        ('2F', 'ระดับน้ำมัน', fmt(d.fuel), '%'),
      ]);
    } else {
      note = 'รถรองรับ ${rows.length} ค่า · อ่านวนทีละค่า (ครบรอบประมาณ ${(rows.length * 0.12).toStringAsFixed(0)} วินาที)';
    }
    final half = (rows.length + 1) ~/ 2;
    Widget col(List<(String, String, String, String)> list) => Column(
          children: [for (final r in list) _cell(r)],
        );
    return Container(
      color: Colors.black,
      padding: const EdgeInsets.fromLTRB(12, 36, 12, 12),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Row(children: [
          const Text('ค่า OBD2 ทั้งหมด',
              style: TextStyle(fontSize: 16, fontWeight: FontWeight.w800, color: kAccent)),
          const SizedBox(width: 12),
          Expanded(
              child: Text(note,
                  style: const TextStyle(fontSize: 11.5, color: Colors.white54),
                  overflow: TextOverflow.ellipsis)),
        ]),
        const SizedBox(height: 6),
        Expanded(
          child: SingleChildScrollView(
            child: Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
              Expanded(child: col(rows.sublist(0, half))),
              const SizedBox(width: 12),
              Expanded(child: col(rows.sublist(half))),
            ]),
          ),
        ),
      ]),
    );
  }

  Widget _cell((String, String, String, String) r) {
    return Container(
      padding: const EdgeInsets.symmetric(vertical: 5, horizontal: 6),
      decoration: const BoxDecoration(
          border: Border(bottom: BorderSide(color: Color(0xFF242424)))),
      child: Row(children: [
        SizedBox(
          width: 30,
          child: Text(r.$1,
              style: const TextStyle(fontSize: 11, color: Color(0xFF6A8FB0), fontWeight: FontWeight.w700)),
        ),
        Expanded(
          child: Text(r.$2,
              style: const TextStyle(fontSize: 13, color: Colors.white70),
              overflow: TextOverflow.ellipsis),
        ),
        Text(r.$3,
            style: const TextStyle(
                fontSize: 16,
                fontWeight: FontWeight.w800,
                fontFeatures: [FontFeature.tabularFigures()])),
        SizedBox(
          width: 44,
          child: Text(' ${r.$4}',
              style: const TextStyle(fontSize: 11, color: kGrey),
              overflow: TextOverflow.clip),
        ),
      ]),
    );
  }
}
