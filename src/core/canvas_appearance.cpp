// SPDX-License-Identifier: LGPL-3.0-or-later
// Appearance rules shared by commits, the decoder and the models (D-106).
#include "canvas_appearance.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <iterator>

namespace QindaTK::CanvasAppearanceRules {
namespace {
OperationError problem(ErrorCode code, const QString &message, const QString &field)
{
    return {code, message, QStringLiteral("objects.appearance.") + field,
            QStringLiteral("canvas.appearance")};
}

constexpr const char *AlignmentNames[] = {"center", "inside", "outside"};
constexpr const char *BlendNames[] = {"normal", "multiply", "screen", "overlay", "darken",
                                      "lighten", "color-dodge", "color-burn", "hard-light",
                                      "soft-light", "difference", "exclusion"};
constexpr const char *PatternNames[] = {"none", "lines", "grid", "dots", "checker"};

struct Swatch {
    const char *id;
    const char *label;
    CanvasPatternKind kind;
    qreal spacing;
    qreal angle;
    qreal weight;
};
constexpr Swatch Swatches[] = {
    {"hatch", QT_TRANSLATE_NOOP("CanvasAppearance", "Hatch"), CanvasPatternKind::Lines, 8, 45, 2},
    {"crosshatch", QT_TRANSLATE_NOOP("CanvasAppearance", "Crosshatch"), CanvasPatternKind::Grid, 8, 45, 1.5},
    {"grid", QT_TRANSLATE_NOOP("CanvasAppearance", "Grid"), CanvasPatternKind::Grid, 10, 0, 1},
    {"dots", QT_TRANSLATE_NOOP("CanvasAppearance", "Dots"), CanvasPatternKind::Dots, 8, 0, 3},
    {"checker", QT_TRANSLATE_NOOP("CanvasAppearance", "Checker"), CanvasPatternKind::Checker, 16, 0, 8},
};

bool validColor(const QColor &color)
{
    const int spec = int(color.spec());
    if (spec < int(QColor::Invalid) || spec > int(QColor::ExtendedRgb)) return false;
    if (spec == int(QColor::Invalid)) return true;
    float r = 0, g = 0, b = 0, a = 0;
    color.getRgbF(&r, &g, &b, &a);
    return std::isfinite(r) && std::isfinite(g) && std::isfinite(b) && std::isfinite(a);
}

// Short ASCII identifiers: they appear in objectNames and SVG references.
bool token(const QString &value)
{
    if (value.isEmpty() || value.size() > MaximumTokenLength) return false;
    for (const QChar c : value) {
        const char16_t u = c.unicode();
        const bool alnum = (u >= u'a' && u <= u'z') || (u >= u'A' && u <= u'Z') || (u >= u'0' && u <= u'9');
        if (!alnum && u != u'-' && u != u'_' && u != u'.') return false;
    }
    return true;
}

bool numeric(const QVariant &value)
{
    const int type = value.typeId();
    return type == QMetaType::Double || type == QMetaType::Float || type == QMetaType::Int
        || type == QMetaType::UInt || type == QMetaType::LongLong || type == QMetaType::ULongLong;
}

// A number in [low, high]; missing keys take `fallback`. JSON has no -0.
std::optional<double> number(const QVariantMap &map, const char *key, double fallback,
                             double low, double high)
{
    const QString name = QLatin1String(key);
    if (!map.contains(name)) return fallback;
    const QVariant value = map.value(name);
    const double n = value.toDouble();
    if (!numeric(value) || !std::isfinite(n) || n < low || n > high) return std::nullopt;
    return n == 0 ? 0.0 : n;
}

// A second pass settles number spellings, so stored bytes are a fixed point.
QByteArray canonicalJson(const QJsonObject &object)
{
    const QByteArray once = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return QJsonDocument(QJsonDocument::fromJson(once).object()).toJson(QJsonDocument::Compact);
}

QJsonObject parameters(const CanvasEffect &effect)
{
    return QJsonDocument::fromJson(effect.parameters).object();
}

OperationError unknownKey(const QString &kind, const QString &key)
{
    return problem(ErrorCode::Validation,
                   QStringLiteral("Unknown %1 parameter: %2").arg(kind, key),
                   QStringLiteral("effects.parameters"));
}
} // namespace

QString alignmentName(CanvasStrokeAlignment value)
{
    const auto index = qsizetype(value);
    return index < qsizetype(std::size(AlignmentNames)) ? QLatin1String(AlignmentNames[index]) : QString();
}

std::optional<CanvasStrokeAlignment> alignment(const QString &name)
{
    for (qsizetype i = 0; i < qsizetype(std::size(AlignmentNames)); ++i)
        if (name == QLatin1String(AlignmentNames[i])) return CanvasStrokeAlignment(i);
    return std::nullopt;
}

QString blendName(CanvasBlendMode mode)
{
    const auto index = qsizetype(mode);
    return index < qsizetype(std::size(BlendNames)) ? QLatin1String(BlendNames[index]) : QString();
}

std::optional<CanvasBlendMode> blend(const QString &name)
{
    for (qsizetype i = 0; i < qsizetype(std::size(BlendNames)); ++i)
        if (name == QLatin1String(BlendNames[i])) return CanvasBlendMode(i);
    return std::nullopt;
}

QStringList blendNames()
{
    QStringList names;
    for (const char *name : BlendNames) names.append(QLatin1String(name));
    return names;
}

QPainter::CompositionMode compositionMode(CanvasBlendMode mode)
{
    switch (mode) {
    case CanvasBlendMode::Normal: return QPainter::CompositionMode_SourceOver;
    case CanvasBlendMode::Multiply: return QPainter::CompositionMode_Multiply;
    case CanvasBlendMode::Screen: return QPainter::CompositionMode_Screen;
    case CanvasBlendMode::Overlay: return QPainter::CompositionMode_Overlay;
    case CanvasBlendMode::Darken: return QPainter::CompositionMode_Darken;
    case CanvasBlendMode::Lighten: return QPainter::CompositionMode_Lighten;
    case CanvasBlendMode::ColorDodge: return QPainter::CompositionMode_ColorDodge;
    case CanvasBlendMode::ColorBurn: return QPainter::CompositionMode_ColorBurn;
    case CanvasBlendMode::HardLight: return QPainter::CompositionMode_HardLight;
    case CanvasBlendMode::SoftLight: return QPainter::CompositionMode_SoftLight;
    case CanvasBlendMode::Difference: return QPainter::CompositionMode_Difference;
    case CanvasBlendMode::Exclusion: return QPainter::CompositionMode_Exclusion;
    }
    return QPainter::CompositionMode_SourceOver;
}

QString patternName(CanvasPatternKind kind)
{
    const auto index = qsizetype(kind);
    return index < qsizetype(std::size(PatternNames)) ? QLatin1String(PatternNames[index]) : QString();
}

std::optional<CanvasPatternKind> patternKind(const QString &name)
{
    for (qsizetype i = 0; i < qsizetype(std::size(PatternNames)); ++i)
        if (name == QLatin1String(PatternNames[i])) return CanvasPatternKind(i);
    return std::nullopt;
}

QVariantList patternSwatches()
{
    QVariantList list;
    for (const Swatch &swatch : Swatches)
        list.append(QVariantMap{{QStringLiteral("id"), QLatin1String(swatch.id)},
                                {QStringLiteral("label"),
                                 QCoreApplication::translate("CanvasAppearance", swatch.label)},
                                {QStringLiteral("kind"), patternName(swatch.kind)}});
    return list;
}

std::optional<CanvasPattern> patternSwatch(const QString &id)
{
    for (const Swatch &swatch : Swatches)
        if (id == QLatin1String(swatch.id))
            return CanvasPattern{swatch.kind, QColor(), swatch.spacing, swatch.angle, swatch.weight};
    return std::nullopt;
}

QString swatchId(const CanvasPattern &pattern)
{
    for (const Swatch &swatch : Swatches)
        if (pattern.kind == swatch.kind && pattern.spacing == swatch.spacing
            && pattern.angle == swatch.angle && pattern.weight == swatch.weight)
            return QLatin1String(swatch.id);
    return {};
}

QStringList supportedEffects()
{
    return {QStringLiteral("drop-shadow"), QStringLiteral("gaussian-blur"), QStringLiteral("color-adjust")};
}

bool supportedEffect(const QString &kind)
{
    return kind == QLatin1String("drop-shadow") || kind == QLatin1String("gaussian-blur")
        || kind == QLatin1String("color-adjust");
}

QString effectName(const QString &kind)
{
    if (kind == QLatin1String("drop-shadow"))
        return QCoreApplication::translate("CanvasAppearance", "Drop shadow");
    if (kind == QLatin1String("gaussian-blur"))
        return QCoreApplication::translate("CanvasAppearance", "Gaussian blur");
    if (kind == QLatin1String("color-adjust"))
        return QCoreApplication::translate("CanvasAppearance", "Colour adjustment");
    return kind;
}

OperationError canonicalParameters(const QString &kind, const QVariantMap &values, QByteArray *out)
{
    if (!token(kind))
        return problem(ErrorCode::Validation, QStringLiteral("Invalid effect kind"), QStringLiteral("effects.kind"));
    QJsonObject json;
    if (kind == QLatin1String("drop-shadow")) {
        for (auto it = values.cbegin(); it != values.cend(); ++it)
            if (it.key() != QLatin1String("offsetX") && it.key() != QLatin1String("offsetY")
                && it.key() != QLatin1String("blur") && it.key() != QLatin1String("color"))
                return unknownKey(kind, it.key());
        const auto dx = number(values, "offsetX", 0, -MaximumShadowOffset, MaximumShadowOffset);
        const auto dy = number(values, "offsetY", 4, -MaximumShadowOffset, MaximumShadowOffset);
        const auto blur = number(values, "blur", 4, 0, MaximumBlur);
        QColor color(0, 0, 0, 128);
        if (values.contains(QStringLiteral("color"))) {
            const QVariant value = values.value(QStringLiteral("color"));
            color = value.typeId() == QMetaType::QColor ? value.value<QColor>()
                : value.typeId() == QMetaType::QString ? QColor::fromString(value.toString()) : QColor();
        }
        if (!dx || !dy || !blur || !color.isValid())
            return problem(ErrorCode::Validation,
                           QStringLiteral("A drop shadow needs offsets within ±1000, a blur from 0 to 250 and a colour"),
                           QStringLiteral("effects.parameters"));
        json = QJsonObject{{QStringLiteral("offsetX"), *dx}, {QStringLiteral("offsetY"), *dy},
                           {QStringLiteral("blur"), *blur},
                           {QStringLiteral("color"), color.name(QColor::HexArgb)}};
    } else if (kind == QLatin1String("gaussian-blur")) {
        for (auto it = values.cbegin(); it != values.cend(); ++it)
            if (it.key() != QLatin1String("radius")) return unknownKey(kind, it.key());
        const auto radius = number(values, "radius", 2, 0, MaximumBlur);
        if (!radius)
            return problem(ErrorCode::Validation, QStringLiteral("A blur radius must be from 0 to 250"),
                           QStringLiteral("effects.parameters"));
        json = QJsonObject{{QStringLiteral("radius"), *radius}};
    } else if (kind == QLatin1String("color-adjust")) {
        for (auto it = values.cbegin(); it != values.cend(); ++it)
            if (it.key() != QLatin1String("brightness") && it.key() != QLatin1String("contrast")
                && it.key() != QLatin1String("saturation"))
                return unknownKey(kind, it.key());
        const auto brightness = number(values, "brightness", 0, -1, 1);
        const auto contrast = number(values, "contrast", 1, 0, MaximumContrast);
        const auto saturation = number(values, "saturation", 1, 0, 1);
        if (!brightness || !contrast || !saturation)
            return problem(ErrorCode::Validation,
                           QStringLiteral("A colour adjustment needs brightness from -1 to 1, contrast from 0 to 4 "
                                          "and saturation from 0 to 1"),
                           QStringLiteral("effects.parameters"));
        json = QJsonObject{{QStringLiteral("brightness"), *brightness}, {QStringLiteral("contrast"), *contrast},
                           {QStringLiteral("saturation"), *saturation}};
    } else {
        json = QJsonObject::fromVariantMap(values);
    }
    *out = canonicalJson(json);
    if (out->size() > MaximumParameterBytes)
        return problem(ErrorCode::Unsupported, QStringLiteral("Effect parameters are too large"),
                       QStringLiteral("effects.parameters"));
    return {};
}

QVariantMap parameterMap(const CanvasEffect &effect)
{
    return parameters(effect).toVariantMap();
}

Shadow shadow(const CanvasEffect &effect)
{
    const QJsonObject json = parameters(effect);
    return {QPointF(json.value(QStringLiteral("offsetX")).toDouble(),
                    json.value(QStringLiteral("offsetY")).toDouble()),
            json.value(QStringLiteral("blur")).toDouble(),
            QColor::fromString(json.value(QStringLiteral("color")).toString())};
}

qreal blurRadius(const CanvasEffect &effect)
{
    return parameters(effect).value(QStringLiteral("radius")).toDouble();
}

ColorAdjust colorAdjust(const CanvasEffect &effect)
{
    const QJsonObject json = parameters(effect);
    return {json.value(QStringLiteral("brightness")).toDouble(), json.value(QStringLiteral("contrast")).toDouble(),
            json.value(QStringLiteral("saturation")).toDouble()};
}

OperationError validate(const CanvasObject &o)
{
    const CanvasAppearance &a = *o.appearance;
    const bool closedVector = o.shape != CanvasShape::Image && o.closesArea();
    if (quint8(a.strokeAlignment) > quint8(CanvasStrokeAlignment::Outside))
        return problem(ErrorCode::Validation, QStringLiteral("Invalid stroke alignment"), QStringLiteral("strokeAlignment"));
    if (a.strokeAlignment != CanvasStrokeAlignment::Center && !closedVector)
        return problem(ErrorCode::Unsupported, QStringLiteral("Inside and outside strokes need a closed vector shape"),
                       QStringLiteral("strokeAlignment"));
    if (!std::isfinite(a.miterLimit) || a.miterLimit < 1 || a.miterLimit > 100)
        return problem(ErrorCode::Validation, QStringLiteral("The miter limit must be from 1 to 100"), QStringLiteral("miterLimit"));
    if (!std::isfinite(a.opacity) || a.opacity < 0 || a.opacity > 1)
        return problem(ErrorCode::Validation, QStringLiteral("Opacity must be from 0 to 1"), QStringLiteral("opacity"));
    if (quint8(a.blend) > quint8(CanvasBlendMode::Exclusion))
        return problem(ErrorCode::Validation, QStringLiteral("Invalid blend mode"), QStringLiteral("blend"));
    const CanvasPattern &p = a.pattern;
    if (quint8(p.kind) > quint8(CanvasPatternKind::Checker))
        return problem(ErrorCode::Validation, QStringLiteral("Invalid pattern kind"), QStringLiteral("pattern"));
    if (p.kind == CanvasPatternKind::None) {
        if (!(p == CanvasPattern{}))
            return problem(ErrorCode::Validation, QStringLiteral("A cleared pattern keeps no settings"), QStringLiteral("pattern"));
    } else {
        if (!closedVector)
            return problem(ErrorCode::Unsupported, QStringLiteral("Pattern fills need a closed vector shape"), QStringLiteral("pattern"));
        if (!p.ink.isValid() || !validColor(p.ink))
            return problem(ErrorCode::Validation, QStringLiteral("Pattern ink must be a valid colour"), QStringLiteral("pattern.ink"));
        if (!std::isfinite(p.spacing) || p.spacing < MinimumPatternSpacing || p.spacing > MaximumPatternSpacing
            || !std::isfinite(p.weight) || p.weight <= 0 || p.weight > p.spacing)
            return problem(ErrorCode::Validation,
                           QStringLiteral("Pattern spacing must be from 0.05 to 10000 and its weight positive and at most the spacing"),
                           QStringLiteral("pattern.spacing"));
        if (!std::isfinite(p.angle) || p.angle < -360 || p.angle > 360)
            return problem(ErrorCode::Validation, QStringLiteral("Pattern angle must be from -360 to 360 degrees"), QStringLiteral("pattern.angle"));
    }
    if (a.effects.size() > MaximumEffects)
        return problem(ErrorCode::Unsupported, QStringLiteral("Too many appearance effects"), QStringLiteral("effects"));
    QSet<QString> ids;
    for (const CanvasEffect &effect : a.effects) {
        if (!token(effect.id) || ids.contains(effect.id))
            return problem(ErrorCode::Validation, QStringLiteral("Effect ids must be unique short tokens"), QStringLiteral("effects.id"));
        ids.insert(effect.id);
        if (!token(effect.kind))
            return problem(ErrorCode::Validation, QStringLiteral("Invalid effect kind"), QStringLiteral("effects.kind"));
        if (effect.parameters.size() > MaximumParameterBytes)
            return problem(ErrorCode::Unsupported, QStringLiteral("Effect parameters are too large"), QStringLiteral("effects.parameters"));
        QJsonParseError error{};
        const QJsonDocument json = QJsonDocument::fromJson(effect.parameters, &error);
        if (error.error != QJsonParseError::NoError || !json.isObject()
            || canonicalJson(json.object()) != effect.parameters)
            return problem(ErrorCode::Validation, QStringLiteral("Effect parameters must be canonical JSON objects"),
                           QStringLiteral("effects.parameters"));
        if (supportedEffect(effect.kind)) {
            QByteArray canonical;
            if (const OperationError e = canonicalParameters(effect.kind, json.object().toVariantMap(), &canonical))
                return e;
            if (canonical != effect.parameters)
                return problem(ErrorCode::Validation, QStringLiteral("Effect parameters must list every value of their kind"),
                               QStringLiteral("effects.parameters"));
        }
    }
    return {};
}

qreal strokeReach(const CanvasObject &o)
{
    if (!o.hasStroke()) return 0;
    const qreal width = std::max(o.strokeWidth, 0.0);
    return o.appearance->strokeAlignment == CanvasStrokeAlignment::Center ? width / 2 : width;
}

qreal strokeOutset(const CanvasObject &o)
{
    if (!o.hasStroke() || o.appearance->strokeAlignment == CanvasStrokeAlignment::Inside) return 0;
    // A square cap's corner lies sqrt(2) half-pens from the path's end.
    constexpr qreal Diagonal = 1.4142135623730951;
    bool openEnds = o.shape == CanvasShape::Polyline;
    if (o.shape == CanvasShape::Path) {
        openEnds = !o.pathClosed;
        for (const CanvasPathContour &c : o.extraContours) openEnds = openEnds || !c.closed;
    }
    return strokeReach(o) * (o.strokeCap == Qt::SquareCap && openEnds ? Diagonal : 1);
}

std::optional<QRectF> miterTips(const CanvasObject &o)
{
    if (!o.hasStroke() || o.strokeJoin != Qt::MiterJoin
        || o.appearance->strokeAlignment == CanvasStrokeAlignment::Inside) return std::nullopt;
    QVector<CanvasPathContour> contours;
    if (o.shape == CanvasShape::Path) {
        contours.append(CanvasPathContour{o.pathNodes, o.pathClosed});
        contours += o.extraContours;
    } else if (o.shape == CanvasShape::Polyline) {
        CanvasPathContour line;
        line.closed = false;
        for (const QPointF &p : o.points) line.nodes.append({p, p, p});
        contours.append(line);
    } else if (o.shape == CanvasShape::Rect || o.shape == CanvasShape::Image) {
        const QPointF p = o.rect.topLeft(), q = o.rect.bottomRight(), r(q.x(), p.y()), t(p.x(), q.y());
        contours.append(CanvasPathContour{{{p, p, p}, {r, r, r}, {q, q, q}, {t, t, t}}, true});
    }
    const qreal limit = std::max(o.appearance->miterLimit, 1.0);
    const qreal reach = strokeReach(o);
    const auto unit = [](QPointF v) {
        const qreal length = std::hypot(v.x(), v.y());
        return length > 0 ? v / length : QPointF();
    };
    qreal left = qInf(), top = qInf(), right = -qInf(), bottom = -qInf();
    for (const CanvasPathContour &c : contours) {
        const qsizetype n = c.nodes.size();
        if (n < 2) continue;
        const auto at = [&](qsizetype i) { return c.nodes.at((i + n) % n); };
        for (qsizetype i = c.closed ? 0 : 1; i < (c.closed ? n : n - 1); ++i) {
            const CanvasPathNode p = at(i - 1), node = at(i), q = at(i + 1);
            // Tangents of the cubics ending and starting here, in document
            // space (a non-uniform transform changes the angle).
            const QPointF a = o.transform.map(node.anchor);
            const QPointF in = unit(a - o.transform.map(node.in != node.anchor ? node.in
                                                        : p.out != p.anchor ? p.out : p.anchor));
            const QPointF out = unit(o.transform.map(node.out != node.anchor ? node.out
                                                     : q.in != q.anchor ? q.in : q.anchor) - a);
            if (in.isNull() || out.isNull()) continue;
            const qreal cosHalf = std::sqrt((1 + std::clamp(QPointF::dotProduct(in, out), -1.0, 1.0)) / 2);
            // The painter bevels past the limit; the RHI tessellator clips
            // its spike at the limit (canvas_tessellator.cpp), so bound both.
            const qreal ratio = cosHalf > 0 ? std::min(1 / cosHalf, limit) : limit;
            const QPointF tip = a + unit(in - out) * (reach * ratio);
            left = std::min(left, tip.x());
            right = std::max(right, tip.x());
            top = std::min(top, tip.y());
            bottom = std::max(bottom, tip.y());
        }
    }
    if (right < left) return std::nullopt;
    return QRectF(QPointF(left, top), QPointF(right, bottom));
}

QMarginsF effectMargins(const CanvasObject &o)
{
    qreal left = 0, top = 0, right = 0, bottom = 0;
    for (const CanvasEffect &effect : o.appearance->effects) {
        if (!effect.enabled) continue;
        if (effect.kind == QLatin1String("drop-shadow")) {
            // The shadow is everything painted so far, shifted and blurred.
            const Shadow s = shadow(effect);
            const qreal spread = 3 * s.blur;
            left = std::max(left, left - s.offset.x() + spread);
            right = std::max(right, right + s.offset.x() + spread);
            top = std::max(top, top - s.offset.y() + spread);
            bottom = std::max(bottom, bottom + s.offset.y() + spread);
        } else if (effect.kind == QLatin1String("gaussian-blur")) {
            const qreal spread = 3 * blurRadius(effect);
            left += spread;
            top += spread;
            right += spread;
            bottom += spread;
        }
    }
    return {left, top, right, bottom};
}

qreal effectInputReach(const CanvasObject &o)
{
    qreal reach = 0;
    for (const CanvasEffect &effect : o.appearance->effects) {
        if (!effect.enabled) continue;
        if (effect.kind == QLatin1String("drop-shadow")) {
            const Shadow s = shadow(effect);
            reach += std::max(std::abs(s.offset.x()), std::abs(s.offset.y())) + 3 * s.blur;
        } else if (effect.kind == QLatin1String("gaussian-blur")) {
            reach += 3 * blurRadius(effect);
        }
    }
    return reach;
}

bool hasRenderedEffects(const CanvasObject &o)
{
    return std::any_of(o.appearance->effects.cbegin(), o.appearance->effects.cend(),
                       [](const CanvasEffect &e) { return e.enabled && supportedEffect(e.kind); });
}

bool needsLayer(const CanvasObject &o)
{
    const CanvasAppearance &a = *o.appearance;
    return a.opacity < 1 || a.blend != CanvasBlendMode::Normal || hasRenderedEffects(o)
        || (o.hasStroke() && a.strokeAlignment != CanvasStrokeAlignment::Center);
}

} // namespace QindaTK::CanvasAppearanceRules
