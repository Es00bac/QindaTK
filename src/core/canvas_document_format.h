// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "canvas_document_state.h"
#include "document_codec.h"
#include <limits>
#include <optional>

namespace QindaTK {

// AGENT-CONTRACT (docs/canvas.md#native-format): the canvas native file.
// Big-endian binary: signature "QTKCANVS", version, identity header
// (document UUID, revision, reset generation), exact 64-bit counters, page,
// extensions JSON, layers, images, objects, then a SHA-256 of everything
// before it. Records are written in ascending id order, so equal content
// gives equal bytes. Image tiles are stored as raw ARGB32 premultiplied rows: qUncompress
// ignores its declared size, so compressed tiles would need a bounded
// inflater before the decoder could accept them.
namespace CanvasDocumentFormat {
inline constexpr char Signature[] = "QTKCANVS";
inline constexpr qsizetype SignatureBytes = 8;
inline constexpr quint32 Version = 1;
inline constexpr const char *SchemaId = "qindatk.canvas-document.v1";
inline constexpr const char *FormatId = "qindatk.canvas";
// Valid ids are 1 .. IdCeiling - 1 and valid paint orders are below
// PaintOrderCeiling, so the "next" counters (max + 1) always stay storable.
inline constexpr quint64 IdCeiling = std::numeric_limits<quint64>::max() - 1;
inline constexpr qint64 PaintOrderCeiling = std::numeric_limits<qint64>::max() - 1;

[[nodiscard]] bool recognizes(const QByteArray &prefix);
// Identity header only (no checksum or body validation); any thread.
[[nodiscard]] std::optional<SnapshotHeader> inspect(const QByteArray &bytes,
                                                    OperationError *error = nullptr);
// Worker-safe: reads shared immutable state only.
[[nodiscard]] QByteArray encode(const CanvasDocumentState &state, OperationError *error = nullptr);
// Full validation (checksum, identity, counters, duplicates, references,
// finite geometry, dimensions and totals) before decoded pixels are built.
[[nodiscard]] std::optional<CanvasDocumentState> decode(const QByteArray &bytes,
                                                        const CanvasDocumentLimits &limits,
                                                        OperationError *error = nullptr);

// Exact encoded sizes, so commits can keep every document saveable: a file
// is FixedBytes + its records + the extensions JSON. Names are UTF-16 code
// units, so any QString (a leading U+FEFF, an unpaired surrogate) is exact.
inline constexpr qsizetype FixedBytes = SignatureBytes + 4 + 16 + 8 + 8 + 4 * 8 + 4 * 8 + 11
    + 4 + 4 + 4 + 4 + 32;
[[nodiscard]] qsizetype objectRecordBytes(const CanvasObject &object);
[[nodiscard]] qsizetype layerRecordBytes(const CanvasLayer &layer);
[[nodiscard]] qsizetype imageRecordBytes(const CanvasTiledImage &image);

// Per-item rules shared with CanvasDocument's commit validation.
[[nodiscard]] OperationError validateObject(const CanvasObject &object, CanvasObjectId id,
                                            const CanvasDocumentLimits &limits);
[[nodiscard]] OperationError validateLayer(const CanvasLayer &layer, CanvasLayerId id,
                                           const CanvasDocumentLimits &limits);
[[nodiscard]] OperationError validateImage(const CanvasTiledImage &image, CanvasImageId id,
                                           const CanvasDocumentLimits &limits);
[[nodiscard]] OperationError validatePage(const QRectF &rect, const QColor &color);
[[nodiscard]] qsizetype imageBytes(const CanvasTiledImage &image);
} // namespace CanvasDocumentFormat

// A canvas codec with explicit bounds; canvasDocumentCodec() uses defaults.
DocumentCodecPtr makeCanvasDocumentCodec(const CanvasDocumentLimits &limits);

} // namespace QindaTK
