// SPDX-License-Identifier: LGPL-3.0-or-later
#include "engine_schedule.h"

namespace QindaTK {

EngineScheduleSnapshot::EngineScheduleSnapshot() = default;
EngineScheduleSnapshot::~EngineScheduleSnapshot() = default;

const EngineScheduleSnapshot::Parameter *EngineScheduleSnapshot::parameter(const QString &id) const
{
    const auto it = m_parameters.constFind(id);
    return it == m_parameters.cend() ? nullptr : &*it;
}

std::optional<qint64> EngineScheduleSnapshot::ticksAtSample(qint64 sample) const
{
    return TimelineTime::rescale(sample, {m_sampleRate, 1}, m_timebase, TimeMap::NearestAway);
}

std::optional<qint64> EngineScheduleSnapshot::sampleAtTicks(qint64 ticks) const
{
    return TimelineTime::rescale(ticks, m_timebase, {m_sampleRate, 1}, TimeMap::NearestAway);
}

std::optional<qint64> EngineScheduleSnapshot::musicalTickAtSample(qint64 sample) const
{
    const auto tick = m_timeMap->toTick(SamplePosition{sample}, TimeMap::NearestAway);
    return tick ? std::optional<qint64>(tick->value) : std::nullopt;
}

std::optional<qint64> EngineScheduleSnapshot::sampleAtMusicalTick(qint64 musicalTick) const
{
    const auto sample = m_timeMap->fromTick(MusicalTick{musicalTick}, TimeMap::NearestAway);
    return sample ? std::optional<qint64>(sample->value) : std::nullopt;
}

qint64 EngineScheduleSnapshot::microsecondsPerQuarterAt(qint64 musicalTick) const
{
    qint64 tempo = 500000;
    qint64 at = std::numeric_limits<qint64>::min();
    for (const auto &event : m_tempoEvents)
        if (event.kind == TempoEvent::Kind::Tempo && event.tick <= musicalTick && event.tick >= at) {
            tempo = event.microsecondsPerQuarter;
            at = event.tick;
        }
    return tempo;
}

EngineScheduleSnapshot::Value EngineScheduleSnapshot::valueAtTicks(const QString &parameterId, qint64 ticks) const
{
    const Parameter *found = parameter(parameterId);
    if (!found) return {};
    if (found->overriding) return {true, false, found->liveValue};
    if (found->mode == AutomationMode::Off) return {true, false, found->parameter.defaultValue};
    return {true, !found->points.isEmpty(), AutomationCurve::valueAt(found->parameter, found->points, ticks)};
}

EngineScheduleSnapshot::Value EngineScheduleSnapshot::valueAtSample(const QString &parameterId, qint64 sample) const
{
    const auto ticks = ticksAtSample(sample);
    return ticks ? valueAtTicks(parameterId, *ticks) : Value{};
}

bool EngineScheduleSnapshot::renderBlock(const QString &parameterId, qint64 startSample, int frames, double *out) const
{
    const Parameter *found = parameter(parameterId);
    if (!found || frames < 0 || (frames > 0 && !out)) return false;
    for (int i = 0; i < frames; ++i) {
        const auto ticks = ticksAtSample(startSample + i);
        if (!ticks) return false;
        out[i] = found->overriding ? found->liveValue
                 : found->mode == AutomationMode::Off
                     ? found->parameter.defaultValue
                     : AutomationCurve::valueAt(found->parameter, found->points, *ticks);
    }
    return true;
}

EngineSchedule::EngineSchedule(QObject *parent)
    : QObject(parent)
{
    publish();
}

EngineSchedule::~EngineSchedule()
{
    for (const auto &connection : std::as_const(m_connections)) disconnect(connection);
}

void EngineSchedule::setTempoModel(TempoMapModel *model)
{
    if (m_tempo == model) return;
    m_tempo = model;
    reconnect();
    emit configurationChanged();
}

void EngineSchedule::setAutomationModel(AutomationModel *model)
{
    if (m_automation == model) return;
    m_automation = model;
    reconnect();
    emit configurationChanged();
}

QString EngineSchedule::generationText() const
{
    const auto snapshot = acquire();
    return snapshot ? QString::number(snapshot->generation()) : QString();
}

void EngineSchedule::reconnect()
{
    for (const auto &connection : std::as_const(m_connections)) disconnect(connection);
    m_connections.clear();
    if (m_tempo) {
        // revisionChanged fires on commit, undo, redo and load, never on a preview.
        m_connections << connect(m_tempo, &TempoMapModel::revisionChanged, this, &EngineSchedule::publish);
        if (TimeMap *map = m_tempo->timeMap()) {
            m_connections << connect(map, &TimeMap::changed, this, [this, map] {
                const auto current = acquire();
                // Sample rate, PPQ or origin moved; a tempo preview alone does not republish.
                if (!current || !current->m_timeMap || current->sampleRate() != map->sampleRate()
                    || current->ticksPerQuarter() != map->ticksPerQuarter()
                    || current->m_timeMap->originSample() != map->originSample())
                    publish();
            });
        }
        m_connections << connect(m_tempo, &TempoMapModel::timeMapChanged, this, &EngineSchedule::reconnect);
    }
    if (m_automation) {
        m_connections << connect(m_automation, &AutomationModel::revisionChanged, this, &EngineSchedule::publish)
                      << connect(m_automation, &AutomationModel::parametersChanged, this, &EngineSchedule::publish)
                      << connect(m_automation, &AutomationModel::modeChanged, this, &EngineSchedule::publish)
                      << connect(m_automation, &AutomationModel::writeStateChanged, this, &EngineSchedule::publish)
                      << connect(m_automation, &AutomationModel::contentReset, this, &EngineSchedule::publish);
    }
    publish();
}

void EngineSchedule::publish()
{
    auto next = std::make_shared<EngineScheduleSnapshot>();
    next->m_generation = ++m_generation;
    auto map = std::make_unique<TimeMap>();
    if (const TimeMap *source = m_tempo ? m_tempo->timeMap() : nullptr) {
        map->setSampleRate(source->sampleRate());
        map->setFrameRate(source->frameRateNumerator(), source->frameRateDenominator());
        map->setTicksPerQuarter(source->ticksPerQuarter());
        map->setOriginSample(source->originSample());
    }
    if (m_tempo) {
        next->m_tempoEvents = m_tempo->committedEvents();
        next->m_tempoRevision = m_tempo->revisionValue();
        map->setTempoSegments(m_tempo->segmentsFor(next->m_tempoEvents));
    }
    next->m_sampleRate = map->sampleRate();
    next->m_ticksPerQuarter = map->ticksPerQuarter();
    // AGENT-GUARD: the copy belongs to no thread, so an engine thread may
    // query it and drop the last reference; nothing may connect to it.
    map->moveToThread(nullptr);
    next->m_timeMap = std::move(map);
    if (m_automation) {
        next->m_timebase = m_automation->timebase();
        next->m_automationRevision = m_automation->revisionValue();
        for (const auto &id : m_automation->parameterIds()) {
            const AutomationModel::Lane *lane = m_automation->lane(id);
            if (!lane) continue;
            EngineScheduleSnapshot::Parameter parameter;
            parameter.parameter = lane->parameter;
            parameter.points = lane->points;
            parameter.mode = lane->mode;
            // AGENT-NOTE (D-100): a held control (touched) is heard in every
            // mode, so a fader moves the sound even in read mode or with the
            // transport stopped; only a write pass records it.
            parameter.overriding = lane->writing || lane->touched;
            parameter.liveValue = lane->lastValue;
            next->m_parameters.insert(id, parameter);
            next->m_order.push_back(id);
        }
    }
    m_current.store(std::move(next), std::memory_order_release);
    emit published();
}

} // namespace QindaTK
