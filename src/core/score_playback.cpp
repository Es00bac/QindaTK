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
    releaseAudition();
    m_score = score;
    if (m_score)
        m_scoreConnection = connect(m_score, &ScoreModel::contentChanged, this, [this] {
            if (m_autoSync && m_session) sync();
            else relink();
        });
    relink();
    emit scoreChanged();
}

void ScorePlayback::setSession(AudioSession *session)
{
    if (m_session == session) return;
    releaseAudition();
    for (const auto &connection : std::as_const(m_sessionConnections)) disconnect(connection);
    m_sessionConnections.clear();
    m_session = session;
    if (m_session) {
        // An undo, a reopened file or another host edit can add or remove the linked objects.
        m_sessionConnections << connect(m_session->timeline(), &TimelineModel::structureChanged, this, &ScorePlayback::relink)
                             << connect(m_session->timeline(), &TimelineModel::clipsChanged, this, &ScorePlayback::relink)
                             << connect(m_session->midi(), &MidiSequenceModel::contentChanged, this, &ScorePlayback::relink);
    }
    relink();
    emit sessionChanged();
}

void ScorePlayback::setStartTicks(const QString &ticks)
{
    const auto value = TimelineTime::parseDecimal(ticks);
    if (!value || *value < 0 || *value == m_start) return;
    m_start = *value;
    emit linkChanged(); // the clip moves on the next sync
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

// The link is found, never remembered: the derived track and pattern IDs
// and the pattern's clip on that track (D-180).
void ScorePlayback::relink()
{
    QString track, pattern, clip;
    if (m_score && m_session) {
        const QUuid scoreId = m_score->content().id;
        const QUuid trackId = Score::playbackTrackId(scoreId);
        const QUuid patternId = Score::playbackPatternId(scoreId);
        if (m_session->timeline()->trackRecord(trackId)) track = Midi::idText(trackId);
        if (m_session->midi()->pattern(patternId)) pattern = Midi::idText(patternId);
        const QString resource = Midi::patternResourceId(patternId);
        for (const QString &id : m_session->timeline()->allClipIds()) {
            const TimelineClip *record = m_session->timeline()->clipRecord(QUuid::fromString(id));
            if (record && record->trackId == trackId && record->resourceId == resource) clip = id;
        }
    }
    if (track == m_trackId && pattern == m_patternId && clip == m_clipId) return;
    m_trackId = track;
    m_patternId = pattern;
    m_clipId = clip;
    emit linkChanged();
}

const TimelineClip *ScorePlayback::linkedClip() const
{
    return m_session && !m_clipId.isEmpty() ? m_session->timeline()->clipRecord(QUuid::fromString(m_clipId)) : nullptr;
}

QVariantMap ScorePlayback::sync()
{
    if (!m_score || !m_session) return fail(tr("Score playback needs a score and an audio session"));
    const int ppq = m_session->timeMap()->ticksPerQuarter();
    const ScoreMidiResult conversion = Score::toMidi(m_score->content(), ppq);
    if (!conversion.error.isEmpty()) return fail(conversion.error);
    const QString name = m_score->title().isEmpty() ? tr("Score") : m_score->title();
    const QUuid trackId = Score::playbackTrackId(m_score->content().id);
    const QUuid patternId = Score::playbackPatternId(m_score->content().id);
    TimelineModel *timeline = m_session->timeline();
    MidiSequenceModel *midi = m_session->midi();

    // AGENT-GUARD: sync spans two histories (timeline, MIDI). Each step that
    // committed is counted and undone in reverse when a later step fails, so
    // a failed sync leaves the session as it was.
    int timelineSteps = 0;
    bool midiStep = false;
    const auto rollback = [&](const QString &message) {
        while (timelineSteps-- > 0 && timeline->undo()) {}
        if (midiStep) midi->undo();
        relink();
        return fail(message);
    };
    const auto commitTimeline = [&](const TimelineEditRequest &request, const TimelineEditInput &input, QString *error) {
        OperationError problem;
        const QString operation = timeline->begin(request, &problem);
        if (!operation.isEmpty()) {
            const TimelinePlan plan = timeline->update(operation, input);
            if (plan.error) problem = plan.error;
            else if (!(problem = timeline->commit(operation))) {
                ++timelineSteps;
                return true;
            }
            timeline->cancel(operation);
        }
        *error = problem.message.isEmpty() ? tr("The timeline refused the playback clip") : problem.message;
        return false;
    };
    QString error;
    if (!timeline->trackRecord(trackId)) {
        TimelineEditRequest request;
        request.kind = TimelineEditKind::AddTrack;
        TimelineTrack track;
        track.id = trackId;
        track.name = name;
        track.kind = QStringLiteral("midi");
        request.track = track;
        request.label = tr("Add score track");
        if (!commitTimeline(request, {}, &error) || !timeline->trackRecord(trackId)) return rollback(error);
    }

    MidiContent next = midi->content();
    qsizetype index = -1;
    for (qsizetype p = 0; p < next.patterns.size(); ++p)
        if (next.patterns[p].id == patternId) index = p;
    if (index < 0) {
        MidiPattern created;
        created.id = patternId;
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
    const qint64 length = pattern.length;
    const quint64 midiRevision = midi->revisionValue();
    if (!midi->commitContent(next, tr("Update score playback"), &error)) return rollback(error);
    midiStep = midi->revisionValue() != midiRevision;

    // The clip must start at startTicks and span the pattern's beats.
    relink();
    const auto musical = timeline->musicalTickFor(m_start);
    const auto end = musical ? timeline->ticksForMusicalTick(*musical + length) : std::nullopt;
    const TimelineClip *clip = linkedClip();
    if (!clip || !end || clip->start != m_start || clip->duration != *end - m_start) {
        if (clip) {
            TimelineEditRequest request;
            request.kind = TimelineEditKind::Delete;
            request.clipIds = {clip->id};
            request.label = tr("Replace score clip");
            if (!commitTimeline(request, {}, &error)) return rollback(error);
        }
        const int historyBefore = timeline->historyCount();
        const QVariantMap inserted = m_session->insertPattern(Midi::idText(trackId), Midi::idText(patternId),
                                                              QString::number(m_start), QString::number(length));
        if (!inserted.value(QStringLiteral("ok")).toBool())
            return rollback(inserted.value(QStringLiteral("error")).toString());
        timelineSteps += timeline->historyCount() > historyBefore ? 1 : 0;
    }
    relink();
    m_noteMap = conversion.noteMap;
    m_report = Score::midiResultMap(conversion);
    m_report.insert(QStringLiteral("patternId"), m_patternId);
    m_report.insert(QStringLiteral("trackId"), m_trackId);
    m_report.insert(QStringLiteral("clipId"), m_clipId);
    if (!m_lastError.isEmpty()) {
        m_lastError.clear();
        emit lastErrorChanged();
    }
    emit synced();
    return m_report;
}

std::optional<qint64> ScorePlayback::clipMusicalStart() const
{
    if (!m_session) return std::nullopt;
    const TimelineClip *clip = linkedClip();
    return m_session->timeline()->musicalTickFor(clip ? clip->start : m_start);
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
