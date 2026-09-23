// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "timeline_types.h"

#include <QHash>
#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

class TimelineModel;

// AGENT-CONTRACT: the domain decides which edits are legal. TimelineModel's
// planner resolves an intent (clamping, snapping, ripple) with these explicit
// capabilities, then calls validate() with the complete proposed change set
// before any preview is shown as valid and again before commit. A host that
// needs overwrite edits, crossfades, beat-locked clips or engine vetoes
// subclasses this in C++ and returns an OperationError; the model never
// commits a change set this rejects. QML configures the flags only.
class TimelineEditPolicy : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(OverlapMode overlapMode READ overlapMode WRITE setOverlapMode NOTIFY changed)
    Q_PROPERTY(RippleScope rippleScope READ rippleScope WRITE setRippleScope NOTIFY changed)
    Q_PROPERTY(QString minimumClipTicks READ minimumClipTicksText WRITE setMinimumClipTicksText NOTIFY changed)
    Q_PROPERTY(bool allowNegativeTime READ allowNegativeTime WRITE setAllowNegativeTime NOTIFY changed)
    Q_PROPERTY(bool allowBeyondSource READ allowBeyondSource WRITE setAllowBeyondSource NOTIFY changed)
    Q_PROPERTY(bool requireMatchingTrackKind READ requireMatchingTrackKind WRITE setRequireMatchingTrackKind NOTIFY changed)
    Q_PROPERTY(int maxChangesPerEdit READ maxChangesPerEdit WRITE setMaxChangesPerEdit NOTIFY changed)
    Q_PROPERTY(QVariantMap capabilities READ capabilities NOTIFY changed)
public:
    enum OverlapMode { AllowOverlap, RejectOverlap };
    Q_ENUM(OverlapMode)
    enum RippleScope { RippleTrack, RippleAllTracks };
    Q_ENUM(RippleScope)

    explicit TimelineEditPolicy(QObject *parent = nullptr);

    OverlapMode overlapMode() const { return m_overlap; }
    void setOverlapMode(OverlapMode mode);
    RippleScope rippleScope() const { return m_rippleScope; }
    void setRippleScope(RippleScope scope);
    qint64 minimumClipTicks() const { return m_minimumClipTicks; }
    QString minimumClipTicksText() const { return QString::number(m_minimumClipTicks); }
    void setMinimumClipTicksText(const QString &ticks);
    bool setMinimumClipTicks(qint64 ticks);
    bool allowNegativeTime() const { return m_allowNegativeTime; }
    void setAllowNegativeTime(bool allow);
    bool allowBeyondSource() const { return m_allowBeyondSource; }
    void setAllowBeyondSource(bool allow);
    bool requireMatchingTrackKind() const { return m_requireMatchingKind; }
    void setRequireMatchingTrackKind(bool require);
    int maxChangesPerEdit() const { return m_maxChanges; }
    void setMaxChangesPerEdit(int count);

    // Operations: move, moveAcrossTracks, trim, split, delete, ripple,
    // markers, tracks, range. An unavailable operation carries its reason.
    static QStringList operations();
    Q_INVOKABLE bool isAvailable(const QString &operation) const;
    Q_INVOKABLE QString unavailableReason(const QString &operation) const;
    Q_INVOKABLE bool setUnavailable(const QString &operation, const QString &reason);
    Q_INVOKABLE bool setAvailable(const QString &operation);
    QVariantMap capabilities() const;

    // Domain veto for a complete proposed change set. The default accepts.
    virtual OperationError validate(const TimelineModel &model,
                                    const TimelineChangeSet &changes) const;
    OperationError requireOperation(const QString &operation) const;

signals:
    void changed();

private:
    OverlapMode m_overlap = AllowOverlap;
    RippleScope m_rippleScope = RippleTrack;
    qint64 m_minimumClipTicks = 1;
    bool m_allowNegativeTime = false;
    bool m_allowBeyondSource = false;
    bool m_requireMatchingKind = true;
    int m_maxChanges = 100000;
    QHash<QString, QString> m_unavailable;
};

} // namespace QindaTK
