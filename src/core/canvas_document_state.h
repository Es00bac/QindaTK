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
    CanvasObjectId nextObject = 1;
    CanvasLayerId nextLayer = 1;
    CanvasImageId nextImage = 1;
    qint64 nextZ = 1;
    QHash<CanvasObjectId, CanvasObject> objects;
    QHash<CanvasLayerId, CanvasLayer> layers;
    QHash<CanvasImageId, CanvasTiledImagePtr> images;
    QJsonObject extensions;
};

} // namespace QindaTK
