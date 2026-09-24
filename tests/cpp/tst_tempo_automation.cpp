// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-049 focused checks: tempo edits recompute the TimeMap and move
// beat-locked (not time-locked) clips exactly and reversibly; automation
// edits, record passes and engine snapshots agree before/after undo and
// save/reopen. Known answers use 48 kHz, 960 PPQ and 705,600,000 ticks/s.
#include "engine_schedule.h"
#include "mock_automation_engine.h"
#include "timeline_fixture.h"

#include <QtTest>
#include <thread>

using namespace QindaTK;
using namespace TimelineFixture;

namespace {
constexpr qint64 PPQ = 960;
constexpr qint64 Bar = 4 * PPQ; // 4/4

struct Session {
    TimeMap map;
    TimelineModel timeline;
    TempoMapModel tempo;
    Session()
    {
        timeline.setTimeMap(&map);
        tempo.setTimeMap(&map);
        tempo.setTimeline(&timeline);
    }
    // Clip 1 beat-locked at bar 3 (4 s at 120 BPM), clip 2 time-locked at the
    // same place, clip 3 beat-locked 12,345 ticks past bar 3 (off the PPQ grid).
    void load()
    {
        QVector<TimelineClip> clips{clip(11, 1, 4 * S, S), clip(12, 2, 4 * S, S), clip(13, 3, 4 * S + 12345, S)};
        clips[0].anchor = clips[2].anchor = TimelineClip::Anchor::Musical;
        clips[0] = timeline.anchoredClip(clips[0]);
        clips[2] = timeline.anchoredClip(clips[2]);
        QVERIFY(timeline.setContent({S, 1}, {track(1), track(2), track(3)}, clips, {}));
    }
    qint64 start(int n) const { return timeline.clipRecord(uid(n))->start; }
};

QVariantMap addTempo(qint64 tick, const QString &bpm)
{
    return {{QStringLiteral("kind"), QStringLiteral("addTempo")}, {QStringLiteral("tick"), QString::number(tick)},
            {QStringLiteral("bpm"), bpm}};
}

AutomationParameter cutoff()
{
    AutomationParameter parameter;
    parameter.id = QStringLiteral("synth/cutoff");
    parameter.name = QStringLiteral("Cutoff");
    parameter.range = {UnitDimension::Frequency, QStringLiteral("Hz"), 20.0, 20000.0};
    parameter.defaultValue = 1000.0;
    return parameter;
}

QVariantMap point(qint64 ticks, double value)
{
    return {{QStringLiteral("kind"), QStringLiteral("addPoint")}, {QStringLiteral("parameterId"), cutoff().id},
            {QStringLiteral("ticks"), QString::number(ticks)}, {QStringLiteral("value"), value}};
}
} // namespace

class TestTempoAutomation : public QObject {
    Q_OBJECT
private slots:
    void tempoEditMovesBeatLockedClipsOnly()
    {
        Session s;
        s.load();
        QCOMPARE(s.start(11), 4 * S);
        const qint64 offClip = s.start(13);
        // 60 BPM from bar 2: bar 3 is now 2 s + 4 s = 6 s later in absolute time.
        const QVariantMap result = s.tempo.applyEdit(addTempo(Bar, QStringLiteral("60")));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
        QCOMPARE(s.map.tempoSegments().size(), 2);
        QCOMPARE(s.map.tempoSegments().at(1).toMap().value(QStringLiteral("microsecondsPerQuarter")).toString(),
                 QStringLiteral("1000000"));
        QCOMPARE(s.start(11), 6 * S);   // beat-locked follows the beat
        QCOMPARE(s.start(12), 4 * S);   // time-locked stays
        QCOMPARE(s.start(13) - 6 * S, offClip - 4 * S); // sub-PPQ offset held exactly
        QVERIFY(s.tempo.undo());
        QCOMPARE(s.start(11), 4 * S);
        QCOMPARE(s.start(13), offClip); // bit-exact after undo
        QVERIFY(s.tempo.redo());
        QCOMPARE(s.start(11), 6 * S);
    }

    void tempoPreviewCancelAndMove()
    {
        Session s;
        s.load();
        QVERIFY(s.tempo.applyEdit(addTempo(Bar, QStringLiteral("60"))).value(QStringLiteral("ok")).toBool());
        const QString id = s.tempo.eventIds().last();
        const QString revision = s.tempo.revisionText();
        const QVariantMap begun = s.tempo.beginEdit({{QStringLiteral("kind"), QStringLiteral("move")},
                                                     {QStringLiteral("eventId"), id}});
        const QString operation = begun.value(QStringLiteral("operationId")).toString();
        QVERIFY(!operation.isEmpty());
        // Preview: the 60 BPM change moved to bar 3 leaves clip 1 at 4 s live.
        QVERIFY(s.tempo.updateEdit(operation, {{QStringLiteral("tick"), QString::number(2 * Bar)}})
                    .value(QStringLiteral("valid")).toBool());
        QCOMPARE(s.start(11), 4 * S);
        QVERIFY(s.tempo.cancelEdit(operation));
        QCOMPARE(s.start(11), 6 * S);
        QCOMPARE(s.tempo.revisionText(), revision);
        // A meter change off a bar line and moving the first tempo are refused.
        QVERIFY(!s.tempo.applyEdit({{QStringLiteral("kind"), QStringLiteral("addMeter")},
                                    {QStringLiteral("tick"), QString::number(Bar + PPQ)},
                                    {QStringLiteral("numerator"), 3}, {QStringLiteral("denominator"), 4}})
                     .value(QStringLiteral("ok")).toBool());
        QVERIFY(!s.tempo.beginEdit({{QStringLiteral("kind"), QStringLiteral("move")},
                                    {QStringLiteral("eventId"), s.tempo.eventIds().first()}})
                     .value(QStringLiteral("ok")).toBool());
        QCOMPARE(s.tempo.formatPosition(QString::number(Bar + PPQ + 5)), QStringLiteral("2.2.005"));
        QCOMPARE(s.tempo.parsePosition(QStringLiteral("2.2.5")).value(QStringLiteral("tick")).toString(),
                 QString::number(Bar + PPQ + 5));
    }

    void tempoAndTimelinePersist()
    {
        Session s;
        s.load();
        QVERIFY(s.tempo.applyEdit(addTempo(Bar, QStringLiteral("90.5"))).value(QStringLiteral("ok")).toBool());
        QVERIFY(s.tempo.applyEdit({{QStringLiteral("kind"), QStringLiteral("addMeter")},
                                   {QStringLiteral("tick"), QString::number(2 * Bar)},
                                   {QStringLiteral("numerator"), 7}, {QStringLiteral("denominator"), 8}})
                    .value(QStringLiteral("ok")).toBool());
        const QString tempoJson = s.tempo.toJson();
        const QString timelineJson = s.timeline.toJson();
        const qint64 moved = s.start(13);
        Session reopened;
        QVERIFY(reopened.tempo.loadJson(tempoJson).value(QStringLiteral("ok")).toBool());
        QVERIFY(reopened.timeline.loadJson(timelineJson).value(QStringLiteral("ok")).toBool());
        QCOMPARE(reopened.tempo.contentMap(), s.tempo.contentMap());
        QCOMPARE(reopened.start(13), moved);
        QCOMPARE(reopened.timeline.clipRecord(uid(13))->musicalOffset, s.timeline.clipRecord(uid(13))->musicalOffset);
        QCOMPARE(reopened.map.tempoSegments(), s.map.tempoSegments());
    }

    void tempoEditRefusesBeatLockedOverlap()
    {
        Session s;
        s.timeline.policy()->setOverlapMode(TimelineEditPolicy::RejectOverlap);
        QVector<TimelineClip> clips{clip(21, 1, 4 * S, S), clip(22, 1, 5 * S, S)};
        clips[1].anchor = TimelineClip::Anchor::Musical; // bar 3 + 2 beats
        clips[1] = s.timeline.anchoredClip(clips[1]);
        QVERIFY(s.timeline.setContent({S, 1}, {track(1)}, clips, {}));
        // Faster tempo pulls the beat-locked clip onto the time-locked one.
        const QVariantMap result = s.tempo.applyEdit(addTempo(Bar, QStringLiteral("240")));
        QVERIFY(!result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(s.map.tempoSegments().size(), 1);
        QCOMPARE(s.timeline.clipRecord(uid(22))->start, 5 * S);
    }

    void automationEditsUndoAndPersist()
    {
        AutomationModel model;
        QVERIFY(model.addParameter(cutoff()));
        const QString id = cutoff().id;
        QVERIFY(model.applyEdit(point(0, 200.0)).value(QStringLiteral("ok")).toBool());
        QVERIFY(model.applyEdit(point(2 * S, 1200.0)).value(QStringLiteral("ok")).toBool());
        QVERIFY(model.applyEdit(point(4 * S, 800.125)).value(QStringLiteral("ok")).toBool());
        QCOMPARE(model.valueAt(id, QString::number(S)), 700.0);
        const QString second = model.adjacentPoint(id, model.adjacentPoint(id, {}, 1), 1);
        // A vertical move keeps the exact time; a segment move shifts both ends.
        QVERIFY(model.applyEdit({{QStringLiteral("kind"), QStringLiteral("movePoints")}, {QStringLiteral("parameterId"), id},
                                 {QStringLiteral("pointIds"), QStringList{second}}},
                                {{QStringLiteral("ticks"), QString::number(2 * S)}, {QStringLiteral("value"), 1500.0}})
                    .value(QStringLiteral("ok")).toBool());
        QCOMPARE(model.pointData(id, second).value(QStringLiteral("ticks")).toString(), QString::number(2 * S));
        QCOMPARE(model.pointData(id, second).value(QStringLiteral("value")).toDouble(), 1500.0);
        QVERIFY(model.applyEdit({{QStringLiteral("kind"), QStringLiteral("moveSegment")}, {QStringLiteral("parameterId"), id},
                                 {QStringLiteral("pointId"), second}}, {{QStringLiteral("value"), 1600.0}})
                    .value(QStringLiteral("ok")).toBool());
        QCOMPARE(model.valueAt(id, QString::number(4 * S)), 900.125);
        // Crossing a neighbour is clamped one tick short, never reordered.
        QVERIFY(model.applyEdit({{QStringLiteral("kind"), QStringLiteral("movePoints")}, {QStringLiteral("parameterId"), id},
                                 {QStringLiteral("pointIds"), QStringList{second}}},
                                {{QStringLiteral("ticks"), QString::number(9 * S)}}).value(QStringLiteral("ok")).toBool());
        QCOMPARE(model.pointData(id, second).value(QStringLiteral("ticks")).toString(), QString::number(4 * S - 1));
        QVERIFY(model.undo());
        // A cubic handle makes the segment an S-curve evaluated by CurveEvaluator.
        QVERIFY(model.applyEdit({{QStringLiteral("kind"), QStringLiteral("setHandle")}, {QStringLiteral("parameterId"), id},
                                 {QStringLiteral("pointId"), second}, {QStringLiteral("side"), QStringLiteral("out")}},
                                {{QStringLiteral("handle"), QVariantMap{{QStringLiteral("x"), 0.5}, {QStringLiteral("y"), 0.9}}}})
                    .value(QStringLiteral("ok")).toBool());
        const double curved = model.valueAt(id, QString::number(3 * S));
        QVERIFY(curved > 1250.0 && curved < 20000.0);
        const QString json = model.toJson();
        AutomationModel reopened;
        QVERIFY(reopened.loadJson(json).value(QStringLiteral("ok")).toBool());
        QCOMPARE(reopened.contentMap(), model.contentMap());
        QCOMPARE(reopened.valueAt(id, QString::number(3 * S)), curved);
        // Narrowing the range clamps out-of-range values in one undo step.
        QVERIFY(model.applyEdit({{QStringLiteral("kind"), QStringLiteral("editParameter")}, {QStringLiteral("parameterId"), id}},
                                {{QStringLiteral("maximum"), 1000.0}}).value(QStringLiteral("ok")).toBool());
        QCOMPARE(model.valueAt(id, QString::number(2 * S)), 1000.0);
        QVERIFY(model.undo());
        QCOMPARE(model.contentMap(), reopened.contentMap());
        while (model.canUndo()) QVERIFY(model.undo());
        QCOMPARE(model.valueAt(id, QString::number(S)), 1000.0); // default, no points
    }

    void recordPassThroughMockEngine()
    {
        TimeMap map;
        TempoMapModel tempo;
        tempo.setTimeMap(&map);
        AutomationModel model;
        QVERIFY(model.addParameter(cutoff()));
        const QString id = cutoff().id;
        QVERIFY(model.applyEdit(point(0, 500.0)).value(QStringLiteral("ok")).toBool());
        model.setModeAvailable(AutomationMode::Latch, false, QStringLiteral("Mock engine: no latch"));
        QCOMPARE(model.setMode(id, AutomationMode::Latch).code, ErrorCode::Unsupported);
        QVERIFY(!model.setMode(id, AutomationMode::Touch));
        EngineSchedule schedule;
        schedule.setTempoModel(&tempo);
        schedule.setAutomationModel(&model);
        MockAutomationEngine engine;
        engine.setSchedule(&schedule);
        engine.setBlockFrames(4800); // 0.1 s at 48 kHz
        engine.play();
        engine.process();                       // 0.0 s
        QVERIFY(engine.touch(id, 5000.0).value(QStringLiteral("ok")).toBool());
        QVERIFY(model.writeState(id).value(QStringLiteral("writing")).toBool());
        QCOMPARE(engine.valuesAt(QStringLiteral("0")).value(QStringLiteral("values")).toMap().value(id).toDouble(), 5000.0);
        engine.process();                       // 0.1 s
        engine.move(id, 6000.0);
        engine.process();                       // 0.2 s
        engine.move(id, 7000.0);
        engine.release(id);                     // at 0.3 s: touch ends the pass
        QVERIFY(!model.writeState(id).value(QStringLiteral("writing")).toBool());
        QCOMPARE(model.historyCount(), 2);      // point + one record pass
        engine.stop();
        // Engine playback reads the committed pass at exact timestamps.
        const auto snapshot = schedule.acquire();
        QCOMPARE(snapshot->valueAtSample(id, 9600).value, 6000.0); // 0.2 s
        QVERIFY(snapshot->valueAtSample(id, 9600).automated);
        QCOMPARE(snapshot->valueAtSample(id, 0).value, 500.0);     // the entry keeps the original start
        QVERIFY(model.undo());
        QCOMPARE(schedule.acquire()->valueAtSample(id, 9600).value, 500.0);
        QCOMPARE(snapshot->valueAtSample(id, 9600).value, 6000.0); // an old snapshot never changes
        // Write mode writes from transport start until stop, touched or not.
        QVERIFY(!model.setMode(id, AutomationMode::Write));
        engine.locate(QStringLiteral("48000"));
        engine.play();
        QVERIFY(model.writeState(id).value(QStringLiteral("writing")).toBool());
        engine.process();
        engine.stop();
        QVERIFY(!model.writeState(id).value(QStringLiteral("writing")).toBool());
        QCOMPARE(model.historyCount(), 2);
    }

    void snapshotIsReadableOnAnotherThread()
    {
        TimeMap map;
        TempoMapModel tempo;
        tempo.setTimeMap(&map);
        QVERIFY(tempo.applyEdit(addTempo(Bar, QStringLiteral("60"))).value(QStringLiteral("ok")).toBool());
        AutomationModel model;
        QVERIFY(model.addParameter(cutoff()));
        QVERIFY(model.applyEdit(point(0, 100.0)).value(QStringLiteral("ok")).toBool());
        QVERIFY(model.applyEdit(point(S, 300.0)).value(QStringLiteral("ok")).toBool());
        EngineSchedule schedule;
        schedule.setTempoModel(&tempo);
        schedule.setAutomationModel(&model);
        std::vector<double> block;
        std::optional<qint64> beat;
        std::thread worker([&] {
            const auto snapshot = schedule.acquire();
            block.resize(4);
            snapshot->renderBlock(cutoff().id, 12000, 4, block.data());
            beat = snapshot->musicalTickAtSample(48000 * 3); // 2 s at 120 + 1 s at 60 BPM
        });
        worker.join();
        QCOMPARE(block.at(0), 150.0); // 0.25 s
        QVERIFY(beat.has_value());
        QCOMPARE(*beat, Bar + PPQ);
    }
};

QTEST_GUILESS_MAIN(TestTempoAutomation)
#include "tst_tempo_automation.moc"
