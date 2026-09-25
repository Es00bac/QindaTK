// SPDX-License-Identifier: LGPL-3.0-or-later
#include "slide_show.h"
#include "slide_deck.h"
#include "slide_render.h"

#include <algorithm>

namespace QindaTK {
using namespace Slides;

SlideShow::SlideShow(QObject *parent) : QObject(parent)
{
    // AGENT-NOTE: 16 ms only paces redraws; positions come from monotonic
    // nanoseconds converted to exact ticks, so a late frame skips ahead
    // instead of slowing the show down.
    m_frameTimer.setInterval(16);
    m_frameTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_frameTimer, &QTimer::timeout, this, &SlideShow::tick);
}

void SlideShow::setDeck(SlideDeck *deck)
{
    if (m_deck == deck) return;
    disconnect(m_deckConnection);
    m_deck = deck;
    if (deck) m_deckConnection = connect(deck, &SlideDeck::changed, this, &SlideShow::deckContentChanged);
    stop();
    m_slideId = deck && !shownSlides().isEmpty() ? shownSlides().first() : QString();
    m_step = 0;
    m_ticks = 0;
    m_fromSlideId.clear();
    emit deckChanged();
    emit positionChanged();
    emit frameChanged();
}

QStringList SlideShow::shownSlides() const
{
    QStringList out;
    if (!m_deck) return out;
    for (const Slide &s : m_deck->state().slides) if (!s.hidden) out << s.id;
    return out;
}

int SlideShow::currentIndex() const
{
    if (!m_deck) return 0;
    int shownBefore = 0;
    for (const Slide &s : m_deck->state().slides) {
        if (s.id == m_slideId) return shownBefore;
        if (!s.hidden) ++shownBefore;
    }
    return 0;
}

QString SlideShow::nextSlideId() const
{
    if (!m_deck) return {};
    const auto &slides = m_deck->state().slides;
    for (int i = slideIndex(m_deck->state(), m_slideId) + 1; i > 0 && i < slides.size(); ++i)
        if (!slides.at(i).hidden) return slides.at(i).id;
    return {};
}

QString SlideShow::previousSlideId() const
{
    if (!m_deck) return {};
    const auto &slides = m_deck->state().slides;
    for (int i = slideIndex(m_deck->state(), m_slideId) - 1; i >= 0; --i)
        if (!slides.at(i).hidden) return slides.at(i).id;
    return {};
}

QString SlideShow::currentTitle() const
{
    const int i = m_deck ? slideIndex(m_deck->state(), m_slideId) : -1;
    return i < 0 ? QString() : slideTitle(m_deck->state(), m_deck->state().slides.at(i));
}

QString SlideShow::nextTitle() const
{
    const QString id = nextSlideId();
    const int i = m_deck && !id.isEmpty() ? slideIndex(m_deck->state(), id) : -1;
    return i < 0 ? QString() : slideTitle(m_deck->state(), m_deck->state().slides.at(i));
}

QString SlideShow::speakerNotes() const { return m_deck ? m_deck->notes(m_slideId) : QString(); }

int SlideShow::buildCount() const
{
    const int i = m_deck ? slideIndex(m_deck->state(), m_slideId) : -1;
    return i < 0 ? 0 : schedule(m_deck->state().slides.at(i)).buildCount;
}

bool SlideShow::animating() const
{
    const int i = m_deck ? slideIndex(m_deck->state(), m_slideId) : -1;
    if (i < 0 || !m_running) return false;
    return m_ticks < schedule(m_deck->state().slides.at(i)).stepDurations.value(m_step);
}

SlideFrame SlideShow::currentFrame() const
{
    SlideFrame frame;
    if (m_deck && !m_slideId.isEmpty())
        frame = evaluate(m_deck->state(), m_slideId, m_step, m_ticks, m_fromSlideId);
    frame.blackedOut = m_blackedOut || m_ended || !m_deck || m_slideId.isEmpty();
    return frame;
}

QString SlideShow::elapsedText() const
{
    const qint64 seconds = m_elapsed / TicksPerSecond;
    const qint64 h = seconds / 3600, m = (seconds / 60) % 60, s = seconds % 60;
    const QString ms = QStringLiteral("%1:%2").arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    return h > 0 ? QStringLiteral("%1:%2").arg(h).arg(ms) : ms;
}

void SlideShow::setBlackedOut(bool blackedOut)
{
    if (m_blackedOut == blackedOut) return;
    m_blackedOut = blackedOut;
    emit stateChanged();
    emit frameChanged();
}

void SlideShow::setLiveClock(bool live)
{
    if (m_liveClock == live) return;
    m_liveClock = live;
    updateTimer();
    emit stateChanged();
}

bool SlideShow::fail(const QString &message)
{
    m_error = message;
    emit stateChanged();
    return false;
}

void SlideShow::place(QString slideId, int step, qint64 ticks, QString from)
{
    m_slideId = std::move(slideId);
    m_step = step;
    m_ticks = ticks;
    m_fromSlideId = std::move(from);
    m_ended = false;
    m_error.clear();
    updateTimer();
    emit stateChanged();
    emit positionChanged();
    emit frameChanged();
}

bool SlideShow::start(const QString &slideId)
{
    if (!m_deck) return fail(QStringLiteral("No slide deck"));
    const QString first = slideId.isEmpty() ? shownSlides().value(0) : slideId;
    if (first.isEmpty() || slideIndex(m_deck->state(), first) < 0) return fail(QStringLiteral("No slide to show"));
    m_running = true;
    m_elapsed = 0;
    m_timerState = QStringLiteral("running");
    emit clockChanged();
    place(first, 0, 0, {});
    return true;
}

void SlideShow::stop()
{
    const bool changed = m_running || m_timerState != QStringLiteral("stopped");
    m_running = false;
    m_ended = false;
    m_timerState = QStringLiteral("stopped");
    updateTimer();
    if (!changed) return;
    emit stateChanged();
    emit clockChanged();
    emit frameChanged();
}

bool SlideShow::next()
{
    if (!m_running || !m_deck) return fail(QStringLiteral("The show is not running"));
    if (m_ended) return false;
    const int i = slideIndex(m_deck->state(), m_slideId);
    if (i < 0) return fail(QStringLiteral("The current slide no longer exists"));
    const SlideSchedule plan = schedule(m_deck->state().slides.at(i));
    const qint64 end = plan.stepDurations.value(m_step);
    if (m_ticks < end) { // a click completes the running build first
        place(m_slideId, m_step, end, m_fromSlideId);
        return true;
    }
    if (m_step < plan.buildCount) {
        place(m_slideId, m_step + 1, 0, {});
        return true;
    }
    const QString following = nextSlideId();
    if (following.isEmpty()) {
        m_ended = true;
        updateTimer();
        emit stateChanged();
        emit frameChanged();
        emit finished();
        return false;
    }
    place(following, 0, 0, m_slideId);
    return true;
}

bool SlideShow::previous()
{
    if (!m_running || !m_deck) return fail(QStringLiteral("The show is not running"));
    if (m_ended) { place(m_slideId, m_step, m_ticks, {}); return true; }
    const int i = slideIndex(m_deck->state(), m_slideId);
    if (i < 0) return fail(QStringLiteral("The current slide no longer exists"));
    if (m_step > 0) {
        const SlideSchedule plan = schedule(m_deck->state().slides.at(i));
        place(m_slideId, m_step - 1, plan.stepDurations.value(m_step - 1), {});
        return true;
    }
    const QString before = previousSlideId();
    if (before.isEmpty()) return false;
    const SlideSchedule plan = schedule(m_deck->state().slides.at(slideIndex(m_deck->state(), before)));
    place(before, plan.buildCount, plan.stepDurations.value(plan.buildCount), {});
    return true;
}

bool SlideShow::goToSlide(const QString &slideId)
{
    if (!m_running || !m_deck) return fail(QStringLiteral("The show is not running"));
    if (slideIndex(m_deck->state(), slideId) < 0) return fail(QStringLiteral("Unknown slide"));
    place(slideId, 0, 0, slideId == m_slideId ? QString() : m_slideId);
    return true;
}

bool SlideShow::goToIndex(int index)
{
    const QStringList shown = shownSlides();
    if (index < 0 || index >= shown.size()) return fail(QStringLiteral("No slide at that position"));
    return goToSlide(shown.at(index));
}

bool SlideShow::advance(const QString &ticks)
{
    const auto parsed = TimelineTime::parseDecimal(ticks);
    if (!parsed) return fail(QStringLiteral("Ticks are a decimal integer"));
    return advanceTicks(*parsed);
}

bool SlideShow::advanceMilliseconds(int milliseconds)
{
    if (milliseconds < 0) return fail(QStringLiteral("Time only moves forward"));
    return advanceTicks(qint64(milliseconds) * (TicksPerSecond / 1000));
}

bool SlideShow::advanceTicks(qint64 ticks)
{
    if (ticks < 0) return fail(QStringLiteral("Time only moves forward"));
    if (!m_running || !m_deck) return fail(QStringLiteral("The show is not running"));
    if (m_timerState == QStringLiteral("running")) {
        m_elapsed += ticks;
        emit clockChanged();
    }
    const int i = slideIndex(m_deck->state(), m_slideId);
    if (i < 0 || m_ended) return true;
    // Past the step's end every frame is identical, so position saturates.
    const qint64 end = schedule(m_deck->state().slides.at(i)).stepDurations.value(m_step);
    const qint64 next = m_ticks >= end ? m_ticks : std::min(end, m_ticks + ticks);
    if (next != m_ticks) {
        m_ticks = next;
        if (m_ticks >= end) updateTimer();
        emit frameChanged();
    }
    return true;
}

bool SlideShow::timerAction(const QString &action)
{
    if (action == QStringLiteral("start") || action == QStringLiteral("resume")) {
        if (m_timerState == QStringLiteral("stopped")) m_elapsed = 0;
        m_timerState = QStringLiteral("running");
    } else if (action == QStringLiteral("pause")) {
        if (m_timerState != QStringLiteral("running")) return false;
        m_timerState = QStringLiteral("paused");
    } else if (action == QStringLiteral("reset")) {
        m_elapsed = 0;
        m_timerState = m_running ? QStringLiteral("running") : QStringLiteral("stopped");
    } else {
        return fail(QStringLiteral("Unknown timer action %1").arg(action));
    }
    updateTimer();
    emit clockChanged();
    return true;
}

QImage SlideShow::renderFrame(int width, int height) const
{
    return m_deck ? renderImage(m_deck->state(), currentFrame(), QSize(width, height)) : QImage();
}

void SlideShow::deckContentChanged()
{
    if (!m_deck) return;
    const DeckState &state = m_deck->state();
    if (slideIndex(state, m_slideId) < 0) {
        // The shown slide was deleted (or undone away): continue at the first shown slide.
        m_slideId = shownSlides().value(0);
        m_step = 0;
        m_ticks = 0;
    }
    if (slideIndex(state, m_fromSlideId) < 0) m_fromSlideId.clear();
    const int i = slideIndex(state, m_slideId);
    if (i >= 0) {
        const SlideSchedule plan = schedule(state.slides.at(i));
        m_step = std::min(m_step, plan.buildCount);
        m_ticks = std::min(m_ticks, plan.stepDurations.value(m_step));
    }
    updateTimer();
    emit positionChanged();
    emit frameChanged();
}

void SlideShow::updateTimer()
{
    const bool needed = m_liveClock && m_running && !m_ended
        && (animating() || m_timerState == QStringLiteral("running"));
    if (needed == m_frameTimer.isActive()) return;
    if (needed) {
        m_monotonic.start();
        m_lastNanoseconds = 0;
        m_frameTimer.start();
    } else {
        m_frameTimer.stop();
    }
}

void SlideShow::tick()
{
    // Exact ticks for the total monotonic time, minus what was applied:
    // one rounding for the whole run, so there is no drift.
    const qint64 now = m_monotonic.nsecsElapsed();
    const auto total = TimelineTime::mulDiv(now, quint64(TicksPerSecond), 1000000000ull, TimeMap::Floor);
    const auto applied = TimelineTime::mulDiv(m_lastNanoseconds, quint64(TicksPerSecond), 1000000000ull, TimeMap::Floor);
    m_lastNanoseconds = now;
    if (total && applied && *total > *applied) advanceTicks(*total - *applied);
    updateTimer();
}

} // namespace QindaTK
