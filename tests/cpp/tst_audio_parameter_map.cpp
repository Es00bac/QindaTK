// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QtTest>
#include <cmath>
#include "audio_parameter_map.h"

using namespace QindaTK;

class tst_audio_parameter_map : public QObject {
    Q_OBJECT
private slots:
    void gainSilenceUnityAndLogarithmicRange();
};

void tst_audio_parameter_map::gainSilenceUnityAndLogarithmicRange()
{
    AudioParameterMap map;
    map.setDescriptor({{"taper", "gainDb"}, {"minimum", -90.0},
                       {"maximum", 12.0}, {"unityPosition", 0.75}});
    QVERIFY(map.lastError().isEmpty());
    QCOMPARE(map.positionFor(0), 0.75);
    QCOMPARE(map.positionFor(-90, true), 0.0);
    QCOMPARE(map.positionFor(-90, false), 0.001);
    QCOMPARE(map.valueAt(0).value("silent").toBool(), true);
    QCOMPARE(map.valueAt(0.75).value("value").toDouble(), 0.0);
    QCOMPARE(map.valueAt(1).value("value").toDouble(), 12.0);
    const double mid = map.valueAt(0.5).value("value").toDouble();
    QVERIFY(std::abs(map.positionFor(mid) - 0.5) < 1e-12);

    map.setDescriptor({{"taper", "logarithmic"}, {"minimum", 20.0},
                       {"maximum", 20000.0}});
    QVERIFY(std::abs(map.valueAt(0.5).value("value").toDouble()
                     - std::sqrt(20.0 * 20000.0)) < 1e-8);
    QVERIFY(std::abs(map.positionFor(1000.0) - std::log(50.0) / std::log(1000.0)) < 1e-12);
    map.setDescriptor({{"taper", "logarithmic"}, {"minimum", 0.0},
                       {"maximum", 20000.0}});
    QVERIFY(!map.lastError().isEmpty());
    QCOMPARE(map.descriptor().value("minimum").toDouble(), 20.0);
}

QTEST_MAIN(tst_audio_parameter_map)
#include "tst_audio_parameter_map.moc"
