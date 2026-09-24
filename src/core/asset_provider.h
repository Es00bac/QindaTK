// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "job_model.h"
#include "resource_resolver.h"
#include <QHash>
#include <QRect>
#include <QSize>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

struct AssetRequest {
    QString resourceId;
    QString uri;
    QString revision;
    QString kind = QStringLiteral("metadata");
    QRect region;
    QSize size;
    qint64 timeTicks = 0;
    qint64 rateNumerator = 1;
    qint64 rateDenominator = 1;
    int quality = 75;
    int columns = 0;
    int pageIndex = 0; // zero-based PDF page
    int rotation = 0;  // clockwise degrees: 0, 90, 180, 270
};

struct AssetResult {
    QVariantMap metadata;
    QByteArray bytes;
    QString mimeType;
    QVariantList peaks;
    OperationError error;
};

// Implementations run in the JobModel pool, never on a QML/render thread.
class AssetBackend {
public:
    virtual ~AssetBackend() = default;
    virtual AssetResult fetch(const AssetRequest &request, const CancellationToken &cancel,
                              const JobProgress &progress) = 0;
};
using AssetBackendPtr = std::shared_ptr<AssetBackend>;

class FileAssetBackend final : public AssetBackend {
public:
    AssetResult fetch(const AssetRequest &request, const CancellationToken &cancel,
                      const JobProgress &progress) override;
};

class AssetProvider : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::ResourceResolver *resolver READ resolver WRITE setResolver NOTIFY resolverChanged)
    Q_PROPERTY(QindaTK::JobModel *jobs READ jobs CONSTANT)
    Q_PROPERTY(qint64 cacheBudgetBytes READ cacheBudgetBytes WRITE setCacheBudgetBytes NOTIFY cacheChanged)
    Q_PROPERTY(qint64 cacheBytes READ cacheBytes NOTIFY cacheChanged)
    Q_PROPERTY(int inFlightCount READ inFlightCount NOTIFY changed)
    Q_PROPERTY(int subscriptionCount READ subscriptionCount NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
public:
    explicit AssetProvider(QObject *parent = nullptr);
    ResourceResolver *resolver() const { return m_resolver; }
    void setResolver(ResourceResolver *resolver);
    JobModel *jobs() const { return m_jobs; }
    qint64 cacheBudgetBytes() const { return m_cacheBudget; }
    void setCacheBudgetBytes(qint64 bytes);
    qint64 cacheBytes() const { return m_cacheBytes; }
    int inFlightCount() const { return m_flights.size(); }
    int subscriptionCount() const { return m_subscriptions.size(); }
    QString lastError() const { return m_lastError; }
    void setBackend(AssetBackendPtr backend);
    QString request(AssetRequest request, PropertyDocument *document = nullptr,
                    const QString &viewId = {});
    Q_INVOKABLE QString requestAsset(const QString &resourceId, const QString &kind,
                                    const QVariantMap &options = {},
                                    PropertyDocument *document = nullptr,
                                    const QString &viewId = {});
    Q_INVOKABLE bool release(const QString &subscriptionId);
    Q_INVOKABLE QVariantMap asset(const QString &subscriptionId) const;
    Q_INVOKABLE void invalidateView(const QString &viewId);
    Q_INVOKABLE void invalidateResource(const QString &resourceId);
    Q_INVOKABLE void clearCache();
signals:
    void resolverChanged();
    void cacheChanged();
    void changed();
    void assetChanged(const QString &subscriptionId);
    void assetReady(const QString &subscriptionId);
private:
    struct Subscription {
        QString id;
        QString key;
        QString resourceId;
        QString viewId;
        quint64 viewGeneration = 0;
        SnapshotHeader expected;
        QPointer<PropertyDocument> document;
        QString state = QStringLiteral("loading");
        QVariantMap data;
        QString error;
        QMetaObject::Connection changedConnection;
        QMetaObject::Connection destroyedConnection;
    };
    struct Flight {
        QString jobId;
        QString resourceId;
        QString key;
        quint64 resourceGeneration = 0;
        QStringList subscribers;
    };
    struct CacheEntry {
        QVariantMap data;
        QString resourceId;
        qint64 cost = 0;
        quint64 used = 0;
    };
    static QString keyFor(const AssetRequest &request);
    static QVariantMap resultMap(const AssetResult &result);
    void finishFlight(const QString &jobId, JobModel::JobState state,
                      const QVariantMap &output, const QString &error);
    void trimCache();
    void markStale(const QString &subscriptionId);
    bool current(const Subscription &subscription) const;
    QPointer<ResourceResolver> m_resolver;
    JobModel *m_jobs = nullptr;
    AssetBackendPtr m_backend;
    QHash<QString, Subscription> m_subscriptions;
    QHash<QString, Flight> m_flights;
    QHash<QString, QString> m_jobKeys;
    QHash<QString, CacheEntry> m_cache;
    QHash<QString, quint64> m_viewGenerations;
    QHash<QString, quint64> m_resourceGenerations;
    qint64 m_cacheBudget = 32 * 1024 * 1024;
    qint64 m_cacheBytes = 0;
    quint64 m_access = 0;
    QString m_lastError;
};

} // namespace QindaTK
