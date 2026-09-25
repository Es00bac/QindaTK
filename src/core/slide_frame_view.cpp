// SPDX-License-Identifier: LGPL-3.0-or-later
#include "slide_frame_view.h"
#include "slide_deck.h"
#include "slide_render.h"
#include "slide_show.h"

#include <QPainter>

namespace QindaTK {
using namespace Slides;

namespace {
SlideFrame staticFrame(const SlideDeck &deck, const QString &slideId, int step, const QString &ticks)
{
    if (step < 0) return finalFrame(deck.state(), slideId);
    const auto parsed = TimelineTime::parseDecimal(ticks.isEmpty() ? QStringLiteral("0") : ticks);
    return evaluate(deck.state(), slideId, step, parsed.value_or(0));
}
} // namespace

SlideFrameView::SlideFrameView(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setOpaquePainting(true);
    setAntialiasing(true);
}

void SlideFrameView::reconnect()
{
    for (const auto &c : m_connections) disconnect(c);
    m_connections.clear();
    auto repaint = [this] { update(); };
    if (m_show) {
        m_connections << connect(m_show, &SlideShow::frameChanged, this, repaint)
                      << connect(m_show, &SlideShow::deckChanged, this, repaint)
                      << connect(m_show, &SlideShow::stateChanged, this, repaint);
    }
    if (m_deck) m_connections << connect(m_deck, &SlideDeck::changed, this, repaint);
    update();
    emit sourceChanged();
}

void SlideFrameView::setShow(SlideShow *show)
{
    if (m_show == show) return;
    m_show = show;
    reconnect();
}

void SlideFrameView::setDeck(SlideDeck *deck)
{
    if (m_deck == deck) return;
    m_deck = deck;
    reconnect();
}

void SlideFrameView::setSlideId(const QString &id)
{
    if (m_slideId == id) return;
    m_slideId = id;
    update();
    emit sourceChanged();
}

void SlideFrameView::setStep(int step)
{
    if (m_step == step) return;
    m_step = step;
    update();
    emit sourceChanged();
}

void SlideFrameView::setTicks(const QString &ticks)
{
    if (m_ticks == ticks) return;
    m_ticks = ticks;
    update();
    emit sourceChanged();
}

QImage SlideFrameView::grabFrame(int width, int height) const
{
    if (m_show && m_show->deck()) return renderImage(m_show->deck()->state(), m_show->currentFrame(), QSize(width, height));
    if (m_deck) return renderImage(m_deck->state(), staticFrame(*m_deck, m_slideId, m_step, m_ticks), QSize(width, height));
    return {};
}

void SlideFrameView::paint(QPainter *painter)
{
    const QRectF target(0, 0, width(), height());
    QString shown;
    if (m_show && m_show->deck()) {
        const SlideFrame frame = m_show->currentFrame();
        paintFrame(*painter, m_show->deck()->state(), frame, target);
        shown = frame.blackedOut ? QString() : frame.slideId;
    } else if (m_deck) {
        const SlideFrame frame = staticFrame(*m_deck, m_slideId, m_step, m_ticks);
        paintFrame(*painter, m_deck->state(), frame, target);
        shown = m_slideId;
    } else {
        painter->fillRect(target, Qt::black);
    }
    m_displayed = shown;
    ++m_paintCount;
    emit painted();
}

} // namespace QindaTK
