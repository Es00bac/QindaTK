// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "midi_types.h"
#include "tempo_map_model.h"

#include <QByteArray>
#include <QStringList>
#include <QVector>
#include <optional>

namespace QindaTK {

// AGENT-CONTRACT (D-150): Standard MIDI File interchange, the declared MIDI
// capability of WP-064 (docs/audio-midi.md "Interchange"). Export writes
// format 1 at the session PPQ: a conductor track (tempo, meter) and one track
// for the pattern (notes, CC, pitch bend, channel and per-note pressure).
// MIDI 1.0 resolution applies: 16-bit velocities and 32-bit values are
// reduced (and reported); per-note pitch bend and step rows are not
// representable and are reported, never silently lost. Import reads formats
// 0 and 1 (running status, note-on velocity 0 as note-off, FIFO pairing of
// overlapping notes of one pitch) and rescales positions exactly to the
// session PPQ when the file's division divides evenly, else to the nearest
// tick (reported).
namespace MidiFile {

struct ExportReport {
    int reducedVelocities = 0;  // not exactly representable at 7 bits
    int reducedValues = 0;      // controller values below their MIDI 1.0 resolution
    int droppedNoteBends = 0;   // per-note pitch bend has no MIDI 1.0 message
    int droppedSteps = 0;       // enabled steps of step rows (not exported)
    int ambiguousOverlaps = 0;  // same-pitch notes nested so FIFO pairing reorders their ends
};

struct Imported {
    int format = 1;
    int division = 960;
    QVector<MidiPattern> patterns;
    QVector<TempoEvent> tempo;  // tempo and meter meta events at the session PPQ
    int roundedPositions = 0;   // positions not exactly representable at the session PPQ
    int unterminatedNotes = 0;  // ended at the end of their track
    int skippedEvents = 0;      // program changes, SysEx and other messages without a model
};

QByteArray write(const MidiPattern &pattern, int ticksPerQuarter, const QVector<TempoEvent> &tempo,
                 ExportReport *report);
std::optional<Imported> read(const QByteArray &data, int ticksPerQuarter, QString *error);

} // namespace MidiFile
} // namespace QindaTK
