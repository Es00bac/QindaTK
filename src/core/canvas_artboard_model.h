// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "canvas_document.h"

#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT (docs/canvas.md#artboard-model, D-093): connects
// Tk.ArtboardView gestures to CanvasDocument artboards. The document owns
// the boards; this model owns editor state only: the active board (not
// saved, like the active layer) and one gesture preview. A drag previews in
// `boards` and commits one undoable edit on finish. Cancel, a change to the
// board list from elsewhere (undo, another view) or a document reset drops
// the preview without touching the document. Object coordinates never
// change when a board is arranged or resized.
class CanvasArtboardModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::CanvasDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    // The document's records with the running preview applied: bind
    // ArtboardView.boardData to it.
    Q_PROPERTY(QVariantList boards READ boards NOTIFY boardsChanged)
    Q_PROPERTY(QString activeBoardId READ activeBoardId WRITE setActiveBoardId NOTIFY activeBoardChanged)
    Q_PROPERTY(QRectF activeRect READ activeRect NOTIFY boardsChanged)
    Q_PROPERTY(qreal minimumSize READ minimumSize WRITE setMinimumSize NOTIFY minimumSizeChanged)
    Q_PROPERTY(bool previewing READ previewing NOTIFY boardsChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    explicit CanvasArtboardModel(QObject *parent = nullptr) : QObject(parent) {}

    CanvasDocument *document() const { return m_document; }
    void setDocument(CanvasDocument *document);
    QVariantList boards() const;
    QString activeBoardId() const { return m_active; }
    void setActiveBoardId(const QString &id);
    QRectF activeRect() const;
    qreal minimumSize() const { return m_minimumSize; }
    void setMinimumSize(qreal size);
    bool previewing() const { return !m_previewId.isEmpty(); }
    QString lastError() const { return m_lastError; }

    Q_INVOKABLE bool activate(const QString &id);
    // ArtboardView gesture reports, in document coordinates. A resize moves
    // the bottom-right corner and keeps each side at least `minimumSize`.
    Q_INVOKABLE bool previewArrange(const QString &id, const QPointF &start, const QPointF &current);
    Q_INVOKABLE bool previewResize(const QString &id, const QPointF &start, const QPointF &current);
    Q_INVOKABLE bool finish(const QString &id, const QPointF &current);
    Q_INVOKABLE void cancel();
    // A new board over `rect` (normalized); it becomes active. Id or "".
    Q_INVOKABLE QString createBoard(const QRectF &rect, const QString &name = {});
    Q_INVOKABLE bool removeActive();
    Q_INVOKABLE QString boardAt(const QPointF &documentPoint) const;

signals:
    void documentChanged();
    void boardsChanged();
    void activeBoardChanged();
    void minimumSizeChanged();
    void errorChanged();
    void committed(const QString &id);

private:
    bool preview(const QString &id, const QPointF &start, const QPointF &current, bool resize);
    QRectF previewed(const QPointF &current) const;
    void onDocumentChanged();
    bool fail(const QString &message);

    QPointer<CanvasDocument> m_document;
    QVector<CanvasArtboard> m_snapshot; // the board list the preview began on
    quint64 m_snapshotGeneration = 0;
    QString m_active;
    QString m_previewId;
    QRectF m_previewOriginal;
    QRectF m_previewRect;
    QPointF m_previewStart;
    bool m_previewResize = false;
    bool m_committing = false;
    qreal m_minimumSize = 1;
    QString m_lastError;
};

} // namespace QindaTK
