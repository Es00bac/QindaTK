// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QPainterPath>
#include <QStringView>

namespace QindaTK {

// Parses an SVG path `d` attribute (M L H V C S Q T A Z and their relative
// forms, implicit command repetition, squeezed arc flags) into a
// QPainterPath. Arcs become cubic Béziers. `ok` reports a syntax error;
// the path parsed so far is still returned.
[[nodiscard]] QPainterPath svgPathToPainterPath(QStringView d, bool *ok = nullptr);

} // namespace QindaTK
