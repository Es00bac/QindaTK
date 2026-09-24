// SPDX-License-Identifier: LGPL-3.0-or-later
// Saved artboard and layer-tree records (D-092, D-093): the validation that
// commits and the decoder share, and their place in the extensions block.
#include "canvas_document_format.h"

#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace QindaTK::CanvasDocumentFormat {
namespace {
OperationError problem(ErrorCode code, const QString &message, const QString &field)
{
    return {code, message, field, QStringLiteral("canvas.scene")};
}

bool finiteArea(const QRectF &r)
{
    return std::isfinite(r.x()) && std::isfinite(r.y()) && std::isfinite(r.width())
        && std::isfinite(r.height()) && std::isfinite(r.right()) && std::isfinite(r.bottom())
        && r.width() > 0 && r.height() > 0;
}

// Exact 64-bit layer ids travel as decimal strings (JSON numbers are doubles).
std::optional<quint64> parseId(const QJsonValue &value)
{
    if (!value.isString()) return std::nullopt;
    bool ok = false;
    const quint64 id = value.toString().toULongLong(&ok);
    return ok ? std::optional<quint64>(id) : std::nullopt;
}

bool validCounter(const QJsonValue &value)
{
    const double counter = value.toDouble(-1);
    return value.isDouble() && counter >= 1 && counter <= 9007199254740992.0
        && std::floor(counter) == counter;
}

OperationError takeArtboards(const CanvasDocumentLimits &limits, CanvasDocumentState *state)
{
    state->artboards.clear();
    state->nextArtboard = 1;
    const QJsonValue record = state->extensions.take(QLatin1String(ArtboardExtensionKey));
    if (record.isUndefined()) return {};
    const QJsonObject object = record.toObject();
    const QJsonValue next = object.value(QStringLiteral("next"));
    const QJsonValue items = object.value(QStringLiteral("items"));
    if (!record.isObject() || !items.isArray() || !validCounter(next))
        return problem(ErrorCode::Validation, QStringLiteral("Invalid artboard record"), QStringLiteral("artboards"));
    const QJsonArray array = items.toArray();
    if (array.size() > limits.maximumArtboards)
        return problem(ErrorCode::Unsupported, QStringLiteral("Too many artboards"), QStringLiteral("artboards"));
    state->artboards.reserve(array.size());
    for (const QJsonValue &entry : array) {
        const QJsonObject item = entry.toObject();
        const auto value = [&item](const char *key) { return item.value(QLatin1String(key)); };
        bool numbers = true;
        for (const char *key : {"x", "y", "width", "height", "bleed"}) numbers = numbers && value(key).isDouble();
        const QColor background = QColor::fromString(value("background").toString());
        if (!entry.isObject() || !value("id").isString() || !value("name").isString() || !numbers
            || !value("background").isString() || !background.isValid())
            return problem(ErrorCode::Validation, QStringLiteral("Invalid artboard"), QStringLiteral("artboards"));
        CanvasArtboard board;
        board.id = value("id").toString();
        board.name = value("name").toString();
        board.rect = QRectF(value("x").toDouble(), value("y").toDouble(),
                            value("width").toDouble(), value("height").toDouble());
        board.background = background;
        board.bleed = value("bleed").toDouble();
        state->artboards.append(board);
    }
    state->nextArtboard = quint64(next.toDouble());
    return validateArtboards(state->artboards, limits);
}

OperationError takeLayerTree(const CanvasDocumentLimits &limits, CanvasDocumentState *state)
{
    const QJsonValue record = state->extensions.take(QLatin1String(LayerTreeExtensionKey));
    if (!record.isUndefined()) {
        const QJsonValue entries = record.toObject().value(QStringLiteral("layers"));
        if (!record.isObject() || !entries.isArray()
            || entries.toArray().size() > state->layers.size())
            return problem(ErrorCode::Validation, QStringLiteral("Invalid layer tree record"), QStringLiteral("layers"));
        QSet<CanvasLayerId> seen;
        for (const QJsonValue &entry : entries.toArray()) {
            const QJsonObject item = entry.toObject();
            const auto id = parseId(item.value(QStringLiteral("id")));
            const auto parent = parseId(item.value(QStringLiteral("parent")));
            const QJsonValue group = item.value(QStringLiteral("group"));
            if (!entry.isObject() || !id || !parent || !group.isBool()
                || !state->layers.contains(*id) || seen.contains(*id))
                return problem(ErrorCode::Validation, QStringLiteral("Invalid layer tree entry"), QStringLiteral("layers.parent"));
            seen.insert(*id);
            CanvasLayer &layer = state->layers[*id];
            layer.parent = *parent;
            layer.group = group.toBool();
        }
    }
    if (const auto e = validateLayerForest(state->layers, limits)) return e;
    const bool grouped = std::any_of(state->layers.cbegin(), state->layers.cend(),
                                     [](const CanvasLayer &layer) { return layer.group; });
    if (grouped) {
        for (const CanvasObject &object : std::as_const(state->objects)) {
            const auto found = state->layers.constFind(object.layer);
            if (found != state->layers.cend() && found->group)
                return problem(ErrorCode::Validation, QStringLiteral("A layer group cannot hold objects"), QStringLiteral("objects.layer"));
        }
    }
    return {};
}
} // namespace

OperationError validateArtboards(const QVector<CanvasArtboard> &boards, const CanvasDocumentLimits &limits)
{
    if (boards.size() > limits.maximumArtboards)
        return problem(ErrorCode::Unsupported, QStringLiteral("Too many artboards"), QStringLiteral("artboards"));
    QSet<QString> seen;
    seen.reserve(boards.size());
    for (const CanvasArtboard &board : boards) {
        if (board.id.isEmpty() || board.id.size() > limits.maximumArtboardIdLength || seen.contains(board.id))
            return problem(ErrorCode::Validation, QStringLiteral("Invalid or duplicate artboard id"), QStringLiteral("artboards.id"));
        if (board.name.size() > limits.maximumNameLength)
            return problem(ErrorCode::Unsupported, QStringLiteral("Artboard name is too long"), QStringLiteral("artboards.name"));
        if (!finiteArea(board.rect))
            return problem(ErrorCode::Validation, QStringLiteral("An artboard needs a finite rectangle of positive size"), QStringLiteral("artboards.rect"));
        // AGENT-GUARD: the record saves #AARRGGBB, so only 8-bit sRGB colours
        // are content; commands normalize before they build the edit.
        if (!board.background.isValid() || board.background.spec() != QColor::Rgb
            || board.background != QColor::fromRgba(board.background.rgba()))
            return problem(ErrorCode::Validation, QStringLiteral("An artboard background must be an 8-bit sRGB colour"), QStringLiteral("artboards.background"));
        if (!std::isfinite(board.bleed) || board.bleed < 0 || !finiteArea(board.exportRect()))
            return problem(ErrorCode::Validation, QStringLiteral("Artboard bleed must be finite and not negative"), QStringLiteral("artboards.bleed"));
        seen.insert(board.id);
    }
    return {};
}

OperationError validateLayerForest(const QHash<CanvasLayerId, CanvasLayer> &layers,
                                   const CanvasDocumentLimits &limits)
{
    QHash<CanvasLayerId, int> depth;
    depth.reserve(layers.size());
    QVector<CanvasLayerId> chain;
    bool content = false;
    for (auto it = layers.cbegin(); it != layers.cend(); ++it) {
        content = content || !it->group;
        chain.clear();
        int base = -1;
        CanvasLayerId cursor = it.key();
        // Walk up to the first ancestor with a known depth (or the top level).
        while (cursor != 0) {
            const auto known = depth.constFind(cursor);
            if (known != depth.cend()) {
                base = known.value();
                break;
            }
            if (chain.contains(cursor))
                return problem(ErrorCode::Validation, QStringLiteral("The layer hierarchy contains a cycle"), QStringLiteral("layers.parent"));
            if (chain.size() > limits.maximumLayerDepth)
                return problem(ErrorCode::Unsupported, QStringLiteral("Layers are nested too deeply"), QStringLiteral("layers.parent"));
            chain.append(cursor);
            const CanvasLayerId parent = layers.constFind(cursor)->parent;
            if (parent != 0) {
                const auto found = layers.constFind(parent);
                if (found == layers.cend())
                    return problem(ErrorCode::Validation, QStringLiteral("A layer's parent is missing"), QStringLiteral("layers.parent"));
                if (!found->group)
                    return problem(ErrorCode::Validation, QStringLiteral("A layer's parent must be a layer group"), QStringLiteral("layers.parent"));
            }
            cursor = parent;
        }
        for (qsizetype i = chain.size() - 1; i >= 0; --i) {
            if (++base >= limits.maximumLayerDepth)
                return problem(ErrorCode::Unsupported, QStringLiteral("Layers are nested too deeply"), QStringLiteral("layers.parent"));
            depth.insert(chain.at(i), base);
        }
    }
    if (!layers.isEmpty() && !content)
        return problem(ErrorCode::Validation, QStringLiteral("A canvas keeps at least one content layer"), QStringLiteral("layers.group"));
    return {};
}

QJsonObject savedExtensions(const CanvasDocumentState &state)
{
    QJsonObject extensions = extensionsWithGuides(state);
    extensions.remove(QLatin1String(ArtboardExtensionKey));
    extensions.remove(QLatin1String(LayerTreeExtensionKey));
    if (!state.artboards.isEmpty() || state.nextArtboard > 1) {
        QJsonArray items;
        for (const CanvasArtboard &board : state.artboards)
            items.append(QJsonObject{{QStringLiteral("id"), board.id},
                                     {QStringLiteral("name"), board.name},
                                     {QStringLiteral("x"), board.rect.x()},
                                     {QStringLiteral("y"), board.rect.y()},
                                     {QStringLiteral("width"), board.rect.width()},
                                     {QStringLiteral("height"), board.rect.height()},
                                     {QStringLiteral("background"), board.background.name(QColor::HexArgb)},
                                     {QStringLiteral("bleed"), board.bleed}});
        extensions.insert(QLatin1String(ArtboardExtensionKey),
                          QJsonObject{{QStringLiteral("next"), double(state.nextArtboard)},
                                      {QStringLiteral("items"), items}});
    }
    QList<CanvasLayerId> ids = state.layers.keys();
    std::sort(ids.begin(), ids.end());
    QJsonArray tree;
    for (const CanvasLayerId id : std::as_const(ids)) {
        const CanvasLayer &layer = *state.layers.constFind(id);
        if (layer.parent == 0 && !layer.group) continue; // flat layers add nothing
        tree.append(QJsonObject{{QStringLiteral("id"), QString::number(id)},
                                {QStringLiteral("parent"), QString::number(layer.parent)},
                                {QStringLiteral("group"), layer.group}});
    }
    if (!tree.isEmpty())
        extensions.insert(QLatin1String(LayerTreeExtensionKey), QJsonObject{{QStringLiteral("layers"), tree}});
    return extensions;
}

OperationError takeSavedExtensions(const QJsonObject &extensions, const CanvasDocumentLimits &limits,
                                   CanvasDocumentState *state)
{
    if (const auto e = takeGuides(extensions, limits, state)) return e;
    if (const auto e = takeArtboards(limits, state)) return e;
    return takeLayerTree(limits, state);
}

} // namespace QindaTK::CanvasDocumentFormat
