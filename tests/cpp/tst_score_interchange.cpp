// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-067 interchange and playback: MusicXML round trip of the declared
// feature set with every ID, a foreign file's unsupported content reported,
// the score -> MIDI conversion (ties merged, staccato, tuplets, exact PPQ),
// playback through the session's MIDI engine and tempo map, stable MIDI note
// IDs across syncs, and audition of a chord (docs/notation.md).
#include "score_midi.h"
#include "score_playback.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <vector>

using namespace QindaTK;

namespace {

constexpr int Rate = 48000;

bool ok(const QVariantMap &result)
{
    return result.value(QStringLiteral("ok")).toBool();
}

QString eventOf(const QVariantMap &result)
{
    return result.value(QStringLiteral("eventId")).toString();
}

// C4 quarter (f), E4 quarter staccato, G4 A4 B4 triplet eighths, C5 quarter
// tied to a C5 half in measure 2, then rests.
struct Melody {
    ScoreModel model;
    QString part, staff, voice;
    QStringList measures;
    QString c4, e4, g4, c5, c5Held;
    Melody()
    {
        const QVariantMap made = model.newScore(QStringLiteral("Melody"), 2, QStringLiteral("4/4"), 0);
        part = made.value(QStringLiteral("partId")).toString();
        staff = made.value(QStringLiteral("staffId")).toString();
        voice = made.value(QStringLiteral("voiceId")).toString();
        for (const QVariant &m : model.measures()) measures << m.toMap().value(QStringLiteral("id")).toString();
        c4 = eventOf(note(0, 0, QStringLiteral("C4"), QStringLiteral("quarter")));
        e4 = eventOf(note(0, 960, QStringLiteral("E4"), QStringLiteral("quarter")));
        g4 = eventOf(note(0, 1920, QStringLiteral("G4"), QStringLiteral("triplet-eighth")));
        note(0, 2240, QStringLiteral("A4"), QStringLiteral("triplet-eighth"));
        note(0, 2560, QStringLiteral("B4"), QStringLiteral("triplet-eighth"));
        c5 = eventOf(note(0, 2880, QStringLiteral("C5"), QStringLiteral("quarter")));
        c5Held = eventOf(note(1, 3840, QStringLiteral("C5"), QStringLiteral("half")));
        model.editEvent(c4, QStringLiteral("dynamic"), QStringLiteral("f"));
        model.editEvent(e4, QStringLiteral("articulation"), QStringLiteral("staccato"));
        model.editEvent(c5, QStringLiteral("tie"));
    }
    QVariantMap note(int measure, qint64 tick, const QString &pitch, const QString &duration, const QString &inStaff = {},
                     const QString &inVoice = {})
    {
        return model.insertNote(part, inStaff.isEmpty() ? staff : inStaff, inVoice.isEmpty() ? voice : inVoice,
                                measures.value(measure), QString::number(tick), pitch, duration);
    }
    QString noteId(const QString &eventId) const
    {
        return model.eventData(eventId).value(QStringLiteral("notes")).toList().value(0).toMap().value(QStringLiteral("id")).toString();
    }
};

// (sample, type, number) of the scheduled events of one channel.
QList<QList<qint64>> events(const AudioRenderGraph &graph, const QString &channel, int frames)
{
    std::vector<MidiScheduledEvent> out(1024);
    const int count = graph.midiEvents(channel, 0, frames, out.data(), int(out.size()));
    QList<QList<qint64>> list;
    for (int i = 0; i < std::min<int>(count, int(out.size())); ++i)
        list.push_back({out[std::size_t(i)].sample, out[std::size_t(i)].type, out[std::size_t(i)].number});
    return list;
}

qint64 noteOn(const QList<QList<qint64>> &list, int pitch)
{
    for (const auto &event : list)
        if (event.at(1) == MidiScheduledEvent::NoteOn && event.at(2) == pitch) return event.at(0);
    return -1;
}

} // namespace

class TestScoreInterchange : public QObject {
    Q_OBJECT
private slots:
    void musicXmlRoundTripKeepsEveryId()
    {
        Melody m;
        ScoreModel &model = m.model;
        model.setComposer(QStringLiteral("Q. Composer"));
        QVERIFY(ok(model.setPart(m.part, {{QStringLiteral("name"), QStringLiteral("Flute")}, {QStringLiteral("channel"), 3}})));
        const QVariantMap piano = model.addPart(QStringLiteral("Piano"), {QStringLiteral("treble"), QStringLiteral("bass")}, 1);
        const QString pianoId = piano.value(QStringLiteral("partId")).toString();
        const QStringList staves = piano.value(QStringLiteral("staffIds")).toStringList();
        const QVariantList pianoStaves = model.parts().value(1).toMap().value(QStringLiteral("staves")).toList();
        const QString upper = pianoStaves.value(0).toMap().value(QStringLiteral("voices")).toList().value(0).toMap()
                                  .value(QStringLiteral("id")).toString();
        const QString lower = pianoStaves.value(1).toMap().value(QStringLiteral("voices")).toList().value(0).toMap()
                                  .value(QStringLiteral("id")).toString();
        const QString second = model.addVoice(staves[0]).value(QStringLiteral("voiceId")).toString();
        const QString chord = eventOf(model.insertNote(pianoId, staves[0], upper, m.measures[0], QStringLiteral("0"),
                                                       QStringLiteral("C4"), QStringLiteral("whole")));
        QVERIFY(ok(model.editEvent(chord, QStringLiteral("addPitch"), QStringLiteral("Eb4"))));
        QVERIFY(ok(model.editEvent(chord, QStringLiteral("addPitch"), QStringLiteral("G4"))));
        QVERIFY(ok(model.insertNote(pianoId, staves[0], second, m.measures[0], QStringLiteral("960"), QStringLiteral("Bb3"),
                                    QStringLiteral("quarter"))));
        QVERIFY(ok(model.insertNote(pianoId, staves[1], lower, m.measures[0], QStringLiteral("0"), QStringLiteral("C3"),
                                    QStringLiteral("dotted-half"))));
        const QString bassRest = model.adjacentEvent(eventOf(model.insertNote(pianoId, staves[1], lower, m.measures[1],
                                                                               QStringLiteral("3840"), QStringLiteral("G2"),
                                                                               QStringLiteral("whole"))),
                                                     QStringLiteral("previous"));
        QVERIFY(ok(model.editEvent(bassRest, QStringLiteral("articulation"), QStringLiteral("fermata"))));
        QVERIFY(ok(model.editEvent(m.c4, QStringLiteral("articulation"), QStringLiteral("accent"))));
        QVERIFY(ok(model.appendMeasures(1)));
        const QString last = model.measures().value(2).toMap().value(QStringLiteral("id")).toString();
        QVERIFY(ok(model.setTimeSignature(last, 3, 4)));
        QVERIFY(ok(model.setKeySignature(last, -2)));
        QVERIFY(ok(model.setBreak(m.measures[0], QStringLiteral("system"))));
        QVERIFY(ok(model.setBreak(m.measures[1], QStringLiteral("page"))));

        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("roundtrip.musicxml"));
        const QVariantMap written = model.exportMusicXml(file);
        QVERIFY2(ok(written), qPrintable(written.value(QStringLiteral("error")).toString()));
        ScoreModel imported;
        const QVariantMap report = imported.importMusicXml(file);
        QVERIFY2(ok(report), qPrintable(report.value(QStringLiteral("error")).toString()));
        QCOMPARE(report.value(QStringLiteral("unsupported")).toStringList(), QStringList());
        QCOMPARE(report.value(QStringLiteral("durationMismatches")).toInt(), 0);
        QCOMPARE(report.value(QStringLiteral("filledRests")).toInt(), 0);
        QCOMPARE(imported.toJson(), model.toJson()); // every object, value and ID
        QVERIFY(imported.content() == model.content());
        QVERIFY(imported.canUndo()); // the import is one undo step
    }

    void foreignMusicXmlReportsWhatItCannotKeep()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("foreign.musicxml"));
        QFile out(file);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(R"(<?xml version="1.0" encoding="UTF-8"?>
<score-partwise version="4.0">
  <part-list><score-part id="P1"><part-name>Voice</part-name></score-part></part-list>
  <part id="P1">
    <measure number="1">
      <attributes><divisions>2</divisions><key><fifths>0</fifths></key>
        <time><beats>4</beats><beat-type>4</beat-type></time><clef><sign>G</sign><line>2</line></clef></attributes>
      <note><grace/><pitch><step>B</step><octave>3</octave></pitch><voice>1</voice><type>eighth</type></note>
      <note><pitch><step>C</step><octave>4</octave></pitch><duration>2</duration><voice>1</voice><type>quarter</type>
        <notations><slur type="start"/></notations></note>
      <note><rest/><duration>4</duration><voice>1</voice><type>half</type></note>
      <note><pitch><step>D</step><octave>4</octave></pitch><duration>2</duration><voice>1</voice><type>quarter</type>
        <lyric><text>la</text></lyric></note>
    </measure>
  </part>
</score-partwise>
)");
        out.close();
        ScoreModel model;
        const QVariantMap report = model.importMusicXml(file);
        QVERIFY2(ok(report), qPrintable(report.value(QStringLiteral("error")).toString()));
        QCOMPARE(report.value(QStringLiteral("unsupported")).toStringList(),
                 (QStringList{QStringLiteral("grace"), QStringLiteral("lyric"), QStringLiteral("slur")}));
        QCOMPARE(report.value(QStringLiteral("events")).toInt(), 3);
        QCOMPARE(report.value(QStringLiteral("durationMismatches")).toInt(), 0);
        QCOMPARE(model.parts().value(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Voice"));
        const QVariantList order = model.readingOrder();
        QCOMPARE(order.size(), 3);
        QCOMPARE(order.value(2).toMap().value(QStringLiteral("label")).toString(),
                 QStringLiteral("Measure 1, beat 4, voice 1: quarter note D 4"));
    }

    void pickupBarAndTempoAreHandledAndReported()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("pickup.musicxml"));
        QFile out(file);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(R"(<?xml version="1.0" encoding="UTF-8"?>
<score-partwise version="4.0">
  <part-list><score-part id="P1"><part-name>Flute</part-name></score-part></part-list>
  <part id="P1">
    <measure number="0" implicit="yes">
      <attributes><divisions>1</divisions><key><fifths>0</fifths><mode>major</mode></key>
        <time><beats>3</beats><beat-type>4</beat-type></time><clef><sign>G</sign><line>2</line></clef></attributes>
      <direction placement="above"><direction-type><words>Allegretto</words></direction-type><sound tempo="96"/></direction>
      <note><pitch><step>G</step><octave>4</octave></pitch><duration>1</duration><voice>1</voice><type>quarter</type>
        <stem>up</stem></note>
    </measure>
    <measure number="1">
      <note><pitch><step>C</step><octave>5</octave></pitch><duration>3</duration><voice>1</voice><type>half</type><dot/></note>
    </measure>
  </part>
</score-partwise>
)");
        out.close();
        ScoreModel model;
        const QVariantMap report = model.importMusicXml(file);
        QVERIFY2(ok(report), qPrintable(report.value(QStringLiteral("error")).toString()));
        QCOMPARE(report.value(QStringLiteral("pickups")).toInt(), 1);
        QCOMPARE(report.value(QStringLiteral("unsupported")).toStringList(),
                 (QStringList{QStringLiteral("tempo"), QStringLiteral("words")}));
        QCOMPARE(report.value(QStringLiteral("recomputed")).toStringList(),
                 (QStringList{QStringLiteral("key/mode"), QStringLiteral("stem")}));
        QCOMPARE(report.value(QStringLiteral("filledRests")).toInt(), 0);
        const QVariantList measures = model.measures();
        QCOMPARE(measures.value(0).toMap().value(QStringLiteral("pickup")).toString(), QStringLiteral("960"));
        QCOMPARE(measures.value(1).toMap().value(QStringLiteral("startTicks")).toString(), QStringLiteral("960"));
        QCOMPARE(model.totalTicksText(), QStringLiteral("3840"));
        const QVariantList order = model.readingOrder();
        QCOMPARE(order.value(0).toMap().value(QStringLiteral("label")).toString(),
                 QStringLiteral("Measure 1, beat 3, voice 1: quarter note G 4")); // the upbeat
        // The pickup survives export and import.
        const QString again = dir.filePath(QStringLiteral("again.musicxml"));
        QVERIFY(ok(model.exportMusicXml(again)));
        ScoreModel reread;
        QVERIFY(ok(reread.importMusicXml(again)));
        QCOMPARE(reread.toJson(), model.toJson());
        // A pickup becomes a full bar again only on a bar of rests.
        QVERIFY(!ok(model.setPickup(measures.value(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("0"))));
    }

    void midiConversionMergesTiesAndKeepsNotation()
    {
        Melody m;
        const ScoreMidiResult result = Score::toMidi(m.model.content(), 960);
        QVERIFY(result.error.isEmpty());
        QCOMPARE(result.length, qint64(7680));
        QList<QList<qint64>> notes;
        for (const ScoreMidiNote &note : result.notes)
            notes.push_back({note.note.tick, note.note.duration, note.note.pitch, note.note.velocity});
        const qint64 f = Midi::velocityFromUnit(Score::dynamicVelocity(QStringLiteral("f")));
        const qint64 accentless = f;
        QCOMPARE(notes, (QList<QList<qint64>>{{0, 960, 60, f}, {960, 480, 64, accentless}, {1920, 320, 67, f},
                                              {2240, 320, 69, f}, {2560, 320, 71, f}, {2880, 2880, 72, f}}));
        // The tie chain is one MIDI note whose ID is derived from its first written note.
        const QUuid held = Score::midiNoteId(QUuid::fromString(m.noteId(m.c5)));
        QCOMPARE(result.noteMap.value(QUuid::fromString(m.noteId(m.c5))), held);
        QCOMPARE(result.noteMap.value(QUuid::fromString(m.noteId(m.c5Held))), held);
        QCOMPARE(result.retained.value(QStringLiteral("tiesMerged")).toInt(), 1);
        QCOMPARE(result.retained.value(QStringLiteral("tupletEvents")).toInt(), 3);
        QCOMPARE(result.retained.value(QStringLiteral("articulations")).toInt(), 1);

        const ScoreMidiResult half = Score::toMidi(m.model.content(), 480);
        QVERIFY(half.error.isEmpty());
        QCOMPARE(half.notes[2].note.tick, qint64(960));
        QCOMPARE(half.notes[2].note.duration, qint64(160));
        QVERIFY(!Score::toMidi(m.model.content(), 100).error.isEmpty()); // 320 ticks are not exact at 100 PPQ

        // An unrelated edit keeps every other MIDI note ID.
        QVERIFY(ok(m.model.editEvent(m.e4, QStringLiteral("pitch"), QStringLiteral("F4"))));
        const ScoreMidiResult again = Score::toMidi(m.model.content(), 960);
        QCOMPARE(again.notes[0].note.id, result.notes[0].note.id);
        QCOMPARE(again.notes[1].note.id, result.notes[1].note.id); // same written note, new pitch
        QCOMPARE(again.notes[1].note.pitch, 65);
    }

    void playbackSchedulesThroughTheSessionAndTempoMap()
    {
        Melody m;
        AudioSession session;
        ScorePlayback playback;
        playback.setScore(&m.model);
        playback.setSession(&session);
        const QVariantMap synced = playback.sync();
        QVERIFY2(ok(synced), qPrintable(synced.value(QStringLiteral("error")).toString()));
        QVERIFY(!playback.trackId().isEmpty() && !playback.patternId().isEmpty() && !playback.clipId().isEmpty());
        const MidiPattern *pattern = session.midi()->pattern(QUuid::fromString(playback.patternId()));
        QVERIFY(pattern);
        QCOMPARE(pattern->notes.size(), 6);
        QCOMPARE(pattern->length, qint64(7680));
        const QString channel = AudioMixer::trackChannelId(QUuid::fromString(playback.trackId()));
        using E = MidiScheduledEvent;
        // 120 BPM, 960 PPQ, 48 kHz: 25 samples per tick.
        QCOMPARE(events(*session.compileGraph(Rate, true), channel, 8 * 24000),
                 (QList<QList<qint64>>{{0, E::NoteOn, 60}, {24000, E::NoteOff, 60}, {24000, E::NoteOn, 64},
                                       {36000, E::NoteOff, 64}, {48000, E::NoteOn, 67}, {56000, E::NoteOff, 67},
                                       {56000, E::NoteOn, 69}, {64000, E::NoteOff, 69}, {64000, E::NoteOn, 71},
                                       {72000, E::NoteOff, 71}, {72000, E::NoteOn, 72}, {144000, E::NoteOff, 72}}));
        const QString at = playback.timelineTicksFor(QStringLiteral("2240"));
        QVERIFY(!at.isEmpty());
        QCOMPARE(playback.scoreTickAt(at), QStringLiteral("2240"));

        // 60 BPM from beat 3: the triplets and the tied note follow the tempo map.
        QVERIFY(ok(session.tempo()->applyEdit({{QStringLiteral("kind"), QStringLiteral("addTempo")},
                                                {QStringLiteral("tick"), QStringLiteral("1920")},
                                                {QStringLiteral("bpm"), QStringLiteral("60")}})));
        const auto slow = events(*session.compileGraph(Rate, true), channel, 8 * 24000);
        QCOMPARE(noteOn(slow, 67), qint64(48000));
        QCOMPARE(noteOn(slow, 69), qint64(64000));
        QCOMPARE(noteOn(slow, 72), qint64(96000));
        QVERIFY(session.tempo()->undo());

        // A score edit re-syncs as one MIDI step; unchanged notes keep their IDs.
        const QString c4Midi = playback.midiNoteFor(m.noteId(m.c4));
        QVERIFY(!c4Midi.isEmpty());
        const QString clip = playback.clipId();
        playback.setAutoSync(true);
        QVERIFY(ok(m.model.editEvent(m.e4, QStringLiteral("pitch"), QStringLiteral("F4"))));
        QCOMPARE(playback.midiNoteFor(m.noteId(m.c4)), c4Midi);
        QCOMPARE(playback.clipId(), clip);
        QCOMPARE(session.midi()->undoText(), QStringLiteral("Update score playback"));
        QCOMPARE(noteOn(events(*session.compileGraph(Rate, true), channel, 8 * 24000), 65), qint64(24000));
    }

    void playbackLinkSurvivesReopenAndReassignment()
    {
        Melody m;
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("session.qaudio"));
        const auto midiTracks = [](AudioSession &session) {
            int count = 0;
            for (const QString &id : session.timeline()->trackIds())
                count += session.timeline()->trackData(id).value(QStringLiteral("kind")).toString() == QLatin1String("midi");
            return count;
        };
        QString track, pattern, clip;
        {
            AudioSession session;
            ScorePlayback playback;
            playback.setScore(&m.model);
            playback.setSession(&session);
            QVERIFY(ok(playback.sync()));
            track = playback.trackId();
            pattern = playback.patternId();
            clip = playback.clipId();
            QCOMPARE(track, Score::idText(Score::playbackTrackId(m.model.content().id)));
            QVERIFY(session.save(file));
        }
        AudioSession reopened;
        QVERIFY(reopened.open(file));
        ScorePlayback playback;
        playback.setSession(&reopened);
        playback.setScore(&m.model);
        // Found before any sync, and a sync reuses them.
        QCOMPARE(playback.trackId(), track);
        QCOMPARE(playback.patternId(), pattern);
        QCOMPARE(playback.clipId(), clip);
        QVERIFY(ok(playback.sync()));
        QCOMPARE(playback.clipId(), clip);
        QCOMPARE(midiTracks(reopened), 1);
        QCOMPARE(reopened.midi()->patterns().size(), 1);
        QCOMPARE(reopened.timeline()->clipCount(), 1);

        // Another score gets its own link; switching back finds the first.
        ScoreModel other;
        other.newScore(QStringLiteral("Other"), 1, QStringLiteral("4/4"), 0);
        playback.setScore(&other);
        QVERIFY(playback.trackId().isEmpty());
        QVERIFY(ok(playback.sync()));
        QVERIFY(playback.trackId() != track);
        playback.setScore(&m.model);
        QCOMPARE(playback.trackId(), track);
        QVERIFY(ok(playback.sync()));
        QCOMPARE(midiTracks(reopened), 2);
        QCOMPARE(reopened.midi()->patterns().size(), 2);
        QCOMPARE(reopened.timeline()->clipCount(), 2);
    }

    void failedSyncRollsBack()
    {
        Melody m;
        AudioSession session;
        ScorePlayback playback;
        playback.setScore(&m.model);
        playback.setSession(&session);
        QVERIFY(ok(playback.sync()));
        const QString clip = playback.clipId();
        const MidiPattern before = *session.midi()->pattern(QUuid::fromString(playback.patternId()));
        const int timelineHistory = session.timeline()->historyCount();
        // A hidden track refuses new clips; the longer score needs a new one.
        TimelineEditRequest hide;
        hide.kind = TimelineEditKind::TrackProperty;
        hide.trackId = QUuid::fromString(playback.trackId());
        hide.property = QStringLiteral("hidden");
        hide.value = true;
        OperationError problem;
        const QString operation = session.timeline()->begin(hide, &problem);
        QVERIFY(!operation.isEmpty());
        QVERIFY(!session.timeline()->update(operation, {}).error);
        QVERIFY(!session.timeline()->commit(operation));
        QVERIFY(ok(m.model.appendMeasures(1)));
        const QVariantMap failed = playback.sync();
        QVERIFY(!ok(failed));
        QVERIFY(!playback.lastError().isEmpty());
        // The MIDI step and the clip removal were undone.
        QCOMPARE(*session.midi()->pattern(QUuid::fromString(playback.patternId())), before);
        QCOMPARE(playback.clipId(), clip);
        QVERIFY(session.timeline()->clipRecord(QUuid::fromString(clip)));
        QCOMPARE(session.timeline()->clipCount(), 1);
        QCOMPARE(session.timeline()->undoText(), QStringLiteral("Change track")); // only the host's hide remains
        QVERIFY(session.timeline()->historyCount() >= timelineHistory);
    }

    void auditionSoundsTheChord()
    {
        Melody m;
        QVERIFY(ok(m.model.editEvent(m.c4, QStringLiteral("addPitch"), QStringLiteral("E4"))));
        QVERIFY(ok(m.model.editEvent(m.c4, QStringLiteral("addPitch"), QStringLiteral("G4"))));
        AudioSession session;
        ScorePlayback playback;
        playback.setScore(&m.model);
        playback.setSession(&session);
        QVERIFY(!playback.audition(m.c4)); // no engine
        QVERIFY(!playback.lastError().isEmpty());
        AudioEngine engine;
        session.setEngine(&engine);
        QSignalSpy applied(&engine, &AudioEngine::configurationApplied);
        engine.applyConfiguration(QStringLiteral("test"), {{QStringLiteral("deviceId"), QStringLiteral("builtin:null")},
                                                          {QStringLiteral("sampleRate"), Rate},
                                                          {QStringLiteral("bufferSize"), 256},
                                                          {QStringLiteral("inputLayout"), QStringLiteral("None")},
                                                          {QStringLiteral("outputLayout"), QStringLiteral("Stereo")}});
        QVERIFY(!applied.isEmpty() || applied.wait(5000));
        QCOMPARE(applied.last().at(1).toString(), QStringLiteral("accepted"));
        QVERIFY(playback.audition(m.c4));
        QTRY_COMPARE(engine.auditionVoices(), 3);
        QVERIFY(playback.releaseAudition());
        QTRY_COMPARE(engine.auditionVoices(), 0);
        session.setEngine(nullptr);
    }
};

QTEST_MAIN(TestScoreInterchange)
#include "tst_score_interchange.moc"
