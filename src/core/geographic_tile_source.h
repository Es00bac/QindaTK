// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QCache>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QtQml/qqmlregistration.h>
#include <memory>

namespace QindaTK {
// AGENT-CONTRACT: GeographicMapCanvas supplies visible XYZ keys; this source
// owns local/network decoding, stale-result rejection and bounded caches.
// Image export reads local or cached tiles without replacing the live visible set.
class GeographicTileSource : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString attribution READ attribution NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(int minZoom READ minZoom NOTIFY changed)
    Q_PROPERTY(int maxZoom READ maxZoom NOTIFY changed)
    Q_PROPERTY(int tileSize READ tileSize NOTIFY changed)
    Q_PROPERTY(int cacheLimitBytes READ cacheLimitBytes WRITE setCacheLimitBytes NOTIFY cacheChanged)
    Q_PROPERTY(int cacheBytes READ cacheBytes NOTIFY cacheChanged)
    Q_PROPERTY(int cachedTileCount READ cachedTileCount NOTIFY cacheChanged)
    Q_PROPERTY(QString networkTemplate READ networkTemplate NOTIFY changed)
    Q_PROPERTY(QString diskCacheDirectory READ diskCacheDirectory WRITE setDiskCacheDirectory NOTIFY diskCacheChanged)
    Q_PROPERTY(qint64 diskCacheLimitBytes READ diskCacheLimitBytes WRITE setDiskCacheLimitBytes NOTIFY diskCacheChanged)
    Q_PROPERTY(qint64 diskCacheBytes READ diskCacheBytes NOTIFY diskCacheChanged)
public:
    explicit GeographicTileSource(QObject *parent = nullptr);
    ~GeographicTileSource() override;
    QString attribution() const { return m_attribution; }
    QString lastError() const { return m_lastError; }
    bool loaded() const { return !m_paths.isEmpty() || !m_urlTemplate.isEmpty(); }
    int minZoom() const { return m_minZoom; }
    int maxZoom() const { return m_maxZoom; }
    int tileSize() const { return m_tileSize; }
    int cacheLimitBytes() const { return m_cacheLimitBytes; }
    int cacheBytes() const { return m_cache.totalCost() * 1024; }
    int cachedTileCount() const { return m_cache.size(); }
    QString networkTemplate() const { return m_urlTemplate; }
    QString diskCacheDirectory() const { return m_diskCacheDirectory; }
    void setDiskCacheDirectory(const QString &directory);
    qint64 diskCacheLimitBytes() const { return m_diskCacheLimitBytes; }
    void setDiskCacheLimitBytes(qint64 bytes);
    qint64 diskCacheBytes() const;
    void setCacheLimitBytes(int bytes);
    Q_INVOKABLE bool loadManifest(const QString &path);
    Q_INVOKABLE bool useNetworkTemplate(const QString &urlTemplate, const QString &attribution,
                                        int minZoom = 0, int maxZoom = 18, int tileSize = 256);
    Q_INVOKABLE void clearDiskCache();
    Q_INVOKABLE void clear();
    static QString key(int z, int x, int y);
    void setVisibleTiles(const QSet<QString> &keys);
    // 0 = absent, 1 = loading, 2 = cached, 3 = decode failed.
    int tileState(const QString &key) const;
    QImage tileImage(const QString &key);
    QImage imageForExport(const QString &key);
signals:
    void changed();
    void cacheChanged();
    void diskCacheChanged();
    void tileReady(const QString &key);
private:
    struct NetworkState;
    void request(const QString &key);
    void requestNetwork(const QString &key, quint64 serial);
    void cancelNetworkRequests(const QSet<QString> &keep);
    void configureDiskCache();
    QImage cachedNetworkImage(const QString &key);
    void finishTile(const QString &key, quint64 serial, QImage image);
    QHash<QString, QString> m_paths;
    QCache<QString, QImage> m_cache;
    QSet<QString> m_visible, m_failed;
    QHash<QString, quint64> m_pending;
    QString m_attribution, m_lastError;
    QString m_urlTemplate, m_diskCacheDirectory;
    int m_minZoom = 0, m_maxZoom = 18, m_tileSize = 256;
    int m_cacheLimitBytes = 16 * 1024 * 1024;
    qint64 m_diskCacheLimitBytes = 64 * 1024 * 1024;
    quint64 m_requestSerial = 0;
    std::unique_ptr<NetworkState> m_network;
};
}
