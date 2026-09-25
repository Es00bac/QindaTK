// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-062 on a live engine. Every device is silent (built-in null and
// loopback): a route change during playback switches at a period boundary
// with no partial sample, the per-channel meters measure known post-fader
// levels, mixing with buses, sends and automation allocates nothing on the
// device thread, and a device port never falls back to another device.
#include "audio_parameter_binding.h"
#include "audio_session.h"

#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <functional>

using namespace QindaTK;

#include "audio_allocation_probe.h"

namespace {

constexpr int Rate = 48000;
constexpr int Period = 256;
constexpr qint64 TicksPerSample = TimelineTime::kDefaultTicksPerSecond / Rate;

float ramp(qint64 frame) { return float(frame + 1) / 65536.0f; } // exact, never 0
bool ok(const QVariantMap &result) { return result.value(QStringLiteral("ok")).toBool(); }
double dbfs(double linear) { return 20.0 * std::log10(linear); }

double peakOf(const AudioMeterModel *meter, int channel)
{
    return meter->levels().value(channel).toMap().value(QStringLiteral("peakDbfs")).toDouble();
}

bool silent(const AudioMeterModel *meter)
{
    return meter->levels().value(0).toMap().value(QStringLiteral("silent")).toBool();
}

struct Rig {
    QTemporaryDir dir;
    AudioEngine engine;
    AudioSession session;
    TransportModel *transport = engine.transport();
    AudioMixerModel *mixer = session.mixer();
    Rig()
    {
        session.setMediaDirectory(dir.filePath(QStringLiteral("takes")));
        session.setEngine(&engine);
    }
    QString apply(const QString &device, const QString &in, const QString &out)
    {
        QSignalSpy spy(&engine, &AudioEngine::configurationApplied);
        engine.applyConfiguration(QStringLiteral("test"), {{QStringLiteral("deviceId"), device},
                                                           {QStringLiteral("sampleRate"), Rate},
                                                           {QStringLiteral("bufferSize"), Period},
                                                           {QStringLiteral("inputLayout"), in},
                                                           {QStringLiteral("outputLayout"), out}});
        if (spy.isEmpty() && !spy.wait(5000)) return QStringLiteral("timeout");
        return spy.last().at(1).toString();
    }
    // A track playing `frames` of a mono file.
    QString track(const QString &name, qint64 frames, const std::function<float(qint64)> &value)
    {
        const QString id = session.addTrack(name);
        AudioBuffer buffer;
        buffer.sampleRate = Rate;
        buffer.channels = 1;
        buffer.frames = frames;
        buffer.samples.resize(std::size_t(frames));
        for (qint64 i = 0; i < frames; ++i) buffer.samples[std::size_t(i)] = value(i);
        const QString file = dir.filePath(name + QStringLiteral(".wav"));
        if (id.isEmpty() || !AudioFile::writeWav(file, buffer) || !ok(session.importMedia(file, id))) return {};
        return QStringLiteral("track:") + id;
    }
    bool play()
    {
        if (!QTest::qWaitFor([this] { return transport->canPlay(); }, 5000)) return false;
        transport->requestLoop(true, QStringLiteral("0"), QStringLiteral("96000"));
        transport->requestPlay();
        return QTest::qWaitFor([this] { return transport->playing(); }, 5000);
    }
};

} // namespace

class TestAudioMixerEngine : public QObject {
    Q_OBJECT
private slots:
    void routeChangesDuringPlaybackSwitchAtPeriodBoundaries()
    {
        Rig rig;
        QCOMPARE(rig.apply(QStringLiteral("builtin:loopback"), QStringLiteral("Mono"), QStringLiteral("Mono")),
                 QStringLiteral("accepted"));
        const QString source = rig.track(QStringLiteral("Source"), 96000, ramp);
        QVERIFY(!source.isEmpty());
        const QString takes = rig.session.addTrack(QStringLiteral("Takes"));
        QVERIFY(rig.session.setTrackArmed(takes, true));
        QTRY_VERIFY(rig.transport->canRecord());
        QSignalSpy recorded(&rig.session, &AudioSession::takeRecorded);
        rig.transport->requestRecord(true);
        QTRY_VERIFY(rig.transport->recording());
        QTest::qWait(150);
        QVERIFY(ok(rig.mixer->setOutput(source, QString()))); // disconnected while playing
        QTest::qWait(150);
        QVERIFY(ok(rig.mixer->setOutput(source, QStringLiteral("master"))));
        QTest::qWait(150);
        rig.transport->requestStop();
        QTRY_VERIFY(!rig.transport->playing());
        QTRY_COMPARE(int(recorded.size()), 1);

        // The take is what the device played one period earlier (loopback):
        // the source until the route changed, then exact silence, then the
        // source again. Every sample is one or the other, and both switches
        // fall on period boundaries: the graph changes only between periods.
        const QVariantMap take = recorded.first().at(2).toMap();
        QCOMPARE(take.value(QStringLiteral("recordedSample")).toString(), QStringLiteral("0"));
        const AudioBufferPtr data = AudioFile::readWav(take.value(QStringLiteral("path")).toString());
        QVERIFY(data && data->frames > Period * 60);
        const float *samples = data->channel(0);
        qint64 silentFrom = -1;
        qint64 silentTo = -1;
        qint64 silentCount = 0;
        int mismatches = 0;
        for (qint64 j = Period; j < data->frames; ++j) {
            if (samples[j] == ramp(j - Period)) continue;
            if (samples[j] == 0.0f) {
                if (silentFrom < 0) silentFrom = j;
                silentTo = j + 1;
                ++silentCount;
                continue;
            }
            if (++mismatches < 4) qWarning("take[%lld] = %.9g, want %.9g or 0", j, samples[j], ramp(j - Period));
        }
        QCOMPARE(mismatches, 0);
        QVERIFY2(silentFrom > Period && silentTo < data->frames, qPrintable(QStringLiteral("%1..%2").arg(silentFrom).arg(silentTo)));
        QCOMPARE(silentCount, silentTo - silentFrom); // one contiguous silence
        QCOMPARE(silentFrom % Period, qint64(0));
        QCOMPARE(silentTo % Period, qint64(0));
        QVERIFY(silentCount >= Period * 10);
    }

    void channelMetersMeasurePostFaderLevelsWithoutAllocating()
    {
        Rig rig;
        QCOMPARE(rig.apply(QStringLiteral("builtin:null"), QStringLiteral("None"), QStringLiteral("Stereo")),
                 QStringLiteral("accepted"));
        const QString one = rig.track(QStringLiteral("One"), 96000, [](qint64) { return 0.5f; });
        const QString two = rig.track(QStringLiteral("Two"), 96000, [](qint64) { return 0.25f; });
        const QString three = rig.track(QStringLiteral("Three"), 96000,
                                        [](qint64 i) { return float(0.125 * std::sin(0.05 * double(i))); });
        QVERIFY(!one.isEmpty() && !two.isEmpty() && !three.isEmpty());
        const QString bus = rig.mixer->addBus(QStringLiteral("Bus")).value(QStringLiteral("channelId")).toString();
        QVERIFY(ok(rig.mixer->setOutput(one, bus)));
        QVERIFY(ok(rig.mixer->addSend(two, bus)));
        AudioParameterBinding *busGain = rig.mixer->parameterBinding(bus, QStringLiteral("gain"));
        QVERIFY(busGain && busGain->begin(0.0) && busGain->commit(-6.0));
        // Per-sample pan automation on Three drives the automated path.
        const QString pan = rig.mixer->ensureParameter(three, QStringLiteral("pan"));
        for (const auto &[sample, value] : {std::pair{0, -1.0}, std::pair{96000, 1.0}})
            QVERIFY(ok(rig.session.automation()->applyEdit({{QStringLiteral("kind"), QStringLiteral("addPoint")},
                                                            {QStringLiteral("parameterId"), pan},
                                                            {QStringLiteral("ticks"), QString::number(sample * TicksPerSample)},
                                                            {QStringLiteral("value"), value}})));
        AudioMeterModel *oneMeter = rig.mixer->meter(one);
        QPointer<AudioMeterModel> busMeter = rig.mixer->meter(bus);
        QVERIFY(oneMeter && busMeter);
        QCOMPARE(rig.engine.channelMeter(one), oneMeter); // one model per channel, owned by the engine
        QVERIFY(rig.play());

        // Post-fader levels on audio time: One at 0.5 on both lanes; the bus
        // holds One plus Two's post-fader send, then its own -6 dB fader.
        const double busPeak = dbfs(0.75 * std::pow(10.0, -6.0 / 20.0));
        QTRY_COMPARE(oneMeter->channelCount(), 2);
        QTRY_VERIFY(std::abs(peakOf(oneMeter, 0) - dbfs(0.5)) < 1e-3);
        QTRY_VERIFY(std::abs(peakOf(busMeter, 1) - busPeak) < 1e-3);

        // Route, mute and fader changes while playing: the device thread only
        // adopts the next graph; it never allocates.
        g_probeThread.store(rig.engine.realtimeThreadToken());
        g_deviceAllocations.store(0);
        g_probing.store(true);
        QTest::qWait(100);
        QVERIFY(ok(rig.mixer->setOutput(three, bus)));
        QVERIFY(ok(rig.mixer->setMuted(bus, true)));
        QTest::qWait(100);
        QVERIFY(ok(rig.mixer->setMuted(bus, false)));
        QVERIFY(ok(rig.mixer->setOutput(three, QStringLiteral("master"))));
        QVERIFY(busGain->begin(-6.0) && busGain->preview(-12.0) && busGain->commit(-6.0));
        QTest::qWait(100);
        g_probing.store(false);
#if defined(QINDATK_ALLOCATION_PROBE)
        QCOMPARE(g_deviceAllocations.load(), 0);
#endif

        // A muted channel feeds nothing: its meter and its share of the bus fall away.
        QVERIFY(ok(rig.mixer->setMuted(one, true)));
        QTRY_VERIFY(silent(oneMeter));
        QTRY_VERIFY(std::abs(peakOf(busMeter, 0) - dbfs(0.25 * std::pow(10.0, -6.0 / 20.0))) < 1e-3);
        // A channel that leaves the mix has no measurement any more, and its
        // meter is released (the engine keeps meters of live channels only).
        QCOMPARE(busMeter->droppedBlocks(), qint64(0));
        QVERIFY(ok(rig.mixer->removeBus(bus)));
        QTRY_VERIFY(busMeter.isNull());
        QVERIFY(rig.mixer->undo()); // the bus is back: a new meter measures it again
        QPointer<AudioMeterModel> again = rig.mixer->meter(bus);
        QVERIFY(again);
        QTRY_VERIFY(again->channelCount() == 2);
        QVERIFY(rig.mixer->redo());
        QTRY_VERIFY(again.isNull());
        rig.engine.stop();
        QTRY_COMPARE(rig.engine.stateName(), QStringLiteral("stopped"));
        QCOMPARE(oneMeter->channelCount(), 0); // no stream, no channels
    }

    void largeMixesMeterTheMasterAndWatchedStrips()
    {
        // Audit (WP-062, D-113): more channels than meter slots. The master
        // and the strips a view watches are always metered; the rest explain.
        Rig rig;
        QCOMPARE(rig.apply(QStringLiteral("builtin:null"), QStringLiteral("None"), QStringLiteral("Stereo")),
                 QStringLiteral("accepted"));
        for (int i = 0; i < 130; ++i) QVERIFY(!rig.session.addTrack(QStringLiteral("Silent %1").arg(i)).isEmpty());
        const QString loud = rig.track(QStringLiteral("Loud"), 96000, [](qint64) { return 0.5f; });
        QVERIFY(!loud.isEmpty());
        QCOMPARE(rig.mixer->channelIds().size(), 132);
        AudioMeterModel *master = rig.mixer->meter(QStringLiteral("master"));
        AudioMeterModel *loudMeter = rig.mixer->meter(loud);
        QVERIFY(master && loudMeter);
        QVERIFY(rig.mixer->meterProblem(QStringLiteral("master")).isEmpty());
        QVERIFY2(rig.mixer->meterProblem(loud).contains(QStringLiteral("not metered")), qPrintable(rig.mixer->meterProblem(loud)));
        QVERIFY(rig.play());
        QTRY_VERIFY(std::abs(peakOf(master, 0) - dbfs(0.5)) < 1e-3); // the master, compiled last, is fed
        QCOMPARE(loudMeter->channelCount(), 0);
        // A strip that shows the channel watches it: the next graph meters it.
        QCOMPARE(rig.mixer->watchMeter(loud), loudMeter);
        QVERIFY(rig.mixer->meterProblem(loud).isEmpty());
        QTRY_VERIFY(std::abs(peakOf(loudMeter, 0) - dbfs(0.5)) < 1e-3);
        QVERIFY(std::abs(peakOf(master, 1) - dbfs(0.5)) < 1e-3);
        rig.mixer->unwatchMeter(loud);
        QVERIFY(!rig.mixer->meterProblem(loud).isEmpty());
        QTRY_COMPARE(loudMeter->channelCount(), 0);
        QVERIFY(std::abs(peakOf(master, 0) - dbfs(0.5)) < 1e-3);
    }

    void devicePortsNeverFallBackToAnotherDevice()
    {
        Rig rig;
        QCOMPARE(rig.apply(QStringLiteral("builtin:null"), QStringLiteral("None"), QStringLiteral("Stereo")),
                 QStringLiteral("accepted"));
        QVERIFY(!rig.track(QStringLiteral("Tone"), 96000, [](qint64) { return 0.5f; }).isEmpty());
        const QString port = AudioMixer::devicePortId(QStringLiteral("builtin:null"), 1, 2);
        const auto listed = [&](const QString &id) {
            for (const QVariant &entry : rig.mixer->outputPorts())
                if (entry.toMap().value(QStringLiteral("id")).toString() == id) return entry.toMap();
            return QVariantMap();
        };
        QVERIFY(listed(port).value(QStringLiteral("available")).toBool()); // the running device, by stable ID
        QVERIFY(ok(rig.mixer->setOutput(QStringLiteral("master"), port)));
        QVERIFY(rig.play());
        AudioMeterModel *output = rig.engine.outputMeter();
        QTRY_VERIFY(std::abs(peakOf(output, 0) - dbfs(0.5)) < 1e-3);

        // Another device: the route keeps its port, now unavailable and
        // explained, and the new device plays nothing until routed.
        QCOMPARE(rig.apply(QStringLiteral("builtin:loopback"), QStringLiteral("None"), QStringLiteral("Stereo")),
                 QStringLiteral("accepted"));
        QCOMPARE(rig.mixer->channel(QStringLiteral("master")).value(QStringLiteral("output")).toString(), port);
        QVERIFY(!rig.mixer->channel(QStringLiteral("master")).value(QStringLiteral("outputAvailable")).toBool());
        const QVariantMap stale = listed(port);
        QVERIFY(!stale.value(QStringLiteral("available")).toBool());
        QVERIFY(stale.value(QStringLiteral("reason")).toString().contains(QStringLiteral("not the running device")));
        QVERIFY(listed(AudioMixer::devicePortId(QStringLiteral("builtin:loopback"), 1, 2))
                    .value(QStringLiteral("available")).toBool());
        QVERIFY(rig.play());
        QTest::qWait(100);
        QTRY_VERIFY(silent(output));
        // An explicit route to the main output plays again.
        QVERIFY(ok(rig.mixer->setOutput(QStringLiteral("master"), QStringLiteral("main"))));
        QTRY_VERIFY(std::abs(peakOf(output, 0) - dbfs(0.5)) < 1e-3);
        rig.engine.stop();
        QTRY_COMPARE(rig.engine.stateName(), QStringLiteral("stopped"));
    }
};

QTEST_MAIN(TestAudioMixerEngine)
#include "tst_audio_mixer_engine.moc"
