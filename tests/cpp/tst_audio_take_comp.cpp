// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-065 take comps without a device: comp edits and the crossfade policy,
// loop passes as takes, exact rendered boundary samples, undo/redo, previews
// and cancelled gestures, audition and comparison, reopen and relink, punch
// into a clip and into a comp, and TakeCompView driving a real comp.
#include "audio_session.h"
#include "audio_take_comp_fixture.h"

#include <QFile>
#include <QJSValue>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <cmath>
#include <numbers>

using namespace QindaTK;
using namespace QindaTK::CompFixture;

namespace {

// A session with one armed track holding the synthetic loop recording.
struct Scenario {
    QTemporaryDir dir;
    AudioSession session;
    AudioTakeCompModel *comps = session.comps();
    QString track, compId, clipId, resourceId, t1, t2, t3;
    bool setUp()
    {
        track = session.addTrack(QStringLiteral("Vocal"));
        if (track.isEmpty() || !session.setTrackArmed(track, true)) return false;
        const QVariantMap placed = session.placeTake(loopTake(writeMono(dir.filePath(QStringLiteral("take.wav")), 3150, ramp)));
        compId = placed.value(QStringLiteral("compId")).toString();
        clipId = placed.value(QStringLiteral("clipId")).toString();
        resourceId = placed.value(QStringLiteral("resourceId")).toString();
        t1 = takeId(comps, compId, QStringLiteral("Take 1"));
        t2 = takeId(comps, compId, QStringLiteral("Take 2"));
        t3 = takeId(comps, compId, QStringLiteral("Take 3"));
        return placed.value(QStringLiteral("ok")).toBool() && !compId.isEmpty() && !t3.isEmpty();
    }
    std::vector<float> out(const char *name, qint64 end = -1) { return render(session, dir.filePath(QString::fromLatin1(name)), end); }
    std::vector<float> live(qint64 end = 3000) { return device(session, end); }
    bool linear4() { return comps->setFade(compId, QStringLiteral("4"), QStringLiteral("linear")).value(QStringLiteral("ok")).toBool(); }
};

// Take n's file frame heard at comp sample c (the passes of loopTake).
double take1(qint64 c) { return double(ramp(c)); }
double take2(qint64 c) { return double(ramp(1000 + c)); }
double take3(qint64 c) { return double(ramp(2000 + c)); }
double rising(qint64 j) { return (double(j) + 0.5) / 4.0; } // linear 4-sample fade in

// The latest-wins comp of loopTake with a 4-sample linear crossfade, at timeline sample t.
float initialComp(qint64 t)
{
    const qint64 c = t - 1800;
    if (c < 0 || c >= 1200) return 0.0f;
    if (c < 200) return float(take1(c));
    if (c < 204) return float(take1(c) * (1.0 - rising(c - 200)) + take3(c) * rising(c - 200));
    if (c < 1146) return float(take3(c));
    if (c < 1150) return float(take3(c) * (1.0 - rising(c - 1146)) + take2(c) * rising(c - 1146));
    return float(take2(c));
}

int mismatches(const std::vector<float> &data, qint64 from, qint64 to, const std::function<float(qint64)> &want)
{
    int count = 0;
    for (qint64 t = from; t < to; ++t) {
        const float expected = want(t);
        const float actual = t < qint64(data.size()) ? data[size_t(t)] : 0.0f;
        if (actual != expected && ++count < 4) qWarning("sample %lld = %.9g, want %.9g", t, actual, expected);
    }
    return count;
}

QString segmentAt(const AudioTakeCompModel *comps, const QString &compId, const QString &start)
{
    for (const QVariant &value : comps->compData(compId).value(QStringLiteral("segments")).toList())
        if (value.toMap().value(QStringLiteral("start")).toString() == start)
            return value.toMap().value(QStringLiteral("segmentId")).toString();
    return {};
}

} // namespace

class TestAudioTakeComp : public QObject {
    Q_OBJECT
private slots:
    void compEditsAndCrossfadePolicy()
    {
        AudioTakeComp comp;
        comp.id = QStringLiteral("comp:test");
        comp.sampleRate = Rate;
        comp.length = 1000;
        comp.fadeSamples = 8;
        comp.fadeShape = AudioCompFade::Linear;
        // a covers [0, 1000), b [100, 1000) (it starts later), c [0, 1000).
        comp.takes = {{QStringLiteral("a"), QStringLiteral("media:a"), QStringLiteral("A"), 0, 0, 1000},
                      {QStringLiteral("b"), QStringLiteral("media:b"), QStringLiteral("B"), -100, 0, 900},
                      {QStringLiteral("c"), QStringLiteral("media:c"), QStringLiteral("C"), 500, 500, 1500}};
        const auto shape = [&comp] {
            QStringList parts;
            for (const AudioCompSegment &s : comp.segments) parts << QStringLiteral("%1:%2-%3").arg(s.takeId).arg(s.start).arg(s.end);
            return parts.join(QLatin1Char('|'));
        };
        QVERIFY(AudioComp::select(comp, QStringLiteral("a"), 0, 1000).isEmpty());
        const QString first = comp.segments.first().id;
        QVERIFY(!AudioComp::select(comp, QStringLiteral("b"), 50, 300).isEmpty()); // b has no audio before 100
        QVERIFY(!AudioComp::select(comp, QStringLiteral("a"), 900, 1001).isEmpty());
        QCOMPARE(shape(), QStringLiteral("a:0-1000"));
        QVERIFY(AudioComp::select(comp, QStringLiteral("b"), 300, 500).isEmpty());
        QCOMPARE(shape(), QStringLiteral("a:0-300|b:300-500|a:500-1000"));
        QCOMPARE(comp.segments.at(0).id, first); // the left part keeps its ID
        QVERIFY(comp.segments.at(2).id != first);
        // Overlapping two segments, then adjacent to itself.
        QVERIFY(AudioComp::select(comp, QStringLiteral("c"), 400, 600).isEmpty());
        QCOMPARE(shape(), QStringLiteral("a:0-300|b:300-400|c:400-600|a:600-1000"));
        QVERIFY(AudioComp::select(comp, QStringLiteral("c"), 600, 700).isEmpty());
        QCOMPARE(shape(), QStringLiteral("a:0-300|b:300-400|c:400-600|c:600-700|a:700-1000"));
        QVERIFY(AudioComp::merge(comp, comp.segments.at(2).id).isEmpty());
        QCOMPARE(shape(), QStringLiteral("a:0-300|b:300-400|c:400-700|a:700-1000"));
        QVERIFY(!AudioComp::merge(comp, comp.segments.at(3).id).isEmpty()); // nothing follows
        QVERIFY(AudioComp::split(comp, 650).isEmpty());
        QVERIFY(!AudioComp::split(comp, 650).isEmpty()); // already an edge
        QCOMPARE(shape(), QStringLiteral("a:0-300|b:300-400|c:400-650|c:650-700|a:700-1000"));
        const QString bId = comp.segments.at(1).id;
        QVERIFY(AudioComp::select(comp, QStringLiteral("b"), 300, 400).isEmpty());
        QCOMPARE(comp.segments.at(1).id, bId); // the same range keeps its segment
        QVERIFY(AudioComp::clear(comp, 0, 100).isEmpty());
        QVERIFY(!AudioComp::clear(comp, 0, 100).isEmpty()); // nothing left there
        QCOMPARE(shape(), QStringLiteral("a:100-300|b:300-400|c:400-650|c:650-700|a:700-1000"));
        QVERIFY(AudioComp::validate(comp).isEmpty());

        // Centred 8-sample fades, bounded by half of each segment; none inside one take.
        const QVector<AudioCompPiece> pieces = AudioComp::resolve(comp);
        QCOMPARE(pieces.size(), 5);
        QCOMPARE(pieces.at(0).fadeOutStart, qint64(296));
        QCOMPARE(pieces.at(0).playEnd, qint64(304));
        QCOMPARE(pieces.at(1).fadeInStart, qint64(296));
        QCOMPARE(pieces.at(1).fadeInEnd, qint64(304));
        QCOMPARE(pieces.at(1).fadeOutStart, qint64(396));
        QCOMPARE(pieces.at(2).playStart, qint64(396));
        QCOMPARE(pieces.at(2).playEnd, qint64(650)); // c | c: cut exactly
        QCOMPARE(pieces.at(3).playStart, qint64(650));
        QCOMPARE(pieces.at(3).fadeOutStart, qint64(696));
        QCOMPARE(pieces.at(4).fadeInEnd, qint64(704));
        QCOMPARE(pieces.at(0).playStart, qint64(100)); // a gap edge is cut exactly

        // A take that starts at the junction moves the whole fade after it;
        // two takes that meet with no audio beyond it are cut.
        AudioTakeComp edge = comp;
        edge.segments.clear();
        edge.takes[1] = {QStringLiteral("b"), QStringLiteral("media:b"), QStringLiteral("B"), -500, 0, 500};
        QVERIFY(AudioComp::select(edge, QStringLiteral("a"), 0, 500).isEmpty());
        QVERIFY(AudioComp::select(edge, QStringLiteral("b"), 500, 1000).isEmpty());
        QVector<AudioCompPiece> moved = AudioComp::resolve(edge);
        QCOMPARE(moved.at(1).fadeInStart, qint64(500));
        QCOMPARE(moved.at(1).fadeInEnd, qint64(508));
        QCOMPARE(moved.at(0).playEnd, qint64(508));
        edge.takes[0].sourceEnd = 500;
        moved = AudioComp::resolve(edge);
        QCOMPARE(moved.at(0).playEnd, qint64(500));
        QCOMPARE(moved.at(1).playStart, qint64(500));
        QCOMPARE(moved.at(1).fadeInEnd, moved.at(1).fadeInStart);

        QCOMPARE(AudioComp::fadeWeight(AudioCompFade::Linear, true, 0, 0, 4), 0.125);
        QCOMPARE(AudioComp::fadeWeight(AudioCompFade::Linear, false, 3, 0, 4), 0.125);
        QCOMPARE(AudioComp::fadeWeight(AudioCompFade::Linear, true, 4, 0, 4), 1.0);
        QVERIFY(std::abs(AudioComp::fadeWeight(AudioCompFade::EqualPower, true, 1, 0, 3) - std::sqrt(0.5)) < 1e-15);

        // The codec round-trips and refuses overlapping segments.
        QString error;
        const std::optional<AudioTakeComp> decoded = AudioComp::fromMap(AudioComp::toMap(comp), &error);
        QVERIFY2(decoded && *decoded == comp, qPrintable(error));
        QVariantMap broken = AudioComp::toMap(comp);
        QVariantList segments = broken.value(QStringLiteral("segments")).toList();
        QVariantMap overlap = segments.at(1).toMap();
        overlap.insert(QStringLiteral("start"), QStringLiteral("250"));
        segments[1] = overlap;
        broken.insert(QStringLiteral("segments"), segments);
        QVERIFY(!AudioComp::fromMap(broken, &error));
        QVERIFY(!error.isEmpty());
    }

    void loopPassesBecomeTakesOfOneComp()
    {
        Scenario s;
        QVERIFY2(s.setUp(), qPrintable(s.session.lastError()));
        const TimelineClip *clip = s.session.timeline()->clipRecord(QUuid::fromString(s.clipId));
        QVERIFY(clip);
        QCOMPARE(clip->resourceId, s.compId);
        QCOMPARE(clip->start, ticks(1800)); // the first pass is heard from 1900 - 100
        QCOMPARE(clip->duration, ticks(1200));
        QCOMPARE(clip->sourceOffset, qint64(0));
        QCOMPARE(s.session.compForClip(s.clipId), s.compId);
        QVERIFY(s.session.missingMedia().isEmpty());
        const QVariantMap data = s.comps->compData(s.compId);
        QCOMPARE(data.value(QStringLiteral("length")).toString(), QStringLiteral("1200"));
        QCOMPARE(data.value(QStringLiteral("fadeSamples")).toString(), QStringLiteral("240"));
        QCOMPARE(data.value(QStringLiteral("fadeShape")).toString(), QStringLiteral("equalPower"));
        // Pass n: file frames [B, E) heard from H; offset = B - (H - 1800).
        const QStringList expected{QStringLiteral("0/0/1200"), QStringLiteral("1000/1200/2200"),
                                   QStringLiteral("2000/2200/3150")};
        QStringList takes;
        for (const QVariant &value : data.value(QStringLiteral("takes")).toList()) {
            const QVariantMap take = value.toMap();
            QCOMPARE(take.value(QStringLiteral("resourceId")).toString(), s.resourceId);
            takes << QStringLiteral("%1/%2/%3").arg(take.value(QStringLiteral("sourceOffset")).toString(),
                                                    take.value(QStringLiteral("sourceStart")).toString(),
                                                    take.value(QStringLiteral("sourceEnd")).toString());
        }
        QCOMPARE(takes, expected);
        QCOMPARE(layout(s.comps, s.compId), QStringLiteral("Take 1:0-200|Take 3:200-1150|Take 2:1150-1200"));
        // Default 240-sample equal-power fades move to the side that has audio.
        const QVariantList pieces = s.comps->pieces(s.compId);
        const auto field = [&](int i, const char *key) { return pieces.at(i).toMap().value(QString::fromLatin1(key)).toString(); };
        QCOMPARE(field(0, "fadeOutStartSample"), QStringLiteral("200"));
        QCOMPARE(field(0, "playEndSample"), QStringLiteral("440"));
        QCOMPARE(field(1, "fadeInEndSample"), QStringLiteral("440"));
        QCOMPARE(field(1, "fadeOutStartSample"), QStringLiteral("910"));
        QCOMPARE(field(1, "playEndSample"), QStringLiteral("1150"));
        QCOMPARE(field(2, "playStartSample"), QStringLiteral("910"));
        QCOMPARE(field(2, "fadeInEndSample"), QStringLiteral("1150"));
        // Creating the comp is the timeline's step, not a comp step.
        QVERIFY(!s.comps->canUndo());
        QVERIFY(s.session.timeline()->canUndo());
    }

    void renderedCompHasExactBoundariesAndUndoes()
    {
        Scenario s;
        QVERIFY(s.setUp());
        const std::vector<float> original = s.out("original.wav");
        QCOMPARE(qint64(original.size()), qint64(3000));
        QVERIFY(s.linear4());
        const std::vector<float> faded = s.out("faded.wav");
        QCOMPARE(mismatches(faded, 0, 3000, initialComp), 0);
        QCOMPARE(faded[1999], ramp(199));
        QCOMPARE(faded[2000], float(take1(200) * 0.875 + take3(200) * 0.125));
        QCOMPARE(faded[2004], ramp(2204));

        // Overlapping selection inside the Take 3 segment.
        QVERIFY(s.comps->select(s.compId, s.t1, QStringLiteral("600"), QStringLiteral("800")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(layout(s.comps, s.compId),
                 QStringLiteral("Take 1:0-200|Take 3:200-600|Take 1:600-800|Take 3:800-1150|Take 2:1150-1200"));
        std::vector<float> data = s.out("overlap.wav");
        QCOMPARE(data[1800 + 597], float(take3(597)));
        QCOMPARE(data[1800 + 598], float(take3(598) * 0.875 + take1(598) * 0.125));
        QCOMPARE(data[1800 + 601], float(take3(601) * 0.125 + take1(601) * 0.875));
        QCOMPARE(data[1800 + 602], float(take1(602)));
        QCOMPARE(data[1800 + 700], float(take1(700)));
        QCOMPARE(data[1800 + 799], float(take1(799) * 0.625 + take3(799) * 0.375));
        QCOMPARE(data[1800 + 802], float(take3(802)));

        // Adjacent to a segment of the same take: that junction is seamless.
        QVERIFY(s.comps->select(s.compId, s.t2, QStringLiteral("1100"), QStringLiteral("1150")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(layout(s.comps, s.compId), QStringLiteral("Take 1:0-200|Take 3:200-600|Take 1:600-800|Take 3:800-1100|"
                                                           "Take 2:1100-1150|Take 2:1150-1200"));
        const std::vector<float> adjacent = s.out("adjacent.wav");
        QCOMPARE(adjacent[1800 + 1149], float(take2(1149)));
        QCOMPARE(adjacent[1800 + 1150], float(take2(1150)));
        QCOMPARE(adjacent[1800 + 1102], float(take2(1102)));
        QVERIFY(s.comps->merge(s.compId, segmentAt(s.comps, s.compId, QStringLiteral("1100"))).value(QStringLiteral("ok")).toBool());
        QCOMPARE(layout(s.comps, s.compId),
                 QStringLiteral("Take 1:0-200|Take 3:200-600|Take 1:600-800|Take 3:800-1100|Take 2:1100-1200"));
        QCOMPARE(s.out("merged.wav"), adjacent);
        QVERIFY(s.comps->split(s.compId, QStringLiteral("1120")).value(QStringLiteral("ok")).toBool());
        QVERIFY(!s.comps->split(s.compId, QStringLiteral("1120")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(s.out("split.wav"), adjacent);
        QCOMPARE(s.comps->historyCount(), 5);

        // Undo walks back to the recorded comp's exact samples; redo forward.
        for (int i = 0; i < 5; ++i) QVERIFY(s.comps->undo());
        QVERIFY(!s.comps->undo());
        QCOMPARE(layout(s.comps, s.compId), QStringLiteral("Take 1:0-200|Take 3:200-1150|Take 2:1150-1200"));
        QCOMPARE(s.out("undone.wav"), original);
        for (int i = 0; i < 5; ++i) QVERIFY(s.comps->redo());
        QCOMPARE(s.out("redone.wav"), adjacent);
    }

    void previewsAuditionAndComparison()
    {
        Scenario s;
        QVERIFY(s.setUp());
        QVERIFY(s.linear4());
        const std::vector<float> committed = s.out("committed.wav");

        // A cancelled gesture leaves the committed samples and no history.
        QVERIFY(s.comps->preview(s.compId, s.t1, QStringLiteral("0"), QStringLiteral("1200")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(s.comps->previewCompId(), s.compId);
        QCOMPARE(mismatches(s.live(), 1800, 3000, [](qint64 t) { return float(take1(t - 1800)); }), 0);
        QVERIFY(s.comps->cancelPreview());
        QVERIFY(s.comps->previewCompId().isEmpty());
        QCOMPARE(s.out("cancelled.wav"), committed);
        QCOMPARE(s.comps->historyCount(), 1);

        // Each update previews from the committed comp, not the last preview.
        QVERIFY(s.comps->preview(s.compId, s.t2, QStringLiteral("200"), QStringLiteral("1200")).value(QStringLiteral("ok")).toBool());
        QVERIFY(s.comps->preview(s.compId, s.t1, QStringLiteral("0"), QStringLiteral("100")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(s.live()[1800 + 500], float(take3(500)));
        QVERIFY(!s.comps->preview(s.compId, s.t2, QStringLiteral("0"), QStringLiteral("100")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(s.comps->previewCompId(), s.compId); // a refused update keeps the preview
        QVERIFY(s.comps->commitPreview().value(QStringLiteral("changed")).toBool());
        QCOMPARE(s.comps->historyCount(), 2);
        QCOMPARE(layout(s.comps, s.compId), QStringLiteral("Take 1:0-100|Take 1:100-200|Take 3:200-1150|Take 2:1150-1200"));
        QCOMPARE(s.out("same.wav"), committed); // same take on both sides: identical samples

        // Audition plays one take alone; comparison toggles two; neither is saved.
        QVERIFY(s.comps->setAudition(s.compId, s.t2));
        QCOMPARE(s.comps->audition(s.compId), s.t2);
        QCOMPARE(mismatches(s.live(), 0, 3000, [](qint64 t) {
                     return t >= 2000 && t < 3000 ? float(take2(t - 1800)) : 0.0f; }), 0);
        QVERIFY(s.comps->compare(s.compId, s.t3, s.t1));
        QCOMPARE(mismatches(s.live(), 0, 3000, [](qint64 t) {
                     return t >= 2000 && t < 2950 ? float(take3(t - 1800)) : 0.0f; }), 0);
        QCOMPARE(s.comps->toggleComparison(s.compId), s.t1);
        QCOMPARE(mismatches(s.live(), 0, 3000, [](qint64 t) {
                     return t >= 1800 ? float(take1(t - 1800)) : 0.0f; }), 0);
        QVERIFY(!s.comps->setAudition(s.compId, QStringLiteral("take:none")));
        QVERIFY(!s.comps->contentMap().value(QStringLiteral("comps")).toList().first().toMap().contains(QStringLiteral("audition")));
        QVERIFY(s.comps->setAudition(s.compId, {}));
        QCOMPARE(s.out("back.wav"), committed);
        QCOMPARE(s.comps->historyCount(), 2);
    }

    void reopenedAndRelinkedCompRendersTheSame()
    {
        Scenario s;
        QVERIFY(s.setUp());
        QVERIFY(s.comps->select(s.compId, s.t1, QStringLiteral("600"), QStringLiteral("800")).value(QStringLiteral("ok")).toBool());
        const std::vector<float> before = s.out("before.wav", 3000);
        const QString file = s.dir.filePath(QStringLiteral("session.json"));
        QVERIFY2(s.session.save(file), qPrintable(s.session.lastError()));

        AudioSession reopened;
        QVERIFY2(reopened.open(file), qPrintable(reopened.lastError()));
        QCOMPARE(reopened.comps()->contentMap(), s.comps->contentMap());
        QCOMPARE(reopened.compForClip(s.clipId), s.compId);
        QCOMPARE(render(reopened, s.dir.filePath(QStringLiteral("reopened.wav")), 3000), before);
        QVERIFY(!reopened.comps()->canUndo());

        const QString moved = s.dir.filePath(QStringLiteral("moved.wav"));
        QVERIFY(QFile::rename(s.dir.filePath(QStringLiteral("take.wav")), moved));
        AudioSession missing;
        QVERIFY(missing.open(file));
        QCOMPARE(missing.missingMedia(), QStringList{s.resourceId});
        const std::vector<float> silent = render(missing, s.dir.filePath(QStringLiteral("silent.wav")), 3000);
        QCOMPARE(qint64(silent.size()), qint64(3000));
        QVERIFY(std::all_of(silent.begin(), silent.end(), [](float v) { return v == 0.0f; }));
        for (const QVariant &take : missing.comps()->viewState(s.compId).value(QStringLiteral("takes")).toList())
            QCOMPARE(take.toMap().value(QStringLiteral("available")).toBool(), false);
        QVERIFY(missing.relinkMedia(s.resourceId, moved));
        QVERIFY(missing.missingMedia().isEmpty());
        QCOMPARE(render(missing, s.dir.filePath(QStringLiteral("relinked.wav")), 3000), before);
    }

    void punchIntoAClipThenIntoItsComp()
    {
        QTemporaryDir dir;
        AudioSession session;
        AudioTakeCompModel *comps = session.comps();
        const QString track = session.addTrack(QStringLiteral("Vocal"));
        QVERIFY(session.setTrackArmed(track, true));
        const QVariantMap imported = session.importMedia(writeMono(dir.filePath(QStringLiteral("ramp.wav")), 4000, ramp), track);
        const QString clipId = imported.value(QStringLiteral("clipId")).toString();
        const QString media = imported.value(QStringLiteral("resourceId")).toString();
        QVERIFY(!clipId.isEmpty());
        const std::vector<float> original = render(session, dir.filePath(QStringLiteral("original.wav")), 4000);

        // Heard [800, 1800), punched [1000, 1500): the clip becomes a comp in one step.
        const QVariantMap placed = session.placeTake(
            linearTake(writeMono(dir.filePath(QStringLiteral("punch.wav")), 1000, down), 800, 1000, 1000, 1500));
        QVERIFY2(placed.value(QStringLiteral("ok")).toBool(), qPrintable(session.lastError()));
        QCOMPARE(placed.value(QStringLiteral("clipId")).toString(), clipId);
        const QString compId = placed.value(QStringLiteral("compId")).toString();
        QCOMPARE(session.compForClip(clipId), compId);
        QCOMPARE(layout(comps, compId), QStringLiteral("ramp:0-1000|Take 2:1000-1500|ramp:1500-4000"));
        const std::vector<float> punched = render(session, dir.filePath(QStringLiteral("punched.wav")), 4000);
        // Crossfades [880, 1120) and [1380, 1620); the punch and the rest are exact.
        QCOMPARE(mismatches(punched, 0, 880, [](qint64 t) { return ramp(t); }), 0);
        QCOMPARE(mismatches(punched, 1120, 1380, [](qint64 t) { return down(t - 800); }), 0);
        QCOMPARE(mismatches(punched, 1620, 4000, [](qint64 t) { return ramp(t); }), 0);
        const double angle = (1000.0 - 880.0 + 0.5) / 240.0 * std::numbers::pi / 2.0;
        QVERIFY(std::abs(punched[1000] - (double(ramp(1000)) * std::cos(angle) + double(down(200)) * std::sin(angle))) < 1e-6);

        // Timeline undo restores the plain clip and its samples; redo the comp.
        QVERIFY(session.timeline()->undo());
        QCOMPARE(session.timeline()->clipRecord(QUuid::fromString(clipId))->resourceId, media);
        QCOMPARE(render(session, dir.filePath(QStringLiteral("undone.wav")), 4000), original);
        QVERIFY(session.timeline()->redo());
        QCOMPARE(render(session, dir.filePath(QStringLiteral("redone.wav")), 4000), punched);

        // A second punch records into the comp as one comp step.
        const QVariantMap again = session.placeTake(
            linearTake(writeMono(dir.filePath(QStringLiteral("punch2.wav")), 1000, down), 1800, 1000, 2000, 2400));
        QVERIFY2(again.value(QStringLiteral("ok")).toBool(), qPrintable(session.lastError()));
        QCOMPARE(again.value(QStringLiteral("compId")).toString(), compId);
        QCOMPARE(layout(comps, compId),
                 QStringLiteral("ramp:0-1000|Take 2:1000-1500|ramp:1500-2000|Take 3:2000-2400|ramp:2400-4000"));
        QCOMPARE(comps->historyCount(), 1);
        QCOMPARE(mismatches(render(session, dir.filePath(QStringLiteral("second.wav")), 4000), 2120, 2280,
                            [](qint64 t) { return down(t - 1800); }), 0);
        QVERIFY(comps->undo());
        QCOMPARE(comps->compData(compId).value(QStringLiteral("takes")).toList().size(), 2);
        QCOMPARE(render(session, dir.filePath(QStringLiteral("first.wav")), 4000), punched);
    }

    void takeCompViewDrivesTheComp()
    {
        Scenario s;
        QVERIFY(s.setUp());
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("session"), &s.session);
        engine.rootContext()->setContextProperty(QStringLiteral("hostCompId"), s.compId);
        QQmlComponent component(&engine);
        component.setData(QByteArrayLiteral(
            "import QtQuick\nimport QindaTK as Tk\n"
            "Tk.TakeCompView {\n"
            "  width: 900; height: 320\n"
            "  readonly property var snapshot: session.comps.revision >= 0 ? session.comps.viewState(hostCompId, 12, 8) : ({})\n"
            "  takes: snapshot.takes; compSegments: snapshot.compSegments; sampleBoundaries: snapshot.sampleBoundaries\n"
            "  auditionTakeId: snapshot.audition || \"\"\n"
            "  onCompProposalPreviewRequested: (id, take, start, end) => session.comps.preview(hostCompId, take, start, end)\n"
            "  onCompProposalPreviewCanceled: id => session.comps.cancelPreview()\n"
            "  onCompProposalRequested: (id, segment, take, start, end) => {\n"
            "    if (session.comps.previewCompId !== hostCompId) session.comps.preview(hostCompId, take, start, end)\n"
            "    proposal = { proposalId: id, status: \"pending\" } }\n"
            "  onProposalDecisionRequested: (id, accept) => {\n"
            "    if (accept) session.comps.commitPreview(); else session.comps.cancelPreview()\n"
            "    proposal = null }\n"
            "  onAuditionRequested: (take, source, start, end) => session.comps.setAudition(hostCompId, take, start, end)\n"
            "  onAuditionStopRequested: take => session.comps.setAudition(hostCompId, \"\")\n"
            "}\n"), QUrl());
        std::unique_ptr<QObject> view(component.create());
        QVERIFY2(view, qPrintable(component.errorString()));
        QQuickWindow window;
        window.resize(900, 320);
        qobject_cast<QQuickItem *>(view.get())->setParentItem(window.contentItem());
        window.show();
        QVERIFY(view->property("snapshotValid").toBool());
        const QVariantMap state = s.comps->viewState(s.compId, 12, 8);
        const QStringList boundaries = state.value(QStringLiteral("sampleBoundaries")).toStringList();
        QVERIFY(boundaries.contains(QStringLiteral("910")) && boundaries.contains(QStringLiteral("440")));
        for (const QVariant &take : state.value(QStringLiteral("takes")).toList()) {
            QVERIFY(take.toMap().value(QStringLiteral("available")).toBool());
            QCOMPARE(take.toMap().value(QStringLiteral("peaks")).toList().size(), 8);
        }
        const double cells = double(boundaries.size() - 1);
        const auto call = [&](const char *method, QVariantList args) {
            QVariant result;
            bool ok = false;
            switch (args.size()) {
            case 0: ok = QMetaObject::invokeMethod(view.get(), method, Q_RETURN_ARG(QVariant, result)); break;
            case 1: ok = QMetaObject::invokeMethod(view.get(), method, Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, args[0])); break;
            case 2: ok = QMetaObject::invokeMethod(view.get(), method, Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, args[0]),
                                                   Q_ARG(QVariant, args[1])); break;
            default: ok = QMetaObject::invokeMethod(view.get(), method, Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, args[0]),
                                                    Q_ARG(QVariant, args[1]), Q_ARG(QVariant, args[2])); break;
            }
            return ok && result.toBool();
        };
        // Swipe Take 1 over the cells [300, 500): previewed while dragging, committed on accept.
        const auto cell = [&](const char *sample, int shift = 0) { return double(boundaries.indexOf(QLatin1String(sample)) + shift) + 0.5; };
        QVERIFY(call("beginSwipe", {s.t1, cell("300"), cells}));
        QVERIFY(call("updateSwipe", {cell("500", -1), cells}));
        QCOMPARE(s.comps->previewCompId(), s.compId);
        QVariant shown = view->property("sampleBoundaries");
        if (shown.metaType() == QMetaType::fromType<QJSValue>()) shown = shown.value<QJSValue>().toVariant();
        QCOMPARE(shown.toStringList(), boundaries); // the table holds still while previewing
        QVERIFY(call("finishSwipe", {}));
        QVERIFY(view->property("hasPendingProposal").toBool());
        QVERIFY(call("decideProposal", {true}));
        QVERIFY(s.comps->previewCompId().isEmpty());
        QCOMPARE(layout(s.comps, s.compId),
                 QStringLiteral("Take 1:0-200|Take 3:200-300|Take 1:300-500|Take 3:500-1150|Take 2:1150-1200"));
        QCOMPARE(s.comps->historyCount(), 1);
        const std::vector<float> accepted = s.out("accepted.wav");

        // Escape-cancelled and rejected gestures leave the comp and its samples.
        const QStringList next = s.comps->viewState(s.compId, 12, 8).value(QStringLiteral("sampleBoundaries")).toStringList();
        const double nextCells = double(next.size() - 1);
        const auto at = [&](const char *sample) { return double(next.indexOf(QLatin1String(sample))) + 0.5; };
        QVERIFY(call("beginSwipe", {s.t2, at("1000"), nextCells}));
        QCOMPARE(s.comps->previewCompId(), s.compId);
        QVERIFY(call("cancelSwipe", {}));
        QVERIFY(s.comps->previewCompId().isEmpty());
        QVERIFY(call("beginSwipe", {s.t2, at("1000"), nextCells}));
        QVERIFY(call("finishSwipe", {}));
        QVERIFY(call("decideProposal", {false}));
        QVERIFY(s.comps->previewCompId().isEmpty());
        QCOMPARE(s.comps->historyCount(), 1);
        QCOMPARE(s.out("rejected.wav"), accepted);

        // Audition from the view's selection.
        view->setProperty("selectionStartSample", QStringLiteral("300"));
        view->setProperty("selectionEndSample", QStringLiteral("500"));
        QVERIFY(call("requestAudition", {s.t2}));
        QCOMPARE(s.comps->audition(s.compId), s.t2);
        QCOMPARE(s.comps->auditionRange(s.compId).value(QStringLiteral("endSample")).toString(), QStringLiteral("500"));
        QCOMPARE(view->property("auditionTakeId").toString(), s.t2);
        QVERIFY(call("requestAudition", {s.t2})); // the same take again ends it
        QVERIFY(s.comps->audition(s.compId).isEmpty());
    }
};

QTEST_MAIN(TestAudioTakeComp)
#include "tst_audio_take_comp.moc"
