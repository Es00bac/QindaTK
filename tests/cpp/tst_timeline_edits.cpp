// SPDX-License-Identifier: LGPL-3.0-or-later
#include "timeline_fixture.h"

#include <QSignalSpy>
#include <QtTest>
#include <limits>

using namespace QindaTK;
using namespace TimelineFixture;

// Regression cases from the WP-048 independent review: ripple edit points,
// extreme pointer positions, history limits, time-map changes, insert IDs and
// the IDs a commit reports.
class TimelineEditsTest : public QObject {
    Q_OBJECT
private slots:
    void parallelRippleTrimsInsertTimeOnce();
    void extremePointersAreRejectedNotWrapped();
    void historyLimitKeepsTheNearestRedo();
    void timeMapChangeRetiresTheEdit();
    void insertTemplatesNeedDistinctIds();
    void commitReportsCreatedIds();
};

namespace {
qint64 startOf(const TimelineModel &model, int n) { return model.clipRecord(uid(n))->start; }

void moveOneSecond(TimelineModel &model)
{
    auto r = request(TimelineEditKind::Move, {uid(11)}, 0);
    auto input = at(S);
    input.snap = false;
    QVERIFY(!model.apply(r, input));
}
} // namespace

void TimelineEditsTest::parallelRippleTrimsInsertTimeOnce()
{
    // All-tracks scope: clips 11 and 21 end together at 1 s; one 0.5 s ripple
    // trim of both inserts 0.5 s once, on every track.
    TimelineModel model;
    QVERIFY(model.setContent({S, 1}, {track(1), track(2), track(3)},
                             {clip(11, 1, 0, S), clip(12, 1, S, S), clip(21, 2, 0, S), clip(31, 3, 2 * S, S)}, {}));
    model.policy()->setRippleScope(TimelineEditPolicy::RippleAllTracks);
    auto trim = request(TimelineEditKind::TrimEnd, {uid(11), uid(21)}, S, true);
    trim.anchorClipId = uid(11);
    auto input = at(S + S / 2);
    input.snap = false;
    QVERIFY(!model.apply(trim, input));
    QCOMPARE(model.clipRecord(uid(11))->end(), S + S / 2);
    QCOMPARE(model.clipRecord(uid(21))->end(), S + S / 2);
    QCOMPARE(startOf(model, 12), S + S / 2); // no gap opens after the trimmed clip
    QCOMPARE(startOf(model, 31), 2 * S + S / 2);
    QVERIFY(model.undo());
    QCOMPARE(startOf(model, 12), S);
    QCOMPARE(startOf(model, 31), 2 * S);

    // Distinct edit points still accumulate downstream: 11 ends at 1 s and 22
    // at 1.5 s, so the clip after both moves by both trims.
    TimelineModel staggered;
    QVERIFY(staggered.setContent({S, 1}, {track(1), track(2), track(3)},
                                 {clip(11, 1, 0, S), clip(22, 2, 0, S + S / 2), clip(31, 3, 2 * S, S)}, {}));
    staggered.policy()->setRippleScope(TimelineEditPolicy::RippleAllTracks);
    auto both = request(TimelineEditKind::TrimEnd, {uid(11), uid(22)}, S, true);
    both.anchorClipId = uid(11);
    auto quarter = at(S + S / 4);
    quarter.snap = false;
    QVERIFY(!staggered.apply(both, quarter));
    QCOMPARE(startOf(staggered, 31), 2 * S + S / 2);

    // Track scope with overlap allowed: two clips on one track that end
    // together are one edit point, not two.
    TimelineModel overlapping;
    QVERIFY(overlapping.setContent({S, 1}, {track(1)},
                                   {clip(11, 1, 0, S), clip(12, 1, S / 2, S / 2), clip(13, 1, 2 * S, S)}, {}));
    overlapping.policy()->setOverlapMode(TimelineEditPolicy::AllowOverlap);
    auto pair = request(TimelineEditKind::TrimEnd, {uid(11), uid(12)}, S, true);
    QVERIFY(!overlapping.apply(pair, input));
    QCOMPARE(startOf(overlapping, 13), 2 * S + S / 2);
}

void TimelineEditsTest::extremePointersAreRejectedNotWrapped()
{
    constexpr qint64 max = std::numeric_limits<qint64>::max();
    constexpr qint64 min = std::numeric_limits<qint64>::min();
    TimelineModel model;
    QVERIFY(model.setContent({S, 1}, {track(1)}, {clip(11, 1, 10 * S, S)}, {}));
    const quint64 revision = model.revisionValue();
    // A far-right pointer overflows start + delta: refused, never wrapped to 0.
    auto far = at(max);
    far.snap = false;
    const OperationError error = model.apply(request(TimelineEditKind::Move, {uid(11)}, 0), far);
    QCOMPARE(error.code, ErrorCode::InvalidArgument);
    QCOMPARE(startOf(model, 11), 10 * S);
    // A far-left pointer still clamps at zero, the documented behaviour.
    auto left = at(min);
    left.snap = false;
    QVERIFY(!model.apply(request(TimelineEditKind::Move, {uid(11)}, 0), left));
    QCOMPARE(startOf(model, 11), 0);
    // Splits far outside the clip fail cleanly; a trim clamps at the minimum length.
    QVERIFY(model.apply(request(TimelineEditKind::Split, {uid(11)}), far));
    QVERIFY(model.apply(request(TimelineEditKind::Split, {uid(11)}), left));
    QVERIFY(!model.apply(request(TimelineEditKind::TrimStart, {uid(11)}, 0), far));
    QCOMPARE(model.clipRecord(uid(11))->duration, model.policy()->minimumClipTicks());
    QCOMPARE(model.clipRecord(uid(11))->end(), S);
    QCOMPARE(model.revisionValue(), revision + 2);
}

void TimelineEditsTest::historyLimitKeepsTheNearestRedo()
{
    // Five one-second moves, three undone: the cursor sits after step 2.
    TimelineModel model;
    QVERIFY(model.setContent({S, 1}, {track(1)}, {clip(11, 1, 0, S)}, {}));
    for (int i = 0; i < 5; ++i) moveOneSecond(model);
    for (int i = 0; i < 3; ++i) QVERIFY(model.undo());
    QCOMPARE(startOf(model, 11), 2 * S);
    // A limit of three drops the oldest undo steps before any redo step.
    model.setHistoryLimit(3);
    QCOMPARE(model.historyCount(), 3);
    QVERIFY(!model.canUndo());
    for (int i = 0; i < 3; ++i) QVERIFY2(model.redo(), qPrintable(model.lastError()));
    QCOMPARE(startOf(model, 11), 5 * S);
    QVERIFY(!model.canRedo());

    // With nothing left to undo, the redo steps farthest from the cursor go.
    TimelineModel undone;
    QVERIFY(undone.setContent({S, 1}, {track(1)}, {clip(11, 1, 0, S)}, {}));
    for (int i = 0; i < 5; ++i) moveOneSecond(undone);
    for (int i = 0; i < 5; ++i) QVERIFY(undone.undo());
    undone.setHistoryLimit(2);
    QVERIFY(undone.redo());
    QVERIFY(undone.redo());
    QVERIFY(!undone.canRedo());
    QCOMPARE(startOf(undone, 11), 2 * S);
}

void TimelineEditsTest::timeMapChangeRetiresTheEdit()
{
    TimelineModel model;
    QVERIFY(model.setContent({S, 1}, {track(1)}, {clip(11, 1, 0, S)}, {}));
    QSignalSpy invalidated(&model, &TimelineModel::editInvalidated);
    // A grid-snapped drag: its preview snapped under the old frame rate.
    const QString operation = model.begin(request(TimelineEditKind::Move, {uid(11)}, 0));
    QVERIFY(!operation.isEmpty());
    auto input = at(S / 3, S / 10);
    input.grid = TimelineSnapGrid::Frames;
    QVERIFY(!model.update(operation, input).error);
    const quint64 revision = model.revisionValue();
    QVERIFY(model.timeMap()->setFrameRate(25, 1));
    QCOMPARE(invalidated.count(), 1);
    QCOMPARE(invalidated.first().at(1).toString(), QStringLiteral("The time map changed"));
    QVERIFY(!model.editing());
    QCOMPARE(model.commit(operation).code, ErrorCode::StaleRevision);
    QCOMPARE(startOf(model, 11), 0);
    QCOMPARE(model.revisionValue(), revision); // no musical marker moved

    // A musical marker being added: a tempo change before commit retires it
    // even though no stored marker moves.
    const QVariantMap begun = model.beginEdit({{QStringLiteral("kind"), QStringLiteral("addMarker")},
        {QStringLiteral("marker"), QVariantMap{{QStringLiteral("position"), QString::number(2 * S)},
                                               {QStringLiteral("anchor"), QStringLiteral("musical")}}}});
    const QString add = begun.value(QStringLiteral("operationId")).toString();
    QVERIFY(model.updateEdit(add, {}).value(QStringLiteral("ok")).toBool());
    model.timeMap()->setTempoSegments({QVariantMap{{QStringLiteral("tick"), QStringLiteral("0")},
        {QStringLiteral("microsecondsPerQuarter"), QStringLiteral("1000000")},
        {QStringLiteral("numerator"), 4}, {QStringLiteral("denominator"), 4}}});
    QVERIFY(!model.commitEdit(add).value(QStringLiteral("ok")).toBool());
    QCOMPARE(model.markerCount(), 0);
}

void TimelineEditsTest::insertTemplatesNeedDistinctIds()
{
    TimelineModel model;
    QVERIFY(model.setContent({S, 1}, {track(1)}, {}, {}));
    const QString trackId = uid(1).toString(QUuid::WithoutBraces);
    const QVariantMap first{{QStringLiteral("id"), uid(50).toString(QUuid::WithoutBraces)},
                            {QStringLiteral("start"), QStringLiteral("0")},
                            {QStringLiteral("duration"), QString::number(S)}};
    QVariantMap second = first;
    second.insert(QStringLiteral("start"), QString::number(3 * S));
    const QVariantMap result = model.applyEdit({{QStringLiteral("kind"), QStringLiteral("insertClips")},
                                                {QStringLiteral("trackId"), trackId},
                                                {QStringLiteral("clips"), QVariantList{first, second}}},
                                               {{QStringLiteral("pointerTicks"), QStringLiteral("0")}});
    QVERIFY(!result.value(QStringLiteral("ok")).toBool());
    QCOMPARE(result.value(QStringLiteral("error")).toString(), QStringLiteral("Inserted clips need distinct IDs"));
    QCOMPARE(model.clipCount(), 0);
    QVERIFY(!model.editing());
    second.insert(QStringLiteral("id"), uid(51).toString(QUuid::WithoutBraces));
    QVERIFY(model.applyEdit({{QStringLiteral("kind"), QStringLiteral("insertClips")},
                             {QStringLiteral("trackId"), trackId},
                             {QStringLiteral("clips"), QVariantList{first, second}}},
                            {{QStringLiteral("pointerTicks"), QStringLiteral("0")}})
                .value(QStringLiteral("ok")).toBool());
    QCOMPARE(model.clipCount(), 2);
}

void TimelineEditsTest::commitReportsCreatedIds()
{
    TimelineModel model;
    QVERIFY(model.setContent({S, 1}, {track(1)}, {clip(11, 1, 0, 2 * S)}, {}));
    const QString position = QString::number(S);
    // Two markers at one tick: each commit names the marker it created.
    QStringList created;
    for (int i = 0; i < 2; ++i) {
        const QVariantMap result = model.applyEdit({{QStringLiteral("kind"), QStringLiteral("addMarker")},
            {QStringLiteral("marker"), QVariantMap{{QStringLiteral("position"), position}}}});
        const QStringList ids = result.value(QStringLiteral("createdIds")).toStringList();
        QCOMPARE(ids.size(), 1);
        QCOMPARE(model.markerData(ids.first()).value(QStringLiteral("position")).toString(), position);
        created << ids;
    }
    QVERIFY(created.at(0) != created.at(1));
    // A split creates exactly its right part; a move creates nothing.
    const QVariantMap split = model.applyEdit({{QStringLiteral("kind"), QStringLiteral("split")},
                                               {QStringLiteral("clipIds"), QStringList{uid(11).toString(QUuid::WithoutBraces)}}},
                                              {{QStringLiteral("pointerTicks"), position}, {QStringLiteral("snap"), false}});
    const QStringList parts = split.value(QStringLiteral("createdIds")).toStringList();
    QCOMPARE(parts.size(), 1);
    QCOMPARE(model.clipData(parts.first()).value(QStringLiteral("start")).toString(), position);
    const QVariantMap move = model.applyEdit({{QStringLiteral("kind"), QStringLiteral("move")},
                                              {QStringLiteral("clipIds"), parts},
                                              {QStringLiteral("grabTicks"), position}},
                                             {{QStringLiteral("pointerTicks"), QString::number(3 * S)},
                                              {QStringLiteral("snap"), false}});
    QVERIFY(move.value(QStringLiteral("ok")).toBool());
    QVERIFY(move.value(QStringLiteral("createdIds")).toStringList().isEmpty());
}

QTEST_GUILESS_MAIN(TimelineEditsTest)
#include "tst_timeline_edits.moc"
