// SPDX-License-Identifier: LGPL-3.0-or-later
// Internals shared by audio_pitch.cpp (analysis) and audio_pitch_correct.cpp
// (TD-PSOLA). Not installed.
#pragma once

#include "audio_pitch.h"

namespace QindaTK::AudioPitch::detail {

// A window onto a signal: sample i is data[i − origin] inside
// [origin, origin + count) and zero elsewhere. The window must cover every
// sample of the source a computation reads, so zero only ever stands for
// "outside the source".
struct Signal {
    const float *data = nullptr;
    qint64 origin = 0;
    qint64 count = 0;
    float at(qint64 i) const { return i >= origin && i < origin + count ? data[i - origin] : 0.0f; }
};

// Samples a YIN frame centred on `centre` reads: [centre − before, centre + after).
struct Reach {
    qint64 before = 0;
    qint64 after = 0;
};
Reach frameReach(const Settings &settings, int sampleRate);

std::optional<Analysis> analyseSignal(const Signal &signal, qint64 sourceFrames, int sampleRate, qint64 start,
                                      qint64 end, const Settings &settings, const CancellationToken &cancel);

double median(std::vector<double> values);

} // namespace QindaTK::AudioPitch::detail
