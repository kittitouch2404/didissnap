import 'package:flutter/material.dart' hide Text;
import '../i18n.dart';

/// คู่มือสั้น ๆ: ของที่ซ่อนอยู่ / กดตรงไหนทำอะไร
class HelpPage extends StatelessWidget {
  const HelpPage({super.key});

  static const List<(IconData, String, List<String>)> _sec = [
    (Icons.touch_app, 'หน้าปัด (ทุกหน้า)', [
      'ปัดซ้าย/ขวา = เปลี่ยนหน้าปัด · จุดด้านล่างบอกว่าอยู่หน้าไหน',
      'มุมซ้ายบน (จุดสี + BT/WiFi) แตะ = ดูสถานะการเชื่อมต่อ',
      'ปุ่ม MENU มุมขวาบน = เมนูตั้งค่า',
      'เปิด/ปิด/เพิ่มหน้า: MENU → หน้าปัด → หน้าที่แสดง',
    ]),
    (Icons.gif_box_outlined, 'หน้า GIF', [
      'กดค้างที่ตัวเลข (ครึ่งล่างจอ) = ตั้งค่าหน้านี้: เลือก GIF / แบบ A-B / 3 หรือ 6 ค่า / การเล่นภาพ / สี 2 โทน / ซูม-เลื่อนภาพ',
      'แตะช่องค่าด้านขวา = เปลี่ยนเป็นค่าถัดไป (โหมด 6 ค่า: แตะคอลัมน์ซ้าย/ขวาแยกกัน)',
      'การเล่นภาพ: ค้างตามรอบ / วนเร็วตามความเร็วรถ / วนเร็วตามรอบ / ความเร็วปกติ',
      'สุ่มสี: เปิด "สุ่มสี 2 โทนทุกครั้งที่เปิดแอป" หรือกด "สุ่มเลย"',
      'เพิ่มหน้า GIF ได้ถึง 20 หน้า: MENU → หน้าปัด → เพิ่มหน้า GIF',
    ]),
    (Icons.show_chart, 'หน้ากราฟ', [
      'แตะชื่อค่า = เปลี่ยนค่า · + / − = จำนวนเส้น (1-6) · "30s" = ช่วงเวลา',
      '❚❚ หยุด = ค้างภาพไว้ดู',
      'เทียบไฟล์เก่า = เปิด CSV ที่บันทึกไว้ เป็นเส้นประจาง ๆ · แถบเลื่อนเลือกช่วง · "เลื่อนตาม" ให้เล่นไปพร้อมเวลาจริง',
    ]),
    (Icons.warning_amber_rounded, 'เตือน', [
      'ตั้งที่ MENU → ทริป/เตือน: น้ำร้อน / แบตต่ำ / รอบเปลี่ยนเกียร์ / เร็วเกิน / เปลี่ยนน้ำมันเครื่อง',
      'เลือกเสียงแยกแต่ละเรื่อง + ปุ่ม ▶ ลองฟัง',
      'หน้าเตือนเต็มจอแดงกะพริบ: แตะจอ = ปิด และเงียบเรื่องนั้น 2 นาที',
    ]),
    (Icons.route, 'ทริป / น้ำมัน / ความเร็ว', [
      'ทริป A = ค่าที่โชว์ในหน้าปัด · ทริป B = นับระยะยาว · รีเซ็ตแยกกันได้',
      'km/L คำนวณจาก MAF + ค่า stoich (AFR SCALE) เป็นค่าประมาณ',
      'ความเร็วให้ตรง: ตั้งเอง หรือ "อัตโนมัติด้วย GPS" (ขับนิ่ง ๆ 40+ km/h สักพัก)',
      'เตือนเปลี่ยนน้ำมันเครื่อง: กด "เพิ่งเปลี่ยน" ทุกครั้งที่เปลี่ยน',
    ]),
    (Icons.table_chart_outlined, 'บันทึก CSV', [
      'แตะ LOG ที่แถบบนของ MENU = เปิด/ปิดบันทึกเร็ว ๆ · บนหน้าปัดจะมี ● REC',
      'MENU → บันทึก CSV → ปุ่มดาวน์โหลด = เอาไฟล์ไปไว้ Download/Didissnap (เปิดด้วย Excel / Google Sheets)',
      'ลบไฟล์เก่า: กดเองเมื่ออยากล้าง (เลือกเก็บกี่ไฟล์)',
    ]),
    (Icons.tune, 'Custom PID / CVT', [
      'MENU → การเชื่อมต่อ → Custom PID: ใส่ header / คำสั่ง / สูตร แบบ Torque แล้วกด "บันทึก + ทดสอบ" ดูคำตอบดิบ',
      'สูตร: A B C D = ไบต์ข้อมูล · ใช้ + - * / ( ) ได้ · {A:7} = บิต',
      'โชว์ในช่องขวาหน้า GIF ได้ (CUSTOM 1-4) และส่งให้จอ CYD ด้วย',
    ]),
    (Icons.directions_car, 'รถหลายคัน / สำรอง', [
      'MENU → รถ/สำรอง: แต่ละคันจำค่าตั้งของตัวเอง · แตะ CAR ที่แถบบน = สลับคัน',
      'สำรองทั้งหมด = ไฟล์ .zip (ค่าตั้ง + รูป GIF + โลโก้ + เสียง) ใน Download/Didissnap',
      'ลงแอปใหม่ -> คืนค่าจากไฟล์สำรอง แล้วตั้งสิทธิ์ (แสดงทับแอปอื่น / แบต / GPS) ใหม่',
    ]),
    (Icons.power_settings_new, 'เปิดเอง / โลโก้ / เสียง', [
      'MENU → เปิดอัตโนมัติ: เปิด "จอรถตื่น / ติดเครื่อง" + อนุญาต "แสดงทับแอปอื่น" + ปิดประหยัดแบต',
      'ถ้าไม่เด้ง: ใช้เมนูของจอรถเอง "Auto start app / แอปเริ่มต้นเมื่อ ACC ON"',
      'โลโก้สุ่ม OYT + โลโก้ของฉัน + เสียงบิดกุญแจ: MENU → หน้าปัด → ตอนเปิดแอป',
    ]),
    (Icons.system_update, 'อัปเดต / ปัญหา', [
      'MENU → อัปเดตแอป: เช็ก / โหลด + ติดตั้งในแอป (ติดตั้งทับได้ ค่าตั้งไม่หาย)',
      'repo ส่วนตัว: ใส่ GitHub token (อ่านอย่างเดียว) ในหน้าอัปเดต',
      'เจอปัญหา: MENU → ส่ง log ปัญหา → บันทึกลง Download แล้วส่งไฟล์ให้ช่วยดู',
      'สี MENU: แตะ "สี" มุมขวาบนของ MENU',
    ]),
  ];

  static const List<(IconData, String, List<String>)> _secEn = [
    (Icons.touch_app, 'Dashboard (all pages)', [
      'Swipe left/right = change dashboard · dots at the bottom show the page',
      'Top-left (colored dot + BT/WiFi): tap = connection status',
      'MENU button top-right = settings',
      'Show/hide/add pages: MENU → Dashboard → Pages shown',
    ]),
    (Icons.gif_box_outlined, 'GIF pages', [
      'Long-press the numbers (lower half) = page settings: GIF / style A-B / 3 or 6 values / playback / two-tone colors / zoom & move',
      'Tap a right-side value slot = next value (6-value mode: left/right columns separately)',
      'Playback: hold by RPM / loop with car speed / loop with RPM / normal speed',
      'Random colors: turn on "random two-tone colors every launch" or press "Randomize"',
      'Up to 20 GIF pages: MENU → Dashboard → Add GIF page',
    ]),
    (Icons.show_chart, 'Graph page', [
      'Tap a value name = change it · + / − = number of lines (1-6) · "30s" = time span',
      '❚❚ Pause = freeze the graph',
      'Compare old file = open a saved CSV as faint dashed lines · slider picks the range · "Follow" plays it along in real time',
    ]),
    (Icons.warning_amber_rounded, 'Alerts', [
      'Set in MENU → Trip/Alerts: coolant hot / low battery / shift RPM / overspeed / oil change',
      'Choose a sound for each alert + ▶ to preview',
      'Full-screen flashing red alert: tap = close and silence that alert for 2 minutes',
    ]),
    (Icons.route, 'Trip / fuel / speed', [
      'Trip A = shown on the dashboard · Trip B = long-term · reset separately',
      'km/L is estimated from MAF + stoich (AFR SCALE)',
      'Speed correction: manual, or "Auto with GPS" (drive steadily 40+ km/h for a while)',
      'Oil change reminder: press "Just changed" every oil change',
    ]),
    (Icons.table_chart_outlined, 'CSV logging', [
      'Tap LOG on the MENU top bar = quick logging on/off · the dashboard shows ● REC',
      'MENU → CSV log → download button = copy to Download/Didissnap (open with Excel / Google Sheets)',
      'Delete old files: press it when you want (choose how many to keep)',
    ]),
    (Icons.tune, 'Custom PID / CVT', [
      'MENU → Connection → Custom PID: header / command / formula like Torque, then "Save + test" to see the raw reply',
      'Formula: A B C D = data bytes · + - * / ( ) allowed · {A:7} = bit',
      'Show in GIF right slots (CUSTOM 1-4), also sent to the CYD screen',
    ]),
    (Icons.directions_car, 'Multiple cars / backup', [
      'MENU → Car/Backup: each car keeps its own settings · tap CAR on the top bar = switch car',
      'Back up everything = .zip (settings + GIFs + logo + sounds) in Download/Didissnap',
      'After reinstalling: restore from the backup, then set permissions again (overlay / battery / GPS)',
    ]),
    (Icons.power_settings_new, 'Auto start / logo / sound', [
      'MENU → Auto start: enable "Car screen wakes" + allow "Display over other apps" + disable battery saver',
      'If it does not pop up: use the head unit\'s own "Auto start app / start app on ACC ON" menu',
      'Random OYT logo + my logo + key-on sound: MENU → Dashboard → On app start',
    ]),
    (Icons.system_update, 'Update / problems', [
      'MENU → Update app: check / download + install in the app (installs over, settings kept)',
      'Private repo: enter a read-only GitHub token on the update page',
      'Problem: MENU → Send problem log → Save to Download, then send the file',
      'Menu color: tap "Color" at the top-right of MENU · Language: tap LANG',
    ]),
  ];

  @override
  Widget build(BuildContext context) {
    LangScope.watch(context);
    final a = Theme.of(context).colorScheme.primary;
    final secs = langEn.value ? _secEn : _sec;
    return Scaffold(
      appBar: AppBar(
        title: const Text('คู่มือ', style: TextStyle(fontSize: 22, fontWeight: FontWeight.w900, fontStyle: FontStyle.italic)),
      ),
      body: ListView(
        padding: const EdgeInsets.fromLTRB(16, 8, 16, 24),
        children: [
          for (final (ic, t, lines) in secs)
            Card(
              color: const Color(0xFF121212),
              child: ExpansionTile(
                leading: Icon(ic, color: a, size: 30),
                title: Text(t, style: const TextStyle(fontSize: 19, fontWeight: FontWeight.w800)),
                childrenPadding: const EdgeInsets.fromLTRB(20, 0, 20, 14),
                expandedCrossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  for (final l in lines)
                    Padding(
                      padding: const EdgeInsets.only(bottom: 6),
                      child: Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
                        Padding(
                          padding: const EdgeInsets.only(top: 8, right: 10),
                          child: Container(width: 6, height: 6, decoration: BoxDecoration(color: a, shape: BoxShape.circle)),
                        ),
                        Expanded(child: Text(l, style: const TextStyle(fontSize: 16, height: 1.4))),
                      ]),
                    ),
                ],
              ),
            ),
        ],
      ),
    );
  }
}
