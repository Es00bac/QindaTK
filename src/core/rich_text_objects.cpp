// SPDX-License-Identifier: LGPL-3.0-or-later
#include "rich_text_objects.h"

#include <QFile>
#include <QPainter>
#include <QUrl>

namespace QindaTK {
using namespace RichText;

QImage RichTextImageRenderer::image(const QString &source)
{
    if (source.isEmpty() || m_failed.contains(source)) return {};
    if (QImage *cached = m_cache.object(source)) return *cached;
    QImage decoded;
    const QUrl url(source);
    if (url.scheme() == u"data") {
        const QString text = url.toString(QUrl::FullyEncoded);
        const qsizetype comma = text.indexOf(u',');
        if (comma > 0 && text.left(comma).endsWith(QLatin1String(";base64")))
            decoded.loadFromData(QByteArray::fromBase64(text.mid(comma + 1).toLatin1()));
    } else if (url.scheme() == u"file" || url.scheme() == u"qrc") {
        const QString path = url.scheme() == u"qrc" ? u':' + url.path() : url.toLocalFile();
        QFile file(path);
        if (file.open(QIODevice::ReadOnly) && file.size() <= 32 * 1024 * 1024) decoded.loadFromData(file.readAll());
    }
    if (decoded.isNull()) {
        m_failed.insert(source);
        return {};
    }
    const int cost = int(qMax<qsizetype>(1, decoded.sizeInBytes() / 1024));
    m_cache.insert(source, new QImage(decoded), cost);
    return decoded;
}

void RichTextImageRenderer::paint(QPainter *painter, const QRectF &rect, const RichObject &object,
                                  const PaintContext &context)
{
    const QImage decoded = object.kind == u"image" ? image(object.source) : QImage();
    if (decoded.isNull()) {
        const bool resolvable = object.source.startsWith(QLatin1String("file:"))
            || object.source.startsWith(QLatin1String("qrc:")) || object.source.startsWith(QLatin1String("data:"));
        paintPlaceholder(painter, rect, object, context, resolvable);
        return;
    }
    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->drawImage(rect, decoded);
    painter->restore();
}

void RichTextImageRenderer::paintPlaceholder(QPainter *painter, const QRectF &rect, const RichObject &object,
                                             const PaintContext &context, bool missing)
{
    painter->save();
    painter->setPen(QPen(context.border, 1.0, missing ? Qt::DashLine : Qt::SolidLine));
    painter->setBrush(context.objectFill);
    painter->drawRect(rect.adjusted(0.5, 0.5, -0.5, -0.5));
    const QString label = object.altText.isEmpty() ? object.kind : object.altText;
    if (rect.height() >= 10 && rect.width() >= 16) {
        QFont font = painter->font();
        font.setPixelSize(qBound(8, int(rect.height() / 4), 12));
        painter->setFont(font);
        painter->setPen(context.objectText);
        painter->drawText(rect.adjusted(3, 2, -3, -2), Qt::AlignCenter | Qt::TextWordWrap, label);
    }
    painter->restore();
}

} // namespace QindaTK
