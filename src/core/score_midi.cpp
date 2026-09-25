// SPDX-License-Identifier: LGPL-3.0-or-later
// Score -> MIDI conversion (WP-067, D-180, docs/notation.md#playback).
#include "score_midi.h"

#include "score_model.h"

#include <QObject>
#include <algorithm>

namespace QindaTK::Score {

namespace {

const QUuid &midiNamespace()
{
    static const QUuid ns = QUuid::fromString(QStringLiteral("{6f1c9a52-3d0e-5b7a-9c4e-1a2b3c4d5e6f}"));
    return ns;
}

} // namespace

QUuid midiNoteId(const QUuid &scoreNoteId)
{
    return QUuid::createUuidV5(midiNamespace(), scoreNoteId.toRfc4122());
}

QUuid playbackTrackId(const QUuid &scoreId)
{
    return QUuid::createUuidV5(midiNamespace(), scoreId.toRfc4122() + QByteArrayLiteral("/track"));
}

QUuid playbackPatternId(const QUuid &scoreId)
{
    return QUuid::createUuidV5(midiNamespace(), scoreId.toRfc4122() + QByteArrayLiteral("/pattern"));
}

double dynamicVelocity(const QString &dynamic)
{
    static const double levels[] = {0.20, 0.30, 0.42, 0.54, 0.66, 0.78, 0.90, 1.0};
    const qsizetype index = dynamicNames().indexOf(dynamic.isEmpty() ? QStringLiteral("mf") : dynamic);
    return levels[std::clamp<qsizetype>(index, 0, 7)];
}

ScoreMidiResult toMidi(const ScoreContent &content, int ticksPerQuarter, const QSet<QUuid> &parts)
{
    ScoreMidiResult result;
    if (ticksPerQuarter < 1) {
        result.error = QObject::tr("The target resolution must be positive");
        return result;
    }
    const auto rescale = [&](qint64 tick, qint64 *out) {
        const qint64 scaled = tick * ticksPerQuarter;
        if (scaled % kPpq) return false;
        *out = scaled / kPpq;
        return true;
    };
    if (!rescale(totalTicks(content), &result.length)) {
        result.error = QObject::tr("The score length is not exact at %1 PPQ").arg(ticksPerQuarter);
        return result;
    }
    int spelled = 0, tied = 0, tupletEvents = 0, articulations = 0, fermatas = 0, voices = 0;
    for (const ScorePart &part : content.parts) {
        if (!parts.isEmpty() && !parts.contains(part.id)) continue;
        for (const ScoreStaff &staff : part.staves)
            for (const ScoreVoice &voice : staff.voices) {
                ++voices;
                QString dynamic;
                QSet<QUuid> continuation; // notes a tie from earlier already covers
                for (int e = 0; e < voice.events.size(); ++e) {
                    const ScoreEvent &event = voice.events[e];
                    if (!event.dynamic.isEmpty()) dynamic = event.dynamic;
                    if (event.duration.actual > 1) ++tupletEvents;
                    articulations += int(event.articulations.size());
                    fermatas += event.articulations.contains(QStringLiteral("fermata")) ? 1 : 0;
                    for (const ScoreNote &head : event.notes) {
                        ++spelled;
                        if (continuation.contains(head.id)) continue;
                        // Follow the tie chain to its last written event.
                        ScoreMidiNote midi;
                        midi.scoreNotes.push_back(head.id);
                        int last = e;
                        const ScoreNote *note = &head;
                        while (note->tie && last + 1 < voice.events.size()) {
                            const int target = tieTarget(*note, voice.events[last + 1]);
                            if (target < 0) break;
                            ++last;
                            note = &voice.events[last].notes[target];
                            continuation.insert(note->id);
                            midi.scoreNotes.push_back(note->id);
                            ++tied;
                        }
                        const ScoreEvent &lastEvent = voice.events[last];
                        const qint64 finalTicks = ticks(lastEvent.duration).value_or(0);
                        qint64 sounding = finalTicks;
                        if (lastEvent.articulations.contains(QStringLiteral("staccatissimo"))) sounding = finalTicks / 4;
                        else if (lastEvent.articulations.contains(QStringLiteral("staccato"))) sounding = finalTicks / 2;
                        const qint64 end = lastEvent.tick + std::max<qint64>(1, sounding);
                        qint64 start = 0, stop = 0;
                        if (!rescale(event.tick, &start) || !rescale(end, &stop)) {
                            result.error = QObject::tr("Event %1 is not exact at %2 PPQ").arg(idText(event.id)).arg(ticksPerQuarter);
                            return result;
                        }
                        double velocity = dynamicVelocity(dynamic);
                        if (event.articulations.contains(QStringLiteral("marcato"))) velocity *= 1.3;
                        else if (event.articulations.contains(QStringLiteral("accent"))) velocity *= 1.15;
                        midi.note.id = midiNoteId(head.id);
                        midi.note.tick = start;
                        midi.note.duration = std::max<qint64>(1, stop - start);
                        midi.note.pitch = midiPitch(head.pitch);
                        midi.note.channel = part.channel;
                        midi.note.velocity = Midi::velocityFromUnit(std::min(1.0, velocity));
                        for (const QUuid &id : midi.scoreNotes) result.noteMap.insert(id, midi.note.id);
                        result.notes.push_back(midi);
                    }
                }
            }
    }
    std::sort(result.notes.begin(), result.notes.end(), [](const ScoreMidiNote &a, const ScoreMidiNote &b) {
        if (a.note.tick != b.note.tick) return a.note.tick < b.note.tick;
        if (a.note.pitch != b.note.pitch) return a.note.pitch < b.note.pitch;
        if (a.note.channel != b.note.channel) return a.note.channel < b.note.channel;
        return a.note.id.toString() < b.note.id.toString();
    });
    result.retained = {{QStringLiteral("spelledNotes"), spelled},
                       {QStringLiteral("tiesMerged"), tied},
                       {QStringLiteral("tupletEvents"), tupletEvents},
                       {QStringLiteral("articulations"), articulations},
                       {QStringLiteral("fermatas"), fermatas},
                       {QStringLiteral("voices"), voices}};
    return result;
}

QVariantMap midiResultMap(const ScoreMidiResult &result)
{
    if (!result.error.isEmpty()) return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), result.error}};
    QVariantList notes;
    for (const ScoreMidiNote &midi : result.notes) {
        QStringList chain;
        for (const QUuid &id : midi.scoreNotes) chain.push_back(idText(id));
        notes.push_back(QVariantMap{{QStringLiteral("id"), idText(midi.note.id)},
                                    {QStringLiteral("tick"), QString::number(midi.note.tick)},
                                    {QStringLiteral("duration"), QString::number(midi.note.duration)},
                                    {QStringLiteral("pitch"), midi.note.pitch},
                                    {QStringLiteral("channel"), midi.note.channel},
                                    {QStringLiteral("velocity16"), int(midi.note.velocity)},
                                    {QStringLiteral("scoreNotes"), chain}});
    }
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("notes"), notes},
            {QStringLiteral("length"), QString::number(result.length)},
            {QStringLiteral("retained"), result.retained}};
}

} // namespace QindaTK::Score

namespace QindaTK {

QVariantMap ScoreModel::midiConversion(int ticksPerQuarter) const
{
    return Score::midiResultMap(Score::toMidi(m_content, ticksPerQuarter));
}

} // namespace QindaTK
