// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_style_model.h"
#include "canvas_appearance.h"
#include "canvas_document_format.h"
#include "canvas_managed_paint.h"

#include <QSet>
#include <cmath>
#include <optional>

namespace QindaTK {
namespace {
bool colorFrom(const QVariant &value, QColor &color)
{
    if (value.isNull()) { color = {}; return true; }
    if (value.metaType().id() == QMetaType::QColor) color = value.value<QColor>();
    else if (value.metaType().id() == QMetaType::QString) color = QColor(value.toString());
    else return false;
    return color.isValid();
}

std::optional<double> numberFrom(const QVariant &value, double low, double high)
{
    bool ok = false;
    const double n = value.toDouble(&ok);
    if (!ok || !std::isfinite(n) || n < low || n > high) return std::nullopt;
    return n;
}

// A validated style request, applied to each selected object in turn.
struct StyleRequest {
    std::optional<QColor> fill, stroke;
    std::optional<qreal> width, miterLimit, opacity;
    std::optional<QVector<qreal>> dash;
    std::optional<Qt::PenCapStyle> cap;
    std::optional<Qt::PenJoinStyle> join;
    std::optional<Qt::FillRule> fillRule;
    std::optional<CanvasStrokeAlignment> alignment;
    std::optional<CanvasBlendMode> blend;
    bool patternSet = false;
    std::optional<CanvasPattern> pattern; // nullopt with patternSet: clear
};

QString parsePattern(const QVariant &value, StyleRequest &request)
{
    request.patternSet = true;
    if (value.isNull()) return {};
    if (value.metaType().id() == QMetaType::QString) {
        request.pattern = CanvasAppearanceRules::patternSwatch(value.toString());
        return request.pattern ? QString() : QStringLiteral("Unknown pattern: %1").arg(value.toString());
    }
    if (value.metaType().id() != QMetaType::QVariantMap) return QStringLiteral("Invalid pattern");
    const QVariantMap map = value.toMap();
    for (auto it = map.cbegin(); it != map.cend(); ++it)
        if (it.key() != QLatin1String("kind") && it.key() != QLatin1String("ink") && it.key() != QLatin1String("spacing")
            && it.key() != QLatin1String("angle") && it.key() != QLatin1String("weight"))
            return QStringLiteral("Unknown pattern setting: %1").arg(it.key());
    const auto kind = CanvasAppearanceRules::patternKind(map.value(QStringLiteral("kind")).toString());
    if (!kind || *kind == CanvasPatternKind::None) return QStringLiteral("Unknown pattern kind");
    CanvasPattern pattern;
    pattern.kind = *kind;
    const auto spacing = map.contains(QStringLiteral("spacing"))
        ? numberFrom(map.value(QStringLiteral("spacing")), 0, 1e9) : std::optional<double>(8);
    const auto angle = map.contains(QStringLiteral("angle"))
        ? numberFrom(map.value(QStringLiteral("angle")), -1e9, 1e9) : std::optional<double>(0);
    if (!spacing || !angle) return QStringLiteral("Invalid pattern spacing or angle");
    const auto weight = map.contains(QStringLiteral("weight"))
        ? numberFrom(map.value(QStringLiteral("weight")), 0, 1e9) : std::optional<double>(std::min(1.0, *spacing));
    if (!weight) return QStringLiteral("Invalid pattern weight");
    pattern.spacing = *spacing;
    pattern.angle = *angle;
    pattern.weight = *weight;
    if (map.contains(QStringLiteral("ink")) && (!colorFrom(map.value(QStringLiteral("ink")), pattern.ink) || !pattern.ink.isValid()))
        return QStringLiteral("Invalid pattern ink");
    request.pattern = pattern;
    return {};
}

QString parse(const QVariantMap &style, StyleRequest &r)
{
    static const QStringList keys{QStringLiteral("fill"), QStringLiteral("stroke"), QStringLiteral("strokeWidth"),
        QStringLiteral("dash"), QStringLiteral("cap"), QStringLiteral("join"), QStringLiteral("fillRule"),
        QStringLiteral("alignment"), QStringLiteral("miterLimit"), QStringLiteral("opacity"),
        QStringLiteral("blend"), QStringLiteral("pattern")};
    for (auto it = style.cbegin(); it != style.cend(); ++it)
        if (!keys.contains(it.key())) return QStringLiteral("This style property is not supported yet: %1").arg(it.key());
    QColor color;
    if (style.contains(QStringLiteral("fill"))) {
        if (!colorFrom(style.value(QStringLiteral("fill")), color)) return QStringLiteral("Invalid fill colour");
        r.fill = color;
    }
    if (style.contains(QStringLiteral("stroke"))) {
        if (!colorFrom(style.value(QStringLiteral("stroke")), color)) return QStringLiteral("Invalid stroke colour");
        r.stroke = color;
    }
    if (style.contains(QStringLiteral("strokeWidth"))
        && !(r.width = numberFrom(style.value(QStringLiteral("strokeWidth")), 0, 1e9)))
        return QStringLiteral("Invalid stroke width");
    if (style.contains(QStringLiteral("dash"))) {
        const QVariantList values = style.value(QStringLiteral("dash")).toList();
        if (values.size() % 2 || values.size() > 4096) return QStringLiteral("Invalid dash pattern");
        QVector<qreal> dash;
        for (const QVariant &value : values) {
            const auto length = numberFrom(value, 0, 1e6);
            if (!length || *length <= 0) return QStringLiteral("Invalid dash pattern");
            dash.append(*length);
        }
        r.dash = dash;
    }
    const QString cap = style.value(QStringLiteral("cap")).toString();
    if (style.contains(QStringLiteral("cap"))) {
        if (cap != QLatin1String("butt") && cap != QLatin1String("round") && cap != QLatin1String("square"))
            return QStringLiteral("Unsupported stroke cap");
        r.cap = cap == QLatin1String("round") ? Qt::RoundCap : cap == QLatin1String("square") ? Qt::SquareCap : Qt::FlatCap;
    }
    const QString join = style.value(QStringLiteral("join")).toString();
    if (style.contains(QStringLiteral("join"))) {
        if (join != QLatin1String("miter") && join != QLatin1String("round") && join != QLatin1String("bevel"))
            return QStringLiteral("Unsupported stroke join");
        r.join = join == QLatin1String("round") ? Qt::RoundJoin : join == QLatin1String("bevel") ? Qt::BevelJoin : Qt::MiterJoin;
    }
    const QString rule = style.value(QStringLiteral("fillRule")).toString();
    if (style.contains(QStringLiteral("fillRule"))) {
        if (rule != QLatin1String("evenodd") && rule != QLatin1String("nonzero")) return QStringLiteral("Unsupported fill rule");
        r.fillRule = rule == QLatin1String("nonzero") ? Qt::WindingFill : Qt::OddEvenFill;
    }
    if (style.contains(QStringLiteral("alignment"))
        && !(r.alignment = CanvasAppearanceRules::alignment(style.value(QStringLiteral("alignment")).toString())))
        return QStringLiteral("Unsupported stroke alignment");
    if (style.contains(QStringLiteral("miterLimit"))
        && !(r.miterLimit = numberFrom(style.value(QStringLiteral("miterLimit")), 1, 100)))
        return QStringLiteral("The miter limit must be from 1 to 100");
    if (style.contains(QStringLiteral("opacity"))
        && !(r.opacity = numberFrom(style.value(QStringLiteral("opacity")), 0, 1)))
        return QStringLiteral("Opacity must be from 0 to 1");
    if (style.contains(QStringLiteral("blend"))
        && !(r.blend = CanvasAppearanceRules::blend(style.value(QStringLiteral("blend")).toString())))
        return QStringLiteral("Unsupported blend mode");
    if (style.contains(QStringLiteral("pattern")))
        return parsePattern(style.value(QStringLiteral("pattern")), r);
    return {};
}

void applyTo(CanvasObject &after, const CanvasObject &before, const StyleRequest &r)
{
    if (r.fill) { after.fill = *r.fill; after.managedFill.clear(); }
    if (r.stroke) after.stroke = *r.stroke;
    if (r.width) after.strokeWidth = *r.width;
    if (r.dash) after.dashPattern = *r.dash;
    if (r.cap) after.strokeCap = *r.cap;
    if (r.join) after.strokeJoin = *r.join;
    if (r.fillRule) after.fillRule = *r.fillRule;
    CanvasAppearance appearance = *after.appearance;
    if (r.alignment) appearance.strokeAlignment = *r.alignment;
    if (r.miterLimit) appearance.miterLimit = *r.miterLimit;
    if (r.opacity) appearance.opacity = *r.opacity;
    if (r.blend) appearance.blend = *r.blend;
    if (r.patternSet && !r.pattern) appearance.pattern = {};
    if (r.patternSet && r.pattern) {
        // Ink not given: keep the current ink, else take the stroke colour.
        CanvasPattern pattern = *r.pattern;
        if (!pattern.ink.isValid())
            pattern.ink = before.appearance->pattern.ink.isValid() ? before.appearance->pattern.ink
                : before.hasStroke() ? before.stroke : QColor(Qt::black);
        appearance.pattern = pattern;
    }
    after.appearance = appearance;
}
} // namespace

void CanvasStyleModel::setDocument(CanvasDocumentAdapter *document)
{
    if (m_document == document) return;
    if (m_document) disconnect(m_document, nullptr, this, nullptr);
    m_document = document;
    if (document) {
        connect(document, &QObject::destroyed, this, [this] {
            m_document = nullptr;
            refresh();
            emit documentChanged();
        });
        connect(document, &CanvasDocumentAdapter::edited, this, [this] { refresh(); });
        connect(document, &CanvasDocumentAdapter::reset, this, [this] { refresh(); });
    }
    refresh();
    emit documentChanged();
}

bool CanvasStyleModel::fail(const QString &message)
{
    if (m_lastError != message) {
        m_lastError = message;
        emit errorChanged();
    }
    return false;
}

bool CanvasStyleModel::succeed(int changedObjects)
{
    if (!m_lastError.isEmpty()) {
        m_lastError.clear();
        emit errorChanged();
    }
    emit applied(changedObjects);
    return true;
}

bool CanvasStyleModel::apply(const QVariantList &selectedIds, const QVariantMap &style)
{
    if (!m_document) return fail(tr("No canvas document"));
    if (style.isEmpty()) return fail(tr("No style change was requested"));
    StyleRequest request;
    if (const QString error = parse(style, request); !error.isEmpty()) return fail(error);
    CanvasEdit edit;
    edit.label = tr("Change object style");
    QSet<CanvasObjectId> seen;
    for (const QVariant &value : selectedIds) {
        bool idOk = false;
        const CanvasObjectId id = value.toString().toULongLong(&idOk);
        if (!idOk || id == 0) return fail(tr("Invalid selected object ID"));
        if (seen.contains(id)) continue;
        seen.insert(id);
        const CanvasObject *before = m_document->object(id);
        if (!before) return fail(tr("A selected object no longer exists"));
        if (!m_document->effectiveVisible(*before) || m_document->effectiveLocked(*before))
            return fail(tr("A selected object is hidden or locked"));
        CanvasObject after = *before;
        applyTo(after, *before, request);
        if (const OperationError error = CanvasDocumentFormat::validateObject(
                after, id, CanvasDocumentLimits{}))
            return fail(error.message);
        if (after != *before) edit.changes.append({id, *before, after});
    }
    if (seen.isEmpty()) return fail(tr("Select an object first"));
    if (!edit.isEmpty() && !m_document->commit(edit))
        return fail(tr("Document changed before the style could commit"));
    return succeed(int(edit.changes.size()));
}

bool CanvasStyleModel::applyManaged(const QVariantList &selectedIds, const QByteArray &payload,
                                    const QColor &preview)
{
    if (!m_document) return fail(tr("No canvas document"));
    if (payload.isEmpty() || !preview.isValid()) return fail(tr("Invalid managed fill"));
    CanvasEdit edit;
    edit.label = tr("Change managed fill");
    QSet<CanvasObjectId> seen;
    for (const QVariant &value : selectedIds) {
        bool ok = false;
        const CanvasObjectId id = value.toString().toULongLong(&ok);
        if (!ok || id == 0) return fail(tr("Invalid selected object ID"));
        if (seen.contains(id)) continue;
        seen.insert(id);
        const CanvasObject *before = m_document->object(id);
        if (!before || !m_document->effectiveVisible(*before) || m_document->effectiveLocked(*before))
            return fail(tr("A selected object is missing, hidden or locked"));
        if (before->shape == CanvasShape::Polyline || before->shape == CanvasShape::Image
            || (before->shape == CanvasShape::Path && !before->hasClosedContour()))
            return fail(tr("Managed fills require a closed vector object"));
        CanvasObject after = *before;
        after.fill = preview;
        after.managedFill = payload;
        if (const OperationError error = CanvasDocumentFormat::validateObject(after, id, CanvasDocumentLimits{}))
            return fail(error.message);
        if (after != *before) edit.changes.append({id, *before, after});
    }
    if (seen.isEmpty()) return fail(tr("Select an object first"));
    if (!edit.isEmpty() && !m_document->commit(edit)) return fail(tr("Document changed before the fill could commit"));
    return succeed(int(edit.changes.size()));
}

bool CanvasStyleModel::applyPicker(const QVariantList &selectedIds, ColorPickerModel *picker)
{
    if (!picker || picker->targetIds().isEmpty()) return fail(tr("Select a typed picker color"));
    OperationError error;
    const QByteArray payload = CanvasManagedPaint::solid(
        picker->colorValue(picker->targetIds().constFirst()), picker->colorContext().document, &error);
    if (error) return fail(error.message);
    ColorEngine engine;
    const QBrush brush = CanvasManagedPaint::brush(payload, QRectF(0,0,1,1), engine, &error);
    return error ? fail(error.message) : applyManaged(selectedIds, payload, brush.color());
}

bool CanvasStyleModel::applyGradient(const QVariantList &selectedIds, GradientEditorModel *gradient)
{
    if (!gradient) return fail(tr("Select a typed gradient"));
    OperationError error;
    const QByteArray payload = CanvasManagedPaint::gradient(gradient->definition(), &error);
    if (error) return fail(error.message);
    const QColor preview(gradient->previewSamples().value(gradient->previewSamples().size()/2).toString());
    return applyManaged(selectedIds, payload, preview);
}

} // namespace QindaTK
