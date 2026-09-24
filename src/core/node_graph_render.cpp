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

QPainter::CompositionMode compositionMode(const QString &mode)
{
    if (mode == QLatin1String("multiply")) return QPainter::CompositionMode_Multiply;
    if (mode == QLatin1String("screen")) return QPainter::CompositionMode_Screen;
    if (mode == QLatin1String("overlay")) return QPainter::CompositionMode_Overlay;
    if (mode == QLatin1String("darken")) return QPainter::CompositionMode_Darken;
    if (mode == QLatin1String("lighten")) return QPainter::CompositionMode_Lighten;
    if (mode == QLatin1String("difference")) return QPainter::CompositionMode_Difference;
    if (mode == QLatin1String("add")) return QPainter::CompositionMode_Plus;
    return QPainter::CompositionMode_SourceOver;
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

bool NodeGraphModel::clearSourceImage(const QString &nodeId)
{
    const auto it = m_sources.find(nodeId);
    if (it == m_sources.end()) return false;
    m_sourceBytes -= it->sizeInBytes();
    m_sources.erase(it);
    invalidateFrom(nodeId);
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
    m_evaluated.append(nodeId);
    const Node node = m_nodes.at(index);
    const auto value = [&](const char *parameterId) {
        return parameter(node.type, node.parameters, QString::fromLatin1(parameterId));
    };
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
        result.fill(QColor(value("color").toString()));
    } else if (node.type == QLatin1String("mask")) {
        result = blank(size);
        QPainter painter(&result);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        const QRectF area(value("x").toDouble(), value("y").toDouble(),
                          value("width").toDouble(), value("height").toDouble());
        if (value("shape").toString() == QLatin1String("ellipse")) painter.drawEllipse(area);
        else painter.drawRect(area);
    } else if (node.type == QLatin1String("transform")) {
        const QImage source = input(QStringLiteral("image"));
        if (!source.isNull()) {
            // Scale and rotate about the canvas centre, then translate.
            const QPointF centre(size.width() / 2.0, size.height() / 2.0);
            const double scale = value("scale").toDouble();
            result = blank(size);
            QPainter painter(&result);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.translate(centre + QPointF(value("x").toDouble(), value("y").toDouble()));
            painter.rotate(value("rotation").toDouble());
            painter.scale(scale, scale);
            painter.translate(-centre);
            painter.drawImage(QPoint(0, 0), source);
        }
    } else if (node.type == QLatin1String("blend")) {
        const QImage background = input(QStringLiteral("background"));
        QImage foreground = input(QStringLiteral("foreground"));
        const bool masked = !inputNode(nodeId, QStringLiteral("mask")).isEmpty();
        const QImage mask = masked ? input(QStringLiteral("mask")) : QImage();
        if (!background.isNull() && !foreground.isNull() && (!masked || !mask.isNull())) {
            if (masked) {
                QPainter cut(&foreground);
                cut.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                cut.drawImage(QPoint(0, 0), mask);
            }
            result = blank(size);
            QPainter painter(&result);
            painter.drawImage(QPoint(0, 0), background);
            painter.setCompositionMode(compositionMode(value("mode").toString()));
            painter.setOpacity(value("opacity").toDouble());
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
    m_evaluated.clear();
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
            {QStringLiteral("revision"), QVariant::fromValue<qulonglong>(m_revision)},
            {QStringLiteral("evaluatedNodeIds"), m_evaluated}};
}

} // namespace QindaTK
