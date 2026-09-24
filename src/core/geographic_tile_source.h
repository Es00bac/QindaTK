// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QCache>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
// AGENT-CONTRACT: GeographicMapCanvas supplies visible XYZ keys; this source
// owns local decoding, stale-result rejection and the bounded image cache.
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
public:
    explicit GeographicTileSource(QObject *parent = nullptr);
    QString attribution() const { return m_attribution; }
    QString lastError() const { return m_lastError; }
    bool loaded() const { return !m_paths.isEmpty(); }
    int minZoom() const { return m_minZoom; }
    int maxZoom() const { return m_maxZoom; }
    int tileSize() const { return m_tileSize; }
    int cacheLimitBytes() const { return m_cacheLimitBytes; }
    int cacheBytes() const { return m_cache.totalCost() * 1024; }
    int cachedTileCount() const { return m_cache.size(); }
    void setCacheLimitBytes(int bytes);
    Q_INVOKABLE bool loadManifest(const QString &path);
    Q_INVOKABLE void clear();
    static QString key(int z, int x, int y);
    void setVisibleTiles(const QSet<QString> &keys);
    // 0 = absent, 1 = loading, 2 = cached, 3 = decode failed.
    int tileState(const QString &key) const;
    QImage tileImage(const QString &key);
signals:
    void changed();
    void cacheChanged();
    void tileReady(const QString &key);
private:
    void request(const QString &key);
    QHash<QString, QString> m_paths;
    QCache<QString, QImage> m_cache;
    QSet<QString> m_visible, m_failed;
    QHash<QString, quint64> m_pending;
    QString m_attribution, m_lastError;
    int m_minZoom = 0, m_maxZoom = 18, m_tileSize = 256;
    int m_cacheLimitBytes = 16 * 1024 * 1024;
    quint64 m_requestSerial = 0;
};
}
