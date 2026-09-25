// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"

#include <QtGlobal>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace QindaTK {

// AGENT-CONTRACT (D-194…D-197, docs/audio-pitch.md): pitch analysis and
// correction on one source's samples. Pure functions over planar float
// samples; they run on worker threads (analysis, previews) or the GUI thread
// (apply, replay) and never on the audio device thread, which only plays the
// immutable buffers an edit produces.
//
// Units: pitch is in cents on the MIDI scale, 100 × MIDI note number, so
// A4 = 440 Hz = 6900 cents (PitchEditor's `pitchCents`). Positions are
// source frame indices; ranges are half-open [start, end).
namespace AudioPitch {

inline constexpr double kReferenceHz = 440.0;
inline constexpr double kReferenceCents = 6900.0;
inline constexpr double kHopSeconds = 0.005;       // analysis frame spacing
// Declared correction limits (D-196): beyond them TD-PSOLA's artefacts
// (doubled or lost pulses, envelope smearing) dominate.
inline constexpr double kMaxShiftCents = 700.0;
inline constexpr double kMinFormantRatio = 0.8;
inline constexpr double kMaxFormantRatio = 1.25;
inline constexpr double kMinStretch = 2.0 / 3.0;
inline constexpr double kMaxStretch = 1.5;
inline constexpr double kBoundaryFadeSeconds = 0.005;

double hzToCents(double hz);
double centsToHz(double cents);
int hopFor(int sampleRate); // round(rate × 5 ms), at least 1

// D-194: YIN (de Cheveigné & Kawahara 2002) with an FFT difference function,
// cumulative-mean normalisation, absolute threshold, and a centred
// re-evaluation of the difference around the chosen lag for parabolic
// interpolation.
struct Settings {
    double minimumHz = 60.0;
    double maximumHz = 1000.0;
    double threshold = 0.15;        // YIN absolute threshold on d'(τ)
    double silenceDb = -50.0;       // frames at or below this RMS (dBFS) are unvoiced
    double noteToleranceCents = 50.0;
    int minimumNoteFrames = 4;      // 20 ms at the 5 ms hop
    int maximumGapFrames = 6;       // unvoiced frames (30 ms) bridged inside a segment
};

struct Frame {
    qint64 centre = 0;       // frame f is centred on f × hop
    double hz = 0.0;         // best estimate, also for unvoiced frames (0 when silent)
    double cents = 0.0;
    double confidence = 0.0; // 1 − d'(τ), clamped to 0…1
    bool voiced = false;
};

struct Note {
    qint64 start = 0;        // [start, end) in samples, on frame-cell edges
    qint64 end = 0;
    int firstFrame = 0;      // indices into Analysis::frames
    int lastFrame = 0;       // inclusive
    double cents = 0.0;      // median of the note's voiced frames
    int segment = 0;
};

struct Segment {
    qint64 start = 0;
    qint64 end = 0;
    int firstNote = 0;
    int noteCount = 0;
};

struct Analysis {
    int sampleRate = 0;
    int hop = 0;
    int window = 0;          // YIN integration window W (samples)
    int minimumLag = 0;
    int maximumLag = 0;
    qint64 sourceFrames = 0; // length of the analysed signal
    std::vector<Frame> frames;
    std::vector<Note> notes;
    std::vector<Segment> segments;
    // Index of the frame whose cell [c − hop/2, c + hop/2) holds `sample`, or −1.
    int frameAt(qint64 sample) const;
};

// nullptr when `settings` suit `sampleRate`; otherwise the reason (English).
const char *checkSettings(const Settings &settings, int sampleRate);

// Analyses the frames whose centres lie in [start, end) of a mono signal of
// `frames` samples (samples outside the signal are zero). Frame centres sit on
// the global grid f × hop, so partial analyses agree with a whole-source one.
// nullopt when cancelled or the settings are invalid.
std::optional<Analysis> analyse(const float *mono, qint64 frames, int sampleRate, qint64 start, qint64 end,
                                const Settings &settings, const CancellationToken &cancel = {});

// Groups voiced frames into segments (voiced runs, bridging short gaps) and
// notes (splits where the pitch leaves the running median by more than the
// tolerance for at least minimumNoteFrames frames). Called by analyse().
void segment(Analysis &analysis, const Settings &settings);

// D-195/D-196: one correction of a region by TD-PSOLA (Moulines & Charpentier
// 1990) on epoch-aligned pitch marks, with grain resampling for formants and
// a piecewise-linear time map for retiming.
struct Correction {
    qint64 start = 0;        // the replaced region [start, end)
    qint64 end = 0;
    // Pitch: the median of the region's voiced frames moves to targetCents;
    // `amount` 0 keeps the contour's deviations from that median (vibrato,
    // scoops), 1 flattens every voiced frame onto the target. NaN: no pitch change.
    double targetCents = std::numeric_limits<double>::quiet_NaN();
    double amount = 0.0;
    double formantRatio = 1.0; // spectral envelope scale on voiced grains
    // Time: source sample `anchor` is heard at `target`; [start, anchor) and
    // [anchor, end) stretch linearly to [start, target) and [target, end).
    // −1: no retiming.
    qint64 anchor = -1;
    qint64 target = -1;
    Settings analysis;
};

enum class Problem {
    None,
    Range,          // region outside the signal or shorter than 4 hops
    Settings,       // analysis settings invalid for the rate
    Shift,          // |shift| > kMaxShiftCents
    Formant,        // ratio outside kMinFormantRatio…kMaxFormantRatio
    Time,           // anchor/target outside the region or stretch beyond limits
    Unvoiced,       // a pitch or formant change over a region without voiced frames
    Nothing,        // no pitch, formant or time change requested
    Cancelled,
};

struct CorrectionResult {
    qint64 start = 0;
    std::vector<std::vector<float>> channels; // replacement samples for [start, end)
    double sourceCents = std::numeric_limits<double>::quiet_NaN(); // median of the region's voiced frames
    int voicedFrames = 0;
    int marks = 0;                            // analysis pitch marks used
};

// Checks the parts of `correction` that need no analysis.
Problem check(const Correction &correction, qint64 frames, int sampleRate);

// `channels` are planar pointers of `frames` samples each; pitch is analysed
// on their mean, and every channel shares the same marks (so a stereo image
// keeps its phase relations). Samples outside [start, end) are never touched;
// the first and last kBoundaryFadeSeconds (at most a quarter of the region)
// crossfade from and back to the original.
std::optional<CorrectionResult> correct(const std::vector<const float *> &channels, qint64 frames, int sampleRate,
                                        const Correction &correction, const CancellationToken &cancel,
                                        Problem *problem = nullptr);

} // namespace AudioPitch
} // namespace QindaTK
