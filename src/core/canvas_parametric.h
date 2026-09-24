// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "canvas_object.h"
#include <QString>

// AGENT-CONTRACT (docs/vector-authoring.md#parametric-shapes): a parametric
// object is a closed one-contour Path whose `rect` and parametric fields are
// the editable definition and whose `pathNodes` are generated from it here.
// CanvasDocument (canvas_document_parametric.cpp) edits the definition and
// regenerates the nodes in one CanvasEdit; the native codec persists both.
namespace QindaTK::CanvasParametric {
inline constexpr quint32 MinimumPoints = 3;  // polygon sides or star tips
inline constexpr quint32 MaximumPoints = 64;

// Stable definition IDs: "polygon", "star" and "rounded-rectangle".
[[nodiscard]] CanvasParametricKind kind(const QString &id);
[[nodiscard]] QString id(CanvasParametricKind kind);
// The definition alone: finite positive bounds and only the parameters the
// kind owns (the others are zero). Kind None carries no parameters.
[[nodiscard]] bool valid(const CanvasObject &object);
// Rebuilds local cubic path nodes from persisted parameters. The document
// stores both the editable definition and the resulting geometry so the
// normal canvas renderer, hit tester and SVG exporter have one path contract.
// Empty when the definition is invalid or the kind is None.
[[nodiscard]] QVector<CanvasPathNode> nodes(const CanvasObject &object);
// AGENT-GUARD: object validation (every commit and every decoded file)
// requires a parametric object's stored nodes to be its generated nodes, so
// an edit that changes nodes must clearParametric() or it is rejected, and a
// later parameter edit can never silently discard them. The comparison
// tolerates last-bit sin/cos differences between platforms.
[[nodiscard]] bool matchesNodes(const CanvasObject &object);
}
