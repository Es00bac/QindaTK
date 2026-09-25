// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-064 on a live engine (silent built-in devices): launches stay quantized
// by the device thread however late the GUI posts them, queued and playing
// are distinct, quantized stops land on the grid, audition notes and
// launched clips end on note-off, all-notes-off, transport stop and device
// loss, and none of it allocates on the device thread.
#include "audio_engine_p.h"
#include "audio_session.h"

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
};

QTEST_MAIN(TestMidiEngine)
#include "tst_midi_engine.moc"
