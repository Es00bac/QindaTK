// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-061 loudness fixtures: ITU-R BS.1770-4 momentary, short-term and gated
// integrated loudness through the realtime tap, against analytic sine levels
// and the EBU Tech 3341 gating cases (tolerance ±0.1 LU there), plus history,
// reset, report provenance and layout changes.
#include "audio_loudness_model.h"

#include <QSignalSpy>
#include <QtTest>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <utility>
#include <vector>

using namespace QindaTK;

namespace {

constexpr int Rate = 48000;
constexpr double Silence = -std::numeric_limits<double>::infinity();

// |H(f)|^2 of the K-weighting cascade, evaluated from its coefficients.
double kPower(int rate, double hz)
{
    const std::complex<double> z = std::polar(1.0, -2.0 * std::numbers::pi * hz / rate);
    std::complex<double> h = 1.0;
    for (const AudioBiquad &f : kWeightingFilters(rate))
        h *= (f.b0 + f.b1 * z + f.b2 * z * z) / (1.0 + f.a1 * z + f.a2 * z * z);
    return std::norm(h);
}

// BS.1770 loudness of a steady 1 kHz sine of `amplitude` on `channels` front channels.
double sineLufs(double amplitude, int channels, int rate = Rate)
{
    return -0.691 + 10.0 * std::log10(channels * amplitude * amplitude / 2.0 * kPower(rate, 1000.0));
}

double amplitudeOf(double dbfs)
{
    return std::isinf(dbfs) ? 0.0 : std::pow(10.0, dbfs / 20.0);
}

// Streams a phase-continuous 1 kHz tone, (dBFS, seconds) per segment, on every
// channel through an AudioMeterTap in 256-frame periods into the model.
void play(AudioLoudnessModel &model, int channels, const std::vector<std::pair<double, double>> &segments,
          int rate = Rate)
{
    AudioMeterTap tap(rate, channels);
    const QString layout = channels == 1 ? QStringLiteral("Mono") : channels == 2 ? QStringLiteral("Stereo") : QString();
    model.configure(rate, channels, layout, tap.blockFrames());
    std::vector<float> period(std::size_t(256) * std::size_t(channels));
    AudioMeterBlock block;
    qint64 frame = 0;
    for (const auto &[dbfs, seconds] : segments) {
        const double amplitude = amplitudeOf(dbfs);
        const qint64 end = frame + qint64(std::llround(seconds * rate));
        while (frame < end) {
            const int count = int(std::min<qint64>(256, end - frame));
            for (int i = 0; i < count; ++i) {
                const float s = float(amplitude * std::sin(2.0 * std::numbers::pi * 1000.0 * double(frame + i) / rate));
                for (int c = 0; c < channels; ++c) period[std::size_t(i) * std::size_t(channels) + std::size_t(c)] = s;
            }
            tap.process(period.data(), count);
            while (tap.pop(block)) model.consume(block);
            frame += count;
        }
    }
    model.publish();
}

bool near(double actual, double expected, double tolerance)
{
    return std::abs(actual - expected) < tolerance;
}

} // namespace

class TestAudioLoudness : public QObject {
    Q_OBJECT
private slots:
    void steadyTonesMatchTheAnalyticLevel()
    {
        // EBU Tech 3341 cases 1 and 2 (stereo 1 kHz at -23 and -33 dBFS):
        // -23.0 and -33.0 LUFS ±0.1; here also within 0.001 LU of the exact
        // level the K-filter response gives the tone.
        for (const double dbfs : {-23.0, -33.0}) {
            AudioLoudnessModel model;
            play(model, 2, {{dbfs, 4.0}});
            const double expected = sineLufs(amplitudeOf(dbfs), 2);
            QVERIFY2(near(expected, dbfs, 0.1), qPrintable(QString::number(expected)));
            QVERIFY2(near(model.momentaryLufs(), expected, 1e-3), qPrintable(QString::number(model.momentaryLufs(), 'g', 10)));
            QVERIFY2(near(model.shortTermLufs(), expected, 1e-3), qPrintable(QString::number(model.shortTermLufs(), 'g', 10)));
            QVERIFY2(near(model.integratedLufs(), expected, 1e-3), qPrintable(QString::number(model.integratedLufs(), 'g', 10)));
            QCOMPARE(model.measuredSeconds(), 4.0);
            QCOMPARE(model.gatedBlocks(), 37); // one 400 ms block per 100 ms from 0.4 s to 4.0 s
        }
        // Mono carries one channel of energy: 10 log10 2 = 3.01 LU below stereo.
        AudioLoudnessModel mono;
        play(mono, 1, {{-23.0, 1.0}});
        QVERIFY(near(mono.momentaryLufs(), sineLufs(amplitudeOf(-23.0), 1), 1e-3));
        QVERIFY(near(sineLufs(1.0, 2) - sineLufs(1.0, 1), 10.0 * std::log10(2.0), 1e-12));
        QVERIFY(std::isnan(mono.shortTermLufs())); // needs 3 s
        // 44.1 kHz derives its own K-filter; the level follows that response.
        AudioLoudnessModel cd;
        play(cd, 2, {{-23.0, 1.0}}, 44100);
        QVERIFY(near(cd.momentaryLufs(), sineLufs(amplitudeOf(-23.0), 2, 44100), 1e-3));
    }

    void gatingFollowsTech3341()
    {
        // Case 3: -36 / -23 / -36 dBFS for 10 / 60 / 10 s; the relative gate
        // (10 LU below the ungated mean) drops the quiet parts.
        AudioLoudnessModel three;
        play(three, 2, {{-36.0, 10.0}, {-23.0, 60.0}, {-36.0, 10.0}});
        QVERIFY2(near(three.integratedLufs(), -23.0, 0.1), qPrintable(QString::number(three.integratedLufs(), 'g', 10)));
        // Case 5: -26 / -20 / -26 dBFS for 20 / 20.1 / 20 s.
        AudioLoudnessModel five;
        play(five, 2, {{-26.0, 20.0}, {-20.0, 20.1}, {-26.0, 20.0}});
        QVERIFY2(near(five.integratedLufs(), -23.0, 0.1), qPrintable(QString::number(five.integratedLufs(), 'g', 10)));
        // Short-term follows the last 3 s only.
        QVERIFY(near(five.shortTermLufs(), sineLufs(amplitudeOf(-26.0), 2), 1e-3));
        QVERIFY(five.maxShortTermLufs() > five.shortTermLufs() + 5.0);
    }

    void absoluteGateAndSilence()
    {
        // -72 dBFS stereo is -71.99 LUFS: every block is below the -70 LUFS gate.
        AudioLoudnessModel quiet;
        play(quiet, 2, {{-72.0, 2.0}});
        QVERIFY(near(quiet.momentaryLufs(), sineLufs(amplitudeOf(-72.0), 2), 1e-3));
        QVERIFY(std::isnan(quiet.integratedLufs()));
        QCOMPARE(quiet.gatedBlocks(), 0);
        // -68 dBFS passes it.
        AudioLoudnessModel audible;
        play(audible, 2, {{-68.0, 2.0}});
        QVERIFY(near(audible.integratedLufs(), sineLufs(amplitudeOf(-68.0), 2), 1e-3));
        // Digital silence: momentary is -inf, integrated is not measurable.
        AudioLoudnessModel silence;
        play(silence, 2, {{Silence, 1.0}});
        QVERIFY(std::isinf(silence.momentaryLufs()) && silence.momentaryLufs() < 0);
        QVERIFY(std::isnan(silence.integratedLufs()));
        QVERIFY(silence.summary().contains(QStringLiteral("−∞")));
    }

    void historyResetAndReport()
    {
        AudioLoudnessModel model;
        QSignalSpy measured(&model, &AudioLoudnessModel::measured);
        play(model, 2, {{-23.0, 4.0}});
        QVERIFY(!measured.isEmpty());
        // Short-term readings begin once 3 s are measured: one per 100 ms.
        QCOMPARE(model.historyMetricId(), QStringLiteral("bs1770.short-term"));
        QVariantList history = model.history();
        QCOMPARE(history.size(), 11);
        QCOMPARE(history.first().toMap().value(QStringLiteral("time")).toDouble(), 3.0);
        QCOMPARE(history.last().toMap().value(QStringLiteral("time")).toDouble(), 4.0);
        QCOMPARE(history.last().toMap().value(QStringLiteral("value")).toDouble(), model.shortTermLufs());
        // A shorter history keeps the newest readings.
        model.setHistorySeconds(1.0);
        history = model.history();
        QCOMPARE(history.size(), 10);
        QVERIFY(near(history.first().toMap().value(QStringLiteral("time")).toDouble(), 3.1, 1e-12));
        model.setHistorySeconds(0.5);
        QVERIFY(!model.lastError().isEmpty());
        QCOMPARE(model.historySeconds(), 1.0);

        const QVariantMap report = model.report();
        QCOMPARE(report.value(QStringLiteral("schema")).toString(), QStringLiteral("qindatk.loudness-report/1"));
        QCOMPARE(report.value(QStringLiteral("algorithm")).toString(), QStringLiteral("ITU-R BS.1770-4"));
        QCOMPARE(report.value(QStringLiteral("unit")).toString(), QStringLiteral("LUFS"));
        QCOMPARE(report.value(QStringLiteral("channelLayout")).toString(), QStringLiteral("Stereo"));
        QCOMPARE(report.value(QStringLiteral("subBlockMilliseconds")).toDouble(), 100.0);
        const QVariantMap gating = report.value(QStringLiteral("gating")).toMap();
        QCOMPARE(gating.value(QStringLiteral("absoluteLufs")).toDouble(), -70.0);
        QCOMPARE(gating.value(QStringLiteral("relativeLu")).toDouble(), -10.0);
        QCOMPARE(gating.value(QStringLiteral("blockMilliseconds")).toDouble(), 400.0);
        QCOMPARE(gating.value(QStringLiteral("gatedBlocks")).toInt(), 37);
        const QVariantList metrics = report.value(QStringLiteral("metrics")).toList();
        QCOMPARE(metrics.size(), 5);
        QCOMPARE(metrics.at(2).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("bs1770.integrated"));
        QCOMPARE(metrics.at(2).toMap().value(QStringLiteral("value")).toDouble(), model.integratedLufs());
        QCOMPARE(report.value(QStringLiteral("history")).toMap().value(QStringLiteral("samples")).toList().size(), 10);

        // Reset clears the measurement and history but keeps the stream layout.
        model.reset();
        QVERIFY(std::isnan(model.integratedLufs()));
        QVERIFY(std::isnan(model.momentaryLufs()));
        QVERIFY(model.history().isEmpty());
        QCOMPARE(model.measuredSeconds(), 0.0);
        QCOMPARE(model.channelLayout(), QStringLiteral("Stereo"));
        // Momentary history: readings from 0.4 s, one per 100 ms.
        model.setHistoryMetric(QStringLiteral("momentary"));
        QVERIFY(model.lastError().isEmpty());
        QCOMPARE(model.historyMetricId(), QStringLiteral("bs1770.momentary"));
        play(model, 2, {{-23.0, 1.0}});
        QCOMPARE(model.history().size(), 7);
        QVERIFY(near(model.history().first().toMap().value(QStringLiteral("time")).toDouble(), 0.4, 1e-12));
        model.setHistoryMetric(QStringLiteral("peak"));
        QVERIFY(!model.lastError().isEmpty());

        // BS.1770-4 channel weights; a new layout starts a new measurement.
        QCOMPARE(AudioLoudnessModel::channelWeightsFor(QStringLiteral("5.1"), 6),
                 (QVector<double>{1.0, 1.0, 1.0, 0.0, 1.41, 1.41}));
        QCOMPARE(AudioLoudnessModel::channelWeightsFor(QStringLiteral("Stereo"), 2), (QVector<double>{1.0, 1.0}));
        model.configure(Rate, 1, QStringLiteral("Mono"));
        QVERIFY(std::isnan(model.integratedLufs()));
        QVERIFY(model.history().isEmpty());
        QCOMPARE(model.channelLayout(), QStringLiteral("Mono"));
    }

    void fixturePathMeasuresTheSameAsTheTap()
    {
        // The QML fixture path runs the same tap: 1 s of a mono -6 dBFS tone.
        QVariantList samples;
        samples.reserve(Rate);
        for (int i = 0; i < Rate; ++i) samples.append(0.5 * std::sin(2.0 * std::numbers::pi * 1000.0 * i / Rate));
        AudioLoudnessModel model;
        QVERIFY(model.submitInterleaved(samples, 1, Rate));
        QCOMPARE(model.channelLayout(), QStringLiteral("Mono"));
        QVERIFY(near(model.momentaryLufs(), sineLufs(0.5, 1), 1e-3));
        QVERIFY(!model.submitInterleaved(samples, 0, Rate));
        QVERIFY(!model.lastError().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestAudioLoudness)
#include "tst_audio_loudness.moc"
