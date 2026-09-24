// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QtTest>
#include "audio_meter_model.h"

using namespace QindaTK;

class tst_audio_meter_model : public QObject {
    Q_OBJECT
private slots:
    void calibratedBlocksHoldAndClip();
};

void tst_audio_meter_model::calibratedBlocksHoldAndClip()
{
    const float source[] = {0.5f, 0.0f, -0.5f, 1.0f};
    const auto block = measureInterleaved(source, 2);
    QCOMPARE(block.peaks.size(), 2);
    QCOMPARE(block.peaks.at(0), 0.5);
    QCOMPARE(block.rms.at(0), 0.5);
    QVERIFY(qAbs(block.rms.at(1) - std::sqrt(0.5)) < 1e-6);

    AudioMeterModel meter;
    QVERIFY(meter.submitInterleaved({0.5, 0.0, -0.5, 1.0}, 2, 20));
    QCOMPARE(meter.channelCount(), 2);
    const auto first = meter.levels().at(0).toMap();
    const auto second = meter.levels().at(1).toMap();
    QVERIFY(qAbs(first.value("peakDbfs").toDouble() + 6.0205999) < 1e-5);
    QVERIFY(qAbs(second.value("rmsDbfs").toDouble() + 3.01029996) < 1e-5);
    QVERIFY(second.value("clipped").toBool());

    QVERIFY(meter.submitLevels({0.1, 0.0}, {0.1, 0.0}, 500));
    QCOMPARE(meter.levels().at(0).toMap().value("heldPeakDbfs").toDouble(),
             first.value("heldPeakDbfs").toDouble());
    QVERIFY(meter.submitLevels({0.1, 0.0}, {0.1, 0.0}, 2000));
    QVERIFY(meter.levels().at(0).toMap().value("heldPeakDbfs").toDouble()
            < first.value("heldPeakDbfs").toDouble());
    meter.resetClip();
    QVERIFY(!meter.levels().at(1).toMap().value("clipped").toBool());

    QVERIFY(meter.submitInterleaved({QVariant(qQNaN()), 0.0}, 2, 20));
    QCOMPARE(meter.levels().at(0).toMap().value("invalidSamples").toLongLong(), 1);
    QVERIFY(meter.levels().at(0).toMap().value("silent").toBool());
    QVERIFY(!meter.submitLevels({0.2}, {0.3}, 20));
}

QTEST_MAIN(tst_audio_meter_model)
#include "tst_audio_meter_model.moc"
