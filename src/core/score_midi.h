// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "midi_types.h"
#include "score_types.h"

#include <QHash>
#include <QSet>

namespace QindaTK {

// AGENT-CONTRACT (D-180, docs/notation.md#playback): the one conversion from
// the score (written music, D-178) to MIDI notes (D-150). Tied notes become
// one MIDI note; staccato sounds half and staccatissimo a quarter of the
// last written value; the dynamic in force in the voice (default mf) sets
// the velocity and accent/marcato raise it. Ticks rescale exactly to the
// target PPQ or the conversion fails. MIDI note IDs are UUIDv5 of the tie
// chain's first score note, so every re-conversion keeps them. Spelling,
// voices, tuplet ratios and articulation symbols stay only in the score.
struct ScoreMidiNote {
    MidiNote note;
    QVector<QUuid> scoreNotes; // the tie chain, first to last
};

struct ScoreMidiResult {
    QVector<ScoreMidiNote> notes; // sorted by tick, pitch, channel
    qint64 length = 0;            // the score's length at the target PPQ
    QHash<QUuid, QUuid> noteMap;  // score note -> MIDI note
    QVariantMap retained;         // what the score keeps that the MIDI notes do not
    QString error;
};

namespace Score {
QUuid midiNoteId(const QUuid &scoreNoteId);
double dynamicVelocity(const QString &dynamic); // 0..1, mf when empty
ScoreMidiResult toMidi(const ScoreContent &content, int ticksPerQuarter, const QSet<QUuid> &parts = {});
QVariantMap midiResultMap(const ScoreMidiResult &result);
} // namespace Score

} // namespace QindaTK
