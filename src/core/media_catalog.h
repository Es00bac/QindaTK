// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "job_model.h"
#include "resource_resolver.h"

#include <QAbstractListModel>
#include <QHash>
#include <QPointer>
#include <QStringList>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT (docs/media-playback.md "Media catalog", D-099): the media
// bin's model. An asset is a stable assetId over a ResourceResolver resource
// ID; bins, labels and tags organize assets without touching the resource ID
// that sequences reference, and relinking changes only the resolver's
// location. Rows are the assets matching binFilter and searchText. Metadata,
// proxies and test signals are JobModel jobs; a proxy replaces pictures in
// previews only and must match the original's frame count and rate.
class MediaCatalog : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::ResourceResolver *resolver READ resolver WRITE setResolver NOTIFY resolverChanged)
    Q_PROPERTY(QString binFilter READ binFilter WRITE setBinFilter NOTIFY filterChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY filterChanged)
    Q_PROPERTY(QVariantList bins READ bins NOTIFY binsChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged)
    Q_PROPERTY(QString proxyDirectory READ proxyDirectory WRITE setProxyDirectory NOTIFY proxyDirectoryChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(QindaTK::JobModel *jobs READ jobs CONSTANT)
public:
    static constexpr int kMaxAssets = 100000;
    static constexpr int kMaxBins = 1024;
    static constexpr int kMaxRelinkCandidates = 10000;
    enum Role {
        AssetIdRole = Qt::UserRole + 1, ResourceIdRole, LabelRole, CaptionRole, BinIdRole, BinNameRole,
        TagsRole, UriRole, FileNameRole, AvailabilityRole, OnlineRole, MetadataStateRole, MediaTypeRole,
        CodecRole, ResolutionRole, FrameRateRole, FrameCountRole, DurationRole, SourceTimecodeRole,
        AudioRole, ProxyStatusRole, ProxyResourceIdRole, ProxyActiveRole, ProxyAvailableRole, ErrorRole,
        MetadataRole
    };
    Q_ENUM(Role)

    explicit MediaCatalog(QObject *parent = nullptr);
    ~MediaCatalog() override;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    ResourceResolver *resolver() const { return m_resolver; }
    void setResolver(ResourceResolver *resolver);
    QString binFilter() const { return m_binFilter; }
    void setBinFilter(const QString &binId);
    QString searchText() const { return m_searchText; }
    void setSearchText(const QString &text);
    QVariantList bins() const;
    int count() const { return int(m_visible.size()); }
    int totalCount() const { return int(m_assets.size()); }
    QString proxyDirectory() const { return m_proxyDirectory; }
    void setProxyDirectory(const QString &directory);
    QString lastError() const { return m_lastError; }
    JobModel *jobs() const { return m_jobs; }

    // Registers `uri` as a new external resource and adds an asset for it.
    // Returns the asset ID, or "" with lastError.
    Q_INVOKABLE QString importMedia(const QString &uri, const QString &binId = {}, const QString &label = {});
    // Adds an asset over a resource the host already registered.
    Q_INVOKABLE bool addAsset(const QString &assetId, const QString &resourceId, const QString &label = {},
                              const QString &binId = {});
    Q_INVOKABLE bool removeAsset(const QString &assetId);
    Q_INVOKABLE QString addBin(const QString &name);
    Q_INVOKABLE bool renameBin(const QString &binId, const QString &name);
    // Assets in a removed bin move to the catalog root (binId "").
    Q_INVOKABLE bool removeBin(const QString &binId);
    Q_INVOKABLE bool moveToBin(const QStringList &assetIds, const QString &binId);
    Q_INVOKABLE bool setLabel(const QString &assetId, const QString &label);
    Q_INVOKABLE bool setTags(const QString &assetId, const QStringList &tags);
    Q_INVOKABLE bool relink(const QString &assetId, const QString &uri);
    // Relinks offline assets to files in `folderUri` whose content hash
    // matches the resolver's expected hash; returns the number relinked.
    Q_INVOKABLE int relinkMissingFrom(const QString &folderUri);
    // Starts a proxy job (FFV1/PCM Matroska, at most maxHeight lines) and
    // returns its JobModel ID, or "" with lastError.
    Q_INVOKABLE QString createProxy(const QString &assetId, int maxHeight = 540);
    Q_INVOKABLE bool setProxyActive(const QString &assetId, bool active);
    Q_INVOKABLE void refresh(const QString &assetId = {});
    Q_INVOKABLE QVariantMap asset(const QString &assetId) const;
    Q_INVOKABLE QString assetIdForResource(const QString &resourceId) const;
    Q_INVOKABLE QVariantMap contentMap() const;
    Q_INVOKABLE bool loadContent(const QVariantMap &content);
    // Writes a bars, frame-code and sync-pop test signal (docs/media-playback.md
    // "Test signal"); options: width, height, frameRateNumerator/Denominator
    // (decimal strings), frames, sampleRate, channels, popInterval.
    Q_INVOKABLE QString generateTestSignal(const QString &uri, const QVariantMap &options = {});

    // The local proxy path to decode instead of `resourceId`, when that
    // asset's proxy is ready, active and online; otherwise "".
    QString activeProxyUri(const QString &resourceId) const;

signals:
    void resolverChanged();
    void filterChanged();
    void binsChanged();
    void countChanged();
    void proxyDirectoryChanged();
    void errorChanged();
    void assetChanged(const QString &assetId);
    void proxyChanged(const QString &resourceId);
    // kind: "metadata", "proxy" or "testSignal".
    void jobFinished(const QString &jobId, const QString &kind, const QString &assetId, bool ok,
                     const QString &error);

private:
    struct Asset {
        QString assetId;
        QString resourceId;
        QString label;
        QString binId;
        QStringList tags;
        QVariantMap entry; // cached resolver lookup, refreshed on resourceChanged
        QVariantMap metadata;
        QString metadataState = QStringLiteral("pending"); // pending, ready, failed, missing
        QString error;
        QString proxyResourceId;
        QString proxyStatus = QStringLiteral("none"); // none, creating, ready, failed
        QString proxyError;
        bool proxyActive = false;
        quint64 probeGeneration = 0;
    };
    struct Bin {
        QString binId;
        QString name;
    };
    struct PendingJob {
        QString kind;
        QString assetId;
        quint64 generation = 0;
        QString path;
    };
    int assetIndex(const QString &assetId) const;
    int binIndex(const QString &binId) const;
    QString binName(const QString &binId) const;
    QVariantMap resource(const QString &resourceId) const;
    void refreshEntry(Asset &asset) const;
    QVariantMap assetMap(const Asset &asset) const;
    QString proxyUriFor(const Asset &asset) const;
    bool matches(const Asset &asset) const;
    void rebuildVisible();
    void assetUpdated(int index);
    void probe(int index);
    void onResourceChanged(const QString &resourceId);
    void onJobFinished(const QString &jobId, JobModel::JobState state, const QVariantMap &result,
                       const QString &error);
    bool fail(const QString &message);
    void clearError();

    QPointer<ResourceResolver> m_resolver;
    JobModel *m_jobs = nullptr;
    QVector<Asset> m_assets;
    QVector<int> m_visible;
    QVector<Bin> m_bins;
    QHash<QString, PendingJob> m_pending;
    QString m_binFilter;
    QString m_searchText;
    QString m_proxyDirectory;
    QString m_lastError;
    quint64 m_serial = 0;
};

} // namespace QindaTK
