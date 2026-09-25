// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-065 audit gaps: splits and merges next to a long crossfade are silent,
// audition and comparison play only their range and never reach an export,
// and a recording past either end of a comp grows it so all of it can be
// selected, with timeline undo returning the old comp intact.
#include "audio_session.h"
#include "audio_take_comp_fixture.h"

#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>

using namespace QindaTK;
using namespace QindaTK::CompFixture;

namespace {

bool ok(const QVariantMap &result)
{
    return result.value(QStringLiteral("ok")).toBool();
}

struct Loop {
    QTemporaryDir dir;
    AudioSession session;
    AudioTakeCompModel *comps = session.comps();
    QString track, compId, clipId, t1, t2, t3;
    bool setUp()
    {
        track = session.addTrack(QStringLiteral("Vocal"));
        if (track.isEmpty() || !session.setTrackArmed(track, true)) return false;
        const QVariantMap placed = session.placeTake(loopTake(writeMono(dir.filePath(QStringLiteral("take.wav")), 3150, ramp)));
        compId = placed.value(QStringLiteral("compId")).toString();
        clipId = placed.value(QStringLiteral("clipId")).toString();
        t1 = takeId(comps, compId, QStringLiteral("Take 1"));
        t2 = takeId(comps, compId, QStringLiteral("Take 2"));
        t3 = takeId(comps, compId, QStringLiteral("Take 3"));
        return ok(placed) && !t3.isEmpty();
    }
    std::vector<float> out(const char *name, qint64 end = 3600)
    {
        return render(session, dir.filePath(QString::fromLatin1(name)), end);
    }
    const TimelineClip *clip() const { return session.timeline()->clipRecord(QUuid::fromString(clipId)); }
};

QString junction(const AudioTakeCompModel *comps, const QString &compId, const QString &takeId)
{
    for (const QVariant &value : comps->pieces(compId)) {
        const QVariantMap piece = value.toMap();
        if (piece.value(QStringLiteral("takeId")).toString() == takeId)
            return piece.value(QStringLiteral("fadeInStartSample")).toString() + QLatin1Char('-')
                + piece.value(QStringLiteral("fadeInEndSample")).toString();
    }
    return {};
}

} // namespace

class TestAudioTakeCompEdges : public QObject {
    Q_OBJECT
private slots:
    // Audit finding 2: the half-segment bound used each segment's own length,
    // so a split beside a 240-sample fade moved the fade.
    void splitsAndMergesBesideALongFadeAreSilent()
    {
        Loop s;
        QVERIFY(s.setUp());
        QVERIFY(ok(s.comps->select(s.compId, s.t2, QStringLiteral("1100"), QStringLiteral("1200"))));
        QCOMPARE(layout(s.comps, s.compId), QStringLiteral("Take 1:0-200|Take 3:200-1100|Take 2:1100-1200"));
        QCOMPARE(junction(s.comps, s.compId, s.t2), QStringLiteral("910-1150"));
        const std::vector<float> whole = s.out("whole.wav");
        QCOMPARE(qint64(whole.size()), qint64(3600));

        // Split both sides of the junction close to it: the runs are unchanged.
        QVERIFY(ok(s.comps->split(s.compId, QStringLiteral("1120"))));
        QVERIFY(ok(s.comps->split(s.compId, QStringLiteral("1090"))));
        QCOMPARE(layout(s.comps, s.compId),
                 QStringLiteral("Take 1:0-200|Take 3:200-1090|Take 3:1090-1100|Take 2:1100-1120|Take 2:1120-1200"));
        const QVariantList pieces = s.comps->pieces(s.compId);
        QCOMPARE(pieces.size(), 5);
        for (int i : {1, 2}) QCOMPARE(pieces.at(i).toMap().value(QStringLiteral("fadeOutStartSample")).toString(), QStringLiteral("910"));
        for (int i : {3, 4}) QCOMPARE(pieces.at(i).toMap().value(QStringLiteral("fadeInEndSample")).toString(), QStringLiteral("1150"));
        QCOMPARE(s.out("split.wav"), whole);
        // The view draws each part of the fade inside its own segment.
        for (const QVariant &value : s.comps->viewState(s.compId, 12, 0).value(QStringLiteral("compSegments")).toList()) {
            const QVariantMap segment = value.toMap();
            const qint64 start = segment.value(QStringLiteral("startSample")).toString().toLongLong();
            const qint64 end = segment.value(QStringLiteral("endSample")).toString().toLongLong();
            for (const char *key : {"fadeInEndSample", "fadeOutStartSample"}) {
                if (!segment.contains(QString::fromLatin1(key))) continue;
                const qint64 edge = segment.value(QString::fromLatin1(key)).toString().toLongLong();
                QVERIFY2(edge >= start && edge <= end, key);
            }
        }

        // Merging back is silent too, and so is a split far from any junction.
        QVERIFY(ok(s.comps->merge(s.compId, segmentIdAt(s.comps, s.compId, 3))));
        QVERIFY(ok(s.comps->merge(s.compId, segmentIdAt(s.comps, s.compId, 1))));
        QCOMPARE(layout(s.comps, s.compId), QStringLiteral("Take 1:0-200|Take 3:200-1100|Take 2:1100-1200"));
        QCOMPARE(s.out("merged.wav"), whole);
        QVERIFY(ok(s.comps->split(s.compId, QStringLiteral("210"))));
        QCOMPARE(s.out("near-start.wav"), whole);
    }

    // Audit finding 4: audition and comparison play their range only, can be
    // ended, and an export renders the committed comp while they (or a
    // preview) are active.
    void auditionPlaysItsRangeAndExportsPlayTheComp()
    {
        Loop s;
        QVERIFY(s.setUp());
        const std::vector<float> committed = s.out("committed.wav");
        const std::vector<float> live = device(s.session, 3600);
        QCOMPARE(live, committed); // nothing auditioned: the device plays the comp

        QVERIFY(s.comps->setAudition(s.compId, s.t2, QStringLiteral("1000"), QStringLiteral("1100")));
        QCOMPARE(s.comps->audition(s.compId), s.t2);
        const QVariantMap range = s.comps->auditionRange(s.compId);
        QCOMPARE(range.value(QStringLiteral("startSample")).toString(), QStringLiteral("1000"));
        QCOMPARE(range.value(QStringLiteral("endSample")).toString(), QStringLiteral("1100"));
        std::vector<float> data = device(s.session, 3600);
        for (qint64 t = 0; t < 3600; ++t) {
            const float want = t >= 2800 && t < 2900 ? ramp(1000 + t - 1800) : 0.0f;
            if (data[size_t(t)] != want) QFAIL(qPrintable(QStringLiteral("audition sample %1").arg(t)));
        }
        QCOMPARE(s.out("export-audition.wav"), committed);

        QVERIFY(s.comps->compare(s.compId, s.t3, s.t1, QStringLiteral("300"), QStringLiteral("400")));
        data = device(s.session, 3600);
        QCOMPARE(data[2099], 0.0f);
        QCOMPARE(data[2100], ramp(2000 + 300));
        QCOMPARE(data[2199], ramp(2000 + 399));
        QCOMPARE(data[2200], 0.0f);
        QCOMPARE(s.comps->toggleComparison(s.compId), s.t1);
        data = device(s.session, 3600);
        QCOMPARE(data[2100], ramp(300));
        QCOMPARE(data[2200], 0.0f);
        QCOMPARE(s.out("export-compare.wav"), committed);

        QVERIFY(!s.comps->setAudition(s.compId, s.t2, QStringLiteral("500"), QStringLiteral("400")));
        QVERIFY(!s.comps->setAudition(s.compId, s.t2, QStringLiteral("0"), QStringLiteral("1201")));
        QVERIFY(s.comps->setAudition(s.compId, QString()));
        QVERIFY(s.comps->auditionRange(s.compId).isEmpty());
        QCOMPARE(device(s.session, 3600), committed);

        // A swipe in progress is heard on the device, never in an export.
        QVERIFY(ok(s.comps->preview(s.compId, s.t1, QStringLiteral("0"), QStringLiteral("1200"))));
        QCOMPARE(device(s.session, 3600)[2500], ramp(700));
        QCOMPARE(s.out("export-preview.wav"), committed);
        QVERIFY(s.comps->cancelPreview());
    }

    // Audit finding 3: a punch that runs past the comp end grows the comp; the
    // new audio is selected past the old end, a trim reaches it, and timeline
    // undo returns the clip to the old comp and its samples.
    void recordingPastTheCompEndGrowsIt()
    {
        Loop s;
        QVERIFY(s.setUp());
        QVERIFY(ok(s.comps->setFade(s.compId, QStringLiteral("0"), QStringLiteral("linear"))));
        const std::vector<float> before = s.out("before.wav");
        const QVariantMap placed = s.session.placeTake(
            linearTake(writeMono(s.dir.filePath(QStringLiteral("late.wav")), 1000, down), 2500, 1000, 2600, 3400));
        QVERIFY2(ok(placed), qPrintable(s.session.lastError()));
        QCOMPARE(placed.value(QStringLiteral("clipId")).toString(), s.clipId);
        const QString grown = placed.value(QStringLiteral("compId")).toString();
        QVERIFY(grown != s.compId);
        QCOMPARE(s.clip()->resourceId, grown);
        QCOMPARE(s.clip()->sourceOffset, qint64(0));
        QCOMPARE(s.clip()->sourceDuration, ticks(1700));
        QCOMPARE(s.comps->compData(grown).value(QStringLiteral("length")).toString(), QStringLiteral("1700"));
        QCOMPARE(layout(s.comps, grown), QStringLiteral("Take 1:0-200|Take 3:200-800|Take 4:800-1600"));
        // The old comp is untouched (timeline undo needs it).
        QCOMPARE(layout(s.comps, s.compId), QStringLiteral("Take 1:0-200|Take 3:200-1150|Take 2:1150-1200"));

        // Inside the clip window the punch plays; the clip has not moved.
        const std::vector<float> grownOut = s.out("grown.wav");
        std::vector<float> data = grownOut;
        QCOMPARE(data[2599], before[2599]);
        QCOMPARE(data[2600], down(100));
        QCOMPARE(data[2999], down(499));
        QCOMPARE(data[3000], 0.0f);
        // Trimming the clip end out reaches the audio past the old comp end.
        TimelineEditRequest trim;
        trim.kind = TimelineEditKind::TrimEnd;
        trim.clipIds = {QUuid::fromString(s.clipId)};
        TimelineEditInput input;
        input.pointerTicks = ticks(500);
        input.snap = false;
        QVERIFY(!s.session.timeline()->apply(trim, input));
        QCOMPARE(s.clip()->end(), ticks(3500));
        data = s.out("trimmed.wav");
        QCOMPARE(data[3000], down(500));
        QCOMPARE(data[3399], down(899));
        QCOMPARE(data[3400], 0.0f); // the punch ends at 3400

        // Undo the trim, then the recording: the old comp plays again.
        QVERIFY(s.session.timeline()->undo());
        QVERIFY(s.session.timeline()->undo());
        QCOMPARE(s.clip()->resourceId, s.compId);
        QCOMPARE(s.out("undone.wav"), before);
        QVERIFY(s.session.timeline()->redo());
        QCOMPARE(s.out("redone.wav"), grownOut);
        QCOMPARE(s.clip()->resourceId, grown);
    }

    // The same before the comp origin: positions shift, the clip's source
    // offset follows, and what plays inside the clip window stays aligned.
    void recordingBeforeTheCompStartGrowsIt()
    {
        Loop s;
        QVERIFY(s.setUp());
        QVERIFY(ok(s.comps->setFade(s.compId, QStringLiteral("0"), QStringLiteral("linear"))));
        const std::vector<float> before = s.out("before.wav");
        const QVariantMap placed = s.session.placeTake(
            linearTake(writeMono(s.dir.filePath(QStringLiteral("early.wav")), 800, down), 1500, 800, 1600, 2000));
        QVERIFY2(ok(placed), qPrintable(s.session.lastError()));
        const QString grown = placed.value(QStringLiteral("compId")).toString();
        QVERIFY(grown != s.compId);
        QCOMPARE(s.clip()->start, ticks(1800));
        QCOMPARE(s.clip()->sourceOffset, ticks(300));
        QCOMPARE(s.clip()->sourceDuration, ticks(1500));
        QCOMPARE(layout(s.comps, grown), QStringLiteral("Take 4:100-500|Take 3:500-1450|Take 2:1450-1500"));
        const std::vector<float> data = s.out("grown.wav");
        QCOMPARE(data[1800], down(300));
        QCOMPARE(data[1999], down(499));
        for (qint64 t = 2000; t < 3000; ++t)
            if (data[size_t(t)] != before[size_t(t)]) QFAIL(qPrintable(QStringLiteral("sample %1 moved").arg(t)));

        // Trimming the clip start out plays the pre-roll audio.
        TimelineEditRequest trim;
        trim.kind = TimelineEditKind::TrimStart;
        trim.clipIds = {QUuid::fromString(s.clipId)};
        TimelineEditInput input;
        input.pointerTicks = -ticks(200);
        input.snap = false;
        QVERIFY(!s.session.timeline()->apply(trim, input));
        QCOMPARE(s.clip()->start, ticks(1600));
        const std::vector<float> trimmed = s.out("trimmed.wav");
        QCOMPARE(trimmed[1599], 0.0f);
        QCOMPARE(trimmed[1600], down(100));
        QVERIFY(s.session.timeline()->undo());
        QVERIFY(s.session.timeline()->undo());
        QCOMPARE(s.out("undone.wav"), before);
    }

private:
    static QString segmentIdAt(const AudioTakeCompModel *comps, const QString &compId, int index)
    {
        return comps->compData(compId).value(QStringLiteral("segments")).toList().at(index).toMap()
            .value(QStringLiteral("segmentId")).toString();
    }
};

QTEST_MAIN(TestAudioTakeCompEdges)
#include "tst_audio_take_comp_edges.moc"
