// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "media_player.h"
#include "resource_resolver.h"
#include "timeline_model.h"

#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT (docs/sequence-edits.md "Multicam", D-121): the backend of
// MulticamView. Angles are synchronized sources: at timeline tick T, angle a
// shows source ticks (T - syncStartTicks) + a.syncOffsetTicks. The program is
// ordinary clips on `trackId`; switchAngle() is one SwitchSource transaction
// that cuts the program clip at the (frame-exact) switch time and gives the
// part from there the angle's source at that sync position. With
// audioFollowsVideo the linked sound is cut and switched with it; without it
// the sound is untouched. An angle whose sound is a separate file
// (audioResourceId) has its own sync offset and length for that file, so its
// sound lands at (T - syncStartTicks) + a.audioSyncOffsetTicks. The program angle at a time is the angle whose
// resource the program clip there shows, so the cuts reopen from the saved
// timeline plus this model's angle table (contentMap/loadContent).
class MulticamModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::TimelineModel *timeline READ timeline WRITE setTimeline NOTIFY bindingChanged)
    Q_PROPERTY(QindaTK::ResourceResolver *resolver READ resolver WRITE setResolver NOTIFY bindingChanged)
    Q_PROPERTY(QindaTK::MediaPlayer *player READ player WRITE setPlayer NOTIFY bindingChanged)
    Q_PROPERTY(QString trackId READ trackId WRITE setTrackId NOTIFY settingsChanged)
    Q_PROPERTY(QString audioTrackId READ audioTrackId WRITE setAudioTrackId NOTIFY settingsChanged)
    Q_PROPERTY(QString syncStartTicks READ syncStartTicks WRITE setSyncStartTicks NOTIFY settingsChanged)
    Q_PROPERTY(bool audioFollowsVideo READ audioFollowsVideo WRITE setAudioFollowsVideo NOTIFY settingsChanged)
    Q_PROPERTY(QString playheadTicks READ playheadTicks WRITE setPlayheadTicks NOTIFY anglesChanged)
    Q_PROPERTY(QVariantList angles READ angles NOTIFY anglesChanged)
    Q_PROPERTY(int angleCount READ angleCount NOTIFY anglesChanged)
    Q_PROPERTY(QString activeAngleId READ activeAngleId NOTIFY anglesChanged)
    Q_PROPERTY(QString frameRateNumerator READ frameRateNumerator NOTIFY anglesChanged)
    Q_PROPERTY(QString frameRateDenominator READ frameRateDenominator NOTIFY anglesChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
public:
    struct Angle {
        QString id;
        QString label;
        QString resourceId;
        QString audioResourceId; // empty: the picture's resource carries the sound
        qint64 syncOffset = 0;   // source ticks at sync time 0
        qint64 sourceDuration = -1;
        // The separate sound file's own sync position and length (used only
        // with audioResourceId; the offset defaults to syncOffset, the length to unknown).
        qint64 audioSyncOffset = 0;
        qint64 audioSourceDuration = -1;
    };
    static constexpr int kMaxAngles = 64;

    explicit MulticamModel(QObject *parent = nullptr);

    TimelineModel *timeline() const { return m_timeline; }
    void setTimeline(TimelineModel *timeline);
    ResourceResolver *resolver() const { return m_resolver; }
    void setResolver(ResourceResolver *resolver);
    MediaPlayer *player() const { return m_player; }
    void setPlayer(MediaPlayer *player);
    QString trackId() const { return m_trackId; }
    void setTrackId(const QString &trackId);
    QString audioTrackId() const { return m_audioTrackId; }
    void setAudioTrackId(const QString &trackId);
    QString syncStartTicks() const { return QString::number(m_syncStart); }
    void setSyncStartTicks(const QString &ticks);
    bool audioFollowsVideo() const { return m_audioFollows; }
    void setAudioFollowsVideo(bool follows);
    QString playheadTicks() const { return QString::number(m_playhead); }
    void setPlayheadTicks(const QString &ticks);
    QVariantList angles() const;
    int angleCount() const { return int(m_angles.size()); }
    QString activeAngleId() const;
    QString frameRateNumerator() const;
    QString frameRateDenominator() const;
    QString lastError() const { return m_lastError; }
    const QVector<Angle> &angleRecords() const { return m_angles; }

    // angle: {angleId, label, resourceId, audioResourceId, syncOffsetTicks, sourceDurationTicks,
    // audioSyncOffsetTicks, audioSourceDurationTicks}.
    Q_INVOKABLE bool addAngle(const QVariantMap &angle);
    Q_INVOKABLE bool removeAngle(const QString &angleId);
    Q_INVOKABLE bool setSyncOffset(const QString &angleId, const QString &syncOffsetTicks);
    // Source ticks angle `angleId` shows at timeline tick `ticks`; "" when unknown.
    Q_INVOKABLE QString sourceTicksAt(const QString &angleId, const QString &ticks) const;
    // The angle the program shows at `ticks` ("" in a gap or on a non-angle clip).
    Q_INVOKABLE QString angleAt(const QString &ticks) const;
    // Places a synchronized program clip of `angleId` (plus its linked sound on
    // audioTrackId when set). Returns {ok, clipIds, transactionId, error}.
    Q_INVOKABLE QVariantMap insertProgram(const QString &angleId, const QString &startTicks,
                                          const QString &durationTicks);
    // Cuts to `angleId` at `atTicks` (the playhead when empty). Returns {ok,
    // transactionId, atTicks, activeAngleId, error}.
    Q_INVOKABLE QVariantMap switchAngle(const QString &angleId, const QString &atTicks = {});
    // Program clips on trackId, and angle sound clips on audioTrackId, whose
    // source is not where the sync map puts it.
    Q_INVOKABLE QStringList unsyncedClipIds() const;
    Q_INVOKABLE QVariantMap contentMap() const;
    Q_INVOKABLE bool loadContent(const QVariantMap &content);

signals:
    void bindingChanged();
    void settingsChanged();
    void anglesChanged();
    void lastErrorChanged();

private:
    const Angle *angle(const QString &id) const;
    const Angle *angleForResource(const QString &resourceId) const;
    std::optional<qint64> sourceAt(const Angle &angle, qint64 ticks, bool sound = false) const;
    const Angle *angleForSound(const QString &resourceId) const;
    const TimelineClip *programClipAt(qint64 ticks) const;
    bool available(const Angle &angle) const;
    bool fail(const QString &message);
    QVariantMap refuse(const QString &message);
    void followPlayer();

    QPointer<TimelineModel> m_timeline;
    QPointer<ResourceResolver> m_resolver;
    QPointer<MediaPlayer> m_player;
    QMetaObject::Connection m_timelineLink;
    QMetaObject::Connection m_playerLink;
    QString m_trackId;
    QString m_audioTrackId;
    qint64 m_syncStart = 0;
    bool m_audioFollows = true;
    qint64 m_playhead = 0;
    QVector<Angle> m_angles;
    QString m_lastError;
};

} // namespace QindaTK
