// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "audio_session.h"
#include "score_model.h"

#include <QPointer>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT (D-180, docs/notation.md#playback): plays a ScoreModel
// through an AudioSession's MIDI model and engine (WP-064). sync() writes
// the score's MIDI rendering (Score::toMidi) into one pattern of
// `session.midi` and keeps one beat-locked clip of it on a MIDI track at
// `startTicks`, so the tempo map (WP-049) times it and the engine schedules
// it like any pattern. The score stays the source; the pattern is derived
// and is replaced whole on every sync. The track and pattern IDs are UUIDv5
// of the score ID (Score::playbackTrackId / playbackPatternId) and the clip
// is the one of that pattern on that track, so the link lives in the saved
// session itself: a reopened session, a new ScorePlayback or a score set
// again finds them instead of adding copies. A sync that fails part-way
// undoes the steps it made. Audition uses the engine's cue voices.
class ScorePlayback : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::ScoreModel *score READ score WRITE setScore NOTIFY scoreChanged)
    Q_PROPERTY(QindaTK::AudioSession *session READ session WRITE setSession NOTIFY sessionChanged)
    Q_PROPERTY(QString startTicks READ startTicks WRITE setStartTicks NOTIFY linkChanged)
    Q_PROPERTY(QString trackId READ trackId NOTIFY linkChanged)
    Q_PROPERTY(QString patternId READ patternId NOTIFY linkChanged)
    Q_PROPERTY(QString clipId READ clipId NOTIFY linkChanged)
    Q_PROPERTY(bool autoSync READ autoSync WRITE setAutoSync NOTIFY autoSyncChanged)
    Q_PROPERTY(QVariantMap report READ report NOTIFY synced)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
public:
    explicit ScorePlayback(QObject *parent = nullptr);
    ~ScorePlayback() override;

    ScoreModel *score() const { return m_score; }
    void setScore(ScoreModel *score);
    AudioSession *session() const { return m_session; }
    void setSession(AudioSession *session);
    QString startTicks() const { return QString::number(m_start); }
    void setStartTicks(const QString &ticks);
    QString trackId() const { return m_trackId; }
    QString patternId() const { return m_patternId; }
    QString clipId() const { return m_clipId; }
    bool autoSync() const { return m_autoSync; }
    void setAutoSync(bool on);
    QVariantMap report() const { return m_report; }
    QString lastError() const { return m_lastError; }

    // Creates or updates the pattern, the MIDI track (named after the score)
    // and the clip: {ok, patternId, trackId, clipId, notes, retained}.
    Q_INVOKABLE QVariantMap sync();
    // Score tick <-> timeline ticks through the clip's beat anchor and the tempo map.
    Q_INVOKABLE QString scoreTickAt(const QString &timelineTicks) const;
    Q_INVOKABLE QString timelineTicksFor(const QString &scoreTick) const;
    // The engine transport as a score tick ("" outside the score).
    Q_INVOKABLE QString currentScoreTick() const;
    // ScoreModel::locate of the transport position (the playback cursor).
    Q_INVOKABLE QVariantMap cursor(const QVariantMap &options = {}) const;
    Q_INVOKABLE QString midiNoteFor(const QString &scoreNoteId) const;
    // Sounds every note of the event on the engine's cue voices until released.
    Q_INVOKABLE bool audition(const QString &eventId);
    Q_INVOKABLE bool releaseAudition();

signals:
    void scoreChanged();
    void sessionChanged();
    void linkChanged();
    void autoSyncChanged();
    void synced();
    void lastErrorChanged();

private:
    QVariantMap fail(const QString &message);
    void relink();
    const TimelineClip *linkedClip() const;
    std::optional<qint64> clipMusicalStart() const;

    QPointer<ScoreModel> m_score;
    QPointer<AudioSession> m_session;
    QMetaObject::Connection m_scoreConnection;
    QList<QMetaObject::Connection> m_sessionConnections;
    qint64 m_start = 0;
    QString m_trackId;
    QString m_patternId;
    QString m_clipId;
    bool m_autoSync = false;
    QHash<QUuid, QUuid> m_noteMap;
    QList<int> m_auditioned;
    QVariantMap m_report;
    QString m_lastError;
};

} // namespace QindaTK
