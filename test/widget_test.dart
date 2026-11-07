import 'package:flutter_test/flutter_test.dart';
import 'package:didissnap/data/car_data.dart';
import 'package:didissnap/data/elm_source.dart';

void main() {
  test('gear estimate', () {
    expect(estimateGear(3000, 40.2, [7.6, 13.4, 19.6, 25.9, 32.4]), '2');
    expect(estimateGear(900, 0, [7.6, 13.4]), 'N');
  });

  test('parse PID', () {
    expect(ElmSource.parsePid('410C1AF8', '0C'), [0x1A, 0xF8]);
    expect(ElmSource.parsePid('NO DATA', '0C'), null);
  });

  test('parse DTC', () {
    expect(ElmSource.parseDtc('43020133D016', true), ['P0133', 'U1016']);
    expect(ElmSource.parseDtc('43013304200000', false), ['P0133', 'P0420']);
  });
}
