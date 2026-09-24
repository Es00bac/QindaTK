// SPDX-License-Identifier: LGPL-3.0-or-later
#include "node_graph_model.h"

#include <QBuffer>
#include <QColorSpace>
#include <QPainter>

namespace QindaTK {
namespace {
constexpr qint64 kMaxPixels = 16'000'000;
constexpr qint64 kMaxSourceBytes = 256 * 1024 * 1024;
constexpr qint64 kMaxCacheBytes = 128 * 1024 * 1024;

QImage blank(const QSize &size)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    image.fill(Qt::transparent);
    return image;
}
}

bool NodeGraphModel::setSourceImage(const QString &nodeId, const QImage &image)
{
    const int index = nodeIndex(nodeId);
    const QColorSpace space = image.colorSpace();
    const qint64 oldBytes = m_sources.contains(nodeId) ? m_sources.value(nodeId).sizeInBytes() : 0;
    if (index < 0 || m_nodes.at(index).type != QLatin1String("source")
        || image.isNull() || qint64(image.width()) * image.height() > kMaxPixels
        || m_sourceBytes - oldBytes + image.sizeInBytes() > kMaxSourceBytes
        || !space.isValid() || space.primaries() != QColorSpace::Primaries::SRgb
        || space.transferFunction() != QColorSpace::TransferFunction::SRgb) {
        setError(QStringLiteral("Source node requires a tagged sRGB image within the 256 MiB source budget"));
        return false;
    }
    m_sources.insert(nodeId, image);
    m_sourceBytes = m_sourceBytes - oldBytes + image.sizeInBytes();
    invalidateFrom(nodeId);
    setError({});
    emit changed();
    return true;
}

QString NodeGraphModel::inputNode(const QString &nodeId, const QString &portId) const
{
    for (const Edge &edge : m_edges)
        if (edge.toNode == nodeId && edge.toPort == portId) return edge.fromNode;
    return {};
}

QImage NodeGraphModel::evaluate(const QString &nodeId, const QSize &size,
                                 QSet<QString> *visiting)
{
    if (m_cache.contains(nodeId)) return m_cache.value(nodeId);
    const int index = nodeIndex(nodeId);
    if (index < 0 || visiting->contains(nodeId)) {
        setError(QStringLiteral("Missing node or graph cycle"));
        return {};
    }
    visiting->insert(nodeId);
    const Node node = m_nodes.at(index);
    const auto input = [&](const QString &portId) -> QImage {
        const QString source = inputNode(nodeId, portId);
        if (source.isEmpty()) {
            setError(QStringLiteral("Unconnected %1 input on %2").arg(portId, node.title));
            return {};
        }
        return evaluate(source, size, visiting);
    };
    QImage result;
    if (node.type == QLatin1String("source")) {
        const QImage source = m_sources.value(nodeId);
        if (source.isNull()) {
            setError(QStringLiteral("Source pixels are unavailable for %1").arg(node.title));
        } else {
            result = blank(size);
            QPainter painter(&result);
            painter.drawImage(QPoint(0, 0), source);
        }
    } else if (node.type == QLatin1String("solid")) {
        result = blank(size);
        result.fill(QColor(node.parameters.value(QStringLiteral("color"),
                                  QStringLiteral("transparent")).toString()));
    } else if (node.type == QLatin1String("transform")) {
        const QImage source = input(QStringLiteral("image"));
        if (!source.isNull()) {
            result = blank(size);
            QPainter painter(&result);
            painter.translate(node.parameters.value(QStringLiteral("x"), 0).toDouble(),
                              node.parameters.value(QStringLiteral("y"), 0).toDouble());
            const double scale = node.parameters.value(QStringLiteral("scale"), 1).toDouble();
            painter.scale(scale, scale);
            painter.drawImage(QPoint(0, 0), source);
        }
    } else if (node.type == QLatin1String("blend")) {
        const QImage background = input(QStringLiteral("background"));
        const QImage foreground = input(QStringLiteral("foreground"));
        if (!background.isNull() && !foreground.isNull()) {
            result = blank(size);
            QPainter painter(&result);
            painter.drawImage(QPoint(0, 0), background);
            painter.setOpacity(node.parameters.value(QStringLiteral("opacity"), 1).toDouble());
            painter.drawImage(QPoint(0, 0), foreground);
        }
    } else if (node.type == QLatin1String("output")) {
        result = input(QStringLiteral("image"));
    }
    visiting->remove(nodeId);
    if (!result.isNull()) {
        m_cache.insert(nodeId, result);
        m_cacheOrder.append(nodeId);
        m_cacheBytes += result.sizeInBytes();
        while (m_cacheBytes > kMaxCacheBytes && m_cacheOrder.size() > 1) {
            const QString oldest = m_cacheOrder.takeFirst();
            m_cacheBytes -= m_cache.value(oldest).sizeInBytes();
            m_cache.remove(oldest);
        }
    }
    return result;
}

QVariantMap NodeGraphModel::render(const QString &outputNodeId, int width, int height)
{
    const int index = nodeIndex(outputNodeId);
    if (index < 0 || m_nodes.at(index).type != QLatin1String("output")
        || width < 1 || height < 1 || qint64(width) * height > kMaxPixels) {
        setError(QStringLiteral("Select an output node and a canvas of at most 16 MP"));
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), m_error}};
    }
    const QSize size(width, height);
    if (m_cacheSize != size) { invalidateAll(); m_cacheSize = size; }
    setError({});
    QSet<QString> visiting;
    const QImage candidate = evaluate(outputNodeId, size, &visiting);
    if (candidate.isNull())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), m_error}};
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !candidate.save(&buffer, "PNG")) {
        setError(QStringLiteral("Could not encode composited PNG"));
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), m_error}};
    }
    m_frame = candidate;
    m_imageDataUrl = QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());
    emit frameChanged();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("imageDataUrl"), m_imageDataUrl},
            {QStringLiteral("width"), width}, {QStringLiteral("height"), height},
            {QStringLiteral("revision"), QVariant::fromValue<qulonglong>(m_revision)}};
}

} // namespace QindaTK
