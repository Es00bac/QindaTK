// SPDX-License-Identifier: LGPL-3.0-or-later
#include "slide_preview.h"
#include "slide_deck.h"
#include "slide_timeline.h"

#include <algorithm>

namespace QindaTK {
using namespace Slides;

SlidePreview::SlidePreview(QObject *parent) : QObject(parent)
{
    // Same pacing rule as SlideShow: the timer only paces redraws, time is
    // monotonic nanoseconds converted to exact ticks once per run.
    m_timer.setInterval(16);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &SlidePreview::tick);
}

void SlidePreview::setDeck(SlideDeck *deck)
{
    if (m_deck == deck) return;
    disconnect(m_deckConnection);
    m_deck = deck;
    // A deck edit invalidates the sampled step: stop at the complete slide.
    if (deck) m_deckConnection = connect(deck, &SlideDeck::changed, this, [this] { if (m_state == QStringLiteral("running")) cancel(); });
    reset();
    emit changed();
}

void SlidePreview::setLiveClock(bool live)
{
    if (m_liveClock == live) return;
    m_liveClock = live;
    if (!live) m_timer.stop();
    emit changed();
}

bool SlidePreview::fail(const QString &message)
{
    m_error = message;
    emit changed();
    return false;
}

void SlidePreview::setState(const QString &state)
{
    m_state = state;
    const bool live = m_liveClock && state == QStringLiteral("running");
    if (live && !m_timer.isActive()) {
        m_monotonic.start();
        m_lastNanoseconds = 0;
        m_timer.start();
    } else if (!live) {
        m_timer.stop();
    }
    emit changed();
    emit frameChanged();
}

bool SlidePreview::start(const QString &effectId)
{
    if (!m_deck) return fail(QStringLiteral("No slide deck"));
    for (const Slide &slide : m_deck->state().slides) {
        const SlideSchedule plan = schedule(slide);
        for (const EffectTiming &timing : plan.effects) {
            if (timing.effectId != effectId) continue;
            m_effectId = effectId;
            m_slideId = slide.id;
            m_step = timing.step;
            m_ticks = 0;
            m_end = plan.stepDurations.value(timing.step);
            m_error.clear();
            setState(m_end > 0 ? QStringLiteral("running") : QStringLiteral("finished"));
            return true;
        }
    }
    return fail(QStringLiteral("Unknown effect"));
}

void SlidePreview::cancel()
{
    if (m_state != QStringLiteral("running")) return;
    m_ticks = m_end; // the build complete, as a click in the show gives
    setState(QStringLiteral("canceled"));
    emit finished(true);
}

void SlidePreview::reset()
{
    m_ticks = 0;
    m_end = 0;
    m_effectId.clear();
    setState(QStringLiteral("idle"));
}

bool SlidePreview::advance(const QString &ticks)
{
    const auto parsed = TimelineTime::parseDecimal(ticks);
    if (!parsed) return fail(QStringLiteral("Ticks are a decimal integer"));
    return advanceTicks(*parsed);
}

bool SlidePreview::advanceTicks(qint64 ticks)
{
    if (ticks < 0) return fail(QStringLiteral("Time only moves forward"));
    if (m_state != QStringLiteral("running")) return false;
    m_ticks = std::min(m_end, m_ticks + ticks);
    if (m_ticks >= m_end) {
        setState(QStringLiteral("finished"));
        emit finished(false);
    } else {
        emit frameChanged();
    }
    return true;
}

void SlidePreview::tick()
{
    const qint64 now = m_monotonic.nsecsElapsed();
    const auto total = TimelineTime::mulDiv(now, quint64(TicksPerSecond), 1000000000ull, TimeMap::Floor);
    const auto applied = TimelineTime::mulDiv(m_lastNanoseconds, quint64(TicksPerSecond), 1000000000ull, TimeMap::Floor);
    m_lastNanoseconds = now;
    if (total && applied && *total > *applied) advanceTicks(*total - *applied);
}

} // namespace QindaTK
