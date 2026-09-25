// SPDX-License-Identifier: LGPL-3.0-or-later
#include "motion_frame_renderer.h"
#include "automation_model.h"
#include "motion_composition.h"
#include "timeline_time.h"
#include "tracking_roto_model.h"

#include <QColorSpace>
#include <QFileInfo>
#include <QImageReader>
#include <QMutexLocker>
#include <QPainter>
#include <QSaveFile>
#include <QTransform>
#include <algorithm>
#include <limits>

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
    for (const auto &link : std::as_const(m_compositionLinks)) disconnect(link);
    m_compositionLinks.clear();
    m_composition = composition;
    if (composition) {
        m_compositionLinks = {
            connect(composition, &MotionComposition::layersChanged, this, &MotionFrameRenderer::invalidate),
            connect(composition, &MotionComposition::animationModelChanged, this, [this] {
                watchAnimation();
                invalidate();
            })};
    }
    watchAnimation();
    invalidate();
    emit compositionChanged();
}

void MotionFrameRenderer::watchAnimation()
{
    for (const auto &link : std::as_const(m_animationLinks)) disconnect(link);
    m_animationLinks.clear();
    AutomationModel *model = m_composition ? m_composition->animationModel() : nullptr;
    if (!model) return;
    // Gesture previews and record passes change sampled values between revisions.
    m_animationLinks = {
        connect(model, &AutomationModel::revisionChanged, this, &MotionFrameRenderer::invalidate),
        connect(model, &AutomationModel::pointsChanged, this, &MotionFrameRenderer::invalidate),
        connect(model, &AutomationModel::parametersChanged, this, &MotionFrameRenderer::invalidate),
        connect(model, &AutomationModel::contentReset, this, &MotionFrameRenderer::invalidate)};
}

void MotionFrameRenderer::setRotoModel(TrackingRotoModel *model)
{
    if (m_rotoModel == model) return;
    for (const auto &link : std::as_const(m_rotoLinks)) disconnect(link);
    m_rotoLinks.clear();
    m_rotoModel = model;
    if (model) {
        m_rotoLinks = {connect(model, &TrackingRotoModel::changed, this, &MotionFrameRenderer::invalidate),
                       connect(model, &TrackingRotoModel::analysisChanged, this, &MotionFrameRenderer::invalidate)};
    }
    invalidate();
    emit rotoModelChanged();
}

void MotionFrameRenderer::invalidate()
{
    ++m_contentRevision;
    if (!m_cache.isEmpty()) {
        m_cache.clear();
        emit cacheChanged();
    }
    emit contentInvalidated();
}

void MotionFrameRenderer::clearFrameCache()
{
    if (m_cache.isEmpty()) return;
    m_cache.clear();
    emit cacheChanged();
}

void MotionFrameRenderer::setFrameCacheLimit(int limit)
{
    limit = std::clamp(limit, 0, kMaxCachedFrames);
    if (m_cacheLimit == limit) return;
    m_cacheLimit = limit;
    trimCache();
    emit cacheChanged();
}

void MotionFrameRenderer::trimCache()
{
    qsizetype bytes = 0;
    for (qsizetype i = 0; i < m_cache.size(); ++i) {
        bytes += m_cache.at(i).image.sizeInBytes();
        if (i >= m_cacheLimit || (i > 0 && bytes > kMaxCacheBytes)) {
            m_cache.resize(i);
            return;
        }
    }
}

void MotionFrameRenderer::publish(const QImage &image, const QString &ticks)
{
    {
        QMutexLocker lock(&m_frameMutex);
        m_frame = image;
    }
    m_frameTicks = ticks;
    m_frameRevision = m_contentRevision;
    emit frameChanged();
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

void MotionFrameRenderer::setMaskTicksPerFrame(const QString &ticks)
{
    const auto parsed = TimelineTime::parseDecimal(ticks);
    if (!parsed || *parsed < 1) {
        setError(QStringLiteral("maskTicksPerFrame must be a positive exact tick count."));
        return;
    }
    if (m_maskTicksPerFrame == *parsed) return;
    m_maskTicksPerFrame = *parsed;
    invalidate();
    emit maskTimingChanged();
}

QString MotionFrameRenderer::sourceFrame(const QString &sourceId) const
{
    const auto it = m_sourceFrames.constFind(sourceId);
    return it == m_sourceFrames.cend() ? QString() : QString::number(it.value());
}

bool MotionFrameRenderer::setSourceImage(const QString &sourceId, const QImage &image, qint64 sourceFrame)
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
    if (sourceFrame >= 0) m_sourceFrames.insert(sourceId, sourceFrame);
    else m_sourceFrames.remove(sourceId);
    invalidate();
    setError({});
    return true;
}

bool MotionFrameRenderer::registerEffectProcessor(const QString &effectId,
                                                 MotionEffectRegistry::Processor processor)
{
    if (!m_effects.registerProcessor(effectId, std::move(processor))) return false;
    invalidate();
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
    m_sourceFrames.remove(sourceId);
    invalidate();
}

QVariantMap MotionFrameRenderer::render(const QString &timeTicks, int width, int height)
{
    if (!m_composition || width < 1 || height < 1 || qint64(width) * height > kMaxPixels) {
        setError(QStringLiteral("A composition and a frame of at most 16 megapixels are required."));
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), m_error}};
    }
    // A cached frame belongs to the current content revision: invalidate()
    // clears the cache, so a hit can never return a stale composite.
    const auto exact = TimelineTime::parseDecimal(timeTicks);
    const QString key = exact ? TimelineTime::toDecimal(*exact) : QString();
    for (qsizetype i = 0; exact && i < m_cache.size(); ++i) {
        if (m_cache.at(i).ticks != key || m_cache.at(i).width != width || m_cache.at(i).height != height) continue;
        m_cache.move(i, 0);
        const CachedFrame hit = m_cache.first();
        publish(hit.image, timeTicks);
        setError({});
        return {{QStringLiteral("ok"), true}, {QStringLiteral("timeTicks"), timeTicks},
                {QStringLiteral("width"), width}, {QStringLiteral("height"), height},
                {QStringLiteral("drawnLayers"), hit.drawn}, {QStringLiteral("missingSourceIds"), hit.missing},
                {QStringLiteral("cached"), true}, {QStringLiteral("contentRevision"), contentRevision()}};
    }
    const QVariantMap evaluation = m_composition->evaluate(timeTicks);
    if (!evaluation.value(QStringLiteral("ok")).toBool()) {
        setError(evaluation.value(QStringLiteral("error")).toString());
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), m_error}};
    }
    const QVariantList evaluatedLayers = evaluation.value(QStringLiteral("layers")).toList();
    qsizetype effectRecords = 0;
    for (const QVariant &entry : evaluatedLayers) {
        const QVariantMap layer = entry.toMap();
        if (!layer.value(QStringLiteral("visible")).toBool()
            || layer.value(QStringLiteral("opacity")).toDouble() <= 0.0
            || layer.value(QStringLiteral("sourceId")).toString().isEmpty()) continue;
        const QVariant effectValue = layer.value(QStringLiteral("effects"));
        if (!effectValue.isValid()) continue;
        if (effectValue.metaType().id() != QMetaType::QVariantList) {
            setError(QStringLiteral("Layer effects must be an array of effect records."));
            return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), m_error}};
        }
        const QVariantList effects = effectValue.toList();
        effectRecords += effects.size();
        if (effectRecords > 64) {
            setError(QStringLiteral("A rendered frame may contain at most 64 effect records."));
            return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), m_error}};
        }
        QString effectError;
        if (!m_effects.validate(effects, &effectError)) {
            setError(effectError);
            return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), m_error}};
        }
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
    for (const QVariant &entry : evaluatedLayers) {
        const QVariantMap layer = entry.toMap();
        if (!layer.value(QStringLiteral("visible")).toBool()) continue;
        const double opacity = layer.value(QStringLiteral("opacity")).toDouble();
        if (opacity <= 0.0) continue;
        const QString sourceId = layer.value(QStringLiteral("sourceId")).toString();
        if (sourceId.isEmpty()) continue;
        const auto source = m_sources.constFind(sourceId);
        if (source == m_sources.cend()) { missing.append(sourceId); continue; }
        const QVariantList effects = layer.value(QStringLiteral("effects")).toList();
        const MotionEffectResult effected = m_effects.apply(source.value(), effects);
        if (!effected) {
            painter.end();
            setError(QStringLiteral("Layer %1: %2").arg(layer.value(QStringLiteral("layerId")).toString(), effected.error));
            return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), m_error}};
        }
        QImage layerImage = encodedSrgb(effected.image);
        const QString maskId = layer.value(QStringLiteral("maskId")).toString();
        if (!maskId.isEmpty()) {
            // AGENT-NOTE (D-142): a mask cuts the pixels it was drawn on, so it
            // samples the decoded source frame when the feed reported one and
            // the layer-local time otherwise, never the composition time.
            qint64 maskFrame = m_sourceFrames.value(sourceId, -1);
            if (maskFrame < 0) {
                const auto local = TimelineTime::parseDecimal(layer.value(QStringLiteral("localTicks")).toString());
                if (local && *local >= 0) maskFrame = *local / m_maskTicksPerFrame;
            }
            if (!m_rotoModel || maskFrame < 0 || maskFrame > std::numeric_limits<int>::max()) {
                painter.end();
                setError(QStringLiteral("Masked layer needs a roto model and a nonnegative layer frame."));
                return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), m_error}};
            }
            layerImage = m_rotoModel->applyMask(maskId, int(maskFrame), layerImage);
            if (layerImage.isNull()) {
                painter.end();
                setError(QStringLiteral("Layer %1: %2").arg(layer.value(QStringLiteral("layerId")).toString(),
                                      m_rotoModel->lastError()));
                return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), m_error}};
            }
        }
        const QVariantList values = layer.value(QStringLiteral("matrix")).toList();
        if (values.size() != 6) continue;
        const QTransform matrix(values.at(0).toDouble(), values.at(1).toDouble(), 0.0,
                                values.at(2).toDouble(), values.at(3).toDouble(), 0.0,
                                values.at(4).toDouble(), values.at(5).toDouble(), 1.0);
        painter.save();
        painter.setOpacity(opacity);
        painter.setTransform(matrix);
        painter.drawImage(QPointF(0, 0), layerImage);
        painter.restore();
        ++drawn;
    }
    painter.end();
    publish(output, timeTicks);
    if (exact && m_cacheLimit > 0) {
        m_cache.prepend(CachedFrame{key, width, height, output, drawn, missing});
        trimCache();
        emit cacheChanged();
    }
    setError({});
    return {{QStringLiteral("ok"), true}, {QStringLiteral("timeTicks"), timeTicks},
            {QStringLiteral("width"), width}, {QStringLiteral("height"), height},
            {QStringLiteral("drawnLayers"), drawn}, {QStringLiteral("missingSourceIds"), missing},
            {QStringLiteral("cached"), false}, {QStringLiteral("contentRevision"), contentRevision()}};
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
