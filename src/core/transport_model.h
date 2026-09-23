// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "time_map.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

namespace QindaTK {

struct TransportSnapshot {
    quint64 revision = 0; // Monotonic engine revision, not a GUI frame count.
    qint64 positionSample = 0;
    double playbackRate = 1.0;
    bool playing = false;
    bool recording = false;
    bool loopEnabled = false;
    bool punchEnabled = false;
    qint64 loopStart = 0;
    qint64 loopEnd = 0;
    qint64 punchStart = 0;
    qint64 punchEnd = 0;
    bool canPlay = false;
    bool canSeek = false;
    bool canRecord = false;
    bool canLoop = false;
    bool canPunch = false;
    QString playUnavailableReason;
    QString seekUnavailableReason;
    QString recordUnavailableReason;
    QString loopUnavailableReason;
    QString punchUnavailableReason;
    QString deviceError;
};

struct TransportCommand {
    enum class Kind { Play, Stop, Seek, Record, Loop, Punch };
    QString id;
    Kind kind = Kind::Play;
    qint64 positionSample = 0;
    bool enabled = false;
    qint64 rangeStart = 0;
    qint64 rangeEnd = 0;
};

struct TransportReply {
    QString requestId;
    bool accepted = false;
    QString error;
    TransportSnapshot snapshot;
};

// Host engine seam. Submit acknowledgments and pushed snapshots may arrive
// from any thread. A real audio/video engine supplies this adapter later.
class TransportAdapter : public QObject {
    Q_OBJECT
public:
    using ReplyCallback = std::function<void(TransportReply)>;
    using SnapshotCallback = std::function<void(TransportSnapshot)>;
    using QObject::QObject;
    ~TransportAdapter() override = default;
    virtual TransportSnapshot snapshot() const = 0;
    virtual void submit(const TransportCommand &command, ReplyCallback completion) = 0;
    virtual void setSnapshotSink(SnapshotCallback sink) = 0;
};

// GUI projection of acknowledged engine state. No position interpolation or
// QML timer can advance its exact sample position. Unsolicited position
// snapshots are coalesced to at most 30 publishes/second; command replies
// publish promptly. Older revisions cannot roll state backward.
class TransportModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::TimeMap *timeMap READ timeMap WRITE setTimeMap NOTIFY changed)
    Q_PROPERTY(QString positionSample READ positionSample NOTIFY changed)
    Q_PROPERTY(double playbackRate READ playbackRate NOTIFY changed)
    Q_PROPERTY(QString loopStartSample READ loopStartSample NOTIFY changed)
    Q_PROPERTY(QString loopEndSample READ loopEndSample NOTIFY changed)
    Q_PROPERTY(QString punchStartSample READ punchStartSample NOTIFY changed)
    Q_PROPERTY(QString punchEndSample READ punchEndSample NOTIFY changed)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(bool recording READ recording NOTIFY changed)
    Q_PROPERTY(bool loopEnabled READ loopEnabled NOTIFY changed)
    Q_PROPERTY(bool punchEnabled READ punchEnabled NOTIFY changed)
    Q_PROPERTY(bool canPlay READ canPlay NOTIFY changed)
    Q_PROPERTY(bool canSeek READ canSeek NOTIFY changed)
    Q_PROPERTY(bool canRecord READ canRecord NOTIFY changed)
    Q_PROPERTY(bool canLoop READ canLoop NOTIFY changed)
    Q_PROPERTY(bool canPunch READ canPunch NOTIFY changed)
    Q_PROPERTY(QString playUnavailableReason READ playUnavailableReason NOTIFY changed)
    Q_PROPERTY(QString seekUnavailableReason READ seekUnavailableReason NOTIFY changed)
    Q_PROPERTY(QString recordUnavailableReason READ recordUnavailableReason NOTIFY changed)
    Q_PROPERTY(QString loopUnavailableReason READ loopUnavailableReason NOTIFY changed)
    Q_PROPERTY(QString punchUnavailableReason READ punchUnavailableReason NOTIFY changed)
    Q_PROPERTY(QString deviceError READ deviceError NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY changed)
    Q_PROPERTY(QString engineRevision READ engineRevision NOTIFY changed)
public:
    explicit TransportModel(QObject *parent = nullptr);
    ~TransportModel() override;
    void setAdapter(TransportAdapter *adapter);
    TransportAdapter *adapter() const { return m_adapter; }
    TimeMap *timeMap() const { return m_timeMap; }
    void setTimeMap(TimeMap *timeMap);
    QString positionSample() const;
    double playbackRate() const { return m_state.playbackRate; }
    QString loopStartSample() const;
    QString loopEndSample() const;
    QString punchStartSample() const;
    QString punchEndSample() const;
    bool playing() const { return m_state.playing; }
    bool recording() const { return m_state.recording; }
    bool loopEnabled() const { return m_state.loopEnabled; }
    bool punchEnabled() const { return m_state.punchEnabled; }
    bool canPlay() const { return m_state.canPlay && m_state.deviceError.isEmpty(); }
    bool canSeek() const { return m_state.canSeek; }
    bool canRecord() const { return m_state.canRecord && m_state.deviceError.isEmpty(); }
    bool canLoop() const { return m_state.canLoop; }
    bool canPunch() const { return m_state.canPunch; }
    QString playUnavailableReason() const { return m_state.deviceError.isEmpty() ? m_state.playUnavailableReason : m_state.deviceError; }
    QString seekUnavailableReason() const { return m_state.seekUnavailableReason; }
    QString recordUnavailableReason() const { return m_state.deviceError.isEmpty() ? m_state.recordUnavailableReason : m_state.deviceError; }
    QString loopUnavailableReason() const { return m_state.loopUnavailableReason; }
    QString punchUnavailableReason() const { return m_state.punchUnavailableReason; }
    QString deviceError() const { return m_state.deviceError; }
    QString lastError() const { return m_lastError; }
    int pendingCount() const { return m_pending.size(); }
    QString engineRevision() const { return QString::number(m_state.revision); }
    Q_INVOKABLE QString requestPlay();
    Q_INVOKABLE QString requestStop();
    Q_INVOKABLE QString requestSeek(const QString &positionSample);
    Q_INVOKABLE QString requestSeekRelative(const QString &deltaSamples);
    Q_INVOKABLE QString requestRecord(bool enabled);
    Q_INVOKABLE QString requestLoop(bool enabled, const QString &startSample, const QString &endSample);
    Q_INVOKABLE QString requestPunch(bool enabled, const QString &startSample, const QString &endSample);
    Q_INVOKABLE QString formatPosition(const QString &displayMode = QStringLiteral("timecode"),
                                       bool dropFrame = false) const;
    // Adapter callback entry; safe to call from non-GUI threads.
    void offerSnapshot(TransportSnapshot snapshot);
signals:
    void changed();
    void commandFinished(const QString &requestId, bool accepted, const QString &error);
    void rejected(const QString &reason);
private:
    struct DeliveryState {
        std::mutex mutex;
        TransportModel *model = nullptr;
        quint64 generation = 0;
    };
    static std::optional<qint64> parseSample(const QString &text);
    QString request(TransportCommand command);
    QString reject(const QString &reason);
    void acceptReply(TransportReply reply, quint64 adapterGeneration);
    void applySnapshot(const TransportSnapshot &snapshot);
    void publishCoalesced();
    QPointer<TransportAdapter> m_adapter;
    QPointer<TimeMap> m_timeMap;
    TransportSnapshot m_state;
    std::optional<TransportSnapshot> m_coalesced;
    QTimer m_publishTimer;
    QElapsedTimer m_publishElapsed;
    QHash<QString, TransportCommand::Kind> m_pending;
    QString m_lastError;
    quint64 m_requestSerial = 0;
    quint64 m_adapterGeneration = 0;
    bool m_hasSnapshot = false;
    int m_minSnapshotIntervalMs = 33;
    std::shared_ptr<DeliveryState> m_delivery;
};

} // namespace QindaTK
