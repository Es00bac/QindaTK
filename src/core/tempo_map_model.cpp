// SPDX-License-Identifier: LGPL-3.0-or-later
#include "tempo_map_model.h"

#include "timeline_time.h"

#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {

QUuid initialId(const char *name)
{
    static const QUuid ns(QStringLiteral("6f1c2c1e-4b0a-5a51-9b43-49a3b9f5e049"));
    return QUuid::createUuidV5(ns, QString::fromLatin1(name));
}

qint64 floorDiv(qint64 value, qint64 divisor)
{
    qint64 quotient = value / divisor;
    if (value % divisor != 0 && ((value < 0) != (divisor < 0))) --quotient;
    return quotient;
}

OperationError invalid(const QString &message, const QString &field = {})
{
    return {ErrorCode::Validation, message, field, {}};
}

int spanFor(const QVector<TempoMapModel::MeterSpan> &spans, qint64 tick)
{
    int index = 0;
    for (int i = 1; i < spans.size() && spans.at(i).tick <= tick; ++i) index = i;
    return index;
}

} // namespace

TempoMapModel::TempoMapModel(QObject *parent)
    : QObject(parent), m_events(defaultEvents())
{
}

TempoMapModel::~TempoMapModel()
{
    disconnect(m_timeMapConnection);
}

QVector<TempoEvent> TempoMapModel::defaultEvents()
{
    TempoEvent tempo;
    tempo.id = initialId("tempo");
    TempoEvent meter;
    meter.id = initialId("meter");
    meter.kind = TempoEvent::Kind::Meter;
    return {meter, tempo};
}

void TempoMapModel::sortEvents(QVector<TempoEvent> &events)
{
    std::sort(events.begin(), events.end(), [](const TempoEvent &a, const TempoEvent &b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        if (a.kind != b.kind) return a.kind == TempoEvent::Kind::Meter;
        return a.id < b.id;
    });
}

void TempoMapModel::setTimeMap(TimeMap *map)
{
    if (m_timeMap == map) return;
    if (m_session) cancel(m_session->operationId);
    disconnect(m_timeMapConnection);
    m_timeMap = map;
    if (map) {
        m_timeMapConnection = connect(map, &TimeMap::changed, this, &TempoMapModel::onTimeMapChanged);
        if (auto failure = publish(m_events)) fail(failure);
    }
    emit timeMapChanged();
    emit eventsChanged();
}

void TempoMapModel::setTimeline(TimelineModel *timeline)
{
    if (m_timeline == timeline) return;
    m_timeline = timeline;
    emit timelineChanged();
    emit eventsChanged();
}

void TempoMapModel::onTimeMapChanged()
{
    // Our own publish needs no echo. Another change (sample rate, PPQ, origin)
    // moves converted positions, so the rows are rebuilt.
    if (!m_publishing) emit eventsChanged();
}

void TempoMapModel::setMinimumBpm(double bpm)
{
    if (!std::isfinite(bpm) || bpm <= 0 || bpm >= m_maximumBpm || bpm == m_minimumBpm) return;
    m_minimumBpm = bpm;
    emit settingsChanged();
}

void TempoMapModel::setMaximumBpm(double bpm)
{
    if (!std::isfinite(bpm) || bpm <= m_minimumBpm || bpm == m_maximumBpm) return;
    m_maximumBpm = bpm;
    emit settingsChanged();
}

std::pair<qint64, qint64> TempoMapModel::microsecondRange() const
{
    const qint64 low = qint64(std::ceil(60000000.0 / m_maximumBpm));
    const qint64 high = qint64(std::floor(60000000.0 / m_minimumBpm));
    return {std::clamp<qint64>(low, 1, 60000000), std::clamp<qint64>(high, 1, 60000000)};
}

QVariantMap TempoMapModel::capabilities() const
{
    const auto entry = [](bool available, const QString &reason) {
        return QVariantMap{{QStringLiteral("available"), available}, {QStringLiteral("reason"), reason}};
    };
    return {{QStringLiteral("tempoSteps"), entry(true, {})},
            {QStringLiteral("meterChanges"), entry(true, {})},
            {QStringLiteral("tempoRamps"), entry(false, tr("TimeMap tempo segments are constant; "
                                                           "ramped tempo needs a TimeMap extension"))}};
}

QVector<TempoEvent> TempoMapModel::effectiveEvents() const
{
    return m_session && m_session->hasPreview ? m_session->preview : m_events;
}

const TempoEvent *TempoMapModel::event(const QUuid &id) const
{
    for (const auto &event : m_events)
        if (event.id == id) return &event;
    return nullptr;
}

OperationError TempoMapModel::validate(const QVector<TempoEvent> &source) const
{
    if (source.size() > kMaxEvents)
        return invalid(tr("A tempo map holds at most %1 events").arg(kMaxEvents));
    QVector<TempoEvent> events = source;
    sortEvents(events);
    const int ppq = ticksPerQuarter();
    const auto [lowest, highest] = microsecondRange();
    QSet<QUuid> ids;
    QSet<qint64> tempoTicks, meterTicks, allTicks;
    const TempoEvent *previousMeter = nullptr;
    for (const auto &event : events) {
        if (event.id.isNull() || ids.contains(event.id))
            return invalid(tr("Tempo event IDs must be unique UUIDs"), QStringLiteral("id"));
        ids.insert(event.id);
        if (event.tick < 0) return invalid(tr("A tempo event cannot lie before bar 1"), QStringLiteral("tick"));
        allTicks.insert(event.tick);
        if (event.kind == TempoEvent::Kind::Tempo) {
            if (tempoTicks.contains(event.tick))
                return invalid(tr("Two tempo changes share one position"), QStringLiteral("tick"));
            tempoTicks.insert(event.tick);
            if (event.microsecondsPerQuarter < lowest || event.microsecondsPerQuarter > highest)
                return invalid(tr("Tempo must be %1 to %2 BPM").arg(m_minimumBpm).arg(m_maximumBpm),
                               QStringLiteral("microsecondsPerQuarter"));
            continue;
        }
        if (meterTicks.contains(event.tick))
            return invalid(tr("Two meter changes share one position"), QStringLiteral("tick"));
        meterTicks.insert(event.tick);
        const int d = event.denominator;
        if (event.numerator < 1 || event.numerator > 64 || d < 1 || d > 64 || (d & (d - 1)) != 0
            || (qint64(4) * ppq) % d != 0)
            return invalid(tr("Meter needs 1 to 64 beats of a power-of-two note value"), QStringLiteral("meter"));
        if (previousMeter) {
            const qint64 bar = qint64(4) * ppq / previousMeter->denominator * previousMeter->numerator;
            if ((event.tick - previousMeter->tick) % bar != 0)
                return invalid(tr("A meter change must fall on a bar line (%1)")
                                   .arg(positionText(event.tick, meterSpans(events, event.id))),
                               QStringLiteral("tick"));
        }
        previousMeter = &event;
    }
    if (!tempoTicks.contains(0)) return invalid(tr("The tempo map needs a tempo at bar 1"));
    if (!meterTicks.contains(0)) return invalid(tr("The tempo map needs a meter at bar 1"));
    if (allTicks.size() > kMaxEvents) return invalid(tr("The tempo map has too many segments"));
    return {};
}

QVariantList TempoMapModel::segmentsFor(const QVector<TempoEvent> &source) const
{
    QVector<TempoEvent> events = source;
    sortEvents(events);
    QVariantList segments;
    qint64 tempo = 500000;
    int numerator = 4, denominator = 4;
    for (qsizetype i = 0; i < events.size();) {
        const qint64 tick = events.at(i).tick;
        for (; i < events.size() && events.at(i).tick == tick; ++i) {
            const auto &event = events.at(i);
            if (event.kind == TempoEvent::Kind::Tempo) tempo = event.microsecondsPerQuarter;
            else {
                numerator = event.numerator;
                denominator = event.denominator;
            }
        }
        segments.push_back(QVariantMap{{QStringLiteral("tick"), QString::number(tick)},
                                       {QStringLiteral("microsecondsPerQuarter"), QString::number(tempo)},
                                       {QStringLiteral("numerator"), numerator},
                                       {QStringLiteral("denominator"), denominator}});
    }
    return segments;
}

OperationError TempoMapModel::publish(const QVector<TempoEvent> &events)
{
    if (!m_timeMap) return {};
    const QVariantList segments = segmentsFor(events);
    {
        QScopedValueRollback guard(m_publishing, true);
        m_timeMap->setTempoSegments(segments);
    }
    if (m_timeMap->tempoSegments() != segments)
        return {ErrorCode::BackendFailure, tr("The time map rejected the tempo map: %1").arg(m_timeMap->lastError()),
                {}, {}};
    // Beat-locked clips were re-derived synchronously by the timeline.
    return m_timeline ? m_timeline->musicalPlacementError() : OperationError{};
}

QVector<TempoMapModel::MeterSpan> TempoMapModel::meterSpans(const QVector<TempoEvent> &events,
                                                            const QUuid &exclude) const
{
    const qint64 whole = qint64(4) * ticksPerQuarter();
    QVector<MeterSpan> spans;
    for (const auto &event : events) {
        if (event.kind != TempoEvent::Kind::Meter || event.id == exclude || event.denominator < 1) continue;
        spans.push_back({event.tick, 0, std::max<qint64>(1, whole / event.denominator), event.numerator});
    }
    std::sort(spans.begin(), spans.end(), [](const auto &a, const auto &b) { return a.tick < b.tick; });
    if (spans.isEmpty() || spans.first().tick != 0) spans.prepend({0, 0, std::max<qint64>(1, whole / 4), 4});
    for (int i = 1; i < spans.size(); ++i) {
        const auto &previous = spans.at(i - 1);
        spans[i].bar = previous.bar + floorDiv(spans.at(i).tick - previous.tick,
                                               previous.beatTicks * previous.numerator);
    }
    return spans;
}

QString TempoMapModel::positionText(qint64 tick, const QVector<MeterSpan> &spans)
{
    const auto &span = spans.at(spanFor(spans, tick));
    const qint64 barTicks = span.beatTicks * span.numerator;
    const qint64 bars = floorDiv(tick - span.tick, barTicks);
    const qint64 within = tick - span.tick - bars * barTicks;
    return QStringLiteral("%1.%2.%3").arg(span.bar + bars + 1).arg(within / span.beatTicks + 1)
        .arg(within % span.beatTicks, 3, 10, QLatin1Char('0'));
}

QString TempoMapModel::bpmText(qint64 microsecondsPerQuarter)
{
    const auto milli = microsecondsPerQuarter > 0
        ? TimelineTime::mulDiv(60000000000LL, 1, quint64(microsecondsPerQuarter), TimeMap::NearestAway)
        : std::nullopt;
    if (!milli) return QString();
    return QStringLiteral("%1.%2").arg(*milli / 1000).arg(*milli % 1000, 3, 10, QLatin1Char('0'));
}

QVariantMap TempoMapModel::eventRow(const TempoEvent &event, const QVector<MeterSpan> &spans,
                                    bool preview) const
{
    const bool tempo = event.kind == TempoEvent::Kind::Tempo;
    const QString position = positionText(event.tick, spans);
    const QString value = tempo ? tr("%1 BPM").arg(bpmText(event.microsecondsPerQuarter))
                                : QStringLiteral("%1/%2").arg(event.numerator).arg(event.denominator);
    return {{QStringLiteral("id"), event.id.toString(QUuid::WithoutBraces)},
            {QStringLiteral("kind"), tempo ? QStringLiteral("tempo") : QStringLiteral("meter")},
            {QStringLiteral("tick"), QString::number(event.tick)},
            {QStringLiteral("microsecondsPerQuarter"), QString::number(event.microsecondsPerQuarter)},
            {QStringLiteral("bpm"), 60000000.0 / double(event.microsecondsPerQuarter)},
            {QStringLiteral("bpmText"), bpmText(event.microsecondsPerQuarter)},
            {QStringLiteral("numerator"), event.numerator},
            {QStringLiteral("denominator"), event.denominator},
            {QStringLiteral("valueText"), value},
            {QStringLiteral("initial"), event.tick == 0},
            {QStringLiteral("position"), position},
            {QStringLiteral("positionTicks"), m_timeline ? m_timeline->ticksAtMusicalTick(QString::number(event.tick))
                                                         : QString()},
            {QStringLiteral("preview"), preview},
            {QStringLiteral("label"), tempo ? tr("Tempo %1 at %2").arg(value, position)
                                            : tr("Meter %1 at %2").arg(value, position)}};
}

QVariantList TempoMapModel::events() const
{
    QVector<TempoEvent> events = effectiveEvents();
    sortEvents(events);
    const auto spans = meterSpans(events);
    QVariantList rows;
    rows.reserve(events.size());
    for (const auto &event : events) {
        const TempoEvent *committed = this->event(event.id);
        rows.push_back(eventRow(event, spans, !committed || !(*committed == event)));
    }
    return rows;
}

QVariantMap TempoMapModel::eventData(const QString &id) const
{
    QVector<TempoEvent> events = effectiveEvents();
    const auto spans = meterSpans(events);
    for (const auto &event : events)
        if (event.id == QUuid(id)) return eventRow(event, spans, false);
    return {};
}

QStringList TempoMapModel::eventIds() const
{
    QVector<TempoEvent> events = effectiveEvents();
    sortEvents(events);
    QStringList ids;
    for (const auto &event : events) ids.push_back(event.id.toString(QUuid::WithoutBraces));
    return ids;
}

QString TempoMapModel::adjacentEvent(const QString &fromId, int direction) const
{
    const QStringList ids = eventIds();
    if (ids.isEmpty()) return {};
    const qsizetype index = ids.indexOf(fromId);
    if (index < 0) return direction < 0 ? ids.last() : ids.first();
    return ids.value(std::clamp<qsizetype>(index + (direction < 0 ? -1 : 1), 0, ids.size() - 1));
}

QVariantMap TempoMapModel::tempoAt(const QString &tickText) const
{
    const auto tick = TimelineTime::parseDecimal(tickText);
    if (!tick) return {{QStringLiteral("ok"), false}};
    QVector<TempoEvent> events = effectiveEvents();
    sortEvents(events);
    const TempoEvent *found = nullptr;
    for (const auto &event : events)
        if (event.kind == TempoEvent::Kind::Tempo && (event.tick <= *tick || !found)) found = &event;
    if (!found) return {{QStringLiteral("ok"), false}};
    return {{QStringLiteral("ok"), true}, {QStringLiteral("eventId"), found->id.toString(QUuid::WithoutBraces)},
            {QStringLiteral("microsecondsPerQuarter"), QString::number(found->microsecondsPerQuarter)},
            {QStringLiteral("bpm"), 60000000.0 / double(found->microsecondsPerQuarter)},
            {QStringLiteral("bpmText"), bpmText(found->microsecondsPerQuarter)}};
}

QVariantMap TempoMapModel::meterAt(const QString &tickText) const
{
    const auto tick = TimelineTime::parseDecimal(tickText);
    if (!tick) return {{QStringLiteral("ok"), false}};
    const auto spans = meterSpans(effectiveEvents());
    const auto &span = spans.at(spanFor(spans, *tick));
    const qint64 barTicks = span.beatTicks * span.numerator;
    return {{QStringLiteral("ok"), true}, {QStringLiteral("numerator"), span.numerator},
            {QStringLiteral("denominator"), int(qint64(4) * ticksPerQuarter() / span.beatTicks)},
            {QStringLiteral("beatTicks"), QString::number(span.beatTicks)},
            {QStringLiteral("barTicks"), QString::number(barTicks)},
            {QStringLiteral("bar"), QString::number(span.bar + floorDiv(*tick - span.tick, barTicks) + 1)}};
}

QString TempoMapModel::formatPosition(const QString &tick) const
{
    const auto value = TimelineTime::parseDecimal(tick);
    return value ? positionText(*value, meterSpans(effectiveEvents())) : QString();
}

QVariantMap TempoMapModel::parsePosition(const QString &text) const
{
    static const QRegularExpression pattern(QStringLiteral("^\\s*(\\d{1,9})(?:[.:](\\d{1,3})(?:[.:](\\d{1,7}))?)?\\s*$"));
    const auto match = pattern.match(text);
    const auto failure = [](const QString &message) {
        return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
    };
    if (!match.hasMatch()) return failure(tr("Enter a position as bar.beat.tick"));
    const qint64 bar = match.captured(1).toLongLong();
    const qint64 beat = match.captured(2).isEmpty() ? 1 : match.captured(2).toLongLong();
    const qint64 sub = match.captured(3).isEmpty() ? 0 : match.captured(3).toLongLong();
    if (bar < 1) return failure(tr("Bars start at 1"));
    const auto spans = meterSpans(effectiveEvents());
    int index = 0;
    for (int i = 1; i < spans.size() && spans.at(i).bar <= bar - 1; ++i) index = i;
    const auto &span = spans.at(index);
    if (beat < 1 || beat > span.numerator) return failure(tr("Bar %1 has %2 beats").arg(bar).arg(span.numerator));
    if (sub >= span.beatTicks) return failure(tr("A beat has %1 ticks").arg(span.beatTicks));
    const qint64 tick = span.tick + (bar - 1 - span.bar) * span.beatTicks * span.numerator
                      + (beat - 1) * span.beatTicks + sub;
    return {{QStringLiteral("ok"), true}, {QStringLiteral("tick"), QString::number(tick)}};
}

QString TempoMapModel::formatBpm(const QString &microsecondsPerQuarter) const
{
    const auto value = TimelineTime::parseDecimal(microsecondsPerQuarter);
    return value ? bpmText(*value) : QString();
}

QVariantMap TempoMapModel::parseBpm(const QString &text) const
{
    static const QRegularExpression pattern(QStringLiteral("^\\s*(\\d{1,4})(?:\\.(\\d{1,6}))?\\s*(?:bpm)?\\s*$"),
                                            QRegularExpression::CaseInsensitiveOption);
    const auto match = pattern.match(text);
    const auto failure = [](const QString &message) {
        return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
    };
    if (!match.hasMatch()) return failure(tr("Enter a tempo in BPM"));
    const QString fraction = match.captured(2);
    const qint64 digits = (match.captured(1) + fraction).toLongLong();
    quint64 scale = 1;
    for (int i = 0; i < fraction.size(); ++i) scale *= 10;
    // BPM = digits / scale, so microseconds per quarter = 60e6 * scale / digits.
    const auto micro = digits > 0 ? TimelineTime::mulDiv(60000000, scale, quint64(digits), TimeMap::NearestAway)
                                  : std::nullopt;
    const auto [lowest, highest] = microsecondRange();
    if (!micro || *micro < lowest || *micro > highest)
        return failure(tr("Tempo must be %1 to %2 BPM").arg(m_minimumBpm).arg(m_maximumBpm));
    return {{QStringLiteral("ok"), true}, {QStringLiteral("microsecondsPerQuarter"), QString::number(*micro)},
            {QStringLiteral("bpmText"), bpmText(*micro)}};
}

QString TempoMapModel::microsecondsForBpm(double bpm) const
{
    if (!std::isfinite(bpm) || bpm <= 0) return {};
    const auto [lowest, highest] = microsecondRange();
    return QString::number(std::clamp<qint64>(std::llround(60000000.0 / bpm), lowest, highest));
}

QString TempoMapModel::snapTick(const QString &tickText, const QString &grid, const QString &excludeId) const
{
    const auto tick = TimelineTime::parseDecimal(tickText);
    if (!tick) return {};
    if (grid != QLatin1String("bar") && grid != QLatin1String("beat")) return QString::number(*tick);
    const auto spans = meterSpans(effectiveEvents(), QUuid(excludeId));
    const auto &span = spans.at(spanFor(spans, *tick));
    const qint64 step = grid == QLatin1String("bar") ? span.beatTicks * span.numerator : span.beatTicks;
    return QString::number(span.tick + floorDiv(*tick - span.tick + step / 2, step) * step);
}

QString TempoMapModel::stepTick(const QString &tickText, const QString &grid, int steps,
                                const QString &excludeId) const
{
    const auto parsed = TimelineTime::parseDecimal(tickText);
    if (!parsed) return {};
    qint64 tick = *parsed;
    const auto spans = meterSpans(effectiveEvents(), QUuid(excludeId));
    const bool bars = grid == QLatin1String("bar");
    const auto stepOf = [bars](const MeterSpan &span) { return bars ? span.beatTicks * span.numerator : span.beatTicks; };
    for (int i = 0; i < std::min(std::abs(steps), 100000); ++i) {
        if (steps > 0) {
            const auto &span = spans.at(spanFor(spans, tick));
            tick = span.tick + (floorDiv(tick - span.tick, stepOf(span)) + 1) * stepOf(span);
        } else {
            const auto &span = spans.at(spanFor(spans, tick - 1));
            tick = span.tick + floorDiv(tick - 1 - span.tick, stepOf(span)) * stepOf(span);
        }
    }
    return QString::number(tick);
}

OperationError TempoMapModel::fail(OperationError error)
{
    m_lastError = error.message;
    emit lastErrorChanged();
    emit rejected(error.message);
    return error;
}

} // namespace QindaTK
