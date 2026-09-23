// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "canvas_document.h"
#include "canvas_view_state.h"

#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT (docs/canvas.md#spatial-snapping): a pure, document-space
// query. A gesture controller supplies the moving point and excluded object
// IDs; it may use the returned point for preview or commit. SnapModel never
// changes the document or holds a gesture, so disabling it mid-drag cannot
// leave a latent constraint behind.
class CanvasSnapModel : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(SnapModel)
    Q_PROPERTY(QindaTK::CanvasDocumentAdapter *document READ document WRITE setDocument NOTIFY changed)
    Q_PROPERTY(QindaTK::CanvasViewState *viewState READ viewState WRITE setViewState NOTIFY changed)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(qreal tolerancePixels READ tolerancePixels WRITE setTolerancePixels NOTIFY changed)
    Q_PROPERTY(qreal gridSpacing READ gridSpacing WRITE setGridSpacing NOTIFY changed)
    Q_PROPERTY(QPointF gridOrigin READ gridOrigin WRITE setGridOrigin NOTIFY changed)
    Q_PROPERTY(QVariantList guides READ guides WRITE setGuides NOTIFY changed)
public:
    explicit CanvasSnapModel(QObject *parent = nullptr) : QObject(parent) {}

    CanvasDocumentAdapter *document() const { return m_document; }
    void setDocument(CanvasDocumentAdapter *document);
    CanvasViewState *viewState() const { return m_viewState; }
    void setViewState(CanvasViewState *state);
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    qreal tolerancePixels() const { return m_tolerancePixels; }
    void setTolerancePixels(qreal value);
    qreal gridSpacing() const { return m_gridSpacing; }
    void setGridSpacing(qreal value);
    QPointF gridOrigin() const { return m_gridOrigin; }
    void setGridOrigin(QPointF origin);
    QVariantList guides() const { return m_guides; }
    void setGuides(const QVariantList &guides);

    // `guides`: {id, orientation: "vertical"|"horizontal", position,
    // visible?}. Host owns their persistence and transactions. Result has
    // point, delta, snapped, and per-axis provenance {source, targetId,
    // feature, position, distancePixels}; an unsnapped axis is an empty map.
    Q_INVOKABLE QVariantMap snapPoint(QPointF point,
                                     const QVariantList &excludedObjectIds = {}) const;

signals:
    void changed();

private:
    QPointer<CanvasDocumentAdapter> m_document;
    QPointer<CanvasViewState> m_viewState;
    bool m_enabled = true;
    qreal m_tolerancePixels = 8.0;
    qreal m_gridSpacing = 0.0;
    QPointF m_gridOrigin;
    QVariantList m_guides;
};

} // namespace QindaTK
