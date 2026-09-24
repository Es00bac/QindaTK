// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_style_model.h"
#include "canvas_document_format.h"

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
            && it.key() != QLatin1String("strokeWidth"))
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
        if (style.contains(QStringLiteral("fill"))) after.fill = fill;
        if (style.contains(QStringLiteral("stroke"))) after.stroke = stroke;
        if (style.contains(QStringLiteral("strokeWidth"))) after.strokeWidth = width;
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

} // namespace QindaTK
