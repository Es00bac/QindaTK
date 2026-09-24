// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"
#include "canvas_object.h"
#include <QHash>
#include <QJsonObject>

namespace QindaTK {

// AGENT-CONTRACT (docs/canvas.md#persistence): bounds shared by the canvas
// commit validator and the native decoder. A committed canvas therefore
// always saves and reopens, and a hostile file cannot make the decoder
// allocate beyond these totals before it fails.
struct CanvasDocumentLimits {
    qsizetype maximumEncodedBytes = qsizetype(320) * 1024 * 1024;
    qsizetype maximumObjects = 2'000'000;
    qsizetype maximumLayers = 4096;
    qsizetype maximumImages = 4096;
    qsizetype maximumPointsPerObject = 1'000'000;
    qsizetype maximumTotalPoints = 16'000'000;
    qsizetype maximumNameLength = 4096;
    int maximumImageSide = 65536;
    int minimumTileSize = 16;
    int maximumTileSize = 4096;
    // Decoded ARGB32 bytes of all images together.
    qsizetype maximumImageBytes = qsizetype(256) * 1024 * 1024;
    qsizetype maximumExtensionBytes = 16 * 1024 * 1024;
    // Document guides share the extensions JSON block (see CanvasGuide).
    qsizetype maximumGuides = 1024;
    qsizetype maximumGuideIdLength = 64;
    // Artboards and the layer tree share it too (CanvasArtboard, CanvasLayer).
    qsizetype maximumArtboards = 1024;
    qsizetype maximumArtboardIdLength = 64;
    int maximumLayerDepth = 64; // nesting levels; a top-level layer is depth 0
};

// AGENT-CONTRACT (docs/canvas.md#document-guides): a guide is document
// content with a stable string id, an axis (a vertical guide is the line
// x = position) and a finite document-unit position. It is saved inside
// the extensions JSON under CanvasDocumentFormat::GuideExtensionKey, so
// the binary layout is unchanged and an older reader keeps it opaque.
struct CanvasGuide {
    QString id;
    Qt::Orientation orientation = Qt::Vertical;
    qreal position = 0;
    friend bool operator==(const CanvasGuide &, const CanvasGuide &) = default;
};

// AGENT-CONTRACT (docs/canvas.md#artboards, D-093): an artboard is document
// content with a stable string id, a name, a document-space rectangle of
// positive finite size, an 8-bit sRGB background painted below every object,
// and a non-negative `bleed` that grows its export region. List order is
// board order: a later board's background paints above an earlier one's.
// Saved in the extensions JSON under CanvasDocumentFormat::ArtboardExtensionKey.
struct CanvasArtboard {
    QString id;
    QString name;
    QRectF rect;
    QColor background{Qt::white};
    qreal bleed = 0;
    [[nodiscard]] QRectF exportRect() const { return rect.adjusted(-bleed, -bleed, bleed, bleed); }
    friend bool operator==(const CanvasArtboard &, const CanvasArtboard &) = default;
};

// Immutable, implicitly shared copy of a canvas document's content. Taking
// one on the GUI thread is O(1): objects are values in shared hashes and
// images are immutable shared tiles, so a worker can encode it while the
// document keeps changing (the next edit detaches the document's copy).
struct CanvasDocumentState {
    DocumentId documentId;
    quint64 revision = 0;
    quint64 resetGeneration = 0;
    QRectF pageRect;
    QColor pageColor;
    QByteArray outputProfileIcc; // empty = sRGB; owned RGB ICC bytes in native v2
    CanvasObjectId nextObject = 1;
    CanvasLayerId nextLayer = 1;
    CanvasImageId nextImage = 1;
    qint64 nextZ = 1;
    QHash<CanvasObjectId, CanvasObject> objects;
    QHash<CanvasLayerId, CanvasLayer> layers;
    QHash<CanvasImageId, CanvasTiledImagePtr> images;
    QVector<CanvasGuide> guides; // document order
    quint64 nextGuide = 1;
    QVector<CanvasArtboard> artboards; // board order
    quint64 nextArtboard = 1;
    QJsonObject extensions;
};

} // namespace QindaTK
