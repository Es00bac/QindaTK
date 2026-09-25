// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-066 plugins in the render graph: processed audio from real CLAP
// plugins (offline renders, exact samples), latency compensation across
// tracks and bypass, an instrument playing MIDI notes on their exact samples,
// and on a live engine (silent built-in devices): no allocation on the device
// thread, a crash and a hang during playback that leave the session playing,
// and unloading while the engine plays.
#include "audio_parameter_binding.h"
#include "audio_session.h"

#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cerrno>
#include <csignal>

using namespace QindaTK;

#include "audio_allocation_probe.h"

namespace {

constexpr int Rate = 48000;
constexpr qint64 TicksPerSample = TimelineTime::kDefaultTicksPerSecond / Rate;
const QString Gain = QStringLiteral("org.qindatk.test.gain");
const QString Synth = QStringLiteral("org.qindatk.test.synth");
const QString Faulty = QStringLiteral("org.qindatk.test.faulty");

bool ok(const QVariantMap &result) { return result.value(QStringLiteral("ok")).toBool(); }
bool alive(qint64 pid) { return pid > 0 && (::kill(pid_t(pid), 0) == 0 || errno != ESRCH); }
float ramp(qint64 frame) { return float(frame + 1) / 65536.0f; } // exact in float, never 0

struct Rig {
    QTemporaryDir dir;
    AudioEngine engine;
    AudioSession *session = new AudioSession;
    AudioPluginHost *plugins = session->plugins();
    TransportModel *transport = engine.transport();
    Rig()
    {
        plugins->registry()->setCacheFile(dir.filePath(QStringLiteral("cache.json")));
        plugins->registry()->setSearchPaths({QFileInfo(QStringLiteral(QINDATK_TEST_CLAP)).absolutePath()});
        plugins->registry()->scanNow();
        session->setMediaDirectory(dir.filePath(QStringLiteral("takes")));
    }
    ~Rig() { delete session; }
    QString track(const QString &name, qint64 frames)
    {
        const QString id = session->addTrack(name);
        AudioBuffer buffer;
        buffer.sampleRate = Rate;
        buffer.channels = 1;
        buffer.frames = frames;
        buffer.samples.resize(std::size_t(frames));
        for (qint64 i = 0; i < frames; ++i) buffer.samples[std::size_t(i)] = ramp(i);
        const QString file = dir.filePath(name + QStringLiteral(".wav"));
        if (id.isEmpty() || !AudioFile::writeWav(file, buffer) || !ok(session->importMedia(file, id))) return {};
        return QStringLiteral("track:") + id;
    }
    QString add(const QString &channel, const QString &pluginId)
    {
        const QVariantMap result = plugins->addPlugin(channel, pluginId);
        if (!ok(result)) qWarning("add %s: %s", qPrintable(pluginId), qPrintable(result.value(QStringLiteral("error")).toString()));
        return result.value(QStringLiteral("instanceId")).toString();
    }
    bool set(const QString &instance, int parameter, double value)
    {
        AudioParameterBinding *binding = plugins->parameterBinding(instance, parameter);
        return binding && binding->begin(value) && binding->commit(value);
    }
    AudioBufferPtr render(const QString &name, qint64 frames)
    {
        const QString file = dir.filePath(name);
        const QVariantMap result = session->renderToFile(file, QStringLiteral("0"), QString::number(frames * TicksPerSample));
        if (!ok(result)) qWarning("render: %s", qPrintable(result.value(QStringLiteral("error")).toString()));
        return ok(result) ? AudioFile::readWav(file) : nullptr;
    }
    QString state(const QString &instance) { return plugins->instance(instance).value(QStringLiteral("state")).toString(); }
    QString apply()
    {
        session->setEngine(&engine);
        QSignalSpy spy(&engine, &AudioEngine::configurationApplied);
        engine.applyConfiguration(QStringLiteral("test"), {{QStringLiteral("deviceId"), QStringLiteral("builtin:loopback")},
                                                           {QStringLiteral("sampleRate"), Rate},
                                                           {QStringLiteral("bufferSize"), 256},
                                                           {QStringLiteral("inputLayout"), QStringLiteral("Stereo")},
                                                           {QStringLiteral("outputLayout"), QStringLiteral("Stereo")}});
        if (spy.isEmpty() && !spy.wait(5000)) return QStringLiteral("timeout");
        return spy.last().at(1).toString();
    }
    bool play()
    {
        if (!QTest::qWaitFor([this] { return transport->canPlay(); }, 5000)) return false;
        transport->requestLoop(true, QStringLiteral("0"), QStringLiteral("96000"));
        transport->requestPlay();
        return QTest::qWaitFor([this] { return transport->playing(); }, 5000);
    }
    qint64 callbacks() const { return engine.statistics().value(QStringLiteral("callbacks")).toLongLong(); }
};

} // namespace

class TestAudioPluginEngine : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("QINDATK_PLUGIN_HOST", QINDATK_TEST_PLUGIN_HOST); }

    void pluginProcessesTheChannel()
    {
        Rig rig;
        const QString channel = rig.track(QStringLiteral("A"), 6000);
        const QString id = rig.add(channel, Gain);
        QVERIFY(!id.isEmpty());
        QVERIFY(rig.set(id, 1, 0.5));
        const AudioBufferPtr out = rig.render(QStringLiteral("gain.wav"), 6000);
        QVERIFY(out && out->frames == 6000 && out->channels == 2);
        // The mono file plays on both lanes of the stereo track; the plugin
        // halves it (exact in float), sample for sample.
        for (qint64 i = 0; i < out->frames; ++i) {
            QCOMPARE(out->channel(0)[i], ramp(i) * 0.5f);
            QCOMPARE(out->channel(1)[i], ramp(i) * 0.5f);
        }
        const AudioPluginProcessorPtr processor = rig.plugins->instanceObject(id)->processor();
        QVERIFY(processor->processedBlocks() > 0);
        QCOMPARE(processor->lateBlocks(), quint64(0));
        // Bypassed, the channel is dry.
        QVERIFY(ok(rig.plugins->setBypassed(id, true)));
        const AudioBufferPtr dry = rig.render(QStringLiteral("dry.wav"), 6000);
        QVERIFY(dry);
        for (qint64 i = 0; i < dry->frames; ++i) QCOMPARE(dry->channel(0)[i], ramp(i));
    }

    void latencyIsCompensatedAcrossTheMix()
    {
        Rig rig;
        const QString a = rig.track(QStringLiteral("A"), 6000);
        const QString b = rig.track(QStringLiteral("B"), 6000);
        const QString id = rig.add(a, Gain);
        QVERIFY(rig.set(id, 2, 64)); // Latency: the plugin restarts with 64 frames
        QTRY_COMPARE(rig.plugins->instance(id).value(QStringLiteral("latencyFrames")).toInt(), 64);
        const AudioRenderGraphPtr graph = rig.session->compileGraph(Rate, true);
        QCOMPARE(graph->insertLatency(a), 64);
        QCOMPARE(graph->compensation(a), 0);
        QCOMPARE(graph->compensation(b), 64); // B waits for A at the master
        QCOMPARE(graph->pluginLatency(), 64);
        // A (delayed by the plugin) and B (delayed by compensation) meet at
        // the master aligned; the file render reads 64 frames ahead, so the
        // file lines up with the timeline: every sample is exactly 2 x ramp.
        const AudioBufferPtr out = rig.render(QStringLiteral("aligned.wav"), 6000);
        QVERIFY(out && out->frames == 6000);
        for (qint64 i = 0; i < out->frames; ++i) QCOMPARE(out->channel(0)[i], ramp(i) * 2.0f);
        // Bypass keeps the alignment: the dry path is delayed by the latency too.
        QVERIFY(ok(rig.plugins->setBypassed(id, true)));
        const AudioBufferPtr bypassed = rig.render(QStringLiteral("bypassed.wav"), 6000);
        QVERIFY(bypassed);
        for (qint64 i = 0; i < bypassed->frames; ++i) QCOMPARE(bypassed->channel(0)[i], ramp(i) * 2.0f);
    }

    void instrumentPlaysNotesOnTheirSamples()
    {
        Rig rig;
        const QString track = rig.session->addMidiTrack(QStringLiteral("Synth"));
        const QString channel = AudioMixer::trackChannelId(QUuid::fromString(track));
        MidiSequenceModel *midi = rig.session->midi();
        const QString pattern = midi->addPattern(QStringLiteral("P"), QStringLiteral("7680")).value(QStringLiteral("patternId")).toString();
        // 120 BPM, 960 PPQ at 48 kHz: tick 480 is sample 12000, tick 960 is 24000.
        midi->insertNote(pattern, {{QStringLiteral("tick"), QStringLiteral("480")}, {QStringLiteral("duration"), QStringLiteral("480")},
                                   {QStringLiteral("pitch"), 60}, {QStringLiteral("velocity16"), 65535}});
        QVERIFY(ok(rig.session->insertPattern(track, pattern)));
        const QString id = rig.add(channel, Synth);
        QVERIFY(!id.isEmpty());
        const AudioBufferPtr out = rig.render(QStringLiteral("synth.wav"), 36000);
        QVERIFY(out && out->frames == 36000);
        // Level 0.25 x velocity 1 exactly on [12000, 24000); the built-in test
        // instrument is silent while a plugin plays the track.
        for (qint64 i = 0; i < out->frames; ++i) {
            const float expected = i >= 12000 && i < 24000 ? 0.25f : 0.0f;
            QCOMPARE(out->channel(0)[i], expected);
            QCOMPARE(out->channel(1)[i], expected);
        }
    }

    void devicePlaybackAllocatesNothing()
    {
        Rig rig;
        QCOMPARE(rig.apply(), QStringLiteral("accepted"));
        const QString a = rig.track(QStringLiteral("A"), 96000);
        const QString gain = rig.add(a, Gain);
        QVERIFY(rig.set(gain, 2, 32));
        QTRY_COMPARE(rig.plugins->instance(gain).value(QStringLiteral("latencyFrames")).toInt(), 32);
        const QString track = rig.session->addMidiTrack(QStringLiteral("Synth"));
        MidiSequenceModel *midi = rig.session->midi();
        const QString pattern = midi->addPattern(QStringLiteral("P"), QStringLiteral("7680")).value(QStringLiteral("patternId")).toString();
        midi->insertNote(pattern, {{QStringLiteral("tick"), QStringLiteral("0")}, {QStringLiteral("duration"), QStringLiteral("960")},
                                   {QStringLiteral("pitch"), 64}});
        QVERIFY(ok(rig.session->insertPattern(track, pattern)));
        const QString synth = rig.add(AudioMixer::trackChannelId(QUuid::fromString(track)), Synth);
        QVERIFY(!synth.isEmpty());
        QVERIFY(rig.play());
        QTest::qWait(100); // first blocks: plugin processes warm up
        g_probeThread.store(rig.engine.realtimeThreadToken());
        g_deviceAllocations.store(0);
        g_probing.store(true);
        const quint64 before = rig.plugins->instanceObject(gain)->processor()->processedBlocks();
        QVERIFY(rig.set(gain, 1, 0.75)); // a parameter change during playback
        QTest::qWait(300);
        g_probing.store(false);
#if defined(QINDATK_ALLOCATION_PROBE)
        QCOMPARE(g_deviceAllocations.load(), 0);
#endif
        QVERIFY(rig.plugins->instanceObject(gain)->processor()->processedBlocks() > before);
        QVERIFY(rig.plugins->instanceObject(synth)->processor()->processedBlocks() > 0);
        rig.transport->requestStop();
        QTRY_VERIFY(!rig.transport->playing());
    }

    void crashAndHangDuringPlaybackLeaveTheSessionPlaying()
    {
        Rig rig;
        rig.plugins->setHangTimeoutMs(300);
        QCOMPARE(rig.apply(), QStringLiteral("accepted"));
        const QString a = rig.track(QStringLiteral("A"), 96000);
        const QString crash = rig.add(a, Faulty);
        const QString hang = rig.add(QStringLiteral("master"), Faulty);
        QVERIFY(!crash.isEmpty() && !hang.isEmpty());
        QSignalSpy failed(rig.plugins, &AudioPluginHost::instanceFailed);
        QVERIFY(rig.play());
        QVERIFY(rig.set(crash, 1, 1.0)); // sent with the next block: SIGSEGV
        QTRY_COMPARE(rig.state(crash), QStringLiteral("failed"));
        const QString crashError = rig.plugins->instance(crash).value(QStringLiteral("error")).toString();
        QVERIFY2(crashError.contains(QStringLiteral("crashed")), qPrintable(crashError));
        QVERIFY(rig.transport->playing());
        QVERIFY(rig.set(hang, 2, 1.0)); // process() never returns
        QTRY_COMPARE_WITH_TIMEOUT(rig.state(hang), QStringLiteral("failed"), 10000);
        QVERIFY(rig.plugins->instance(hang).value(QStringLiteral("error")).toString().contains(QStringLiteral("stopped responding")));
        QCOMPARE(failed.size(), 2);
        // The device kept running through both: callbacks continue, the
        // transport still plays, and failed inserts pass the signal.
        const qint64 callbacks = rig.callbacks();
        QTRY_VERIFY(rig.callbacks() > callbacks + 10);
        QVERIFY(rig.transport->playing());
        rig.transport->requestStop();
        QTRY_VERIFY(!rig.transport->playing());
    }

    void unloadWhileTheEnginePlays()
    {
        Rig rig;
        QCOMPARE(rig.apply(), QStringLiteral("accepted"));
        const QString a = rig.track(QStringLiteral("A"), 96000);
        const QString id = rig.add(a, Gain);
        const qint64 pid = rig.plugins->instance(id).value(QStringLiteral("processId")).toLongLong();
        QVERIFY(alive(pid));
        QVERIFY(rig.play());
        QTest::qWait(100);
        delete rig.session; // the engine keeps the last graph and plays on
        rig.session = nullptr;
        QVERIFY(!alive(pid));
        const qint64 callbacks = rig.callbacks();
        QTRY_VERIFY(rig.callbacks() > callbacks + 10);
        QVERIFY(rig.transport->playing());
        rig.transport->requestStop();
        QTRY_VERIFY(!rig.transport->playing());
    }
};

QTEST_MAIN(TestAudioPluginEngine)
#include "tst_audio_plugin_engine.moc"
