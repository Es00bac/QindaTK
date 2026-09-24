// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"
#include "canvas_object.h"

#include <QMarginsF>
#include <QPainter>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <optional>

namespace QindaTK::CanvasAppearanceRules {

// AGENT-CONTRACT (docs/vector-authoring.md#appearance, D-106): the rules that
// commit validation (validateObject), the native v6 decoder, CanvasStyleModel
// and CanvasAppearanceModel share. Effect offsets and blurs are document
// units, like stroke widths; pattern geometry is local units.
inline constexpr qsizetype MaximumEffects = 32;
inline constexpr qsizetype MaximumTokenLength = 64; // effect ids and kinds
inline constexpr qsizetype MaximumParameterBytes = 16 * 1024;
inline constexpr qreal MaximumShadowOffset = 1000;
inline constexpr qreal MaximumBlur = 250; // standard deviation
inline constexpr qreal MinimumPatternSpacing = 0.05;
inline constexpr qreal MaximumPatternSpacing = 10000;

[[nodiscard]] QString alignmentName(CanvasStrokeAlignment alignment);
[[nodiscard]] std::optional<CanvasStrokeAlignment> alignment(const QString &name);
// CSS mix-blend-mode keywords ("normal", "multiply", ..., "exclusion").
[[nodiscard]] QString blendName(CanvasBlendMode mode);
[[nodiscard]] std::optional<CanvasBlendMode> blend(const QString &name);
[[nodiscard]] QStringList blendNames();
[[nodiscard]] QPainter::CompositionMode compositionMode(CanvasBlendMode mode);
// "none", "lines", "grid", "dots", "checker".
[[nodiscard]] QString patternName(CanvasPatternKind kind);
[[nodiscard]] std::optional<CanvasPatternKind> patternKind(const QString &name);

// Built-in pattern swatches as FillStyleEditor.patterns records ({id,label}),
// the definition each id stands for (its ink is left invalid for the caller)
// and the swatch a definition matches ("" for a custom pattern).
[[nodiscard]] QVariantList patternSwatches();
[[nodiscard]] std::optional<CanvasPattern> patternSwatch(const QString &id);
[[nodiscard]] QString swatchId(const CanvasPattern &pattern);

// Effect kinds the reference renderer draws and SVG export writes:
// "drop-shadow" {offsetX, offsetY, blur, color} and "gaussian-blur" {radius}.
[[nodiscard]] QStringList supportedEffects();
[[nodiscard]] bool supportedEffect(const QString &kind);
[[nodiscard]] QString effectName(const QString &kind);
// Canonical compact JSON for `parameters` of `kind`. A supported kind takes
// only its own keys, filling missing ones with defaults; any other kind keeps
// an arbitrary JSON object so a richer host's effect survives unchanged.
[[nodiscard]] OperationError canonicalParameters(const QString &kind, const QVariantMap &parameters,
                                                 QByteArray *out);
[[nodiscard]] QVariantMap parameterMap(const CanvasEffect &effect);

struct Shadow {
    QPointF offset;
    qreal blur = 0;
    QColor color;
};
// Values of a validated effect of the matching kind.
[[nodiscard]] Shadow shadow(const CanvasEffect &effect);
[[nodiscard]] qreal blurRadius(const CanvasEffect &effect);

// Enum ranges, finite bounded values, closed vector shapes for alignment and
// patterns, unique token ids and canonical parameters.
[[nodiscard]] OperationError validate(const CanvasObject &object);

// Distance from the outline that stroke pixels cover on their painted side:
// half the width when centred, the whole width inside or outside.
[[nodiscard]] qreal strokeReach(const CanvasObject &object);
// How far stroke pixels extend OUTSIDE the outline (bounds padding).
[[nodiscard]] qreal strokeOutset(const CanvasObject &object);
// Growth of the painted area beyond documentBounds from enabled effects.
[[nodiscard]] QMarginsF effectMargins(const CanvasObject &object);
// Source distance an output pixel can depend on across the whole stack.
[[nodiscard]] qreal effectInputReach(const CanvasObject &object);
[[nodiscard]] bool hasRenderedEffects(const CanvasObject &object);
// Alignment, opacity, blend or an enabled supported effect: the object is
// painted through an offscreen layer (D-107).
[[nodiscard]] bool needsLayer(const CanvasObject &object);

} // namespace QindaTK::CanvasAppearanceRules

namespace QindaTK::CanvasAppearanceCodec {
// Native v6 per-object block (D-106), big-endian like the rest of the file:
// empty for the default appearance.
inline constexpr qsizetype MaximumBytes = 1024 * 1024;
[[nodiscard]] QByteArray encode(const CanvasAppearance &appearance);
// Reads a block written by encode(); callers still run validate().
[[nodiscard]] OperationError decode(const QByteArray &bytes, CanvasAppearance *appearance);
} // namespace QindaTK::CanvasAppearanceCodec
