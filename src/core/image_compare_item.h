// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QImage>
#include <QQuickPaintedItem>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Paints both sources through one viewport transform for aligned comparison.
class ImageCompareItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QUrl beforeSource READ beforeSource WRITE setBeforeSource NOTIFY sourcesChanged)
    Q_PROPERTY(QUrl afterSource READ afterSource WRITE setAfterSource NOTIFY sourcesChanged)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY viewChanged)
    Q_PROPERTY(qreal split READ split WRITE setSplit NOTIFY viewChanged)
    Q_PROPERTY(bool showAfter READ showAfter WRITE setShowAfter NOTIFY viewChanged)
    Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY viewChanged)
    Q_PROPERTY(qreal panX READ panX WRITE setPanX NOTIFY viewChanged)
    Q_PROPERTY(qreal panY READ panY WRITE setPanY NOTIFY viewChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY sourcesChanged)
public:
    explicit ImageCompareItem(QQuickItem *parent = nullptr);
    QUrl beforeSource() const { return m_beforeSource; }
    QUrl afterSource() const { return m_afterSource; }
    QString mode() const { return m_mode; }
    qreal split() const { return m_split; }
    bool showAfter() const { return m_showAfter; }
    qreal zoom() const { return m_zoom; }
    qreal panX() const { return m_panX; }
    qreal panY() const { return m_panY; }
    QString lastError() const { return m_error; }
    void setBeforeSource(const QUrl &source);
    void setAfterSource(const QUrl &source);
    void setMode(const QString &mode);
    void setSplit(qreal value);
    void setShowAfter(bool value);
    void setZoom(qreal value);
    void setPanX(qreal value);
    void setPanY(qreal value);
    bool setImages(const QImage &before, const QImage &after);
    QImage renderImage(const QSize &size) const;
    void paint(QPainter *painter) override;
signals:
    void sourcesChanged();
    void viewChanged();
private:
    void reload();
    static QImage read(const QUrl &url);
    QImage difference() const;
    QImage m_before, m_after, m_difference;
    QUrl m_beforeSource, m_afterSource;
    QString m_mode = QStringLiteral("split"), m_error;
    qreal m_split = 0.5, m_zoom = 1.0, m_panX = 0.0, m_panY = 0.0;
    bool m_showAfter = true;
};

} // namespace QindaTK
