// SPDX-License-Identifier: LGPL-3.0-or-later
// AudioSession: MIDI tracks, pattern clips and MIDI files (WP-064,
// docs/audio-midi.md). Patterns live in `midi`; the timeline holds
// beat-locked clips whose resource is "pattern:<uuid>".
#include "audio_session.h"

namespace QindaTK {

QString AudioSession::addMidiTrack(const QString &name)
{
    clearError();
    TimelineEditRequest request;
    request.kind = TimelineEditKind::AddTrack;
    TimelineTrack track;
    track.id = QUuid::createUuid();
    track.name = name;
    track.kind = QStringLiteral("midi");
    request.track = track;
    OperationError problem;
    const QString operation = m_timeline->begin(request, &problem);
    QStringList created;
    if (operation.isEmpty() || m_timeline->update(operation, {}).error
        || (problem = m_timeline->commit(operation, nullptr, &created))) {
        if (!operation.isEmpty()) m_timeline->cancel(operation);
        fail(problem.message.isEmpty() ? tr("The track could not be added") : problem.message);
        return {};
    }
    return created.value(0);
}

QVariantMap AudioSession::insertPattern(const QString &trackId, const QString &patternId, const QString &startTicks,
                                        const QString &lengthTicks)
{
    clearError();
    const auto refuse = [this](const QString &message) {
        fail(message);
        return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
    };
    const MidiPattern *pattern = m_midi->pattern(QUuid::fromString(patternId));
    if (!pattern) return refuse(tr("There is no pattern %1").arg(patternId));
    const TimelineTrack *track = m_timeline->trackRecord(QUuid::fromString(trackId));
    if (!track || track->kind != QLatin1String("midi")) return refuse(tr("Patterns go on MIDI tracks"));
    const auto start = TimelineTime::parseDecimal(startTicks);
    const auto length = lengthTicks.isEmpty() ? std::optional<qint64>(pattern->length)
                                              : TimelineTime::parseDecimal(lengthTicks);
    if (!start || *start < 0 || !length || *length < 1) return refuse(tr("The clip needs a start and a length in ticks"));
    // The clip spans `length` musical ticks from the beat it starts on; its
    // duration is absolute timeline ticks at the current tempo (D-054).
    const auto musical = m_timeline->musicalTickFor(*start);
    const auto end = musical ? m_timeline->ticksForMusicalTick(*musical + *length) : std::nullopt;
    if (!end || *end <= *start) return refuse(tr("The clip is outside the tempo map"));
    TimelineClip clip;
    clip.id = QUuid::createUuid();
    clip.duration = *end - *start;
    clip.resourceId = Midi::patternResourceId(pattern->id);
    clip.kind = QStringLiteral("midi");
    clip.label = pattern->name;
    clip.anchor = TimelineClip::Anchor::Musical;
    QString error;
    const QString clipId = insertClip(clip, trackId, *start, &error);
    if (clipId.isEmpty()) return refuse(error);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("clipId"), clipId}};
}

QVariantMap AudioSession::exportMidi(const QString &patternId, const QString &file)
{
    return m_midi->exportMidiFile(patternId, file, m_tempo->events());
}

QVariantMap AudioSession::importMidi(const QString &file)
{
    return m_midi->importMidiFile(file);
}

} // namespace QindaTK
