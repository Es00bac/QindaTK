// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "audio_media.h"
#include "audio_pitch.h"

#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace QindaTK {

struct AudioSourceEdit;

// AGENT-CONTRACT (D-197, docs/audio-pitch.md): the "pitch" operation of
// AudioSourceEditModel. audio_source_edit.cpp dispatches every pitch-specific
// step here, so the shared edit code only names the operation. The stored
// parameters are the whole edit: prepare() re-analyses the samples it is
// given and recomputes the patch deterministically, so replay on open, redo
// and relink need nothing else.
namespace AudioPitchEditing {

// Map keys: targetPitchCents (absent: no pitch change), correctionAmount
// (0…1, default 0), formantRatio (default 1), anchorSample/targetSample
// (decimal strings; absent: no retiming), minimumHz, maximumHz,
// voicingThreshold.
bool readMap(const QVariantMap &map, AudioSourceEdit &edit, QString *error);
void writeMap(const AudioSourceEdit &edit, QVariantMap &map);
QStringList parameters();
QString label(const AudioSourceEdit &edit);
QString validate(const AudioBuffer &buffer, const AudioSourceEdit &edit);
bool prepare(const AudioBuffer &buffer, AudioSourceEdit &edit, const CancellationToken &cancel, QString *error);
QString problemText(AudioPitch::Problem problem);
// "A4", "C#5 +12 ct": nearest equal-tempered note and the remaining cents.
QString noteName(double cents, bool withDeviation = false);

} // namespace AudioPitchEditing
} // namespace QindaTK
