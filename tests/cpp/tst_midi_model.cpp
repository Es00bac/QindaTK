// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-064 MIDI data: resolutions, exact reversible edits (insert, move,
// resize, quantize, controller and per-note events, drag gestures, step
// rows), JSON persistence and Standard MIDI File interchange.
#include "midi_sequence_model.h"
#include "midi_smf.h"

#include <QFile>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QtTest>
#include <algorithm>

using namespace QindaTK;

namespace {

bool ok(const QVariantMap &result)
{
    return result.value(QStringLiteral("ok")).toBool();
}

QUuid uuid(const QString &text)
{
    return QUuid::fromString(text);
}

const MidiNote *noteOf(const MidiSequenceModel &model, const QString &pattern, const QString &note)
{
    for (const MidiNote &entry : model.pattern(uuid(pattern))->notes)
        if (entry.id == uuid(note)) return &entry;
    return nullptr;
}

QString addNote(MidiSequenceModel &model, const QString &pattern, qint64 tick, qint64 duration, int pitch,
                quint16 velocity, int channel = 0)
{
    return model.insertNote(pattern, {{QStringLiteral("tick"), QString::number(tick)},
                                      {QStringLiteral("duration"), QString::number(duration)},
                                      {QStringLiteral("pitch"), pitch}, {QStringLiteral("channel"), channel},
                                      {QStringLiteral("velocity16"), int(velocity)}})
        .value(QStringLiteral("noteId")).toString();
}

QString raw(quint32 value)
{
    return QString::number(value);
}

} // namespace

class TestMidiModel : public QObject {
    Q_OBJECT
private slots:
    void resolutionsScaleExactly()
    {
        QCOMPARE(Midi::scaleUp(127, 7, 16), 0xFFFFu);
        QCOMPARE(Midi::scaleUp(64, 7, 16), 0x8000u);
        QCOMPARE(Midi::scaleUp(0, 7, 16), 0u);
        QCOMPARE(Midi::scaleUp(8192, 14, 32), 0x80000000u);
        QCOMPARE(Midi::scaleUp(16383, 14, 32), 0xFFFFFFFFu);
        for (quint32 v = 0; v < 128; ++v) {
            QCOMPARE(Midi::scaleDown(Midi::scaleUp(v, 7, 16), 16, 7), v);
            QCOMPARE(Midi::scaleDown(Midi::scaleUp(v, 7, 32), 32, 7), v);
        }
        for (quint32 v : {0u, 1u, 8191u, 8192u, 8193u, 16383u}) QCOMPARE(Midi::scaleDown(Midi::scaleUp(v, 14, 32), 32, 14), v);
        for (int v = 1; v < 128; ++v) QCOMPARE(Midi::velocityTo7(Midi::velocityFrom7(v)), v);
        // A controller lane edits in normalized units at the controller's
        // resolution; 32-bit per-note values survive that path bit-exactly.
        for (quint32 v : {0u, 1u, 0x12345679u, 0x80000000u, 0xFFFFFFFEu, 0xFFFFFFFFu})
            QCOMPARE(Midi::valueFromUnit(MidiController::NoteBend, Midi::valueToUnit(MidiController::NoteBend, v)), v);
        QCOMPARE(Midi::valueFromUnit(MidiController::ControlChange, 100.0 / 127.0), Midi::scaleUp(100, 7, 32));
        QCOMPARE(Midi::valueFromUnit(MidiController::PitchBend, 0.5), Midi::scaleUp(8192, 14, 32));
        QCOMPARE(Midi::descriptor(QStringLiteral("noteBend")).value(QStringLiteral("resolutionBits")).toInt(), 32);
        QVERIFY(Midi::descriptor(QStringLiteral("cc:128")).isEmpty());
    }

    void notesEditExactlyAndUndoRestoresThePattern()
    {
        MidiSequenceModel model;
        const QString p = model.addPattern(QStringLiteral("Lead")).value(QStringLiteral("patternId")).toString();
        QCOMPARE(model.pattern(uuid(p))->length, qint64(3840));
        const QString a = addNote(model, p, 0, 480, 60, 40000);
        const QString b = model.insertNote(p, {{QStringLiteral("startTicks"), QStringLiteral("240")},
                                               {QStringLiteral("endTicks"), QStringLiteral("720")},
                                               {QStringLiteral("pitch"), 60}, {QStringLiteral("velocity"), 0.5}})
                              .value(QStringLiteral("noteId")).toString();
        QVERIFY(!a.isEmpty() && !b.isEmpty()); // overlapping notes of one pitch are allowed
        QCOMPARE(noteOf(model, p, b)->velocity, quint16(32768));
        const QVariantMap expression = model.setEvent(p, {{QStringLiteral("controller"), QStringLiteral("noteBend")},
                                                          {QStringLiteral("noteId"), b},
                                                          {QStringLiteral("tick"), QStringLiteral("300")},
                                                          {QStringLiteral("raw"), raw(305419897u)}});
        QVERIFY2(ok(expression), qPrintable(expression.value(QStringLiteral("error")).toString()));
        const MidiContent before = model.content();

        // Move: the per-note expression moves with its note, value untouched.
        QVERIFY(ok(model.moveNotes(p, {b}, QStringLiteral("120"), 2)));
        QCOMPARE(noteOf(model, p, b)->tick, qint64(360));
        QCOMPARE(noteOf(model, p, b)->pitch, 62);
        QCOMPARE(model.pattern(uuid(p))->events.first().tick, qint64(420));
        QCOMPARE(model.pattern(uuid(p))->events.first().value, 305419897u);
        // Resize: the expression stays inside [start, end) of its note.
        QVERIFY(ok(model.setNote(p, b, {{QStringLiteral("duration"), QStringLiteral("40")}})));
        QCOMPARE(noteOf(model, p, b)->duration, qint64(40));
        QCOMPARE(model.pattern(uuid(p))->events.first().tick, qint64(399));
        // A refused edit changes nothing and records nothing.
        const QString revision = model.revisionText();
        QVERIFY(!ok(model.setNote(p, b, {{QStringLiteral("pitch"), 200}})));
        QVERIFY(!model.lastError().isEmpty());
        QCOMPARE(model.revisionText(), revision);
        QCOMPARE(model.undoText(), QStringLiteral("Edit note"));
        // Delete takes the note's expression with it.
        QVERIFY(ok(model.deleteNotes(p, {b})));
        QVERIFY(model.pattern(uuid(p))->events.isEmpty());
        QCOMPARE(model.pattern(uuid(p))->notes.size(), 1);

        QVERIFY(model.undo() && model.undo() && model.undo());
        QVERIFY(model.content() == before);
        QVERIFY(model.redo() && model.redo() && model.redo());
        QCOMPARE(model.pattern(uuid(p))->notes.size(), 1);
        QCOMPARE(model.pattern(uuid(p))->notes.first().id, uuid(a));
    }

    void quantizeMovesTowardTheGridExactly()
    {
        MidiSequenceModel model;
        const QString p = model.addPattern(QStringLiteral("Q")).value(QStringLiteral("patternId")).toString();
        const QString a = addNote(model, p, 250, 300, 60, 30000);
        const QString b = addNote(model, p, 719, 260, 62, 30000);
        QVERIFY(ok(model.quantizeNotes(p, {}, QStringLiteral("240"), 50)));
        QCOMPARE(noteOf(model, p, a)->tick, qint64(245)); // 250 + round(-10 * 50 %)
        QCOMPARE(noteOf(model, p, b)->tick, qint64(720)); // 719 + round(1 * 50 %), halves away from zero
        QCOMPARE(noteOf(model, p, a)->duration, qint64(300));
        QVERIFY(model.undo());
        QCOMPARE(noteOf(model, p, a)->tick, qint64(250));
        QVERIFY(ok(model.quantizeNotes(p, {a, b}, QStringLiteral("240"), 100, true)));
        QCOMPARE(noteOf(model, p, a)->tick, qint64(240));
        QCOMPARE(noteOf(model, p, a)->end(), qint64(480));
        QCOMPARE(noteOf(model, p, b)->tick, qint64(720));
        QCOMPARE(noteOf(model, p, b)->end(), qint64(960));
        QVERIFY(!ok(model.quantizeNotes(p, {}, QStringLiteral("0"), 100)));
    }

    void dragGesturesPreviewAndCommitOneStep()
    {
        MidiSequenceModel model;
        const QString p = model.addPattern(QStringLiteral("Drag")).value(QStringLiteral("patternId")).toString();
        const QString a = addNote(model, p, 0, 480, 60, 30000);
        QVERIFY(ok(model.previewNoteEdit(QStringLiteral("drag-1"), p, a, QStringLiteral("move"), QStringLiteral("960"), 64)));
        QVERIFY(model.editing());
        QCOMPARE(model.noteRows(p, QStringLiteral("0"), QStringLiteral("3840")).first().toMap()
                     .value(QStringLiteral("startTicks")).toString(), QStringLiteral("960"));
        QCOMPARE(noteOf(model, p, a)->tick, qint64(0)); // the committed pattern (and the engine) are unchanged
        QVERIFY(!ok(model.insertNote(p, {{QStringLiteral("tick"), 0}, {QStringLiteral("duration"), 10}})));
        // Every update plans from the committed note, not from the last preview.
        QVERIFY(ok(model.previewNoteEdit(QStringLiteral("drag-1"), p, a, QStringLiteral("move"), QStringLiteral("1920"), 65)));
        QVERIFY(ok(model.finishEdit(QStringLiteral("drag-1"))));
        QVERIFY(!model.editing());
        QCOMPARE(noteOf(model, p, a)->tick, qint64(1920));
        QCOMPARE(noteOf(model, p, a)->pitch, 65);
        QCOMPARE(model.undoText(), QStringLiteral("Move note"));
        QVERIFY(model.undo());
        QCOMPARE(noteOf(model, p, a)->tick, qint64(0));
        QCOMPARE(noteOf(model, p, a)->pitch, 60);

        QVERIFY(ok(model.previewNoteEdit(QStringLiteral("drag-2"), p, a, QStringLiteral("resizeEnd"), QStringLiteral("240"), 60)));
        QVERIFY(model.cancelEdit(QStringLiteral("drag-2")));
        QCOMPARE(noteOf(model, p, a)->duration, qint64(480));
        QVERIFY(!model.canRedo() || model.redoText() == QStringLiteral("Move note"));

        QVERIFY(ok(model.previewEventEdit(QStringLiteral("vel-1"), p, QStringLiteral("velocity"), a, {}, 0.25)));
        QVERIFY(ok(model.finishEdit(QStringLiteral("vel-1"))));
        QCOMPARE(noteOf(model, p, a)->velocity, Midi::velocityFromUnit(0.25));
    }

    void controllerLaneDragsAndDeletesAreOneStepEach()
    {
        MidiSequenceModel model;
        const QString p = model.addPattern(QStringLiteral("Lane"), QStringLiteral("3840")).value(QStringLiteral("patternId")).toString();
        const QString note = addNote(model, p, 960, 480, 60, 0x8000);
        const QString cc = model.setEvent(p, {{QStringLiteral("controller"), QStringLiteral("cc:1")}, {QStringLiteral("tick"), 240},
                                              {QStringLiteral("value"), 0.25}}).value(QStringLiteral("eventId")).toString();
        const QString bend = model.setEvent(p, {{QStringLiteral("controller"), QStringLiteral("noteBend")},
                                                {QStringLiteral("noteId"), note}, {QStringLiteral("tick"), 1000},
                                                {QStringLiteral("raw"), raw(0x90000000u)}}).value(QStringLiteral("eventId")).toString();
        QVERIFY(!cc.isEmpty() && !bend.isEmpty());
        const MidiPattern committed = *model.pattern(uuid(p));
        const auto eventOf = [&](const QString &id) -> const MidiControlEvent * {
            for (const MidiControlEvent &event : model.pattern(uuid(p))->events)
                if (event.id == uuid(id)) return &event;
            return nullptr;
        };
        const auto lane = [&](const QString &controller) {
            return model.controllerEvents(p, controller, QStringLiteral("0"), QStringLiteral("3840"));
        };
        // A CC drag moves tick and value; the lane shows the preview, the pattern keeps the commit.
        QVERIFY(ok(model.previewEventEdit(QStringLiteral("cc-1"), p, QStringLiteral("cc:1"), cc, QStringLiteral("480"), 0.5)));
        QVERIFY(ok(model.previewEventEdit(QStringLiteral("cc-1"), p, QStringLiteral("cc:1"), cc, QStringLiteral("720"), 1.0)));
        QCOMPARE(lane(QStringLiteral("cc:1")).first().toMap().value(QStringLiteral("ticks")).toString(), QStringLiteral("720"));
        QCOMPARE(eventOf(cc)->tick, qint64(240));
        QVERIFY(!ok(model.previewEventEdit(QStringLiteral("cc-1"), p, QStringLiteral("cc:7"), cc, QStringLiteral("720"), 1.0)));
        QVERIFY(ok(model.finishEdit(QStringLiteral("cc-1"))));
        QCOMPARE(eventOf(cc)->tick, qint64(720));
        QCOMPARE(eventOf(cc)->value, Midi::valueFromUnit(MidiController::ControlChange, 1.0));
        QCOMPARE(model.undoText(), QStringLiteral("Edit %1").arg(Midi::descriptor(QStringLiteral("cc:1"))
                                                                     .value(QStringLiteral("label")).toString()));
        // A per-note event dragged outside its note is re-clamped inside [tick, end).
        QVERIFY(ok(model.previewEventEdit(QStringLiteral("nb-1"), p, QStringLiteral("noteBend"), bend, QStringLiteral("3000"), 0.75)));
        QVERIFY(ok(model.finishEdit(QStringLiteral("nb-1"))));
        QCOMPARE(eventOf(bend)->tick, qint64(1439));
        QCOMPARE(eventOf(bend)->value, Midi::valueFromUnit(MidiController::NoteBend, 0.75));
        QVERIFY(ok(model.previewEventEdit(QStringLiteral("nb-2"), p, QStringLiteral("noteBend"), bend, QStringLiteral("10"), 0.75)));
        QVERIFY(ok(model.finishEdit(QStringLiteral("nb-2"))));
        QCOMPARE(eventOf(bend)->tick, qint64(960));
        // A canceled drag leaves nothing behind.
        QVERIFY(ok(model.previewEventEdit(QStringLiteral("cc-2"), p, QStringLiteral("cc:1"), cc, QStringLiteral("0"), 0.0)));
        QVERIFY(model.cancelEdit(QStringLiteral("cc-2")));
        QCOMPARE(eventOf(cc)->tick, qint64(720));
        // Delete: one step for the events named; a velocity is not deletable; an unknown ID refuses all.
        QVERIFY(!ok(model.deleteEvents(p, {note})));
        QVERIFY(!ok(model.deleteEvents(p, {cc, QUuid::createUuid().toString()})));
        QVERIFY(eventOf(cc));
        QVERIFY(ok(model.deleteEvents(p, {cc, bend})));
        QVERIFY(!eventOf(cc) && !eventOf(bend));
        QVERIFY(lane(QStringLiteral("cc:1")).isEmpty());
        QCOMPARE(model.undoText(), QStringLiteral("Delete controller events"));
        QVERIFY(model.undo());
        QCOMPARE(eventOf(bend)->tick, qint64(960));
        for (int i = 0; i < 3; ++i) QVERIFY(model.undo());
        QVERIFY(*model.pattern(uuid(p)) == committed); // every drag was exactly one step
    }

    void nestedNoteReportIsExactAndLinear()
    {
        // Against the pairwise definition on random overlapping notes.
        QRandomGenerator random(64);
        for (int round = 0; round < 20; ++round) {
            QVector<MidiNote> notes;
            for (int i = 0; i < 200; ++i)
                notes.push_back({QUuid::createUuid(), random.bounded(2000), 1 + random.bounded(600),
                                 60 + random.bounded(3), random.bounded(2), 0x8000});
            MidiPattern pattern;
            pattern.notes = notes;
            Midi::sortPattern(pattern);
            int pairwise = 0;
            for (qsizetype i = 0; i < pattern.notes.size(); ++i)
                for (qsizetype j = i + 1; j < pattern.notes.size(); ++j) {
                    const MidiNote &a = pattern.notes.at(i);
                    const MidiNote &b = pattern.notes.at(j);
                    if (a.pitch == b.pitch && a.channel == b.channel && b.tick < a.end() && b.end() < a.end()) {
                        ++pairwise;
                        break;
                    }
                }
            QCOMPARE(MidiFile::nestedNotes(pattern.notes), pairwise);
        }
        // The largest legal pattern of one key without nesting: no pairwise scan.
        MidiPattern big;
        for (int i = 0; i < Midi::kMaxNotes; ++i) big.notes.push_back({QUuid::createUuid(), qint64(i) * 10, 15, 60, 0, 0x8000});
        QElapsedTimer timer;
        timer.start();
        QCOMPARE(MidiFile::nestedNotes(big.notes), 0);
        QVERIFY2(timer.elapsed() < 1000, qPrintable(QString::number(timer.elapsed())));
    }

    void stepRowsArePolymetricAndUndoRestoresThePattern()
    {
        MidiSequenceModel model;
        const QString p = model.addPattern(QStringLiteral("Beat")).value(QStringLiteral("patternId")).toString();
        const MidiPattern empty = *model.pattern(uuid(p));
        const QString three = model.addRow(p, {{QStringLiteral("pitch"), 36}, {QStringLiteral("steps"), 3},
                                               {QStringLiteral("stepTicks"), QStringLiteral("240")}})
                                  .value(QStringLiteral("rowId")).toString();
        const QString four = model.addRow(p, {{QStringLiteral("pitch"), 38}, {QStringLiteral("steps"), 4}})
                                 .value(QStringLiteral("rowId")).toString();
        const QVariantList rows = model.stepRows(p);
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.at(0).toMap().value(QStringLiteral("steps")).toList().size(), 3);
        QCOMPARE(rows.at(1).toMap().value(QStringLiteral("steps")).toList().size(), 4);
        const QString step = rows.at(0).toMap().value(QStringLiteral("steps")).toList().at(1).toMap()
                                 .value(QStringLiteral("id")).toString();
        QVERIFY(ok(model.setStep(p, three, step, QStringLiteral("enabled"), true)));
        QVERIFY(ok(model.setStep(p, three, step, QStringLiteral("probability"), 0.35)));
        QVERIFY(!ok(model.setStep(p, three, step, QStringLiteral("gate"), 1.5)));
        QVERIFY(ok(model.setRow(p, four, {{QStringLiteral("steps"), 5}})));
        QCOMPARE(model.pattern(uuid(p))->rows.at(1).steps.size(), 5);
        for (int i = 0; i < 5; ++i) QVERIFY(model.undo());
        QVERIFY(*model.pattern(uuid(p)) == empty);
    }

    void jsonRoundTripIsExact()
    {
        MidiSequenceModel model;
        const QString p = model.addPattern(QStringLiteral("Keep")).value(QStringLiteral("patternId")).toString();
        const QString a = addNote(model, p, 0, 480, 60, 12345, 3);
        QVERIFY(ok(model.setEvent(p, {{QStringLiteral("controller"), QStringLiteral("notePressure")},
                                      {QStringLiteral("noteId"), a}, {QStringLiteral("raw"), raw(0xFFFFFFFFu)}})));
        QVERIFY(ok(model.setEvent(p, {{QStringLiteral("controller"), QStringLiteral("pitchBend")}, {QStringLiteral("tick"), 10},
                                      {QStringLiteral("channel"), 3}, {QStringLiteral("raw"), raw(0x80000001u)}})));
        const QString row = model.addRow(p, {{QStringLiteral("steps"), 2}}).value(QStringLiteral("rowId")).toString();
        const QString step = model.stepRows(p).first().toMap().value(QStringLiteral("steps")).toList().first().toMap()
                                 .value(QStringLiteral("id")).toString();
        QVERIFY(ok(model.setStep(p, row, step, QStringLiteral("gate"), 0.3)));
        QVERIFY(ok(model.setPattern(p, {{QStringLiteral("seed"), 4294967295.0}})));
        const QString scene = model.addScene(QStringLiteral("A")).value(QStringLiteral("sceneId")).toString();
        QVERIFY(ok(model.setSlot(QUuid::createUuid().toString(QUuid::WithoutBraces), scene, p)));
        model.setQuantization(QStringLiteral("beat"));

        MidiSequenceModel copy;
        QVERIFY(ok(copy.loadJson(model.toJson())));
        QVERIFY(copy.content() == model.content());
        QCOMPARE(copy.quantization(), QStringLiteral("beat"));
        QVERIFY(!copy.canUndo()); // loading starts a new history

        MidiSequenceModel other;
        other.setTicksPerQuarter(480);
        QVERIFY(!ok(other.loadJson(model.toJson()))); // never rescaled silently
        QVERIFY(other.content().patterns.isEmpty());
        QVERIFY(!ok(model.removePattern(p))); // a slot plays it
    }

    void midiFileRoundTripKeepsDeclaredData()
    {
        QTemporaryDir dir;
        MidiSequenceModel model;
        const QString p = model.addPattern(QStringLiteral("Lead")).value(QStringLiteral("patternId")).toString();
        addNote(model, p, 0, 480, 60, Midi::velocityFrom7(100));
        addNote(model, p, 480, 480, 60, Midi::velocityFrom7(64)); // back to back on one key
        const QString c = addNote(model, p, 240, 960, 64, Midi::velocityFrom7(90), 1);
        const auto event = [&](const QString &controller, qint64 tick, quint32 value, const QString &note = {}) {
            QVariantMap input{{QStringLiteral("controller"), controller}, {QStringLiteral("tick"), tick},
                              {QStringLiteral("raw"), raw(value)}};
            if (!note.isEmpty()) input.insert(QStringLiteral("noteId"), note);
            return ok(model.setEvent(p, input));
        };
        QVERIFY(event(QStringLiteral("cc:7"), 0, Midi::scaleUp(100, 7, 32)));
        QVERIFY(event(QStringLiteral("pitchBend"), 120, Midi::scaleUp(12345, 14, 32)));
        QVERIFY(event(QStringLiteral("channelPressure"), 600, Midi::scaleUp(77, 7, 32)));
        QVERIFY(event(QStringLiteral("notePressure"), 300, Midi::scaleUp(50, 7, 32), c));
        QVERIFY(event(QStringLiteral("noteBend"), 400, 0x12345678u, c));
        const QVariantList tempo{
            QVariantMap{{QStringLiteral("kind"), QStringLiteral("tempo")}, {QStringLiteral("tick"), QStringLiteral("0")},
                        {QStringLiteral("microsecondsPerQuarter"), QStringLiteral("500000")}},
            QVariantMap{{QStringLiteral("kind"), QStringLiteral("meter")}, {QStringLiteral("tick"), QStringLiteral("0")},
                        {QStringLiteral("numerator"), 3}, {QStringLiteral("denominator"), 4}},
            QVariantMap{{QStringLiteral("kind"), QStringLiteral("tempo")}, {QStringLiteral("tick"), QStringLiteral("2880")},
                        {QStringLiteral("microsecondsPerQuarter"), QStringLiteral("750000")}}};
        const QString file = dir.filePath(QStringLiteral("lead.mid"));
        const QVariantMap exported = model.exportMidiFile(p, file, tempo);
        QVERIFY2(ok(exported), qPrintable(exported.value(QStringLiteral("error")).toString()));
        QCOMPARE(exported.value(QStringLiteral("droppedNoteBends")).toInt(), 1); // reported, not silently lost
        QCOMPARE(exported.value(QStringLiteral("reducedVelocities")).toInt(), 0);
        QCOMPARE(exported.value(QStringLiteral("reducedValues")).toInt(), 0);

        MidiSequenceModel copy;
        const QVariantMap imported = copy.importMidiFile(file);
        QVERIFY2(ok(imported), qPrintable(imported.value(QStringLiteral("error")).toString()));
        QCOMPARE(imported.value(QStringLiteral("roundedPositions")).toInt(), 0);
        QCOMPARE(imported.value(QStringLiteral("patternIds")).toStringList().size(), 1);
        const QVariantList importedTempo = imported.value(QStringLiteral("tempo")).toList();
        QCOMPARE(importedTempo.size(), 3);
        QCOMPARE(importedTempo.at(2).toMap().value(QStringLiteral("tick")).toString(), QStringLiteral("2880"));
        QCOMPARE(importedTempo.at(2).toMap().value(QStringLiteral("microsecondsPerQuarter")).toString(), QStringLiteral("750000"));
        QCOMPARE(importedTempo.at(1).toMap().value(QStringLiteral("numerator")).toInt(), 3);
        const MidiPattern &original = *model.pattern(uuid(p));
        const MidiPattern &back = copy.content().patterns.first();
        QCOMPARE(back.name, QStringLiteral("Lead"));
        QCOMPARE(back.length, original.length);
        QCOMPARE(back.notes.size(), original.notes.size());
        for (qsizetype i = 0; i < original.notes.size(); ++i) {
            QCOMPARE(back.notes.at(i).tick, original.notes.at(i).tick);
            QCOMPARE(back.notes.at(i).duration, original.notes.at(i).duration);
            QCOMPARE(back.notes.at(i).pitch, original.notes.at(i).pitch);
            QCOMPARE(back.notes.at(i).channel, original.notes.at(i).channel);
            QCOMPARE(back.notes.at(i).velocity, original.notes.at(i).velocity);
        }
        QCOMPARE(back.events.size(), 4);
        for (const MidiControlEvent &entry : back.events) {
            const auto same = std::find_if(original.events.begin(), original.events.end(), [&](const MidiControlEvent &o) {
                return o.kind == entry.kind && o.tick == entry.tick && o.value == entry.value && o.number == entry.number;
            });
            QVERIFY(same != original.events.end());
            if (entry.kind == MidiController::NotePressure) { // still attached to its note
                const auto note = std::find_if(back.notes.begin(), back.notes.end(),
                                               [&](const MidiNote &n) { return n.id == entry.noteId; });
                QVERIFY(note != back.notes.end());
                QCOMPARE(note->pitch, 64);
            }
        }
    }

    void midiFileReaderHandlesRunningStatusAndDivision()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("type0.mid"));
        QFile out(file);
        QVERIFY(out.open(QIODevice::WriteOnly));
        // Format 0, 480 per quarter; running status and note-on velocity 0 as note-off.
        out.write(QByteArray::fromHex("4d546864000000060000000101e0"
                                      "4d54726b00000014"
                                      "00903c64" "83603c00" "003e50" "8170803e40" "00ff2f00"));
        out.close();
        MidiSequenceModel model;
        const QVariantMap imported = model.importMidiFile(file);
        QVERIFY2(ok(imported), qPrintable(imported.value(QStringLiteral("error")).toString()));
        QCOMPARE(imported.value(QStringLiteral("format")).toInt(), 0);
        QCOMPARE(imported.value(QStringLiteral("division")).toInt(), 480);
        QCOMPARE(imported.value(QStringLiteral("roundedPositions")).toInt(), 0);
        const MidiPattern &pattern = model.content().patterns.first();
        QCOMPARE(pattern.notes.size(), 2);
        QCOMPARE(pattern.notes.at(0).tick, qint64(0));
        QCOMPARE(pattern.notes.at(0).duration, qint64(960)); // exact x2 to the session PPQ
        QCOMPARE(pattern.notes.at(0).velocity, Midi::velocityFrom7(100));
        QCOMPARE(pattern.notes.at(1).tick, qint64(960));
        QCOMPARE(pattern.notes.at(1).duration, qint64(480));
        QCOMPARE(pattern.notes.at(1).pitch, 62);
        QCOMPARE(pattern.length, qint64(1440));
        QVERIFY(model.undo()); // the import is one step
        QVERIFY(model.content().patterns.isEmpty());

        MidiSequenceModel nested;
        const QString p = nested.addPattern(QStringLiteral("N")).value(QStringLiteral("patternId")).toString();
        addNote(nested, p, 0, 960, 60, Midi::velocityFrom7(90));
        addNote(nested, p, 240, 240, 60, Midi::velocityFrom7(90));
        const QVariantMap report = nested.exportMidiFile(p, dir.filePath(QStringLiteral("nested.mid")));
        QCOMPARE(report.value(QStringLiteral("ambiguousOverlaps")).toInt(), 1);
        QVERIFY(!ok(nested.importMidiFile(dir.filePath(QStringLiteral("missing.mid")))));
    }
};

QTEST_GUILESS_MAIN(TestMidiModel)
#include "tst_midi_model.moc"
