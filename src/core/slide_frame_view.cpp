// SPDX-License-Identifier: LGPL-3.0-or-later
#include "slide_frame_view.h"
#include "slide_deck.h"
#include "slide_render.h"
#include "slide_show.h"

#include <QKeyEvent>
#include <QPainter>
#include <algorithm>

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
        m_connections << connect(m_show, &SlideShow::annotationsChanged, this, repaint)
                      << connect(m_show, &SlideShow::frameChanged, this, repaint)
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

void SlideFrameView::setKeyNavigation(bool enabled)
{
    if (m_keyNavigation == enabled) return;
    m_keyNavigation = enabled;
    setActiveFocusOnTab(enabled);
    emit sourceChanged();
}

void SlideFrameView::keyPressEvent(QKeyEvent *event)
{
    if (!m_keyNavigation || !m_show) { event->ignore(); return; }
    switch (event->key()) {
    case Qt::Key_Right: case Qt::Key_Down: case Qt::Key_Space: case Qt::Key_PageDown:
        m_show->next(); break;
    case Qt::Key_Left: case Qt::Key_Up: case Qt::Key_PageUp: case Qt::Key_Backspace:
        m_show->previous(); break;
    case Qt::Key_B: case Qt::Key_Period:
        m_show->setBlackedOut(!m_show->blackedOut()); break;
    default:
        event->ignore();
        return;
    }
    event->accept();
}

QRectF SlideFrameView::slideRect() const
{
    const SlideDeck *deck = m_show && m_show->deck() ? m_show->deck() : m_deck.data();
    const QSizeF slide = deck ? deck->state().size : QSizeF(960, 540);
    const qreal scale = std::min(width() / slide.width(), height() / slide.height());
    const QSizeF size = slide * scale;
    return {(width() - size.width()) / 2, (height() - size.height()) / 2, size.width(), size.height()};
}

QPointF SlideFrameView::mapToSlide(const QPointF &point) const
{
    const SlideDeck *deck = m_show && m_show->deck() ? m_show->deck() : m_deck.data();
    const QRectF rect = slideRect();
    if (!deck || rect.isEmpty()) return {};
    const QSizeF slide = deck->state().size;
    return {(point.x() - rect.x()) * slide.width() / rect.width(), (point.y() - rect.y()) * slide.height() / rect.height()};
}

QPointF SlideFrameView::mapFromSlide(const QPointF &point) const
{
    const SlideDeck *deck = m_show && m_show->deck() ? m_show->deck() : m_deck.data();
    const QRectF rect = slideRect();
    if (!deck || rect.isEmpty()) return {};
    const QSizeF slide = deck->state().size;
    return {rect.x() + point.x() * rect.width() / slide.width(), rect.y() + point.y() * rect.height() / slide.height()};
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
