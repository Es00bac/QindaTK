// SPDX-License-Identifier: LGPL-3.0-or-later
#include "midi_launcher_model.h"

#include "audio_engine.h"
#include "audio_engine_p.h"

namespace QindaTK {

MidiLauncherModel::MidiLauncherModel(MidiSequenceModel *midi, QObject *parent)
    : QObject(parent), m_midi(midi)
{
    connect(m_midi, &MidiSequenceModel::contentChanged, this, [this] {
        refreshKeys();
        emit slotListChanged();
    });
    refreshKeys();
}

void MidiLauncherModel::setEngine(AudioEngine *engine)
{
    if (m_engine == engine) return;
    for (const auto &connection : std::as_const(m_connections)) disconnect(connection);
    m_connections.clear();
    reset(tr("The audio engine was replaced"));
    m_engine = engine;
    if (engine) {
        m_connections << connect(engine, &AudioEngine::midiLaunchEvent, this, &MidiLauncherModel::engineEvent)
                      << connect(engine, &AudioEngine::midiReset, this, &MidiLauncherModel::reset)
                      << connect(engine, &AudioEngine::stateChanged, this, &MidiLauncherModel::slotListChanged);
    }
    emit slotListChanged();
}

void MidiLauncherModel::refreshKeys()
{
    m_slotKeys.clear();
    m_laneKeys.clear();
    for (const MidiSlot &slot : m_midi->content().launchSlots) {
        m_slotKeys.insert(Midi::key(slot.id), slot.id);
        m_laneKeys.insert(Midi::key(slot.trackId), slot.trackId);
    }
}

const MidiSlot *MidiLauncherModel::findSlot(const QUuid &id) const
{
    for (const MidiSlot &slot : m_midi->content().launchSlots)
        if (slot.id == id) return &slot;
    return nullptr;
}

QVariantList MidiLauncherModel::slotList() const
{
    const bool running = m_engine && m_engine->stateValue() == AudioEngine::State::Running;
    QVariantList list;
    for (const MidiSlot &slot : m_midi->content().launchSlots) {
        const Runtime runtime = m_runtime.value(slot.id);
        const MidiPattern *pattern = m_midi->pattern(slot.patternId);
        const QString state = runtime.queued ? QStringLiteral("queued")
                              : runtime.playing ? QStringLiteral("playing") : QStringLiteral("ready");
        list.push_back(QVariantMap{{QStringLiteral("id"), Midi::idText(slot.id)},
                                   {QStringLiteral("sceneId"), Midi::idText(slot.sceneId)},
                                   {QStringLiteral("trackId"), Midi::idText(slot.trackId)},
                                   {QStringLiteral("patternId"), Midi::idText(slot.patternId)},
                                   {QStringLiteral("label"), !slot.label.isEmpty() ? slot.label
                                                             : pattern ? pattern->name : QString()},
                                   {QStringLiteral("state"), state},
                                   {QStringLiteral("stopping"), runtime.stopping},
                                   {QStringLiteral("startSample"), QString::number(runtime.startSample)},
                                   {QStringLiteral("available"), running},
                                   {QStringLiteral("reason"), running ? QString() : tr("No audio device is running")}});
    }
    return list;
}

QStringList MidiLauncherModel::playingSlots() const
{
    QStringList ids;
    for (auto it = m_runtime.cbegin(); it != m_runtime.cend(); ++it)
        if (it->playing) ids.push_back(Midi::idText(it.key()));
    ids.sort();
    return ids;
}

QStringList MidiLauncherModel::queuedSlots() const
{
    QStringList ids;
    for (auto it = m_runtime.cbegin(); it != m_runtime.cend(); ++it)
        if (it->queued) ids.push_back(Midi::idText(it.key()));
    ids.sort();
    return ids;
}

QVariantMap MidiLauncherModel::slotState(const QString &slotId) const
{
    const QUuid id = QUuid::fromString(slotId);
    if (!findSlot(id)) return {};
    const Runtime runtime = m_runtime.value(id);
    return {{QStringLiteral("state"), runtime.queued ? QStringLiteral("queued")
                                      : runtime.playing ? QStringLiteral("playing") : QStringLiteral("ready")},
            {QStringLiteral("startSample"), QString::number(runtime.startSample)},
            {QStringLiteral("stopSample"), QString::number(runtime.stopSample)},
            {QStringLiteral("stopping"), runtime.stopping}};
}

bool MidiLauncherModel::reject(const QString &requestId, const QString &message)
{
    m_lastError = message;
    emit lastErrorChanged();
    emit launchAcknowledged(requestId, QStringLiteral("rejected"), message);
    return false;
}

bool MidiLauncherModel::post(const QString &requestId, const QUuid &slot, quint64 lane, bool launching,
                             const QString &quantization)
{
    const auto quantize = quantization.isEmpty() ? std::optional<MidiQuantize>(m_midi->content().quantize)
                                                 : Midi::parseQuantize(quantization);
    if (!quantize) return reject(requestId, tr("Launch quantization is none, beat or bar"));
    if (!m_engine) return reject(requestId, tr("No audio engine is attached to the session"));
    QString error;
    const quint64 serial = launching ? m_engine->postLaunch(lane, Midi::key(slot), *quantize, &error)
                                     : m_engine->postLaunchStop(lane, *quantize, &error);
    if (!serial) return reject(requestId, error);
    m_requests.insert(serial, {requestId, slot});
    return true;
}

bool MidiLauncherModel::launch(const QString &requestId, const QString &slotId, const QString &quantization)
{
    const MidiSlot *slot = findSlot(QUuid::fromString(slotId));
    if (!slot) return reject(requestId, tr("There is no launcher slot %1").arg(slotId));
    return post(requestId, slot->id, Midi::key(slot->trackId), true, quantization);
}

bool MidiLauncherModel::stop(const QString &requestId, const QString &slotId, const QString &quantization)
{
    const MidiSlot *slot = findSlot(QUuid::fromString(slotId));
    if (!slot) return reject(requestId, tr("There is no launcher slot %1").arg(slotId));
    return post(requestId, slot->id, Midi::key(slot->trackId), false, quantization);
}

bool MidiLauncherModel::stopAll(const QString &requestId, const QString &quantization)
{
    return post(requestId, {}, 0, false, quantization);
}

void MidiLauncherModel::acknowledge(quint64 serial, const QString &state, const QString &message)
{
    const auto found = m_requests.constFind(serial);
    if (found == m_requests.cend()) return;
    const Request request = *found;
    m_requests.erase(found);
    emit launchAcknowledged(request.id, state, message);
}

QString MidiLauncherModel::codeText(int code)
{
    switch (code) {
    case MidiEngineEvent::NoGraph: return tr("The session has not reached the audio device yet");
    case MidiEngineEvent::NoSlot: return tr("The slot is not on a MIDI track of the playing session");
    case MidiEngineEvent::NoLane: return tr("Too many tracks are launching clips");
    case MidiEngineEvent::TransportStopped: return tr("The transport stopped");
    default: return {};
    }
}

void MidiLauncherModel::engineEvent(int kind, quint64 serial, quint64 lane, quint64 slot, qint64 sample, int code)
{
    const QUuid slotId = m_slotKeys.value(slot);
    const QString at = QString::number(sample);
    switch (kind) {
    case MidiEngineEvent::Queued: {
        Runtime &runtime = m_runtime[slotId];
        runtime.queued = true;
        runtime.startSample = sample;
        if (m_requests.contains(serial)) m_launches.insert(serial, m_requests.value(serial));
        acknowledge(serial, QStringLiteral("accepted"), tr("Starts at sample %1").arg(at));
        break;
    }
    case MidiEngineEvent::Started: {
        const QUuid track = m_laneKeys.value(lane);
        for (const MidiSlot &other : m_midi->content().launchSlots)
            if (other.trackId == track && other.id != slotId) m_runtime[other.id].playing = false;
        Runtime &runtime = m_runtime[slotId];
        runtime = Runtime{};
        runtime.playing = true;
        runtime.startSample = sample;
        m_launches.remove(serial);
        emit slotStarted(Midi::idText(slotId), at);
        break;
    }
    case MidiEngineEvent::StopQueued:
        for (const MidiSlot &other : m_midi->content().launchSlots)
            if ((!lane || Midi::key(other.trackId) == lane) && m_runtime.value(other.id).playing) {
                m_runtime[other.id].stopping = true;
                m_runtime[other.id].stopSample = sample;
            }
        acknowledge(serial, QStringLiteral("accepted"), tr("Stops at sample %1").arg(at));
        break;
    case MidiEngineEvent::Stopped:
        if (slot) {
            Runtime &runtime = m_runtime[slotId];
            runtime.playing = false;
            runtime.stopping = false;
            runtime.stopSample = sample;
            emit slotStopped(Midi::idText(slotId), at);
        } else {
            acknowledge(serial, QStringLiteral("accepted"), tr("Nothing was playing"));
        }
        break;
    case MidiEngineEvent::Canceled:
        if (m_launches.contains(serial)) { // a queued launch that never started
            const Request request = m_launches.take(serial);
            m_runtime[request.slot].queued = false;
            emit launchAcknowledged(request.id, QStringLiteral("canceled"),
                                    code ? codeText(code) : tr("Replaced by a later request"));
        } else if (!slotId.isNull()) {
            m_runtime[slotId].stopping = false; // a stop that a launch replaced
        }
        break;
    case MidiEngineEvent::Rejected:
        acknowledge(serial, QStringLiteral("rejected"), codeText(code));
        break;
    default:
        return;
    }
    emit slotListChanged();
}

void MidiLauncherModel::reset(const QString &reason)
{
    const auto requests = std::exchange(m_requests, {});
    const auto launches = std::exchange(m_launches, {});
    m_runtime.clear();
    for (const Request &request : requests) emit launchAcknowledged(request.id, QStringLiteral("canceled"), reason);
    for (const Request &request : launches) emit launchAcknowledged(request.id, QStringLiteral("canceled"), reason);
    emit slotListChanged();
}

} // namespace QindaTK
