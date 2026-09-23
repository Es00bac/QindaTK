// SPDX-License-Identifier: LGPL-3.0-or-later
#include "time_map.h"

#include <QtTest/QtTest>
#include <limits>

using namespace QindaTK;

class TimeMapTest : public QObject {
    Q_OBJECT
private slots:
    void frameRateAndRounding();
    void tempoSegmentsAndNegativeTime();
    void timecodeBoundary();
    void precisionSafeFacade();
    void validationAndOverflow();
};

void TimeMapTest::frameRateAndRounding() {
    TimeMap map;
    QCOMPARE(map.frameToSample(QStringLiteral("30000")).value("value").toString(), QStringLiteral("48048000"));
    QCOMPARE(map.sampleToFrame(QStringLiteral("48048000")).value("value").toString(), QStringLiteral("30000"));
    QCOMPARE(map.frameToSample(QStringLiteral("1"), TimeMap::TowardZero).value("value").toString(), QStringLiteral("1601"));
    QCOMPARE(map.frameToSample(QStringLiteral("1"), TimeMap::Ceil).value("value").toString(), QStringLiteral("1602"));
    QCOMPARE(map.frameToSample(QStringLiteral("-1"), TimeMap::Floor).value("value").toString(), QStringLiteral("-1602"));
    QCOMPARE(map.frameToSample(QStringLiteral("-1"), TimeMap::NearestAway).value("value").toString(), QStringLiteral("-1602"));
    QVERIFY(map.setFrameRate(24, 1));
    QCOMPARE(map.frameToSample(QStringLiteral("100")).value("value").toString(), QStringLiteral("200000"));
    QVERIFY(map.setFrameRate(60000, 2002));
    QCOMPARE(map.frameRateNumerator(), 30000);
    QCOMPARE(map.frameRateDenominator(), 1001);
}

void TimeMapTest::tempoSegmentsAndNegativeTime() {
    TimeMap map;
    QCOMPARE(map.tickToSample(QStringLiteral("960")).value("value").toString(), QStringLiteral("24000"));
    map.setTempoSegments({QVariantMap{{"tick", "0"}, {"microsecondsPerQuarter", "500000"},
                                      {"numerator", 4}, {"denominator", 4}},
                          QVariantMap{{"tick", "1920"}, {"microsecondsPerQuarter", "1000000"},
                                      {"numerator", 3}, {"denominator", 4}}});
    QCOMPARE(map.tempoSegments().size(), 2);
    QCOMPARE(map.tickToSample(QStringLiteral("1920")).value("value").toString(), QStringLiteral("48000"));
    QCOMPARE(map.tickToSample(QStringLiteral("2880")).value("value").toString(), QStringLiteral("96000"));
    QCOMPARE(map.sampleToTick(QStringLiteral("120000")).value("value").toString(), QStringLiteral("3360"));
    QCOMPARE(map.tickToSample(QStringLiteral("-960")).value("value").toString(), QStringLiteral("-24000"));
    QCOMPARE(map.sampleToTick(QStringLiteral("-24000")).value("value").toString(), QStringLiteral("-960"));
    QCOMPARE(map.sampleToTick(QStringLiteral("48000")).value("value").toString(), QStringLiteral("1920"));
    QCOMPARE(map.sampleToTick(QStringLiteral("47999"), TimeMap::Floor).value("value").toString(), QStringLiteral("1919"));
    map.setSampleRate(44100);
    QCOMPARE(map.tickToSample(QStringLiteral("960")).value("value").toString(), QStringLiteral("22050"));
}

void TimeMapTest::timecodeBoundary() {
    TimeMap map;
    QCOMPARE(map.formatTimecode(QStringLiteral("0"), true).value("value").toString(), QStringLiteral("00:00:00;00"));
    QCOMPARE(map.formatTimecode(QStringLiteral("1799"), true).value("value").toString(), QStringLiteral("00:00:59;29"));
    QCOMPARE(map.formatTimecode(QStringLiteral("1800"), true).value("value").toString(), QStringLiteral("00:01:00;02"));
    QCOMPARE(map.formatTimecode(QStringLiteral("17982"), true).value("value").toString(), QStringLiteral("00:10:00;00"));
    QCOMPARE(map.formatTimecode(QStringLiteral("107892"), true).value("value").toString(), QStringLiteral("01:00:00;00"));
    QCOMPARE(map.formatTimecode(QStringLiteral("-1800"), true).value("value").toString(), QStringLiteral("-00:01:00;02"));
    QCOMPARE(map.parseTimecode(QStringLiteral("00:01:00;02"), true).value("value").toString(), QStringLiteral("1800"));
    QCOMPARE(map.parseTimecode(QStringLiteral("-00:01:00;02"), true).value("value").toString(), QStringLiteral("-1800"));
    QVERIFY(!map.parseTimecode(QStringLiteral("00:01:00;00"), true).value("ok").toBool());
    QCOMPARE(map.parseTimecode(QStringLiteral("00:10:00;00"), true).value("value").toString(), QStringLiteral("17982"));
    QCOMPARE(map.formatTimecode(QStringLiteral("1800")).value("value").toString(), QStringLiteral("00:01:00:00"));
    for (qint64 index : {0LL, 1LL, 1798LL, 1799LL, 1800LL, 17981LL, 17982LL,
                          107891LL, 107892LL, 1078920LL, -1799LL, -1800LL}) {
        const QString frame = QString::number(index);
        const QString label = map.formatTimecode(frame, true).value("value").toString();
        QCOMPARE(map.parseTimecode(label, true).value("value").toString(), frame);
    }
    QVERIFY(map.setFrameRate(60000, 1001));
    QCOMPARE(map.formatTimecode(QStringLiteral("3600"), true).value("value").toString(), QStringLiteral("00:01:00;04"));
    QCOMPARE(map.formatTimecode(QStringLiteral("35964"), true).value("value").toString(), QStringLiteral("00:10:00;00"));
    QVERIFY(!map.parseTimecode(QStringLiteral("00:01:00;03"), true).value("ok").toBool());
    QVERIFY(map.setFrameRate(24, 1));
    QVERIFY(!map.formatTimecode(QStringLiteral("12"), true).value("ok").toBool());
    QCOMPARE(map.formatTimecode(QStringLiteral("24")).value("value").toString(), QStringLiteral("00:00:01:00"));
}

void TimeMapTest::precisionSafeFacade() {
    TimeMap map;
    const QString hugeFrame = QStringLiteral("5000000000000000");
    const QVariantMap sample = map.frameToSample(hugeFrame);
    QVERIFY(sample.value("ok").toBool());
    const __int128 n = static_cast<__int128>(5000000000000000LL) * 48000 * 1001;
    QCOMPARE(sample.value("value").toString(), QString::number(static_cast<qint64>((n + 15000) / 30000)));
    QCOMPARE(map.sampleToFrame(sample.value("value").toString()).value("value").toString(), hugeFrame);
    QCOMPARE(map.compareExact(QStringLiteral("9007199254740993"), QStringLiteral("9007199254740992"))
                 .value("value").toString(), QStringLiteral("1"));
    QCOMPARE(map.sampleToPixel(QStringLiteral("9007199254741017"), QStringLiteral("9007199254740997"), 2.0)
                 .value("value").toDouble(), 40.0);
    QCOMPARE(map.pixelToSample(40.0, QStringLiteral("9007199254740997"), 2.0)
                 .value("value").toString(), QStringLiteral("9007199254741017"));
    map.setOriginSample(QStringLiteral("9007199254740997"));
    QCOMPARE(map.frameToSample(QStringLiteral("0")).value("value").toString(), QStringLiteral("9007199254740997"));
    map.setOriginSample(QString::number(std::numeric_limits<qint64>::min()));
    map.setSampleRate(2);
    QVERIFY(map.setFrameRate(1, 1));
    QCOMPARE(map.frameToSample(QString::number(std::numeric_limits<qint64>::max()))
                 .value("value").toString(), QStringLiteral("9223372036854775806"));
    map.setSampleRate(768000);
    map.setTicksPerQuarter(1);
    map.setTempoSegments({QVariantMap{{"tick", "0"}, {"microsecondsPerQuarter", "60000000"},
                                      {"numerator", 4}, {"denominator", 4}}});
    QVERIFY(map.sampleToTick(QString::number(std::numeric_limits<qint64>::max())).value("ok").toBool());
}

void TimeMapTest::validationAndOverflow() {
    TimeMap map;
    QVERIFY(!map.frameToSample(QStringLiteral("1e9")).value("ok").toBool());
    QVERIFY(!map.frameToSample(QStringLiteral(" 1")).value("ok").toBool());
    QVERIFY(!map.frameToSample(QStringLiteral("9223372036854775808")).value("ok").toBool());
    QVERIFY(!map.frameToSample(QString::number(std::numeric_limits<qint64>::max())).value("ok").toBool());
    QVERIFY(!map.setFrameRate(0, 1));
    QCOMPARE(map.frameRateNumerator(), 30000);
    map.setSampleRate(0);
    QCOMPARE(map.sampleRate(), 48000);
    const auto old = map.tempoSegments();
    map.setTempoSegments({QVariantMap{{"tick", "1"}, {"microsecondsPerQuarter", "500000"},
                                      {"numerator", 4}, {"denominator", 4}}});
    QCOMPARE(map.tempoSegments(), old);
    QVERIFY(!map.pixelToSample(1e19, QStringLiteral("0"), 1.0).value("ok").toBool());
}

QTEST_MAIN(TimeMapTest)
#include "tst_time_map.moc"
