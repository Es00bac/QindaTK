// SPDX-License-Identifier: LGPL-3.0-or-later
#include "test_transport_clock.h"
#include "timeline_fixture.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QtTest>
#include <memory>

using namespace QindaTK;
using namespace TimelineFixture;

// EditTimeline against the WP-047 transport contract, driven by the labeled
// deterministic TestTransportClock (no audio device or engine is involved).
class TimelineTransportTest : public QObject {
    Q_OBJECT
private slots:
    void playheadSeekAndLoopFollowAcknowledgedState();
};

void TimelineTransportTest::playheadSeekAndLoopFollowAcknowledgedState()
{
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(QINDATK_BUILD_QML_DIR));
    TimeMap map; // one exact time map shared by the transport and the timeline
    TestTransportClock clock;
    TransportModel transport;
    transport.setTimeMap(&map);
    transport.setAdapter(&clock);
    TimelineModel model;
    model.setTimeMap(&map);
    QVERIFY(model.setContent({S, 1}, {track(1)}, {clip(11, 1, S, S)}, {}));
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nimport QindaTK as Tk\nTk.EditTimeline { width: 800; height: 400 }",
                      QUrl(QStringLiteral("inline.qml")));
    std::unique_ptr<QObject> timeline(component.create());
    QVERIFY2(timeline, qPrintable(component.errorString()));
    timeline->setProperty("model", QVariant::fromValue(&model));
    timeline->setProperty("transportModel", QVariant::fromValue(&transport));

    // The playhead is the engine's acknowledged position, converted exactly.
    clock.externalState(96000, false, false);
    QTRY_COMPARE(timeline->property("effectivePlayheadTicks").toString(), QString::number(2 * S));

    // A seek is a request: nothing moves until the engine acknowledges it.
    QSignalSpy seeks(timeline.get(), SIGNAL(seekRequested(QString)));
    QVERIFY(QMetaObject::invokeMethod(timeline.get(), "seek", Q_ARG(QVariant, QString::number(3 * S))));
    QCOMPARE(seeks.count(), 1);
    QCOMPARE(transport.pendingCount(), 1);
    QCOMPARE(timeline->property("effectivePlayheadTicks").toString(), QString::number(2 * S));
    QVERIFY(clock.acknowledge(clock.pendingIds().constFirst()));
    QTRY_COMPARE(timeline->property("effectivePlayheadTicks").toString(), QString::number(3 * S));

    // The loop band on the ruler shows only the acknowledged loop.
    QObject *loop = timeline->findChild<QObject *>(QStringLiteral("timelineLoopRange"));
    QVERIFY(loop);
    QVERIFY(!loop->property("hasRange").toBool());
    transport.requestLoop(true, QStringLiteral("48000"), QStringLiteral("96000"));
    QVERIFY(clock.acknowledge(clock.pendingIds().constFirst()));
    QTRY_COMPARE(loop->property("startTicks").toString(), QString::number(S));
    QCOMPARE(loop->property("endTicks").toString(), QString::number(2 * S));
    QVariant committed;
    QVERIFY(QMetaObject::invokeMethod(loop, "commitRange", Q_RETURN_ARG(QVariant, committed),
                                      Q_ARG(QVariant, QString::number(S)),
                                      Q_ARG(QVariant, QString::number(4 * S))));
    QVERIFY(committed.toBool());
    QCOMPARE(transport.pendingCount(), 1);
    QCOMPARE(loop->property("endTicks").toString(), QString::number(2 * S)); // still the old loop
    QVERIFY(clock.acknowledge(clock.pendingIds().constFirst()));
    QTRY_COMPARE(loop->property("endTicks").toString(), QString::number(4 * S));
    QCOMPARE(transport.loopEndSample(), QStringLiteral("192000"));
}

QTEST_MAIN(TimelineTransportTest)
#include "tst_timeline_transport.moc"
