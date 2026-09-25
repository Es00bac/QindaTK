// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-064 on a live engine (silent built-in devices): launches stay quantized
// by the device thread however late the GUI posts them, queued and playing
// are distinct, quantized stops land on the grid, audition notes and
// launched clips end on note-off, all-notes-off, transport stop and device
// loss, and none of it allocates on the device thread. A stop before the
// boundary cancels a queued launch, destroying a session ends its clips, and
// a take recorded from the MIDI input becomes a pattern in its slot.
#include "audio_engine_p.h"
#include "audio_session.h"

#if defined(QINDATK_HAVE_ALSA)
#include <alsa/asoundlib.h>
#endif

#include <QSignalSpy>
#include <QThread>
#include <QtTest>

using namespace QindaTK;

#include "audio_allocation_probe.h"

namespace {

constexpr int Rate = 48000;
constexpr qint64 Beat = 24000; // 120 BPM


struct Rig {
    AudioEngine engine;
    AudioSession session;
    TransportModel *transport = engine.transport();
    MidiSequenceModel *midi = session.midi();
    MidiLauncherModel *launcher = session.launcher();
    QString track;
    QString cells[2]; // not "slots": a Qt keyword macro
    Rig()
    {
        session.setEngine(&engine);
        track = session.addMidiTrack(QStringLiteral("Synth"));
        for (int i = 0; i < 2; ++i) {
            const QString pattern = midi->addPattern(QStringLiteral("P%1").arg(i)).value(QStringLiteral("patternId")).toString();
            midi->insertNote(pattern, {{QStringLiteral("tick"), QStringLiteral("0")},
                                       {QStringLiteral("duration"), QStringLiteral("960")},
                                       {QStringLiteral("pitch"), 60 + i}});
            const QString scene = midi->addScene({}).value(QStringLiteral("sceneId")).toString();
            cells[i] = midi->setSlot(track, scene, pattern).value(QStringLiteral("slotId")).toString();
        }
    }
    QString apply(const QString &device, const QString &in, const QString &out)
    {
        QSignalSpy spy(&engine, &AudioEngine::configurationApplied);
        engine.applyConfiguration(QStringLiteral("test"), {{QStringLiteral("deviceId"), device},
                                                           {QStringLiteral("sampleRate"), Rate},
                                                           {QStringLiteral("bufferSize"), 256},
                                                           {QStringLiteral("inputLayout"), in},
                                                           {QStringLiteral("outputLayout"), out}});
        if (spy.isEmpty() && !spy.wait(5000)) return QStringLiteral("timeout");
        return spy.last().at(1).toString();
    }
    bool play()
    {
        if (!QTest::qWaitFor([this] { return transport->canPlay(); }, 5000)) return false;
        transport->requestPlay();
        return QTest::qWaitFor([this] { return transport->playing(); }, 5000);
    }
};

// The engine events of one kind: (serial, slot key, sample).
QList<QList<qint64>> ofKind(const QSignalSpy &spy, int kind)
{
    QList<QList<qint64>> list;
    for (const auto &args : spy)
        if (args.at(0).toInt() == kind)
            list.push_back({qint64(args.at(1).toULongLong()), qint64(args.at(3).toULongLong()), args.at(4).toLongLong()});
    return list;
}

} // namespace

class TestMidiEngine : public QObject {
    Q_OBJECT
private slots:
    void launchesStayQuantizedUnderADelayedUi()
    {
        Rig rig;
        QCOMPARE(rig.apply(QStringLiteral("builtin:null"), QStringLiteral("None"), QStringLiteral("Stereo")),
                 QStringLiteral("accepted"));
        QVERIFY(rig.play());
        QSignalSpy events(&rig.engine, &AudioEngine::midiLaunchEvent);
        QSignalSpy acks(rig.launcher, &MidiLauncherModel::launchAcknowledged);
        QString stateWhenAccepted;
        connect(rig.launcher, &MidiLauncherModel::launchAcknowledged, this,
                [&](const QString &id, const QString &state) {
                    if (id == QLatin1String("r3") && state == QLatin1String("accepted"))
                        stateWhenAccepted = rig.launcher->slotState(rig.cells[0]).value(QStringLiteral("state")).toString();
                });

        // A busy GUI thread posts three launches between long stalls; the
        // device thread quantizes each when it arrives, never from GUI time.
        g_probeThread.store(rig.engine.realtimeThreadToken());
        g_deviceAllocations.store(0);
        g_probing.store(true);
        QVERIFY(rig.launcher->launch(QStringLiteral("r1"), rig.cells[0], QStringLiteral("beat")));
        QThread::msleep(40);
        QVERIFY(rig.launcher->launch(QStringLiteral("r2"), rig.cells[1], QStringLiteral("beat")));
        QThread::msleep(40);
        QVERIFY(rig.launcher->launch(QStringLiteral("r3"), rig.cells[0], QStringLiteral("beat")));
        QTRY_VERIFY_WITH_TIMEOUT(rig.launcher->playingSlots()
                                     == QStringList{rig.cells[0].toLower()}, 3000);
        QTRY_VERIFY(rig.engine.launchedClips() == 1);
        g_probing.store(false);
#if defined(QINDATK_ALLOCATION_PROBE)
        QCOMPARE(g_deviceAllocations.load(), 0);
#endif
        const auto queued = ofKind(events, MidiEngineEvent::Queued);
        const auto started = ofKind(events, MidiEngineEvent::Started);
        QCOMPARE(queued.size(), 3);
        for (const auto &entry : queued) QCOMPARE(entry.at(2) % Beat, qint64(0)); // on the beat grid
        QVERIFY(!started.isEmpty());
        for (const auto &entry : started) { // every start happens exactly at its queued sample
            const auto match = std::find_if(queued.begin(), queued.end(),
                                            [&](const QList<qint64> &q) { return q.at(0) == entry.at(0); });
            QVERIFY(match != queued.end());
            QCOMPARE(entry.at(2), match->at(2));
        }
        QCOMPARE(started.last().at(0), queued.last().at(0)); // the last request wins the lane
        QCOMPARE(stateWhenAccepted, QStringLiteral("queued")); // queued is reported apart from playing
        QCOMPARE(rig.launcher->slotState(rig.cells[0]).value(QStringLiteral("state")).toString(), QStringLiteral("playing"));
        // Every request got exactly one answer, and each replaced one was canceled or started.
        QStringList answered;
        for (const auto &args : acks)
            if (args.at(1).toString() == QLatin1String("accepted")) answered << args.at(0).toString();
        QCOMPARE(answered, (QStringList{QStringLiteral("r1"), QStringLiteral("r2"), QStringLiteral("r3")}));
        QCOMPARE(ofKind(events, MidiEngineEvent::Canceled).size() + started.size(), 3);
    }

    void quantizedStopLandsOnTheGrid()
    {
        Rig rig;
        QCOMPARE(rig.apply(QStringLiteral("builtin:null"), QStringLiteral("None"), QStringLiteral("Stereo")),
                 QStringLiteral("accepted"));
        QVERIFY(rig.play());
        QSignalSpy events(&rig.engine, &AudioEngine::midiLaunchEvent);
        QVERIFY(rig.launcher->launch(QStringLiteral("go"), rig.cells[1], QStringLiteral("none")));
        QTRY_COMPARE(rig.engine.launchedClips(), 1);
        QTRY_VERIFY(!rig.launcher->playingSlots().isEmpty()); // the GUI has drained the launch's events
        QSignalSpy acks(rig.launcher, &MidiLauncherModel::launchAcknowledged);
        QVERIFY(rig.launcher->stop(QStringLiteral("halt"), rig.cells[1], QStringLiteral("beat")));
        QTRY_COMPARE(acks.size(), 1);
        QCOMPARE(acks.first().at(0).toString(), QStringLiteral("halt"));
        QCOMPARE(acks.first().at(1).toString(), QStringLiteral("accepted"));
        QTRY_VERIFY_WITH_TIMEOUT(!ofKind(events, MidiEngineEvent::Stopped).isEmpty(), 3000);
        const auto stopped = ofKind(events, MidiEngineEvent::Stopped);
        QCOMPARE(stopped.first().at(2) % Beat, qint64(0));
        QCOMPARE(stopped.first().at(2), ofKind(events, MidiEngineEvent::StopQueued).first().at(2));
        QTRY_COMPARE(rig.engine.launchedClips(), 0);
        QCOMPARE(rig.launcher->slotState(rig.cells[1]).value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
        // An unknown slot is refused at once, with its answer.
        QVERIFY(!rig.launcher->launch(QStringLiteral("none"), QUuid::createUuid().toString(QUuid::WithoutBraces)));
    }

    void auditionAndStopsNeverLeaveAStuckNote()
    {
        Rig rig;
        QCOMPARE(rig.apply(QStringLiteral("builtin:null"), QStringLiteral("None"), QStringLiteral("Stereo")),
                 QStringLiteral("accepted"));
        g_probeThread.store(rig.engine.realtimeThreadToken());
        g_deviceAllocations.store(0);
        g_probing.store(true);
        QVERIFY(rig.engine.auditionNoteOn(60, 0.8));
        QVERIFY(rig.engine.auditionNoteOn(64, 0.8));
        QTRY_COMPARE(rig.engine.auditionVoices(), 2); // audition sounds with the transport stopped
        QVERIFY(rig.engine.auditionNoteOff(60));
        QTRY_COMPARE(rig.engine.auditionVoices(), 1);
        QVERIFY(rig.engine.allNotesOff());
        QTRY_COMPARE(rig.engine.auditionVoices(), 0);
        // A transport stop is an all-notes-off for audition and launched clips.
        QVERIFY(rig.play());
        QVERIFY(rig.engine.auditionNoteOn(67, 0.5));
        QVERIFY(rig.launcher->launch(QStringLiteral("go"), rig.cells[0], QStringLiteral("none")));
        QTRY_COMPARE(rig.engine.auditionVoices(), 1);
        QTRY_COMPARE(rig.engine.launchedClips(), 1);
        rig.transport->requestStop();
        QTRY_VERIFY(!rig.transport->playing());
        QTRY_COMPARE(rig.engine.auditionVoices(), 0);
        QCOMPARE(rig.engine.launchedClips(), 0);
        QTRY_VERIFY(rig.launcher->playingSlots().isEmpty());
        g_probing.store(false);
#if defined(QINDATK_ALLOCATION_PROBE)
        QCOMPARE(g_deviceAllocations.load(), 0);
#endif
    }

    void deviceLossEndsEveryNoteAndClip()
    {
        Rig rig;
        const QString loopback = QStringLiteral("builtin:loopback");
        QCOMPARE(rig.apply(loopback, QStringLiteral("Mono"), QStringLiteral("Mono")), QStringLiteral("accepted"));
        QVERIFY(rig.play());
        QVERIFY(rig.launcher->launch(QStringLiteral("go"), rig.cells[0], QStringLiteral("none")));
        QVERIFY(rig.engine.auditionNoteOn(72, 1.0));
        QTRY_COMPARE(rig.engine.launchedClips(), 1);
        QTRY_COMPARE(rig.engine.auditionVoices(), 1);
        QSignalSpy reset(&rig.engine, &AudioEngine::midiReset);
        QVERIFY(rig.engine.setBuiltinDeviceConnected(loopback, false));
        QTRY_COMPARE(rig.engine.stateName(), QStringLiteral("lost"));
        QTRY_COMPARE(reset.size(), 1);
        QCOMPARE(rig.engine.auditionVoices(), 0);
        QCOMPARE(rig.engine.launchedClips(), 0);
        QVERIFY(rig.launcher->playingSlots().isEmpty());
        QVERIFY(!rig.engine.auditionNoteOn(60)); // nothing to play on
        // Recovery never resumes a note or a clip.
        QVERIFY(rig.engine.setBuiltinDeviceConnected(loopback, true));
        QCOMPARE(rig.apply(loopback, QStringLiteral("Mono"), QStringLiteral("Mono")), QStringLiteral("accepted"));
        QVERIFY(rig.play());
        QTest::qWait(100);
        QCOMPARE(rig.engine.auditionVoices(), 0);
        QCOMPARE(rig.engine.launchedClips(), 0);
        // Detaching the session from its engine stops what it started.
        QVERIFY(rig.launcher->launch(QStringLiteral("again"), rig.cells[1], QStringLiteral("none")));
        QTRY_COMPARE(rig.engine.launchedClips(), 1);
        rig.session.setEngine(nullptr);
        QTRY_COMPARE(rig.engine.launchedClips(), 0);
    }

    void aStopBeforeTheBoundaryCancelsTheLaunch()
    {
        Rig rig;
        QCOMPARE(rig.apply(QStringLiteral("builtin:null"), QStringLiteral("None"), QStringLiteral("Stereo")),
                 QStringLiteral("accepted"));
        QVERIFY(rig.play());
        QTRY_VERIFY(rig.transport->positionSample().toLongLong() > 2400); // past the bar line at 0
        QSignalSpy acks(rig.launcher, &MidiLauncherModel::launchAcknowledged);
        QVERIFY(rig.launcher->launch(QStringLiteral("go"), rig.cells[0], QStringLiteral("bar"))); // ~2 s away
        QVERIFY(rig.launcher->stop(QStringLiteral("halt"), rig.cells[0], QStringLiteral("bar")));
        QTRY_COMPARE(acks.size(), 3);
        QCOMPARE(acks.at(0).at(0).toString(), QStringLiteral("go"));
        QCOMPARE(acks.at(0).at(1).toString(), QStringLiteral("accepted"));
        QCOMPARE(acks.at(1).at(0).toString(), QStringLiteral("go"));
        QCOMPARE(acks.at(1).at(1).toString(), QStringLiteral("canceled"));
        QCOMPARE(acks.at(2).at(0).toString(), QStringLiteral("halt"));
        QCOMPARE(acks.at(2).at(1).toString(), QStringLiteral("accepted"));
        QCOMPARE(acks.at(2).at(2).toString(), QStringLiteral("Nothing was playing"));
        QCOMPARE(rig.launcher->slotState(rig.cells[0]).value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
        QTest::qWait(300);
        QCOMPARE(rig.engine.launchedClips(), 0);
    }

    void destroyingASessionEndsItsClips()
    {
        AudioEngine engine;
        auto session = std::make_unique<AudioSession>();
        session->setEngine(&engine);
        const QString track = session->addMidiTrack(QStringLiteral("Synth"));
        MidiSequenceModel *midi = session->midi();
        const QString pattern = midi->addPattern(QStringLiteral("P")).value(QStringLiteral("patternId")).toString();
        const QString scene = midi->addScene({}).value(QStringLiteral("sceneId")).toString();
        const QString cell = midi->setSlot(track, scene, pattern).value(QStringLiteral("slotId")).toString();
        QSignalSpy applied(&engine, &AudioEngine::configurationApplied);
        engine.applyConfiguration(QStringLiteral("test"), {{QStringLiteral("deviceId"), QStringLiteral("builtin:null")},
                                                           {QStringLiteral("sampleRate"), Rate},
                                                           {QStringLiteral("bufferSize"), 256},
                                                           {QStringLiteral("outputLayout"), QStringLiteral("Stereo")}});
        QTRY_COMPARE(applied.size(), 1);
        QTRY_VERIFY(engine.transport()->canPlay());
        engine.transport()->requestPlay();
        QTRY_VERIFY(engine.transport()->playing());
        QVERIFY(session->launcher()->launch(QStringLiteral("go"), cell, QStringLiteral("none")));
        QVERIFY(engine.auditionNoteOn(64));
        QTRY_COMPARE(engine.launchedClips(), 1);
        session.reset(); // a page torn down while the app's engine lives on
        QTRY_COMPARE(engine.launchedClips(), 0);
        QTRY_COMPARE(engine.auditionVoices(), 0);
        QVERIFY(engine.transport()->playing()); // only what the session started ended
    }

    void aRecordedTakeBecomesThePatternOfItsSlot()
    {
        Rig rig;
        QCOMPARE(rig.apply(QStringLiteral("builtin:null"), QStringLiteral("None"), QStringLiteral("Stereo")),
                 QStringLiteral("accepted"));
        QVERIFY(rig.play());
        const QString before = rig.midi->scenes().first().toMap().value(QStringLiteral("slots")).toList().first().toMap()
                                   .value(QStringLiteral("patternId")).toString();
        QSignalSpy acks(rig.launcher, &MidiLauncherModel::launchAcknowledged);
        QSignalSpy finished(rig.launcher, &MidiLauncherModel::recordingFinished);
        QVERIFY(rig.launcher->record(QStringLiteral("rec"), rig.cells[0], QStringLiteral("beat")));
        QCOMPARE(rig.launcher->recordingSlot(), rig.cells[0].toLower());
        QCOMPARE(rig.launcher->slotState(rig.cells[0]).value(QStringLiteral("state")).toString(), QStringLiteral("recording"));
        QCOMPARE(rig.launcher->slotList().first().toMap().value(QStringLiteral("state")).toString(), QStringLiteral("recording"));
        QTRY_COMPARE(acks.size(), 1);
        QCOMPARE(acks.first().at(1).toString(), QStringLiteral("accepted"));
        const qint64 start = rig.launcher->slotState(rig.cells[0]).value(QStringLiteral("startSample")).toLongLong();
        QCOMPARE(start % Beat, qint64(0)); // the engine quantized the take's start
        QTRY_VERIFY(rig.transport->positionSample().toLongLong() > start);

        // Notes arrive through the session's MIDI input path (7-bit velocities).
        MidiInputPort *input = rig.session.midiInput();
        input->receiveNote(60, 100);
        QTest::qWait(150);
        input->receiveNote(64, 90);
        QTest::qWait(150);
        input->receiveNote(60, 0);
        QTest::qWait(100);
        QVERIFY(rig.launcher->stopRecording(QStringLiteral("end"), QStringLiteral("beat")));
        QTRY_COMPARE(finished.size(), 1); // 64 is still held: it ends with the take
        const QString taken = finished.first().at(1).toString();
        QVERIFY(!taken.isEmpty());
        QCOMPARE(finished.first().at(2).toInt(), 2);
        const MidiPattern *pattern = rig.midi->pattern(QUuid::fromString(taken));
        QVERIFY(pattern);
        QCOMPARE(pattern->length % 960, qint64(0)); // beat start, beat stop: whole beats
        QCOMPARE(pattern->notes.size(), 2);
        const MidiNote &first = pattern->notes.at(0);
        const MidiNote &second = pattern->notes.at(1);
        QCOMPARE(first.pitch, 60);
        QCOMPARE(first.velocity, Midi::velocityFrom7(100));
        QCOMPARE(second.pitch, 64);
        // At least 150 ms (288 ticks) apart, stamped to the device period (256 frames, ~10 ticks).
        QVERIFY2(second.tick - first.tick >= 278 && second.tick - first.tick < 2000,
                 qPrintable(QString::number(second.tick - first.tick)));
        QVERIFY(first.end() > second.tick && first.end() < second.end());
        QCOMPARE(second.end(), pattern->length);
        QCOMPARE(rig.midi->undoText(), QStringLiteral("Record clip"));
        QCOMPARE(rig.launcher->slotList().first().toMap().value(QStringLiteral("patternId")).toString(), taken);
        QVERIFY(rig.midi->undo()); // one step restores the slot's previous pattern
        QCOMPARE(rig.launcher->slotList().first().toMap().value(QStringLiteral("patternId")).toString(), before);
        QVERIFY(!rig.midi->pattern(QUuid::fromString(taken)));
        QCOMPARE(input->heldNotes(), 1);
        input->receiveNote(64, 0);
        QCOMPARE(input->heldNotes(), 0);
        QTRY_COMPARE(rig.engine.auditionVoices(), 0);
    }

    void theMidiInputPortPlaysAndRecordsFromASequencerClient()
    {
#if !defined(QINDATK_HAVE_ALSA)
        QSKIP("Built without the ALSA sequencer");
#else
        Rig rig;
        QCOMPARE(rig.apply(QStringLiteral("builtin:null"), QStringLiteral("None"), QStringLiteral("Stereo")),
                 QStringLiteral("accepted"));
        MidiInputPort *input = rig.session.midiInput();
        if (!input->openPort()) QSKIP("No ALSA sequencer here");
        snd_seq_t *peer = nullptr;
        if (snd_seq_open(&peer, "default", SND_SEQ_OPEN_OUTPUT, 0) < 0) QSKIP("No ALSA sequencer here");
        const int port = snd_seq_create_simple_port(peer, "test keyboard", SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
                                                    SND_SEQ_PORT_TYPE_MIDI_GENERIC);
        QVERIFY(port >= 0);
        QVERIFY2(input->connectPort(QStringLiteral("%1:%2").arg(snd_seq_client_id(peer)).arg(port)),
                 qPrintable(input->lastError()));
        const auto send = [&](int pitch, int velocity) {
            snd_seq_event_t event;
            snd_seq_ev_clear(&event);
            if (velocity > 0) snd_seq_ev_set_noteon(&event, 0, pitch, velocity);
            else snd_seq_ev_set_noteoff(&event, 0, pitch, 0);
            snd_seq_ev_set_source(&event, port);
            snd_seq_ev_set_subs(&event);
            snd_seq_ev_set_direct(&event);
            return snd_seq_event_output_direct(peer, &event) >= 0;
        };
        QVERIFY(rig.play());
        QSignalSpy finished(rig.launcher, &MidiLauncherModel::recordingFinished);
        QVERIFY(rig.launcher->record(QStringLiteral("rec"), rig.cells[1], QStringLiteral("none")));
        QTRY_VERIFY(!rig.launcher->recordingSlot().isEmpty());
        QTest::qWait(50);
        QVERIFY(send(67, 127));
        QTRY_COMPARE(input->heldNotes(), 1);
        QTRY_COMPARE(rig.engine.auditionVoices(), 1); // the keyboard sounds through the test instrument
        QTest::qWait(100);
        QVERIFY(send(67, 0));
        QTRY_COMPARE(input->heldNotes(), 0);
        QVERIFY(rig.launcher->stopRecording(QStringLiteral("end"), QStringLiteral("none")));
        QTRY_COMPARE(finished.size(), 1);
        const MidiPattern *pattern = rig.midi->pattern(QUuid::fromString(finished.first().at(1).toString()));
        QVERIFY(pattern);
        QCOMPARE(pattern->notes.size(), 1);
        QCOMPARE(pattern->notes.first().pitch, 67);
        QCOMPARE(pattern->notes.first().velocity, quint16(65535));
        QVERIFY(pattern->notes.first().duration >= 180); // held at least 100 ms (192 ticks)
        // Closing the port releases what it holds.
        QVERIFY(send(48, 64));
        QTRY_COMPARE(input->heldNotes(), 1);
        input->closePort();
        QCOMPARE(input->heldNotes(), 0);
        QTRY_COMPARE(rig.engine.auditionVoices(), 0);
        snd_seq_close(peer);
#endif
    }
};

QTEST_MAIN(TestMidiEngine)
#include "tst_midi_engine.moc"
