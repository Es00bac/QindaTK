// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-061 on the running engine (the silent builtin:null device): the output
// meter and loudness measure what the device receives, a bound Fader gesture
// changes it (held, canceled, committed, undone, silenced), and an output
// layout change reconfigures the meters. Device-thread allocation freedom of
// the meter tap is asserted by tst_audio_engine's probe, which renders through
// the same processor.
#include "audio_parameter_binding.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <complex>
#include <numbers>

using namespace QindaTK;

namespace {

constexpr int Rate = 48000;

QString writeTone(const QString &path, qint64 frames, double amplitude)
{
    AudioBuffer buffer;
    buffer.sampleRate = Rate;
    buffer.channels = 1;
    buffer.frames = frames;
    buffer.samples.resize(std::size_t(frames));
    for (qint64 i = 0; i < frames; ++i)
        buffer.samples[std::size_t(i)] = float(amplitude * std::sin(2.0 * std::numbers::pi * 1000.0 * double(i) / Rate));
    return AudioFile::writeWav(path, buffer) ? path : QString();
}

// BS.1770 loudness of a steady 1 kHz sine on `channels` front channels.
double sineLufs(double amplitude, int channels)
{
    const std::complex<double> z = std::polar(1.0, -2.0 * std::numbers::pi * 1000.0 / Rate);
    std::complex<double> h = 1.0;
    for (const AudioBiquad &f : kWeightingFilters(Rate))
        h *= (f.b0 + f.b1 * z + f.b2 * z * z) / (1.0 + f.a1 * z + f.a2 * z * z);
    return -0.691 + 10.0 * std::log10(channels * amplitude * amplitude / 2.0 * std::norm(h));
}

double levelOf(const AudioMeterModel *meter, int channel, const char *key)
{
    return meter->levels().value(channel).toMap().value(QLatin1String(key)).toDouble();
}

struct Rig {
    QTemporaryDir dir;
    AudioEngine engine;
    AudioSession session;
    AudioParameterBinding binding;
    TransportModel *transport = engine.transport();
    QString track;

    Rig()
    {
        session.setMediaDirectory(dir.filePath(QStringLiteral("takes")));
        session.setEngine(&engine);
    }
    QString apply(const QString &outputLayout)
    {
        QSignalSpy spy(&engine, &AudioEngine::configurationApplied);
        engine.applyConfiguration(QStringLiteral("test"), {{QStringLiteral("deviceId"), QStringLiteral("builtin:null")},
                                                           {QStringLiteral("sampleRate"), Rate},
                                                           {QStringLiteral("bufferSize"), 256},
                                                           {QStringLiteral("inputLayout"), QStringLiteral("None")},
                                                           {QStringLiteral("outputLayout"), outputLayout}});
        if (spy.isEmpty() && !spy.wait(5000)) return QStringLiteral("timeout");
        return spy.last().at(1).toString();
    }
    // A 20 s mono 1 kHz tone at amplitude 0.5 on a new track, gain bound.
    bool loadTone()
    {
        track = session.addTrack(QStringLiteral("Tone"));
        const QString file = writeTone(dir.filePath(QStringLiteral("tone.wav")), qint64(Rate) * 20, 0.5);
        if (!session.importMedia(file, track).value(QStringLiteral("ok")).toBool()) return false;
        binding.setSession(&session);
        binding.setParameterId(session.ensureGainParameter(track));
        return binding.valid();
    }
};

const double TonePeak = 20.0 * std::log10(0.5); // -6.02 dBFS sample peak

} // namespace

class TestAudioEngineMeter : public QObject {
    Q_OBJECT
private slots:
    void outputMetersFollowTheBoundFader()
    {
        Rig rig;
        QCOMPARE(rig.apply(QStringLiteral("Stereo")), QStringLiteral("accepted"));
        AudioMeterModel *meter = rig.engine.outputMeter();
        AudioLoudnessModel *loudness = rig.engine.outputLoudness();
        QCOMPARE(meter->channelCount(), 2);
        QCOMPARE(meter->channelLayout(), QStringLiteral("Stereo"));
        QCOMPARE(meter->sampleRate(), Rate);
        QCOMPARE(meter->blockMilliseconds(), 10.0);
        QCOMPARE(loudness->channelLayout(), QStringLiteral("Stereo"));
        QVERIFY(rig.loadTone());
        QTRY_VERIFY(rig.transport->canPlay());
        rig.transport->requestPlay();
        QTRY_VERIFY(rig.transport->playing());

        // Known tone on both channels: sample peak 0.5, RMS 0.5 / sqrt 2, and
        // the BS.1770 level the K-filter gives a 1 kHz sine.
        QTRY_VERIFY(std::abs(levelOf(meter, 1, "rmsDbfs") - (TonePeak - 10.0 * std::log10(2.0))) < 1e-3);
        QTRY_VERIFY(std::abs(levelOf(meter, 0, "peakDbfs") - TonePeak) < 1e-4);
        QTRY_VERIFY(std::abs(loudness->momentaryLufs() - sineLufs(0.5, 2)) < 1e-3);
        QVERIFY(meter->measuredSeconds() > 0.3);

        // A held fader is heard: 20 log10 0.5 dB halves the output peak.
        QVERIFY(rig.binding.begin(0.0));
        QVERIFY(rig.binding.preview(TonePeak));
        QTRY_VERIFY(std::abs(levelOf(meter, 0, "peakDbfs") - 2.0 * TonePeak) < 1e-3);
        // Canceled: the device returns to the committed 0 dB and nothing is stored.
        QVERIFY(rig.binding.cancel());
        QTRY_VERIFY(std::abs(levelOf(meter, 0, "peakDbfs") - TonePeak) < 1e-3);
        QCOMPARE(rig.session.automation()->historyCount(), 0);
        // Committed: one undo step, heard; undo is heard too.
        QVERIFY(rig.binding.begin(0.0));
        QVERIFY(rig.binding.commit(-12.0));
        QCOMPARE(rig.binding.lastResult(), QStringLiteral("static"));
        QCOMPARE(rig.session.automation()->historyCount(), 1);
        QTRY_VERIFY(std::abs(levelOf(meter, 0, "peakDbfs") - (TonePeak - 12.0)) < 1e-3);
        QVERIFY(rig.session.automation()->undo());
        QTRY_VERIFY(std::abs(levelOf(meter, 0, "peakDbfs") - TonePeak) < 1e-3);
        // Silence is exact: silent channels and -inf momentary loudness.
        QVERIFY(rig.binding.begin(0.0));
        QVERIFY(rig.binding.commit(0.0, true));
        QTRY_VERIFY(rig.engine.outputMeter()->levels().value(0).toMap().value(QStringLiteral("silent")).toBool());
        QTRY_VERIFY(std::isinf(loudness->momentaryLufs()) && loudness->momentaryLufs() < 0);
        QCOMPARE(meter->droppedBlocks(), qint64(0));

        rig.engine.stop();
        QTRY_COMPARE(rig.engine.stateName(), QStringLiteral("stopped"));
        QCOMPARE(meter->channelCount(), 0); // no stream, no channels
        QVERIFY(!std::isnan(loudness->maxMomentaryLufs())); // the loudness result stays readable
    }

    void outputLayoutChangeReconfiguresTheMeters()
    {
        Rig rig;
        QCOMPARE(rig.apply(QStringLiteral("Stereo")), QStringLiteral("accepted"));
        QVERIFY(rig.loadTone());
        QTRY_VERIFY(rig.transport->canPlay());
        rig.transport->requestPlay();
        QTRY_VERIFY(std::abs(levelOf(rig.engine.outputMeter(), 0, "peakDbfs") - TonePeak) < 1e-4);

        QCOMPARE(rig.apply(QStringLiteral("Mono")), QStringLiteral("accepted"));
        AudioMeterModel *meter = rig.engine.outputMeter();
        QCOMPARE(meter->channelCount(), 1);
        QCOMPARE(meter->channelLayout(), QStringLiteral("Mono"));
        QCOMPARE(meter->levels().value(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("M"));
        QCOMPARE(rig.engine.outputLoudness()->channelLayout(), QStringLiteral("Mono"));
        QTRY_VERIFY(rig.transport->canPlay());
        rig.transport->requestPlay();
        QTRY_VERIFY(std::abs(levelOf(meter, 0, "peakDbfs") - TonePeak) < 1e-4);
        // One channel of the same tone: 3.01 LU below the stereo level.
        QTRY_VERIFY(std::abs(rig.engine.outputLoudness()->momentaryLufs() - sineLufs(0.5, 1)) < 1e-3);
        rig.engine.stop();
        QTRY_COMPARE(rig.engine.stateName(), QStringLiteral("stopped"));
    }
};

QTEST_MAIN(TestAudioEngineMeter)
#include "tst_audio_engine_meter.moc"
