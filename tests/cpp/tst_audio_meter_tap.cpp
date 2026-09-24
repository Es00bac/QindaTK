// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-061 measurement fixtures: the realtime meter tap (BS.1770 K-weighting,
// 10 ms block sums) and AudioMeterModel on the engine path: known tones,
// silence and non-finite samples, hold/fall on audio time at different
// publish cadences, clip reset, channel changes and a full ring.
#include "audio_meter_model.h"

#include <QSignalSpy>
#include <QtTest>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

using namespace QindaTK;

namespace {

constexpr int Rate = 48000;

// Interleaved 1 kHz sines, one amplitude per channel. At 48 kHz sample 12 of
// every 48-sample cycle is sin(pi/2) = 1, so the sample peak is the amplitude.
std::vector<float> tones(const std::vector<double> &amplitudes, qint64 frames)
{
    const std::size_t channels = amplitudes.size();
    std::vector<float> out(std::size_t(frames) * channels);
    for (qint64 i = 0; i < frames; ++i) {
        const double s = std::sin(2.0 * std::numbers::pi * 1000.0 * double(i) / Rate);
        for (std::size_t c = 0; c < channels; ++c) out[std::size_t(i) * channels + c] = float(amplitudes[c] * s);
    }
    return out;
}

// Feeds a signal in 256-frame device periods; returns every closed block.
std::vector<AudioMeterBlock> measure(AudioMeterTap &tap, const std::vector<float> &signal)
{
    std::vector<AudioMeterBlock> blocks;
    const qint64 channels = tap.channels();
    const qint64 frames = qint64(signal.size()) / channels;
    AudioMeterBlock block;
    for (qint64 at = 0; at < frames; at += 256) {
        tap.process(signal.data() + std::size_t(at * channels), int(std::min<qint64>(256, frames - at)));
        while (tap.pop(block)) blocks.push_back(block);
    }
    return blocks;
}

AudioMeterBlock steadyBlock(float peak)
{
    AudioMeterBlock block;
    block.frames = 480;
    block.channels = 1;
    block.peak[0] = peak;
    block.squares[0] = double(peak) * double(peak) * 480.0 / 2.0;
    return block;
}

QVariant level(const AudioMeterModel &meter, int channel, const char *key)
{
    return meter.levels().value(channel).toMap().value(QLatin1String(key));
}

} // namespace

class TestAudioMeterTap : public QObject {
    Q_OBJECT
private slots:
    void kWeightingMatchesTheStandardAt48k()
    {
        // ITU-R BS.1770-4 Table 1 (pre-filter) and Table 2 (RLB weighting).
        const double shelf[] = {1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241,
                                0.73248077421585};
        const double highPass[] = {1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621};
        const auto filters = kWeightingFilters(48000);
        const double derived[2][5] = {{filters[0].b0, filters[0].b1, filters[0].b2, filters[0].a1, filters[0].a2},
                                      {filters[1].b0, filters[1].b1, filters[1].b2, filters[1].a1, filters[1].a2}};
        for (int i = 0; i < 5; ++i) {
            QVERIFY2(std::abs(derived[0][i] - shelf[i]) < 1e-12, qPrintable(QString::number(derived[0][i], 'g', 17)));
            QVERIFY2(std::abs(derived[1][i] - highPass[i]) < 1e-12, qPrintable(QString::number(derived[1][i], 'g', 17)));
        }
        // Other rates get their own filter from the same analog prototypes.
        QVERIFY(kWeightingFilters(44100)[0].a1 != filters[0].a1);
    }

    void tapMeasuresKnownTonesInTenMillisecondBlocks()
    {
        AudioMeterTap tap(Rate, 2);
        QCOMPARE(tap.blockFrames(), 480);
        QCOMPARE(AudioMeterTap::defaultBlockFrames(44100), 441);
        QCOMPARE(AudioMeterTap::defaultBlockFrames(96000), 960);
        const auto blocks = measure(tap, tones({0.5, 0.25}, Rate / 2));
        QCOMPARE(int(blocks.size()), 50);
        for (int b = 0; b < 50; ++b) {
            const AudioMeterBlock &block = blocks[std::size_t(b)];
            QCOMPARE(block.frames, 480);
            QCOMPARE(block.channels, 2);
            QCOMPARE(block.endFrame, qint64(480) * (b + 1));
            QCOMPARE(block.peak[0], 0.5f);
            QCOMPARE(block.peak[1], 0.25f);
            QVERIFY(std::abs(block.squares[0] / 480.0 - 0.125) < 1e-7);
            QVERIFY(std::abs(block.squares[1] / 480.0 - 0.03125) < 1e-7);
            QCOMPARE(block.invalid[0], 0);
        }

        AudioMeterModel meter;
        meter.configure(Rate, 2, QStringLiteral("Stereo"), tap.blockFrames());
        QCOMPARE(meter.channelCount(), 2);
        for (const AudioMeterBlock &block : blocks) meter.consume(block);
        meter.publish();
        QCOMPARE(level(meter, 0, "name").toString(), QStringLiteral("L"));
        QCOMPARE(level(meter, 1, "name").toString(), QStringLiteral("R"));
        QCOMPARE(level(meter, 0, "peakDbfs").toDouble(), 20.0 * std::log10(0.5));
        QCOMPARE(level(meter, 1, "peakDbfs").toDouble(), 20.0 * std::log10(0.25));
        QCOMPARE(level(meter, 0, "maxPeakDbfs").toDouble(), 20.0 * std::log10(0.5));
        // RMS over the declared 300 ms window (30 whole-cycle blocks): A / sqrt 2.
        QVERIFY(std::abs(level(meter, 0, "rmsDbfs").toDouble() - 20.0 * std::log10(0.5 / std::sqrt(2.0))) < 1e-5);
        QVERIFY(std::abs(level(meter, 1, "rmsDbfs").toDouble() - 20.0 * std::log10(0.25 / std::sqrt(2.0))) < 1e-5);
        QVERIFY(!level(meter, 0, "clipped").toBool());
        QCOMPARE(meter.blockMilliseconds(), 10.0);
        QCOMPARE(meter.rmsWindowMilliseconds(), 300.0);
        QCOMPARE(meter.measuredSeconds(), 0.5);
        QCOMPARE(meter.metric(), QStringLiteral("sample-peak+rms"));
        QVERIFY2(meter.integrationWindow().contains(QStringLiteral("RMS over 300 ms")), qPrintable(meter.integrationWindow()));
    }

    void silenceAndNonFiniteSamples()
    {
        AudioMeterTap tap(Rate, 2);
        std::vector<float> signal(std::size_t(480) * 2 * 3, 0.0f); // three blocks of digital silence
        signal[10] = std::numeric_limits<float>::quiet_NaN();       // frame 5, left
        signal[11] = std::numeric_limits<float>::infinity();        // frame 5, right
        const auto blocks = measure(tap, signal);
        QCOMPARE(int(blocks.size()), 3);
        QCOMPARE(blocks[0].invalid[0], 1);
        QCOMPARE(blocks[0].invalid[1], 1);
        QCOMPARE(blocks[0].peak[0], 0.0f);
        QCOMPARE(blocks[0].weighted[0], 0.0); // measured as silence: the filter state stays clean

        AudioMeterModel meter;
        meter.configure(Rate, 2, QStringLiteral("Stereo"));
        for (const AudioMeterBlock &block : blocks) meter.consume(block);
        meter.publish();
        QVERIFY(level(meter, 0, "silent").toBool());
        QCOMPARE(level(meter, 0, "peakDbfs").toDouble(), AudioMeterModel::kFloorDbfs);
        QCOMPARE(level(meter, 0, "invalidSamples").toLongLong(), qlonglong(1));
        QCOMPARE(level(meter, 1, "invalidSamples").toLongLong(), qlonglong(1));

        // A tone after the invalid samples measures normally: nothing was poisoned.
        const auto after = measure(tap, tones({0.5, 0.5}, 4800));
        for (const AudioMeterBlock &block : after) meter.consume(block);
        meter.publish();
        QVERIFY(std::isfinite(after.back().weighted[0]) && after.back().weighted[0] > 0.0);
        QCOMPARE(level(meter, 0, "peakDbfs").toDouble(), 20.0 * std::log10(0.5));
        QVERIFY(!level(meter, 0, "silent").toBool());
        meter.resetClip();
        QCOMPARE(level(meter, 0, "invalidSamples").toLongLong(), qlonglong(0));
    }

    void holdAndFallRunOnAudioTime()
    {
        // Two meters see the same blocks; one publishes every 10 ms block, the
        // other every 70 ms. Hold (1500 ms) and fall (24 dB/s) follow the
        // blocks' audio duration, so the snapshots agree exactly.
        AudioMeterModel everyBlock, everySeventh;
        for (AudioMeterModel *meter : {&everyBlock, &everySeventh}) meter->configure(Rate, 1, QStringLiteral("Mono"), 480);
        int fed = 0;
        const auto feed = [&](const AudioMeterBlock &block) {
            everyBlock.consume(block);
            everyBlock.publish();
            everySeventh.consume(block);
            if (++fed % 7 == 0) everySeventh.publish();
        };
        const double loud = 20.0 * std::log10(0.5);
        const double quiet = 20.0 * std::log10(double(0.05f));
        feed(steadyBlock(0.5f));
        QCOMPARE(level(everyBlock, 0, "name").toString(), QStringLiteral("M"));
        for (int i = 1; i <= 149; ++i) {
            feed(steadyBlock(0.05f));
            if (i == 10) // the bar falls 0.24 dB per 10 ms block
                QVERIFY(std::abs(level(everyBlock, 0, "displayPeakDbfs").toDouble() - (loud - 2.4)) < 1e-9);
        }
        QCOMPARE(level(everyBlock, 0, "heldPeakDbfs").toDouble(), loud); // 1490 ms: still held
        QCOMPARE(level(everyBlock, 0, "displayPeakDbfs").toDouble(), quiet);
        feed(steadyBlock(0.05f));                                        // 1500 ms: the hold ends
        QVERIFY(std::abs(level(everyBlock, 0, "heldPeakDbfs").toDouble() - (loud - 0.24)) < 1e-9);
        for (int i = 0; i < 100; ++i) feed(steadyBlock(0.05f));
        QCOMPARE(level(everyBlock, 0, "heldPeakDbfs").toDouble(), quiet); // fell to the signal
        everySeventh.publish();
        QCOMPARE(everySeventh.levels(), everyBlock.levels());

        // Clip latch: a full-scale sample latches until reset, then maxima clear too.
        feed(steadyBlock(1.0f));
        feed(steadyBlock(0.05f));
        QVERIFY(level(everyBlock, 0, "clipped").toBool());
        QCOMPARE(level(everyBlock, 0, "maxPeakDbfs").toDouble(), 0.0);
        everyBlock.resetClip();
        QVERIFY(!level(everyBlock, 0, "clipped").toBool());
        QCOMPARE(level(everyBlock, 0, "maxPeakDbfs").toDouble(), AudioMeterModel::kFloorDbfs);
    }

    void channelChangesResetTheMeter()
    {
        AudioMeterModel meter;
        QSignalSpy configured(&meter, &AudioMeterModel::configurationChanged);
        meter.configure(Rate, 2, QStringLiteral("Stereo"));
        AudioMeterTap stereo(Rate, 2);
        const auto blocks = measure(stereo, tones({0.5, 0.5}, 4800));
        for (const AudioMeterBlock &block : blocks) meter.consume(block);
        meter.publish();
        QCOMPARE(meter.channelCount(), 2);
        QCOMPARE(level(meter, 0, "peakDbfs").toDouble(), 20.0 * std::log10(0.5));

        meter.configure(Rate, 1, QStringLiteral("Mono"));
        QCOMPARE(configured.size(), 2);
        QCOMPARE(meter.channelCount(), 1);
        QCOMPARE(meter.channelLayout(), QStringLiteral("Mono"));
        QCOMPARE(level(meter, 0, "name").toString(), QStringLiteral("M"));
        QCOMPARE(level(meter, 0, "peakDbfs").toDouble(), AudioMeterModel::kFloorDbfs);
        QCOMPARE(meter.measuredSeconds(), 0.0);
        // A stereo block drained after the change never mixes into the mono meter.
        meter.consume(blocks.back());
        QCOMPARE(level(meter, 0, "peakDbfs").toDouble(), AudioMeterModel::kFloorDbfs);
        // The same format again changes nothing; no stream means no channels.
        meter.configure(Rate, 1, QStringLiteral("Mono"));
        QCOMPARE(configured.size(), 2);
        meter.configure(0, 0, {});
        QCOMPARE(meter.channelCount(), 0);
        QVERIFY(meter.levels().isEmpty());
        QCOMPARE(AudioMeterModel::channelNames(QStringLiteral("None"), 3),
                 (QStringList{QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")}));
    }

    void fullRingDropsBlocksWithoutBlocking()
    {
        AudioMeterTap tap(Rate, 1);
        const int blocks = int(AudioMeterTap::kRingBlocks) + 76;
        const std::vector<float> signal(std::size_t(480) * std::size_t(blocks), 0.25f);
        tap.process(signal.data(), 480 * blocks); // no consumer: the producer never waits
        QCOMPARE(tap.droppedBlocks(), quint64(76));
        AudioMeterBlock block;
        std::size_t popped = 0;
        while (tap.pop(block)) ++popped;
        QCOMPARE(popped, AudioMeterTap::kRingBlocks);
        QCOMPARE(block.peak[0], 0.25f);
    }
};

QTEST_GUILESS_MAIN(TestAudioMeterTap)
#include "tst_audio_meter_tap.moc"
