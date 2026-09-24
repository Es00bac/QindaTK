// SPDX-License-Identifier: LGPL-3.0-or-later
// Path operations as one document command (D-108).
#include "canvas_document.h"
#include "canvas_path_boolean.h"

#include <QSet>
#include <algorithm>

namespace QindaTK {

qulonglong CanvasDocument::combinePaths(const QVariantList &ids, const QString &operation)
{
    const auto op = CanvasPathBoolean::operation(operation);
    if (!op) {
        refuse(tr("Unknown path operation: %1").arg(operation));
        return 0;
    }
    QVector<const CanvasObject *> operands;
    QSet<CanvasObjectId> seen;
    for (const QVariant &value : ids) {
        bool ok = false;
        const CanvasObjectId id = value.toString().toULongLong(&ok);
        if (!ok || id == 0) {
            refuse(tr("Invalid selected object ID"));
            return 0;
        }
        if (seen.contains(id)) continue;
        seen.insert(id);
        const CanvasObject *o = object(id);
        if (!o) {
            refuse(tr("A selected object no longer exists"));
            return 0;
        }
        if (!effectiveVisible(*o) || effectiveLocked(*o)) {
            refuse(tr("A selected object is hidden or locked"));
            return 0;
        }
        if (!CanvasPathBoolean::eligible(*o)) {
            refuse(tr("Path operations need closed rectangles, ellipses or paths"));
            return 0;
        }
        operands.append(o);
    }
    if (operands.size() < 2) {
        refuse(tr("Select at least two shapes to combine"));
        return 0;
    }
    std::sort(operands.begin(), operands.end(), [this](const CanvasObject *a, const CanvasObject *b) {
        const qint64 ka = paintKey(*a), kb = paintKey(*b);
        return ka != kb ? ka < kb : a->id < b->id;
    });
    const CanvasObject &back = *operands.first();
    OperationError error;
    const auto combined = CanvasPathBoolean::combine(operands, *op, back.transform, &error);
    if (!combined) {
        refuse(error.message);
        return 0;
    }
    // The backmost operand keeps its identity, place and appearance; only its
    // geometry becomes the combined path.
    CanvasObject result = back;
    result.shape = CanvasShape::Path;
    result.rect = QRectF();
    result.points.clear();
    result.clearParametric();
    result.pathNodes = combined->contours.first().nodes;
    result.pathClosed = true;
    result.extraContours = combined->contours.mid(1);
    result.fillRule = combined->fillRule;
    CanvasEdit edit;
    edit.label = tr("Combine paths");
    edit.changes.append({back.id, back, result});
    for (qsizetype i = 1; i < operands.size(); ++i)
        edit.changes.append({operands[i]->id, *operands[i], std::nullopt});
    // AGENT-GUARD: `back` points into the object table, which the commit
    // rewrites; keep the id before committing.
    const CanvasObjectId resultId = back.id;
    return commitRecorded(edit) ? resultId : 0;
}

} // namespace QindaTK
