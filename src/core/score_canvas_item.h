// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "score_paint.h"

#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT (D-179): ScoreView's drawing surface (objectName
// "scoreCanvas"). It paints `snapshot` with Score::paintSnapshot, the same
// painter PDF and image output use; ScoreView supplies the theme colours.
// AGENT-GUARD: paint() may run on the render thread; it reads the copied
// properties only and emits nothing.
class ScoreCanvasItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantMap snapshot READ snapshot WRITE setSnapshot NOTIFY appearanceChanged)
    Q_PROPERTY(QString selectedEventId READ selectedEventId WRITE setSelectedEventId NOTIFY appearanceChanged)
    Q_PROPERTY(double cursorX READ cursorX WRITE setCursorX NOTIFY appearanceChanged)
    Q_PROPERTY(int maximumEvents READ maximumEvents WRITE setMaximumEvents NOTIFY appearanceChanged)
    Q_PROPERTY(QColor surfaceColor READ surfaceColor WRITE setSurfaceColor NOTIFY appearanceChanged)
    Q_PROPERTY(QColor inkColor READ inkColor WRITE setInkColor NOTIFY appearanceChanged)
    Q_PROPERTY(QColor mutedColor READ mutedColor WRITE setMutedColor NOTIFY appearanceChanged)
    Q_PROPERTY(QColor borderColor READ borderColor WRITE setBorderColor NOTIFY appearanceChanged)
    Q_PROPERTY(QColor accentColor READ accentColor WRITE setAccentColor NOTIFY appearanceChanged)
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY appearanceChanged)
public:
    explicit ScoreCanvasItem(QQuickItem *parent = nullptr);
    void paint(QPainter *painter) override;

    QVariantMap snapshot() const { return m_snapshot; }
    void setSnapshot(const QVariantMap &snapshot);
    QString selectedEventId() const { return m_selected; }
    void setSelectedEventId(const QString &id);
    double cursorX() const { return m_cursorX; }
    void setCursorX(double x);
    int maximumEvents() const { return m_maximumEvents; }
    void setMaximumEvents(int count);
    QColor surfaceColor() const { return m_style.surface; }
    void setSurfaceColor(const QColor &color) { setColor(m_style.surface, color); }
    QColor inkColor() const { return m_style.ink; }
    void setInkColor(const QColor &color) { setColor(m_style.ink, color); }
    QColor mutedColor() const { return m_style.muted; }
    void setMutedColor(const QColor &color) { setColor(m_style.muted, color); }
    QColor borderColor() const { return m_style.border; }
    void setBorderColor(const QColor &color) { setColor(m_style.border, color); }
    QColor accentColor() const { return m_style.accent; }
    void setAccentColor(const QColor &color) { setColor(m_style.accent, color); }
    QString fontFamily() const { return m_style.fontFamily; }
    void setFontFamily(const QString &family);

signals:
    void appearanceChanged();

private:
    void setColor(QColor &target, const QColor &color);
    void changed();

    QVariantMap m_snapshot;
    QString m_selected;
    double m_cursorX = -1;
    int m_maximumEvents = 4096;
    ScorePaintStyle m_style;
};

} // namespace QindaTK
