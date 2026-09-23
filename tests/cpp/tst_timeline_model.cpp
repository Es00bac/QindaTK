// SPDX-License-Identifier: LGPL-3.0-or-later
#include "selection_model.h"
#include "timeline_fixture.h"
#include "timeline_planner.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QtTest>
#include <limits>

using namespace QindaTK;
using namespace TimelineFixture;

namespace {
// A domain veto: nothing may start after five seconds.
class FiveSecondPolicy final : public TimelineEditPolicy {
public:
    OperationError validate(const TimelineModel &, const TimelineChangeSet &changes) const override
    {
        for (const auto &change : changes.changes)
            if (change.clipAfter && change.clipAfter->start > 5 * S)
                return {ErrorCode::Validation, QStringLiteral("Past the five second limit"), {}, {}};
        return {};
    }
};

void loadThree(TimelineModel &model)
{
    // Track 1: A [0,1s) B [2s,3s) C [4s,5s); track 2 (video), track 3 (audio).
    QVERIFY(model.setContent({S, 1}, {track(1), track(2, QStringLiteral("video")), track(3)},
                             {clip(11, 1, 0, S), clip(12, 1, 2 * S, S), clip(13, 1, 4 * S, S)}, {}));
}

qint64 startOf(const TimelineModel &model, int n) { return model.clipRecord(uid(n))->start; }
} // namespace

class TimelineModelTest : public QObject {
    Q_OBJECT
private slots:
    void exactArithmeticKnownAnswers();
    void modelConversionsKnownAnswers();
    void largeSessionIndexesTenThousandClips();
    void moveSnapsToNeighbourEdgeAndUndoes();
    void trimEndpointCrossingClampsAtMinimum();
    void cancelledDragLeavesDocumentUnchanged();
    void staleDragIsRejected();
    void rejectedEditsLeaveDocumentUnchanged();
    void rippleEditsShiftDownstreamClips();
    void splitIsDeterministicAndReversible();
    void crossTrackMovesFollowKindPolicy();
    void selectionSurvivesReorderAndRemoval();
    void persistenceRoundtripsExactPositions();
    void markersKeepDeclaredAnchoring();
    void externalHistoryReplaysGuardedSteps();
};

void TimelineModelTest::exactArithmeticKnownAnswers()
{
    using namespace TimelineTime;
    QCOMPARE(*mulDiv(7, 1, 2, TimeMap::TowardZero), 3);
    QCOMPARE(*mulDiv(7, 1, 2, TimeMap::Floor), 3);
    QCOMPARE(*mulDiv(7, 1, 2, TimeMap::Ceil), 4);
    QCOMPARE(*mulDiv(7, 1, 2, TimeMap::NearestAway), 4);
    QCOMPARE(*mulDiv(-7, 1, 2, TimeMap::TowardZero), -3);
    QCOMPARE(*mulDiv(-7, 1, 2, TimeMap::Floor), -4);
    QCOMPARE(*mulDiv(-7, 1, 2, TimeMap::Ceil), -3);
    QCOMPARE(*mulDiv(-5, 1, 2, TimeMap::NearestAway), -3);
    constexpr qint64 max = std::numeric_limits<qint64>::max();
    constexpr qint64 min = std::numeric_limits<qint64>::min();
    QCOMPARE(*mulDiv(max, 3, 3, TimeMap::Floor), max);        // 128-bit intermediate
    QCOMPARE(*mulDiv(min, 1, 1, TimeMap::Floor), min);
    QVERIFY(!mulDiv(max, 2, 1, TimeMap::Floor));              // overflow is reported
    QVERIFY(!mulDiv(1, (quint64(1) << 62) + 1, 1, TimeMap::Floor));
    QCOMPARE(*rescale(48000, {48000, 1}, {S, 1}, TimeMap::Floor), S);
    QCOMPARE(*rescale(1, {30000, 1001}, {S, 1}, TimeMap::Ceil), qint64(23543520));
    QCOMPARE(*rescale(qint64(23543520) * 1001, {S, 1}, {30000, 1001}, TimeMap::Floor), qint64(1001));
    QCOMPARE(*rescale(1, {S, 1}, {48000, 1}, TimeMap::Floor), qint64(0));
    QCOMPARE(*rescale(1, {S, 1}, {48000, 1}, TimeMap::Ceil), qint64(1));
    QCOMPARE(formatSeconds(3 * S / 2, {S, 1}, 3), QStringLiteral("1.500"));
    QCOMPARE(formatSeconds(-S / 1000, {S, 1}, 3), QStringLiteral("-0.001"));
    QCOMPARE(formatSeconds(1, {S, 1}, 9), QStringLiteral("0.000000001"));
    QCOMPARE(*parseSeconds(QStringLiteral("1.5"), {S, 1}, TimeMap::NearestAway), qint64(1058400000));
    QCOMPARE(*parseSeconds(QStringLiteral("-0.000000001"), {S, 1}, TimeMap::NearestAway), qint64(-1));
    QVERIFY(!parseSeconds(QStringLiteral("0.0000000001"), {S, 1}, TimeMap::NearestAway));
    QVERIFY(!parseDecimal(QStringLiteral("+5")));
    QVERIFY(!parseDecimal(QStringLiteral("9223372036854775808")));
    // Frame lines at 1601.6 samples: ceil(k * 8008 / 5).
    QCOMPARE(*gridBracket(3204, 0, 8008, 5), std::make_pair(qint64(3204), qint64(4805)));
    QCOMPARE(*gridBracket(3203, 0, 8008, 5), std::make_pair(qint64(1602), qint64(3204)));
    QCOMPARE(*TimelineRate::normalized(48000, 1000), (TimelineRate{48, 1}));
    QVERIFY(!TimelineRate::normalized(0, 1));
}

void TimelineModelTest::modelConversionsKnownAnswers()
{
    TimelineModel model;
    QCOMPARE(model.timebaseNumeratorText(), QStringLiteral("705600000"));
    QCOMPARE(*model.ticksToSamples(S, TimeMap::Floor), qint64(48000));
    QCOMPARE(*model.samplesToTicks(1, TimeMap::Floor), qint64(14700));
    QCOMPARE(*model.frameStartTicks(3), qint64(70630560));
    QCOMPARE(*model.ticksToFrames(70630560 - 1, TimeMap::Floor), qint64(2));
    QCOMPARE(*model.ticksToFrames(70630560, TimeMap::Floor), qint64(3));
    const QString minute = model.frameStartTicksText(QStringLiteral("1800"));
    QCOMPARE(minute, QStringLiteral("42378336000"));
    QCOMPARE(model.formatTicks(minute, QStringLiteral("timecode"), true), QStringLiteral("00:01:00;02"));
    QCOMPARE(model.parseTicks(QStringLiteral("00:01:00;02"), QStringLiteral("timecode"), true)
                 .value(QStringLiteral("value")).toString(), minute);
    QVERIFY(!model.parseTicks(QStringLiteral("00:01:00;00"), QStringLiteral("timecode"), true)
                 .value(QStringLiteral("ok")).toBool()); // a dropped label does not exist
    QCOMPARE(model.formatTicks(QString::number(3 * S / 2), QStringLiteral("seconds")), QStringLiteral("1.500s"));
    QCOMPARE(model.parseTicks(QStringLiteral("0.25 s"), QStringLiteral("seconds"))
                 .value(QStringLiteral("value")).toString(), QString::number(S / 4));
    QCOMPARE(model.formatTicks(QString::number(S), QStringLiteral("samples")), QStringLiteral("48000"));
    // Beyond 2^53 a QML Number would round; decimal strings stay exact.
    QCOMPARE(model.addTicks(QStringLiteral("9007199254740993"), QStringLiteral("2")),
             QStringLiteral("9007199254740995"));
    QCOMPARE(model.compareTicks(QStringLiteral("9007199254740993"), QStringLiteral("9007199254740992")), 1);
    QCOMPARE(model.addTicks(QStringLiteral("9223372036854775807"), QStringLiteral("1")), QString());
    QCOMPARE(model.stepTicks(QStringLiteral("0"), QStringLiteral("frames"), 1), QStringLiteral("23543520"));
    QCOMPARE(model.stepTicks(QStringLiteral("23543521"), QStringLiteral("frames"), -1), QStringLiteral("23543520"));
    QCOMPARE(model.stepTicks(QStringLiteral("23543520"), QStringLiteral("frames"), -1), QStringLiteral("0"));
    model.timeMap()->setFrameRate(25, 1);
    QCOMPARE(*model.frameStartTicks(25), S);
}

void TimelineModelTest::largeSessionIndexesTenThousandClips()
{
    TimelineModel model;
    QElapsedTimer timer;
    timer.start();
    QVERIFY(loadLargeSession(model));
    const qint64 loadMs = timer.elapsed();
    QCOMPARE(model.trackCount(), 100);
    QCOMPARE(model.clipCount(), 10000);
    QCOMPARE(model.contentEnd(), 199 * S);
    const auto hits = model.clipsInRange(uid(50), 10 * S, 20 * S);
    QCOMPARE(hits.size(), 5);
    QCOMPARE(model.clipRecord(hits.first())->start, 10 * S);
    timer.restart();
    const auto &edges = model.clipEdges();
    QCOMPARE(int(edges.size()), 20000);
    const qint64 edgeMs = timer.elapsed();
    // Move one clip with snapping against 20,000 edges and commit it.
    timer.restart();
    const QUuid target = uid(100000 + 49 * 100 + 5);
    const QString op = model.begin(request(TimelineEditKind::Move, {target}, 10 * S));
    QVERIFY(!op.isEmpty());
    const TimelinePlan plan = model.update(op, at(10 * S + S - S / 100, S / 20));
    QVERIFY(!plan.error);
    QCOMPARE(plan.snap.kind, QStringLiteral("clipEnd"));
    QVERIFY(!model.commit(op));
    const qint64 editMs = timer.elapsed();
    QCOMPARE(model.clipRecord(target)->start, 11 * S);
    qInfo("100 tracks x 10,000 clips: load %lld ms, edge index %lld ms, snapped edit %lld ms",
          loadMs, edgeMs, editMs);
    QVERIFY2(loadMs < 5000 && editMs < 2000, "the fixture must stay interactive");
}

void TimelineModelTest::moveSnapsToNeighbourEdgeAndUndoes()
{
    TimelineModel model;
    loadThree(model);
    const QString revision = model.revisionText();
    const QString op = model.begin(request(TimelineEditKind::Move, {uid(12)}, 2 * S));
    QVERIFY(!op.isEmpty());
    TimelinePlan plan = model.update(op, at(2 * S - S * 95 / 100, S / 10));
    QVERIFY(!plan.error);
    QVERIFY(plan.snap.snapped);
    QCOMPARE(plan.snap.kind, QStringLiteral("clipEnd"));
    QCOMPARE(plan.snap.targetId, uid(11));
    QCOMPARE(model.effectiveClip(uid(12))->start, S);
    QCOMPARE(startOf(model, 12), 2 * S); // committed state is untouched by preview
    QCOMPARE(model.revisionText(), revision);
    plan = model.update(op, at(2 * S - S * 95 / 100, 0)); // snapping off: exact pointer
    QCOMPARE(model.effectiveClip(uid(12))->start, 2 * S - S * 95 / 100);
    plan = model.update(op, at(2 * S - S * 95 / 100, S / 10));
    QString transaction;
    QVERIFY(!model.commit(op, &transaction));
    QCOMPARE(transaction, op);
    QCOMPARE(startOf(model, 12), S);
    QCOMPARE(model.historyCount(), 1);
    QCOMPARE(model.undoText(), QStringLiteral("Move clips"));
    QVERIFY(model.undo());
    QCOMPARE(startOf(model, 12), 2 * S);
    QVERIFY(model.redo());
    QCOMPARE(startOf(model, 12), S);
    // Grid snapping lands on an exact frame start.
    const QString op2 = model.begin(request(TimelineEditKind::Move, {uid(13)}, 4 * S));
    TimelineEditInput input = at(4 * S + 1000, S / 100);
    input.grid = TimelineSnapGrid::Frames;
    plan = model.update(op2, input);
    QCOMPARE(plan.snap.kind, QStringLiteral("grid"));
    QCOMPARE(model.effectiveClip(uid(13))->start, *model.frameStartTicks(*model.ticksToFrames(4 * S, TimeMap::NearestAway)));
    model.cancel(op2);
}

void TimelineModelTest::trimEndpointCrossingClampsAtMinimum()
{
    TimelineModel model;
    loadThree(model);
    QVERIFY(!model.apply(request(TimelineEditKind::TrimEnd, {uid(11)}, S), at(-2 * S)));
    QCOMPARE(model.clipRecord(uid(11))->duration, qint64(1)); // the end never crosses the start
    QVERIFY(model.undo());
    model.policy()->setMinimumClipTicks(S / 10);
    QVERIFY(!model.apply(request(TimelineEditKind::TrimStart, {uid(12)}, 2 * S), at(5 * S)));
    const TimelineClip trimmed = *model.clipRecord(uid(12));
    QCOMPARE(trimmed.duration, S / 10);
    QCOMPARE(trimmed.start, 3 * S - S / 10);
    QCOMPARE(trimmed.sourceOffset, S - S / 10); // media stays aligned
    // Source bounds: extending the head before the media start clamps at zero.
    QVERIFY(model.undo());
    QVERIFY(!model.apply(request(TimelineEditKind::TrimStart, {uid(12)}, 2 * S), at(S)));
    QCOMPARE(startOf(model, 12), 2 * S);
    model.policy()->setAllowBeyondSource(true);
    QVERIFY(!model.apply(request(TimelineEditKind::TrimStart, {uid(12)}, 2 * S), at(S + S / 2)));
    QCOMPARE(startOf(model, 12), S + S / 2);
    QCOMPARE(model.clipRecord(uid(12))->sourceOffset, -S / 2);
}

void TimelineModelTest::cancelledDragLeavesDocumentUnchanged()
{
    TimelineModel model;
    loadThree(model);
    const QVariantMap before = model.contentMap();
    const QString revision = model.revisionText();
    QSignalSpy previews(&model, &TimelineModel::previewChanged);
    const QString op = model.begin(request(TimelineEditKind::Move, {uid(11), uid(12)}, 0));
    for (int step = 1; step <= 20; ++step) model.update(op, at(step * S / 7, 0, (step % 2) * 2));
    QVERIFY(model.hasPreview());
    QVERIFY(previews.count() >= 20);
    model.cancel(op);
    QVERIFY(!model.hasPreview());
    QVERIFY(!model.editing());
    QCOMPARE(model.contentMap(), before);
    QCOMPARE(model.revisionText(), revision);
    QCOMPARE(model.historyCount(), 0);
    QVERIFY(model.commit(op)); // a cancelled operation can no longer commit
}

void TimelineModelTest::staleDragIsRejected()
{
    TimelineModel model;
    loadThree(model);
    QSignalSpy invalidated(&model, &TimelineModel::editInvalidated);
    QString op = model.begin(request(TimelineEditKind::Move, {uid(12)}, 2 * S));
    model.update(op, at(3 * S));
    // A reload during the drag retires the gesture.
    QVERIFY(model.setContent({S, 1}, {track(1)}, {clip(12, 1, 2 * S, S)}, {}));
    QCOMPARE(invalidated.count(), 1);
    QCOMPARE(invalidated.first().first().toString(), op);
    QCOMPARE(model.update(op, at(4 * S)).error.code, ErrorCode::StaleRevision);
    QCOMPARE(model.commit(op).code, ErrorCode::StaleRevision);
    QCOMPARE(startOf(model, 12), 2 * S);
    // Undo during a drag retires it too, and the undo still applies exactly.
    QVERIFY(!model.apply(request(TimelineEditKind::Move, {uid(12)}, 0), at(S)));
    op = model.begin(request(TimelineEditKind::Move, {uid(12)}, 3 * S));
    model.update(op, at(5 * S));
    QVERIFY(model.undo());
    QCOMPARE(invalidated.count(), 2);
    QVERIFY(model.commit(op));
    QCOMPARE(startOf(model, 12), 2 * S);
    // A revision guard from an older snapshot is refused before anything starts.
    TimelineEditRequest guarded = request(TimelineEditKind::Move, {uid(12)}, 0);
    SnapshotHeader old = model.snapshotHeader();
    old.revision.value -= 1;
    guarded.expected = old;
    OperationError error;
    QVERIFY(model.begin(guarded, &error).isEmpty());
    QCOMPARE(error.code, ErrorCode::StaleRevision);
    // A second gesture cannot start while one is active.
    op = model.begin(request(TimelineEditKind::Move, {uid(12)}, 0));
    QVERIFY(model.begin(request(TimelineEditKind::Move, {uid(12)}, 0), &error).isEmpty());
    QCOMPARE(error.code, ErrorCode::Conflict);
    model.cancel(op);
}

void TimelineModelTest::rejectedEditsLeaveDocumentUnchanged()
{
    TimelineModel model;
    loadThree(model);
    const QVariantMap before = model.contentMap();
    model.policy()->setOverlapMode(TimelineEditPolicy::RejectOverlap);
    QString op = model.begin(request(TimelineEditKind::Move, {uid(12)}, 2 * S));
    const TimelinePlan plan = model.update(op, at(S / 2));
    QCOMPARE(plan.error.code, ErrorCode::Conflict);
    QVERIFY(!model.editPreview().value(QStringLiteral("ok")).toBool());
    QCOMPARE(model.commit(op).code, ErrorCode::Conflict);
    QCOMPARE(model.contentMap(), before);
    QCOMPARE(model.historyCount(), 0);
    // A locked clip refuses the gesture before a preview exists.
    TimelineClip locked = *model.clipRecord(uid(13));
    locked.locked = true;
    QVERIFY(model.setContent({S, 1}, {track(1)}, {*model.clipRecord(uid(11)), locked}, {}));
    OperationError error;
    QVERIFY(model.begin(request(TimelineEditKind::Move, {uid(13)}, 0), &error).isEmpty());
    QCOMPARE(error.code, ErrorCode::Validation);
    // A domain policy veto is applied to the complete change set.
    FiveSecondPolicy policy;
    model.setPolicy(&policy);
    op = model.begin(request(TimelineEditKind::Move, {uid(11)}, 0));
    QCOMPARE(model.update(op, at(6 * S)).error.message, QStringLiteral("Past the five second limit"));
    QVERIFY(model.commit(op));
    QCOMPARE(startOf(model, 11), 0);
    model.policy()->setUnavailable(QStringLiteral("split"), QStringLiteral("Splitting is off in this mode"));
    QVERIFY(model.begin(request(TimelineEditKind::Split, {uid(11)}, 0), &error).isEmpty());
    QCOMPARE(error.message, QStringLiteral("Splitting is off in this mode"));
    model.setPolicy(nullptr);
}

void TimelineModelTest::rippleEditsShiftDownstreamClips()
{
    TimelineModel model;
    loadThree(model);
    QVERIFY(!model.apply(request(TimelineEditKind::Delete, {uid(12)}, 0, true), at(0)));
    QVERIFY(!model.clipRecord(uid(12)));
    QCOMPARE(startOf(model, 13), 3 * S); // the one-second hole closed
    QCOMPARE(model.undoText(), QStringLiteral("Ripple delete clips"));
    QVERIFY(model.undo());
    QCOMPARE(startOf(model, 12), 2 * S);
    QCOMPARE(startOf(model, 13), 4 * S);
    QVERIFY(!model.apply(request(TimelineEditKind::TrimEnd, {uid(11)}, S, true), at(S + S / 2)));
    QCOMPARE(model.clipRecord(uid(11))->duration, S + S / 2);
    QCOMPARE(startOf(model, 12), 2 * S + S / 2);
    QCOMPARE(startOf(model, 13), 4 * S + S / 2);
    QVERIFY(model.undo());
    QVERIFY(!model.apply(request(TimelineEditKind::TrimStart, {uid(12)}, 2 * S, true), at(2 * S + S / 4)));
    QCOMPARE(startOf(model, 12), 2 * S); // head ripple keeps the start
    QCOMPARE(model.clipRecord(uid(12))->duration, S - S / 4);
    QCOMPARE(model.clipRecord(uid(12))->sourceOffset, S / 4);
    QCOMPARE(startOf(model, 13), 4 * S - S / 4);
    QVERIFY(model.undo());
    QVERIFY(!model.apply(request(TimelineEditKind::Move, {uid(12)}, 2 * S, true), at(3 * S)));
    QCOMPARE(startOf(model, 11), 0);
    QCOMPARE(startOf(model, 12), 3 * S);
    QCOMPARE(startOf(model, 13), 5 * S);
    QCOMPARE(model.historyCount(), 1); // each commit after an undo replaced the redo branch
    model.policy()->setMaxChangesPerEdit(1);
    QVERIFY(model.apply(request(TimelineEditKind::Move, {uid(12)}, 3 * S, true), at(4 * S)));
    QCOMPARE(startOf(model, 12), 3 * S);
}

void TimelineModelTest::splitIsDeterministicAndReversible()
{
    TimelineModel model;
    loadThree(model);
    const QString op = model.begin(request(TimelineEditKind::Split, {uid(11)}, 0));
    const QUuid right = QUuid::createUuidV5(QUuid(op), uid(11).toRfc4122());
    model.update(op, at(S / 4));
    QVERIFY(model.effectiveClip(right));
    model.update(op, at(S / 2)); // the same new ID across preview updates
    QCOMPARE(model.effectiveClip(right)->start, S / 2);
    QVERIFY(!model.commit(op));
    QCOMPARE(model.clipRecord(uid(11))->duration, S / 2);
    const TimelineClip part = *model.clipRecord(right);
    QCOMPARE(part.start, S / 2);
    QCOMPARE(part.duration, S / 2);
    QCOMPARE(part.sourceOffset, S / 2);
    QCOMPARE(part.resourceId, QStringLiteral("resource:11"));
    QVERIFY(model.undo());
    QVERIFY(!model.clipRecord(right));
    QCOMPARE(model.clipRecord(uid(11))->duration, S);
    QVERIFY(model.redo());
    QCOMPARE(*model.clipRecord(right), part);
    OperationError error = model.apply(request(TimelineEditKind::Split, {uid(12)}, 0), at(S / 2));
    QCOMPARE(error.code, ErrorCode::Validation); // the point is outside the clip
}

void TimelineModelTest::crossTrackMovesFollowKindPolicy()
{
    TimelineModel model;
    loadThree(model);
    OperationError error = model.apply(request(TimelineEditKind::Move, {uid(11)}, 0), at(0, 0, 1));
    QCOMPARE(error.code, ErrorCode::Validation); // audio clip onto the video track
    QCOMPARE(model.clipRecord(uid(11))->trackId, uid(1));
    QVERIFY(!model.apply(request(TimelineEditKind::Move, {uid(11)}, 0), at(0, 0, 2)));
    QCOMPARE(model.clipRecord(uid(11))->trackId, uid(3));
    QVERIFY(!model.apply(request(TimelineEditKind::Move, {uid(11)}, 0), at(S, 0, 9)));
    QCOMPARE(model.clipRecord(uid(11))->trackId, uid(3)); // clamped at the last track
    QCOMPARE(startOf(model, 11), S);
    model.policy()->setRequireMatchingTrackKind(false);
    QVERIFY(!model.apply(request(TimelineEditKind::Move, {uid(11)}, 0), at(0, 0, -1)));
    QCOMPARE(model.clipRecord(uid(11))->trackId, uid(2));
}

void TimelineModelTest::selectionSurvivesReorderAndRemoval()
{
    TimelineModel model;
    loadThree(model);
    SelectionModel selection;
    QObject::connect(&model, &TimelineModel::clipsRemoved, &selection, &SelectionModel::removeIds);
    const QStringList ids{uid(11).toString(QUuid::WithoutBraces), uid(12).toString(QUuid::WithoutBraces)};
    QVERIFY(selection.select(ids.at(0)));
    QVERIFY(selection.toggle(ids.at(1)));
    TimelineEditRequest reorder;
    reorder.kind = TimelineEditKind::ReorderTrack;
    reorder.trackId = uid(3);
    reorder.beforeTrackId = uid(1);
    QVERIFY(!model.apply(reorder, {}));
    QCOMPARE(model.trackIds().first(), uid(3).toString(QUuid::WithoutBraces));
    QCOMPARE(selection.selectedIds(), ids);
    QVERIFY(!model.apply(request(TimelineEditKind::Move, {uid(12)}, 0), at(S)));
    QCOMPARE(selection.selectedIds(), ids);
    QVERIFY(!model.apply(request(TimelineEditKind::Delete, {uid(11)}), {}));
    QCOMPARE(selection.selectedIds(), QStringList{ids.at(1)});
    QVERIFY(model.undo());
    QVERIFY(model.undo());
    QVERIFY(model.undo());
    QCOMPARE(model.trackIds().first(), uid(1).toString(QUuid::WithoutBraces));
}

void TimelineModelTest::persistenceRoundtripsExactPositions()
{
    TimelineModel model;
    TimelineClip huge = clip(21, 1, 9007199254740993LL, 3);
    huge.sourceOffset = (qint64(1) << 55) + 7;
    huge.sourceDuration = (qint64(1) << 56);
    TimelineMarker marker;
    marker.id = uid(31);
    marker.position = (qint64(1) << 60) + 5;
    marker.duration = 9;
    marker.label = QStringLiteral("Far cue");
    QVERIFY(model.setContent({S, 1}, {track(1)}, {huge}, {marker}));
    QVERIFY(model.setEditRange(QStringLiteral("9007199254740993"), QStringLiteral("9007199254740997")));
    const QString json = model.toJson();
    QVERIFY(json.contains(QStringLiteral("\"start\":\"9007199254740993\"")));
    TimelineModel reopened;
    QVERIFY(reopened.loadJson(json).value(QStringLiteral("ok")).toBool());
    QCOMPARE(reopened.documentId(), model.documentId());
    QCOMPARE(*reopened.clipRecord(uid(21)), huge);
    QCOMPARE(reopened.markerRecord(uid(31))->position, marker.position);
    QCOMPARE(reopened.editRangeStartText(), QStringLiteral("9007199254740993"));
    QCOMPARE(reopened.contentMap(), model.contentMap());
    // Numbers are refused where a QML Number could already have rounded.
    QVariantMap bad = model.contentMap();
    QVariantList clips = bad.value(QStringLiteral("clips")).toList();
    QVariantMap first = clips.first().toMap();
    first.insert(QStringLiteral("start"), 9007199254740993.0);
    clips[0] = first;
    bad.insert(QStringLiteral("clips"), clips);
    QVERIFY(!reopened.loadContent(bad).value(QStringLiteral("ok")).toBool());
    QCOMPARE(reopened.contentMap(), model.contentMap()); // a rejected load changes nothing
}

void TimelineModelTest::markersKeepDeclaredAnchoring()
{
    TimelineModel model;
    QVERIFY(model.setContent({S, 1}, {track(1)}, {}, {}));
    TimelineEditRequest add;
    add.kind = TimelineEditKind::AddMarker;
    TimelineMarker musical;
    musical.id = uid(41);
    musical.position = 2 * S; // four quarters at the default 120 BPM
    musical.anchor = TimelineMarker::Anchor::Musical;
    musical.label = QStringLiteral("Bar 2");
    add.marker = musical;
    QVERIFY(!model.apply(add, {}));
    TimelineMarker timed = musical;
    timed.id = uid(42);
    timed.anchor = TimelineMarker::Anchor::Time;
    add.marker = timed;
    QVERIFY(!model.apply(add, {}));
    QCOMPARE(model.markerRecord(uid(41))->musicalTick, qint64(3840));
    const QString revision = model.revisionText();
    model.timeMap()->setTempoSegments({QVariantMap{{QStringLiteral("tick"), QStringLiteral("0")},
        {QStringLiteral("microsecondsPerQuarter"), QStringLiteral("1000000")},
        {QStringLiteral("numerator"), 4}, {QStringLiteral("denominator"), 4}}});
    QCOMPARE(model.markerRecord(uid(41))->position, 4 * S); // follows the beat
    QCOMPARE(model.markerRecord(uid(42))->position, 2 * S); // keeps its time
    QVERIFY(model.revisionText() != revision);
    model.timeMap()->setFrameRate(24000, 1001);             // another rate view moves nothing
    QCOMPARE(model.markerRecord(uid(41))->position, 4 * S);
    // Moving a musical marker re-derives its beat; undo restores the beat.
    TimelineEditRequest move;
    move.kind = TimelineEditKind::MoveMarker;
    move.markerId = uid(41);
    move.grabTicks = 4 * S;
    QVERIFY(!model.apply(move, at(6 * S)));
    QCOMPARE(model.markerRecord(uid(41))->musicalTick, qint64(5760));
    QVERIFY(model.undo());
    QCOMPARE(model.markerRecord(uid(41))->musicalTick, qint64(3840));
    TimelineModel reopened;
    reopened.timeMap()->setTempoSegments(model.timeMap()->tempoSegments());
    QVERIFY(reopened.loadJson(model.toJson()).value(QStringLiteral("ok")).toBool());
    QCOMPARE(reopened.markerRecord(uid(41))->anchor, TimelineMarker::Anchor::Musical);
    QCOMPARE(reopened.markerRecord(uid(41))->position, 4 * S);
    QCOMPARE(reopened.markerRecord(uid(42))->position, 2 * S);
}

void TimelineModelTest::externalHistoryReplaysGuardedSteps()
{
    TimelineModel model;
    loadThree(model);
    model.setExternalUndo(true);
    QSignalSpy committed(&model, &TimelineModel::committed);
    QString transaction;
    QVERIFY(!model.apply(request(TimelineEditKind::Move, {uid(13)}, 4 * S), at(6 * S), &transaction));
    QCOMPARE(committed.count(), 1);
    QCOMPARE(committed.first().at(0).toString(), transaction);
    const QVariantList payload = committed.first().at(2).toList();
    QCOMPARE(payload.size(), 1);
    QCOMPARE(payload.first().toMap().value(QStringLiteral("after")).toMap()
                 .value(QStringLiteral("start")).toString(), QString::number(6 * S));
    QVERIFY(!model.canUndo());
    QVERIFY(!model.undo());
    QVERIFY(!model.applyHistoryChanges(transaction, true, QStringLiteral("1")));
    QVERIFY(model.applyHistoryChanges(transaction, true, model.revisionText()));
    QCOMPARE(startOf(model, 13), 4 * S);
    QVERIFY(!model.applyHistoryChanges(transaction, true, model.revisionText())); // diverged
    QVERIFY(model.applyHistoryChanges(transaction, false, model.revisionText()));
    QCOMPARE(startOf(model, 13), 6 * S);
    QVERIFY(model.releaseHistoryTransaction(transaction));
    QVERIFY(!model.applyHistoryChanges(transaction, true, model.revisionText()));
}

QTEST_MAIN(TimelineModelTest)
#include "tst_timeline_model.moc"
