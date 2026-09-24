// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_document.h"
#include "canvas_document_format.h"
#include "canvas_transform.h"
#include "color_profile.h"
#include "undo_service.h"

#include <QColorSpace>
#include <QUuid>
#include <algorithm>

namespace QindaTK {

namespace {
// AGENT-GUARD: bounded so a malformed adapter hierarchy (a parent cycle)
// cannot hang painting or picking; CanvasDocument validates depth <= 64.
constexpr int maximumLayerWalk = 256;
} // namespace

bool CanvasDocumentAdapter::effectiveVisible(const CanvasObject &object) const
{
    return object.visible && layerEffectivelyVisible(object.layer);
}

bool CanvasDocumentAdapter::effectiveLocked(const CanvasObject &object) const
{
    return object.locked || layerEffectivelyLocked(object.layer);
}

bool CanvasDocumentAdapter::layerEffectivelyVisible(CanvasLayerId id) const
{
    const CanvasLayer *l = layer(id);
    for (int step = 0; l && step < maximumLayerWalk; ++step) {
        if (!l->visible) return false;
        l = l->parent ? layer(l->parent) : nullptr;
    }
    return true;
}

bool CanvasDocumentAdapter::layerEffectivelyLocked(CanvasLayerId id) const
{
    const CanvasLayer *l = layer(id);
    for (int step = 0; l && step < maximumLayerWalk; ++step) {
        if (l->locked) return true;
        l = l->parent ? layer(l->parent) : nullptr;
    }
    return false;
}

qint64 CanvasDocumentAdapter::paintKey(const CanvasObject &object) const
{
    const CanvasLayer *l = layer(object.layer);
    return (qint64(l ? l->order : 0) << 40) + (object.z & ((qint64(1) << 40) - 1));
}

CanvasDocument::CanvasDocument(QObject *parent)
    : CanvasDocumentAdapter(parent), m_id(DocumentId::create()),
      m_documentId(m_id.toString())
{
    CanvasLayer first;
    first.id = m_nextLayer;
    first.name = tr("Layer 1");
    placeLayer(first);
    refreshLayerTree();
    m_activeLayer = first.id;
    attachLocalHistory();
    // A new empty layer on top changes no pixels but invalidates order keys
    // cached by consumers.
    finishChange({}, true);
}

CanvasDocument::CanvasDocument(RestoreTag, QObject *parent) : CanvasDocumentAdapter(parent) {}

CanvasDocument::~CanvasDocument()
{
    disconnect(m_historyConnection);
}

std::unique_ptr<CanvasDocument> CanvasDocument::fromState(const CanvasDocumentState &state,
                                                          const CanvasDocumentLimits &limits)
{
    std::unique_ptr<CanvasDocument> document(new CanvasDocument(RestoreTag{}, nullptr));
    document->m_limits = limits;
    document->m_id = state.documentId;
    document->m_documentId = state.documentId.toString();
    document->m_pageRect = state.pageRect;
    document->m_pageColor = state.pageColor;
    document->m_outputProfileIcc = state.outputProfileIcc;
    document->m_guides = state.guides;
    document->m_nextGuide = state.nextGuide;
    document->m_artboards = state.artboards;
    document->m_nextArtboard = state.nextArtboard;
    for (const CanvasLayer &layer : state.layers) document->placeLayer(layer);
    document->refreshLayerTree();
    for (auto it = state.images.cbegin(); it != state.images.cend(); ++it)
        document->placeImage(it.key(), it.value());
    QVector<QRectF> ignored;
    for (const CanvasObject &object : state.objects) document->place(object, &ignored);
    document->m_nextId = std::max(document->m_nextId, state.nextObject);
    document->m_nextLayer = std::max(document->m_nextLayer, state.nextLayer);
    document->m_nextImage = std::max(document->m_nextImage, state.nextImage);
    document->m_nextZ = std::max(document->m_nextZ, state.nextZ);
    document->m_revision = state.revision;
    document->m_resetGeneration = state.resetGeneration;
    document->ensureActiveLayer();
    document->attachLocalHistory();
    return document;
}

void CanvasDocument::attachLocalHistory()
{
    // AGENT-NOTE: the standalone history keeps the pre-session behaviour:
    // DefaultHistoryLimit steps and a byte budget large enough never to
    // refuse an ordinary edit. A DocumentSession moves this single history
    // into the shared UndoService (adoptModel) and applies its budget.
    m_localHistory = new UndoService(this);
    m_localHistory->setMemoryLimit(quint64(1) << 30);
    m_localHistory->addModel(this);
    m_localHistory->setStepLimit(this, DefaultHistoryLimit);
}

UndoService *CanvasDocument::history() const
{
    if (UndoService *service = historyService()) return service;
    // Released by a session without being destroyed: resume standalone.
    auto *self = const_cast<CanvasDocument *>(this);
    if (m_localHistory && m_localHistory->addModel(self))
        m_localHistory->setStepLimit(self, DefaultHistoryLimit);
    return historyService();
}

void CanvasDocument::historyAttached(UndoService *service)
{
    disconnect(m_historyConnection);
    if (!service) return; // transient during a transfer; the new owner follows
    const QString key = m_documentId;
    m_historyConnection = connect(service, &UndoService::historyChanged, this,
                                  [this, key](const QString &id) {
        if (id == key) emit historyChanged();
    });
    emit historyChanged();
}

UndoService *CanvasDocument::undoService() const { return history(); }

bool CanvasDocument::canUndo() const
{
    const UndoService *service = history();
    return service && service->canUndo(this);
}

bool CanvasDocument::canRedo() const
{
    const UndoService *service = history();
    return service && service->canRedo(this);
}

QString CanvasDocument::undoText() const
{
    const UndoService *service = history();
    return service ? service->undoText(this) : QString{};
}

QString CanvasDocument::redoText() const
{
    const UndoService *service = history();
    return service ? service->redoText(this) : QString{};
}

int CanvasDocument::historyLimit() const
{
    const UndoService *service = history();
    return service ? service->stepLimit(this) : 0;
}

void CanvasDocument::setHistoryLimit(int limit)
{
    if (UndoService *service = history()) service->setStepLimit(this, std::max(1, limit));
}

OperationError CanvasDocument::reject(const OperationError &error)
{
    m_lastError = error.message;
    emit historyChanged();
    return error;
}

bool CanvasDocument::undo()
{
    if (!canUndo()) return false;
    if (const auto problem = history()->undo(this)) return !reject(problem);
    if (!m_lastError.isEmpty()) {
        m_lastError.clear();
        emit historyChanged();
    }
    return true;
}

bool CanvasDocument::redo()
{
    if (!canRedo()) return false;
    if (const auto problem = history()->redo(this)) return !reject(problem);
    if (!m_lastError.isEmpty()) {
        m_lastError.clear();
        emit historyChanged();
    }
    return true;
}

SnapshotHeader CanvasDocument::modelHeader() const
{
    return {QString::fromLatin1(CanvasDocumentFormat::SchemaId), m_id, {m_revision},
            m_resetGeneration};
}

QMetaObject::Connection CanvasDocument::connectModelChanged(QObject *receiver,
                                                            std::function<void()> slot)
{
    return connect(this, &CanvasDocument::changed, receiver, std::move(slot));
}

CanvasDocumentState CanvasDocument::state() const
{
    CanvasDocumentState captured;
    captured.documentId = m_id;
    captured.revision = m_revision;
    captured.resetGeneration = m_resetGeneration;
    captured.pageRect = m_pageRect;
    captured.pageColor = m_pageColor;
    captured.outputProfileIcc = m_outputProfileIcc;
    captured.nextObject = m_nextId;
    captured.nextLayer = m_nextLayer;
    captured.nextImage = m_nextImage;
    captured.nextZ = m_nextZ;
    captured.objects = m_objects; // implicitly shared: O(1) until the next edit
    captured.layers = m_layers;
    captured.images = m_images;
    captured.guides = m_guides;
    captured.nextGuide = m_nextGuide;
    captured.artboards = m_artboards;
    captured.nextArtboard = m_nextArtboard;
    return captured;
}

QRectF CanvasDocument::contentBounds() const
{
    QRectF bounds = m_pageRect;
    for (const QRectF &b : m_bounds) {
        bounds |= b;
    }
    for (const CanvasArtboard &board : m_artboards) bounds |= board.rect;
    return bounds;
}

const CanvasObject *CanvasDocument::object(CanvasObjectId id) const
{
    const auto it = m_objects.constFind(id);
    return it == m_objects.cend() ? nullptr : &it.value();
}

const CanvasLayer *CanvasDocument::layer(CanvasLayerId id) const
{
    const auto it = m_layers.constFind(id);
    return it == m_layers.cend() ? nullptr : &it.value();
}

CanvasTiledImagePtr CanvasDocument::image(CanvasImageId id) const
{
    return m_images.value(id);
}

QVector<CanvasObjectId> CanvasDocument::candidates(const QRectF &region) const
{
    return m_index.query(region);
}

qint64 CanvasDocument::paintKey(const CanvasObject &object) const
{
    // AGENT-NOTE: `order` is sibling-relative since layer groups (D-092);
    // the paint order of a layer is its depth-first rank (refreshLayerTree).
    const qint64 rank = std::max(0, m_layerRank.value(object.layer, 0));
    return (rank << 40) + (object.z & ((qint64(1) << 40) - 1));
}

CanvasObjectId CanvasDocument::allocateId()
{
    return m_nextId < CanvasDocumentFormat::IdCeiling ? m_nextId++ : 0;
}

qint64 CanvasDocument::reserveZ()
{
    constexpr qint64 last = CanvasDocumentFormat::PaintOrderCeiling - 1;
    return m_nextZ < last ? m_nextZ++ : last;
}

void CanvasDocument::setActiveLayer(CanvasLayerId id)
{
    const CanvasLayer *target = layer(id);
    if (id != m_activeLayer && target && !target->group) {
        m_activeLayer = id;
        emit changed();
    }
}

void CanvasDocument::ensureActiveLayer()
{
    const CanvasLayer *current = layer(m_activeLayer);
    if (current && !current->group) return;
    // The topmost content layer in paint order (groups hold no objects).
    m_activeLayer = 0;
    int best = -1;
    for (const CanvasLayer &l : std::as_const(m_layers)) {
        const int rank = m_layerRank.value(l.id, -1);
        if (!l.group && (m_activeLayer == 0 || rank > best)) {
            m_activeLayer = l.id;
            best = rank;
        }
    }
}

int CanvasDocument::nextLayerOrder() const
{
    int order = -1;
    for (const CanvasLayer &l : m_layers) order = std::max(order, l.order);
    return order + 1;
}

QVector<CanvasObjectId> CanvasDocument::degenerateObjects() const
{
    QVector<CanvasObjectId> out;
    for (auto it = m_objects.cbegin(); it != m_objects.cend(); ++it) {
        if (!CanvasGeometry::isRenderable(it.value())) {
            out << it.key();
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

QVector<CanvasObjectId> CanvasDocument::objectIds() const
{
    QVector<CanvasObjectId> ids = m_objects.keys().toVector();
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::optional<CanvasObject> CanvasDocument::copy(CanvasObjectId id) const
{
    const CanvasObject *o = object(id);
    return o ? std::optional<CanvasObject>(*o) : std::nullopt;
}

void CanvasDocument::place(const CanvasObject &object, QVector<QRectF> *damage)
{
    unplace(object.id, damage);
    m_objects.insert(object.id, object);
    // Paint bounds include effect reach, so shadows get candidates and damage.
    const QRectF bounds = CanvasGeometry::paintBounds(object);
    m_bounds.insert(object.id, bounds);
    m_index.insert(object.id, bounds);
    if (!bounds.isNull()) {
        damage->append(bounds);
    }
    ++m_layerUse[object.layer];
    if (object.shape == CanvasShape::Image) ++m_imageUse[object.image];
    m_totalPoints += object.points.size() + object.pathPointCount();
    m_encodedBytes += CanvasDocumentFormat::objectRecordBytes(object);
    m_nextZ = std::max(m_nextZ, object.z + 1);
    m_nextId = std::max(m_nextId, object.id + 1);
}

void CanvasDocument::unplace(CanvasObjectId id, QVector<QRectF> *damage)
{
    const auto it = m_objects.constFind(id);
    if (it == m_objects.cend()) {
        return;
    }
    const auto release = [](auto &uses, quint64 key) {
        if (--uses[key] <= 0) uses.remove(key);
    };
    release(m_layerUse, it->layer);
    if (it->shape == CanvasShape::Image) release(m_imageUse, it->image);
    m_totalPoints -= it->points.size() + it->pathPointCount();
    m_encodedBytes -= CanvasDocumentFormat::objectRecordBytes(*it);
    const QRectF bounds = m_bounds.take(id);
    if (!bounds.isNull()) {
        damage->append(bounds);
    }
    m_index.remove(id);
    m_objects.remove(id);
}

void CanvasDocument::placeImage(CanvasImageId id, const CanvasTiledImagePtr &image)
{
    if (const CanvasTiledImagePtr old = m_images.value(id)) {
        m_imageBytes -= CanvasDocumentFormat::imageBytes(*old);
        m_encodedBytes -= CanvasDocumentFormat::imageRecordBytes(*old);
    }
    if (image) {
        m_images.insert(id, image);
        m_imageBytes += CanvasDocumentFormat::imageBytes(*image);
        m_encodedBytes += CanvasDocumentFormat::imageRecordBytes(*image);
        m_nextImage = std::max(m_nextImage, id + 1);
    } else {
        m_images.remove(id);
    }
}

void CanvasDocument::placeLayer(const CanvasLayer &layer)
{
    unplaceLayer(layer.id);
    m_layers.insert(layer.id, layer);
    m_encodedBytes += CanvasDocumentFormat::layerRecordBytes(layer);
    m_nextLayer = std::max(m_nextLayer, layer.id + 1);
}

void CanvasDocument::unplaceLayer(CanvasLayerId id)
{
    const auto it = m_layers.constFind(id);
    if (it == m_layers.cend()) return;
    m_encodedBytes -= CanvasDocumentFormat::layerRecordBytes(*it);
    m_layers.erase(it);
}

void CanvasDocument::finishChange(const QVector<QRectF> &damage, bool full)
{
    ++m_revision;
    if (full) {
        emit reset(m_revision);
    } else if (!damage.isEmpty()) {
        emit damaged(damage, m_revision);
    }
    emit edited(m_revision);
    emit changed();
}

bool CanvasDocument::setOutputProfileIcc(const QByteArray &bytes)
{
    if (bytes == m_outputProfileIcc) return true;
    if (!bytes.isEmpty()) {
        OperationError error;
        if (!ColorProfile::fromIcc(bytes, &error).isValid()) {
            m_lastError = error.message; emit historyChanged(); return false;
        }
        const QColorSpace space = QColorSpace::fromIccProfile(bytes);
        if (!space.isValid() || !space.isValidTarget() || space.colorModel() != QColorSpace::ColorModel::Rgb) {
            m_lastError = tr("Output ICC profile is not a supported RGB Qt output space");
            emit historyChanged(); return false;
        }
    }
    if (CanvasDocumentFormat::FixedBytes + m_encodedBytes + m_limits.maximumExtensionBytes
            + bytes.size() > m_limits.maximumEncodedBytes) {
        m_lastError = tr("Output ICC profile would exceed the saved-file limit");
        emit historyChanged(); return false;
    }
    m_outputProfileIcc = bytes;
    m_lastError.clear();
    finishChange({}, true);
    return true;
}

void CanvasDocument::layerDamage(CanvasLayerId id, QVector<QRectF> *damage) const
{
    for (auto it = m_objects.cbegin(); it != m_objects.cend(); ++it) {
        if (it->layer == id) {
            const QRectF b = m_bounds.value(it.key());
            if (!b.isNull()) {
                damage->append(b);
            }
        }
    }
}

void CanvasDocument::imageDamage(CanvasImageId id, QVector<QRectF> *damage) const
{
    if (m_imageUse.value(id) == 0) return;
    for (auto it = m_objects.cbegin(); it != m_objects.cend(); ++it) {
        if (it->shape == CanvasShape::Image && it->image == id) {
            const QRectF b = m_bounds.value(it.key());
            if (!b.isNull()) damage->append(b);
        }
    }
}

void CanvasDocument::resetModel()
{
    ++m_resetGeneration;
    // A reset is a load, not an edit: recorded steps cannot apply to it.
    invalidateHistory();
}

void CanvasDocument::clear()
{
    m_objects.clear();
    m_bounds.clear();
    m_index.clear();
    m_images.clear();
    m_layerUse.clear();
    m_imageUse.clear();
    m_totalPoints = 0;
    m_imageBytes = 0;
    m_encodedBytes = 0;
    for (const CanvasLayer &l : std::as_const(m_layers))
        m_encodedBytes += CanvasDocumentFormat::layerRecordBytes(l);
    resetModel();
    finishChange({}, true);
}

} // namespace QindaTK
