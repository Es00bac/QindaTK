// SPDX-License-Identifier: LGPL-3.0-or-later
// Pitch, formant and timing correction by TD-PSOLA on epoch-aligned pitch
// marks (D-195, D-196, docs/audio-pitch.md).
#include "audio_pitch.h"

#include "audio_dsp.h"
#include "audio_pitch_p.h"

#include <algorithm>

namespace QindaTK::AudioPitch {
namespace {

using detail::Signal;

bool hasPitch(const Correction &c) { return std::isfinite(c.targetCents); }
bool hasFormant(const Correction &c) { return std::fabs(c.formantRatio - 1.0) > 1e-9; }
bool hasTime(const Correction &c) { return c.anchor >= 0 || c.target >= 0; }

// Catmull-Rom interpolation; exact at integer positions.
float interpolate(const Signal &x, double position)
{
    const double floorPos = std::floor(position);
    const qint64 i = qint64(floorPos);
    const double f = position - floorPos;
    const double x1 = x.at(i);
    if (f == 0.0) return float(x1);
    const double x0 = x.at(i - 1), x2 = x.at(i + 1), x3 = x.at(i + 2);
    const double a = -0.5 * x0 + 1.5 * x1 - 1.5 * x2 + 0.5 * x3;
    const double b = x0 - 2.5 * x1 + 2.0 * x2 - 0.5 * x3;
    const double c = -0.5 * x0 + 0.5 * x2;
    return float(((a * f + b) * f + c) * f + x1);
}

// The analysis seen as functions of input time.
class Contour {
public:
    Contour(const Analysis &analysis, const Correction &correction, double sourceCents)
        : m_analysis(analysis), m_correction(correction), m_sourceCents(sourceCents)
    {
    }
    int nearest(double t) const
    {
        const double first = double(m_analysis.frames.front().centre);
        const auto index = qint64(std::llround((t - first) / double(m_analysis.hop)));
        return int(std::clamp<qint64>(index, 0, qint64(m_analysis.frames.size()) - 1));
    }
    bool voiced(double t) const { return m_analysis.frames[size_t(nearest(t))].voiced; }
    // Linear between voiced neighbours; NaN when neither neighbour is voiced.
    double cents(double t) const
    {
        const double first = double(m_analysis.frames.front().centre);
        const double u = (t - first) / double(m_analysis.hop);
        const qint64 last = qint64(m_analysis.frames.size()) - 1;
        const qint64 i0 = std::clamp<qint64>(qint64(std::floor(u)), 0, last);
        const qint64 i1 = std::min(i0 + 1, last);
        const Frame &a = m_analysis.frames[size_t(i0)];
        const Frame &b = m_analysis.frames[size_t(i1)];
        if (a.voiced && b.voiced) {
            const double w = std::clamp(u - double(i0), 0.0, 1.0);
            return a.cents + (b.cents - a.cents) * w;
        }
        if (a.voiced || b.voiced) return a.voiced ? a.cents : b.cents;
        return std::numeric_limits<double>::quiet_NaN();
    }
    double period(double t) const
    {
        const double c = cents(t);
        return std::isfinite(c) ? double(m_analysis.sampleRate) / centsToHz(c) : double(m_analysis.hop);
    }
    // Synthesis/analysis period ratio at an input time (1 where unvoiced).
    double beta(double t) const
    {
        if (!hasPitch(m_correction)) return 1.0;
        const double c = cents(t);
        if (!std::isfinite(c)) return 1.0;
        const double shift = (m_correction.targetCents - m_sourceCents)
                             - m_correction.amount * (c - m_sourceCents);
        return std::exp2(std::clamp(shift, -kMaxShiftCents, kMaxShiftCents) / 1200.0);
    }

private:
    const Analysis &m_analysis;
    const Correction &m_correction;
    double m_sourceCents;
};

// Output time → input time: identity outside the region, two linear pieces
// meeting at (target, anchor) inside it.
double inputTime(const Correction &c, double s)
{
    if (!hasTime(c) || s <= double(c.start) || s >= double(c.end)) return s;
    if (s < double(c.target))
        return double(c.start) + (s - double(c.start)) * double(c.anchor - c.start) / double(c.target - c.start);
    return double(c.anchor) + (s - double(c.target)) * double(c.end - c.anchor) / double(c.end - c.target);
}

struct Mark {
    double position = 0.0;
    bool voiced = false;
};

// AGENT-NOTE (D-195): pitch marks sit on the waveform's extreme of one
// polarity once per period inside a voiced run (the polarity is chosen at the
// run's first period), and every hop in unvoiced material. Grains centred on
// the same point of each period are what lets PSOLA move them apart or
// together without doubling pulses.
std::vector<Mark> pitchMarks(const Signal &x, const Contour &contour, qint64 from, qint64 to, int hop,
                             const CancellationToken &cancel, bool *cancelled)
{
    std::vector<Mark> marks;
    qint64 t = from;
    bool run = false;
    int polarity = 1;
    const auto extreme = [&](qint64 lo, qint64 hi) {
        qint64 best = lo;
        for (qint64 u = lo + 1; u <= hi; ++u)
            if (polarity * x.at(u) > polarity * x.at(best)) best = u;
        return best;
    };
    while (t < to) {
        if (marks.size() % 256 == 0 && cancel.isCancelled()) {
            *cancelled = true;
            return {};
        }
        if (!contour.voiced(double(t))) {
            run = false;
            marks.push_back({double(t), false});
            t += hop;
            continue;
        }
        const double period = contour.period(double(t));
        if (!run) {
            const qint64 end = t + std::max<qint64>(1, qint64(period)) - 1;
            float high = x.at(t), low = x.at(t);
            for (qint64 u = t; u <= end; ++u) {
                high = std::max(high, x.at(u));
                low = std::min(low, x.at(u));
            }
            polarity = high >= -low ? 1 : -1;
            t = extreme(t, end);
            run = true;
        }
        // The peak between samples (parabola through the extreme and its
        // neighbours), so mark spacing follows the period, not the sample grid.
        const double y0 = polarity * x.at(t - 1), y1 = polarity * x.at(t), y2 = polarity * x.at(t + 1);
        const double curvature = y0 - 2.0 * y1 + y2;
        const double offset = curvature < 0.0 ? std::clamp(0.5 * (y0 - y2) / curvature, -0.5, 0.5) : 0.0;
        marks.push_back({double(t) + offset, true});
        const qint64 lo = t + std::max<qint64>(1, qint64(std::ceil(0.75 * period)));
        const qint64 hi = std::max(lo, t + qint64(std::floor(1.25 * period)));
        t = extreme(lo, hi);
    }
    return marks;
}

} // namespace

Problem check(const Correction &c, qint64 frames, int sampleRate)
{
    if (sampleRate <= 0 || c.start < 0 || c.end > frames || c.end - c.start < 4 * qint64(hopFor(sampleRate)))
        return Problem::Range;
    if (checkSettings(c.analysis, sampleRate)) return Problem::Settings;
    if (!std::isfinite(c.amount) || c.amount < 0.0 || c.amount > 1.0) return Problem::Shift;
    if (!(c.formantRatio >= kMinFormantRatio - 1e-12 && c.formantRatio <= kMaxFormantRatio + 1e-12))
        return Problem::Formant;
    if (hasTime(c)) {
        if (c.anchor <= c.start || c.anchor >= c.end || c.target <= c.start || c.target >= c.end) return Problem::Time;
        const double first = double(c.target - c.start) / double(c.anchor - c.start);
        const double second = double(c.end - c.target) / double(c.end - c.anchor);
        for (const double stretch : {first, second})
            if (stretch < kMinStretch - 1e-12 || stretch > kMaxStretch + 1e-12) return Problem::Time;
    }
    if (!hasPitch(c) && !hasFormant(c) && !(hasTime(c) && c.anchor != c.target)) return Problem::Nothing;
    return Problem::None;
}

std::optional<CorrectionResult> correct(const std::vector<const float *> &channels, qint64 frames, int sampleRate,
                                        const Correction &c, const CancellationToken &cancel, Problem *problem)
{
    const auto fail = [problem](Problem p) {
        if (problem) *problem = p;
        return std::nullopt;
    };
    if (problem) *problem = Problem::None;
    if (channels.empty()) return fail(Problem::Range);
    if (const Problem p = check(c, frames, sampleRate); p != Problem::None) return fail(p);

    // Everything read lies in [readLo, readHi): the marks and grains reach
    // two maximum lags beyond the region, the analysis one frame reach more.
    const int hop = hopFor(sampleRate);
    const detail::Reach reach = detail::frameReach(c.analysis, sampleRate);
    const qint64 lag = reach.before;
    const qint64 markLo = std::max<qint64>(0, c.start - 2 * lag);
    const qint64 markHi = std::min(frames, c.end + 2 * lag);
    const qint64 readLo = std::max<qint64>(0, markLo - 4 * lag);
    const qint64 readHi = std::min(frames, markHi + 4 * lag);
    std::vector<float> mixed;
    Signal mono{channels.front() + readLo, readLo, readHi - readLo};
    if (channels.size() > 1) {
        mixed.assign(size_t(readHi - readLo), 0.0f);
        const float scale = 1.0f / float(channels.size());
        for (const float *channel : channels)
            for (qint64 i = readLo; i < readHi; ++i) mixed[size_t(i - readLo)] += channel[i] * scale;
        mono = {mixed.data(), readLo, readHi - readLo};
    }
    const auto analysis = detail::analyseSignal(mono, frames, sampleRate, markLo, markHi, c.analysis, cancel);
    if (!analysis) return fail(Problem::Cancelled);
    if (analysis->frames.empty()) return fail(Problem::Range);

    CorrectionResult result;
    result.start = c.start;
    std::vector<double> inside;
    for (const Frame &frame : analysis->frames)
        if (frame.voiced && frame.centre >= c.start && frame.centre < c.end) inside.push_back(frame.cents);
    result.voicedFrames = int(inside.size());
    result.sourceCents = detail::median(std::move(inside));
    if ((hasPitch(c) || hasFormant(c)) && result.voicedFrames == 0) return fail(Problem::Unvoiced);
    if (hasPitch(c) && std::fabs(c.targetCents - result.sourceCents) > kMaxShiftCents + 1e-9) return fail(Problem::Shift);

    const Contour contour(*analysis, c, result.sourceCents);
    bool cancelled = false;
    const std::vector<Mark> marks = pitchMarks(mono, contour, markLo, markHi, hop, cancel, &cancelled);
    if (cancelled) return fail(Problem::Cancelled);
    if (marks.size() < 2) return fail(Problem::Range);
    result.marks = int(marks.size());

    // AGENT-NOTE (D-195): synthesis. Each output mark takes the grain of the
    // analysis mark nearest its input time, spaced by that mark's period
    // divided by the pitch ratio. Grains are Hann-windowed, at least as long
    // as the spacing on both sides (so every output sample has a grain of
    // weight ≥ ½), resampled by the formant ratio on voiced marks, and the sum
    // is divided by the summed window: output = a weighted average of grain
    // readings. With no change the output marks are the analysis marks and
    // every reading is the input sample itself, so the region reproduces to
    // rounding.
    const qint64 length = c.end - c.start;
    const size_t channelCount = channels.size();
    std::vector<std::vector<double>> sum(channelCount, std::vector<double>(size_t(length), 0.0));
    std::vector<double> weight(size_t(length), 0.0);
    std::vector<Signal> inputs;
    for (const float *channel : channels) inputs.push_back({channel, 0, frames});
    size_t k = 0;
    double s = marks.front().position;
    double previousSpacing = 0.0;
    int grains = 0;
    while (s < double(markHi)) {
        if (++grains % 256 == 0 && cancel.isCancelled()) return fail(Problem::Cancelled);
        const double t = inputTime(c, s);
        while (k + 1 < marks.size() && std::fabs(marks[k + 1].position - t) <= std::fabs(marks[k].position - t)) ++k;
        const Mark &mark = marks[k];
        const double analysisSpacing = k + 1 < marks.size() ? marks[k + 1].position - mark.position
                                                            : mark.position - marks[k - 1].position;
        const double spacing = analysisSpacing / (mark.voiced ? contour.beta(mark.position) : 1.0);
        const double gamma = mark.voiced ? c.formantRatio : 1.0;
        const double half = std::max({analysisSpacing / gamma, spacing, previousSpacing});
        const qint64 lo = std::max<qint64>(c.start, qint64(std::ceil(s - half)));
        const qint64 hi = std::min<qint64>(c.end - 1, qint64(std::floor(s + half)));
        for (qint64 n = lo; n <= hi; ++n) {
            const double u = double(n) - s;
            const double w = 0.5 + 0.5 * std::cos(AudioDsp::kPi * u / half);
            if (w <= 0.0) continue;
            const double position = mark.position + gamma * u;
            for (size_t ch = 0; ch < channelCount; ++ch)
                sum[ch][size_t(n - c.start)] += w * double(interpolate(inputs[ch], position));
            weight[size_t(n - c.start)] += w;
        }
        s += spacing;
        previousSpacing = spacing;
    }

    // Crossfade in from and back out to the original at the region's edges.
    const qint64 fade = std::min<qint64>(std::llround(double(sampleRate) * kBoundaryFadeSeconds), length / 4);
    result.channels.assign(channelCount, std::vector<float>(size_t(length)));
    for (size_t ch = 0; ch < channelCount; ++ch) {
        const float *original = channels[ch] + c.start;
        float *out = result.channels[ch].data();
        for (qint64 i = 0; i < length; ++i) {
            const double w = weight[size_t(i)];
            const double processed = w > 1e-9 ? sum[ch][size_t(i)] / w : double(original[i]);
            double mix = 1.0;
            if (i < fade) mix = double(i + 1) / double(fade + 1);
            else if (i >= length - fade) mix = double(length - i) / double(fade + 1);
            out[i] = float(double(original[i]) + mix * (processed - double(original[i])));
        }
    }
    return result;
}

} // namespace QindaTK::AudioPitch
