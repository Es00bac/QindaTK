// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "motion_effect_registry.h"

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
class MotionFrameRenderer : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::MotionComposition *composition READ composition WRITE setComposition NOTIFY compositionChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    explicit MotionFrameRenderer(QObject *parent = nullptr);
    MotionComposition *composition() const { return m_composition; }
    void setComposition(MotionComposition *composition);
    QString lastError() const { return m_error; }
    QImage frame() const;
    bool setSourceImage(const QString &sourceId, const QImage &image);
    bool registerEffectProcessor(const QString &effectId, MotionEffectRegistry::Processor processor);
    Q_INVOKABLE bool loadSource(const QString &sourceId, const QUrl &url);
    Q_INVOKABLE void removeSource(const QString &sourceId);
    Q_INVOKABLE QVariantMap render(const QString &timeTicks, int width, int height);
    Q_INVOKABLE bool exportPng(const QUrl &url);
signals:
    void compositionChanged();
    void frameChanged();
    void errorChanged();
private:
    void setError(const QString &error);
    QPointer<MotionComposition> m_composition;
    QHash<QString, QImage> m_sources;
    MotionEffectRegistry m_effects;
    qsizetype m_sourceBytes = 0;
    mutable QMutex m_frameMutex;
    QImage m_frame;
    QString m_error;
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
