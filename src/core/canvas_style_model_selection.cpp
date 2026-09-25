// SPDX-License-Identifier: LGPL-3.0-or-later
// CanvasStyleModel's read side: the selection's shared style, published in the
// shapes StrokeStyleEditor and FillStyleEditor bind to, and panel requests
// turned into edits of only the fields the user changed.
#include "canvas_style_model.h"
#include "canvas_appearance.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <cmath>

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
    const CanvasAppearance &a = *o.appearance;
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
            {QStringLiteral("fillKind"), a.pattern.kind != CanvasPatternKind::None ? QStringLiteral("pattern")
                 : managedKind(o) == QLatin1String("gradient") ? QStringLiteral("gradient")
                 : o.fill.isValid() && o.fill.alpha() > 0 ? QStringLiteral("solid") : QStringLiteral("none")},
            // A digest, so two different gradients read as mixed.
            {QStringLiteral("gradient"), managedKind(o) == QLatin1String("gradient")
                 ? QVariant(QString::fromLatin1(QCryptographicHash::hash(o.managedFill, QCryptographicHash::Sha1).toHex()))
                 : QVariant()},
            {QStringLiteral("effectCount"), int(a.effects.size())}};
}

// Null values from QML (null, undefined) and an absent value are the same.
bool same(const QVariant &a, const QVariant &b)
{
    if (a.isNull() || b.isNull()) return a.isNull() == b.isNull();
    return a == b;
}

} // namespace

// Mixed style keys under the descriptor field names a panel uses.
QStringList CanvasStyleModelMixed::fields(const QVariantMap &style,
                                          std::initializer_list<std::pair<const char *, const char *>> keys)
{
    const QStringList mixed = style.value(QStringLiteral("mixed")).toStringList();
    QStringList result;
    for (const auto &[key, field] : keys)
        if (mixed.contains(QLatin1String(key))) result.append(QLatin1String(field));
    return result;
}


QVariantList CanvasStyleModel::patterns() const
{
    return CanvasAppearanceRules::patternSwatches();
}

QStringList CanvasStyleModel::blendModes() const
{
    return CanvasAppearanceRules::blendNames();
}

void CanvasStyleModel::adoptSelection(const QVariantList &ids)
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

QVector<CanvasObjectId> CanvasStyleModel::selection() const
{
    QVector<CanvasObjectId> ids;
    for (const QVariant &value : m_selectedIds) {
        bool ok = false;
        const CanvasObjectId id = value.toString().toULongLong(&ok);
        if (ok && id && m_document && m_document->object(id) && !ids.contains(id)) ids.append(id);
    }
    return ids;
}

void CanvasStyleModel::refresh()
{
    observe(selection());
    syncGradientEditor();
    const QVariantMap style = styleOf(m_selectedIds);
    const QVariantList dash = style.value(QStringLiteral("dash")).toList();
    QString mode = dash.isEmpty() ? QStringLiteral("solid")
        : dash == QVariantList{6.0, 3.0} ? QStringLiteral("dash")
        : dash == QVariantList{1.0, 2.0} ? QStringLiteral("dot") : QStringLiteral("custom");
    if (dash.isEmpty() && !m_dashMode.isEmpty()) mode = m_dashMode;
    const QStringList strokeMixed = CanvasStyleModelMixed::fields(style, {
        {"stroke", "color"}, {"strokeWidth", "width"}, {"alignment", "alignment"}, {"cap", "cap"},
        {"join", "join"}, {"miterLimit", "miterLimit"}, {"dash", "dash"}});
    // The width in the chosen unit; the value last typed in this unit is
    // shown exactly while the document still holds what it produced.
    const double documentWidth = style.value(QStringLiteral("strokeWidth"), 0.0).toDouble();
    double width = documentWidth / documentUnitsPer(m_strokeUnit);
    if (m_widthIntent.unit == m_strokeUnit && m_widthIntent.documentWidth == documentWidth
        && !strokeMixed.contains(QStringLiteral("width")))
        width = m_widthIntent.value;
    const QVariantMap stroke{
        {QStringLiteral("width"), width},
        {QStringLiteral("unit"), m_strokeUnit},
        {QStringLiteral("color"), style.value(QStringLiteral("stroke"))},
        {QStringLiteral("alignment"), style.value(QStringLiteral("alignment"), QStringLiteral("center"))},
        {QStringLiteral("cap"), style.value(QStringLiteral("cap"), QStringLiteral("butt"))},
        {QStringLiteral("join"), style.value(QStringLiteral("join"), QStringLiteral("miter"))},
        {QStringLiteral("miterLimit"), style.value(QStringLiteral("miterLimit"), 4.0)},
        {QStringLiteral("dash"), dash},
        {QStringLiteral("dashMode"), mode},
        {QStringLiteral("mixed"), strokeMixed}};
    const QVariantMap fill = fillDescriptor(style);
    if (style == m_selectionStyle && stroke == m_strokeStyle && fill == m_fillStyle) return;
    m_selectionStyle = style;
    m_strokeStyle = stroke;
    m_fillStyle = fill;
    emit selectionStyleChanged();
}

bool CanvasStyleModel::applyStrokeStyle(const QVariantMap &next)
{
    const bool listed = next.contains(QStringLiteral("changed"));
    const QStringList changed = next.value(QStringLiteral("changed")).toStringList();
    // A listed field was set by the user: it applies even when it equals
    // the published (first object's) value of a mixed key.
    const auto wants = [&](const QString &field) {
        if (!next.contains(field)) return false;
        return listed ? changed.contains(field) : !same(next.value(field), m_strokeStyle.value(field));
    };
    bool presentation = false;
    const QString unit = next.value(QStringLiteral("unit"), m_strokeUnit).toString();
    if (unit != m_strokeUnit && (!listed || changed.contains(QStringLiteral("unit")) || wants(QStringLiteral("width")))) {
        if (documentUnitsPer(unit) <= 0) return fail(tr("Unsupported stroke unit: %1").arg(unit));
        m_strokeUnit = unit;
        presentation = true;
    }
    QVariantMap style;
    const auto take = [&](const char *field, const char *key) {
        if (wants(QLatin1String(field))) style.insert(QLatin1String(key), next.value(QLatin1String(field)));
    };
    take("color", "stroke");
    take("alignment", "alignment");
    take("cap", "cap");
    take("join", "join");
    take("miterLimit", "miterLimit");
    take("dash", "dash");
    if (wants(QStringLiteral("width"))) {
        bool ok = false;
        const double typed = next.value(QStringLiteral("width")).toDouble(&ok);
        if (!ok || !std::isfinite(typed)) return fail(tr("Invalid stroke width"));
        const double documentWidth = typed * documentUnitsPer(m_strokeUnit);
        style.insert(QStringLiteral("strokeWidth"), documentWidth);
        m_widthIntent = {m_strokeUnit, typed, documentWidth};
    }
    // "custom" with no values yet is presentation state: keep offering the
    // pattern field until a custom dash is entered.
    const QString mode = next.value(QStringLiteral("dashMode")).toString();
    if (!style.contains(QStringLiteral("dash")) && next.contains(QStringLiteral("dashMode"))
        && mode != m_strokeStyle.value(QStringLiteral("dashMode")).toString()) {
        m_dashMode = mode == QLatin1String("custom") ? mode : QString();
        presentation = true;
    }
    if (style.isEmpty()) {
        if (presentation) refresh();
        return succeed(0);
    }
    m_dashMode.clear();
    const bool ok = apply(m_selectedIds, style);
    if (presentation || !ok) refresh();
    return ok;
}

} // namespace QindaTK
