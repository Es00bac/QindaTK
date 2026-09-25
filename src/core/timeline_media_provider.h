// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "resource_resolver.h"
#include "timeline_types.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <memory>

namespace QindaTK {

struct TimelineMediaRequest {
    quint64 serial = 0;
    QString requestId;
    QString ownerKey;      // a newer request from the same owner supersedes older ones
    QString resourceId;
    QString kind;          // "peaks" or "frames"
    qint64 sourceStart = 0; // exact source ticks in the timeline timebase
    qint64 sourceEnd = 0;
    TimelineRate ticksPerSecond;
    int columns = 0;       // peak columns, or the number of frames
    quint64 resourceGeneration = 0;
    QString location;      // the media URI the resolver maps resourceId to (the ID itself without one)
};

struct TimelineMediaResult {
    QVector<float> peaks;  // magnitude 0..1 per column
    QStringList frames;    // image URLs, one per requested frame
    OperationError error;
};

// AGENT-CONTRACT: host media seam for EditTimeline delegates. start() may run
// work on any thread and calls completion exactly once, from any thread.
// It must stop early when the token is cancelled; the provider discards a
// cancelled, superseded or invalidated completion either way. A backend
// never touches QObjects, QML items or the timeline model.
class TimelineMediaBackend {
public:
    using Completion = std::function<void(TimelineMediaResult)>;
    virtual ~TimelineMediaBackend() = default;
    virtual QString backendId() const = 0;
    virtual void start(const TimelineMediaRequest &request, CancellationToken cancel,
                       Completion completion) = 0;
};
using TimelineMediaBackendPtr = std::shared_ptr<TimelineMediaBackend>;

// Reference backend: a deterministic synthetic signal per resource ID,
// reduced to peaks on the thread pool. It is labeled synthetic; it decodes no
// media and reports frame requests as unsupported rather than inventing them.
TimelineMediaBackendPtr createSyntheticTimelineMediaBackend();

// Decoded backend: audio peaks read from the media file at request.location
// (MediaDecoder, FFmpeg). Each column is the largest absolute sample of every
// channel in its exact source span; media without sound fails the request
// (ErrorCode::Unsupported) rather than drawing silence. No video frames.
TimelineMediaBackendPtr createDecodedTimelineMediaBackend();

class TimelineMediaProvider : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool referenceBackend READ referenceBackend WRITE setReferenceBackend NOTIFY changed)
    Q_PROPERTY(bool decodedBackend READ decodedBackend WRITE setDecodedBackend NOTIFY changed)
    Q_PROPERTY(QindaTK::ResourceResolver *resolver READ resolver WRITE setResolver NOTIFY changed)
    Q_PROPERTY(QString backendId READ backendId NOTIFY changed)
    Q_PROPERTY(int maxInFlight READ maxInFlight WRITE setMaxInFlight NOTIFY changed)
    Q_PROPERTY(int inFlightCount READ inFlightCount NOTIFY statisticsChanged)
    Q_PROPERTY(int queuedCount READ queuedCount NOTIFY statisticsChanged)
    Q_PROPERTY(int requestCount READ requestCount NOTIFY statisticsChanged)
    Q_PROPERTY(QString completedCount READ completedCount NOTIFY statisticsChanged)
    Q_PROPERTY(QString cancelledCount READ cancelledCount NOTIFY statisticsChanged)
    Q_PROPERTY(QString staleDropped READ staleDropped NOTIFY statisticsChanged)
    Q_PROPERTY(int cacheEntries READ cacheEntries NOTIFY statisticsChanged)
public:
    explicit TimelineMediaProvider(QObject *parent = nullptr);
    ~TimelineMediaProvider() override;

    void setBackend(TimelineMediaBackendPtr backend);
    bool referenceBackend() const { return m_reference; }
    void setReferenceBackend(bool enabled);
    bool decodedBackend() const { return m_decoded; }
    void setDecodedBackend(bool enabled);
    ResourceResolver *resolver() const;
    void setResolver(ResourceResolver *resolver);
    QString backendId() const;
    int maxInFlight() const { return m_maxInFlight; }
    void setMaxInFlight(int count);
    int inFlightCount() const { return m_inFlight; }
    int queuedCount() const { return int(m_queue.size()); }
    int requestCount() const { return int(m_records.size()); }
    QString completedCount() const { return QString::number(m_completed); }
    QString cancelledCount() const { return QString::number(m_cancelled); }
    QString staleDropped() const { return QString::number(m_stale); }
    int cacheEntries() const { return int(m_cache.size()); }

    QString request(TimelineMediaRequest request);
    Q_INVOKABLE QString requestPeaks(const QString &ownerKey, const QString &resourceId,
                                     const QString &sourceStartTicks, const QString &sourceEndTicks,
                                     const QString &ticksPerSecondNumerator,
                                     const QString &ticksPerSecondDenominator, int columns);
    Q_INVOKABLE QString requestFrames(const QString &ownerKey, const QString &resourceId,
                                      const QString &sourceStartTicks, const QString &sourceEndTicks,
                                      const QString &ticksPerSecondNumerator,
                                      const QString &ticksPerSecondDenominator, int count);
    Q_INVOKABLE bool cancel(const QString &requestId);
    Q_INVOKABLE void cancelOwner(const QString &ownerKey);
    Q_INVOKABLE QVariantMap result(const QString &requestId) const;
    Q_INVOKABLE void invalidateResource(const QString &resourceId);
    Q_INVOKABLE void clearCache();

signals:
    void changed();
    void statisticsChanged();
    void ready(const QString &requestId, const QString &ownerKey);
    void failed(const QString &requestId, const QString &ownerKey, const QString &error);
    void resourceInvalidated(const QString &resourceId);
    // A new backend replaced the old one; owners should request again.
    void backendChanged();

private:
    struct Record {
        TimelineMediaRequest request;
        QString key;
        QString state = QStringLiteral("queued"); // queued, running, ready, failed, cancelled
        TimelineMediaResult result;
        CancellationSource cancel;
    };
    struct Delivery;
    QString keyFor(const TimelineMediaRequest &request) const;
    void pump();
    void schedulePump();
    void finish(quint64 serial, TimelineMediaResult result);
    void drop(const QString &requestId, bool countCancel);
    void remember(const QString &key, const TimelineMediaResult &result);
    void retainFinished(const QString &requestId);

    TimelineMediaBackendPtr m_backend;
    bool m_reference = false;
    bool m_decoded = false;
    QPointer<ResourceResolver> m_resolver;
    QMetaObject::Connection m_resolverConnection;
    int m_maxInFlight = 4;
    int m_inFlight = 0;
    bool m_pumpScheduled = false;
    quint64 m_serial = 0;
    quint64 m_completed = 0;
    quint64 m_cancelled = 0;
    quint64 m_stale = 0;
    QHash<QString, Record> m_records;
    QHash<quint64, QString> m_serials;
    QHash<QString, QString> m_owners;       // owner key -> newest request ID
    QStringList m_queue;
    QStringList m_finished;                 // finished requests without an owner, oldest first
    QHash<QString, quint64> m_generations;  // resource ID -> generation
    QHash<QString, TimelineMediaResult> m_cache;
    QStringList m_cacheOrder;
    std::shared_ptr<Delivery> m_delivery;
};

} // namespace QindaTK
