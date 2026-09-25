// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-068 DSP fixtures on signals with known pitch (D-194…D-196): YIN on
// sines, a glide, noise and silence; notes and segments; TD-PSOLA pitch,
// vibrato, formant and timing corrections measured by re-analysis, with
// untouched samples outside the region and no click at its edges.
#include "audio_pitch.h"

#include <QtTest>
#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>

using namespace QindaTK;
using namespace QindaTK::AudioPitch;

namespace {

constexpr int Rate = 48000;
constexpr double Pi = 3.14159265358979323846;

std::vector<float> sine(double hz, double seconds, double amplitude = 0.5)
{
    std::vector<float> x(size_t(seconds * Rate));
    for (size_t i = 0; i < x.size(); ++i) x[i] = float(amplitude * std::sin(2.0 * Pi * hz * double(i) / Rate));
    return x;
}

// Harmonics below 5 kHz with a phase-continuous f0(t); `level(h, f)` is the
// amplitude of harmonic h at frequency f.
std::vector<float> harmonic(const std::function<double(double)> &f0, double seconds,
                            const std::function<double(int, double)> &level)
{
    std::vector<float> x(size_t(seconds * Rate));
    double phase = 0.0;
    for (size_t i = 0; i < x.size(); ++i) {
        const double hz = f0(double(i) / Rate);
        double v = 0.0;
        for (int h = 1; h * hz < 5000.0; ++h) v += level(h, h * hz) * std::sin(h * phase);
        x[i] = float(0.3 * v);
        phase += 2.0 * Pi * hz / Rate;
    }
    return x;
}

// A voice-like tone: 1/h harmonics under a 1 kHz formant.
std::vector<float> voice(const std::function<double(double)> &f0, double seconds)
{
    return harmonic(f0, seconds, [](int h, double f) { return (0.25 + 1.0 / (1.0 + std::pow((f - 1000.0) / 250.0, 2))) / h; });
}

// Only a resonance at `formant` Hz, so the harmonic centroid tracks it.
std::vector<float> resonance(double f0, double formant, double seconds)
{
    return harmonic([f0](double) { return f0; }, seconds,
                    [formant](int, double f) { return 1.0 / (1.0 + std::pow((f - formant) / 200.0, 2)); });
}

std::vector<float> noise(qint64 frames)
{
    std::vector<float> x(static_cast<size_t>(frames));
    quint32 state = 1;
    for (float &v : x) {
        state = state * 1664525u + 1013904223u;
        v = float(double(state) / 4294967296.0 - 0.5);
    }
    return x;
}

Analysis analysed(const std::vector<float> &x, qint64 start = 0, qint64 end = -1)
{
    return *analyse(x.data(), qint64(x.size()), Rate, start, end < 0 ? qint64(x.size()) : end, {}, {});
}

std::vector<float> corrected(const std::vector<float> &x, const Correction &c, CorrectionResult *out = nullptr)
{
    Problem problem = Problem::None;
    const auto result = correct({x.data()}, qint64(x.size()), Rate, c, {}, &problem);
    if (!result) return {};
    std::vector<float> y = x;
    std::copy(result->channels[0].begin(), result->channels[0].end(), y.begin() + result->start);
    if (out) *out = *result;
    return y;
}

// Largest |cents − expected| over the frames centred in [start, end); +inf
// when one of them is unvoiced.
double worstError(const std::vector<float> &x, qint64 start, qint64 end, double expected)
{
    double worst = 0.0;
    for (const Frame &frame : analysed(x, start, end).frames)
        worst = frame.voiced ? std::max(worst, std::fabs(frame.cents - expected)) : std::numeric_limits<double>::infinity();
    return worst;
}

double largestStep(const std::vector<float> &x, qint64 from, qint64 to)
{
    double step = 0.0;
    for (qint64 i = std::max<qint64>(1, from); i < to; ++i)
        step = std::max(step, double(std::fabs(x[size_t(i)] - x[size_t(i - 1)])));
    return step;
}

// Power-weighted mean frequency of the harmonics of f0 below 4 kHz over [a, b).
double centroid(const std::vector<float> &x, qint64 a, qint64 b, double f0)
{
    double weighted = 0.0, total = 0.0;
    for (int h = 1; h * f0 < 4000.0; ++h) {
        std::complex<double> sum;
        for (qint64 i = a; i < b; ++i) {
            const double w = 0.5 - 0.5 * std::cos(2.0 * Pi * double(i - a) / double(b - a));
            sum += w * double(x[size_t(i)]) * std::polar(1.0, -2.0 * Pi * h * f0 * double(i) / Rate);
        }
        const double power = std::norm(sum);
        weighted += h * f0 * power;
        total += power;
    }
    return weighted / total;
}

} // namespace

class TestAudioPitch : public QObject {
    Q_OBJECT
private slots:
    void unitsFollowTheMidiScale()
    {
        QCOMPARE(hzToCents(440.0), 6900.0);
        QVERIFY(std::fabs(hzToCents(220.0) - 5700.0) < 1e-9);
        QVERIFY(std::fabs(centsToHz(6000.0) - 261.6255653005986) < 1e-9);
        QCOMPARE(hopFor(48000), 240);
        QCOMPARE(hopFor(44100), 221);
    }

    void sinesAreMeasuredWithinAHundredthOfACent()
    {
        for (const double hz : {65.0, 100.0, 220.0, 261.63, 440.0, 880.0, 990.0}) {
            const Analysis a = analysed(sine(hz, 1.0));
            QCOMPARE(a.hop, 240);
            int measured = 0;
            for (const Frame &frame : a.frames) {
                if (frame.centre < 2400 || frame.centre >= 45600) continue;
                ++measured;
                QVERIFY2(frame.voiced, qPrintable(QStringLiteral("%1 Hz at %2").arg(hz).arg(frame.centre)));
                QVERIFY2(std::fabs(frame.cents - hzToCents(hz)) < 0.02,
                         qPrintable(QStringLiteral("%1 Hz: %2 cents").arg(hz).arg(frame.cents - hzToCents(hz))));
                QVERIFY(frame.confidence > 0.99);
            }
            QCOMPARE(measured, 180);
            QCOMPARE(a.notes.size(), size_t(1));
            QVERIFY(std::fabs(a.notes[0].cents - hzToCents(hz)) < 0.01);
        }
    }

    void aGlideIsFollowedAtEachFrameCentre()
    {
        // 200 → 400 Hz exponentially over one second (1200 cents/s).
        std::vector<float> x(Rate);
        double phase = 0.0;
        for (int i = 0; i < Rate; ++i) {
            x[size_t(i)] = float(0.5 * std::sin(phase));
            phase += 2.0 * Pi * 200.0 * std::exp2(double(i) / Rate) / Rate;
        }
        for (const Frame &frame : analysed(x).frames) {
            const double t = double(frame.centre) / Rate;
            if (t < 0.1 || t > 0.9) continue;
            QVERIFY(frame.voiced);
            QVERIFY2(std::fabs(frame.cents - hzToCents(200.0 * std::exp2(t))) < 5.0, qPrintable(QString::number(t)));
        }
    }

    void noiseAndSilenceAreUnvoiced()
    {
        const Analysis hiss = analysed(noise(Rate));
        QCOMPARE(hiss.frames.size(), size_t(200));
        for (const Frame &frame : hiss.frames) {
            QVERIFY(!frame.voiced);
            QVERIFY(frame.confidence < 0.5);
        }
        QVERIFY(hiss.notes.empty() && hiss.segments.empty());
        const Analysis quiet = analysed(std::vector<float>(Rate, 0.0f));
        for (const Frame &frame : quiet.frames) {
            QVERIFY(!frame.voiced);
            QCOMPARE(frame.hz, 0.0);
            QCOMPARE(frame.confidence, 0.0);
        }
        QVERIFY(quiet.notes.empty());
    }

    void notesAndSegmentsFollowThePitch()
    {
        // Legato A3 → E4 (phase continuous at sample 24000): one phrase, two notes.
        const std::vector<float> legato = voice([](double t) { return t < 0.5 ? 220.0 : 330.0; }, 1.0);
        const Analysis a = analysed(legato);
        QCOMPARE(a.segments.size(), size_t(1));
        QCOMPARE(a.notes.size(), size_t(2));
        QVERIFY(std::fabs(a.notes[0].cents - 5700.0) < 0.05);
        QVERIFY(std::fabs(a.notes[1].cents - hzToCents(330.0)) < 0.05);
        QVERIFY(a.notes[1].start >= 24000 && a.notes[1].start <= 24960); // within 20 ms after the change
        QCOMPARE(a.notes[0].end, a.notes[1].start);
        QCOMPARE(a.notes[1].segment, 0);
        // 100 ms of silence splits phrases.
        std::vector<float> gapped = voice([](double) { return 220.0; }, 0.4);
        gapped.resize(gapped.size() + 4800, 0.0f);
        const std::vector<float> again = voice([](double) { return 220.0; }, 0.4);
        gapped.insert(gapped.end(), again.begin(), again.end());
        const Analysis b = analysed(gapped);
        QCOMPARE(b.segments.size(), size_t(2));
        QCOMPARE(b.notes.size(), size_t(2));
        QVERIFY(b.notes[0].end <= 19680 && b.notes[1].start >= 24000);
        // A partial analysis sits on the same frame grid and agrees exactly.
        const Analysis part = analysed(legato, 12000, 36000);
        QCOMPARE(part.frames.front().centre, qint64(12000));
        QCOMPARE(part.frames.size(), size_t(100));
        for (const Frame &frame : part.frames) {
            const Frame &whole = a.frames[size_t(a.frameAt(frame.centre))];
            QCOMPARE(frame.cents, whole.cents);
            QCOMPARE(frame.confidence, whole.confidence);
            QCOMPARE(frame.voiced, whole.voiced);
        }
    }

    void pitchCorrectionHitsTheTarget()
    {
        // 207 Hz: a period of 231.88 samples, off the sample grid.
        const std::vector<float> x = voice([](double) { return 207.0; }, 1.0);
        const double inputStep = largestStep(x, 1, qint64(x.size()));
        for (const double shift : {100.0, -100.0, 300.0, -300.0, 699.0, -699.0}) {
            Correction c;
            c.start = 9600;
            c.end = 38400;
            c.targetCents = hzToCents(207.0) + shift;
            CorrectionResult result;
            const std::vector<float> y = corrected(x, c, &result);
            QVERIFY2(!y.empty(), qPrintable(QString::number(shift)));
            QVERIFY(std::fabs(result.sourceCents - hzToCents(207.0)) < 0.01);
            QCOMPARE(result.voicedFrames, 120);
            // Every frame of the region's interior sits on the target.
            const double error = worstError(y, 14400, 33600, c.targetCents);
            QVERIFY2(error < 0.25, qPrintable(QStringLiteral("shift %1: %2 cents").arg(shift).arg(error)));
            // Outside the region nothing changed; the edges do not click.
            for (qint64 i = 0; i < qint64(x.size()); ++i)
                if (i < c.start || i >= c.end) QCOMPARE(y[size_t(i)], x[size_t(i)]);
            QVERIFY(largestStep(y, c.start - 50, c.start + 400) <= 1.1 * inputStep);
            QVERIFY(largestStep(y, c.end - 400, c.end + 50) <= 1.1 * inputStep);
        }
    }

    void noChangeReproducesTheInput()
    {
        const std::vector<float> x = voice([](double) { return 200.0; }, 1.0);
        Correction c;
        c.start = 9600;
        c.end = 38400;
        c.targetCents = hzToCents(200.0);
        CorrectionResult probe;
        QVERIFY(!corrected(x, c, &probe).empty());
        c.targetCents = probe.sourceCents; // exactly the median: β = 1 everywhere
        const std::vector<float> y = corrected(x, c);
        for (qint64 i = c.start; i < c.end; ++i) QVERIFY(std::fabs(y[size_t(i)] - x[size_t(i)]) <= 1e-6f);
    }

    void amountFlattensOrKeepsVibrato()
    {
        // 220 Hz with ±30 cents of 5 Hz vibrato.
        const std::vector<float> x = voice([](double t) { return 220.0 * std::exp2(30.0 / 1200.0 * std::sin(2.0 * Pi * 5.0 * t)); }, 1.0);
        QVERIFY(worstError(x, 9600, 38400, 5700.0) > 25.0);
        Correction flat;
        flat.start = 4800;
        flat.end = 43200;
        flat.targetCents = 5700.0;
        flat.amount = 1.0;
        QVERIFY(worstError(corrected(x, flat), 9600, 38400, 5700.0) < 4.0);
        // Amount 0 transposes and keeps the vibrato frame by frame.
        Correction transpose = flat;
        transpose.amount = 0.0;
        transpose.targetCents = 5800.0;
        const Analysis before = analysed(x, 9600, 38400);
        const Analysis after = analysed(corrected(x, transpose), 9600, 38400);
        QCOMPARE(after.frames.size(), before.frames.size());
        for (size_t i = 0; i < after.frames.size(); ++i)
            QVERIFY(std::fabs(after.frames[i].cents - 100.0 - before.frames[i].cents) < 4.0);
    }

    void formantRatioScalesTheEnvelope()
    {
        const std::vector<float> x = resonance(150.0, 1000.0, 1.0);
        const double original = centroid(x, 14400, 33600, 150.0);
        QVERIFY(std::fabs(original - 1000.0) < 20.0);
        struct Case {
            double ratio;
            double shift;
        };
        for (const Case &k : {Case{1.0, 200.0}, Case{1.0, -200.0}, Case{1.25, 0.0}, Case{1.25, 200.0},
                              Case{0.8, 0.0}, Case{0.8, -200.0}}) {
            Correction c;
            c.start = 4800;
            c.end = 43200;
            c.formantRatio = k.ratio;
            if (k.shift != 0.0) c.targetCents = hzToCents(150.0) + k.shift;
            const std::vector<float> y = corrected(x, c);
            const double f0 = 150.0 * std::exp2(k.shift / 1200.0);
            const double moved = centroid(y, 14400, 33600, f0) / original;
            QVERIFY2(std::fabs(moved - k.ratio) < 0.03, qPrintable(QStringLiteral("%1 %2: %3").arg(k.ratio).arg(k.shift).arg(moved)));
            QVERIFY(worstError(y, 14400, 33600, hzToCents(f0)) < 0.25); // the pitch is where it was asked to be
        }
    }

    void retimingMovesANoteStart()
    {
        const std::vector<float> x = voice([](double t) { return t < 0.5 ? 220.0 : 330.0; }, 1.0);
        const Analysis before = analysed(x);
        QCOMPARE(before.notes.size(), size_t(2));
        for (const qint64 target : {qint64(26400), qint64(22000)}) {
            Correction c;
            c.start = 4800;
            c.end = 43200;
            c.anchor = 24000;
            c.target = target;
            const std::vector<float> y = corrected(x, c);
            QCOMPARE(y.size(), x.size());
            const Analysis after = analysed(y);
            QCOMPARE(after.notes.size(), size_t(2));
            const qint64 expected = before.notes[1].start + (target - c.anchor);
            QVERIFY2(std::abs(after.notes[1].start - expected) <= 2 * 240, qPrintable(QString::number(after.notes[1].start)));
            QVERIFY(std::fabs(after.notes[0].cents - before.notes[0].cents) < 0.05);
            QVERIFY(std::fabs(after.notes[1].cents - before.notes[1].cents) < 0.05);
        }
    }

    void channelsShareTheirMarks()
    {
        const std::vector<float> left = voice([](double) { return 200.0; }, 1.0);
        std::vector<float> right(left.size());
        for (size_t i = 0; i < left.size(); ++i) right[i] = 0.5f * left[i];
        Correction c;
        c.start = 9600;
        c.end = 38400;
        c.targetCents = hzToCents(200.0) + 200.0;
        const auto result = correct({left.data(), right.data()}, qint64(left.size()), Rate, c, {});
        QVERIFY(result && result->channels.size() == 2);
        for (size_t i = 0; i < result->channels[0].size(); ++i)
            QCOMPARE(result->channels[1][i], 0.5f * result->channels[0][i]);
    }

    void declaredLimitsAreEnforced()
    {
        const std::vector<float> x = voice([](double) { return 200.0; }, 1.0);
        const auto problem = [&](const std::function<void(Correction &)> &change, const std::vector<float> &signal) {
            Correction c;
            c.start = 9600;
            c.end = 38400;
            c.targetCents = hzToCents(200.0) + 100.0;
            change(c);
            Problem p = Problem::None;
            const bool done = correct({signal.data()}, qint64(signal.size()), Rate, c, {}, &p).has_value();
            return done ? Problem::None : p;
        };
        QCOMPARE(problem([](Correction &) {}, x), Problem::None);
        QCOMPARE(problem([](Correction &c) { c.end = c.start + 900; }, x), Problem::Range); // < 4 hops
        QCOMPARE(problem([](Correction &c) { c.end = 48001; }, x), Problem::Range);
        QCOMPARE(problem([](Correction &c) { c.targetCents += 700.0; }, x), Problem::Shift);
        QCOMPARE(problem([](Correction &c) { c.amount = 1.5; }, x), Problem::Shift);
        QCOMPARE(problem([](Correction &c) { c.formantRatio = 1.3; }, x), Problem::Formant);
        QCOMPARE(problem([](Correction &c) { c.anchor = 24000; c.target = 38400; }, x), Problem::Time);
        QCOMPARE(problem([](Correction &c) { c.anchor = 12000; c.target = 20000; }, x), Problem::Time); // 4.3× stretch
        QCOMPARE(problem([](Correction &c) { c.targetCents = std::nan(""); }, x), Problem::Nothing);
        QCOMPARE(problem([](Correction &c) { c.analysis.maximumHz = 20000.0; }, x), Problem::Settings);
        QCOMPARE(problem([](Correction &) {}, noise(Rate)), Problem::Unvoiced);
        // A retiming of unvoiced material is allowed; a pitch change is not.
        QCOMPARE(problem([](Correction &c) { c.targetCents = std::nan(""); c.anchor = 24000; c.target = 26000; }, noise(Rate)),
                 Problem::None);
    }

    void cancellationStopsBothPasses()
    {
        const std::vector<float> x = voice([](double) { return 200.0; }, 1.0);
        CancellationSource source;
        source.cancel();
        QVERIFY(!analyse(x.data(), qint64(x.size()), Rate, 0, qint64(x.size()), {}, source.token()));
        Correction c;
        c.start = 9600;
        c.end = 38400;
        c.targetCents = hzToCents(200.0) + 100.0;
        Problem p = Problem::None;
        QVERIFY(!correct({x.data()}, qint64(x.size()), Rate, c, source.token(), &p));
        QCOMPARE(p, Problem::Cancelled);
    }
};

QTEST_GUILESS_MAIN(TestAudioPitch)
#include "tst_audio_pitch.moc"
