// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "motion_effect_registry.h"
#include "tracking_roto_model.h"

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QUrl>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
class MotionComposition;

// CPU source-over compositor for MotionComposition's evaluated 2D layers.
// Hosts supply decoded QImages; loadSource is a local-file convenience.
// AGENT-GUARD: every input that can change pixels (layers, animation curves
// and previews, sources, effect processors, roto masks) must call
// invalidate(); otherwise the bounded frame cache returns stale frames.
class MotionFrameRenderer : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::MotionComposition *composition READ composition WRITE setComposition NOTIFY compositionChanged)
    Q_PROPERTY(QindaTK::TrackingRotoModel *rotoModel READ rotoModel WRITE setRotoModel NOTIFY rotoModelChanged)
    Q_PROPERTY(QString maskTicksPerFrame READ maskTicksPerFrame WRITE setMaskTicksPerFrame NOTIFY maskTimingChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(QString contentRevision READ contentRevision NOTIFY contentInvalidated)
    Q_PROPERTY(QString frameTicks READ frameTicks NOTIFY frameChanged)
    Q_PROPERTY(QString frameRevision READ frameRevision NOTIFY frameChanged)
    Q_PROPERTY(int frameCacheLimit READ frameCacheLimit WRITE setFrameCacheLimit NOTIFY cacheChanged)
    Q_PROPERTY(int cachedFrameCount READ cachedFrameCount NOTIFY cacheChanged)
public:
    static constexpr int kMaxCachedFrames = 64;
    static constexpr qsizetype kMaxCacheBytes = 128 * 1024 * 1024;
    explicit MotionFrameRenderer(QObject *parent = nullptr);
    QString contentRevision() const { return QString::number(m_contentRevision); }
    QString frameTicks() const { return m_frameTicks; }
    QString frameRevision() const { return QString::number(m_frameRevision); }
    int frameCacheLimit() const { return m_cacheLimit; }
    void setFrameCacheLimit(int limit);
    int cachedFrameCount() const { return int(m_cache.size()); }
    MotionComposition *composition() const { return m_composition; }
    void setComposition(MotionComposition *composition);
    TrackingRotoModel *rotoModel() const { return m_rotoModel; }
    void setRotoModel(TrackingRotoModel *model);
    // AGENT-CONTRACT (docs/tracking-roto.md "Masks in composition", D-142):
    // a masked layer samples its mask at the source frame its pixels came
    // from (setSourceImage's sourceFrame, which MediaFrameFeed passes), else
    // at floor(layer localTicks / maskTicksPerFrame).
    QString maskTicksPerFrame() const { return QString::number(m_maskTicksPerFrame); }
    void setMaskTicksPerFrame(const QString &ticks);
    QString lastError() const { return m_error; }
    QImage frame() const;
    bool setSourceImage(const QString &sourceId, const QImage &image, qint64 sourceFrame = -1);
    Q_INVOKABLE QString sourceFrame(const QString &sourceId) const;
    bool registerEffectProcessor(const QString &effectId, MotionEffectRegistry::Processor processor);
    Q_INVOKABLE bool loadSource(const QString &sourceId, const QUrl &url);
    Q_INVOKABLE void removeSource(const QString &sourceId);
    Q_INVOKABLE QVariantMap render(const QString &timeTicks, int width, int height);
    Q_INVOKABLE bool exportPng(const QUrl &url);
    Q_INVOKABLE void clearFrameCache();
signals:
    void compositionChanged();
    void rotoModelChanged();
    void maskTimingChanged();
    void frameChanged();
    void errorChanged();
    void contentInvalidated();
    void cacheChanged();
private:
    struct CachedFrame {
        QString ticks; // canonical decimal
        int width = 0;
        int height = 0;
        QImage image;
        int drawn = 0;
        QStringList missing;
    };
    void setError(const QString &error);
    void invalidate();
    void watchAnimation();
    void publish(const QImage &image, const QString &ticks);
    void trimCache();
    QPointer<MotionComposition> m_composition;
    QPointer<TrackingRotoModel> m_rotoModel;
    QList<QMetaObject::Connection> m_compositionLinks, m_animationLinks, m_rotoLinks;
    QHash<QString, QImage> m_sources;
    QHash<QString, qint64> m_sourceFrames; // decoded source frame index per sourceId
    qint64 m_maskTicksPerFrame = 1;
    MotionEffectRegistry m_effects;
    qsizetype m_sourceBytes = 0;
    mutable QMutex m_frameMutex;
    QImage m_frame;
    QString m_error;
    QString m_frameTicks;
    quint64 m_contentRevision = 1;
    quint64 m_frameRevision = 0;
    QList<CachedFrame> m_cache; // most recently used first; current content only
    int m_cacheLimit = 8;
};

class MotionFrameItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::MotionFrameRenderer *renderer READ renderer WRITE setRenderer NOTIFY rendererChanged)
public:
    explicit MotionFrameItem(QQuickItem *parent = nullptr);
    MotionFrameRenderer *renderer() const { return m_renderer; }
    void setRenderer(MotionFrameRenderer *renderer);
    void paint(QPainter *painter) override;
signals:
    void rendererChanged();
private:
    QPointer<MotionFrameRenderer> m_renderer;
    QMetaObject::Connection m_frameConnection;
};

} // namespace QindaTK
