// SPDX-License-Identifier: LGPL-3.0-or-later
#include "motion_onion_skin_item.h"
#include "motion_composition.h"
#include "motion_frame_renderer.h"
#include <QPainter>
#include <algorithm>

namespace QindaTK {
MotionOnionSkinItem::MotionOnionSkinItem(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    connect(this, &MotionOnionSkinItem::requestChanged, this, &MotionOnionSkinItem::markStale);
}
void MotionOnionSkinItem::setRenderer(MotionFrameRenderer *v)
{
    if (m_renderer == v) return;
    disconnect(m_rendererLink);
    m_renderer = v;
    if (v) m_rendererLink = connect(v, &MotionFrameRenderer::contentInvalidated, this, &MotionOnionSkinItem::markStale);
    emit rendererChanged();
    markStale();
}
void MotionOnionSkinItem::markStale()
{
    if (!m_stale) { m_stale = true; emit staleChanged(); }
    if (m_refreshQueued || !m_renderer) return;
    m_refreshQueued = true;
    QMetaObject::invokeMethod(this, [this] {
        m_refreshQueued = false;
        if (m_stale) refresh();
    }, Qt::QueuedConnection);
}
#define STRING_SETTER(Method, Member) void MotionOnionSkinItem::Method(const QString &v) { if (Member == v) return; Member = v; emit requestChanged(); }
STRING_SETTER(setCurrentTicks, m_currentTicks)
STRING_SETTER(setPreviousTicks, m_previousTicks)
STRING_SETTER(setNextTicks, m_nextTicks)
#undef STRING_SETTER
void MotionOnionSkinItem::setFrameWidth(int v) { v = std::clamp(v, 1, 4096); if (m_width == v) return; m_width = v; emit requestChanged(); }
void MotionOnionSkinItem::setFrameHeight(int v) { v = std::clamp(v, 1, 4096); if (m_height == v) return; m_height = v; emit requestChanged(); }
void MotionOnionSkinItem::setPreviousTint(const QColor &v) { if (m_previousTint == v) return; m_previousTint = v; updateTints(); emit styleChanged(); update(); }
void MotionOnionSkinItem::setNextTint(const QColor &v) { if (m_nextTint == v) return; m_nextTint = v; updateTints(); emit styleChanged(); update(); }
void MotionOnionSkinItem::setNeighborOpacity(qreal v) { v = std::clamp(v, 0.0, 1.0); if (qFuzzyCompare(m_opacity, v)) return; m_opacity = v; emit styleChanged(); update(); }

bool MotionOnionSkinItem::refresh()
{
    if (!m_renderer || m_currentTicks.isEmpty() || m_previousTicks.isEmpty() || m_nextTicks.isEmpty()
        || qint64(m_width) * m_height > 4 * 1024 * 1024) {
        m_error = QStringLiteral("Renderer, three exact tick strings and a frame up to 4 megapixels are required.");
        emit refreshed(); update(); return false;
    }
    // Each image is copied before the shared renderer advances to the next
    // exact time; the retained set is replaced only when all three succeed.
    QString error;
    const auto capture = [this, &error](const QString &ticks, QImage *out) {
        const QVariantMap result = m_renderer->render(ticks, m_width, m_height);
        if (!result.value(QStringLiteral("ok")).toBool()) { error = result.value(QStringLiteral("error")).toString(); return false; }
        *out = m_renderer->frame().copy();
        return !out->isNull();
    };
    QImage previous, next, current;
    if (!capture(m_previousTicks, &previous) || !capture(m_nextTicks, &next) || !capture(m_currentTicks, &current)) {
        m_error = error.isEmpty() ? QStringLiteral("Could not retain a rendered frame.") : error;
        emit refreshed(); update(); return false;
    }
    m_previous = previous; m_next = next; m_current = current;
    m_error.clear();
    updateTints();
    if (m_stale) { m_stale = false; emit staleChanged(); }
    emit refreshed(); update(); return true;
}

QImage MotionOnionSkinItem::tinted(const QImage &source, const QColor &color) const
{
    if (source.isNull()) return {};
    QImage result(source.size(), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    QPainter p(&result);
    p.drawImage(0, 0, source);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(result.rect(), color);
    return result;
}
void MotionOnionSkinItem::updateTints()
{
    m_tintedPrevious = tinted(m_previous, m_previousTint);
    m_tintedNext = tinted(m_next, m_nextTint);
}
void MotionOnionSkinItem::paint(QPainter *p)
{
    if (m_current.isNull()) return;
    p->setRenderHint(QPainter::SmoothPixmapTransform);
    const QSizeF target = QSizeF(width(), height());
    const qreal scale = std::min(target.width() / m_current.width(), target.height() / m_current.height());
    const QSizeF fitted(m_current.width() * scale, m_current.height() * scale);
    const QRectF rect((target.width() - fitted.width()) / 2, (target.height() - fitted.height()) / 2, fitted.width(), fitted.height());
    p->save(); p->setOpacity(m_opacity); p->drawImage(rect, m_tintedPrevious);
    p->drawImage(rect, m_tintedNext); p->restore();
    p->drawImage(rect, m_current);
}
} // namespace QindaTK
