// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"
#include "audio_dsp.h"
#include "audio_media.h"

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <optional>

namespace QindaTK {

// AGENT-CONTRACT (D-134, docs/audio-editing.md): one edit of a source's
// samples. Ranges are [start, end) in frames of the source *as it stands
// before this edit*, at the source's own rate. `channel` −1 is every
// channel. Gain, fades, silence and spectral gain replace samples in place;
// delete and crop change the length and always apply to every channel.
struct AudioSourceEdit {
    enum class Kind { Gain, FadeIn, FadeOut, Silence, Delete, Crop, SpectralGain };
    Kind kind = Kind::Gain;
    int channel = -1;
    qint64 start = 0;
    qint64 end = 0;
    double gainDb = 0.0;                 // Gain, SpectralGain (≤ AudioDsp::kSilenceDb is exact zero)
    QString shape = QStringLiteral("linear"); // FadeIn/FadeOut: "linear" or "equalPower"
    double lowHz = 0.0;                  // SpectralGain band, inclusive
    double highHz = 0.0;
    int fftSize = 2048;                  // SpectralGain analysis size; hop fftSize / 4
    // SpectralGain paint/lasso mask (D-186); empty is the lowHz…highHz band.
    std::vector<AudioDsp::SpectralMaskPolygon> mask;

    // Prepared result of a spectral edit: planar replacement samples for the
    // affected channels over [patchStart, patchStart + patch->frames).
    // Not persisted: prepare() recomputes it deterministically. Gain, fades
    // and silence keep no patch; apply() computes them from the base samples
    // (D-187), so a committed edit of those kinds holds only its parameters.
    qint64 patchStart = 0;
    AudioBufferPtr patch;
    quint64 sequence = 0; // commit order inside an AudioSourceEditModel (patch budget)

    bool changesLength() const { return kind == Kind::Delete || kind == Kind::Crop; }
    bool needsPatch() const { return kind == Kind::SpectralGain; }
    QString operation() const;
    QString label() const;
    QVariantMap toMap() const;
    // Accepts {operation, channel, startSample, endSample, gainDb, shape,
    // lowHz, highHz, fftSize, mask}; sample positions are decimal strings or
    // numbers; mask is [[{sample, hz}, …], …] (one list per polygon).
    static std::optional<AudioSourceEdit> fromMap(const QVariantMap &map, QString *error);
};

namespace AudioSourceEditing {
// The operations a source supports, for hosts and tests:
// [{operation, perChannel, changesLength, parameters}].
QVariantList capabilities(const AudioBuffer &buffer);
// Empty when the edit fits `buffer`; otherwise the reason.
QString validate(const AudioBuffer &buffer, const AudioSourceEdit &edit);
// Validates, and computes `edit.patch` for spectral edits (the heavy kind).
// Runs on any thread; false when cancelled or invalid.
bool prepare(const AudioBuffer &buffer, AudioSourceEdit &edit, const CancellationToken &cancel, QString *error);
// The edited buffer. `edit` must be valid for `buffer` and prepared.
AudioBufferPtr apply(const AudioBuffer &buffer, const AudioSourceEdit &edit);
// Applies an in-place (non-length-changing) prepared edit to `buffer` itself.
void applyInPlace(AudioBuffer &buffer, const AudioSourceEdit &edit);
// Fade gain of sample i of an n-sample fade in (fade out uses n - 1 - i).
double fadeInGain(qint64 i, qint64 n, const QString &shape);
} // namespace AudioSourceEditing

} // namespace QindaTK
