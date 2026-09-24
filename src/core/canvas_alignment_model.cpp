// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_alignment_model.h"
#include "canvas_geometry.h"
#include "canvas_transform.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace QindaTK {
namespace {
struct Entry {
    CanvasObject before;
    QRectF bounds;
    QPointF delta;
};

QRectF unionBounds(const QVector<Entry> &entries)
{
    qreal left = std::numeric_limits<qreal>::infinity();
    qreal top = left;
    qreal right = -left;
    qreal bottom = right;
    for (const Entry &entry : entries) {
        left = std::min(left, entry.bounds.left());
        top = std::min(top, entry.bounds.top());
        right = std::max(right, entry.bounds.right());
        bottom = std::max(bottom, entry.bounds.bottom());
    }
    return entries.isEmpty() ? QRectF{} : QRectF(QPointF(left, top), QPointF(right, bottom));
}

qreal low(const QRectF &rect, bool horizontal)
{ return horizontal ? rect.left() : rect.top(); }
qreal high(const QRectF &rect, bool horizontal)
{ return horizontal ? rect.right() : rect.bottom(); }
qreal center(const QRectF &rect, bool horizontal)
{ return horizontal ? rect.center().x() : rect.center().y(); }
qreal span(const QRectF &rect, bool horizontal)
{ return horizontal ? rect.width() : rect.height(); }
} // namespace

void CanvasAlignmentModel::setDocument(CanvasDocumentAdapter *document)
{
    if (m_document == document) return;
    if (m_document) disconnect(m_document, nullptr, this, nullptr);
    m_document = document;
    if (document) {
        connect(document, &QObject::destroyed, this, [this] {
            m_document = nullptr;
            emit documentChanged();
            emit referencesChanged();
        });
        // Boards come and go with edits, undo and resets.
        connect(document, &CanvasDocumentAdapter::edited, this, &CanvasAlignmentModel::referencesChanged);
        connect(document, &CanvasDocumentAdapter::reset, this, &CanvasAlignmentModel::referencesChanged);
    }
    emit documentChanged();
    emit referencesChanged();
}

void CanvasAlignmentModel::setSelectionModel(CanvasSelectionModel *model)
{
    if (m_selectionModel == model) return;
    if (m_selectionModel) disconnect(m_selectionModel, nullptr, this, nullptr);
    m_selectionModel = model;
    if (model) {
        connect(model, &CanvasSelectionModel::selectionChanged, this, &CanvasAlignmentModel::selectionChanged);
        connect(model, &QObject::destroyed, this, &CanvasAlignmentModel::selectionChanged);
    }
    emit selectionModelChanged();
    emit selectionChanged();
}

QStringList CanvasAlignmentModel::selectedIds() const
{
    QStringList ids;
    if (m_selectionModel)
        for (const CanvasObjectId id : m_selectionModel->selection()) ids << QString::number(id);
    return ids;
}

void CanvasAlignmentModel::setArtboardId(const QString &id)
{
    if (m_artboardId == id) return;
    m_artboardId = id;
    emit referencesChanged();
}

const CanvasArtboard *CanvasAlignmentModel::referenceBoard() const
{
    const auto *canvas = qobject_cast<const CanvasDocument *>(m_document.data());
    return canvas ? canvas->artboard(m_artboardId) : nullptr;
}

QVariantList CanvasAlignmentModel::referenceOptions() const
{
    const auto option = [](const QString &value, const QString &text, bool available) {
        return QVariantMap{{QStringLiteral("value"), value}, {QStringLiteral("text"), text},
                           {QStringLiteral("available"), available}};
    };
    QVariantList options{option(QStringLiteral("selection"), tr("Selection"), true),
                         option(QStringLiteral("keyObject"), tr("Key object"), true),
                         option(QStringLiteral("page"), tr("Page"), true)};
    const auto *canvas = qobject_cast<const CanvasDocument *>(m_document.data());
    if (canvas && canvas->artboardCount() > 0)
        options << option(QStringLiteral("artboard"), tr("Artboard"), referenceBoard() != nullptr);
    return options;
}

bool CanvasAlignmentModel::fail(const QString &message)
{
    m_lastError = message;
    emit errorChanged();
    return false;
}

bool CanvasAlignmentModel::apply(const QString &operation, const QVariantList &selectedIds,
                                  const QString &referenceMode, const QString &keyObjectId,
                                  qreal spacing)
{
    if (!m_document) return fail(tr("No canvas document"));
    if (!std::isfinite(spacing) || spacing < 0) return fail(tr("Invalid document-unit gap"));
    const bool distribution = operation.startsWith(QLatin1String("distribute."));
    const bool horizontal = operation.endsWith(QLatin1String("horizontal"));
    const bool vertical = operation.endsWith(QLatin1String("vertical"));
    const bool alignment = operation.startsWith(QLatin1String("align."));
    if ((!distribution && !alignment) || (distribution && !horizontal && !vertical)
        || (alignment && operation != QLatin1String("align.left")
            && operation != QLatin1String("align.horizontalCenter")
            && operation != QLatin1String("align.right")
            && operation != QLatin1String("align.top")
            && operation != QLatin1String("align.verticalCenter")
            && operation != QLatin1String("align.bottom")))
        return fail(tr("Unknown alignment operation"));
    if (referenceMode != QLatin1String("selection")
        && referenceMode != QLatin1String("page")
        && referenceMode != QLatin1String("keyObject")
        && referenceMode != QLatin1String("artboard"))
        return fail(tr("Unknown alignment reference"));

    QVector<Entry> entries;
    entries.reserve(selectedIds.size());
    for (const QVariant &value : selectedIds) {
        bool ok = false;
        const CanvasObjectId id = value.toString().toULongLong(&ok);
        if (!ok || id == 0) return fail(tr("Invalid object ID"));
        if (std::any_of(entries.cbegin(), entries.cend(), [id](const Entry &entry) {
                return entry.before.id == id;
            })) continue;
        const CanvasObject *object = m_document->object(id);
        if (!object) return fail(tr("Selected object no longer exists"));
        if (!m_document->effectiveVisible(*object) || m_document->effectiveLocked(*object))
            return fail(tr("A selected object is hidden or locked"));
        if (!CanvasGeometry::isRenderable(*object))
            return fail(tr("A selected object has invalid geometry"));
        entries.append({*object, CanvasGeometry::documentBounds(*object), {}});
    }
    if (entries.isEmpty()) return fail(tr("Select an object first"));

    QRectF reference;
    CanvasObjectId keyId = 0;
    if (referenceMode == QLatin1String("selection")) reference = unionBounds(entries);
    else if (referenceMode == QLatin1String("page")) reference = m_document->pageRect();
    else if (referenceMode == QLatin1String("artboard")) {
        const CanvasArtboard *board = referenceBoard();
        if (!board) return fail(tr("Choose an artboard to align to"));
        reference = board->rect;
    } else {
        bool ok = false;
        keyId = keyObjectId.toULongLong(&ok);
        const CanvasObject *key = ok ? m_document->object(keyId) : nullptr;
        if (!key || !m_document->effectiveVisible(*key)
            || !CanvasGeometry::isRenderable(*key)) return fail(tr("Key object is unavailable"));
        reference = CanvasGeometry::documentBounds(*key);
    }
    if (!std::isfinite(reference.left()) || !std::isfinite(reference.top())
        || !std::isfinite(reference.right()) || !std::isfinite(reference.bottom()))
        return fail(tr("Reference bounds are invalid"));

    QVector<int> movable;
    for (int i = 0; i < entries.size(); ++i)
        if (entries.at(i).before.id != keyId) movable.append(i);
    if (movable.isEmpty()) return fail(tr("No movable object is selected"));
    if (referenceMode == QLatin1String("selection") && entries.size() < 2)
        return fail(tr("Select at least two objects for this reference"));

    if (alignment) {
        const bool xAxis = operation == QLatin1String("align.left")
                           || operation == QLatin1String("align.horizontalCenter")
                           || operation == QLatin1String("align.right");
        const qreal target = operation.endsWith(QLatin1String("left"))
                             || operation.endsWith(QLatin1String("top")) ? low(reference, xAxis)
                           : operation.endsWith(QLatin1String("right"))
                             || operation.endsWith(QLatin1String("bottom")) ? high(reference, xAxis)
                           : center(reference, xAxis);
        for (int index : movable) {
            const QRectF bounds = entries.at(index).bounds;
            const qreal current = operation.endsWith(QLatin1String("left"))
                                  || operation.endsWith(QLatin1String("top")) ? low(bounds, xAxis)
                                : operation.endsWith(QLatin1String("right"))
                                  || operation.endsWith(QLatin1String("bottom")) ? high(bounds, xAxis)
                                : center(bounds, xAxis);
            if (xAxis) entries[index].delta.setX(target - current);
            else entries[index].delta.setY(target - current);
        }
    } else {
        const bool xAxis = horizontal;
        std::sort(movable.begin(), movable.end(), [&](int a, int b) {
            const qreal ac = center(entries.at(a).bounds, xAxis);
            const qreal bc = center(entries.at(b).bounds, xAxis);
            return ac == bc ? entries.at(a).before.id < entries.at(b).before.id : ac < bc;
        });
        if (referenceMode == QLatin1String("selection") && movable.size() < 3)
            return fail(tr("Select at least three objects to distribute"));
        if (spacing == 0) {
            const qreal first = referenceMode == QLatin1String("selection")
                                    ? center(entries.at(movable.first()).bounds, xAxis)
                                    : low(reference, xAxis);
            const qreal last = referenceMode == QLatin1String("selection")
                                   ? center(entries.at(movable.last()).bounds, xAxis)
                                   : high(reference, xAxis);
            const int divisor = referenceMode == QLatin1String("selection")
                                    ? movable.size() - 1 : movable.size() + 1;
            for (int rank = 0; rank < movable.size(); ++rank) {
                const int index = movable.at(rank);
                const qreal target = referenceMode == QLatin1String("selection")
                                         ? first + (last - first) * rank / divisor
                                         : first + (last - first) * (rank + 1) / divisor;
                const qreal delta = target - center(entries.at(index).bounds, xAxis);
                if (xAxis) entries[index].delta.setX(delta);
                else entries[index].delta.setY(delta);
            }
        } else {
            qreal edge = referenceMode == QLatin1String("selection")
                             ? high(entries.at(movable.first()).bounds, xAxis)
                             : low(reference, xAxis) - spacing;
            const int start = referenceMode == QLatin1String("selection") ? 1 : 0;
            for (int rank = start; rank < movable.size(); ++rank) {
                const int index = movable.at(rank);
                const qreal targetLow = edge + spacing;
                const qreal delta = targetLow - low(entries.at(index).bounds, xAxis);
                if (xAxis) entries[index].delta.setX(delta);
                else entries[index].delta.setY(delta);
                edge = targetLow + span(entries.at(index).bounds, xAxis);
            }
        }
    }

    CanvasEdit edit;
    edit.label = distribution ? tr("Distribute objects") : tr("Align objects");
    for (const Entry &entry : std::as_const(entries)) {
        if (entry.delta.isNull()) continue;
        CanvasObject after = entry.before;
        after.transform = entry.before.transform * QTransform::fromTranslate(
            entry.delta.x(), entry.delta.y());
        if (!CanvasTransform::isFinite(after.transform)
            || !CanvasGeometry::isRenderable(after)) return fail(tr("Alignment exceeds geometry limits"));
        edit.changes.append({entry.before.id, entry.before, after});
    }
    if (!edit.isEmpty() && !m_document->commit(edit))
        return fail(tr("Document changed before alignment could commit"));
    if (!m_lastError.isEmpty()) {
        m_lastError.clear();
        emit errorChanged();
    }
    emit applied(operation, edit.changes.size());
    return true;
}
} // namespace QindaTK
