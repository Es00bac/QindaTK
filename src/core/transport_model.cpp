// SPDX-License-Identifier: LGPL-3.0-or-later
#include "transport_model.h"

#include <QMetaObject>
#include <QRegularExpression>
#include <QThread>
#include <limits>

namespace QindaTK {
TransportModel::TransportModel(QObject *parent)
    : QObject(parent), m_delivery(std::make_shared<DeliveryState>()) {
    m_delivery->model = this;
    m_publishTimer.setSingleShot(true);
    connect(&m_publishTimer, &QTimer::timeout, this, &TransportModel::publishCoalesced);
}
TransportModel::~TransportModel() {
    {
        std::lock_guard<std::mutex> lock(m_delivery->mutex);
        m_delivery->model = nullptr;
        ++m_delivery->generation;
    }
    if (m_adapter) m_adapter->setSnapshotSink({});
}
void TransportModel::setAdapter(TransportAdapter *adapter) {
    if (m_adapter == adapter) return;
    if (m_adapter) { m_adapter->setSnapshotSink({}); disconnect(m_adapter, nullptr, this, nullptr); }
    ++m_adapterGeneration;
    {
        std::lock_guard<std::mutex> lock(m_delivery->mutex);
        m_delivery->generation = m_adapterGeneration;
    }
    m_publishTimer.stop();
    m_coalesced.reset();
    m_publishElapsed.invalidate();
    const auto pending = m_pending.keys();
    m_pending.clear();
    for (const QString &id : pending) emit commandFinished(id, false, QStringLiteral("Transport adapter changed"));
    m_adapter = adapter;
    m_state = {};
    m_hasSnapshot = false;
    m_lastError.clear();
    emit changed();
    if (!adapter) return;
    const quint64 generation = m_adapterGeneration;
    connect(adapter, &QObject::destroyed, this, [this, generation] {
        if (generation != m_adapterGeneration) return;
        ++m_adapterGeneration;
        {
            std::lock_guard<std::mutex> lock(m_delivery->mutex);
            m_delivery->generation = m_adapterGeneration;
        }
        m_adapter = nullptr;
        m_publishTimer.stop();
        m_coalesced.reset();
        const auto pendingIds = m_pending.keys();
        m_pending.clear();
        for (const QString &id : pendingIds)
            emit commandFinished(id, false, QStringLiteral("Transport adapter ended"));
        m_state = {};
        m_hasSnapshot = false;
        emit changed();
    });
    const std::weak_ptr<DeliveryState> delivery = m_delivery;
    adapter->setSnapshotSink([delivery, generation](TransportSnapshot snapshot) {
        const auto state = delivery.lock();
        if (!state) return;
        TransportModel *model = nullptr;
        bool sameThread = false;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->generation != generation || !state->model) return;
            model = state->model;
            sameThread = QThread::currentThread() == model->thread();
            if (!sameThread) {
                // Scheduling under the lock prevents destruction between the
                // validity check and Qt's queued-receiver registration.
                QMetaObject::invokeMethod(model, [delivery, generation, snapshot = std::move(snapshot)]() mutable {
                    const auto current = delivery.lock();
                    if (!current) return;
                    TransportModel *target = nullptr;
                    {
                        std::lock_guard<std::mutex> guard(current->mutex);
                        if (current->generation == generation) target = current->model;
                    }
                    if (target) target->offerSnapshot(std::move(snapshot));
                }, Qt::QueuedConnection);
            }
        }
        // Destruction is confined to the model thread; direct delivery on
        // that thread cannot race destruction after the lock is released.
        if (sameThread) model->offerSnapshot(std::move(snapshot));
    });
    applySnapshot(adapter->snapshot());
}
void TransportModel::setTimeMap(TimeMap *timeMap) {
    if (m_timeMap == timeMap) return;
    if (m_timeMap) disconnect(m_timeMap, nullptr, this, nullptr);
    m_timeMap = timeMap;
    if (timeMap) {
        connect(timeMap, &TimeMap::changed, this, &TransportModel::changed);
        connect(timeMap, &QObject::destroyed, this, &TransportModel::changed);
    }
    emit changed();
}
QString TransportModel::positionSample() const { return QString::number(m_state.positionSample); }
QString TransportModel::loopStartSample() const { return QString::number(m_state.loopStart); }
QString TransportModel::loopEndSample() const { return QString::number(m_state.loopEnd); }
QString TransportModel::punchStartSample() const { return QString::number(m_state.punchStart); }
QString TransportModel::punchEndSample() const { return QString::number(m_state.punchEnd); }
std::optional<qint64> TransportModel::parseSample(const QString &text) {
    if (text.isEmpty() || text.trimmed() != text) return {};
    static const QRegularExpression integer(QStringLiteral("^[+-]?[0-9]+$"));
    if (!integer.match(text).hasMatch()) return {};
    bool ok = false;
    const qint64 value = text.toLongLong(&ok, 10);
    return ok ? std::optional<qint64>(value) : std::nullopt;
}
QString TransportModel::reject(const QString &reason) {
    m_lastError = reason;
    emit changed();
    emit rejected(reason);
    return {};
}
QString TransportModel::request(TransportCommand command) {
    if (!m_adapter) return reject(QStringLiteral("No transport engine is connected"));
    command.id = QString::number(++m_requestSerial);
    const QString id = command.id;
    m_pending.insert(id, command.kind);
    m_lastError.clear();
    emit changed();
    const quint64 generation = m_adapterGeneration;
    const std::weak_ptr<DeliveryState> delivery = m_delivery;
    m_adapter->submit(command, [delivery, generation](TransportReply reply) {
        const auto state = delivery.lock();
        if (!state) return;
        TransportModel *model = nullptr;
        bool sameThread = false;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->generation != generation || !state->model) return;
            model = state->model;
            sameThread = QThread::currentThread() == model->thread();
            if (!sameThread) {
                QMetaObject::invokeMethod(model, [delivery, generation, reply = std::move(reply)]() mutable {
                    const auto current = delivery.lock();
                    if (!current) return;
                    TransportModel *target = nullptr;
                    {
                        std::lock_guard<std::mutex> guard(current->mutex);
                        if (current->generation == generation) target = current->model;
                    }
                    if (target) target->acceptReply(std::move(reply), generation);
                }, Qt::QueuedConnection);
            }
        }
        if (sameThread) model->acceptReply(std::move(reply), generation);
    });
    return id;
}
QString TransportModel::requestPlay() {
    if (!canPlay()) return reject(playUnavailableReason().isEmpty()
        ? QStringLiteral("Playback is unavailable") : playUnavailableReason());
    return request({{}, TransportCommand::Kind::Play});
}
QString TransportModel::requestStop() { return request({{}, TransportCommand::Kind::Stop}); }
QString TransportModel::requestSeek(const QString &positionSample) {
    if (!m_state.canSeek) return reject(m_state.seekUnavailableReason.isEmpty()
        ? QStringLiteral("Seeking is unavailable") : m_state.seekUnavailableReason);
    const auto position = parseSample(positionSample);
    if (!position) return reject(QStringLiteral("Seek position must be an exact signed sample integer"));
    TransportCommand command; command.kind = TransportCommand::Kind::Seek;
    command.positionSample = *position;
    return request(command);
}
QString TransportModel::requestSeekRelative(const QString &deltaSamples) {
    const auto delta = parseSample(deltaSamples);
    if (!delta) return reject(QStringLiteral("Seek delta must be an exact signed sample integer"));
    const qint64 current = m_state.positionSample;
    if ((*delta > 0 && current > std::numeric_limits<qint64>::max() - *delta)
        || (*delta < 0 && current < std::numeric_limits<qint64>::min() - *delta))
        return reject(QStringLiteral("Seek position overflows signed sample range"));
    return requestSeek(QString::number(current + *delta));
}
QString TransportModel::requestRecord(bool enabled) {
    if (enabled && !canRecord()) return reject(recordUnavailableReason().isEmpty()
        ? QStringLiteral("Recording is unavailable") : recordUnavailableReason());
    TransportCommand command; command.kind = TransportCommand::Kind::Record; command.enabled = enabled;
    return request(command);
}
QString TransportModel::requestLoop(bool enabled, const QString &startSample, const QString &endSample) {
    if (enabled && !m_state.canLoop) return reject(m_state.loopUnavailableReason.isEmpty()
        ? QStringLiteral("Looping is unavailable") : m_state.loopUnavailableReason);
    const auto start = parseSample(startSample), end = parseSample(endSample);
    if (!start || !end || *start >= *end) return reject(QStringLiteral("Loop range must contain exact increasing samples"));
    TransportCommand command; command.kind = TransportCommand::Kind::Loop;
    command.enabled = enabled; command.rangeStart = *start; command.rangeEnd = *end;
    return request(command);
}
QString TransportModel::requestPunch(bool enabled, const QString &startSample, const QString &endSample) {
    if (enabled && !m_state.canPunch) return reject(m_state.punchUnavailableReason.isEmpty()
        ? QStringLiteral("Punch is unavailable") : m_state.punchUnavailableReason);
    const auto start = parseSample(startSample), end = parseSample(endSample);
    if (!start || !end || *start >= *end) return reject(QStringLiteral("Punch range must contain exact increasing samples"));
    TransportCommand command; command.kind = TransportCommand::Kind::Punch;
    command.enabled = enabled; command.rangeStart = *start; command.rangeEnd = *end;
    return request(command);
}
QString TransportModel::formatPosition(const QString &displayMode, bool dropFrame) const {
    if (displayMode == QStringLiteral("samples")) return positionSample();
    if (!m_timeMap) return QStringLiteral("—");
    if (displayMode == QStringLiteral("ticks")) {
        const auto converted = m_timeMap->sampleToTick(positionSample());
        return converted.value(QStringLiteral("ok")).toBool() ? converted.value(QStringLiteral("value")).toString() : QStringLiteral("—");
    }
    const auto frame = m_timeMap->sampleToFrame(positionSample());
    if (!frame.value(QStringLiteral("ok")).toBool()) return QStringLiteral("—");
    if (displayMode == QStringLiteral("frames")) return frame.value(QStringLiteral("value")).toString();
    if (displayMode == QStringLiteral("timecode")) {
        const auto formatted = m_timeMap->formatTimecode(frame.value(QStringLiteral("value")).toString(), dropFrame);
        return formatted.value(QStringLiteral("ok")).toBool() ? formatted.value(QStringLiteral("value")).toString() : QStringLiteral("—");
    }
    return QStringLiteral("—");
}
void TransportModel::acceptReply(TransportReply reply, quint64 generation) {
    if (generation != m_adapterGeneration || !m_pending.contains(reply.requestId)) return;
    m_pending.remove(reply.requestId);
    if (!m_coalesced || reply.snapshot.revision >= m_coalesced->revision)
        applySnapshot(reply.snapshot);
    m_lastError = reply.accepted ? QString() : reply.error.isEmpty() ? QStringLiteral("Transport command denied") : reply.error;
    emit changed();
    emit commandFinished(reply.requestId, reply.accepted, m_lastError);
}
void TransportModel::applySnapshot(const TransportSnapshot &snapshot) {
    if (m_hasSnapshot && snapshot.revision <= m_state.revision) return;
    m_state = snapshot;
    m_hasSnapshot = true;
    m_publishElapsed.restart();
    if (m_coalesced && m_coalesced->revision <= snapshot.revision) {
        m_coalesced.reset();
        m_publishTimer.stop();
    }
    emit changed();
}
void TransportModel::offerSnapshot(TransportSnapshot snapshot) {
    if (QThread::currentThread() != thread()) {
        const std::weak_ptr<DeliveryState> delivery = m_delivery;
        const auto state = delivery.lock();
        if (!state) return;
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->model) return;
        const quint64 generation = state->generation;
        QMetaObject::invokeMethod(state->model, [delivery, generation, snapshot = std::move(snapshot)]() mutable {
            const auto current = delivery.lock();
            if (!current) return;
            TransportModel *target = nullptr;
            {
                std::lock_guard<std::mutex> guard(current->mutex);
                if (current->generation == generation) target = current->model;
            }
            if (target) target->offerSnapshot(std::move(snapshot));
        }, Qt::QueuedConnection);
        return;
    }
    if (m_hasSnapshot && snapshot.revision <= m_state.revision) return;
    if (m_coalesced && snapshot.revision <= m_coalesced->revision) return;
    // AGENT-GUARD: only position/progress updates are rate limited. A change
    // of what the transport can do applies at once, so a caller that hears
    // its engine became ready (MediaPlayer.state) can seek in that same turn;
    // coalescing it left canSeek false for up to one interval and rejected
    // the seek (examples/sequence-edit-live parks its monitors that way).
    const bool capabilitiesChanged = !m_hasSnapshot || snapshot.canPlay != m_state.canPlay
        || snapshot.canSeek != m_state.canSeek || snapshot.canRecord != m_state.canRecord
        || snapshot.canLoop != m_state.canLoop || snapshot.canPunch != m_state.canPunch
        || snapshot.deviceError != m_state.deviceError;
    if (capabilitiesChanged) { applySnapshot(snapshot); return; }
    const int elapsed = m_publishElapsed.isValid() ? int(m_publishElapsed.elapsed()) : m_minSnapshotIntervalMs;
    if (elapsed >= m_minSnapshotIntervalMs) { applySnapshot(snapshot); return; }
    m_coalesced = std::move(snapshot);
    if (!m_publishTimer.isActive()) m_publishTimer.start(m_minSnapshotIntervalMs - elapsed);
}
void TransportModel::publishCoalesced() {
    if (!m_coalesced) return;
    const TransportSnapshot latest = *m_coalesced;
    m_coalesced.reset();
    applySnapshot(latest);
}
} // namespace QindaTK
