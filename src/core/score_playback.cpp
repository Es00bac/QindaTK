// SPDX-License-Identifier: LGPL-3.0-or-later
// ScorePlayback: the score's MIDI pattern, clip, cursor and audition in an
// AudioSession (WP-067, D-180, docs/notation.md#playback).
#include "score_playback.h"

#include "score_midi.h"
#include "timeline_time.h"

namespace QindaTK {

ScorePlayback::ScorePlayback(QObject *parent) : QObject(parent) {}

ScorePlayback::~ScorePlayback()
{
    releaseAudition();
}

void ScorePlayback::setScore(ScoreModel *score)
{
    if (m_score == score) return;
    disconnect(m_scoreConnection);
    m_score = score;
    if (m_score)
        m_scoreConnection = connect(m_score, &ScoreModel::contentChanged, this, [this] {
            if (m_autoSync && m_session) sync();
        });
    emit scoreChanged();
}

void ScorePlayback::setSession(AudioSession *session)
{
    if (m_session == session) return;
    releaseAudition();
    m_session = session;
    m_trackId.clear();
    m_patternId.clear();
    m_clipId.clear();
    m_clipLength = 0;
    emit sessionChanged();
    emit linkChanged();
}

void ScorePlayback::setStartTicks(const QString &ticks)
{
    const auto value = TimelineTime::parseDecimal(ticks);
    if (!value || *value < 0 || *value == m_start) return;
    m_start = *value;
    m_clipLength = -1; // the clip moves on the next sync
    emit linkChanged();
}

void ScorePlayback::setAutoSync(bool on)
{
    if (m_autoSync == on) return;
    m_autoSync = on;
    emit autoSyncChanged();
    if (on && m_score && m_session) sync();
}

QVariantMap ScorePlayback::fail(const QString &message)
{
    m_lastError = message;
    emit lastErrorChanged();
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

bool ScorePlayback::replaceClip(qint64 length, QString *error)
{
    TimelineModel *timeline = m_session->timeline();
    if (!m_clipId.isEmpty() && timeline->clipRecord(QUuid::fromString(m_clipId))) {
        TimelineEditRequest request;
        request.kind = TimelineEditKind::Delete;
        request.clipIds = {QUuid::fromString(m_clipId)};
        request.label = tr("Replace score clip");
        OperationError problem;
        const QString operation = timeline->begin(request, &problem);
        if (operation.isEmpty() || timeline->update(operation, {}).error || (problem = timeline->commit(operation))) {
            if (!operation.isEmpty()) timeline->cancel(operation);
            *error = problem.message.isEmpty() ? tr("The old score clip could not be removed") : problem.message;
            return false;
        }
    }
    m_clipId.clear();
    const QVariantMap inserted = m_session->insertPattern(m_trackId, m_patternId, QString::number(m_start),
                                                          QString::number(length));
    if (!inserted.value(QStringLiteral("ok")).toBool()) {
        *error = inserted.value(QStringLiteral("error")).toString();
        return false;
    }
    m_clipId = inserted.value(QStringLiteral("clipId")).toString();
    m_clipLength = length;
    return true;
}

QVariantMap ScorePlayback::sync()
{
    if (!m_score || !m_session) return fail(tr("Score playback needs a score and an audio session"));
    const int ppq = m_session->timeMap()->ticksPerQuarter();
    const ScoreMidiResult conversion = Score::toMidi(m_score->content(), ppq);
    if (!conversion.error.isEmpty()) return fail(conversion.error);
    const QString name = m_score->title().isEmpty() ? tr("Score") : m_score->title();
    TimelineModel *timeline = m_session->timeline();
    if (m_trackId.isEmpty() || !timeline->trackRecord(QUuid::fromString(m_trackId))) {
        m_trackId = m_session->addMidiTrack(name);
        m_clipId.clear();
        if (m_trackId.isEmpty()) return fail(m_session->lastError());
    }
    MidiSequenceModel *midi = m_session->midi();
    MidiContent next = midi->content();
    const QUuid existing = QUuid::fromString(m_patternId);
    qsizetype index = -1;
    for (qsizetype p = 0; p < next.patterns.size(); ++p)
        if (!existing.isNull() && next.patterns[p].id == existing) index = p;
    if (index < 0) {
        MidiPattern created;
        created.id = QUuid::createUuid();
        next.patterns.push_back(created);
        index = next.patterns.size() - 1;
    }
    MidiPattern &pattern = next.patterns[index];
    pattern.name = name;
    pattern.length = std::max<qint64>(1, conversion.length);
    pattern.notes.clear();
    pattern.events.clear();
    pattern.rows.clear();
    for (const ScoreMidiNote &note : conversion.notes) pattern.notes.push_back(note.note);
    Midi::sortPattern(pattern);
    const QUuid patternId = pattern.id;
    const qint64 length = pattern.length;
    QString error;
    if (!midi->commitContent(next, tr("Update score playback"), &error)) return fail(error);
    m_patternId = Midi::idText(patternId);
    if (m_clipId.isEmpty() || !timeline->clipRecord(QUuid::fromString(m_clipId)) || m_clipLength != length)
        if (!replaceClip(length, &error)) return fail(error);
    m_noteMap = conversion.noteMap;
    m_report = Score::midiResultMap(conversion);
    m_report.insert(QStringLiteral("patternId"), m_patternId);
    m_report.insert(QStringLiteral("trackId"), m_trackId);
    m_report.insert(QStringLiteral("clipId"), m_clipId);
    if (!m_lastError.isEmpty()) {
        m_lastError.clear();
        emit lastErrorChanged();
    }
    emit linkChanged();
    emit synced();
    return m_report;
}

std::optional<qint64> ScorePlayback::clipMusicalStart() const
{
    if (!m_session) return std::nullopt;
    const TimelineModel *timeline = m_session->timeline();
    const TimelineClip *clip = timeline->clipRecord(QUuid::fromString(m_clipId));
    return timeline->musicalTickFor(clip ? clip->start : m_start);
}

QString ScorePlayback::scoreTickAt(const QString &timelineTicks) const
{
    const auto ticks = TimelineTime::parseDecimal(timelineTicks);
    const auto origin = clipMusicalStart();
    if (!ticks || !origin || !m_score) return {};
    const auto musical = m_session->timeline()->musicalTickFor(*ticks);
    if (!musical) return {};
    const int ppq = m_session->timeMap()->ticksPerQuarter();
    const qint64 delta = (*musical - *origin) * Score::kPpq;
    // Floor to the score tick sounding at that moment.
    const qint64 tick = delta >= 0 ? delta / ppq : -((-delta + ppq - 1) / ppq);
    if (tick < 0 || tick >= Score::totalTicks(m_score->content())) return {};
    return QString::number(tick);
}

QString ScorePlayback::timelineTicksFor(const QString &scoreTick) const
{
    const auto tick = TimelineTime::parseDecimal(scoreTick);
    const auto origin = clipMusicalStart();
    if (!tick || !origin) return {};
    const int ppq = m_session->timeMap()->ticksPerQuarter();
    const auto ticks = m_session->timeline()->ticksForMusicalTick(*origin + *tick * ppq / Score::kPpq);
    return ticks ? QString::number(*ticks) : QString();
}

QString ScorePlayback::currentScoreTick() const
{
    return m_session ? scoreTickAt(QString::number(m_session->transportTicks())) : QString();
}

QVariantMap ScorePlayback::cursor(const QVariantMap &options) const
{
    const QString tick = currentScoreTick();
    if (tick.isEmpty() || !m_score) return {{QStringLiteral("ok"), false}};
    QVariantMap place = m_score->locate(tick, options);
    place.insert(QStringLiteral("tick"), tick);
    return place;
}

QString ScorePlayback::midiNoteFor(const QString &scoreNoteId) const
{
    return Midi::idText(m_noteMap.value(QUuid::fromString(scoreNoteId)));
}

bool ScorePlayback::audition(const QString &eventId)
{
    releaseAudition();
    AudioEngine *engine = m_session ? m_session->engine() : nullptr;
    if (!engine || !m_score) {
        fail(tr("Audition needs a score and a session with an engine"));
        return false;
    }
    const auto ref = Score::findEvent(m_score->content(), QUuid::fromString(eventId));
    if (!ref.valid()) {
        fail(tr("There is no event %1").arg(eventId));
        return false;
    }
    const ScoreEvent &event = m_score->content().parts[ref.part].staves[ref.staff].voices[ref.voice].events[ref.event];
    for (const ScoreNote &note : event.notes) {
        const int pitch = Score::midiPitch(note.pitch);
        if (!engine->auditionNoteOn(pitch, Score::dynamicVelocity(event.dynamic))) {
            releaseAudition();
            fail(tr("The engine is not running"));
            return false;
        }
        m_auditioned.push_back(pitch);
    }
    return !event.notes.isEmpty();
}

bool ScorePlayback::releaseAudition()
{
    AudioEngine *engine = m_session ? m_session->engine() : nullptr;
    bool released = true;
    for (int pitch : std::as_const(m_auditioned)) released = (engine && engine->auditionNoteOff(pitch)) && released;
    const bool any = !m_auditioned.isEmpty();
    m_auditioned.clear();
    return any && released;
}

} // namespace QindaTK
