// SPDX-License-Identifier: LGPL-3.0-or-later
#include "motion_frame_renderer.h"
#include "motion_composition.h"

#include <QColorSpace>
#include <QFileInfo>
#include <QImageReader>
#include <QMutexLocker>
#include <QPainter>
#include <QSaveFile>
#include <QTransform>

namespace QindaTK {
namespace {
constexpr qint64 kMaxPixels = 16 * 1024 * 1024;
constexpr qsizetype kMaxSourceBytes = 256 * 1024 * 1024;

QImage encodedSrgb(const QImage &image)
{
    QImage result = image.colorSpace().isValid()
        ? image.convertedToColorSpace(QColorSpace(QColorSpace::SRgb),
                                      QImage::Format_ARGB32_Premultiplied)
        : image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    result.setColorSpace(QColorSpace(QColorSpace::SRgb));
    return result;
}
} // namespace

MotionFrameRenderer::MotionFrameRenderer(QObject *parent) : QObject(parent) {}

void MotionFrameRenderer::setComposition(MotionComposition *composition)
{
    if (m_composition == composition) return;
    m_composition = composition;
    emit compositionChanged();
}

void MotionFrameRenderer::setError(const QString &error)
{
    if (m_error == error) return;
    m_error = error;
    emit errorChanged();
}

QImage MotionFrameRenderer::frame() const
{
    QMutexLocker lock(&m_frameMutex);
    return m_frame;
}

bool MotionFrameRenderer::setSourceImage(const QString &sourceId, const QImage &image)
{
    if (sourceId.isEmpty() || image.isNull() || qint64(image.width()) * image.height() > kMaxPixels) {
        setError(QStringLiteral("Source needs a stable ID and at most 16 megapixels."));
        return false;
    }
    const QImage srgb = encodedSrgb(image);
    const qsizetype oldBytes = m_sources.value(sourceId).sizeInBytes();
    if (srgb.isNull() || m_sourceBytes - oldBytes + srgb.sizeInBytes() > kMaxSourceBytes) {
        setError(QStringLiteral("Source conversion failed or exceeds the 256 MiB source budget."));
        return false;
    }
    m_sourceBytes += srgb.sizeInBytes() - oldBytes;
    m_sources.insert(sourceId, srgb);
    setError({});
    return true;
}

bool MotionFrameRenderer::loadSource(const QString &sourceId, const QUrl &url)
{
    QString path;
    if (url.isLocalFile()) path = url.toLocalFile();
    else if (url.scheme() == QLatin1String("qrc")) path = QLatin1Char(':') + url.path();
    if (path.isEmpty()) {
        setError(QStringLiteral("Only local file and qrc image sources are supported."));
        return false;
    }
    QImageReader reader(path);
    const QSize size = reader.size();
    if (size.isValid() && qint64(size.width()) * size.height() > kMaxPixels) {
        setError(QStringLiteral("Source exceeds the 16-megapixel bound."));
        return false;
    }
    const QImage image = reader.read();
    if (image.isNull()) {
        setError(reader.errorString());
        return false;
    }
    return setSourceImage(sourceId, image);
}

void MotionFrameRenderer::removeSource(const QString &sourceId)
{
    const auto it = m_sources.find(sourceId);
    if (it == m_sources.end()) return;
    m_sourceBytes -= it->sizeInBytes();
    m_sources.erase(it);
}

QVariantMap MotionFrameRenderer::render(const QString &timeTicks, int width, int height)
{
    if (!m_composition || width < 1 || height < 1 || qint64(width) * height > kMaxPixels) {
        setError(QStringLiteral("A composition and a frame of at most 16 megapixels are required."));
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), m_error}};
    }
    const QVariantMap evaluation = m_composition->evaluate(timeTicks);
    if (!evaluation.value(QStringLiteral("ok")).toBool()) {
        setError(evaluation.value(QStringLiteral("error")).toString());
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), m_error}};
    }
    QImage output(width, height, QImage::Format_ARGB32_Premultiplied);
    if (output.isNull()) {
        setError(QStringLiteral("Could not allocate the composition frame."));
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), m_error}};
    }
    output.setColorSpace(QColorSpace(QColorSpace::SRgb));
    output.fill(Qt::transparent);
    QStringList missing;
    int drawn = 0;
    QPainter painter(&output);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    for (const QVariant &entry : evaluation.value(QStringLiteral("layers")).toList()) {
        const QVariantMap layer = entry.toMap();
        if (!layer.value(QStringLiteral("visible")).toBool()) continue;
        const double opacity = layer.value(QStringLiteral("opacity")).toDouble();
        if (opacity <= 0.0) continue;
        const QString sourceId = layer.value(QStringLiteral("sourceId")).toString();
        if (sourceId.isEmpty()) continue;
        const auto source = m_sources.constFind(sourceId);
        if (source == m_sources.cend()) { missing.append(sourceId); continue; }
        const QVariantList values = layer.value(QStringLiteral("matrix")).toList();
        if (values.size() != 6) continue;
        const QTransform matrix(values.at(0).toDouble(), values.at(1).toDouble(), 0.0,
                                values.at(2).toDouble(), values.at(3).toDouble(), 0.0,
                                values.at(4).toDouble(), values.at(5).toDouble(), 1.0);
        painter.save();
        painter.setOpacity(opacity);
        painter.setTransform(matrix);
        painter.drawImage(QPointF(0, 0), source.value());
        painter.restore();
        ++drawn;
    }
    painter.end();
    {
        QMutexLocker lock(&m_frameMutex);
        m_frame = output;
    }
    setError({});
    emit frameChanged();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("timeTicks"), timeTicks},
            {QStringLiteral("width"), width}, {QStringLiteral("height"), height},
            {QStringLiteral("drawnLayers"), drawn}, {QStringLiteral("missingSourceIds"), missing}};
}

bool MotionFrameRenderer::exportPng(const QUrl &url)
{
    if (!url.isLocalFile() || frame().isNull()) {
        setError(QStringLiteral("A rendered frame and a local PNG path are required."));
        return false;
    }
    QSaveFile file(url.toLocalFile());
    if (!file.open(QIODevice::WriteOnly)) { setError(file.errorString()); return false; }
    if (!frame().save(&file, "PNG") || !file.commit()) {
        setError(file.errorString());
        return false;
    }
    setError({});
    return true;
}

MotionFrameItem::MotionFrameItem(QQuickItem *parent) : QQuickPaintedItem(parent) {}

void MotionFrameItem::setRenderer(MotionFrameRenderer *renderer)
{
    if (m_renderer == renderer) return;
    disconnect(m_frameConnection);
    m_renderer = renderer;
    if (renderer)
        m_frameConnection = connect(renderer, &MotionFrameRenderer::frameChanged,
                                    this, &QQuickItem::update);
    emit rendererChanged();
    update();
}

void MotionFrameItem::paint(QPainter *painter)
{
    if (!m_renderer) return;
    const QImage image = m_renderer->frame();
    if (image.isNull()) return;
    QSizeF target = image.size();
    target.scale(size(), Qt::KeepAspectRatio);
    const QRectF destination((width() - target.width()) / 2.0,
                             (height() - target.height()) / 2.0,
                             target.width(), target.height());
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->drawImage(destination, image);
}

} // namespace QindaTK
