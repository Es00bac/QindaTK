// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-068 host requests (D-196, D-197): PitchEditor's time drag and the
// Flatten switch (correctionAmount) go through AudioPitchSource.editFor /
// commitEdit into one "pitch" source edit each: the edit map, its clamping
// and refusals, the rendered and re-analysed result, and undo/redo.
#include "audio_pitch.h"
#include "audio_pitch_source.h"
#include "audio_session.h"

#include <QtTest>
#include <cmath>

using namespace QindaTK;

#include "audio_pitch_fixture.h"

using namespace QindaTK::PitchFixture;

class TestAudioPitchRequests : public QObject {
    Q_OBJECT
private slots:
    void retimingThroughTheHost()
    {
        // PitchEditor's time drag moves E4's start (note-24600) through
        // AudioPitchSource: region from A3's start (at most one E4 length
        // back), stretches clamped to 2/3×…1.5× per side, one undo step.
        Fixture f;
        QTRY_VERIFY(f.pitch.isCurrent() && f.pitch.state() == QLatin1String("ready"));
        const std::vector<float> before = render(f.session, f.dir.filePath(QStringLiteral("before.wav")));
        const auto time = [&](const QString &anchor, const QString &target) {
            return f.pitch.editFor(QStringLiteral("segment-600"), QStringLiteral("note-24600"), QStringLiteral("time"),
                                   anchor, target, 0.0, 1.0);
        };
        const auto sample = [](const QVariantMap &made, const char *key) {
            return made.value(QStringLiteral("edit")).toMap().value(QLatin1String(key)).toString();
        };
        // Refusals: only the note's start is an anchor; positions are integers.
        QVariantMap made = time(QStringLiteral("24000"), QStringLiteral("26000"));
        QVERIFY(!ok(made));
        QVERIFY(made.value(QStringLiteral("error")).toString().contains(QStringLiteral("moving its start")));
        QVERIFY(!ok(time(QStringLiteral("24600"), QStringLiteral("26000.5"))));
        QVERIFY(time(QStringLiteral("24600"), QStringLiteral("24600")).value(QStringLiteral("unchanged")).toBool());
        // The edit map: left = max(A3 start 600, 24600 − 23280) = 1320; right = E4's end.
        made = time(QStringLiteral("24600"), QStringLiteral("26000"));
        QVERIFY2(ok(made), qPrintable(made.value(QStringLiteral("error")).toString()));
        QCOMPARE(sample(made, "operation"), QStringLiteral("pitch"));
        QCOMPARE(sample(made, "startSample"), QStringLiteral("1320"));
        QCOMPARE(sample(made, "endSample"), QStringLiteral("47880"));
        QCOMPARE(sample(made, "anchorSample"), QStringLiteral("24600"));
        QCOMPARE(sample(made, "targetSample"), QStringLiteral("26000"));
        QVERIFY(!made.value(QStringLiteral("edit")).toMap().contains(QStringLiteral("targetPitchCents")));
        // Clamping: 23280 samples each side; 1.5× of the left side ends at
        // 1320 + 34920 but 2/3× of the right side already stops at
        // 47880 − 15520 = 32360; symmetrically 1320 + 15520 = 16840.
        QCOMPARE(sample(time(QStringLiteral("24600"), QStringLiteral("40000")), "targetSample"), QStringLiteral("32360"));
        QCOMPARE(sample(time(QStringLiteral("24600"), QStringLiteral("5000")), "targetSample"), QStringLiteral("16840"));
        // The first note has only 600 samples before it: a 1.5× stretch of them.
        made = f.pitch.editFor(QStringLiteral("segment-600"), QStringLiteral("note-600"), QStringLiteral("time"),
                               QStringLiteral("600"), QStringLiteral("5000"), 0.0, 1.0);
        QCOMPARE(sample(made, "startSample"), QStringLiteral("0"));
        QCOMPARE(sample(made, "targetSample"), QStringLiteral("900"));

        // Commit through the host: one step, stale at once, then the new boundary.
        AudioSourceEditModel *edits = f.session.edits();
        const QVariantMap committed = f.pitch.commitEdit(QStringLiteral("t1"), QStringLiteral("segment-600"),
                                                         QStringLiteral("note-24600"), QStringLiteral("time"),
                                                         QStringLiteral("24600"), QStringLiteral("26000"), 0.0, 1.0);
        QVERIFY2(ok(committed), qPrintable(committed.value(QStringLiteral("error")).toString()));
        QCOMPARE(edits->historyCount(), 1);
        QCOMPARE(edits->undoText(), QStringLiteral("Move sample 24600 to 26000"));
        QVERIFY(!f.pitch.isCurrent());
        QVERIFY(!ok(time(QStringLiteral("24600"), QStringLiteral("25000"))));
        QTRY_VERIFY(f.pitch.isCurrent() && f.pitch.state() == QLatin1String("ready"));
        const QVariantList notes = f.pitch.notes();
        QCOMPARE(notes.size(), 2);
        const QVariantMap a3 = notes.at(0).toMap();
        const QVariantMap e4 = notes.at(1).toMap();
        const qint64 moved = e4.value(QStringLiteral("startSample")).toString().toLongLong();
        QVERIFY2(std::abs(moved - 26000) <= 2 * 240, qPrintable(QString::number(moved)));
        QCOMPARE(a3.value(QStringLiteral("startSample")).toString(), QStringLiteral("600"));
        QVERIFY(std::fabs(a3.value(QStringLiteral("pitchCents")).toDouble() - 5700.0) < 0.05);
        QVERIFY(std::fabs(e4.value(QStringLiteral("pitchCents")).toDouble() - AudioPitch::hzToCents(330.0)) < 0.05);

        // The render: untouched outside [1320, 47880); A3 before the boundary, E4 after it.
        const std::vector<float> after = render(f.session, f.dir.filePath(QStringLiteral("after.wav")));
        QCOMPARE(after.size(), before.size());
        for (qint64 i = 0; i < 48000; ++i)
            if (i < 1320 || i >= 47880) QCOMPARE(after[size_t(i)], before[size_t(i)]);
        QVERIFY(worstError(after, 4800, 24000, 5700.0) < 0.05); // measured 0.04
        QVERIFY(worstError(after, 27000, 45600, AudioPitch::hzToCents(330.0)) < 0.25); // measured 0.08
        QVERIFY(edits->undo());
        QCOMPARE(render(f.session, f.dir.filePath(QStringLiteral("undone.wav"))), before);
        QTRY_VERIFY(f.pitch.isCurrent() && f.pitch.state() == QLatin1String("ready"));
        QCOMPARE(f.pitch.notes().at(1).toMap().value(QStringLiteral("startSample")).toString(), QStringLiteral("24600"));
        QVERIFY(edits->redo());
        QCOMPARE(render(f.session, f.dir.filePath(QStringLiteral("redone.wav"))), after);
    }

    void flattenThroughTheHost()
    {
        // The example's Flatten switch: correctionAmount 1 on the source
        // makes a pitch request flatten ±30 cents of vibrato onto the note.
        const AudioBuffer vibrato = voice([](double t) { return 220.0 * std::exp2(30.0 / 1200.0 * std::sin(2.0 * Pi * 5.0 * t)); }, 1.0);
        Fixture f(&vibrato);
        QTRY_VERIFY(f.pitch.isCurrent() && f.pitch.state() == QLatin1String("ready"));
        const std::vector<float> before = render(f.session, f.dir.filePath(QStringLiteral("before.wav")));
        QVERIFY(worstError(before, 9600, 38400, 5700.0) > 25.0);
        const QVariantList notes = f.pitch.notes();
        QCOMPARE(notes.size(), 1);
        const QVariantMap note = notes.at(0).toMap();
        const double cents = note.value(QStringLiteral("pitchCents")).toDouble();
        QVERIFY(std::fabs(cents - 5700.0) < 5.0);
        const auto request = [&](const QString &gesture) {
            return f.pitch.commitEdit(gesture, note.value(QStringLiteral("segmentId")).toString(),
                                      note.value(QStringLiteral("id")).toString(), QStringLiteral("pitch"),
                                      note.value(QStringLiteral("startSample")).toString(),
                                      note.value(QStringLiteral("startSample")).toString(), 5700.0, 1.0);
        };
        // Amount 0 at the note's own pitch is no edit; amount 1 is one.
        const auto own = [&] {
            return f.pitch.editFor(note.value(QStringLiteral("segmentId")).toString(), note.value(QStringLiteral("id")).toString(),
                                   QStringLiteral("pitch"), note.value(QStringLiteral("startSample")).toString(),
                                   note.value(QStringLiteral("startSample")).toString(), cents, 1.0);
        };
        QVERIFY(own().value(QStringLiteral("unchanged")).toBool());
        f.pitch.setCorrectionAmount(1.0);
        QVERIFY(ok(own()) && !own().value(QStringLiteral("unchanged")).toBool());
        const QVariantMap made = f.pitch.editFor(note.value(QStringLiteral("segmentId")).toString(),
                                                 note.value(QStringLiteral("id")).toString(), QStringLiteral("pitch"),
                                                 note.value(QStringLiteral("startSample")).toString(),
                                                 note.value(QStringLiteral("startSample")).toString(), 5700.0, 1.0);
        QVERIFY(ok(made) && !made.value(QStringLiteral("unchanged")).toBool());
        QCOMPARE(made.value(QStringLiteral("edit")).toMap().value(QStringLiteral("correctionAmount")).toDouble(), 1.0);
        QCOMPARE(made.value(QStringLiteral("edit")).toMap().value(QStringLiteral("targetPitchCents")).toDouble(), 5700.0);
        const QVariantMap committed = request(QStringLiteral("g1"));
        QVERIFY2(ok(committed), qPrintable(committed.value(QStringLiteral("error")).toString()));
        AudioSourceEditModel *edits = f.session.edits();
        QCOMPARE(edits->historyCount(), 1);
        QCOMPARE(edits->undoText(), QStringLiteral("Correct pitch to A3 (100%)"));
        QCOMPARE(edits->edits(f.id).at(0).toMap().value(QStringLiteral("correctionAmount")).toDouble(), 1.0);
        const std::vector<float> after = render(f.session, f.dir.filePath(QStringLiteral("after.wav")));
        QVERIFY(worstError(after, 9600, 38400, 5700.0) < 4.0);
        QTRY_VERIFY(f.pitch.isCurrent() && f.pitch.state() == QLatin1String("ready"));
        QVERIFY(std::fabs(pitchAt(f.pitch, 24000) - 5700.0) < 1.0);
        QVERIFY(edits->undo());
        QCOMPARE(render(f.session, f.dir.filePath(QStringLiteral("undone.wav"))), before);
    }
};

QTEST_MAIN(TestAudioPitchRequests)
#include "tst_audio_pitch_requests.moc"
