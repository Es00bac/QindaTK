// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QColor>
#include <QImage>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
class MotionFrameRenderer;

// AGENT-CONTRACT: MotionFrameRenderer owns exact-tick layer evaluation and
// source-over composition; this item only retains/tints adjacent display frames.
class MotionOnionSkinItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::MotionFrameRenderer *renderer READ renderer WRITE setRenderer NOTIFY rendererChanged)
    Q_PROPERTY(QString currentTicks READ currentTicks WRITE setCurrentTicks NOTIFY requestChanged)
    Q_PROPERTY(QString previousTicks READ previousTicks WRITE setPreviousTicks NOTIFY requestChanged)
    Q_PROPERTY(QString nextTicks READ nextTicks WRITE setNextTicks NOTIFY requestChanged)
    Q_PROPERTY(int frameWidth READ frameWidth WRITE setFrameWidth NOTIFY requestChanged)
    Q_PROPERTY(int frameHeight READ frameHeight WRITE setFrameHeight NOTIFY requestChanged)
    Q_PROPERTY(QColor previousTint READ previousTint WRITE setPreviousTint NOTIFY styleChanged)
    Q_PROPERTY(QColor nextTint READ nextTint WRITE setNextTint NOTIFY styleChanged)
    Q_PROPERTY(qreal neighborOpacity READ neighborOpacity WRITE setNeighborOpacity NOTIFY styleChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY refreshed)
public:
    explicit MotionOnionSkinItem(QQuickItem *parent = nullptr);
    MotionFrameRenderer *renderer() const { return m_renderer; }
    void setRenderer(MotionFrameRenderer *value);
    QString currentTicks() const { return m_currentTicks; }
    QString previousTicks() const { return m_previousTicks; }
    QString nextTicks() const { return m_nextTicks; }
    int frameWidth() const { return m_width; }
    int frameHeight() const { return m_height; }
    QColor previousTint() const { return m_previousTint; }
    QColor nextTint() const { return m_nextTint; }
    qreal neighborOpacity() const { return m_opacity; }
    QString lastError() const { return m_error; }
    void setCurrentTicks(const QString &value);
    void setPreviousTicks(const QString &value);
    void setNextTicks(const QString &value);
    void setFrameWidth(int value);
    void setFrameHeight(int value);
    void setPreviousTint(const QColor &value);
    void setNextTint(const QColor &value);
    void setNeighborOpacity(qreal value);
    Q_INVOKABLE bool refresh();
    void paint(QPainter *painter) override;
signals:
    void rendererChanged();
    void requestChanged();
    void styleChanged();
    void refreshed();
private:
    QImage tinted(const QImage &image, const QColor &color) const;
    void updateTints();
    QPointer<MotionFrameRenderer> m_renderer;
    QString m_currentTicks, m_previousTicks, m_nextTicks, m_error;
    int m_width = 640, m_height = 360;
    QColor m_previousTint = QColor(255, 96, 128), m_nextTint = QColor(80, 210, 255);
    qreal m_opacity = 0.32;
    QImage m_previous, m_next, m_current, m_tintedPrevious, m_tintedNext;
};
} // namespace QindaTK
