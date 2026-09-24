// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "canvas_document.h"

namespace QindaTK::CanvasSvgExport {
// SVG 1.1 vector subset: paths, rectangles, ellipses and polylines with
// flat color, transforms, fill rules, caps, joins and dashes, over the page
// (or region) background and the artboard backgrounds. Rejects image and
// managed ICC fills explicitly rather than silently changing output.
QByteArray encode(const CanvasDocument &document, OperationError *error = nullptr);
bool write(const CanvasDocument &document, const QString &filePath, OperationError *error = nullptr);
// The same SVG with `region` (an artboard's export rectangle) as its viewBox
// and backing rectangle, filled with `background` instead of the page.
QByteArray encodeRegion(const CanvasDocument &document, const QRectF &region,
                        const QColor &background, OperationError *error = nullptr);
bool writeRegion(const CanvasDocument &document, const QRectF &region, const QColor &background,
                 const QString &filePath, OperationError *error = nullptr);
} // namespace QindaTK::CanvasSvgExport
