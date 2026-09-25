// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-064 scheduling in the real render graph (no device): notes and
// controllers at exact samples through tempo changes, sample-accurate and
// block-size independent rendering, overlapping notes, no note outliving its
// release, polymetric step rows, seeded probability, launched clips from
// their boundary, the session round trip, controllers the test instrument
// plays, bounded look-back for long notes, patterns the timeline places, and
// PPQ changes that rescale exactly or are refused.
#include "audio_session.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace QindaTK;

namespace {

constexpr int Rate = 48000;
constexpr qint64 SamplesPerTick = 25; // 120 BPM, 960 PPQ, 48 kHz: 24000 samples per quarter

bool ok(const QVariantMap &result)
{
    return result.value(QStringLiteral("ok")).toBool();
}

struct Rig {
    AudioSession session;
    MidiSequenceModel *midi = session.midi();
    QString track = session.addMidiTrack(QStringLiteral("Synth"));
    QString channel = AudioMixer::trackChannelId(QUuid::fromString(track));
    QString pattern = midi->addPattern(QStringLiteral("P"), QStringLiteral("7680")).value(QStringLiteral("patternId")).toString();

    QString note(qint64 tick, qint64 duration, int pitch, int velocity16 = 65535)
    {
        return midi->insertNote(pattern, {{QStringLiteral("tick"), QString::number(tick)},
                                          {QStringLiteral("duration"), QString::number(duration)},
                                          {QStringLiteral("pitch"), pitch}, {QStringLiteral("velocity16"), velocity16}})
            .value(QStringLiteral("noteId")).toString();
    }
    AudioRenderGraphPtr graph() { return session.compileGraph(Rate, true); }
};

// (sample, type, number) of every scheduled event.
QList<QList<qint64>> events(const AudioRenderGraph &graph, const QString &channel, qint64 start, int frames,
                            const MidiLaunchState *launch = nullptr, int pitch = -1)
{
    std::vector<MidiScheduledEvent> out(4096);
    const int count = graph.midiEvents(channel, start, frames, out.data(), int(out.size()), launch);
    QList<QList<qint64>> list;
    for (int i = 0; i < std::min<int>(count, int(out.size())); ++i)
        if (pitch < 0 || out[std::size_t(i)].number == pitch)
            list.push_back({out[std::size_t(i)].sample, out[std::size_t(i)].type, out[std::size_t(i)].number});
    return list;
}

QList<qint64> noteOns(const AudioRenderGraph &graph, const QString &channel, int frames, int pitch)
{
    QList<qint64> samples;
    for (const auto &event : events(graph, channel, 0, frames, nullptr, pitch))
        if (event.at(1) == MidiScheduledEvent::NoteOn) samples.push_back(event.at(0));
    return samples;
}

std::vector<float> render(const AudioRenderGraph &graph, qint64 start, qint64 frames, int block,
                          const MidiLaunchState *launch = nullptr)
{
    std::vector<float> out(std::size_t(frames) * 2, 0.0f);
    std::vector<double> scratch(AudioRenderGraph::kMaxBlock);
    for (qint64 at = 0; at < frames; at += block) {
        const int count = int(std::min<qint64>(block, frames - at));
        graph.render(start + at, count, out.data() + at * 2, 2, scratch.data(), nullptr, launch);
    }
    return out;
}

} // namespace

class TestMidiSchedule : public QObject {
    Q_OBJECT
private slots:
    void notesScheduleAtExactSamplesThroughTempoChanges()
    {
        Rig rig;
        QVERIFY(!rig.track.isEmpty());
        rig.note(480, 480, 60);
        rig.note(2400, 240, 67);
        QVERIFY(ok(rig.midi->setEvent(rig.pattern, {{QStringLiteral("controller"), QStringLiteral("cc:74")},
                                                    {QStringLiteral("tick"), 960},
                                                    {QStringLiteral("raw"), QString::number(Midi::scaleUp(90, 7, 32))}})));
        QVERIFY(ok(rig.session.insertPattern(rig.track, rig.pattern)));
        AudioRenderGraphPtr graph = rig.graph();
        using E = MidiScheduledEvent;
        const QList<QList<qint64>> expected{{12000, E::NoteOn, 60}, {24000, E::NoteOff, 60}, {24000, E::Controller, 74},
                                            {60000, E::NoteOn, 67}, {66000, E::NoteOff, 67}};
        QCOMPARE(events(*graph, rig.channel, 0, 8 * 24000), expected);
        // Any window sees exactly its part of the same schedule.
        QCOMPARE(events(*graph, rig.channel, 24000, 36001), (QList<QList<qint64>>{{24000, E::NoteOff, 60},
                                                                               {24000, E::Controller, 74},
                                                                               {60000, E::NoteOn, 67}}));

        // 60 BPM from beat 3: tick 2400 is 1 s + 0.5 quarter at 1 s.
        QVERIFY(ok(rig.session.tempo()->applyEdit({{QStringLiteral("kind"), QStringLiteral("addTempo")},
                                                   {QStringLiteral("tick"), QStringLiteral("1920")},
                                                   {QStringLiteral("bpm"), QStringLiteral("60")}})));
        graph = rig.graph();
        QCOMPARE(rig.session.schedule()->acquire()->sampleAtMusicalTick(2400), std::optional<qint64>(72000));
        QCOMPARE(events(*graph, rig.channel, 0, 8 * 24000),
                 (QList<QList<qint64>>{{12000, E::NoteOn, 60}, {24000, E::NoteOff, 60}, {24000, E::Controller, 74},
                                       {72000, E::NoteOn, 67}, {84000, E::NoteOff, 67}}));
        QVERIFY(rig.session.tempo()->undo());
        QCOMPARE(events(*rig.graph(), rig.channel, 0, 8 * 24000), expected);
    }

    void renderIsSampleAccurateAndBlockIndependent()
    {
        Rig rig;
        rig.note(480, 480, 69);
        rig.note(720, 480, 69); // overlaps the first note on the same key
        QVERIFY(ok(rig.session.insertPattern(rig.track, rig.pattern)));
        const AudioRenderGraphPtr graph = rig.graph();
        using E = MidiScheduledEvent;
        QCOMPARE(events(*graph, rig.channel, 0, 48000),
                 (QList<QList<qint64>>{{12000, E::NoteOn, 69}, {18000, E::NoteOn, 69},
                                       {24000, E::NoteOff, 69}, {30000, E::NoteOff, 69}}));
        const std::vector<float> whole = render(*graph, 0, 48000, AudioRenderGraph::kMaxBlock);
        QVERIFY(render(*graph, 0, 48000, 37) == whole); // stateless: any block size, same samples
        for (qint64 i = 0; i < 12000; ++i) QCOMPARE(whole[std::size_t(i * 2)], 0.0f);
        QVERIFY(whole[12000 * 2] != 0.0f); // the note starts on its exact sample
        QVERIFY(whole[12000 * 2 + 1] != 0.0f);
        // Both notes sound together between 18000 and 24000.
        const std::vector<float> first = render(*graph, 18000, 1, 1);
        QVERIFY(first[0] != 0.0f);
        // The last note ends at 30000; after its 5 ms release nothing sounds.
        const qint64 silentFrom = 30000 + MidiTestInstrument::releaseFrames(Rate) - 1;
        QVERIFY(whole[std::size_t((silentFrom - 2) * 2)] != 0.0f);
        for (qint64 i = silentFrom; i < 48000; ++i) QCOMPARE(whole[std::size_t(i * 2)], 0.0f);
        // Rendering from the middle (a seek) gives the same samples.
        const std::vector<float> seek = render(*graph, 20000, 1000, 128);
        QVERIFY(std::equal(seek.begin(), seek.end(), whole.begin() + 20000 * 2));
    }

    void polymetricRowsAndSeededProbabilityAreDeterministic()
    {
        Rig rig;
        const auto row = [&](int pitch, int steps) {
            return rig.midi->addRow(rig.pattern, {{QStringLiteral("pitch"), pitch}, {QStringLiteral("steps"), steps},
                                                  {QStringLiteral("stepTicks"), QStringLiteral("240")}})
                .value(QStringLiteral("rowId")).toString();
        };
        const auto stepId = [&](const QString &rowId, int index) {
            for (const QVariant &value : rig.midi->stepRows(rig.pattern))
                if (value.toMap().value(QStringLiteral("id")).toString() == rowId)
                    return value.toMap().value(QStringLiteral("steps")).toList().at(index).toMap()
                        .value(QStringLiteral("id")).toString();
            return QString();
        };
        const QString three = row(36, 3);
        const QString four = row(38, 4);
        QVERIFY(ok(rig.midi->setStep(rig.pattern, three, stepId(three, 0), QStringLiteral("enabled"), true)));
        QVERIFY(ok(rig.midi->setStep(rig.pattern, four, stepId(four, 0), QStringLiteral("enabled"), true)));
        const QString chance = row(40, 16);
        for (int i = 0; i < 16; ++i) {
            QVERIFY(ok(rig.midi->setStep(rig.pattern, chance, stepId(chance, i), QStringLiteral("enabled"), true)));
            QVERIFY(ok(rig.midi->setStep(rig.pattern, chance, stepId(chance, i), QStringLiteral("probability"), 0.5)));
        }
        QVERIFY(ok(rig.session.insertPattern(rig.track, rig.pattern, QStringLiteral("0"), QStringLiteral("11520"))));
        const int frames = int(11520 * SamplesPerTick);
        const AudioRenderGraphPtr graph = rig.graph();

        // Rows loop on their own lengths (3 and 4 steps): 3 against 4.
        QList<qint64> threes, fours;
        for (qint64 tick = 0; tick < 11520; tick += 720) threes.push_back(tick * SamplesPerTick);
        for (qint64 tick = 0; tick < 11520; tick += 960) fours.push_back(tick * SamplesPerTick);
        QCOMPARE(noteOns(*graph, rig.channel, frames, 36), threes);
        QCOMPARE(noteOns(*graph, rig.channel, frames, 38), fours);

        // Probability: the D-151 decision of each step and pass, the same in every render.
        const auto expected = [&](quint32 seed) {
            QList<qint64> samples;
            for (qint64 j = 0; j < 48; ++j)
                if (MidiProgram::plays(seed, Midi::key(QUuid::fromString(chance)), j % 16, j / 16, 0.5))
                    samples.push_back(j * 240 * SamplesPerTick);
            return samples;
        };
        const QList<qint64> rolled = noteOns(*graph, rig.channel, frames, 40);
        QCOMPARE(rolled, expected(rig.midi->pattern(QUuid::fromString(rig.pattern))->seed));
        QCOMPARE(noteOns(*rig.graph(), rig.channel, frames, 40), rolled);
        QVERIFY(ok(rig.midi->setPattern(rig.pattern, {{QStringLiteral("seed"), 12345}})));
        const QList<qint64> reseeded = noteOns(*rig.graph(), rig.channel, frames, 40);
        QCOMPARE(reseeded, expected(12345));
        QVERIFY(reseeded != rolled);
        QVERIFY(rig.midi->undo()); // the seed is part of the pattern's history
        QCOMPARE(noteOns(*rig.graph(), rig.channel, frames, 40), rolled);
    }

    void launchedSlotsPlayFromTheirBoundary()
    {
        Rig rig;
        rig.note(0, 240, 60);
        const QString scene = rig.midi->addScene(QStringLiteral("A")).value(QStringLiteral("sceneId")).toString();
        const QString slot = rig.midi->setSlot(rig.track, scene, rig.pattern).value(QStringLiteral("slotId")).toString();
        QVERIFY(!slot.isEmpty());
        const AudioRenderGraphPtr graph = rig.session.compileGraph(Rate, false);
        const quint64 lane = Midi::key(QUuid::fromString(rig.track));
        const quint64 key = Midi::key(QUuid::fromString(slot));
        QVERIFY(graph->launchProgram(key, lane) >= 0);
        QCOMPARE(graph->launchProgram(key, lane + 1), -1); // a slot only plays on its own track
        QCOMPARE(graph->midiClock().nextGrid(1, MidiQuantize::Bar), qint64(3840));
        QCOMPARE(graph->midiClock().nextGrid(3840, MidiQuantize::Bar), qint64(3840));
        QCOMPARE(graph->midiClock().nextGrid(961, MidiQuantize::Beat), qint64(1920));
        QCOMPARE(graph->midiClock().tickAt(96001, true), qint64(3841));
        MidiLaunchState launch;
        launch.count = 1;
        launch.lanes[0].lane = lane;
        launch.lanes[0].slot = key;
        launch.lanes[0].origin = 3840;
        launch.lanes[0].start = graph->midiClock().sampleAt(3840);
        QCOMPARE(launch.lanes[0].start, qint64(96000));
        QVERIFY(events(*graph, rig.channel, 0, 96000).isEmpty()); // nothing without the launch
        using E = MidiScheduledEvent;
        QCOMPARE(events(*graph, rig.channel, 90000, 200000, &launch),
                 (QList<QList<qint64>>{{96000, E::NoteOn, 60}, {102000, E::NoteOff, 60},
                                       {96000 + 192000, E::NoteOn, 60}})); // the 2-bar pattern loops
        const std::vector<float> out = render(*graph, 95000, 2000, 256, &launch);
        QCOMPARE(out[std::size_t(999 * 2)], 0.0f);
        QVERIFY(out[std::size_t(1000 * 2)] != 0.0f);
        // A stop cuts notes at the stop sample: the replaced clip keeps only its release.
        launch.lanes[0].slot = 0;
        launch.lanes[0].previousSlot = key;
        launch.lanes[0].previousOrigin = 3840;
        launch.lanes[0].previousStart = 96000;
        launch.lanes[0].previousStop = 99000;
        const std::vector<float> cut = render(*graph, 96000, 12000, 512, &launch);
        const qint64 silent = 3000 + MidiTestInstrument::releaseFrames(Rate) - 1;
        for (qint64 i = silent; i < 12000; ++i) QCOMPARE(cut[std::size_t(i * 2)], 0.0f);
    }

    void controllersShapeTheTestInstrument()
    {
        Rig rig;
        const QString note = rig.note(0, 1920, 69); // 440 Hz for one second
        QVERIFY(ok(rig.session.insertPattern(rig.track, rig.pattern)));
        const std::vector<float> plain = render(*rig.graph(), 0, 48000, AudioRenderGraph::kMaxBlock);
        const auto event = [&](const QString &controller, qint64 tick, quint32 value, const QString &noteId = {}) {
            QVariantMap fields{{QStringLiteral("controller"), controller}, {QStringLiteral("tick"), tick},
                               {QStringLiteral("raw"), QString::number(value)}};
            if (!noteId.isEmpty()) fields.insert(QStringLiteral("noteId"), noteId);
            return rig.midi->setEvent(rig.pattern, fields);
        };
        const auto crossings = [](const std::vector<float> &out, qint64 from, qint64 to) {
            int count = 0;
            for (qint64 i = from + 1; i < to; ++i)
                count += (out[std::size_t(i * 2 - 2)] < 0.0f) != (out[std::size_t(i * 2)] < 0.0f);
            return count;
        };
        const auto largestStep = [](const std::vector<float> &out, qint64 from, qint64 to) {
            double step = 0.0;
            for (qint64 i = from + 1; i < to; ++i)
                step = std::max(step, double(std::abs(out[std::size_t(i * 2)] - out[std::size_t(i * 2 - 2)])));
            return step;
        };

        // CC7 volume at beat 2 halves the level from its exact sample on.
        QVERIFY(ok(event(QStringLiteral("cc:7"), 960, 0x80000000u)));
        std::vector<float> shaped = render(*rig.graph(), 0, 48000, AudioRenderGraph::kMaxBlock);
        QVERIFY(render(*rig.graph(), 0, 48000, 37) == shaped); // still stateless: any block size
        for (qint64 i = 0; i < 24000; ++i) QCOMPARE(shaped[std::size_t(i * 2)], plain[std::size_t(i * 2)]);
        const double half = double(0x80000000u) / 4294967295.0;
        for (qint64 i = 24000; i < 48000; i += 997)
            QVERIFY(std::abs(shaped[std::size_t(i * 2)] - plain[std::size_t(i * 2)] * half) < 1e-6);
        QVERIFY(rig.midi->undo());

        // Channel pitch bend to the top (+2 semitones), with continuous phase.
        QVERIFY(ok(event(QStringLiteral("pitchBend"), 960, 0xFFFFFFFFu)));
        shaped = render(*rig.graph(), 0, 48000, AudioRenderGraph::kMaxBlock);
        QVERIFY(render(*rig.graph(), 0, 48000, 37) == shaped);
        const std::vector<float> seek = render(*rig.graph(), 30000, 4000, 128);
        QVERIFY(std::equal(seek.begin(), seek.end(), shaped.begin() + 30000 * 2)); // a seek hears the same bend
        QCOMPARE(crossings(shaped, 0, 24000), crossings(plain, 0, 24000));
        const double ratio = double(crossings(shaped, 24000, 47000)) / double(crossings(plain, 24000, 47000));
        QVERIFY2(std::abs(ratio - std::exp2(2.0 / 12.0)) < 0.01, qPrintable(QString::number(ratio)));
        QVERIFY(largestStep(shaped, 1000, 47000) < 1.3 * largestStep(plain, 1000, 47000)); // no phase jump
        QVERIFY(rig.midi->undo());

        // A per-note bend of +12 semitones (32-bit, MIDI 2.0 range ±48) doubles the frequency.
        QVERIFY(ok(event(QStringLiteral("noteBend"), 960, 0x80000000u + 0x20000000u, note)));
        shaped = render(*rig.graph(), 0, 48000, AudioRenderGraph::kMaxBlock);
        QVERIFY(render(*rig.graph(), 0, 48000, 37) == shaped);
        const double octave = double(crossings(shaped, 24000, 47000)) / double(crossings(plain, 24000, 47000));
        QVERIFY2(std::abs(octave - 2.0) < 0.01, qPrintable(QString::number(octave)));
        // The scheduled stream still carries every controller for other instruments.
        using E = MidiScheduledEvent;
        QCOMPARE(events(*rig.graph(), rig.channel, 0, 48000),
                 (QList<QList<qint64>>{{0, E::NoteOn, 69}, {24000, E::Controller, 69}}));
    }

    void longNotesKeepTheLookBackBounded()
    {
        MidiPattern pattern;
        pattern.id = QUuid::createUuid();
        pattern.length = 3840;
        pattern.notes.push_back({QUuid::createUuid(), 0, qint64(1) << 30, 60, 0, 0x8000});
        MidiStepRow row;
        row.id = QUuid::createUuid();
        row.stepTicks = 240;
        MidiStep step;
        step.id = QUuid::createUuid();
        step.enabled = true;
        row.steps = {step, step, step, step};
        pattern.rows.push_back(row);
        const MidiProgram program = MidiProgram::compile(pattern);
        QCOMPARE(program.notes.front().duration, qint64(3840)); // a played note ends at its own repeat
        // A window 100000 passes after the origin visits only the notes that can still sound there.
        const qint64 far = qint64(3840) * 100000 + 100;
        int visits = 0;
        program.forEachNote(0, far, far + 100, [&](qint64, qint64, int, int, quint16, int) { ++visits; });
        QVERIFY2(visits <= 3, qPrintable(QString::number(visits)));

        // In the graph: the long note sounds until the pattern repeats.
        Rig rig;
        rig.midi->setPattern(rig.pattern, {{QStringLiteral("length"), QStringLiteral("3840")}});
        rig.note(0, qint64(1) << 30, 60);
        QVERIFY(ok(rig.session.insertPattern(rig.track, rig.pattern, QStringLiteral("0"), QStringLiteral("7680"))));
        using E = MidiScheduledEvent;
        QCOMPARE(events(*rig.graph(), rig.channel, 0, 2 * 96000),
                 (QList<QList<qint64>>{{0, E::NoteOn, 60}, {96000, E::NoteOff, 60}, {96000, E::NoteOn, 60}}));
    }

    void patternsThatClipsPlaceCannotBeRemoved()
    {
        Rig rig;
        rig.note(0, 480, 60);
        QVERIFY(ok(rig.session.insertPattern(rig.track, rig.pattern)));
        QVERIFY(!ok(rig.midi->removePattern(rig.pattern)));
        QVERIFY(rig.midi->lastError().contains(QStringLiteral("Synth")));
        // Undoing "Add pattern" would orphan the clip too: refused, nothing changes.
        QVERIFY(rig.midi->undo()); // the note
        QCOMPARE(rig.midi->undoText(), QStringLiteral("Add pattern"));
        QVERIFY(!rig.midi->undo());
        QVERIFY(rig.midi->pattern(QUuid::fromString(rig.pattern)));
        QVERIFY(rig.midi->canUndo());
        // Once the clip is gone (timeline undo), the pattern can go.
        QVERIFY(rig.session.history()->undo());
        QVERIFY(rig.midi->undo());
        QVERIFY(!rig.midi->pattern(QUuid::fromString(rig.pattern)));
    }

    void ppqChangesRescaleExactlyOrAreRefused()
    {
        Rig rig;
        const QString note = rig.note(480, 240, 60);
        QVERIFY(ok(rig.midi->setEvent(rig.pattern, {{QStringLiteral("controller"), QStringLiteral("cc:1")},
                                                    {QStringLiteral("tick"), 960}, {QStringLiteral("value"), 0.5}})));
        QVERIFY(ok(rig.midi->addRow(rig.pattern, {{QStringLiteral("steps"), 4}, {QStringLiteral("stepTicks"), QStringLiteral("240")}})));
        TimeMap *map = rig.session.timeMap();
        map->setTicksPerQuarter(1920);
        QCOMPARE(map->ticksPerQuarter(), 1920);
        QCOMPARE(rig.midi->ticksPerQuarter(), 1920);
        const MidiPattern *pattern = rig.midi->pattern(QUuid::fromString(rig.pattern));
        QCOMPARE(pattern->length, qint64(15360));
        QCOMPARE(pattern->notes.first().tick, qint64(960)); // the same beat position
        QCOMPARE(pattern->notes.first().duration, qint64(480));
        QCOMPARE(pattern->events.first().tick, qint64(1920));
        QCOMPARE(pattern->rows.first().stepTicks, qint64(480));
        // The history moved with it: undo restores rescaled content.
        QVERIFY(rig.midi->undo());
        QVERIFY(rig.midi->undo());
        QCOMPARE(rig.midi->pattern(QUuid::fromString(rig.pattern))->notes.first().tick, qint64(960));
        QVERIFY(rig.midi->redo());
        QVERIFY(rig.midi->redo());
        // 7 PPQ cannot hold tick 960 of 1920 (a half tick): refused, nothing moves.
        QSignalSpy rejected(map, &TimeMap::rejected);
        map->setTicksPerQuarter(7);
        QCOMPARE(rejected.size(), 1);
        QCOMPARE(map->ticksPerQuarter(), 1920);
        QCOMPARE(rig.midi->ticksPerQuarter(), 1920);
        QCOMPARE(rig.midi->pattern(QUuid::fromString(rig.pattern))->notes.first().tick, qint64(960));
        Q_UNUSED(note);
    }

    void sessionRoundTripKeepsMidiAndItsSchedule()
    {
        QTemporaryDir dir;
        Rig rig;
        const QString note = rig.note(960, 480, 62, 1234);
        QVERIFY(ok(rig.midi->setEvent(rig.pattern, {{QStringLiteral("controller"), QStringLiteral("notePressure")},
                                                    {QStringLiteral("noteId"), note},
                                                    {QStringLiteral("raw"), QStringLiteral("4000000001")}})));
        QVERIFY(ok(rig.session.insertPattern(rig.track, rig.pattern, QStringLiteral("0"))));
        const QString file = dir.filePath(QStringLiteral("song.json"));
        QVERIFY(rig.session.save(file));
        AudioSession reopened;
        QVERIFY2(reopened.open(file), qPrintable(reopened.lastError()));
        QVERIFY(reopened.midi()->content() == rig.midi->content());
        QCOMPARE(events(*reopened.compileGraph(Rate, true), rig.channel, 0, 200000),
                 events(*rig.graph(), rig.channel, 0, 200000));
        QVERIFY(!events(*rig.graph(), rig.channel, 0, 200000).isEmpty());
        // The MIDI edit history is the model's own; undo leaves the timeline clip.
        QVERIFY(rig.midi->deleteNotes(rig.pattern, {note}).value(QStringLiteral("ok")).toBool());
        QVERIFY(events(*rig.graph(), rig.channel, 0, 200000).isEmpty());
        QVERIFY(rig.midi->undo());
        QCOMPARE(events(*rig.graph(), rig.channel, 0, 200000).size(), 3); // on, per-note pressure, off
    }
};

QTEST_MAIN(TestMidiSchedule)
#include "tst_midi_schedule.moc"
