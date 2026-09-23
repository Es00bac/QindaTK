// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "canvas_document.h"

#include <QPointer>
#include <QPolygonF>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT (docs/canvas.md#hit-testing): hits resolve to (object id,
// part, index). Order is deterministic: topmost paint order first, ties by
// descending id. Within one object the most specific part wins: Vertex, then
// Segment/Stroke, then Fill/ImageTile.
struct CanvasHit {
    CanvasObjectId object = 0;
    CanvasLayerId layer = 0;
    CanvasPart part = CanvasPart::Fill;
    int index = -1;       // vertex, segment or image-tile index; -1 = whole
    qreal distance = 0;   // document units from the probe to the part
    bool locked = false;  // effective lock (object or layer)
    qint64 paintKey = 0;
};

enum class CanvasLockPolicy : quint8 {
    Skip,   // locked objects are transparent to picking (editing default)
    Report, // locked objects are returned, flagged, and do not occlude
    Block,  // the topmost locked object is returned and occludes the rest
};

enum class CanvasRegionMode : quint8 { Intersect, Contain };

struct CanvasHitPolicy {
    CanvasLockPolicy locked = CanvasLockPolicy::Skip;
    bool includeHidden = false;
    int maxResults = 0; // 0 = all
};

struct CanvasHitStats {
    qsizetype candidates = 0;
    qsizetype tested = 0;
};

class HitTestService : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::CanvasDocumentAdapter *document READ document WRITE setDocument NOTIFY changed)
    Q_PROPERTY(qreal tolerance READ tolerance WRITE setTolerance NOTIFY changed)
public:
    explicit HitTestService(QObject *parent = nullptr) : QObject(parent) {}
    CanvasDocumentAdapter *document() const { return m_document; }
    void setDocument(CanvasDocumentAdapter *document);
    qreal tolerance() const { return m_tolerance; }
    void setTolerance(qreal pixels);

    // Screen-pixel tolerance -> document units. The view transform is a
    // similarity (rotation + uniform zoom), so one factor is exact. Input
    // positions are logical pixels; devicePixelRatio does not enter here.
    [[nodiscard]] static qreal documentTolerance(qreal tolerancePixels, qreal zoom);

    [[nodiscard]] static QVector<CanvasHit> hitPoint(const CanvasDocumentAdapter &document,
                                                     QPointF documentPoint,
                                                     qreal toleranceDocument,
                                                     const CanvasHitPolicy &policy = {},
                                                     CanvasHitStats *stats = nullptr);
    [[nodiscard]] static QVector<CanvasObjectId> hitRegion(const CanvasDocumentAdapter &document,
                                                           const QPolygonF &documentRegion,
                                                           CanvasRegionMode mode,
                                                           const CanvasHitPolicy &policy = {},
                                                           CanvasHitStats *stats = nullptr);
    [[nodiscard]] static QVariantMap toVariant(const CanvasHit &hit);
    [[nodiscard]] static QString partName(CanvasPart part);
    [[nodiscard]] static CanvasLockPolicy lockPolicy(const QString &name);

    // QML facade over the bound document, in DOCUMENT coordinates, with the
    // configured pixel tolerance at `zoom`. lockPolicy: "skip"|"report"|"block".
    Q_INVOKABLE QVariantList pointAt(qreal x, qreal y, qreal zoom = 1.0,
                                     const QString &lockPolicy = QStringLiteral("skip"),
                                     int maxResults = 0) const;
    Q_INVOKABLE QVariantList regionIds(const QRectF &documentRect, bool contain = false) const;

signals:
    void changed();

private:
    QPointer<CanvasDocumentAdapter> m_document;
    qreal m_tolerance = 4.0;
};

} // namespace QindaTK
