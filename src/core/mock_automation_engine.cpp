// SPDX-License-Identifier: LGPL-3.0-or-later
#include "mock_automation_engine.h"

#include <algorithm>

namespace QindaTK {

MockAutomationEngine::MockAutomationEngine(QObject *parent)
    : QObject(parent)
{
}

void MockAutomationEngine::setSchedule(EngineSchedule *schedule)
{
    if (m_schedule == schedule) return;
    m_schedule = schedule;
    emit scheduleChanged();
}

void MockAutomationEngine::setBlockFrames(int frames)
{
    frames = std::clamp(frames, 1, 1 << 20);
    if (frames == m_blockFrames) return;
    m_blockFrames = frames;
    emit scheduleChanged();
}

AutomationModel *MockAutomationEngine::automation() const
{
    return m_schedule ? m_schedule->automationModel() : nullptr;
}

qint64 MockAutomationEngine::ticksNow() const
{
    const auto snapshot = m_schedule ? m_schedule->acquire() : nullptr;
    const auto ticks = snapshot ? snapshot->ticksAtSample(m_position) : std::nullopt;
    return ticks.value_or(0);
}

std::vector<double> MockAutomationEngine::renderBlock(const QString &parameterId, qint64 startSample, int frames) const
{
    std::vector<double> values(size_t(std::max(0, frames)));
    const auto snapshot = m_schedule ? m_schedule->acquire() : nullptr;
    if (!snapshot || !snapshot->renderBlock(parameterId, startSample, frames, values.data())) values.clear();
    return values;
}

void MockAutomationEngine::play()
{
    if (m_playing) return;
    m_playing = true;
    if (auto *model = automation()) model->transportStarted(ticksNow());
    emit processed();
}

void MockAutomationEngine::stop()
{
    if (!m_playing) return;
    m_playing = false;
    if (auto *model = automation()) model->transportStopped(ticksNow());
    emit processed();
}

void MockAutomationEngine::locate(const QString &sample)
{
    const auto value = TimelineTime::parseDecimal(sample);
    if (!value) return;
    m_position = *value;
    if (auto *model = automation()) model->transportMoved(ticksNow());
    emit processed();
}

QVariantMap MockAutomationEngine::describe(const EngineScheduleSnapshot &snapshot, qint64 sample) const
{
    QVariantMap values, automated;
    for (const auto &id : snapshot.parameterIds()) {
        const auto value = snapshot.valueAtSample(id, sample);
        values.insert(id, value.value);
        automated.insert(id, value.automated);
    }
    const auto ticks = snapshot.ticksAtSample(sample);
    const auto musical = snapshot.musicalTickAtSample(sample);
    const qint64 tempo = snapshot.microsecondsPerQuarterAt(musical.value_or(0));
    return {{QStringLiteral("ok"), ticks.has_value()},
            {QStringLiteral("sample"), QString::number(sample)},
            {QStringLiteral("ticks"), ticks ? QString::number(*ticks) : QString()},
            {QStringLiteral("musicalTick"), musical ? QString::number(*musical) : QString()},
            {QStringLiteral("microsecondsPerQuarter"), QString::number(tempo)},
            {QStringLiteral("bpm"), 60000000.0 / double(tempo)},
            {QStringLiteral("values"), values},
            {QStringLiteral("automated"), automated},
            {QStringLiteral("generation"), QString::number(snapshot.generation())}};
}

QVariantMap MockAutomationEngine::process(int frames)
{
    const auto snapshot = m_schedule ? m_schedule->acquire() : nullptr;
    if (!snapshot) return {{QStringLiteral("ok"), false}};
    m_lastBlock = describe(*snapshot, m_position);
    if (m_playing) {
        m_position += frames > 0 ? frames : m_blockFrames;
        if (auto *model = automation()) model->transportMoved(ticksNow());
    }
    emit processed();
    return m_lastBlock;
}

QVariantMap MockAutomationEngine::valuesAt(const QString &sample) const
{
    const auto value = TimelineTime::parseDecimal(sample);
    const auto snapshot = m_schedule ? m_schedule->acquire() : nullptr;
    if (!value || !snapshot) return {{QStringLiteral("ok"), false}};
    return describe(*snapshot, *value);
}

QVariantList MockAutomationEngine::render(const QString &parameterId, const QString &startSample, int frames,
                                          int stride) const
{
    const auto start = TimelineTime::parseDecimal(startSample);
    QVariantList result;
    if (!start) return result;
    const auto values = renderBlock(parameterId, *start, std::clamp(frames, 0, 1 << 20));
    for (size_t i = 0; i < values.size(); i += size_t(std::max(1, stride))) result.push_back(values[i]);
    return result;
}

QVariantMap MockAutomationEngine::touch(const QString &parameterId, double value)
{
    AutomationModel *model = automation();
    if (!model) return {{QStringLiteral("ok"), false}};
    const OperationError error = model->beginGesture(parameterId, ticksNow(), value);
    return {{QStringLiteral("ok"), !error}, {QStringLiteral("error"), error.message}};
}

QVariantMap MockAutomationEngine::move(const QString &parameterId, double value)
{
    AutomationModel *model = automation();
    if (!model) return {{QStringLiteral("ok"), false}};
    const OperationError error = model->recordGesture(parameterId, ticksNow(), value);
    return {{QStringLiteral("ok"), !error}, {QStringLiteral("error"), error.message}};
}

QVariantMap MockAutomationEngine::release(const QString &parameterId)
{
    AutomationModel *model = automation();
    if (!model) return {{QStringLiteral("ok"), false}};
    const OperationError error = model->endGesture(parameterId, ticksNow());
    return {{QStringLiteral("ok"), !error}, {QStringLiteral("error"), error.message}};
}

} // namespace QindaTK
