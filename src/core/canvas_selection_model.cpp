// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_selection_model.h"

#include <utility>

namespace QindaTK {
void CanvasSelectionModel::setDocument(CanvasDocumentAdapter *document)
{
    if (m_document == document) return;
    if (m_document) disconnect(m_document, nullptr, this, nullptr);
    m_document = document;
    if (document) {
        connect(document, &CanvasDocumentAdapter::edited, this, [this] { prune(); });
        connect(document, &CanvasDocumentAdapter::reset, this, [this] { prune(); });
        connect(document, &QObject::destroyed, this, [this] {
            m_document = nullptr;
            if (!m_selection.isEmpty()) {
                m_selection.clear();
                emit selectionChanged();
            }
            emit documentChanged();
        });
    }
    if (!m_selection.isEmpty()) {
        m_selection.clear();
        emit selectionChanged();
    }
    emit documentChanged();
}

void CanvasSelectionModel::setSelection(const QVector<CanvasObjectId> &ids)
{
    QVector<CanvasObjectId> next;
    if (m_document) {
        next.reserve(ids.size());
        for (CanvasObjectId id : ids) {
            const auto *object = m_document->object(id);
            if (object && m_document->effectiveVisible(*object)
                && !m_document->effectiveLocked(*object) && !next.contains(id))
                next.append(id);
        }
    }
    if (next == m_selection) return;
    m_selection = std::move(next);
    emit selectionChanged();
}

QVariantList CanvasSelectionModel::selectedIds() const
{
    QVariantList result;
    for (CanvasObjectId id : m_selection) result << qulonglong(id);
    return result;
}

void CanvasSelectionModel::setSelectedIds(const QVariantList &ids)
{
    QVector<CanvasObjectId> proposed;
    proposed.reserve(ids.size());
    for (const QVariant &value : ids) proposed.append(value.toULongLong());
    setSelection(proposed);
}

void CanvasSelectionModel::prune()
{
    setSelection(m_selection);
}
} // namespace QindaTK
