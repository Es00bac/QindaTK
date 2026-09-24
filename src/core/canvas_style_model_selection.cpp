// SPDX-License-Identifier: LGPL-3.0-or-later
// CanvasStyleModel's read side: the selection's shared style, published in the
// shapes StrokeStyleEditor and FillStyleEditor bind to, and panel requests
// turned into edits of only the fields the user changed.
#include "canvas_style_model.h"
#include "canvas_appearance.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace QindaTK {
namespace {
QVariant colorValue(const QColor &color)
{
    return color.isValid() && color.alpha() > 0 ? QVariant(color) : QVariant();
}

QString managedKind(const CanvasObject &o)
{
    if (o.managedFill.isEmpty()) return {};
    return QJsonDocument::fromJson(o.managedFill).object().value(QStringLiteral("kind")).toString();
}

QVariant patternValue(const CanvasPattern &p)
{
    if (p.kind == CanvasPatternKind::None) return {};
    return QVariantMap{{QStringLiteral("kind"), CanvasAppearanceRules::patternName(p.kind)},
                       {QStringLiteral("ink"), colorValue(p.ink)},
                       {QStringLiteral("spacing"), p.spacing},
                       {QStringLiteral("angle"), p.angle},
                       {QStringLiteral("weight"), p.weight},
                       {QStringLiteral("swatch"), CanvasAppearanceRules::swatchId(p)}};
}

QVariantMap describe(const CanvasObject &o)
{
    QVariantList dash;
    for (qreal length : o.dashPattern) dash.append(length);
    const CanvasAppearance &a = o.appearance;
    return {{QStringLiteral("fill"), colorValue(o.fill)},
            {QStringLiteral("stroke"), colorValue(o.stroke)},
            {QStringLiteral("strokeWidth"), o.strokeWidth},
            {QStringLiteral("dash"), dash},
            {QStringLiteral("cap"), o.strokeCap == Qt::RoundCap ? QStringLiteral("round")
                 : o.strokeCap == Qt::SquareCap ? QStringLiteral("square") : QStringLiteral("butt")},
            {QStringLiteral("join"), o.strokeJoin == Qt::RoundJoin ? QStringLiteral("round")
                 : o.strokeJoin == Qt::BevelJoin ? QStringLiteral("bevel") : QStringLiteral("miter")},
            {QStringLiteral("fillRule"), o.fillRule == Qt::WindingFill ? QStringLiteral("nonzero")
                                                                       : QStringLiteral("evenodd")},
            {QStringLiteral("alignment"), CanvasAppearanceRules::alignmentName(a.strokeAlignment)},
            {QStringLiteral("miterLimit"), a.miterLimit},
            {QStringLiteral("opacity"), a.opacity},
            {QStringLiteral("blend"), CanvasAppearanceRules::blendName(a.blend)},
            {QStringLiteral("pattern"), patternValue(a.pattern)},
            {QStringLiteral("managedFill"), managedKind(o)},
            {QStringLiteral("effectCount"), int(a.effects.size())}};
}

// Null values from QML (null, undefined) and an absent value are the same.
bool same(const QVariant &a, const QVariant &b)
{
    if (a.isNull() || b.isNull()) return a.isNull() == b.isNull();
    return a == b;
}

QStringList mixedOf(const QVariantMap &style, std::initializer_list<const char *> keys)
{
    const QStringList mixed = style.value(QStringLiteral("mixed")).toStringList();
    QStringList result;
    for (const char *key : keys)
        if (mixed.contains(QLatin1String(key))) result.append(QLatin1String(key));
    return result;
}
} // namespace

QVariantList CanvasStyleModel::patterns() const
{
    return CanvasAppearanceRules::patternSwatches();
}

QStringList CanvasStyleModel::blendModes() const
{
    return CanvasAppearanceRules::blendNames();
}

void CanvasStyleModel::setSelectedIds(const QVariantList &ids)
{
    if (m_selectedIds == ids) return;
    m_selectedIds = ids;
    m_dashMode.clear();
    emit selectedIdsChanged();
    refresh();
}

QVariantMap CanvasStyleModel::styleOf(const QVariantList &ids) const
{
    QVariantMap style;
    QStringList mixed;
    int count = 0;
    QSet<CanvasObjectId> seen;
    for (const QVariant &value : ids) {
        bool ok = false;
        const CanvasObjectId id = value.toString().toULongLong(&ok);
        const CanvasObject *o = ok && m_document && !seen.contains(id) ? m_document->object(id) : nullptr;
        if (!o) continue;
        seen.insert(id);
        const QVariantMap current = describe(*o);
        if (count++ == 0) {
            style = current;
            continue;
        }
        for (auto it = current.cbegin(); it != current.cend(); ++it)
            if (!same(style.value(it.key()), it.value()) && !mixed.contains(it.key())) mixed.append(it.key());
    }
    style.insert(QStringLiteral("count"), count);
    style.insert(QStringLiteral("mixed"), mixed);
    return style;
}

void CanvasStyleModel::refresh()
{
    const QVariantMap style = styleOf(m_selectedIds);
    const QVariantList dash = style.value(QStringLiteral("dash")).toList();
    QString mode = dash.isEmpty() ? QStringLiteral("solid")
        : dash == QVariantList{6.0, 3.0} ? QStringLiteral("dash")
        : dash == QVariantList{1.0, 2.0} ? QStringLiteral("dot") : QStringLiteral("custom");
    if (dash.isEmpty() && !m_dashMode.isEmpty()) mode = m_dashMode;
    const QVariantMap stroke{
        {QStringLiteral("width"), style.value(QStringLiteral("strokeWidth"), 0.0)},
        {QStringLiteral("unit"), QStringLiteral("document")},
        {QStringLiteral("color"), style.value(QStringLiteral("stroke"))},
        {QStringLiteral("alignment"), style.value(QStringLiteral("alignment"), QStringLiteral("center"))},
        {QStringLiteral("cap"), style.value(QStringLiteral("cap"), QStringLiteral("butt"))},
        {QStringLiteral("join"), style.value(QStringLiteral("join"), QStringLiteral("miter"))},
        {QStringLiteral("miterLimit"), style.value(QStringLiteral("miterLimit"), 4.0)},
        {QStringLiteral("dash"), dash},
        {QStringLiteral("dashMode"), mode},
        {QStringLiteral("mixed"), mixedOf(style, {"stroke", "strokeWidth", "alignment", "cap", "join",
                                                  "miterLimit", "dash"})}};
    const QVariantMap pattern = style.value(QStringLiteral("pattern")).toMap();
    const QString managed = style.value(QStringLiteral("managedFill")).toString();
    const QString kind = !pattern.isEmpty() ? QStringLiteral("pattern")
        : managed == QLatin1String("gradient") ? QStringLiteral("gradient")
        : !style.value(QStringLiteral("fill")).isNull() || !managed.isEmpty() ? QStringLiteral("solid")
        : QStringLiteral("none");
    // An unset or custom pattern suggests the first swatch when the kind changes.
    QString patternId = pattern.value(QStringLiteral("swatch")).toString();
    if (pattern.isEmpty()) patternId = CanvasAppearanceRules::patternSwatches().constFirst().toMap()
                                           .value(QStringLiteral("id")).toString();
    const QVariantMap fill{
        {QStringLiteral("kind"), kind},
        {QStringLiteral("settings"), QVariantMap{
            {QStringLiteral("solid"), QVariantMap{{QStringLiteral("color"), style.value(QStringLiteral("fill"))}}},
            {QStringLiteral("gradient"), QVariantMap{{QStringLiteral("gradient"), QVariant()}}},
            {QStringLiteral("pattern"), QVariantMap{{QStringLiteral("patternId"), patternId}}}}},
        {QStringLiteral("managed"), !managed.isEmpty()},
        {QStringLiteral("mixed"), mixedOf(style, {"fill", "pattern", "managedFill"})}};
    if (style == m_selectionStyle && stroke == m_strokeStyle && fill == m_fillStyle) return;
    m_selectionStyle = style;
    m_strokeStyle = stroke;
    m_fillStyle = fill;
    emit selectionStyleChanged();
}

bool CanvasStyleModel::applyStrokeStyle(const QVariantMap &next)
{
    QVariantMap style;
    const auto take = [&](const char *field, const char *key) {
        const QString name = QLatin1String(field);
        if (next.contains(name) && !same(next.value(name), m_strokeStyle.value(name)))
            style.insert(QLatin1String(key), next.value(name));
    };
    take("color", "stroke");
    take("width", "strokeWidth");
    take("alignment", "alignment");
    take("cap", "cap");
    take("join", "join");
    take("miterLimit", "miterLimit");
    take("dash", "dash");
    // "custom" with no values yet is presentation state: keep offering the
    // pattern field until a custom dash is entered.
    const QString mode = next.value(QStringLiteral("dashMode")).toString();
    if (!style.contains(QStringLiteral("dash")) && mode != m_strokeStyle.value(QStringLiteral("dashMode")).toString()) {
        m_dashMode = mode == QLatin1String("custom") ? mode : QString();
        refresh();
    }
    if (style.isEmpty()) return succeed(0);
    m_dashMode.clear();
    return apply(m_selectedIds, style);
}

bool CanvasStyleModel::applyFillStyle(const QVariantMap &next)
{
    const QString kind = next.value(QStringLiteral("kind")).toString();
    const QVariantMap settings = next.value(QStringLiteral("settings")).toMap();
    const QVariantMap current = m_fillStyle.value(QStringLiteral("settings")).toMap();
    const bool sameKind = kind == m_fillStyle.value(QStringLiteral("kind")).toString();
    QVariantMap style;
    if (kind == QLatin1String("none")) {
        if (!sameKind) style = {{QStringLiteral("fill"), QVariant()}, {QStringLiteral("pattern"), QVariant()}};
    } else if (kind == QLatin1String("solid")) {
        QVariant color = settings.value(QStringLiteral("solid")).toMap().value(QStringLiteral("color"));
        const QVariant was = current.value(QStringLiteral("solid")).toMap().value(QStringLiteral("color"));
        if (!sameKind || !same(color, was)) {
            // Document data default: a neutral grey rather than a theme role.
            if (color.isNull()) color = QColor(0x80, 0x80, 0x80);
            style = {{QStringLiteral("fill"), color}, {QStringLiteral("pattern"), QVariant()}};
        }
    } else if (kind == QLatin1String("pattern")) {
        const QString id = settings.value(QStringLiteral("pattern")).toMap().value(QStringLiteral("patternId")).toString();
        const QString was = current.value(QStringLiteral("pattern")).toMap().value(QStringLiteral("patternId")).toString();
        if (!sameKind || id != was) style = {{QStringLiteral("pattern"), id}};
    } else {
        return fail(tr("Gradient fills are applied with applyGradient"));
    }
    if (style.isEmpty()) return succeed(0);
    return apply(m_selectedIds, style);
}

} // namespace QindaTK
