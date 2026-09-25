// SPDX-License-Identifier: LGPL-3.0-or-later
// Pitch analysis (D-194, docs/audio-pitch.md): YIN per 5 ms frame, then
// segments and notes. The TD-PSOLA correction lives in audio_pitch_correct.cpp.
#include "audio_pitch.h"

#include "audio_dsp.h"
#include "audio_pitch_p.h"

#include <algorithm>
#include <complex>

namespace QindaTK::AudioPitch {
namespace {

int nextPowerOfTwo(int value)
{
    int n = 2;
    while (n < value) n <<= 1;
    return n;
}

int maximumLag(const Settings &settings, int rate) { return int(std::floor(double(rate) / settings.minimumHz)); }
int minimumLag(const Settings &settings, int rate)
{
    return std::max(2, int(std::floor(double(rate) / settings.maximumHz)));
}

// One YIN estimator, reused for every frame of an analysis by one thread.
// AGENT-NOTE (D-194): steps 1–4 of de Cheveigné & Kawahara (2002) on an FFT
// cross term: d(τ) = e(0) + e(τ) − 2 r(τ) over the integration window
// W = maximum lag, cumulative-mean normalised d'(τ), the first dip below the
// absolute threshold followed to its local minimum (else the global minimum,
// reported unvoiced). Step 5 re-evaluates d at τ−1, τ, τ+1 with each window
// centred on the frame centre and fits a parabola, so a gliding pitch is
// measured at the frame centre rather than W/2 earlier.
class Yin {
public:
    Yin(int rate, const Settings &settings)
        : m_rate(rate)
        , m_settings(settings)
        , m_maxLag(maximumLag(settings, rate))
        , m_minLag(minimumLag(settings, rate))
        , m_window(m_maxLag)
        , m_block(m_window + m_maxLag + 2)
        , m_fft(nextPowerOfTwo(m_block))
        , m_a(size_t(m_fft.size()))
        , m_b(size_t(m_fft.size()))
        , m_energy(size_t(m_block) + 1)
        , m_normalised(size_t(m_maxLag) + 2)
    {
    }

    int window() const { return m_window; }
    int minLag() const { return m_minLag; }
    int maxLag() const { return m_maxLag; }

    Frame measure(const detail::Signal &x, qint64 centre)
    {
        Frame frame;
        frame.centre = centre;
        const qint64 first = centre - (m_window + m_maxLag) / 2;
        std::fill(m_a.begin(), m_a.end(), std::complex<double>());
        std::fill(m_b.begin(), m_b.end(), std::complex<double>());
        m_energy[0] = 0.0;
        for (int j = 0; j < m_block; ++j) {
            const double v = x.at(first + j);
            m_b[size_t(j)] = v;
            if (j < m_window) m_a[size_t(j)] = v;
            m_energy[size_t(j) + 1] = m_energy[size_t(j)] + v * v;
        }
        // Loudness over the W samples centred on the frame.
        const qint64 r0 = centre - m_window / 2 - first;
        const double power = (m_energy[size_t(r0 + m_window)] - m_energy[size_t(r0)]) / double(m_window);
        if (!(power > 1e-20)) return frame; // digital silence: no estimate at all
        const double decibels = 10.0 * std::log10(power);

        m_fft.forward(m_a.data());
        m_fft.forward(m_b.data());
        for (size_t k = 0; k < m_a.size(); ++k) m_a[k] = std::conj(m_a[k]) * m_b[k];
        m_fft.inverse(m_a.data()); // m_a[τ] = Σ_j a[j]·b[j + τ]
        const double e0 = m_energy[size_t(m_window)];
        double running = 0.0;
        m_normalised[0] = 1.0;
        for (int tau = 1; tau <= m_maxLag + 1; ++tau) {
            const double et = m_energy[size_t(tau + m_window)] - m_energy[size_t(tau)];
            const double d = std::max(0.0, e0 + et - 2.0 * m_a[size_t(tau)].real());
            running += d;
            m_normalised[size_t(tau)] = running > 0.0 ? d * double(tau) / running : 1.0;
        }
        int best = -1;
        for (int tau = m_minLag; tau <= m_maxLag; ++tau) {
            if (m_normalised[size_t(tau)] < m_settings.threshold) {
                while (tau + 1 <= m_maxLag && m_normalised[size_t(tau) + 1] < m_normalised[size_t(tau)]) ++tau;
                best = tau;
                break;
            }
        }
        const bool below = best >= 0;
        if (!below) {
            best = m_minLag;
            for (int tau = m_minLag + 1; tau <= m_maxLag; ++tau)
                if (m_normalised[size_t(tau)] < m_normalised[size_t(best)]) best = tau;
        }
        const double d0 = centred(x, centre, best - 1);
        const double d1 = centred(x, centre, best);
        const double d2 = centred(x, centre, best + 1);
        const double curvature = d0 - 2.0 * d1 + d2;
        const double delta = curvature > 0.0 ? std::clamp(0.5 * (d0 - d2) / curvature, -1.0, 1.0) : 0.0;
        const double period = double(best) + delta;
        frame.hz = double(m_rate) / period;
        frame.cents = hzToCents(frame.hz);
        frame.confidence = std::clamp(1.0 - m_normalised[size_t(best)], 0.0, 1.0);
        frame.voiced = below && decibels > m_settings.silenceDb;
        return frame;
    }

private:
    // d(τ) with the window [c − (W+τ)/2, c + (W−τ)/2) and its lagged copy
    // placed symmetrically about the frame centre.
    double centred(const detail::Signal &x, qint64 centre, int tau) const
    {
        const qint64 first = centre - (m_window + tau) / 2;
        double sum = 0.0;
        for (int j = 0; j < m_window; ++j) {
            const double diff = double(x.at(first + j)) - double(x.at(first + j + tau));
            sum += diff * diff;
        }
        return sum;
    }

    int m_rate;
    Settings m_settings;
    int m_maxLag;
    int m_minLag;
    int m_window;
    int m_block;
    AudioDsp::Fft m_fft;
    std::vector<std::complex<double>> m_a;
    std::vector<std::complex<double>> m_b;
    std::vector<double> m_energy;
    std::vector<double> m_normalised;
};

} // namespace

double hzToCents(double hz)
{
    return kReferenceCents + 1200.0 * std::log2(hz / kReferenceHz);
}

double centsToHz(double cents)
{
    return kReferenceHz * std::exp2((cents - kReferenceCents) / 1200.0);
}

int hopFor(int sampleRate)
{
    return std::max(1, int(std::lround(double(sampleRate) * kHopSeconds)));
}

int Analysis::frameAt(qint64 sample) const
{
    if (frames.empty() || hop <= 0 || sample < 0) return -1;
    const qint64 global = (sample + hop / 2) / hop;
    const qint64 index = global - frames.front().centre / hop;
    return index >= 0 && index < qint64(frames.size()) ? int(index) : -1;
}

const char *checkSettings(const Settings &settings, int sampleRate)
{
    if (sampleRate <= 0) return "The source has no sample rate";
    if (!(settings.minimumHz >= 20.0) || !(settings.maximumHz > settings.minimumHz))
        return "The pitch range must start at 20 Hz or above and rise";
    if (settings.maximumHz > double(sampleRate) / 4.0) return "The highest pitch must be at most a quarter of the sample rate";
    if (maximumLag(settings, sampleRate) > 16384) return "The lowest pitch needs a lag longer than 16384 samples";
    if (!(settings.threshold > 0.0 && settings.threshold < 1.0)) return "The voicing threshold must lie between 0 and 1";
    if (!(settings.noteToleranceCents > 0.0) || settings.minimumNoteFrames < 1 || settings.maximumGapFrames < 0)
        return "The note settings are invalid";
    return nullptr;
}

namespace detail {

Reach frameReach(const Settings &settings, int sampleRate)
{
    // The FFT block and the centred re-evaluation both stay within
    // maximum lag + 2 samples of the centre (W = maximum lag).
    const qint64 lag = maximumLag(settings, sampleRate);
    return {lag + 4, lag + 4};
}

double median(std::vector<double> values)
{
    if (values.empty()) return std::numeric_limits<double>::quiet_NaN();
    const size_t mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + qsizetype(mid), values.end());
    const double upper = values[mid];
    if (values.size() % 2) return upper;
    const double lower = *std::max_element(values.begin(), values.begin() + qsizetype(mid));
    return 0.5 * (lower + upper);
}

std::optional<Analysis> analyseSignal(const Signal &signal, qint64 sourceFrames, int sampleRate, qint64 start,
                                      qint64 end, const Settings &settings, const CancellationToken &cancel)
{
    if (checkSettings(settings, sampleRate)) return std::nullopt;
    Yin yin(sampleRate, settings);
    Analysis analysis;
    analysis.sampleRate = sampleRate;
    analysis.hop = hopFor(sampleRate);
    analysis.window = yin.window();
    analysis.minimumLag = yin.minLag();
    analysis.maximumLag = yin.maxLag();
    analysis.sourceFrames = sourceFrames;
    start = std::max<qint64>(0, start);
    end = std::min(end, sourceFrames);
    const qint64 hop = analysis.hop;
    const qint64 firstFrame = (start + hop - 1) / hop;
    const qint64 lastFrame = end > 0 ? (end - 1) / hop : -1;
    if (lastFrame >= firstFrame) analysis.frames.reserve(size_t(lastFrame - firstFrame + 1));
    for (qint64 f = firstFrame; f <= lastFrame; ++f) {
        if ((f - firstFrame) % 32 == 0 && cancel.isCancelled()) return std::nullopt;
        analysis.frames.push_back(yin.measure(signal, f * hop));
    }
    segment(analysis, settings);
    return analysis;
}

} // namespace detail

std::optional<Analysis> analyse(const float *mono, qint64 frames, int sampleRate, qint64 start, qint64 end,
                                const Settings &settings, const CancellationToken &cancel)
{
    return detail::analyseSignal({mono, 0, mono ? frames : 0}, frames, sampleRate, start, end, settings, cancel);
}

void segment(Analysis &analysis, const Settings &settings)
{
    analysis.notes.clear();
    analysis.segments.clear();
    const std::vector<Frame> &frames = analysis.frames;
    const int count = int(frames.size());
    const qint64 hop = analysis.hop;
    const auto cellStart = [&](int f) { return std::clamp<qint64>(frames[size_t(f)].centre - hop / 2, 0, analysis.sourceFrames); };
    const auto cellEnd = [&](int f) {
        return std::clamp<qint64>(frames[size_t(f)].centre - hop / 2 + hop, 0, analysis.sourceFrames);
    };
    const auto addNote = [&](int first, int last) {
        std::vector<double> cents;
        for (int f = first; f <= last; ++f)
            if (frames[size_t(f)].voiced) cents.push_back(frames[size_t(f)].cents);
        Note note;
        note.firstFrame = first;
        note.lastFrame = last;
        note.start = cellStart(first);
        note.end = cellEnd(last);
        note.cents = detail::median(std::move(cents));
        note.segment = int(analysis.segments.size());
        analysis.notes.push_back(note);
    };
    int i = 0;
    while (i < count) {
        if (!frames[size_t(i)].voiced) {
            ++i;
            continue;
        }
        // The voiced run from i, bridging gaps of at most maximumGapFrames.
        int last = i;
        int voicedCount = 1;
        for (int k = i + 1; k < count; ++k) {
            if (!frames[size_t(k)].voiced) continue;
            if (k - last - 1 > settings.maximumGapFrames) break;
            last = k;
            ++voicedCount;
        }
        const int runStart = i;
        i = last + 1;
        if (voicedCount < settings.minimumNoteFrames) continue; // a blip, not a phrase
        Segment seg;
        seg.firstNote = int(analysis.notes.size());
        // Notes: split where the pitch stays away from the note's running
        // median for minimumNoteFrames voiced frames.
        int noteStart = runStart;
        std::vector<double> reference;
        std::vector<double> pendingCents;
        int pending = -1;
        for (int k = runStart; k <= last; ++k) {
            const Frame &frame = frames[size_t(k)];
            if (!frame.voiced) continue;
            const double ref = reference.empty() ? frame.cents : detail::median(reference);
            if (std::fabs(frame.cents - ref) <= settings.noteToleranceCents) {
                pending = -1;
                pendingCents.clear();
                reference.push_back(frame.cents);
                continue;
            }
            if (pending < 0) pending = k;
            pendingCents.push_back(frame.cents);
            if (int(pendingCents.size()) < settings.minimumNoteFrames) continue;
            if (int(reference.size()) >= settings.minimumNoteFrames) {
                addNote(noteStart, pending - 1);
                noteStart = pending;
            }
            // A note too short to stand alone just re-references to where the pitch went.
            reference = pendingCents;
            pendingCents.clear();
            pending = -1;
        }
        addNote(noteStart, last);
        seg.noteCount = int(analysis.notes.size()) - seg.firstNote;
        seg.start = analysis.notes[size_t(seg.firstNote)].start;
        seg.end = analysis.notes.back().end;
        analysis.segments.push_back(seg);
    }
}

} // namespace QindaTK::AudioPitch
