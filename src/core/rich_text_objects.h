// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "rich_text_layout.h"
#include <QCache>
#include <QImage>
#include <QSet>

namespace QindaTK {

// AGENT-CONTRACT (docs/rich-text.md#objects): the seam through which a host
// draws inline/anchored object content. The document stores only geometry,
// placement, alt text and a source URI; resolving `asset:` sources through
// AssetProvider (WP-017) or editing object geometry on a canvas (WP-040) are
// host adapters that subclass this interface.
class RichTextObjectRenderer {
public:
    virtual ~RichTextObjectRenderer() = default;
    virtual void paint(QPainter *painter, const QRectF &rect, const RichText::RichObject &object,
                       const RichText::PaintContext &context) = 0;
};

// Reference renderer: file:, qrc: and data: image sources through a bounded
// decoded-image cache; any other source, or a failed load, paints a labelled
// placeholder so missing content stays visible and selectable.
class RichTextImageRenderer final : public RichTextObjectRenderer {
public:
    static constexpr int MaximumCacheKiB = 64 * 1024;
    void paint(QPainter *painter, const QRectF &rect, const RichText::RichObject &object,
               const RichText::PaintContext &context) override;
    QImage image(const QString &source);
    int cachedImages() const { return int(m_cache.count()); }
    static void paintPlaceholder(QPainter *painter, const QRectF &rect, const RichText::RichObject &object,
                                 const RichText::PaintContext &context, bool missing);
private:
    QCache<QString, QImage> m_cache{MaximumCacheKiB};
    QSet<QString> m_failed;
};

} // namespace QindaTK
