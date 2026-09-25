// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-064 (D-153) on the device-thread processor, driven block by block with
// no device: launched clips, quantized launches and stops, and MIDI takes
// follow transport loop wraps and seeks; an explicit stop cancels a waiting
// launch; recording stamps audition notes with take-relative ticks.
#include "audio_engine_p.h"
#include "audio_session.h"

#include <QtTest>
#include <cmath>
#include <vector>

using namespace QindaTK;

namespace {

constexpr int Rate = 48000;
constexpr qint64 Bar = 96000; // 120 BPM, 4/4
constexpr int Block = 256;

struct Bench {
    AudioSession session;
    MidiSequenceModel *midi = session.midi();
    QString track = session.addMidiTrack(QStringLiteral("Synth"));
    QString pattern = midi->addPattern(QStringLiteral("P"), QStringLiteral("3840")).value(QStringLiteral("patternId")).toString();
    QString slot;
    AudioRenderGraphPtr graph;
    std::unique_ptr<AudioEngineProcessor> processor;
    std::vector<float> rendered; // the mono sum of every rendered frame, in device order
    std::vector<qint64> positions; // transport position of each rendered frame
    quint64 serial = 0;

    Bench()
    {
        midi->insertNote(pattern, {{QStringLiteral("tick"), QStringLiteral("0")},
                                   {QStringLiteral("duration"), QStringLiteral("480")}, {QStringLiteral("pitch"), 69}});
        const QString scene = midi->addScene(QStringLiteral("A")).value(QStringLiteral("sceneId")).toString();
        slot = midi->setSlot(track, scene, pattern).value(QStringLiteral("slotId")).toString();
        graph = session.compileGraph(Rate);
        AudioStreamFormat format;
        format.sampleRate = Rate;
        format.bufferSize = Block;
        format.outputChannels = 2;
        processor = std::make_unique<AudioEngineProcessor>(format, EngineTransportState{});
        processor->offer(graph.get());
    }
    void transport(int kind, qint64 position = 0, qint64 start = 0, qint64 end = 0, bool enabled = false)
    {
        EngineCommand command;
        command.serial = ++serial;
        command.kind = kind;
        command.position = position;
        command.rangeStart = start;
        command.rangeEnd = end;
        command.enabled = enabled;
        QVERIFY(processor->post(command));
    }
    quint64 midiCommand(int kind, MidiQuantize quantize, int pitch = 0)
    {
        MidiCommand command;
        command.serial = ++serial;
        command.kind = kind;
        command.lane = Midi::key(QUuid::fromString(track));
        command.slot = Midi::key(QUuid::fromString(slot));
        command.quantize = int(quantize);
        command.pitch = pitch;
        command.velocity = 40000;
        processor->postMidi(command);
        return command.serial;
    }
    void run(qint64 frames)
    {
        std::vector<float> out(std::size_t(Block) * 2);
        while (frames > 0) {
            const int count = int(std::min<qint64>(Block, frames));
            const qint64 before = processor->position();
            processor->process(nullptr, out.data(), count);
            // Positions inside a block follow the loop wrap (a block may hold one).
            const bool wrapped = processor->position() < before + count && processor->position() != before + count;
            for (int i = 0; i < count; ++i) {
                rendered.push_back(out[std::size_t(i) * 2]);
                positions.push_back(wrapped ? -1 : before + i);
            }
            frames -= count;
        }
    }
    // Runs until the transport reaches `position` (never across a loop wrap).
    void runTo(qint64 position) { run(position - processor->position()); }
    QList<MidiEngineEvent> events()
    {
        QList<MidiEngineEvent> list;
        MidiEngineEvent event;
        while (processor->takeMidiEvent(event)) list.push_back(event);
        return list;
    }
    static QList<MidiEngineEvent> ofKind(const QList<MidiEngineEvent> &events, int kind)
    {
        QList<MidiEngineEvent> list;
        for (const MidiEngineEvent &event : events)
            if (event.kind == kind) list.push_back(event);
        return list;
    }
    // Peak level of the frames rendered at transport positions [from, to).
    double peak(qint64 from, qint64 to, std::size_t since = 0) const
    {
        double level = 0.0;
        for (std::size_t i = since; i < rendered.size(); ++i)
            if (positions[i] >= from && positions[i] < to) level = std::max(level, double(std::abs(rendered[i])));
        return level;
    }
};

} // namespace

class TestMidiLaunch : public QObject {
    Q_OBJECT
private slots:
    void launchAndStopAtTheLoopEndFireAfterTheWrap()
    {
        Bench bench;
        bench.transport(EngineCommand::Loop, 0, 0, 2 * Bar, true); // a bar-aligned two-bar loop
        bench.transport(EngineCommand::Play);
        bench.runTo(150000); // the last bar of the loop
        const quint64 launch = bench.midiCommand(MidiCommand::Launch, MidiQuantize::Bar);
        bench.run(Block);
        auto events = bench.events();
        QCOMPARE(Bench::ofKind(events, MidiEngineEvent::Queued).size(), 1);
        QCOMPARE(Bench::ofKind(events, MidiEngineEvent::Queued).first().sample, 2 * Bar); // the loop end
        bench.run(Bar); // across the wrap
        events = bench.events();
        // Re-quantized from the loop start and reported again, then started there.
        const auto requeued = Bench::ofKind(events, MidiEngineEvent::Queued);
        QCOMPARE(requeued.size(), 1);
        QCOMPARE(requeued.first().serial, launch);
        QCOMPARE(requeued.first().sample, qint64(0));
        const auto started = Bench::ofKind(events, MidiEngineEvent::Started);
        QCOMPARE(started.size(), 1);
        QCOMPARE(started.first().serial, launch);
        QCOMPARE(started.first().sample, qint64(0));
        QCOMPARE(bench.processor->launchedClips(), 1);
        // It sounds: the pattern's note (tick 0, 12000 samples) from the loop start.
        QVERIFY(bench.peak(0, 12000) > 0.05);

        // A playing clip keeps sounding through every later wrap.
        bench.runTo(150000);
        std::size_t mark = bench.rendered.size();
        const quint64 stop = bench.midiCommand(MidiCommand::Stop, MidiQuantize::Bar);
        bench.run(Block);
        events = bench.events();
        QCOMPARE(Bench::ofKind(events, MidiEngineEvent::StopQueued).first().sample, 2 * Bar);
        bench.run(Bar);
        events = bench.events();
        QCOMPARE(Bench::ofKind(events, MidiEngineEvent::StopQueued).size(), 1); // re-quantized to the loop start
        QCOMPARE(Bench::ofKind(events, MidiEngineEvent::StopQueued).first().sample, qint64(0));
        const auto stopped = Bench::ofKind(events, MidiEngineEvent::Stopped);
        QCOMPARE(stopped.size(), 1);
        QCOMPARE(stopped.first().serial, stop);
        QCOMPARE(stopped.first().sample, qint64(0));
        QCOMPARE(bench.processor->launchedClips(), 0);
        QCOMPARE(bench.peak(MidiTestInstrument::releaseFrames(Rate), Bar, mark), 0.0); // stopped at the wrap: silent
    }

    void aPlayingClipFollowsABackwardSeek()
    {
        Bench bench;
        bench.transport(EngineCommand::Play);
        bench.runTo(50000);
        bench.midiCommand(MidiCommand::Launch, MidiQuantize::Bar); // starts at bar 2 (sample 96000)
        bench.runTo(150000);
        QCOMPARE(bench.processor->launchedClips(), 1);
        // Seek before the clip's start: it keeps its musical progress (tick 6000
        // - 3840 = 2160 into its pattern) and is heard from the new position, so
        // its next note falls 3840 - 2160 = 1680 ticks after the seek.
        const std::size_t mark = bench.rendered.size();
        bench.transport(EngineCommand::Seek, 10000);
        bench.run(Bar);
        const qint64 next = 10000 + 1680 * 25;
        QCOMPARE(bench.peak(10000, next, mark), 0.0);
        QVERIFY(bench.peak(next, next + 12000, mark) > 0.05);
        QCOMPARE(bench.processor->launchedClips(), 1);
    }

    void stopBeforeTheBoundaryCancelsTheQueuedLaunch()
    {
        Bench bench;
        bench.transport(EngineCommand::Play);
        bench.runTo(1000);
        const quint64 launch = bench.midiCommand(MidiCommand::Launch, MidiQuantize::Bar);
        const quint64 stop = bench.midiCommand(MidiCommand::Stop, MidiQuantize::Bar);
        bench.run(Bar);
        const auto events = bench.events();
        QCOMPARE(Bench::ofKind(events, MidiEngineEvent::Queued).first().serial, launch);
        const auto canceled = Bench::ofKind(events, MidiEngineEvent::Canceled);
        QCOMPARE(canceled.size(), 1);
        QCOMPARE(canceled.first().serial, launch);
        const auto stopped = Bench::ofKind(events, MidiEngineEvent::Stopped);
        QCOMPARE(stopped.size(), 1); // the stop's answer: nothing was playing
        QCOMPARE(stopped.first().serial, stop);
        QCOMPARE(stopped.first().slot, quint64(0));
        QVERIFY(Bench::ofKind(events, MidiEngineEvent::Started).isEmpty());
        QCOMPARE(bench.peak(0, 2 * Bar), 0.0);
    }

    void recordingStampsNotesFromTheEngineClock()
    {
        Bench bench;
        bench.transport(EngineCommand::Loop, 0, 0, 2 * Bar, true);
        bench.transport(EngineCommand::Play);
        bench.runTo(30000);
        const quint64 take = bench.midiCommand(MidiCommand::RecordStart, MidiQuantize::Beat);
        bench.midiCommand(MidiCommand::NoteOn, MidiQuantize::None, 55); // before the start: not recorded
        bench.run(Block);
        auto events = bench.events();
        QCOMPARE(Bench::ofKind(events, MidiEngineEvent::RecordQueued).first().sample, qint64(48000)); // the next beat
        QVERIFY(Bench::ofKind(events, MidiEngineEvent::NoteRecorded).isEmpty());
        bench.runTo(60000);
        bench.midiCommand(MidiCommand::NoteOn, MidiQuantize::None, 60);
        bench.runTo(72000);
        bench.midiCommand(MidiCommand::NoteOff, MidiQuantize::None, 60);
        bench.run(Block);
        events = bench.events();
        auto notes = Bench::ofKind(events, MidiEngineEvent::NoteRecorded);
        QCOMPARE(notes.size(), 2);
        QCOMPARE(notes.at(0).serial, take);
        QCOMPARE(notes.at(0).pitch, 60);
        QCOMPARE(notes.at(0).velocity, quint16(40000));
        QCOMPARE(notes.at(0).tick, (60000 - 48000) / 25); // take-relative, stamped at the applying period
        QCOMPARE(notes.at(1).tick, (72000 - 48000) / 25);
        QCOMPARE(notes.at(1).velocity, quint16(0));
        // Through the loop wrap the take keeps counting forward.
        bench.run(2 * Bar - 72000 - Block + 4800);
        bench.midiCommand(MidiCommand::NoteOn, MidiQuantize::None, 62);
        bench.run(Block);
        notes = Bench::ofKind(bench.events(), MidiEngineEvent::NoteRecorded);
        QCOMPARE(notes.size(), 1);
        const qint64 position = bench.processor->position() - Block;
        QCOMPARE(notes.first().tick, (2 * Bar + position - 48000) / 25);
        // The stop sizes the take to the next bar line.
        bench.midiCommand(MidiCommand::RecordStop, MidiQuantize::Bar);
        bench.run(Block);
        const auto stopped = Bench::ofKind(bench.events(), MidiEngineEvent::RecordStopped);
        QCOMPARE(stopped.size(), 1);
        QCOMPARE(stopped.first().tick, 3 * 3840 - 1920); // bar 3 of the running take, from beat 2
        bench.midiCommand(MidiCommand::RecordStop, MidiQuantize::Bar);
        bench.run(Block);
        QCOMPARE(Bench::ofKind(bench.events(), MidiEngineEvent::RecordStopped).first().code,
                 int(MidiEngineEvent::NotRecording));
    }
};

QTEST_MAIN(TestMidiLaunch)
#include "tst_midi_launch.moc"
