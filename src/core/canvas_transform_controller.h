// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "canvas_snap_model.h"
#include "tool_controller.h"

#include <QPointer>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT (docs/canvas.md#spatial-transforms): SelectionOverlay reports
// document-space handle motion; this controller owns the one immutable gesture
// snapshot and commits a CanvasEdit only on finish. The document never receives
// preview geometry. ToolController remains the selected-ID source.
class CanvasTransformController : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(SpatialTransform)
    Q_PROPERTY(QindaTK::CanvasDocumentAdapter *document READ document WRITE setDocument NOTIFY changed)
    Q_PROPERTY(QindaTK::ToolController *tools READ tools WRITE setTools NOTIFY changed)
    Q_PROPERTY(QindaTK::CanvasSnapModel *snapModel READ snapModel WRITE setSnapModel NOTIFY changed)
    Q_PROPERTY(QindaTK::CanvasViewState *viewState READ viewState WRITE setViewState NOTIFY changed)
    Q_PROPERTY(QRectF selectionBounds READ selectionBounds NOTIFY changed)
    Q_PROPERTY(QRectF previewBounds READ previewBounds NOTIFY previewChanged)
    Q_PROPERTY(bool active READ active NOTIFY previewChanged)
public:
    explicit CanvasTransformController(QObject *parent = nullptr) : QObject(parent) {}

    CanvasDocumentAdapter *document() const { return m_document; }
    void setDocument(CanvasDocumentAdapter *document);
    ToolController *tools() const { return m_tools; }
    void setTools(ToolController *tools);
    CanvasSnapModel *snapModel() const { return m_snapModel; }
    void setSnapModel(CanvasSnapModel *snapModel);
    CanvasViewState *viewState() const { return m_viewState; }
    void setViewState(CanvasViewState *viewState);
    QRectF selectionBounds() const;
    QRectF previewBounds() const { return m_active ? m_previewBounds : selectionBounds(); }
    bool active() const { return m_active; }

    // The first update captures selected objects and a document revision.
    // `handle`: move or one of SelectionOverlay's eight resize handle names.
    Q_INVOKABLE bool update(const QString &handle, QPointF start, QPointF current,
                            int modifiers = 0);
    Q_INVOKABLE bool finish(const QString &handle);
    Q_INVOKABLE bool cancel(const QString &handle = {});
    // View-pixel keyboard step using the bound pose; commits one edit.
    Q_INVOKABLE bool nudge(const QString &handle, qreal viewDx, qreal viewDy,
                           int modifiers = 0);

signals:
    void changed();
    void previewChanged();
    void gestureFinished(bool committed);
    void gestureCanceled();

private:
    bool capture(const QString &handle);
    void clearPreview();
    static bool supportedHandle(const QString &handle);
    static QPointF handlePoint(const QString &handle, const QRectF &bounds);

    QPointer<CanvasDocumentAdapter> m_document;
    QPointer<ToolController> m_tools;
    QPointer<CanvasSnapModel> m_snapModel;
    QPointer<CanvasViewState> m_viewState;
    QVector<CanvasObject> m_before;
    CanvasEdit m_previewEdit;
    QRectF m_baseBounds;
    QRectF m_previewBounds;
    QString m_handle;
    quint64 m_revision = 0;
    bool m_active = false;
    bool m_committing = false;
};

} // namespace QindaTK
