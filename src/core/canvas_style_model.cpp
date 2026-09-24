// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_style_model.h"
#include "canvas_document_format.h"
#include "canvas_managed_paint.h"

#include <QSet>
#include <cmath>

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
} // namespace

void CanvasStyleModel::setDocument(CanvasDocumentAdapter *document)
{
    if (m_document == document) return;
    if (m_document) disconnect(m_document, nullptr, this, nullptr);
    m_document = document;
    if (document) connect(document, &QObject::destroyed, this, [this] {
        m_document = nullptr;
        emit documentChanged();
    });
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

bool CanvasStyleModel::apply(const QVariantList &selectedIds, const QVariantMap &style)
{
    if (!m_document) return fail(tr("No canvas document"));
    if (style.isEmpty()) return fail(tr("No style change was requested"));
    for (auto it = style.cbegin(); it != style.cend(); ++it) {
        if (it.key() != QLatin1String("fill") && it.key() != QLatin1String("stroke")
            && it.key() != QLatin1String("strokeWidth")
            && it.key() != QLatin1String("dash") && it.key() != QLatin1String("cap")
            && it.key() != QLatin1String("join") && it.key() != QLatin1String("fillRule"))
            return fail(tr("This style property is not supported yet: %1").arg(it.key()));
    }
    QColor fill, stroke;
    if (style.contains(QStringLiteral("fill"))
        && !colorFrom(style.value(QStringLiteral("fill")), fill))
        return fail(tr("Invalid fill colour"));
    if (style.contains(QStringLiteral("stroke"))
        && !colorFrom(style.value(QStringLiteral("stroke")), stroke))
        return fail(tr("Invalid stroke colour"));
    bool widthOk = true;
    const double width = style.contains(QStringLiteral("strokeWidth"))
                             ? style.value(QStringLiteral("strokeWidth")).toDouble(&widthOk) : 0;
    if (!widthOk || !std::isfinite(width) || width < 0)
        return fail(tr("Invalid stroke width"));
    QVector<qreal> dash;
    if (style.contains(QStringLiteral("dash"))) {
        const QVariantList values = style.value(QStringLiteral("dash")).toList();
        if (values.size() % 2 || values.size() > 4096) return fail(tr("Invalid dash pattern"));
        for (const QVariant &value : values) {
            bool ok = false;
            const double length = value.toDouble(&ok);
            if (!ok || !std::isfinite(length) || length <= 0 || length > 1e6)
                return fail(tr("Invalid dash pattern"));
            dash.append(length);
        }
    }
    const QString cap = style.value(QStringLiteral("cap")).toString();
    if (style.contains(QStringLiteral("cap")) && cap != QLatin1String("butt")
        && cap != QLatin1String("round") && cap != QLatin1String("square"))
        return fail(tr("Unsupported stroke cap"));
    const QString join = style.value(QStringLiteral("join")).toString();
    if (style.contains(QStringLiteral("join")) && join != QLatin1String("miter")
        && join != QLatin1String("round") && join != QLatin1String("bevel"))
        return fail(tr("Unsupported stroke join"));
    const QString rule = style.value(QStringLiteral("fillRule")).toString();
    if (style.contains(QStringLiteral("fillRule")) && rule != QLatin1String("evenodd")
        && rule != QLatin1String("nonzero")) return fail(tr("Unsupported fill rule"));

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
        if (style.contains(QStringLiteral("fill"))) { after.fill = fill; after.managedFill.clear(); }
        if (style.contains(QStringLiteral("stroke"))) after.stroke = stroke;
        if (style.contains(QStringLiteral("strokeWidth"))) after.strokeWidth = width;
        if (style.contains(QStringLiteral("dash"))) after.dashPattern = dash;
        if (style.contains(QStringLiteral("cap")))
            after.strokeCap = cap == QLatin1String("round") ? Qt::RoundCap
                : cap == QLatin1String("square") ? Qt::SquareCap : Qt::FlatCap;
        if (style.contains(QStringLiteral("join")))
            after.strokeJoin = join == QLatin1String("round") ? Qt::RoundJoin
                : join == QLatin1String("bevel") ? Qt::BevelJoin : Qt::MiterJoin;
        if (style.contains(QStringLiteral("fillRule")))
            after.fillRule = rule == QLatin1String("nonzero") ? Qt::WindingFill : Qt::OddEvenFill;
        if (const OperationError error = CanvasDocumentFormat::validateObject(
                after, id, CanvasDocumentLimits{}))
            return fail(error.message);
        if (after != *before) edit.changes.append({id, *before, after});
    }
    if (seen.isEmpty()) return fail(tr("Select an object first"));
    if (!edit.isEmpty() && !m_document->commit(edit))
        return fail(tr("Document changed before the style could commit"));
    if (!m_lastError.isEmpty()) {
        m_lastError.clear();
        emit errorChanged();
    }
    emit applied(edit.changes.size());
    return true;
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
    if (!m_lastError.isEmpty()) { m_lastError.clear(); emit errorChanged(); }
    emit applied(edit.changes.size());
    return true;
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
