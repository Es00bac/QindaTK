// SPDX-License-Identifier: LGPL-3.0-or-later
// Print previews (OFF-12, D-130): a bounded LRU cache of sheet images, filled
// on the preview thread for views and on the calling thread for tools.
#include "print_adapter_p.h"
#include "print_preview_renderer_p.h"

namespace QindaTK {

QString PrintAdapter::previewKey(int pageIndex, double zoom) const
{
    return QStringLiteral("%1@%2").arg(pageIndex).arg(zoom, 0, 'g', 6);
}

double PrintAdapter::previewZoomFor(int pageIndex, double zoom)
{
    if (!(zoom > 0) || zoom > 8) return 0;
    return Print::previewZoom(sheetOf(pageIndex), zoom, PreviewImageByteLimit);
}

QImage PrintAdapter::cachedPreview(int pageIndex, double zoom)
{
    const auto found = m_cache.find(previewKey(pageIndex, previewZoomFor(pageIndex, zoom)));
    if (found == m_cache.end()) return {};
    found->lastUse = ++m_cacheClock;
    return found->image;
}

QImage PrintAdapter::previewImage(int pageIndex, double zoom)
{
    RichTextLayout *pages = layout();
    const double effective = previewZoomFor(pageIndex, zoom);
    if (!pages || pageIndex < 0 || pageIndex >= pages->pageCount() || !(effective > 0)) return {};
    if (const QImage cached = cachedPreview(pageIndex, zoom); !cached.isNull()) return cached;
    const QImage image = Print::renderSheet(*pages, sheetOf(pageIndex), effective,
                                            m_ticket.colorMode == QLatin1String("grayscale"), m_images.get());
    ++m_renderCount;
    cachePreview(previewKey(pageIndex, effective), image);
    emit previewChanged();
    return image;
}

bool PrintAdapter::requestPreview(int pageIndex, double zoom)
{
    // AGENT-GUARD: inside an open transaction the layout is stale (see
    // layout()); the commit's changed() invalidates and views ask again.
    if (!m_document || m_document->inTransaction()) return false;
    RichTextLayout *pages = layout();
    const double effective = previewZoomFor(pageIndex, zoom);
    if (!pages || pageIndex < 0 || pageIndex >= pages->pageCount() || !(effective > 0)) return false;
    if (!cachedPreview(pageIndex, zoom).isNull()) return true;
    const QString key = previewKey(pageIndex, effective);
    if (m_pending.contains(key)) return false;
    if (!m_renderer) {
        m_renderer = std::make_unique<Print::PreviewRenderer>();
        m_renderer->setGeneration(m_generation);
        connect(m_renderer.get(), &Print::PreviewRenderer::rendered, this, &PrintAdapter::onPreviewRendered);
    }
    m_pending.insert(key, pageIndex);
    QPointer<RichTextDocument> document = m_document;
    m_renderer->request(m_generation, [document] { return document ? document->toJson() : QString(); }, key,
                        sheetOf(pageIndex), effective, m_ticket.colorMode == QLatin1String("grayscale"));
    emit previewChanged();
    return false;
}

void PrintAdapter::onPreviewRendered(int generation, const QString &key, const QImage &image)
{
    // A result for an older document or ticket was dropped from m_pending
    // by invalidateOutput and never reaches the cache.
    if (generation != m_generation || !m_pending.contains(key)) return;
    const int page = m_pending.take(key);
    ++m_renderCount;
    cachePreview(key, image);
    emit previewChanged();
    if (!image.isNull()) emit previewReady(page);
}

void PrintAdapter::cachePreview(const QString &key, const QImage &image)
{
    if (image.isNull()) return;
    if (const auto old = m_cache.constFind(key); old != m_cache.constEnd()) m_cacheBytes -= old->image.sizeInBytes();
    m_cache.insert(key, {image, ++m_cacheClock});
    m_cacheBytes += image.sizeInBytes();
    trimCache();
}

void PrintAdapter::setPreviewCacheLimit(int pages)
{
    pages = qBound(1, pages, 256);
    if (pages == m_cacheLimit) return;
    m_cacheLimit = pages;
    trimCache();
    emit previewChanged();
}

void PrintAdapter::trimCache()
{
    // AGENT-GUARD: bounded by count and bytes. Every image is at most
    // PreviewImageByteLimit (a quarter of the byte limit), so the newest one,
    // which has the newest use stamp, always fits and is evicted last.
    while (m_cache.size() > m_cacheLimit || m_cacheBytes > PreviewCacheByteLimit) {
        auto oldest = m_cache.begin();
        for (auto it = m_cache.begin(); it != m_cache.end(); ++it)
            if (it->lastUse < oldest->lastUse) oldest = it;
        m_cacheBytes -= oldest->image.sizeInBytes();
        m_cache.erase(oldest);
    }
}

} // namespace QindaTK
