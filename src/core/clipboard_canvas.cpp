// SPDX-License-Identifier: LGPL-3.0-or-later
#include "clipboard_service.h"
#include "canvas_document.h"
#include "canvas_document_format.h"
#include "canvas_svg_export.h"
#include <QSet>
#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {
constexpr auto CanvasMime = "application/vnd.qindatk.canvas-paths";
OperationError clipboardFailure(ErrorCode code, const QString &message)
{ return {code, message, QStringLiteral("canvasPaths"), QStringLiteral("clipboard.canvas")}; }
}

bool ClipboardService::copyCanvasPaths(CanvasDocument *document, const QStringList &objectIds)
{
    if (!document || !m_adapter || objectIds.isEmpty()) {
        setError(clipboardFailure(ErrorCode::InvalidArgument, tr("Select path objects to copy")));
        return false;
    }
    CanvasDocument clip;
    clip.setLimits(document->limits());
    QSet<CanvasObjectId> seen;
    QVector<const CanvasObject *> selected;
    QRectF bounds;
    for (const QString &value : objectIds) {
        bool ok = false;
        const CanvasObjectId id = value.toULongLong(&ok);
        const CanvasObject *source = ok ? document->object(id) : nullptr;
        if (!source || source->shape != CanvasShape::Path) {
            setError(clipboardFailure(ErrorCode::InvalidArgument, tr("Clipboard selection must contain paths")));
            return false;
        }
        if (seen.contains(id)) continue;
        seen.insert(id);
        selected.append(source);
    }
    std::sort(selected.begin(), selected.end(), [document](const auto *a, const auto *b) {
        return document->paintKey(*a) < document->paintKey(*b);
    });
    for (const CanvasObject *source : selected) {
        CanvasObject copy = *source;
        copy.id = 0;
        copy.layer = 0;
        copy.z = 0;
        if (!clip.insert(copy, false)) {
            setError(clipboardFailure(ErrorCode::Unsupported, tr("Path cannot fit the clipboard format")));
            return false;
        }
        const QRectF objectBounds = CanvasGeometry::documentBounds(copy);
        bounds = bounds.isEmpty() ? objectBounds : bounds.united(objectBounds);
    }
    if (bounds.isValid() && !bounds.isEmpty()) clip.setPageRect(bounds);
    clip.setPageColor(Qt::transparent);
    OperationError error;
    const QByteArray bytes = CanvasDocumentFormat::encode(clip.state(), &error);
    if (bytes.isEmpty()) { setError(error); return false; }
    auto *mime = new QMimeData;
    mime->setData(QString::fromLatin1(CanvasMime), bytes);
    const QByteArray svg = document->outputProfileIcc().isEmpty()
        ? CanvasSvgExport::encode(clip, nullptr) : QByteArray{};
    if (!svg.isEmpty()) mime->setData(QStringLiteral("image/svg+xml"), svg);
    if (!m_adapter->setMimeData(mime)) {
        setError(clipboardFailure(ErrorCode::Unavailable, tr("Clipboard adapter rejected the paths")));
        return false;
    }
    m_lastError.clear();
    m_lossWarnings.clear();
    refresh();
    return true;
}

QStringList ClipboardService::pasteCanvasPaths(CanvasDocument *document, qreal offsetX, qreal offsetY)
{
    QStringList inserted;
    const QMimeData *mime = m_adapter ? m_adapter->mimeData() : nullptr;
    if (!document || !mime || !mime->hasFormat(QString::fromLatin1(CanvasMime))
        || !std::isfinite(offsetX) || !std::isfinite(offsetY)) {
        setError(clipboardFailure(ErrorCode::InvalidArgument, tr("No compatible canvas paths to paste")));
        return inserted;
    }
    OperationError error;
    const auto state = CanvasDocumentFormat::decode(mime->data(QString::fromLatin1(CanvasMime)),
                                                    document->limits(), &error);
    if (!state) { setError(error); return inserted; }
    CanvasEdit edit;
    edit.label = tr("Paste paths");
    for (const CanvasObjectId id : [&state] {
        QVector<CanvasObjectId> ids;
        for (auto it = state->objects.cbegin(); it != state->objects.cend(); ++it) ids.append(it.key());
        std::sort(ids.begin(), ids.end());
        return ids;
    }()) {
        CanvasObject copy = state->objects.value(id);
        if (copy.shape != CanvasShape::Path) {
            setError(clipboardFailure(ErrorCode::Unsupported, tr("Clipboard contains an unsupported object")));
            return {};
        }
        copy.id = document->reserveObjectId();
        copy.layer = document->activeLayer();
        copy.z = document->reserveZ();
        copy.transform = copy.transform * QTransform::fromTranslate(offsetX, offsetY);
        if (!copy.id || !copy.layer) {
            setError(clipboardFailure(ErrorCode::Unsupported, tr("Canvas identity limit reached")));
            return {};
        }
        edit.changes.append({copy.id, std::nullopt, copy});
        inserted.append(QString::number(copy.id));
    }
    if (const auto result = document->commitEdit(edit)) {
        setError(result);
        return {};
    }
    m_lastError.clear();
    emit changed();
    return inserted;
}
} // namespace QindaTK
