// SPDX-License-Identifier: LGPL-3.0-or-later
#include "test_transport_clock.h"
#include "time_map.h"
#include "transport_model.h"

#include <QSignalSpy>
#include <QtTest>
#include <atomic>
#include <memory>
#include <thread>

using namespace QindaTK;

class TstTransportModel final : public QObject {
    Q_OBJECT
private slots:
    void acknowledgmentsOutOfOrderRemainAuthoritative() {
        TestTransportClock clock;
        TransportModel model;
        model.setAdapter(&clock);
        QVERIFY(model.canPlay());
        const QString play = model.requestPlay();
        QVERIFY(!play.isEmpty());
        QVERIFY(!model.playing()); // No optimistic state before engine reply.
        const QString seek = model.requestSeek(QStringLiteral("9007199254740993"));
        QVERIFY(!seek.isEmpty());
        QVERIFY(clock.process(play));
        QVERIFY(clock.process(seek));
        QVERIFY(clock.deliver(seek));
        QVERIFY(model.playing());
        QCOMPARE(model.positionSample(), QStringLiteral("9007199254740993"));
        QCOMPARE(model.engineRevision(), QStringLiteral("3"));
        QVERIFY(clock.deliver(play)); // Older revision cannot rewind the GUI.
        QCOMPARE(model.positionSample(), QStringLiteral("9007199254740993"));
        QCOMPARE(model.pendingCount(), 0);
        const QString relative = model.requestSeekRelative(QStringLiteral("1"));
        QVERIFY(clock.acknowledge(relative));
        QCOMPARE(model.positionSample(), QStringLiteral("9007199254740994"));
        QVERIFY(model.requestSeekRelative(QStringLiteral("not-an-integer")).isEmpty());
    }
    void deniedRecordDeviceErrorAndEngineOriginatedStop() {
        TestTransportClock clock;
        TransportModel model;
        model.setAdapter(&clock);
        clock.setDenyRecording(true);
        const QString record = model.requestRecord(true);
        QVERIFY(!record.isEmpty());
        QVERIFY(clock.acknowledge(record));
        QVERIFY(!model.recording());
        QCOMPARE(model.lastError(), QStringLiteral("Recording denied"));
        clock.externalState(24000, true, false);
        QTRY_VERIFY(model.playing());
        QTRY_COMPARE(model.positionSample(), QStringLiteral("24000"));
        clock.setDeviceError(QStringLiteral("Output device lost"));
        QTRY_VERIFY(!model.playing());
        QCOMPARE(model.deviceError(), QStringLiteral("Output device lost"));
        const QString play = model.requestPlay();
        QVERIFY(play.isEmpty());
        QVERIFY(!model.canPlay());
        QVERIFY(!model.playing());
        QCOMPARE(model.lastError(), QStringLiteral("Output device lost"));
    }
    void boundedSnapshotCadenceAndExactTimeMapReadout() {
        TestTransportClock clock;
        TimeMap timeMap;
        timeMap.setSampleRate(48000);
        QVERIFY(timeMap.setFrameRate(24, 1));
        TransportModel model;
        model.setTimeMap(&timeMap);
        model.setAdapter(&clock);
        QSignalSpy changes(&model, &TransportModel::changed);
        clock.externalState(0, true, false);
        for (int i = 0; i < 1000; ++i) QVERIFY(clock.advance(1));
        QTRY_COMPARE(model.positionSample(), QStringLiteral("1000"));
        QVERIFY(changes.size() <= 2); // 1000 engine frames coalesce into at most two GUI snapshots.
        QVERIFY(clock.setPlaybackRate(1.5));
        QTRY_COMPARE(model.playbackRate(), 1.5);
        clock.externalState(48000LL * 3600, false, false);
        QTRY_COMPARE(model.positionSample(), QStringLiteral("172800000"));
        QCOMPARE(model.formatPosition(QStringLiteral("timecode")), QStringLiteral("01:00:00:00"));
        QCOMPARE(model.formatPosition(QStringLiteral("frames")), QStringLiteral("86400"));
        QCOMPARE(model.formatPosition(QStringLiteral("samples")), QStringLiteral("172800000"));
        QVERIFY(!model.formatPosition(QStringLiteral("ticks")).isEmpty());
    }
    void capabilityChangesAreNotCoalesced() {
        TestTransportClock clock;
        TransportModel model;
        model.setAdapter(&clock);
        clock.externalState(100, false, false); // opens a coalescing interval
        QTRY_COMPARE(model.positionSample(), QStringLiteral("100"));
        clock.setCanSeek(false, QStringLiteral("No program"));
        QVERIFY(!model.canSeek()); // at once, not one interval later
        clock.setCanSeek(true, QString());
        QVERIFY(model.canSeek());
        QVERIFY(!model.requestSeek(QStringLiteral("200")).isEmpty()); // same turn as the change
        clock.externalState(300, false, false); // a position change still waits its turn
        clock.externalState(400, false, false);
        QTRY_COMPARE(model.positionSample(), QStringLiteral("400"));
    }
    void rangesCapabilitiesAndAdapterReplacement() {
        TestTransportClock clock;
        TransportModel model;
        model.setAdapter(&clock);
        QVERIFY(model.requestLoop(true, QStringLiteral("10"), QStringLiteral("10")).isEmpty());
        const QString loop = model.requestLoop(true, QStringLiteral("10"), QStringLiteral("20"));
        QVERIFY(clock.acknowledge(loop));
        QVERIFY(model.loopEnabled());
        QCOMPARE(model.loopStartSample(), QStringLiteral("10"));
        QCOMPARE(model.loopEndSample(), QStringLiteral("20"));
        const QString punch = model.requestPunch(true, QStringLiteral("11"), QStringLiteral("19"));
        QVERIFY(clock.acknowledge(punch));
        QVERIFY(model.punchEnabled());
        clock.setCanSeek(false, QStringLiteral("Locked to external clock"));
        QTRY_VERIFY(!model.canSeek());
        QCOMPARE(model.seekUnavailableReason(), QStringLiteral("Locked to external clock"));
        QVERIFY(model.requestSeek(QStringLiteral("15")).isEmpty());
        const QString pending = model.requestPlay();
        QSignalSpy finished(&model, &TransportModel::commandFinished);
        TestTransportClock replacement;
        model.setAdapter(&replacement);
        QCOMPARE(model.pendingCount(), 0);
        QVERIFY(!clock.acknowledge(pending) || model.engineRevision() == QStringLiteral("1"));
        QVERIFY(!finished.isEmpty());
        QCOMPARE(model.positionSample(), QStringLiteral("0"));
    }
    void delayedCallbacksAfterModelDestructionAreHarmless() {
        TestTransportClock clock;
        auto model = std::make_unique<TransportModel>();
        model->setAdapter(&clock);
        const QString pending = model->requestPlay();
        const auto delayedSnapshot = clock.capturedSnapshotSinkForTest();
        QVERIFY(delayedSnapshot);
        model.reset();
        std::atomic<bool> acknowledged{false};
        std::thread delayed([&] {
            acknowledged = clock.acknowledge(pending);
            TransportSnapshot stale = clock.snapshot();
            ++stale.revision;
            delayedSnapshot(stale);
        });
        delayed.join();
        QVERIFY(acknowledged);
        QCoreApplication::processEvents();
    }
};
QTEST_MAIN(TstTransportModel)
#include "tst_transport_model.moc"
