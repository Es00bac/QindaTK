// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QColor>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

class TiledRasterDocument : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int width READ width NOTIFY changed)
    Q_PROPERTY(int height READ height NOTIFY changed)
    Q_PROPERTY(QStringList layerIds READ layerIds NOTIFY changed)
    Q_PROPERTY(QString imageDataUrl READ imageDataUrl NOTIFY changed)
    Q_PROPERTY(qulonglong revision READ revision NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit TiledRasterDocument(QObject *parent = nullptr);
    int width() const { return m_size.width(); }
    int height() const { return m_size.height(); }
    QStringList layerIds() const;
    QString imageDataUrl() const { return m_imageDataUrl; }
    qulonglong revision() const { return m_revision; }
    bool canUndo() const { return m_cursor > 0; }
    bool canRedo() const { return m_cursor < m_history.size(); }
    QString error() const { return m_error; }

    Q_INVOKABLE bool resizeCanvas(int width, int height);
    Q_INVOKABLE bool createLayer(const QString &id, const QString &name);
    Q_INVOKABLE bool addMask(const QString &layerId);
    Q_INVOKABLE bool setMaskEnabled(const QString &layerId, bool enabled);
    Q_INVOKABLE bool beginStroke(const QString &layerId, bool paintMask,
                                 const QColor &color, const QVariantMap &preset);
    Q_INVOKABLE bool sampleStroke(qreal x, qreal y, qreal pressure = 1.0,
                                  qreal tiltX = 0.0, qreal tiltY = 0.0);
    Q_INVOKABLE bool endStroke();
    Q_INVOKABLE void cancelStroke();
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE QImage composite() const;
    Q_INVOKABLE bool exportPng(const QString &path);
    Q_INVOKABLE QVariantMap layerInfo(const QString &layerId) const;

signals:
    void changed();

private:
    struct Sample { QPointF point; qreal pressure = 1; qreal tiltX = 0; qreal tiltY = 0; };
    struct Brush {
        qreal diameter = 24;
        qreal hardness = 0.8;
        qreal spacing = 0.2;
        qreal opacity = 1.0;
        qreal pressureSize = 0.5;
        qreal pressureOpacity = 0.35;
    };
    struct Layer {
        QString id;
        QString name;
        QVector<QImage> pixels;
        QVector<QImage> mask;
        bool hasMask = false;
        bool maskEnabled = true;
        bool visible = true;
    };
    struct TileChange { int index = -1; QImage before; QImage after; };
    struct StrokeEdit { QString layerId; bool mask = false; QVector<TileChange> tiles; qsizetype byteCost = 0; };
    int columns() const;
    QRect tileRect(int index) const;
    int layerIndex(const QString &id) const;
    QImage &writableTile(int layerIndex, int tileIndex, bool mask);
    bool rememberBefore(int tileIndex);
    void stamp(const Sample &sample);
    void paintTile(QImage &tile, int tileIndex, const Sample &sample,
                   qreal diameter, qreal opacity, qreal flatten, qreal angle);
    void applyEdit(const StrokeEdit &edit, bool forward);
    void updatePreview();
    void setError(const QString &error);
    static QString pngDataUrl(const QImage &image);

    QSize m_size{512, 512};
    QVector<Layer> m_layers;
    QVector<StrokeEdit> m_history;
    qsizetype m_historyBytes = 0;
    qsizetype m_allocatedTileBytes = 0;
    qsizetype m_cursor = 0;
    qulonglong m_revision = 0;
    QString m_imageDataUrl;
    QString m_error;
    bool m_strokeActive = false;
    bool m_strokeMask = false;
    bool m_hasPrevious = false;
    int m_strokeLayer = -1;
    QColor m_strokeColor;
    Brush m_brush;
    Sample m_previous;
    QHash<int, QImage> m_strokeBefore;
    QString m_strokeError;
    int m_strokeStampCount = 0;
    qsizetype m_strokePixelWork = 0;
};

} // namespace QindaTK
