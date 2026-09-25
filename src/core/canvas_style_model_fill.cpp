// SPDX-License-Identifier: LGPL-3.0-or-later
// CanvasStyleModel's fill side: the FillStyleEditor descriptor, each object's
// recallable per-kind settings, and fill requests (kind changes restore
// what an object had before; explicit choices apply to every object).
#include "canvas_style_model.h"
#include "canvas_appearance.h"
#include "canvas_document_format.h"
#include "canvas_managed_paint.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace QindaTK {
namespace {
constexpr qsizetype RecallPruneSize = 1024;

bool isGradient(const QByteArray &managed)
{
    return !managed.isEmpty()
        && QJsonDocument::fromJson(managed).object().value(QStringLiteral("kind")).toString() == QLatin1String("gradient");
}

QByteArray definitionBytes(const QByteArray &managed)
{
    return QByteArray::fromBase64(
        QJsonDocument::fromJson(managed).object().value(QStringLiteral("definition")).toString().toLatin1());
}

// FillStyleEditor's gradient record: {id, type, stops: [{id, position}]}.
QVariant gradientSummary(const QByteArray &managed)
{
    if (!isGradient(managed)) return {};
    const auto definition = GradientDefinitionCodec::decode(definitionBytes(managed));
    if (!definition) return {};
    QVariantList stops;
    for (const GradientStop &stop : definition->stops)
        stops.append(QVariantMap{{QStringLiteral("id"), stop.id}, {QStringLiteral("position"), stop.position}});
    return QVariantMap{{QStringLiteral("id"), definition->id}, {QStringLiteral("type"), QStringLiteral("linear")},
                       {QStringLiteral("stops"), stops}};
}

QString firstSwatch()
{
    return CanvasAppearanceRules::patternSwatches().constFirst().toMap().value(QStringLiteral("id")).toString();
}
} // namespace

void CanvasStyleModel::observe(const QVector<CanvasObjectId> &ids)
{
    if (!m_document) return;
    if (m_recall.size() > RecallPruneSize)
        m_recall.removeIf([this](const auto &entry) { return !m_document->object(entry.key()); });
    for (CanvasObjectId id : ids) {
        const CanvasObject *o = m_document->object(id);
        if (!o) continue;
        FillRecall &r = m_recall[id];
        const CanvasPattern &pattern = o->appearance->pattern;
        if (pattern.kind != CanvasPatternKind::None) {
            r.hasPattern = true;
            r.pattern = pattern;
            r.patternFill = o->fill;
        }
        if (isGradient(o->managedFill)) {
            r.gradient = o->managedFill;
            r.gradientPreview = o->fill;
        } else if (pattern.kind == CanvasPatternKind::None && o->fill.isValid() && o->fill.alpha() > 0) {
            r.solid = o->fill;
            r.solidManaged = o->managedFill;
        }
    }
}

void CanvasStyleModel::syncGradientEditor()
{
    const QVector<CanvasObjectId> ids = selection();
    const CanvasObject *key = ids.isEmpty() ? nullptr : m_document->object(ids.constFirst());
    if (!m_gradientEditor || !key) return;
    const QByteArray managed = isGradient(key->managedFill) ? key->managedFill : m_recall.value(key->id).gradient;
    if (managed.isEmpty()) return;
    const QByteArray bytes = definitionBytes(managed);
    if (bytes != m_gradientEditor->encode()) m_gradientEditor->decode(bytes);
}

QVariantMap CanvasStyleModel::fillDescriptor(const QVariantMap &style) const
{
    const QVector<CanvasObjectId> ids = selection();
    const CanvasObject *key = ids.isEmpty() ? nullptr : m_document->object(ids.constFirst());
    const FillRecall recall = key ? m_recall.value(key->id) : FillRecall{};
    const QString kind = style.value(QStringLiteral("fillKind"), QStringLiteral("none")).toString();
    // Each kind's settings show what choosing that kind brings back.
    QVariant solid = style.value(QStringLiteral("fill"));
    if (kind != QLatin1String("solid") && recall.solid.isValid()) solid = recall.solid;
    const QVariantMap pattern = style.value(QStringLiteral("pattern")).toMap();
    QString patternId = pattern.value(QStringLiteral("swatch")).toString();
    bool custom = !pattern.isEmpty() && patternId.isEmpty();
    if (pattern.isEmpty()) {
        patternId = recall.hasPattern ? CanvasAppearanceRules::swatchId(recall.pattern) : firstSwatch();
        custom = recall.hasPattern && patternId.isEmpty();
    }
    const QVariant gradient = gradientSummary(key && isGradient(key->managedFill) ? key->managedFill : recall.gradient);
    return {{QStringLiteral("kind"), kind},
            {QStringLiteral("settings"), QVariantMap{
                {QStringLiteral("solid"), QVariantMap{{QStringLiteral("color"), solid}}},
                {QStringLiteral("gradient"), QVariantMap{{QStringLiteral("gradient"), gradient}}},
                {QStringLiteral("pattern"), QVariantMap{{QStringLiteral("patternId"), patternId},
                                                        {QStringLiteral("custom"), custom}}}}},
            {QStringLiteral("managed"), !style.value(QStringLiteral("managedFill")).toString().isEmpty()},
            {QStringLiteral("mixed"), CanvasStyleModelMixed::fields(style, {
                {"fillKind", "kind"}, {"fill", "color"}, {"pattern", "pattern"}, {"gradient", "gradient"}})}};
}

bool CanvasStyleModel::applyFillKind(const QString &kind, const QVariantMap &settings)
{
    if (!m_document) return fail(tr("No canvas document"));
    const QVector<CanvasObjectId> ids = selection();
    if (ids.isEmpty()) return fail(tr("Select an object first"));
    QByteArray editorGradient;
    QColor editorPreview;
    if (kind == QLatin1String("gradient") && m_gradientEditor) {
        OperationError error;
        editorGradient = CanvasManagedPaint::gradient(m_gradientEditor->definition(), &error);
        if (error) return fail(error.message);
        const QVariantList samples = m_gradientEditor->previewSamples();
        editorPreview = QColor(samples.value(samples.size() / 2).toString());
    }
    CanvasEdit edit;
    edit.label = tr("Change fill kind");
    for (CanvasObjectId id : ids) {
        const CanvasObject *before = m_document->object(id);
        if (!m_document->effectiveVisible(*before) || m_document->effectiveLocked(*before))
            return fail(tr("A selected object is hidden or locked"));
        const FillRecall r = m_recall.value(id);
        CanvasObject after = *before;
        CanvasPattern pattern;
        if (kind == QLatin1String("none")) {
            after.fill = QColor(); // not {}: that picks Qt::color0 (white)
            after.managedFill.clear();
        } else if (kind == QLatin1String("solid")) {
            if (r.solid.isValid()) {
                after.fill = r.solid;
                after.managedFill = r.solidManaged;
            } else {
                const QVariant chosen = settings.value(QStringLiteral("solid")).toMap().value(QStringLiteral("color"));
                // Document data default: a neutral grey rather than a theme role.
                if (!(before->fill.isValid() && before->fill.alpha() > 0))
                    after.fill = chosen.isNull() ? QColor(0x80, 0x80, 0x80) : QColor(chosen.toString());
                after.managedFill.clear();
            }
        } else if (kind == QLatin1String("pattern")) {
            if (r.hasPattern) {
                pattern = r.pattern;
                after.fill = r.patternFill;
                after.managedFill.clear();
            } else {
                const QString swatch = settings.value(QStringLiteral("pattern")).toMap()
                                           .value(QStringLiteral("patternId")).toString();
                const auto chosen = CanvasAppearanceRules::patternSwatch(swatch.isEmpty() ? firstSwatch() : swatch);
                if (!chosen) return fail(tr("Unknown pattern: %1").arg(swatch));
                pattern = *chosen;
                pattern.ink = before->hasStroke() ? before->stroke : QColor(Qt::black);
            }
        } else if (kind == QLatin1String("gradient")) {
            if (before->shape == CanvasShape::Polyline || before->shape == CanvasShape::Image
                || (before->shape == CanvasShape::Path && !before->hasClosedContour()))
                return fail(tr("Managed fills require a closed vector object"));
            after.managedFill = r.gradient.isEmpty() ? editorGradient : r.gradient;
            after.fill = r.gradient.isEmpty() ? editorPreview : r.gradientPreview;
            if (after.managedFill.isEmpty())
                return fail(tr("No gradient to apply: attach a gradient editor or edit one first"));
        } else {
            return fail(tr("Unknown fill kind: %1").arg(kind));
        }
        after.appearance.update([&pattern](CanvasAppearance &a) { a.pattern = pattern; });
        if (const OperationError error = CanvasDocumentFormat::validateObject(after, id, CanvasDocumentLimits{}))
            return fail(error.message);
        if (after != *before) edit.changes.append({id, *before, after});
    }
    if (!edit.isEmpty() && !m_document->commit(edit)) return fail(tr("Document changed before the fill could commit"));
    return succeed(int(edit.changes.size()));
}

bool CanvasStyleModel::applyFillStyle(const QVariantMap &next)
{
    const QString kind = next.value(QStringLiteral("kind")).toString();
    const QVariantMap settings = next.value(QStringLiteral("settings")).toMap();
    const QVariantMap current = m_fillStyle.value(QStringLiteral("settings")).toMap();
    const bool listed = next.contains(QStringLiteral("changed"));
    const QStringList changed = next.value(QStringLiteral("changed")).toStringList();
    const QStringList mixed = m_fillStyle.value(QStringLiteral("mixed")).toStringList();
    const bool kindChange = kind != m_fillStyle.value(QStringLiteral("kind")).toString()
        || mixed.contains(QStringLiteral("kind")) || (listed && changed.contains(QStringLiteral("kind")));
    const auto setting = [](const QVariantMap &map, const char *kindKey, const char *field) {
        return map.value(QLatin1String(kindKey)).toMap().value(QLatin1String(field));
    };
    // A value the user set (listed, or different from the published one).
    const auto chose = [&](const char *kindKey, const char *field) {
        const QVariant value = setting(settings, kindKey, field);
        if (listed) return changed.contains(QLatin1String(field));
        const QVariant was = setting(current, kindKey, field);
        return value.isNull() != was.isNull() || (!value.isNull() && value != was);
    };
    if (kind == QLatin1String("none"))
        return kindChange ? applyFillKind(kind, settings) : succeed(0);
    if (kind == QLatin1String("solid")) {
        if (chose("solid", "color")) {
            QVariant color = setting(settings, "solid", "color");
            if (color.isNull()) color = QColor(0x80, 0x80, 0x80);
            return apply(m_selectedIds, {{QStringLiteral("fill"), color}, {QStringLiteral("pattern"), QVariant()}});
        }
        return kindChange ? applyFillKind(kind, settings) : succeed(0);
    }
    if (kind == QLatin1String("pattern")) {
        if (chose("pattern", "patternId"))
            return apply(m_selectedIds, {{QStringLiteral("pattern"), setting(settings, "pattern", "patternId")}});
        return kindChange ? applyFillKind(kind, settings) : succeed(0);
    }
    if (kind == QLatin1String("gradient")) {
        if (listed && changed.contains(QStringLiteral("gradient"))) return applyGradient(m_selectedIds, m_gradientEditor);
        return kindChange ? applyFillKind(kind, settings) : succeed(0);
    }
    return fail(tr("Unknown fill kind: %1").arg(kind));
}

} // namespace QindaTK
