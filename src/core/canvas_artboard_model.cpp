// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_artboard_model.h"

#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {
bool finitePoint(const QPointF &point)
{
    return std::isfinite(point.x()) && std::isfinite(point.y());
}
} // namespace

void CanvasArtboardModel::setDocument(CanvasDocument *document)
{
    if (m_document == document) return;
    cancel();
    if (m_document) disconnect(m_document, nullptr, this, nullptr);
    m_document = document;
    if (document) {
        connect(document, &CanvasDocument::changed, this, &CanvasArtboardModel::onDocumentChanged);
        connect(document, &QObject::destroyed, this, [this] {
            onDocumentChanged();
            emit documentChanged();
        });
    }
    onDocumentChanged();
    emit documentChanged();
}

QVariantList CanvasArtboardModel::boards() const
{
    QVariantList out;
    if (!m_document) return out;
    for (const CanvasArtboard &board : m_document->artboardRecords()) {
        CanvasArtboard shown = board;
        if (board.id == m_previewId) shown.rect = m_previewRect;
        out << QVariantMap{{QStringLiteral("id"), shown.id},
                           {QStringLiteral("name"), shown.name},
                           {QStringLiteral("rect"), shown.rect},
                           {QStringLiteral("background"), shown.background},
                           {QStringLiteral("bleed"), shown.bleed},
                           {QStringLiteral("exportRect"), shown.exportRect()},
                           {QStringLiteral("active"), shown.id == m_active}};
    }
    return out;
}

void CanvasArtboardModel::setActiveBoardId(const QString &id)
{
    if (id == m_active) return;
    if (!id.isEmpty() && (!m_document || !m_document->artboard(id))) return;
    m_active = id;
    emit activeBoardChanged();
    emit boardsChanged();
}

QRectF CanvasArtboardModel::activeRect() const
{
    if (!m_active.isEmpty() && m_active == m_previewId) return m_previewRect;
    const CanvasArtboard *board = m_document ? m_document->artboard(m_active) : nullptr;
    return board ? board->rect : QRectF{};
}

void CanvasArtboardModel::setMinimumSize(qreal size)
{
    if (!std::isfinite(size) || size <= 0 || size == m_minimumSize) return;
    m_minimumSize = size;
    emit minimumSizeChanged();
}

bool CanvasArtboardModel::fail(const QString &message)
{
    m_lastError = message;
    emit errorChanged();
    return false;
}

bool CanvasArtboardModel::activate(const QString &id)
{
    if (!m_document || !m_document->artboard(id)) return fail(tr("The artboard no longer exists"));
    setActiveBoardId(id);
    return true;
}

bool CanvasArtboardModel::previewArrange(const QString &id, const QPointF &start, const QPointF &current)
{
    return preview(id, start, current, false);
}

bool CanvasArtboardModel::previewResize(const QString &id, const QPointF &start, const QPointF &current)
{
    return preview(id, start, current, true);
}

bool CanvasArtboardModel::preview(const QString &id, const QPointF &start, const QPointF &current,
                                  bool resize)
{
    if (!m_document) return fail(tr("No canvas document"));
    if (!finitePoint(start) || !finitePoint(current)) return false;
    if (id != m_previewId || resize != m_previewResize) {
        const CanvasArtboard *board = m_document->artboard(id);
        if (!board) return fail(tr("The artboard no longer exists"));
        m_previewId = id;
        m_previewResize = resize;
        m_previewOriginal = board->rect;
        m_previewStart = start;
        m_snapshot = m_document->artboardRecords();
        m_snapshotGeneration = m_document->resetGeneration();
    }
    m_previewRect = previewed(current);
    emit boardsChanged();
    return true;
}

QRectF CanvasArtboardModel::previewed(const QPointF &current) const
{
    const QPointF delta = current - m_previewStart;
    if (!m_previewResize) return m_previewOriginal.translated(delta);
    return QRectF(m_previewOriginal.topLeft(),
                  QSizeF(std::max(m_minimumSize, m_previewOriginal.width() + delta.x()),
                         std::max(m_minimumSize, m_previewOriginal.height() + delta.y())));
}

bool CanvasArtboardModel::finish(const QString &id, const QPointF &current)
{
    if (!m_document || id.isEmpty() || id != m_previewId) {
        cancel();
        return false;
    }
    const QRectF rect = finitePoint(current) ? previewed(current) : m_previewRect;
    const QString board = m_previewId;
    m_previewId.clear();
    m_committing = true;
    const bool ok = m_document->setArtboardRect(board, rect);
    m_committing = false;
    emit boardsChanged();
    if (!ok) return m_document->lastError().isEmpty() ? false : fail(m_document->lastError());
    setActiveBoardId(board);
    emit committed(board);
    return true;
}

void CanvasArtboardModel::cancel()
{
    if (m_previewId.isEmpty()) return;
    m_previewId.clear();
    emit boardsChanged();
}

QString CanvasArtboardModel::createBoard(const QRectF &rect, const QString &name)
{
    if (!m_document) {
        fail(tr("No canvas document"));
        return {};
    }
    const QString title = name.isEmpty() ? tr("Artboard %1").arg(m_document->artboardCount() + 1)
                                         : name;
    const QString id = m_document->addArtboard(title, rect.normalized());
    if (id.isEmpty()) {
        fail(m_document->lastError());
        return {};
    }
    setActiveBoardId(id);
    emit committed(id);
    return id;
}

bool CanvasArtboardModel::removeActive()
{
    if (!m_document || m_active.isEmpty()) return fail(tr("No active artboard"));
    return m_document->removeArtboard(m_active) || fail(m_document->lastError());
}

QString CanvasArtboardModel::boardAt(const QPointF &documentPoint) const
{
    return m_document ? m_document->artboardAt(documentPoint.x(), documentPoint.y()) : QString{};
}

void CanvasArtboardModel::onDocumentChanged()
{
    // AGENT-GUARD: a board list changed by anyone else (undo, another view, a
    // reload) invalidates the preview's starting geometry; drop the preview
    // rather than commit a rectangle computed from stale data.
    if (!m_previewId.isEmpty() && !m_committing
        && (!m_document || m_document->artboardRecords() != m_snapshot
            || m_document->resetGeneration() != m_snapshotGeneration))
        m_previewId.clear();
    if (!m_active.isEmpty() && (!m_document || !m_document->artboard(m_active))) {
        m_active.clear();
        emit activeBoardChanged();
    }
    emit boardsChanged();
}

} // namespace QindaTK
