// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QColor>
#include <QImage>
#include <QPoint>

namespace QindaTK::CanvasEffects {

// AGENT-NOTE (D-107): the Filter Effects feGaussianBlur approximation (three
// box blurs of size d = floor(sigma * 3 * sqrt(2 * pi) / 4 + 0.5); for even d
// two boxes of size d offset half a pixel either way and one of size d + 1),
// so the reference canvas and an SVG renderer blur alike. Pixels outside the
// image are transparent. `image` becomes ARGB32 premultiplied; `sigma` is in
// pixels and a d below 2 leaves the image unchanged.
void gaussianBlur(QImage &image, qreal sigma);

// `layer` becomes itself over its shadow: its alpha shifted by `offset`
// pixels, tinted with `color` (alpha included) and blurred with `sigma`.
void dropShadow(QImage &layer, QPoint offset, qreal sigma, const QColor &color);

// Per-pixel colour adjustment on straight (unpremultiplied) sRGB values, as
// the SVG export writes it: feColorMatrix "saturate" then feComponentTransfer
// "linear" (slope = contrast, intercept = 0.5 - 0.5 * contrast + brightness),
// each clamped to 0..1. Alpha is unchanged.
void colorAdjust(QImage &layer, qreal brightness, qreal contrast, qreal saturation);

} // namespace QindaTK::CanvasEffects
