// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-067 score model: note values and spelled pitches, entry with the
// overwrite rule and rest filling, tuplet groups, ties and accidental
// context, notation edits, voices, measures and meters, undo, and the native
// file round trip (docs/notation.md).
#include "score_model.h"

#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

namespace {

bool ok(const QVariantMap &result)
{
    return result.value(QStringLiteral("ok")).toBool();
}

struct Fixture {
    ScoreModel model;
    QString part, staff, voice;
    QStringList measures;

    explicit Fixture(int count = 2, const QString &time = QStringLiteral("4/4"), int fifths = 0)
    {
        const QVariantMap made = model.newScore(QStringLiteral("Test"), count, time, fifths);
        part = made.value(QStringLiteral("partId")).toString();
        staff = made.value(QStringLiteral("staffId")).toString();
        voice = made.value(QStringLiteral("voiceId")).toString();
        refreshMeasures();
    }
    void refreshMeasures()
    {
        measures.clear();
        for (const QVariant &measure : model.measures()) measures << measure.toMap().value(QStringLiteral("id")).toString();
    }
    QVariantMap note(int measure, qint64 tick, const QString &pitch, const QString &duration, const QString &inVoice = {})
    {
        return model.insertNote(part, staff, inVoice.isEmpty() ? voice : inVoice, measures.value(measure),
                                QString::number(tick), pitch, duration);
    }
    // "tick duration pitch(+pitch)|rest" for the events of one voice in one measure.
    QStringList values(int measure, const QString &inVoice = {}) const
    {
        const QVector<qint64> starts = Score::measureStarts(model.content());
        const QUuid voiceId = QUuid::fromString(inVoice.isEmpty() ? voice : inVoice);
        QStringList list;
        for (const ScorePart &p : model.content().parts)
            for (const ScoreStaff &s : p.staves)
                for (const ScoreVoice &v : s.voices) {
                    if (v.id != voiceId) continue;
                    for (const ScoreEvent &event : v.events) {
                        if (event.tick < starts[measure] || event.tick >= starts[measure + 1]) continue;
                        QStringList pitches;
                        for (const ScoreNote &n : event.notes) pitches << Score::pitchName(n.pitch);
                        list << QStringLiteral("%1 %2 %3").arg(event.tick).arg(Score::durationId(event.duration),
                                                               event.rest ? QStringLiteral("rest") : pitches.join(QLatin1Char('+')));
                    }
                }
        return list;
    }
    QString firstPitch(const QString &eventId) const
    {
        return model.eventData(eventId).value(QStringLiteral("notes")).toList().value(0).toMap().value(QStringLiteral("pitch")).toString();
    }
};

} // namespace

class TestScoreModel : public QObject {
    Q_OBJECT
private slots:
    void noteValuesAndSpelledPitches()
    {
        const auto ticksOf = [](const QString &id) { return Score::ticks(*Score::parseDuration(id)).value_or(-1); };
        QCOMPARE(ticksOf(QStringLiteral("quarter")), qint64(960));
        QCOMPARE(ticksOf(QStringLiteral("dotted-half")), qint64(2880));
        QCOMPARE(ticksOf(QStringLiteral("double-dotted-eighth")), qint64(840));
        QCOMPARE(ticksOf(QStringLiteral("triplet-eighth")), qint64(320));
        QCOMPARE(ticksOf(QStringLiteral("tuplet-5:4-16th")), qint64(192));
        QCOMPARE(ticksOf(QStringLiteral("64th")), qint64(60));
        QVERIFY(!Score::parseDuration(QStringLiteral("tuplet-7:4-16th"))); // 960/7: not a whole tick
        QVERIFY(!Score::parseDuration(QStringLiteral("dotted-quaver")));
        for (const QString &id : {QStringLiteral("whole"), QStringLiteral("double-dotted-half"), QStringLiteral("triplet-quarter"),
                                  QStringLiteral("dotted-triplet-eighth"), QStringLiteral("tuplet-5:4-16th"),
                                  QStringLiteral("tuplet-3:2:eighth-quarter")})
            QCOMPARE(Score::durationId(*Score::parseDuration(id)), id);

        QCOMPARE(Score::midiPitch(*Score::parsePitch(QStringLiteral("F#5"))), 78);
        QCOMPARE(Score::midiPitch(*Score::parsePitch(QStringLiteral("Bb3"))), 58);
        QCOMPARE(Score::midiPitch(*Score::parsePitch(QStringLiteral("C##4"))), 62);
        QCOMPARE(Score::midiPitch(*Score::parsePitch(QStringLiteral("Cb4"))), 59);
        QCOMPARE(Score::pitchName(Score::spell(66, 1)), QStringLiteral("F#4"));
        QCOMPARE(Score::pitchName(Score::spell(66, -6)), QStringLiteral("Gb4"));
        QCOMPARE(Score::pitchName(Score::spell(70, -1)), QStringLiteral("Bb4"));
        QCOMPARE(Score::pitchName(Score::spell(61, 0)), QStringLiteral("C#4"));
        QStringList names;
        for (const ScorePitch &pitch : Score::enharmonics(*Score::parsePitch(QStringLiteral("C4")))) names << Score::pitchName(pitch);
        QCOMPARE(names, (QStringList{QStringLiteral("B#3"), QStringLiteral("C4"), QStringLiteral("Dbb4")}));
    }

    void entryOverwritesAndFillsWithRests()
    {
        Fixture f;
        QCOMPARE(f.values(0), QStringList{QStringLiteral("0 whole rest")});
        QVERIFY(ok(f.note(0, 0, QStringLiteral("C4"), QStringLiteral("quarter"))));
        QCOMPARE(f.values(0), (QStringList{QStringLiteral("0 quarter C4"), QStringLiteral("960 quarter rest"),
                                           QStringLiteral("1920 half rest")}));
        QVERIFY(ok(f.note(0, 960, QStringLiteral("E4"), QStringLiteral("eighth"))));
        QCOMPARE(f.values(0), (QStringList{QStringLiteral("0 quarter C4"), QStringLiteral("960 eighth E4"),
                                           QStringLiteral("1440 eighth rest"), QStringLiteral("1920 half rest")}));
        const QString before = f.model.toJson();
        QVERIFY(ok(f.note(0, 960, QStringLiteral("G4"), QStringLiteral("dotted-quarter"))));
        QCOMPARE(f.values(0), (QStringList{QStringLiteral("0 quarter C4"), QStringLiteral("960 dotted-quarter G4"),
                                           QStringLiteral("2400 eighth rest"), QStringLiteral("2880 quarter rest")}));
        for (const QVariant &row : f.model.rhythmReport())
            QCOMPARE(row.toMap().value(QStringLiteral("status")).toString(), QStringLiteral("complete"));
        QCOMPARE(f.model.undoText(), QStringLiteral("Enter note"));
        QVERIFY(f.model.undo());
        QCOMPARE(f.model.toJson(), before); // the complete previous content
        QVERIFY(f.model.redo());

        // Refusals change nothing.
        const QString revision = f.model.revisionText();
        QVERIFY(!ok(f.note(0, 960, QStringLiteral("C4"), QStringLiteral("dotted-whole")))); // crosses the bar line
        QVERIFY(!ok(f.note(0, 3840, QStringLiteral("C4"), QStringLiteral("quarter"))));    // not in measure 1
        QVERIFY(!ok(f.note(0, 0, QStringLiteral("H4"), QStringLiteral("quarter"))));
        QVERIFY(!ok(f.note(0, 0, QStringLiteral("C4"), QStringLiteral("tuplet-7:4-16th"))));
        QCOMPARE(f.model.revisionText(), revision);
        QVERIFY(!f.model.lastError().isEmpty());
    }

    void tupletsFormCompleteGroups()
    {
        Fixture f(1);
        QVERIFY(ok(f.note(0, 0, QStringLiteral("C5"), QStringLiteral("triplet-eighth"))));
        QCOMPARE(f.values(0), (QStringList{QStringLiteral("0 triplet-eighth C5"), QStringLiteral("320 triplet-eighth rest"),
                                           QStringLiteral("640 triplet-eighth rest"), QStringLiteral("960 quarter rest"),
                                           QStringLiteral("1920 half rest")}));
        QVERIFY(ok(f.note(0, 320, QStringLiteral("D5"), QStringLiteral("triplet-eighth"))));
        QCOMPARE(f.values(0), (QStringList{QStringLiteral("0 triplet-eighth C5"), QStringLiteral("320 triplet-eighth D5"),
                                           QStringLiteral("640 triplet-eighth rest"), QStringLiteral("960 quarter rest"),
                                           QStringLiteral("1920 half rest")}));
        QString error;
        const auto groups = Score::tupletGroups(f.model.content().parts[0].staves[0].voices[0].events, &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(groups.size(), 1);
        QCOMPARE(groups[0].first, 0);
        QCOMPARE(groups[0].last, 2);
        QCOMPARE(groups[0].span, qint64(960));
        // A plain value over part of the group replaces the whole group.
        QVERIFY(ok(f.note(0, 480, QStringLiteral("E5"), QStringLiteral("quarter"))));
        QCOMPARE(f.values(0), (QStringList{QStringLiteral("0 eighth rest"), QStringLiteral("480 quarter E5"),
                                           QStringLiteral("1440 eighth rest"), QStringLiteral("1920 half rest")}));
        // A tuplet that does not fit its group ([0, 1920) for triplet quarters) is refused.
        QVERIFY(!ok(f.note(0, 1600, QStringLiteral("C5"), QStringLiteral("triplet-quarter"))));
    }

    void tiesAccidentalsAndSpelling()
    {
        Fixture f(2, QStringLiteral("4/4"), 1); // G major
        const QString sharpHalf = f.note(0, 0, QStringLiteral("F#4"), QStringLiteral("half")).value(QStringLiteral("eventId")).toString();
        const QString natural = f.note(0, 1920, QStringLiteral("F4"), QStringLiteral("quarter")).value(QStringLiteral("eventId")).toString();
        const QString sharpAgain = f.note(0, 2880, QStringLiteral("F#4"), QStringLiteral("quarter")).value(QStringLiteral("eventId")).toString();
        const QString nextBar = f.note(1, 3840, QStringLiteral("F#4"), QStringLiteral("whole")).value(QStringLiteral("eventId")).toString();
        QVERIFY(!nextBar.isEmpty());
        const auto noteId = [&](const QString &eventId) {
            return QUuid::fromString(f.model.eventData(eventId).value(QStringLiteral("notes")).toList().value(0).toMap()
                                         .value(QStringLiteral("id")).toString());
        };
        const ScoreStaff &staff = f.model.content().parts[0].staves[0];
        auto shown = Score::accidentals(f.model.content(), staff, Score::tieContinuations(f.model.content()));
        QCOMPARE(shown.value(noteId(sharpHalf)), QString());               // the key gives F sharp
        QCOMPARE(shown.value(noteId(natural)), QStringLiteral("natural"));
        QCOMPARE(shown.value(noteId(sharpAgain)), QStringLiteral("sharp")); // cancelled earlier in the bar
        QCOMPARE(shown.value(noteId(nextBar)), QString());                 // a new bar resets

        // A tie needs the same sounding pitch next in the voice.
        const QString revision = f.model.revisionText();
        const QVariantMap refused = f.model.editEvent(sharpHalf, QStringLiteral("tie")); // F#4 -> F4
        QVERIFY(!ok(refused));
        QCOMPARE(refused.value(QStringLiteral("code")).toString(), QStringLiteral("tie-target-missing"));
        QCOMPARE(f.model.revisionText(), revision);
        QVERIFY(!f.model.eventData(sharpHalf).value(QStringLiteral("notes")).toList().value(0).toMap().value(QStringLiteral("tie")).toBool());
        QVERIFY(ok(f.model.editEvent(sharpAgain, QStringLiteral("tie"))));
        QVERIFY(Score::tieContinuations(f.model.content()).contains(noteId(nextBar)));
        QVERIFY(f.model.eventData(sharpAgain).value(QStringLiteral("notes")).toList().value(0).toMap().value(QStringLiteral("tie")).toBool());
        // Overwriting the tied-to note drops the tie instead of refusing.
        QVERIFY(ok(f.note(1, 3840, QStringLiteral("A4"), QStringLiteral("whole"))));
        QVERIFY(!f.model.eventData(sharpAgain).value(QStringLiteral("notes")).toList().value(0).toMap().value(QStringLiteral("tie")).toBool());

        // Key of C: F#4 tied over the bar line shows no sharp, the next F#4 restates it.
        Fixture c(2);
        const QString before = c.note(0, 1920, QStringLiteral("F#4"), QStringLiteral("half")).value(QStringLiteral("eventId")).toString();
        const QString over = c.note(1, 3840, QStringLiteral("F#4"), QStringLiteral("quarter")).value(QStringLiteral("eventId")).toString();
        const QString after = c.note(1, 4800, QStringLiteral("F#4"), QStringLiteral("quarter")).value(QStringLiteral("eventId")).toString();
        QVERIFY(ok(c.model.editEvent(before, QStringLiteral("tie"))));
        const auto cNote = [&](const QString &eventId) {
            return QUuid::fromString(c.model.eventData(eventId).value(QStringLiteral("notes")).toList().value(0).toMap()
                                         .value(QStringLiteral("id")).toString());
        };
        const auto cShown = Score::accidentals(c.model.content(), c.model.content().parts[0].staves[0],
                                               Score::tieContinuations(c.model.content()));
        QCOMPARE(cShown.value(cNote(before)), QStringLiteral("sharp"));
        QCOMPARE(cShown.value(cNote(over)), QString());
        QCOMPARE(cShown.value(cNote(after)), QStringLiteral("sharp"));

        QVERIFY(ok(f.model.editEvent(natural, QStringLiteral("respell"))));
        QCOMPARE(f.firstPitch(natural), QStringLiteral("Gbb4"));
        QVERIFY(ok(f.model.editEvent(natural, QStringLiteral("respell"))));
        QCOMPARE(f.firstPitch(natural), QStringLiteral("E#4"));
        QVERIFY(ok(f.model.editEvent(sharpHalf, QStringLiteral("step"), 1)));
        QCOMPARE(f.firstPitch(sharpHalf), QStringLiteral("G4"));
        QVERIFY(ok(f.model.editEvent(sharpHalf, QStringLiteral("transpose"), 1)));
        QCOMPARE(f.firstPitch(sharpHalf), QStringLiteral("G#4"));
    }

    void chordsArticulationsDynamicsAndVoices()
    {
        Fixture f(1);
        const QString chord = f.note(0, 0, QStringLiteral("C4"), QStringLiteral("whole")).value(QStringLiteral("eventId")).toString();
        QVERIFY(ok(f.model.editEvent(chord, QStringLiteral("addPitch"), QStringLiteral("E4"))));
        QVERIFY(ok(f.model.editEvent(chord, QStringLiteral("addPitch"), QStringLiteral("G4"))));
        QVERIFY(!ok(f.model.editEvent(chord, QStringLiteral("addPitch"), QStringLiteral("G4"))));
        QCOMPARE(f.values(0), QStringList{QStringLiteral("0 whole C4+E4+G4")});
        QVERIFY(ok(f.model.editEvent(chord, QStringLiteral("articulation"), QStringLiteral("accent"))));
        QVERIFY(ok(f.model.editEvent(chord, QStringLiteral("articulation"), QStringLiteral("staccato"))));
        QCOMPARE(f.model.eventData(chord).value(QStringLiteral("articulations")).toStringList(),
                 (QStringList{QStringLiteral("staccato"), QStringLiteral("accent")}));
        QVERIFY(ok(f.model.editEvent(chord, QStringLiteral("articulation"), QStringLiteral("accent"))));
        QCOMPARE(f.model.eventData(chord).value(QStringLiteral("articulations")).toStringList(), QStringList{QStringLiteral("staccato")});
        QVERIFY(ok(f.model.editEvent(chord, QStringLiteral("dynamic"), QStringLiteral("ff"))));
        QVERIFY(!ok(f.model.editEvent(chord, QStringLiteral("dynamic"), QStringLiteral("zz"))));
        const QString e4 = f.model.eventData(chord).value(QStringLiteral("notes")).toList().value(1).toMap().value(QStringLiteral("id")).toString();
        QVERIFY(ok(f.model.editEvent(chord, QStringLiteral("removePitch"), e4)));
        QCOMPARE(f.values(0), QStringList{QStringLiteral("0 whole C4+G4")});
        QVERIFY(ok(f.model.deleteEvents({chord})));
        QCOMPARE(f.values(0), QStringList{QStringLiteral("0 whole rest")});
        QVERIFY(f.model.eventData(chord).value(QStringLiteral("articulations")).toStringList().isEmpty());
        QVERIFY(!ok(f.model.editEvent(chord, QStringLiteral("articulation"), QStringLiteral("staccato"))));
        QVERIFY(ok(f.model.editEvent(chord, QStringLiteral("articulation"), QStringLiteral("fermata"))));

        const QVariantMap added = f.model.addVoice(f.staff);
        QVERIFY(ok(added));
        QCOMPARE(added.value(QStringLiteral("number")).toInt(), 2);
        const QString voice2 = added.value(QStringLiteral("voiceId")).toString();
        const QString b4 = f.note(0, 960, QStringLiteral("B4"), QStringLiteral("quarter"), voice2).value(QStringLiteral("eventId")).toString();
        QCOMPARE(f.values(0, voice2), QStringList{QStringLiteral("960 quarter B4")});
        const auto status = [&] {
            for (const QVariant &row : f.model.rhythmReport())
                if (row.toMap().value(QStringLiteral("voiceId")).toString() == voice2)
                    return row.toMap().value(QStringLiteral("status")).toString();
            return QString();
        };
        QCOMPARE(status(), QStringLiteral("partial"));
        QCOMPARE(f.model.adjacentEvent(b4, QStringLiteral("voice-up")), chord);
        QVERIFY(ok(f.model.deleteEvents({b4}))); // a note becomes a rest
        QCOMPARE(f.values(0, voice2), QStringList{QStringLiteral("960 quarter rest")});
        QVERIFY(ok(f.model.deleteEvents({b4}))); // a rest in voice 2 goes
        QCOMPARE(status(), QStringLiteral("empty"));
    }

    void measuresMetersKeysAndBreaks()
    {
        Fixture f(2);
        const QString c4 = f.note(0, 0, QStringLiteral("C4"), QStringLiteral("quarter")).value(QStringLiteral("eventId")).toString();
        QCOMPARE(f.model.appendMeasures(2).value(QStringLiteral("measureIds")).toStringList().size(), 2);
        f.refreshMeasures();
        QCOMPARE(f.measures.size(), 4);
        QVERIFY(ok(f.model.setTimeSignature(f.measures[1], 3, 4))); // measures 2-4 hold only rests
        QCOMPARE(f.model.totalTicksText(), QStringLiteral("12480"));
        QVERIFY(!ok(f.model.setTimeSignature(f.measures[0], 2, 4))); // measure 1 holds a note
        QVERIFY(ok(f.model.insertMeasures(f.measures[0], 1)));
        f.refreshMeasures();
        QCOMPARE(f.model.totalTicksText(), QStringLiteral("16320"));
        QCOMPARE(f.model.eventData(c4).value(QStringLiteral("tick")).toString(), QStringLiteral("3840"));
        QVERIFY(ok(f.model.setKeySignature(f.measures[2], 2)));
        QStringList keys;
        for (const QVariant &m : f.model.measures()) keys << m.toMap().value(QStringLiteral("fifths")).toString();
        QCOMPARE(keys, (QStringList{QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("2"), QStringLiteral("2"), QStringLiteral("2")}));
        QVERIFY(!ok(f.model.setBreak(f.measures.last(), QStringLiteral("system"))));
        QVERIFY(ok(f.model.setBreak(f.measures[0], QStringLiteral("page"))));
        QVERIFY(ok(f.model.removeMeasures({f.measures[0]})));
        QCOMPARE(f.model.eventData(c4).value(QStringLiteral("tick")).toString(), QStringLiteral("0"));
        QCOMPARE(f.model.totalTicksText(), QStringLiteral("12480"));
        QVERIFY(f.model.undo());
        QCOMPARE(f.model.totalTicksText(), QStringLiteral("16320"));
    }

    void nativeFileRoundTrip()
    {
        Fixture f(2, QStringLiteral("3/4"), -3);
        f.note(0, 0, QStringLiteral("Eb4"), QStringLiteral("triplet-quarter"));
        const QString tied = f.note(0, 1920, QStringLiteral("G4"), QStringLiteral("quarter")).value(QStringLiteral("eventId")).toString();
        f.note(1, 2880, QStringLiteral("G4"), QStringLiteral("dotted-half"));
        QVERIFY(ok(f.model.editEvent(tied, QStringLiteral("tie"))));
        QVERIFY(ok(f.model.editEvent(tied, QStringLiteral("dynamic"), QStringLiteral("mp"))));
        QVERIFY(ok(f.model.addPart(QStringLiteral("Cello"), {QStringLiteral("bass")}, 5)));
        f.model.setComposer(QStringLiteral("Anon"));

        ScoreModel copy;
        QVERIFY(ok(copy.loadJson(f.model.toJson())));
        QCOMPARE(copy.toJson(), f.model.toJson());
        QVERIFY(copy.content() == f.model.content());
        QVERIFY(!copy.canUndo());

        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("piece.qscore"));
        QVERIFY(f.model.save(file));
        ScoreModel reopened;
        QVERIFY(ok(reopened.open(file)));
        QVERIFY(reopened.content() == f.model.content());

        QJsonObject wrongPpq = QJsonDocument::fromJson(f.model.toJson().toUtf8()).object();
        wrongPpq.insert(QStringLiteral("ppq"), 480);
        QVERIFY(!ok(reopened.loadJson(QString::fromUtf8(QJsonDocument(wrongPpq).toJson()))));
        QVERIFY(reopened.content() == f.model.content()); // refused: unchanged
    }
};

QTEST_MAIN(TestScoreModel)
#include "tst_score_model.moc"
