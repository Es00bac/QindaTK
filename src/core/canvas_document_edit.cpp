// SPDX-License-Identifier: LGPL-3.0-or-later
// CanvasDocument commit path: whole-edit validation (prepare), application
// (publish), step composition and the recorded page/layer/image mutations.
#include "canvas_document.h"
#include "canvas_document_format.h"
#include "canvas_document_p.h"
#include "undo_service.h"

#include <QSet>
#include <algorithm>

namespace QindaTK {
namespace {
OperationError failure(ErrorCode code, const QString &message, const QString &field = {})
{
    return {code, message, field, QStringLiteral("canvas")};
}

template <typename Change>
bool duplicateTargets(const QVector<Change> &changes)
{
    QSet<quint64> seen;
    seen.reserve(changes.size());
    for (const Change &change : changes) {
        if (seen.contains(change.id)) return true;
        seen.insert(change.id);
    }
    return false;
}

template <typename Change, typename Key>
void mergeInto(QVector<Change> &result, const QVector<Change> &first, const QVector<Change> &second)
{
    QHash<Key, qsizetype> position;
    for (const Change &change : first) {
        position.insert(change.id, result.size());
        result.append(change);
    }
    for (const Change &change : second) {
        const auto it = position.constFind(change.id);
        if (it != position.cend()) {
            result[it.value()].after = change.after;
        } else {
            position.insert(change.id, result.size());
            result.append(change);
        }
    }
    result.removeIf([](const Change &change) { return change.before == change.after; });
}
} // namespace

CanvasCommand::CanvasCommand(CanvasEdit edit) : m_edit(std::move(edit)), m_bytes(estimate(m_edit)) {}

quint64 CanvasCommand::estimate(const CanvasEdit &edit)
{
    const auto objectBytes = [](const std::optional<CanvasObject> &o) -> quint64 {
        return o ? sizeof(CanvasObject) + quint64(o->points.size()) * sizeof(QPointF)
                       + quint64(o->pathPointCount()) * sizeof(CanvasPathNode)
                       + quint64(o->extraContours.size()) * sizeof(CanvasPathContour)
                       + quint64(o->name.size()) * 2
                 : 0;
    };
    const auto layerBytes = [](const std::optional<CanvasLayer> &l) -> quint64 {
        return l ? sizeof(CanvasLayer) + quint64(l->name.size()) * 2 : 0;
    };
    quint64 total = 64 + quint64(edit.label.size()) * 2;
    for (const auto &c : edit.changes) total += 32 + objectBytes(c.before) + objectBytes(c.after);
    for (const auto &l : edit.layers) total += 32 + layerBytes(l.before) + layerBytes(l.after);
    for (const auto &i : edit.images) {
        // Once applied, only this step still references a removed or
        // replaced image; an added image is shared with the live document.
        total += 32;
        if (i.before && i.before != i.after)
            total += quint64(CanvasDocumentFormat::imageBytes(*i.before));
    }
    if (edit.page) total += 64;
    return total;
}

CanvasEdit CanvasCommand::composed(const CanvasEdit &first, const CanvasEdit &second)
{
    CanvasEdit result;
    result.label = second.label.isEmpty() ? first.label : second.label;
    mergeInto<CanvasEdit::Change, CanvasObjectId>(result.changes, first.changes, second.changes);
    mergeInto<CanvasEdit::LayerChange, CanvasLayerId>(result.layers, first.layers, second.layers);
    mergeInto<CanvasEdit::ImageChange, CanvasImageId>(result.images, first.images, second.images);
    if (first.page || second.page) {
        CanvasEdit::PageChange page = first.page ? *first.page : *second.page;
        if (first.page && second.page) {
            page.afterRect = second.page->afterRect;
            page.afterColor = second.page->afterColor;
        }
        if (page.beforeRect != page.afterRect || page.beforeColor != page.afterColor) result.page = page;
    }
    return result;
}

OperationError CanvasDocument::prepareEdit(const CanvasEdit &edit, bool forward) const
{
    if (edit.isEmpty()) return failure(ErrorCode::InvalidArgument, tr("Empty canvas edit"));
    // AGENT-GUARD: a duplicate target would be checked once against the
    // current state but applied twice; reject it before anything else.
    if (duplicateTargets(edit.changes) || duplicateTargets(edit.layers) || duplicateTargets(edit.images))
        return failure(ErrorCode::InvalidArgument, tr("An edit names the same target twice"), QStringLiteral("targets"));

    // The current state must be the expected side of every delta.
    for (const auto &c : edit.changes) {
        const auto &expected = forward ? c.before : c.after;
        const CanvasObject *current = object(c.id);
        if (bool(current) != expected.has_value() || (current && !(*current == *expected)))
            return failure(ErrorCode::StaleRevision, tr("An object changed after this edit was built"), QStringLiteral("objects"));
    }
    for (const auto &l : edit.layers) {
        const auto &expected = forward ? l.before : l.after;
        const CanvasLayer *current = layer(l.id);
        if (bool(current) != expected.has_value() || (current && !(*current == *expected)))
            return failure(ErrorCode::StaleRevision, tr("A layer changed after this edit was built"), QStringLiteral("layers"));
    }
    for (const auto &i : edit.images)
        if (m_images.value(i.id) != (forward ? i.before : i.after))
            return failure(ErrorCode::StaleRevision, tr("An image changed after this edit was built"), QStringLiteral("images"));
    if (edit.page) {
        const auto &p = *edit.page;
        if (m_pageRect != (forward ? p.beforeRect : p.afterRect)
            || m_pageColor != (forward ? p.beforeColor : p.afterColor))
            return failure(ErrorCode::StaleRevision, tr("The page changed after this edit was built"), QStringLiteral("page"));
        if (const auto e = CanvasDocumentFormat::validatePage(forward ? p.afterRect : p.beforeRect,
                                                              forward ? p.afterColor : p.beforeColor))
            return e;
    }

    // Every target must be valid in the resulting document.
    QHash<CanvasLayerId, bool> layerAfter;
    qsizetype layers = m_layers.size();
    qsizetype encoded = m_encodedBytes;
    for (const auto &l : edit.layers) {
        const auto &target = forward ? l.after : l.before;
        if (const CanvasLayer *current = layer(l.id)) encoded -= CanvasDocumentFormat::layerRecordBytes(*current);
        if (target) {
            if (const auto e = CanvasDocumentFormat::validateLayer(*target, l.id, m_limits)) return e;
            encoded += CanvasDocumentFormat::layerRecordBytes(*target);
        }
        layerAfter.insert(l.id, target.has_value());
        layers += int(target.has_value()) - int(m_layers.contains(l.id));
    }
    if (layers < 1) return failure(ErrorCode::Validation, tr("A canvas keeps at least one layer"), QStringLiteral("layers"));
    if (layers > m_limits.maximumLayers) return failure(ErrorCode::Unsupported, tr("Too many layers"), QStringLiteral("layers"));
    QHash<CanvasImageId, bool> imageAfter;
    qsizetype images = m_images.size();
    qsizetype pixels = m_imageBytes;
    for (const auto &i : edit.images) {
        const CanvasTiledImagePtr &target = forward ? i.after : i.before;
        if (target) {
            if (const auto e = CanvasDocumentFormat::validateImage(*target, i.id, m_limits)) return e;
            pixels += CanvasDocumentFormat::imageBytes(*target);
            encoded += CanvasDocumentFormat::imageRecordBytes(*target);
        }
        if (const CanvasTiledImagePtr current = m_images.value(i.id)) {
            pixels -= CanvasDocumentFormat::imageBytes(*current);
            encoded -= CanvasDocumentFormat::imageRecordBytes(*current);
        }
        imageAfter.insert(i.id, bool(target));
        images += int(bool(target)) - int(m_images.contains(i.id));
    }
    if (images > m_limits.maximumImages || pixels > m_limits.maximumImageBytes)
        return failure(ErrorCode::Unsupported, tr("Images exceed the canvas limit"), QStringLiteral("images"));
    const auto layerExists = [&](CanvasLayerId id) {
        const auto it = layerAfter.constFind(id);
        return it != layerAfter.cend() ? it.value() : m_layers.contains(id);
    };
    const auto imageExists = [&](CanvasImageId id) {
        const auto it = imageAfter.constFind(id);
        return it != imageAfter.cend() ? it.value() : m_images.contains(id);
    };
    QHash<CanvasLayerId, qsizetype> layerUse;
    QHash<CanvasImageId, qsizetype> imageUse;
    qsizetype objects = m_objects.size();
    qsizetype points = m_totalPoints;
    for (const auto &c : edit.changes) {
        const auto &current = forward ? c.before : c.after;
        const auto &target = forward ? c.after : c.before;
        if (current) {
            --layerUse[current->layer];
            if (current->shape == CanvasShape::Image) --imageUse[current->image];
            points -= current->points.size() + current->pathPointCount();
            encoded -= CanvasDocumentFormat::objectRecordBytes(*current);
            --objects;
        }
        if (!target) continue;
        if (const auto e = CanvasDocumentFormat::validateObject(*target, c.id, m_limits)) return e;
        if (!layerExists(target->layer))
            return failure(ErrorCode::Validation, tr("An object references a missing layer"), QStringLiteral("objects.layer"));
        if (target->shape == CanvasShape::Image && !imageExists(target->image))
            return failure(ErrorCode::Validation, tr("An image object references a missing image"), QStringLiteral("objects.image"));
        ++layerUse[target->layer];
        if (target->shape == CanvasShape::Image) ++imageUse[target->image];
        points += target->points.size() + target->pathPointCount();
        encoded += CanvasDocumentFormat::objectRecordBytes(*target);
        ++objects;
    }
    if (objects > m_limits.maximumObjects || points > m_limits.maximumTotalPoints)
        return failure(ErrorCode::Unsupported, tr("The canvas object or point limit is exceeded"), QStringLiteral("objects"));
    // AGENT-GUARD: every committed canvas must still save and reopen, with
    // room for the largest allowed extensions JSON. Edits that shrink the
    // file stay possible even when a loaded file is already near the limit.
    if (encoded > m_encodedBytes
        && CanvasDocumentFormat::FixedBytes + encoded + m_outputProfileIcc.size() + m_limits.maximumExtensionBytes
               > m_limits.maximumEncodedBytes)
        return failure(ErrorCode::Unsupported, tr("The canvas would exceed its saved-file size limit"),
                       QStringLiteral("encodedBytes"));
    for (auto it = layerAfter.cbegin(); it != layerAfter.cend(); ++it)
        if (!it.value() && m_layerUse.value(it.key()) + layerUse.value(it.key()) > 0)
            return failure(ErrorCode::Validation, tr("A removed layer still contains objects"), QStringLiteral("layers"));
    for (auto it = imageAfter.cbegin(); it != imageAfter.cend(); ++it)
        if (!it.value() && m_imageUse.value(it.key()) + imageUse.value(it.key()) > 0)
            return failure(ErrorCode::Validation, tr("A removed image is still referenced"), QStringLiteral("images"));
    return {};
}

void CanvasDocument::applyEdit(const CanvasEdit &edit, bool forward, QVector<QRectF> *damage,
                               bool *full)
{
    for (const auto &i : edit.images) {
        const CanvasTiledImagePtr &target = forward ? i.after : i.before;
        const bool replaced = target && m_images.contains(i.id);
        placeImage(i.id, target);
        if (replaced) imageDamage(i.id, damage);
    }
    for (const auto &l : edit.layers) {
        const auto &target = forward ? l.after : l.before;
        const CanvasLayer *current = layer(l.id);
        if (!target || !current || current->order != target->order) *full = true;
        else if (current->visible != target->visible) layerDamage(l.id, damage);
        if (target) placeLayer(*target);
        else unplaceLayer(l.id);
    }
    if (edit.page) {
        const QRectF rect = forward ? edit.page->afterRect : edit.page->beforeRect;
        damage->append(m_pageRect);
        damage->append(rect);
        m_pageRect = rect;
        m_pageColor = forward ? edit.page->afterColor : edit.page->beforeColor;
    }
    const auto step = [&](const CanvasEdit::Change &c) {
        const auto &target = forward ? c.after : c.before;
        if (target) place(*target, damage);
        else unplace(c.id, damage);
    };
    if (forward) std::for_each(edit.changes.cbegin(), edit.changes.cend(), step);
    else std::for_each(edit.changes.crbegin(), edit.changes.crend(), step);
    ensureActiveLayer();
}

OperationError CanvasDocument::prepareCommand(const DocumentCommand &command, bool forward) const
{
    const auto *step = dynamic_cast<const CanvasCommand *>(&command);
    if (!step) return failure(ErrorCode::InvalidArgument, tr("The step belongs to another document kind"));
    return prepareEdit(step->edit(), forward);
}

void CanvasDocument::publishCommand(const DocumentCommand &command, bool forward)
{
    const auto *step = dynamic_cast<const CanvasCommand *>(&command);
    Q_ASSERT(step);
    if (!step) return;
    QVector<QRectF> damage;
    bool full = false;
    applyEdit(step->edit(), forward, &damage, &full);
    finishChange(damage, full);
}

DocumentCommandPtr CanvasDocument::composeCommands(const DocumentCommand &first,
                                                   const DocumentCommand &second) const
{
    const auto *a = dynamic_cast<const CanvasCommand *>(&first);
    const auto *b = dynamic_cast<const CanvasCommand *>(&second);
    if (!a || !b) return {};
    return std::make_shared<const CanvasCommand>(CanvasCommand::composed(a->edit(), b->edit()));
}

OperationError CanvasDocument::commitEdit(const CanvasEdit &edit)
{
    if (edit.baseResetGeneration && *edit.baseResetGeneration != m_resetGeneration)
        return failure(ErrorCode::StaleGeneration, tr("The canvas was reset after this edit was built"),
                       QStringLiteral("resetGeneration"));
    CanvasEdit recorded = edit;
    recorded.baseResetGeneration.reset();
    if (recorded.label.isEmpty()) recorded.label = tr("Edit");
    const QString label = recorded.label;
    UndoService *service = history();
    if (!service) return failure(ErrorCode::Unavailable, tr("The canvas has no history service"));
    return service->commit(this, label, {}, std::make_shared<const CanvasCommand>(std::move(recorded)));
}

void CanvasDocument::applyDirect(const CanvasEdit &edit)
{
    QVector<QRectF> damage;
    bool full = false;
    applyEdit(edit, true, &damage, &full);
    finishChange(damage, full);
}

void CanvasDocument::setPageRect(const QRectF &rect)
{
    if (rect == m_pageRect || CanvasDocumentFormat::validatePage(rect, m_pageColor)) return;
    CanvasEdit edit;
    edit.label = tr("Page size");
    edit.page = CanvasEdit::PageChange{m_pageRect, rect, m_pageColor, m_pageColor};
    if (m_initializing) applyDirect(edit); // a declared initial value, not an edit
    else commitEdit(edit);
}

void CanvasDocument::setPageColor(const QColor &color)
{
    if (color == m_pageColor || CanvasDocumentFormat::validatePage(m_pageRect, color)) return;
    CanvasEdit edit;
    edit.label = tr("Page colour");
    edit.page = CanvasEdit::PageChange{m_pageRect, m_pageRect, m_pageColor, color};
    if (m_initializing) applyDirect(edit);
    else commitEdit(edit);
}

CanvasObjectId CanvasDocument::insert(CanvasObject object, bool record)
{
    if (object.id == 0) object.id = allocateId();
    if (!m_layers.contains(object.layer)) object.layer = m_activeLayer;
    if (object.z == 0) object.z = reserveZ();
    CanvasEdit edit;
    edit.label = tr("Add object");
    edit.changes.append({object.id, std::nullopt, object});
    if (record) return commitEdit(edit) ? 0 : object.id;
    if (prepareEdit(edit, true)) return 0;
    // Unrecorded content: the saved state is no longer reachable by undo.
    // Invalidate first so observers of the change already see it dirty.
    invalidateCleanState();
    applyDirect(edit);
    return object.id;
}

CanvasLayerId CanvasDocument::addLayer(const QString &name)
{
    CanvasLayer l;
    l.id = m_nextLayer;
    l.name = name;
    l.order = nextLayerOrder();
    CanvasEdit edit;
    edit.label = tr("Add layer");
    edit.layers.append({l.id, std::nullopt, l});
    return commitEdit(edit) ? 0 : l.id;
}

CanvasImageId CanvasDocument::addImage(CanvasTiledImage image)
{
    image.id = m_nextImage;
    for (QImage &tile : image.tiles)
        if (tile.format() != QImage::Format_ARGB32_Premultiplied)
            tile = tile.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const CanvasImageId id = image.id;
    CanvasEdit edit;
    edit.label = tr("Add image");
    edit.images.append({id, {}, std::make_shared<const CanvasTiledImage>(std::move(image))});
    return commitEdit(edit) ? 0 : id;
}

bool CanvasDocument::removeLayer(CanvasLayerId id)
{
    const CanvasLayer *current = layer(id);
    if (!current) return false;
    CanvasEdit edit;
    edit.label = tr("Remove layer");
    edit.layers.append({id, *current, std::nullopt});
    return !commitEdit(edit);
}

bool CanvasDocument::removeImage(CanvasImageId id)
{
    const CanvasTiledImagePtr current = m_images.value(id);
    if (!current) return false;
    CanvasEdit edit;
    edit.label = tr("Remove image");
    edit.images.append({id, current, {}});
    return !commitEdit(edit);
}

bool CanvasDocument::setLayerVisible(CanvasLayerId id, bool visible)
{
    const CanvasLayer *current = layer(id);
    if (!current || current->visible == visible) return false;
    CanvasLayer next = *current;
    next.visible = visible;
    CanvasEdit edit;
    edit.label = visible ? tr("Show layer") : tr("Hide layer");
    edit.layers.append({id, *current, next});
    return !commitEdit(edit);
}

bool CanvasDocument::setLayerLocked(CanvasLayerId id, bool locked)
{
    const CanvasLayer *current = layer(id);
    if (!current || current->locked == locked) return false;
    CanvasLayer next = *current;
    next.locked = locked;
    CanvasEdit edit;
    edit.label = locked ? tr("Lock layer") : tr("Unlock layer");
    edit.layers.append({id, *current, next});
    return !commitEdit(edit);
}

} // namespace QindaTK
