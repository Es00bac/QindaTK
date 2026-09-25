// SPDX-License-Identifier: LGPL-3.0-or-later
// MidiLauncherModel: MIDI recording into a launcher slot (WP-064, D-153,
// docs/audio-midi.md). The device thread owns the take's time: it quantizes
// the start, stamps each audition note with a take-relative tick and sizes
// the take on stop; this side pairs the notes and commits one pattern.
#include "midi_launcher_model.h"

#include "audio_engine.h"
#include "audio_engine_p.h"

#include <algorithm>

namespace QindaTK {

std::optional<MidiQuantize> MidiLauncherModel::quantizeOf(const QString &quantization) const
{
    return quantization.isEmpty() ? std::optional<MidiQuantize>(m_midi->content().quantize)
                                  : Midi::parseQuantize(quantization);
}

QString MidiLauncherModel::recordingSlot() const
{
    return m_take ? Midi::idText(m_take->slot) : QString();
}

bool MidiLauncherModel::record(const QString &requestId, const QString &slotId, const QString &quantization)
{
    const MidiSlot *slot = findSlot(QUuid::fromString(slotId));
    if (!slot) return reject(requestId, tr("There is no launcher slot %1").arg(slotId));
    if (m_take) return reject(requestId, tr("Another take is recording"));
    const auto quantize = quantizeOf(quantization);
    if (!quantize) return reject(requestId, tr("Launch quantization is none, beat or bar"));
    if (!m_engine) return reject(requestId, tr("No audio engine is attached to the session"));
    QString error;
    const quint64 serial = m_engine->postRecord(Midi::key(slot->trackId), Midi::key(slot->id), *quantize, &error);
    if (!serial) return reject(requestId, error);
    m_requests.insert(serial, {requestId, slot->id});
    m_take = Take{serial, slot->id, false, -1, {}};
    emit slotListChanged();
    return true;
}

bool MidiLauncherModel::stopRecording(const QString &requestId, const QString &quantization)
{
    const auto quantize = quantizeOf(quantization);
    if (!quantize) return reject(requestId, tr("Launch quantization is none, beat or bar"));
    if (!m_engine) return reject(requestId, tr("No audio engine is attached to the session"));
    QString error;
    const quint64 serial = m_engine->postRecordStop(*quantize, &error);
    if (!serial) return reject(requestId, error);
    m_requests.insert(serial, {requestId, m_take ? m_take->slot : QUuid()});
    return true;
}

void MidiLauncherModel::recordEvent(int kind, quint64 serial, quint64, qint64 sample, qint64 tick, int pitch,
                                    int velocity, int code)
{
    switch (kind) {
    case MidiEngineEvent::RecordQueued:
        if (m_take && m_take->serial == serial) {
            m_take->armed = true;
            m_take->startSample = sample;
        }
        acknowledge(serial, QStringLiteral("accepted"), tr("Records from sample %1").arg(sample));
        break;
    case MidiEngineEvent::NoteRecorded: {
        if (!m_take || m_take->serial != serial) return;
        QVector<TakeNote> &notes = m_take->notes;
        if (velocity > 0) {
            if (notes.size() < Midi::kMaxNotes) notes.push_back({tick, -1, quint16(velocity), pitch});
            return;
        }
        // FIFO pairing: a note-off ends the earliest held note of its key;
        // an all-notes-off (pitch -1) ends every held note.
        for (TakeNote &note : notes)
            if (note.off < 0 && (pitch < 0 || note.pitch == pitch)) {
                note.off = tick;
                if (pitch >= 0) break;
            }
        return;
    }
    case MidiEngineEvent::RecordStopped:
        if (m_take) finishTake(tick);
        acknowledge(serial, QStringLiteral("accepted"),
                    code == MidiEngineEvent::NotRecording ? codeText(code) : tr("Recording stopped at sample %1").arg(sample));
        break;
    default:
        return;
    }
    emit slotListChanged();
}

// Commits the take as a new pattern in its slot (one undo step). `length`
// is the engine's take length; 0 derives it from the notes (device loss).
void MidiLauncherModel::finishTake(qint64 length)
{
    const Take take = std::move(*m_take);
    m_take.reset();
    const MidiSlot *slot = findSlot(take.slot);
    qint64 last = 0;
    for (const TakeNote &note : take.notes) last = std::max({last, note.on + 1, note.off});
    if (!slot || take.notes.isEmpty()) {
        emit recordingFinished(Midi::idText(take.slot), {}, 0);
        return;
    }
    const qint64 end = std::max(length, last);
    MidiPattern pattern;
    pattern.id = QUuid::createUuid();
    const MidiPattern *previous = m_midi->pattern(slot->patternId);
    pattern.name = tr("%1 take").arg(!slot->label.isEmpty() ? slot->label : previous ? previous->name : tr("Clip"));
    pattern.length = std::max<qint64>(1, end);
    pattern.seed = quint32(Midi::key(pattern.id));
    for (const TakeNote &held : take.notes) {
        MidiNote note;
        note.id = QUuid::createUuid();
        note.tick = std::min(held.on, pattern.length - 1);
        note.duration = std::max<qint64>(1, (held.off < 0 ? end : held.off) - note.tick); // held notes end with the take
        note.pitch = held.pitch;
        note.velocity = held.velocity;
        pattern.notes.push_back(note);
    }
    Midi::sortPattern(pattern);
    MidiContent next = m_midi->content();
    next.patterns.push_back(pattern);
    for (MidiSlot &cell : next.launchSlots)
        if (cell.id == take.slot) cell.patternId = pattern.id;
    QString error;
    if (!m_midi->commitContent(next, tr("Record clip"), &error)) {
        m_lastError = error;
        emit lastErrorChanged();
        emit recordingFinished(Midi::idText(take.slot), {}, 0);
        return;
    }
    emit recordingFinished(Midi::idText(take.slot), Midi::idText(pattern.id), int(pattern.notes.size()));
}

} // namespace QindaTK
