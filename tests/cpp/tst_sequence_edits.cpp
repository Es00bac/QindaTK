// SPDX-License-Identifier: LGPL-3.0-or-later
#include "sequence_fixture.h"

#include <QtTest>

using namespace QindaTK;
using namespace SequenceFixture;

// WP-071 sequence edits on TimelineModel (docs/sequence-edits.md): roll,
// ripple, slip and slide with linked sound, frame-exact bounds, transitions
// and their handles, source switches, links and undo. No media is needed:
// every bound comes from the clip records (D-118).
class SequenceEditsTest : public QObject {
    Q_OBJECT
private slots:
    void rollMovesTheCutWithLinkedSound();
    void rollClampsToHandlesOnFrames();
    void slipKeepsPlacementWithinSource();
    void slideRollsBothNeighbours();
    void rippleTrimMovesLaterClipsOnLinkedTracks();
    void linkedSplitAndDeleteKeepGroups();
    void transitionsNeedACutAndHandles();
    void editsKeepTransitionsLegal();
    void transitionsPersistAndParseFromMaps();
    void switchSourceCutsLinkedClipsInSync();
    void offlineClipsKeepExactOffsets();
    void undoRestoresEveryLinkedChange();
};

void SequenceEditsTest::rollMovesTheCutWithLinkedSound()
{
    Session s;
    QVERIFY(s.loadStandard());
    // 5 frames and 3 ticks round to the frame start 55F.
    const OperationError error = s.edit(TimelineEditKind::Roll, {ClipA, ClipB}, 5 * F + 3);
    QVERIFY2(!error, qPrintable(error.message));
    QCOMPARE(s.at(ClipA).duration, 55 * F);
    QCOMPARE(s.at(ClipB).start, 55 * F);
    QCOMPARE(s.at(ClipB).duration, 45 * F);
    QCOMPARE(s.at(ClipB).sourceOffset, 25 * F);
    QCOMPARE(s.at(SoundA).duration, 55 * F);
    QCOMPARE(s.at(SoundB).start, 55 * F);
    QCOMPARE(s.at(SoundB).sourceOffset, 25 * F);
    QCOMPARE(s.at(ClipC).start, 100 * F); // a roll never ripples
    QCOMPARE(s.model.contentEnd(), 125 * F);
    QCOMPARE(s.model.historyCount(), 1);

    // Unlinked: only the picture's cut moves.
    QVERIFY(!s.edit(TimelineEditKind::Roll, {ClipA, ClipB}, -F, false));
    QCOMPARE(s.at(ClipA).duration, 54 * F);
    QCOMPARE(s.at(SoundA).duration, 55 * F);
    // A roll needs two adjacent clips on one track.
    QCOMPARE(s.edit(TimelineEditKind::Roll, {ClipA, ClipC}, F).code, ErrorCode::Validation);
    QCOMPARE(s.edit(TimelineEditKind::Roll, {ClipA}, F).code, ErrorCode::InvalidArgument);
}

void SequenceEditsTest::rollClampsToHandlesOnFrames()
{
    Session s;
    QVERIFY(s.loadStandard());
    // B has 20F of head handle: a roll back stops there.
    QVERIFY(!s.edit(TimelineEditKind::Roll, {ClipA, ClipB}, -100 * F));
    QCOMPARE(s.at(ClipB).start, 30 * F);
    QCOMPARE(s.at(ClipB).sourceOffset, 0);
    QCOMPARE(s.at(SoundB).sourceOffset, 0);
    QVERIFY(s.model.undo());
    // A has 40F of tail handle: a roll forward stops there.
    QVERIFY(!s.edit(TimelineEditKind::Roll, {ClipA, ClipB}, 100 * F));
    QCOMPARE(s.at(ClipA).duration, 90 * F);
    QCOMPARE(s.at(ClipA).sourceOffset + s.at(ClipA).duration, s.at(ClipA).sourceDuration);
    QCOMPARE(s.at(ClipB).start, 90 * F);
    // Every edit point is a frame start.
    const auto frame = s.model.ticksToFrames(s.at(ClipB).start, TimeMap::Floor);
    QCOMPARE(s.model.frameStartTicks(*frame).value_or(-1), s.at(ClipB).start);
}

void SequenceEditsTest::slipKeepsPlacementWithinSource()
{
    Session s;
    QVERIFY(s.loadStandard());
    QVERIFY(!s.edit(TimelineEditKind::Slip, {ClipB}, 7 * F));
    QCOMPARE(s.at(ClipB).start, 50 * F);
    QCOMPARE(s.at(ClipB).duration, 50 * F);
    QCOMPARE(s.at(ClipB).sourceOffset, 27 * F);
    QCOMPARE(s.at(SoundB).sourceOffset, 27 * F); // the linked sound slips with it
    // The tail handle left is 100F - (27F + 50F) = 23F.
    QVERIFY(!s.edit(TimelineEditKind::Slip, {ClipB}, 1000 * F));
    QCOMPARE(s.at(ClipB).sourceOffset, 50 * F);
    QVERIFY(!s.edit(TimelineEditKind::Slip, {ClipB}, -1000 * F));
    QCOMPARE(s.at(ClipB).sourceOffset, 0);
    // C uses all of its source: it cannot slip, and nothing changes.
    const int steps = s.model.historyCount();
    QVERIFY(!s.edit(TimelineEditKind::Slip, {ClipC}, F));
    QCOMPARE(s.at(ClipC).sourceOffset, 0);
    QCOMPARE(s.model.historyCount(), steps);
}

void SequenceEditsTest::slideRollsBothNeighbours()
{
    Session s;
    QVERIFY(s.loadStandard());
    QVERIFY(!s.edit(TimelineEditKind::Slide, {ClipB}, 5 * F));
    QCOMPARE(s.at(ClipB).start, 55 * F);
    QCOMPARE(s.at(ClipB).duration, 50 * F);
    QCOMPARE(s.at(ClipB).sourceOffset, 20 * F); // the slid clip shows the same frames
    QCOMPARE(s.at(ClipA).duration, 55 * F);
    QCOMPARE(s.at(ClipC).start, 105 * F);
    QCOMPARE(s.at(ClipC).duration, 20 * F);
    QCOMPARE(s.at(ClipC).sourceOffset, 5 * F);
    QCOMPARE(s.at(SoundB).start, 55 * F);
    QCOMPARE(s.at(SoundA).duration, 55 * F);
    QCOMPARE(s.model.contentEnd(), 125 * F);
    QVERIFY(s.model.undo());
    // Sliding back would need head handle on C, which has none: nothing moves.
    const quint64 revision = s.model.revisionValue();
    QVERIFY(!s.edit(TimelineEditKind::Slide, {ClipB}, -5 * F));
    QCOMPARE(s.model.revisionValue(), revision);
    QCOMPARE(s.at(ClipB).start, 50 * F);
}

void SequenceEditsTest::rippleTrimMovesLaterClipsOnLinkedTracks()
{
    Session s;
    QVERIFY(s.loadStandard());
    QVERIFY(!s.edit(TimelineEditKind::TrimEnd, {ClipA}, -10 * F + 5, true, true));
    QCOMPARE(s.at(ClipA).duration, 40 * F);
    QCOMPARE(s.at(SoundA).duration, 40 * F);
    QCOMPARE(s.at(ClipB).start, 40 * F);
    QCOMPARE(s.at(ClipC).start, 90 * F);
    QCOMPARE(s.at(SoundB).start, 40 * F);
    QCOMPARE(s.at(ClipB).sourceOffset, 20 * F);
    // Ripple of the incoming head: B's in point moves, B stays at its start.
    QVERIFY(!s.edit(TimelineEditKind::TrimStart, {ClipB}, 4 * F, true, true));
    QCOMPARE(s.at(ClipB).start, 40 * F);
    QCOMPARE(s.at(ClipB).sourceOffset, 24 * F);
    QCOMPARE(s.at(ClipB).duration, 46 * F);
    QCOMPARE(s.at(SoundB).sourceOffset, 24 * F);
    QCOMPARE(s.at(ClipC).start, 86 * F);
}

void SequenceEditsTest::linkedSplitAndDeleteKeepGroups()
{
    Session s;
    QVERIFY(s.loadStandard());
    TimelineEditRequest split;
    split.kind = TimelineEditKind::Split;
    split.clipIds = {uid(ClipB)};
    split.linked = true;
    TimelineEditInput input;
    input.pointerTicks = 60 * F;
    input.snap = false;
    QString transaction;
    QVERIFY(!s.model.apply(split, input, &transaction));
    QCOMPARE(s.model.clipCount(), 7);
    const QUuid rightPicture = QUuid::createUuidV5(QUuid(transaction), uid(ClipB).toRfc4122());
    const QUuid rightSound = QUuid::createUuidV5(QUuid(transaction), uid(SoundB).toRfc4122());
    const TimelineClip *picture = s.model.clipRecord(rightPicture);
    const TimelineClip *sound = s.model.clipRecord(rightSound);
    QVERIFY(picture && sound);
    QCOMPARE(picture->sourceOffset, 30 * F);
    QCOMPARE(sound->sourceOffset, 30 * F);
    QVERIFY(!picture->linkId.isNull());
    QCOMPARE(picture->linkId, sound->linkId); // the right parts form a new pair
    QVERIFY(picture->linkId != uid(LinkB));
    const QUuid rightGroup = picture->linkId; // records move when the model changes
    QCOMPARE(s.model.linkedClips(uid(LinkB)), (QVector<QUuid>{uid(ClipB), uid(SoundB)}));
    // Deleting a picture takes its sound with it.
    QVERIFY(!s.edit(TimelineEditKind::Delete, {ClipA}, 0));
    QVERIFY(!s.model.clipRecord(uid(ClipA)));
    QVERIFY(!s.model.clipRecord(uid(SoundA)));
    // Unlinking the picture leaves the sound alone in the group.
    TimelineEditRequest unlink;
    unlink.kind = TimelineEditKind::Unlink;
    unlink.clipIds = {rightPicture};
    QVERIFY(!s.model.apply(unlink, {}));
    QVERIFY(s.model.clipRecord(rightPicture)->linkId.isNull());
    QCOMPARE(s.model.linkedClips(rightGroup), QVector<QUuid>{rightSound});
}

void SequenceEditsTest::transitionsNeedACutAndHandles()
{
    Session s;
    QVERIFY(s.loadStandard());
    // A centred 10-frame dissolve at the A|B cut: 5 frames each side.
    OperationError error = s.transition(ClipB, QStringLiteral("dissolve"), 10 * F, QStringLiteral("center"));
    QVERIFY2(!error, qPrintable(error.message));
    QCOMPARE(s.at(ClipB).transition.beforeCut, 5 * F);
    QCOMPARE(s.at(ClipB).transition.afterCut, 5 * F);
    QCOMPARE(s.at(ClipB).transition.type, QStringLiteral("dissolve"));
    // An odd frame count puts the odd frame before the cut; 7F + 3 ticks rounds to 7 frames.
    QVERIFY(!s.transition(ClipB, QStringLiteral("dissolve"), 7 * F + 3, QStringLiteral("center")));
    QCOMPARE(s.at(ClipB).transition.beforeCut, 4 * F);
    QCOMPARE(s.at(ClipB).transition.afterCut, 3 * F);
    // C has no head handle: an end-aligned transition needs one, a start-aligned one does not.
    error = s.transition(ClipC, QStringLiteral("dissolve"), 10 * F, QStringLiteral("end"));
    QCOMPARE(error.code, ErrorCode::Validation);
    QCOMPARE(error.fieldPath, QStringLiteral("handles"));
    QVERIFY(s.at(ClipC).transition.isNull());
    QVERIFY(!s.transition(ClipC, QStringLiteral("wipe"), 10 * F, QStringLiteral("start"),
                          {{QStringLiteral("direction"), QStringLiteral("up")}}));
    QCOMPARE(s.at(ClipC).transition.beforeCut, 0);
    QCOMPARE(s.at(ClipC).transition.afterCut, 10 * F);
    // B's 30F tail handle limits a start-aligned transition into C.
    QCOMPARE(s.transition(ClipC, QStringLiteral("dip"), 31 * F, QStringLiteral("start")).code,
             ErrorCode::Validation);
    // No clip ends where A starts; parameters are checked.
    QCOMPARE(s.transition(ClipA, QStringLiteral("dissolve"), 4 * F, QStringLiteral("center")).code,
             ErrorCode::Validation);
    QCOMPARE(s.transition(ClipB, QStringLiteral("wipe"), 4 * F, QStringLiteral("center"),
                          {{QStringLiteral("direction"), QStringLiteral("diagonal")}}).code,
             ErrorCode::Validation);
    // Linked: the sound at the same cut takes the transition too.
    QVERIFY(!s.transition(ClipB, QStringLiteral("dissolve"), 2 * F, QStringLiteral("center"), {}, true));
    QCOMPARE(s.at(SoundB).transition.beforeCut, F);
    // Removing (no transition in the request) clears it.
    TimelineEditRequest remove;
    remove.kind = TimelineEditKind::SetTransition;
    remove.clipIds = {uid(ClipB)};
    QVERIFY(!s.model.apply(remove, {}));
    QVERIFY(s.at(ClipB).transition.isNull());
    QVERIFY(!s.at(SoundB).transition.isNull());
}

void SequenceEditsTest::editsKeepTransitionsLegal()
{
    Session s;
    QVERIFY(s.loadStandard());
    QVERIFY(!s.transition(ClipB, QStringLiteral("dissolve"), 10 * F, QStringLiteral("center")));
    // Slipping B so it keeps only 2F of head handle would break the 5F lead-in.
    OperationError error = s.edit(TimelineEditKind::Slip, {ClipB}, -18 * F, false);
    QCOMPARE(error.code, ErrorCode::Validation);
    QCOMPARE(error.fieldPath, QStringLiteral("handles"));
    QCOMPARE(s.at(ClipB).sourceOffset, 20 * F);
    // A roll moves the transition with the cut while B keeps 5F of head handle.
    QCOMPARE(s.edit(TimelineEditKind::Roll, {ClipA, ClipB}, -16 * F, false).code, ErrorCode::Validation);
    QVERIFY(!s.edit(TimelineEditKind::Roll, {ClipA, ClipB}, -15 * F, false));
    QCOMPARE(s.at(ClipB).start, 35 * F);
    QCOMPARE(s.at(ClipB).transition.beforeCut, 5 * F);
    // Splitting B inside the window leaves too little of B after the cut.
    TimelineEditRequest split;
    split.kind = TimelineEditKind::Split;
    split.clipIds = {uid(ClipB)};
    TimelineEditInput input;
    input.pointerTicks = 37 * F;
    input.snap = false;
    QCOMPARE(s.model.apply(split, input).code, ErrorCode::Validation);
    // Deleting A removes the cut, so the same change set drops the transition.
    QVERIFY(!s.edit(TimelineEditKind::Delete, {ClipA}, 0, false));
    QVERIFY(s.at(ClipB).transition.isNull());
    QVERIFY(s.model.undo());
    QVERIFY(s.model.clipRecord(uid(ClipA)));
    QCOMPARE(s.at(ClipB).transition.beforeCut, 5 * F);
    // Moving the clips together keeps the cut and the transition.
    QVERIFY(!s.edit(TimelineEditKind::Move, {ClipA, ClipB, ClipC}, 2 * F, false));
    QCOMPARE(s.at(ClipB).start, 37 * F);
    QCOMPARE(s.at(ClipB).transition.afterCut, 5 * F);
}

void SequenceEditsTest::transitionsPersistAndParseFromMaps()
{
    Session s;
    QVERIFY(s.loadStandard());
    const QVariantMap result = s.model.applyEdit(
        {{QStringLiteral("kind"), QStringLiteral("setTransition")},
         {QStringLiteral("clipIds"), QStringList{uid(ClipB).toString(QUuid::WithoutBraces)}},
         {QStringLiteral("frameExact"), true},
         {QStringLiteral("transition"), QVariantMap{
              {QStringLiteral("type"), QStringLiteral("dip")},
              {QStringLiteral("afterCut"), QString::number(6 * F)},
              {QStringLiteral("alignment"), QStringLiteral("end")},
              {QStringLiteral("parameters"), QVariantMap{{QStringLiteral("color"), QStringLiteral("#ffffff")}}}}}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
    const QVariantMap data = s.model.clipData(uid(ClipB).toString(QUuid::WithoutBraces));
    const QVariantMap transition = data.value(QStringLiteral("transition")).toMap();
    QCOMPARE(transition.value(QStringLiteral("beforeCut")).toString(), QString::number(6 * F));
    QCOMPARE(transition.value(QStringLiteral("afterCut")).toString(), QStringLiteral("0"));
    QCOMPARE(transition.value(QStringLiteral("parameters")).toMap().value(QStringLiteral("color")).toString(),
             QStringLiteral("#ffffff"));
    QCOMPARE(data.value(QStringLiteral("linkId")).toString(), uid(LinkB).toString(QUuid::WithoutBraces));

    Session reopened;
    const QVariantMap loaded = reopened.model.loadJson(s.model.toJson());
    QVERIFY2(loaded.value(QStringLiteral("ok")).toBool(), qPrintable(loaded.value(QStringLiteral("error")).toString()));
    QCOMPARE(reopened.at(ClipB), s.at(ClipB));
    QCOMPARE(reopened.at(SoundA).linkId, uid(LinkA));
    // A number where ticks belong, or an unknown type, is refused.
    QString error;
    QVERIFY(!TimelineCodec::transitionFromMap({{QStringLiteral("type"), QStringLiteral("dissolve")},
                                               {QStringLiteral("afterCut"), 5}}, &error));
    QVERIFY(!TimelineCodec::transitionFromMap({{QStringLiteral("type"), QStringLiteral("spin")}}, &error));
}

void SequenceEditsTest::switchSourceCutsLinkedClipsInSync()
{
    Session s;
    const QString video = QStringLiteral("video"), audio = QStringLiteral("audio");
    QVERIFY(s.model.setContent({S, 1}, {track(V1, video), track(A1, audio)},
                               {clip(31, V1, video, 0, 100 * F, 10 * F, 200 * F, QStringLiteral("cam1"), 93),
                                clip(32, A1, audio, 0, 100 * F, 10 * F, 200 * F, QStringLiteral("cam1"), 93)}, {}));
    TimelineEditRequest request;
    request.kind = TimelineEditKind::SwitchSource;
    request.clipIds = {uid(31)};
    request.linked = true;
    request.frameExact = true;
    request.resourceId = QStringLiteral("cam2");
    request.sourceOffset = 37 * F;
    request.sourceDuration = 200 * F;
    TimelineEditInput input;
    input.pointerTicks = 30 * F + 2; // rounds to the frame start 30F
    input.snap = false;
    QString transaction;
    OperationError error = s.model.apply(request, input, &transaction);
    QVERIFY2(!error, qPrintable(error.message));
    QCOMPARE(s.at(31).duration, 30 * F);
    QCOMPARE(s.at(31).resourceId, QStringLiteral("cam1"));
    const TimelineClip *picture = s.model.clipRecord(QUuid::createUuidV5(QUuid(transaction), uid(31).toRfc4122()));
    const TimelineClip *sound = s.model.clipRecord(QUuid::createUuidV5(QUuid(transaction), uid(32).toRfc4122()));
    QVERIFY(picture && sound);
    QCOMPARE(picture->start, 30 * F);
    QCOMPARE(picture->duration, 70 * F);
    QCOMPARE(picture->resourceId, QStringLiteral("cam2"));
    QCOMPARE(picture->sourceOffset, 37 * F);
    QCOMPARE(sound->resourceId, QStringLiteral("cam2"));
    QCOMPARE(sound->sourceOffset, 37 * F);
    QCOMPARE(picture->linkId, sound->linkId);
    QVERIFY(picture->linkId != uid(93));
    const QUuid pictureId = picture->id, soundId = sound->id; // records move when the model changes

    // The new source must cover the whole switched part.
    request.clipIds = {pictureId};
    request.resourceId = QStringLiteral("cam3");
    request.sourceOffset = 50 * F;
    request.sourceDuration = 60 * F;
    input.pointerTicks = 60 * F;
    error = s.model.apply(request, input);
    QCOMPARE(error.code, ErrorCode::Validation);
    QCOMPARE(error.fieldPath, QStringLiteral("sourceOffset"));
    // Without link following, the sound keeps its source; at a clip's start
    // the clip switches whole.
    request.linked = false;
    request.sourceDuration = 200 * F;
    input.pointerTicks = 30 * F;
    QVERIFY(!s.model.apply(request, input));
    QCOMPARE(s.model.clipRecord(pictureId)->resourceId, QStringLiteral("cam3"));
    QCOMPARE(s.model.clipRecord(pictureId)->sourceOffset, 50 * F);
    QCOMPARE(s.model.clipRecord(soundId)->resourceId, QStringLiteral("cam2"));
    QCOMPARE(s.model.clipCount(), 4);
}

void SequenceEditsTest::offlineClipsKeepExactOffsets()
{
    Session s;
    const QString video = QStringLiteral("video");
    const qint64 far = 9007199254740993; // 2^53 + 1, not representable as a double
    QVERIFY(s.model.setContent({S, 1}, {track(V1, video)},
                               {clip(41, V1, video, 0, 10 * F, far, -1, QStringLiteral("offline:1")),
                                clip(42, V1, video, 10 * F, 10 * F, far, -1, QStringLiteral("offline:2"))}, {}));
    // Unknown source length: tail handles are unbounded, heads are the offsets.
    QVERIFY(!s.edit(TimelineEditKind::Slip, {41}, F));
    QCOMPARE(s.at(41).sourceOffset, qint64(9007199282964993));
    QVERIFY(!s.edit(TimelineEditKind::Roll, {41, 42}, 3 * F));
    QCOMPARE(s.at(41).duration, 13 * F);
    QCOMPARE(s.at(42).sourceOffset, far + 3 * F);
    QVERIFY(s.model.undo());
    QVERIFY(s.model.undo());
    QCOMPARE(s.at(41).sourceOffset, far);
    QCOMPARE(s.at(42).sourceOffset, far);
    QCOMPARE(s.model.clipData(uid(41).toString(QUuid::WithoutBraces)).value(QStringLiteral("sourceOffset")).toString(),
             QStringLiteral("9007199254740993"));
}

void SequenceEditsTest::undoRestoresEveryLinkedChange()
{
    Session s;
    QVERIFY(s.loadStandard());
    const QVariantMap original = s.model.contentMap();
    QVERIFY(!s.edit(TimelineEditKind::Roll, {ClipA, ClipB}, 3 * F));
    QVERIFY(!s.edit(TimelineEditKind::Slide, {ClipB}, 2 * F));
    QVERIFY(!s.transition(ClipB, QStringLiteral("dissolve"), 4 * F, QStringLiteral("center"), {}, true));
    const QVariantMap edited = s.model.contentMap();
    QCOMPARE(s.model.historyCount(), 3);
    while (s.model.canUndo()) QVERIFY(s.model.undo());
    QCOMPARE(s.model.contentMap(), original);
    while (s.model.canRedo()) QVERIFY(s.model.redo());
    QCOMPARE(s.model.contentMap(), edited);
    // A cancelled preview changes nothing.
    TimelineEditRequest request;
    request.kind = TimelineEditKind::Slip;
    request.clipIds = {uid(ClipB)};
    request.linked = true;
    const QString operation = s.model.begin(request);
    QVERIFY(!operation.isEmpty());
    TimelineEditInput input;
    input.pointerTicks = 2 * F;
    input.snap = false;
    QVERIFY(!s.model.update(operation, input).error);
    QCOMPARE(s.model.effectiveClip(uid(SoundB))->sourceOffset, s.at(SoundB).sourceOffset + 2 * F);
    s.model.cancel(operation);
    QCOMPARE(s.model.contentMap(), edited);
}

QTEST_MAIN(SequenceEditsTest)
#include "tst_sequence_edits.moc"
