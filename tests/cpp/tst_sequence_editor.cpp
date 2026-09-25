// SPDX-License-Identifier: LGPL-3.0-or-later
#include "multicam_model.h"
#include "sequence_editor.h"
#include "sequence_fixture.h"

#include <QtTest>

using namespace QindaTK;
using namespace SequenceFixture;

// WP-071 backends of TrimView, TransitionEditor and MulticamView
// (docs/sequence-edits.md): every request becomes a TimelineModel
// transaction, previews show the live overlay, and offline media is reported
// without blocking record-bounded edits.
namespace {
QString id(int n)
{
    return uid(n).toString(QUuid::WithoutBraces);
}

QString ticks(qint64 value)
{
    return QString::number(value);
}
} // namespace

class SequenceEditorTest : public QObject {
    Q_OBJECT
private slots:
    void trimPreviewCommitsOneStep();
    void trimSessionsCancelAndRetire();
    void trimPreviewsRequestTheAdjacentFrames();
    void transitionEditsAreTransactions();
    void multicamSwitchesDuringPlaybackAndReopens();
    void multicamRefusesIllegalSwitches();
    void multicamSeparateSoundKeepsItsOwnSync();
};

void SequenceEditorTest::trimPreviewCommitsOneStep()
{
    Session s;
    QVERIFY(s.loadStandard());
    SequenceEditor editor;
    editor.setTimeline(&s.model);
    QVERIFY(!editor.selectEditPoint(id(ClipA), id(ClipC))); // not adjacent
    QVERIFY(editor.selectCut(id(V1), ticks(52 * F)));        // the A|B cut is nearest
    QCOMPARE(editor.outgoingClipId(), id(ClipA));
    QCOMPARE(editor.incomingClipId(), id(ClipB));
    QVariantMap edit = editor.trimEdit();
    QCOMPARE(edit.value(QStringLiteral("cutTicks")).toString(), ticks(50 * F));
    QCOMPARE(edit.value(QStringLiteral("leftTailHandleTicks")).toString(), ticks(40 * F));
    QCOMPARE(edit.value(QStringLiteral("rightHeadHandleTicks")).toString(), ticks(20 * F));
    QCOMPARE(edit.value(QStringLiteral("rightHeadTicks")).toString(), ticks(20 * F));
    QCOMPARE(edit.value(QStringLiteral("leftTailTicks")).toString(), ticks(60 * F - 1));
    QCOMPARE(edit.value(QStringLiteral("rateNumerator")).toString(), QStringLiteral("25"));
    // No files exist for these resources: the view reports them offline.
    QCOMPARE(edit.value(QStringLiteral("missingLeft")).toBool(), true);

    const QVariantMap preview = editor.previewTrim(QStringLiteral("roll"), {}, ticks(5 * F + 7));
    QVERIFY2(preview.value(QStringLiteral("ok")).toBool(), qPrintable(preview.value(QStringLiteral("error")).toString()));
    QCOMPARE(editor.trimStatus(), QStringLiteral("ready"));
    QCOMPARE(editor.trimAppliedDelta(), ticks(5 * F));
    QVERIFY(s.model.editing());
    // The snapshot follows the preview; the committed record does not move yet.
    QCOMPARE(editor.trimEdit().value(QStringLiteral("cutTicks")).toString(), ticks(55 * F));
    QCOMPARE(editor.trimEdit().value(QStringLiteral("previewing")).toBool(), true);
    QCOMPARE(s.at(ClipA).duration, 50 * F);
    // Updating the same session replaces the preview.
    QVERIFY(editor.previewTrim(QStringLiteral("roll"), {}, ticks(3 * F)).value(QStringLiteral("ok")).toBool());
    QCOMPARE(editor.trimEdit().value(QStringLiteral("cutTicks")).toString(), ticks(53 * F));
    const QVariantMap committed = editor.commitTrim();
    QVERIFY(committed.value(QStringLiteral("ok")).toBool());
    QCOMPARE(editor.trimStatus(), QStringLiteral("committed"));
    QVERIFY(editor.trimOperationId().isEmpty());
    QCOMPARE(s.at(ClipA).duration, 53 * F);
    QCOMPARE(s.at(SoundB).start, 53 * F); // linked by default
    QCOMPARE(s.model.historyCount(), 1);

    // Ripple of the outgoing clip's end closes up everything after it.
    QVERIFY(editor.previewTrim(QStringLiteral("ripple"), QStringLiteral("left"), ticks(-3 * F))
                .value(QStringLiteral("ok")).toBool());
    QVERIFY(editor.commitTrim().value(QStringLiteral("ok")).toBool());
    QCOMPARE(s.at(ClipA).duration, 50 * F);
    QCOMPARE(s.at(ClipB).start, 50 * F);
    QCOMPARE(s.at(ClipC).start, 97 * F);
    QCOMPARE(s.model.historyCount(), 2);
    QVERIFY(!editor.commitTrim().value(QStringLiteral("ok")).toBool()); // nothing left to commit
    QVERIFY(!editor.previewTrim(QStringLiteral("roll"), {}, QStringLiteral("5.5")).value(QStringLiteral("ok")).toBool());
    QVERIFY(!editor.previewTrim(QStringLiteral("spin"), {}, ticks(F)).value(QStringLiteral("ok")).toBool());
}

void SequenceEditorTest::trimSessionsCancelAndRetire()
{
    Session s;
    QVERIFY(s.loadStandard());
    SequenceEditor editor;
    editor.setTimeline(&s.model);
    QVERIFY(editor.selectEditPoint(id(ClipA), id(ClipB)));
    const QVariantMap before = s.model.contentMap();
    QVERIFY(editor.previewTrim(QStringLiteral("slip"), QStringLiteral("right"), ticks(7 * F))
                .value(QStringLiteral("ok")).toBool());
    QCOMPARE(s.model.effectiveClip(uid(ClipB))->sourceOffset, 27 * F);
    const QString slipOperation = editor.trimOperationId();
    // A different mode restarts the session.
    QVERIFY(editor.previewTrim(QStringLiteral("slide"), QStringLiteral("right"), ticks(5 * F))
                .value(QStringLiteral("ok")).toBool());
    QVERIFY(editor.trimOperationId() != slipOperation);
    QCOMPARE(s.model.effectiveClip(uid(ClipB))->start, 55 * F);
    QCOMPARE(s.model.effectiveClip(uid(ClipB))->sourceOffset, 20 * F);
    QVERIFY(editor.cancelTrim());
    QCOMPARE(editor.trimStatus(), QStringLiteral("idle"));
    QVERIFY(!s.model.editing());
    QCOMPARE(s.model.contentMap(), before);
    QCOMPARE(s.model.historyCount(), 0);
    // A slip that would break nothing but exceeds the handle is clamped, not refused.
    QVERIFY(editor.previewTrim(QStringLiteral("slip"), QStringLiteral("left"), ticks(1000 * F))
                .value(QStringLiteral("ok")).toBool());
    QCOMPARE(editor.trimAppliedDelta(), ticks(40 * F));
    // Reloading the timeline retires the preview.
    QVERIFY(s.model.loadContent(before).value(QStringLiteral("ok")).toBool());
    QCOMPARE(editor.trimStatus(), QStringLiteral("rejected"));
    QVERIFY(editor.trimOperationId().isEmpty());
    // A locked partner refuses a linked edit.
    TimelineEditRequest lock;
    lock.kind = TimelineEditKind::TrackProperty;
    lock.trackId = uid(A1);
    lock.property = QStringLiteral("locked");
    lock.value = true;
    QVERIFY(!s.model.apply(lock, {}));
    QVERIFY(editor.selectEditPoint(id(ClipA), id(ClipB)));
    const QVariantMap refused = editor.previewTrim(QStringLiteral("roll"), {}, ticks(F));
    QVERIFY(!refused.value(QStringLiteral("ok")).toBool());
    QCOMPARE(editor.trimStatus(), QStringLiteral("rejected"));
    QVERIFY(!editor.trimError().isEmpty());
    editor.setLinked(false);
    QVERIFY(editor.previewTrim(QStringLiteral("roll"), {}, ticks(F)).value(QStringLiteral("ok")).toBool());
    QVERIFY(editor.commitTrim().value(QStringLiteral("ok")).toBool());
    QCOMPARE(s.at(ClipA).duration, 51 * F);
    QCOMPARE(s.at(SoundA).duration, 50 * F);
}

// VID-03: the two filmstrips show the outgoing clip's last frame and the
// incoming clip's first frame of the would-be edit (source ticks; the last
// tick of the outgoing part lies in its last frame).
void SequenceEditorTest::trimPreviewsRequestTheAdjacentFrames()
{
    Session s;
    QVERIFY(s.loadStandard());
    SequenceEditor editor;
    editor.setTimeline(&s.model);
    QVERIFY(editor.selectEditPoint(id(ClipA), id(ClipB)));
    const auto frames = [&editor](const char *key) { return editor.trimEdit().value(QLatin1String(key)).toString(); };
    struct Case {
        const char *mode;
        const char *side;
        qint64 delta;
        qint64 leftHead, leftTail, rightHead, rightTail; // expected, in frames (tails exclusive)
    };
    // A: src 10F, 50F long. B: src 20F, 50F long, 30F of tail handle.
    const Case cases[] = {
        {"roll", "", 5 * F, 10, 65, 25, 70},  // the cut moves: A shows 5 more frames, B starts 5 later
        {"roll", "", -4 * F, 10, 56, 16, 70},
        {"slip", "right", 3 * F, 10, 60, 23, 73}, // B's content moves under a fixed cut
        {"slip", "left", -6 * F, 4, 54, 20, 70},
        {"slide", "right", 5 * F, 10, 65, 20, 70}, // B moves, A's tail follows, B's frames stay
        {"ripple", "left", -3 * F, 10, 57, 20, 70},
    };
    for (const Case &c : cases) {
        const QVariantMap preview = editor.previewTrim(QLatin1String(c.mode), QLatin1String(c.side), ticks(c.delta));
        QVERIFY2(preview.value(QStringLiteral("ok")).toBool(), qPrintable(preview.value(QStringLiteral("error")).toString()));
        QCOMPARE(frames("leftHeadTicks"), ticks(c.leftHead * F));
        QCOMPARE(frames("leftTailTicks"), ticks(c.leftTail * F - 1));
        QCOMPARE(frames("rightHeadTicks"), ticks(c.rightHead * F));
        QCOMPARE(frames("rightTailTicks"), ticks(c.rightTail * F - 1));
        QVERIFY(editor.cancelTrim());
        // Cancelling shows the committed frames again.
        QCOMPARE(frames("leftTailTicks"), ticks(60 * F - 1));
        QCOMPARE(frames("rightHeadTicks"), ticks(20 * F));
    }
}

void SequenceEditorTest::transitionEditsAreTransactions()
{
    Session s;
    QVERIFY(s.loadStandard());
    SequenceEditor editor;
    editor.setTimeline(&s.model);
    editor.setLinked(false);
    editor.setTransitionClipId(id(ClipB));
    QVariantMap t = editor.transition();
    QCOMPARE(t.value(QStringLiteral("leftClipId")).toString(), id(ClipA));
    QCOMPARE(t.value(QStringLiteral("hasTransition")).toBool(), false);
    QCOMPARE(t.value(QStringLiteral("headHandleTicks")).toString(), ticks(20 * F));
    QCOMPARE(t.value(QStringLiteral("tailHandleTicks")).toString(), ticks(40 * F));
    // The first edit creates a one-second (25-frame) centred dissolve.
    QVERIFY(editor.editTransition(QStringLiteral("type"), QStringLiteral("dissolve")).value(QStringLiteral("ok")).toBool());
    t = editor.transition();
    QCOMPARE(t.value(QStringLiteral("hasTransition")).toBool(), true);
    QCOMPARE(t.value(QStringLiteral("durationTicks")).toString(), ticks(25 * F));
    QCOMPARE(t.value(QStringLiteral("beforeCutTicks")).toString(), ticks(13 * F));
    QCOMPARE(t.value(QStringLiteral("afterCutTicks")).toString(), ticks(12 * F));
    QCOMPARE(t.value(QStringLiteral("windowStartTicks")).toString(), ticks(37 * F));
    QVERIFY(editor.editTransition(QStringLiteral("parameter:curve"), QStringLiteral("equalPower"))
                .value(QStringLiteral("ok")).toBool());
    QCOMPARE(s.at(ClipB).transition.parameters.value(QStringLiteral("curve")).toString(), QStringLiteral("equalPower"));
    QVERIFY(editor.editTransition(QStringLiteral("alignment"), QStringLiteral("start")).value(QStringLiteral("ok")).toBool());
    QCOMPARE(s.at(ClipB).transition.beforeCut, 0);
    QCOMPARE(s.at(ClipB).transition.afterCut, 25 * F);
    QCOMPARE(editor.transitionWindow(id(ClipB)).value(QStringLiteral("endTicks")).toString(), ticks(75 * F));
    // Wipe parameters appear once the type is a wipe.
    QVERIFY(editor.editTransition(QStringLiteral("type"), QStringLiteral("wipe")).value(QStringLiteral("ok")).toBool());
    const QVariantList parameters = editor.transition().value(QStringLiteral("parameters")).toList();
    QCOMPARE(parameters.first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("direction"));
    // 60 frames after the cut exceed A's 40-frame tail handle: refused, unchanged.
    const QVariantMap tooLong = editor.editTransition(QStringLiteral("durationTicks"), ticks(60 * F));
    QVERIFY(!tooLong.value(QStringLiteral("ok")).toBool());
    QVERIFY(!editor.transitionError().isEmpty());
    QCOMPARE(s.at(ClipB).transition.afterCut, 25 * F);
    QVERIFY(!editor.editTransition(QStringLiteral("bogus"), QStringLiteral("1")).value(QStringLiteral("ok")).toBool());
    QVERIFY(!editor.previewTransition().value(QStringLiteral("ok")).toBool()); // no player bound
    QVERIFY(editor.removeTransition(id(ClipB)).value(QStringLiteral("ok")).toBool());
    QCOMPARE(editor.transition().value(QStringLiteral("hasTransition")).toBool(), false);
    QCOMPARE(s.model.historyCount(), 5);
    QVERIFY(s.model.undo());
    QCOMPARE(editor.transition().value(QStringLiteral("type")).toString(), QStringLiteral("wipe"));
    // Links through the editor.
    QVERIFY(editor.unlinkClips({id(ClipC)}).value(QStringLiteral("ok")).toBool());
    QVERIFY(editor.linkClips({id(ClipC), id(SoundB)}).value(QStringLiteral("ok")).toBool());
    QCOMPARE(s.at(ClipC).linkId, s.at(SoundB).linkId);
    QVERIFY(s.at(ClipB).linkId != s.at(SoundB).linkId);
}

void SequenceEditorTest::multicamSwitchesDuringPlaybackAndReopens()
{
    Session s;
    QVERIFY(s.model.setContent({S, 1}, {track(V1, QStringLiteral("video")), track(A1, QStringLiteral("audio"))},
                               {}, {}));
    MulticamModel multicam;
    multicam.setTimeline(&s.model);
    multicam.setTrackId(id(V1));
    multicam.setAudioTrackId(id(A1));
    QVERIFY(multicam.addAngle({{QStringLiteral("angleId"), QStringLiteral("wide")},
                               {QStringLiteral("resourceId"), QStringLiteral("cam1")},
                               {QStringLiteral("sourceDurationTicks"), ticks(200 * F)}}));
    QVERIFY(multicam.addAngle({{QStringLiteral("angleId"), QStringLiteral("close")},
                               {QStringLiteral("resourceId"), QStringLiteral("cam2")},
                               {QStringLiteral("syncOffsetTicks"), ticks(10 * F)},
                               {QStringLiteral("sourceDurationTicks"), ticks(200 * F)}}));
    QVERIFY(!multicam.addAngle({{QStringLiteral("angleId"), QStringLiteral("wide")},
                                {QStringLiteral("resourceId"), QStringLiteral("cam9")}})); // duplicate ID
    const QVariantMap inserted = multicam.insertProgram(QStringLiteral("wide"), QStringLiteral("0"), ticks(100 * F));
    QVERIFY2(inserted.value(QStringLiteral("ok")).toBool(), qPrintable(inserted.value(QStringLiteral("error")).toString()));
    QCOMPARE(inserted.value(QStringLiteral("clipIds")).toStringList().size(), 2);
    // Mock playback: a switch at frame 20 (sound follows), back at 45, and at
    // 70 with the sound left alone.
    for (qint64 frame = 0; frame < 100; ++frame) {
        multicam.setPlayheadTicks(ticks(frame * F + 5)); // mid-frame times cut at that frame's start
        if (frame == 20) QVERIFY(multicam.switchAngle(QStringLiteral("close")).value(QStringLiteral("ok")).toBool());
        if (frame == 45) QVERIFY(multicam.switchAngle(QStringLiteral("wide")).value(QStringLiteral("ok")).toBool());
        if (frame == 70) {
            multicam.setAudioFollowsVideo(false);
            QVERIFY(multicam.switchAngle(QStringLiteral("close")).value(QStringLiteral("ok")).toBool());
        }
    }
    QCOMPARE(multicam.angleAt(ticks(20 * F - 1)), QStringLiteral("wide"));
    QCOMPARE(multicam.angleAt(ticks(20 * F)), QStringLiteral("close"));
    QCOMPARE(multicam.angleAt(ticks(45 * F)), QStringLiteral("wide"));
    QCOMPARE(multicam.angleAt(ticks(70 * F)), QStringLiteral("close"));
    QCOMPARE(multicam.sourceTicksAt(QStringLiteral("close"), ticks(20 * F)), ticks(30 * F));
    const QVector<QUuid> picture = s.model.clipsInRange(uid(V1), 20 * F, 20 * F + 1);
    QCOMPARE(s.model.clipRecord(picture.first())->sourceOffset, 30 * F);
    const QVector<QUuid> soundAt20 = s.model.clipsInRange(uid(A1), 20 * F, 20 * F + 1);
    QCOMPARE(s.model.clipRecord(soundAt20.first())->resourceId, QStringLiteral("cam2"));
    QCOMPARE(s.model.clipRecord(soundAt20.first())->sourceOffset, 30 * F);
    const QVector<QUuid> soundAt70 = s.model.clipsInRange(uid(A1), 70 * F, 70 * F + 1);
    QCOMPARE(s.model.clipRecord(soundAt70.first())->resourceId, QStringLiteral("cam1"));
    QCOMPARE(s.model.clipCount(), 7); // four picture parts, three sound parts
    QVERIFY(multicam.unsyncedClipIds().isEmpty());
    // The view's snapshot: the program angle at the playhead and each angle's source time.
    QCOMPARE(multicam.activeAngleId(), QStringLiteral("close"));
    const QVariantList angles = multicam.angles();
    QCOMPARE(angles.at(1).toMap().value(QStringLiteral("active")).toBool(), true);
    QCOMPARE(angles.at(1).toMap().value(QStringLiteral("frameTimeTicks")).toString(), ticks(109 * F + 5));
    QCOMPARE(angles.at(0).toMap().value(QStringLiteral("available")).toBool(), false); // no file for cam1

    // Reopen from the saved timeline and angle table.
    Session reopened;
    QVERIFY(reopened.model.loadJson(s.model.toJson()).value(QStringLiteral("ok")).toBool());
    MulticamModel restored;
    QVERIFY(restored.loadContent(multicam.contentMap()));
    restored.setTimeline(&reopened.model);
    for (const qint64 frame : {0, 19, 20, 44, 45, 69, 70, 99})
        QCOMPARE(restored.angleAt(ticks(frame * F)), multicam.angleAt(ticks(frame * F)));
    QVERIFY(restored.unsyncedClipIds().isEmpty());
    QCOMPARE(restored.audioFollowsVideo(), false);
    // Undo takes back the last cut only.
    QVERIFY(s.model.undo());
    QCOMPARE(multicam.angleAt(ticks(70 * F)), QStringLiteral("wide"));
    QCOMPARE(multicam.angleAt(ticks(20 * F)), QStringLiteral("close"));
}

void SequenceEditorTest::multicamRefusesIllegalSwitches()
{
    Session s;
    QVERIFY(s.model.setContent({S, 1}, {track(V1, QStringLiteral("video"))}, {}, {}));
    MulticamModel multicam;
    multicam.setTimeline(&s.model);
    multicam.setTrackId(id(V1));
    QVERIFY(multicam.addAngle({{QStringLiteral("angleId"), QStringLiteral("a")},
                               {QStringLiteral("resourceId"), QStringLiteral("cam1")},
                               {QStringLiteral("sourceDurationTicks"), ticks(100 * F)}}));
    // cam2 started 30 frames after the sync point: nothing to show before then.
    QVERIFY(multicam.addAngle({{QStringLiteral("angleId"), QStringLiteral("late")},
                               {QStringLiteral("resourceId"), QStringLiteral("cam2")},
                               {QStringLiteral("syncOffsetTicks"), ticks(-30 * F)},
                               {QStringLiteral("sourceDurationTicks"), ticks(100 * F)}}));
    QVERIFY(!multicam.switchAngle(QStringLiteral("late"), ticks(10 * F)).value(QStringLiteral("ok")).toBool());
    QVERIFY(multicam.lastError().contains(QStringLiteral("program clip"))); // a gap
    QVERIFY(multicam.insertProgram(QStringLiteral("a"), QStringLiteral("0"), ticks(50 * F)).value(QStringLiteral("ok")).toBool());
    const QVariantMap early = multicam.switchAngle(QStringLiteral("late"), ticks(10 * F));
    QVERIFY(!early.value(QStringLiteral("ok")).toBool());
    QCOMPARE(s.model.clipCount(), 1);
    QVERIFY(!multicam.switchAngle(QStringLiteral("a"), ticks(10 * F)).value(QStringLiteral("ok")).toBool()); // already on program
    QVERIFY(multicam.switchAngle(QStringLiteral("late"), ticks(35 * F)).value(QStringLiteral("ok")).toBool());
    QCOMPARE(multicam.sourceTicksAt(QStringLiteral("late"), ticks(35 * F)), ticks(5 * F));
    QVERIFY(!multicam.loadContent({{QStringLiteral("schema"), QStringLiteral("other")}}));
    QCOMPARE(multicam.angleCount(), 2);
}

// An angle recorded with separate sound: the sound clip is placed and cut
// from the recorder's own sync offset and bounded by its own length.
void SequenceEditorTest::multicamSeparateSoundKeepsItsOwnSync()
{
    Session s;
    QVERIFY(s.model.setContent({S, 1}, {track(V1, QStringLiteral("video")), track(A1, QStringLiteral("audio"))},
                               {}, {}));
    MulticamModel multicam;
    multicam.setTimeline(&s.model);
    multicam.setTrackId(id(V1));
    multicam.setAudioTrackId(id(A1));
    QVERIFY(multicam.addAngle({{QStringLiteral("angleId"), QStringLiteral("wide")},
                               {QStringLiteral("resourceId"), QStringLiteral("cam1")},
                               {QStringLiteral("audioResourceId"), QStringLiteral("rec1")},
                               {QStringLiteral("syncOffsetTicks"), ticks(10 * F)},
                               {QStringLiteral("sourceDurationTicks"), ticks(200 * F)},
                               {QStringLiteral("audioSyncOffsetTicks"), ticks(57 * F + 11)},
                               {QStringLiteral("audioSourceDurationTicks"), ticks(300 * F)}}));
    QVERIFY(multicam.addAngle({{QStringLiteral("angleId"), QStringLiteral("close")},
                               {QStringLiteral("resourceId"), QStringLiteral("cam2")},
                               {QStringLiteral("audioResourceId"), QStringLiteral("rec2")},
                               {QStringLiteral("syncOffsetTicks"), ticks(30 * F)},
                               {QStringLiteral("sourceDurationTicks"), ticks(200 * F)},
                               {QStringLiteral("audioSyncOffsetTicks"), ticks(-20 * F)},
                               {QStringLiteral("audioSourceDurationTicks"), ticks(80 * F)}}));
    QVERIFY(multicam.insertProgram(QStringLiteral("wide"), QStringLiteral("0"), ticks(100 * F)).value(QStringLiteral("ok")).toBool());
    const auto soundAt = [&s](qint64 at) { return *s.model.clipRecord(s.model.clipsInRange(uid(A1), at, at + 1).first()); };
    const auto pictureAt = [&s](qint64 at) { return *s.model.clipRecord(s.model.clipsInRange(uid(V1), at, at + 1).first()); };
    QCOMPARE(pictureAt(0).sourceOffset, 10 * F);
    QCOMPARE(soundAt(0).resourceId, QStringLiteral("rec1"));
    QCOMPARE(soundAt(0).sourceOffset, 57 * F + 11);
    QCOMPARE(soundAt(0).sourceDuration, 300 * F);
    // A cut at 40F: cam2 from 70F, and rec2 (started 20F after sync) from 20F.
    QVERIFY(multicam.switchAngle(QStringLiteral("close"), ticks(40 * F)).value(QStringLiteral("ok")).toBool());
    QCOMPARE(pictureAt(40 * F).sourceOffset, 70 * F);
    QCOMPARE(soundAt(40 * F).resourceId, QStringLiteral("rec2"));
    QCOMPARE(soundAt(40 * F).sourceOffset, 20 * F);
    QCOMPARE(soundAt(40 * F).sourceDuration, 80 * F);
    QVERIFY(multicam.unsyncedClipIds().isEmpty());
    QVERIFY(s.model.undo());
    // rec2 has nothing before 20F: a cut at 10F is refused although cam2 has picture there.
    QVERIFY(!multicam.switchAngle(QStringLiteral("close"), ticks(10 * F)).value(QStringLiteral("ok")).toBool());
    // A recorder 1F too short for the part after 40F is refused too.
    QVERIFY(multicam.removeAngle(QStringLiteral("close")));
    QVERIFY(multicam.addAngle({{QStringLiteral("angleId"), QStringLiteral("close")},
                               {QStringLiteral("resourceId"), QStringLiteral("cam2")},
                               {QStringLiteral("audioResourceId"), QStringLiteral("rec2")},
                               {QStringLiteral("syncOffsetTicks"), ticks(30 * F)},
                               {QStringLiteral("sourceDurationTicks"), ticks(200 * F)},
                               {QStringLiteral("audioSyncOffsetTicks"), ticks(-20 * F)},
                               {QStringLiteral("audioSourceDurationTicks"), ticks(79 * F)}}));
    QVERIFY(!multicam.switchAngle(QStringLiteral("close"), ticks(40 * F)).value(QStringLiteral("ok")).toBool());
    QCOMPARE(s.model.clipCount(), 2);
    // Slipping only the sound is drift, and unsyncedClipIds reports it.
    const QString sound = soundAt(0).id.toString(QUuid::WithoutBraces);
    TimelineEditRequest slip;
    slip.kind = TimelineEditKind::Slip;
    slip.clipIds = {soundAt(0).id};
    TimelineEditInput input;
    input.pointerTicks = F;
    input.snap = false;
    QVERIFY(!s.model.apply(slip, input));
    QCOMPARE(multicam.unsyncedClipIds(), QStringList{sound});
    // The audio sync fields survive the saved angle table.
    MulticamModel restored;
    QVERIFY(restored.loadContent(multicam.contentMap()));
    QCOMPARE(restored.angleRecords().at(0).audioSyncOffset, 57 * F + 11);
    QCOMPARE(restored.angleRecords().at(1).audioSourceDuration, 79 * F);
    QCOMPARE(multicam.angles().at(1).toMap().value(QStringLiteral("audioSyncOffsetTicks")).toString(), ticks(-20 * F));
}

QTEST_MAIN(SequenceEditorTest)
#include "tst_sequence_editor.moc"
