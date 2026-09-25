// SPDX-License-Identifier: LGPL-3.0-or-later
#include "audio_mixer_model.h"

#include "audio_parameter_binding.h"
#include "audio_session.h"
#include "timeline_model.h"

#include <algorithm>

namespace QindaTK {
namespace {

// Audio and MIDI tracks are mixer channels (WP-064: MIDI tracks play the
// built-in test instrument through their channel).
bool isAudio(const QString &kind)
{
    return kind.isEmpty() || kind == QLatin1String("audio") || kind == QLatin1String("midi");
}

QVariantMap entry(const QString &id, const QString &label, const QString &layout, bool available,
                  const QString &reason = {})
{
    return {{QStringLiteral("id"), id}, {QStringLiteral("label"), label}, {QStringLiteral("layout"), layout},
            {QStringLiteral("available"), available}, {QStringLiteral("reason"), reason}};
}

} // namespace

AudioMixerModel::AudioMixerModel(AudioSession *session, TimelineModel *timeline, AutomationModel *automation)
    : QObject(session), m_session(session), m_timeline(timeline), m_automation(automation),
      m_strips(new AudioMixerStripModel(this)),
      m_routing(new AudioMixerStripModel({"portId", "label", "layout", "available"}, this))
{
    connect(m_timeline, &TimelineModel::revisionChanged, this, [this] { refresh(); });
    connect(m_timeline, &TimelineModel::contentReset, this, [this] { refresh(); });
    connect(m_automation, &AutomationModel::writeStateChanged, this, &AudioMixerModel::refreshHeld);
    connect(m_automation, &AutomationModel::contentReset, this, &AudioMixerModel::refreshHeld);
    refresh(true);
}

AudioMixerModel::~AudioMixerModel()
{
    for (const auto &connection : std::as_const(m_engineConnections)) disconnect(connection);
}

void AudioMixerModel::setEngine(AudioEngine *engine)
{
    if (m_engine == engine) return;
    for (const auto &connection : std::as_const(m_engineConnections)) disconnect(connection);
    m_engineConnections.clear();
    m_engine = engine;
    // Port availability follows the running stream (D-112).
    if (engine)
        m_engineConnections << connect(engine, &AudioEngine::stateChanged, this, [this] { refresh(); })
                            << connect(engine, &AudioEngine::formatChanged, this, [this] { refresh(); });
    emit engineChanged();
    refresh(true);
}

QStringList AudioMixerModel::hiddenChannels() const
{
    QStringList hidden(m_hidden.cbegin(), m_hidden.cend());
    std::sort(hidden.begin(), hidden.end());
    return hidden;
}

QString AudioMixerModel::channelName(const QString &channelId) const
{
    if (AudioMixer::isTrack(channelId)) {
        const TimelineTrack *track = m_timeline->trackRecord(AudioMixer::trackOf(channelId));
        return !track ? channelId : track->name.isEmpty() ? tr("Track") : track->name;
    }
    if (const AudioMixerBus *bus = AudioMixer::bus(m_content, channelId)) return bus->name;
    if (channelId == AudioMixer::masterId()) return tr("Master");
    return channelId;
}

QString AudioMixerModel::destinationLabel(const QString &id) const
{
    if (id.isEmpty()) return tr("Disconnected");
    const auto port = AudioMixer::parsePort(id);
    if (!port) return channelName(id);
    if (port->main) return tr("Main output");
    const QString channels = port->count > 1 ? QStringLiteral("%1–%2").arg(port->first).arg(port->first + port->count - 1)
                                             : QString::number(port->first);
    return tr("Out %1 · %2").arg(channels, port->deviceId);
}

QString AudioMixerModel::outputLayout(const QString &destinationId) const
{
    if (destinationId.isEmpty()) return {};
    if (const auto port = AudioMixer::parsePort(destinationId))
        return AudioMixer::layoutName(AudioMixer::layoutForLanes(port->count).value_or(AudioLayout::Stereo));
    if (destinationId == AudioMixer::masterId()) return QStringLiteral("Stereo");
    return AudioMixer::layoutName(AudioMixer::strip(m_content, destinationId).layout);
}

QString AudioMixerModel::activeDeviceId() const
{
    return activeOutputChannels() > 0 ? m_engine->activeConfiguration().value(QStringLiteral("deviceId")).toString()
                                      : QString();
}

int AudioMixerModel::activeOutputChannels() const
{
    return m_engine ? m_engine->streamFormat().outputChannels : 0;
}

QString AudioMixerModel::portProblem(const QString &portId) const
{
    const auto port = AudioMixer::parsePort(portId);
    if (!port) return tr("%1 is not an output").arg(portId);
    if (port->main) return {};
    const QString label = destinationLabel(portId);
    const QString device = activeDeviceId();
    if (device.isEmpty()) return tr("%1 is not available: no audio device is running").arg(label);
    if (port->deviceId != device) return tr("%1 is not available: %2 is not the running device").arg(label, port->deviceId);
    const int channels = activeOutputChannels();
    if (port->first + port->count - 1 > channels)
        return tr("%1 is not available: %2 has %n output channel(s)", nullptr, channels).arg(label, device);
    return {};
}

QVariantList AudioMixerModel::outputPorts() const
{
    QVariantList ports;
    QSet<QString> listed;
    const auto add = [&](const QString &id, const QString &layout) {
        const QString reason = portProblem(id);
        ports.push_back(entry(id, destinationLabel(id), layout, reason.isEmpty(), reason));
        listed.insert(id);
    };
    add(AudioMixer::mainPortId(), QStringLiteral("Stereo"));
    const QString device = activeDeviceId();
    const int channels = activeOutputChannels();
    if (!device.isEmpty()) {
        if (channels >= 6) add(AudioMixer::devicePortId(device, 1, 6), QStringLiteral("5.1"));
        if (channels >= 4) add(AudioMixer::devicePortId(device, 1, 4), QStringLiteral("Quad"));
        if (channels >= 2) add(AudioMixer::devicePortId(device, 1, 2), QStringLiteral("Stereo"));
        for (int channel = 1; channel <= channels; ++channel)
            add(AudioMixer::devicePortId(device, channel, 1), QStringLiteral("Mono"));
    }
    // AGENT-GUARD (D-112): a port a route names stays listed (unavailable,
    // with the reason) after its device goes; nothing takes its place.
    for (const QString &channel : m_channels) {
        const QString output = AudioMixer::strip(m_content, channel).output;
        if (listed.contains(output)) continue;
        if (const auto port = AudioMixer::parsePort(output))
            add(output, AudioMixer::layoutName(AudioMixer::layoutForLanes(port->count).value_or(AudioLayout::Stereo)));
    }
    return ports;
}

QVariantList AudioMixerModel::destinations(const QString &channelId) const
{
    QVariantList list;
    if (!m_channels.contains(channelId)) return list;
    const auto add = [&](const QVariantMap &base) {
        QVariantMap destination = base;
        const QString reason = routeProblem(channelId, base.value(QStringLiteral("id")).toString());
        destination.insert(QStringLiteral("allowed"), reason.isEmpty());
        if (!reason.isEmpty()) destination.insert(QStringLiteral("reason"), reason);
        list.push_back(destination);
    };
    add(entry({}, destinationLabel({}), QString(), true));
    if (channelId != AudioMixer::masterId()) {
        add(entry(AudioMixer::masterId(), channelName(AudioMixer::masterId()), QStringLiteral("Stereo"), true));
        for (const AudioMixerBus &bus : m_content.buses)
            if (bus.id != channelId)
                add(entry(bus.id, bus.name, AudioMixer::layoutName(AudioMixer::strip(m_content, bus.id).layout), true));
    }
    for (const QVariant &port : outputPorts()) add(port.toMap());
    return list;
}

QString AudioMixerModel::loopText(const QString &from, const QStringList &loop) const
{
    QStringList names{channelName(from)};
    for (const QString &id : loop) names.push_back(channelName(id));
    return names.join(QStringLiteral(" → "));
}

QString AudioMixerModel::routeProblem(const QString &from, const QString &to) const
{
    if (to.isEmpty()) return {};
    const QString name = channelName(from);
    if (to == from) return tr("%1 cannot feed itself").arg(name);
    if (AudioMixer::isTrack(to)) return tr("Tracks do not accept input: route %1 to a bus, the master or an output").arg(name);
    if (from == AudioMixer::masterId() && !AudioMixer::isPort(to)) return tr("The master bus feeds only an output");
    if (to == AudioMixer::masterId() || AudioMixer::isBus(to)) {
        if (AudioMixer::isBus(to) && !AudioMixer::bus(m_content, to)) return tr("%1 is not a bus of this mix").arg(to);
        const QStringList loop = AudioMixer::loopThrough(m_content, from, to);
        if (!loop.isEmpty())
            return tr("Routing %1 to %2 would close a feedback loop: %3").arg(name, channelName(to), loopText(from, loop));
        return {};
    }
    if (AudioMixer::isPort(to)) return portProblem(to);
    return tr("%1 is not a destination").arg(to);
}

const AudioMixerSend *AudioMixerModel::findSend(const QString &sendId, QString *channelId) const
{
    for (auto it = m_content.strips.cbegin(); it != m_content.strips.cend(); ++it)
        for (const AudioMixerSend &send : it->sends)
            if (send.id == sendId) {
                if (channelId) *channelId = it.key();
                return &send;
            }
    return nullptr;
}

QStringList AudioMixerModel::parametersOf(const QString &channelId) const
{
    QStringList ids{AudioMixer::parameterId(channelId, QStringLiteral("gain")),
                    AudioMixer::parameterId(channelId, QStringLiteral("pan")),
                    AudioMixer::parameterId(channelId, QStringLiteral("width")),
                    AudioMixer::parameterId(channelId, QStringLiteral("depth"))};
    for (const AudioMixerSend &send : AudioMixer::strip(m_content, channelId).sends)
        ids.push_back(AudioMixer::parameterId(send.id, QStringLiteral("level")));
    return ids;
}

bool AudioMixerModel::isHeld(const QString &channelId) const
{
    for (const QString &id : parametersOf(channelId))
        if (const AutomationModel::Lane *lane = m_automation->lane(id); lane && lane->touched) return true;
    return false;
}

QVariantList AudioMixerModel::stripRow(const QString &id, const AudioMixer::Audibility &audibility) const
{
    const AudioMixerStrip settings = AudioMixer::strip(m_content, id);
    const TimelineTrack *track = AudioMixer::isTrack(id) ? m_timeline->trackRecord(AudioMixer::trackOf(id)) : nullptr;
    const AudioMixerBus *bus = AudioMixer::bus(m_content, id);
    const bool master = id == AudioMixer::masterId();
    QStringList groupNames;
    for (const AudioMixerGroup &group : m_content.groups)
        if (group.members.contains(id)) groupNames.push_back(group.name);
    QVariantList sends;
    for (const AudioMixerSend &send : settings.sends)
        sends.push_back(QVariantMap{{QStringLiteral("id"), send.id},
                                    {QStringLiteral("target"), send.target},
                                    {QStringLiteral("targetName"), channelName(send.target)},
                                    {QStringLiteral("preFader"), send.preFader},
                                    {QStringLiteral("parameterId"), AudioMixer::parameterId(send.id, QStringLiteral("level"))}});
    const bool muted = track ? track->muted : bus ? bus->muted : master && m_content.masterMuted;
    const bool soloed = track ? track->solo : bus && bus->soloed;
    const QString kind = track ? QStringLiteral("track") : bus ? QStringLiteral("bus") : QStringLiteral("master");
    const bool available = !AudioMixer::isPort(settings.output) || portProblem(settings.output).isEmpty();
    // AGENT-CONTRACT: the order of AudioMixerStripModel::roles().
    return {id, channelName(id), kind, AudioMixer::layoutName(settings.layout), settings.output,
            destinationLabel(settings.output), available, muted, soloed, track && track->armed, settings.soloSafe,
            audibility.implicit.contains(id), audibility.muted.contains(id), groupNames, QVariant(sends), isHeld(id),
            m_hidden.contains(id), track ? track->color : QString(), outputLayout(settings.output)};
}

QVariantMap AudioMixerModel::channel(const QString &channelId) const
{
    const int row = m_strips->indexOf(channelId);
    if (row >= 0) return m_strips->get(row);
    if (!m_channels.contains(channelId)) return {};
    // A hidden channel has no strip row; describe it the same way.
    QSet<QString> muted, soloed;
    for (const QString &id : m_channels)
        if (const TimelineTrack *track = AudioMixer::isTrack(id) ? m_timeline->trackRecord(AudioMixer::trackOf(id)) : nullptr) {
            if (track->muted) muted.insert(id);
            if (track->solo) soloed.insert(id);
        }
    const QVariantList values = stripRow(channelId, AudioMixer::audibility(m_content, m_channels, muted, soloed));
    const QList<QByteArray> names = AudioMixerStripModel::roles();
    QVariantMap map;
    for (int i = 0; i < names.size() && i < values.size(); ++i) map.insert(QString::fromLatin1(names.at(i)), values.at(i));
    return map;
}

void AudioMixerModel::refresh(bool force)
{
    QStringList channels;
    QSet<QString> muted, soloed;
    for (const QUuid &trackId : m_timeline->trackOrder()) {
        const TimelineTrack *track = m_timeline->trackRecord(trackId);
        if (!track || !isAudio(track->kind)) continue;
        const QString id = AudioMixer::trackChannelId(trackId);
        channels.push_back(id);
        if (track->muted) muted.insert(id);
        if (track->solo) soloed.insert(id);
    }
    for (const AudioMixerBus &bus : m_content.buses) channels.push_back(bus.id);
    channels.push_back(AudioMixer::masterId());
    const bool channelsChanged = channels != m_channels;
    m_channels = channels;
    const AudioMixer::Audibility audibility = AudioMixer::audibility(m_content, channels, muted, soloed);
    QStringList held;
    QVector<QVariantList> rows;
    QVariantList sources, connections, buses, destinations, groups;
    QVector<QVariantList> inputs;
    for (const QString &id : std::as_const(channels)) {
        const bool holding = isHeld(id);
        if (holding) held.push_back(id);
        // AGENT-GUARD (D-113): a hidden channel keeps its strip while a
        // gesture holds one of its parameters; it leaves when the gesture ends.
        if (!m_hidden.contains(id) || holding) rows.push_back(stripRow(id, audibility));
        const QString layout = AudioMixer::layoutName(AudioMixer::strip(m_content, id).layout);
        sources.push_back(entry(id, channelName(id), layout, true));
        inputs.push_back({id, channelName(id), layout, true});
        const QString output = AudioMixer::strip(m_content, id).output;
        if (!output.isEmpty())
            connections.push_back(QVariantMap{{QStringLiteral("inputId"), id}, {QStringLiteral("outputId"), output}});
    }
    for (const AudioMixerBus &bus : m_content.buses) {
        const QString layout = AudioMixer::layoutName(AudioMixer::strip(m_content, bus.id).layout);
        buses.push_back(QVariantMap{{QStringLiteral("id"), bus.id}, {QStringLiteral("name"), bus.name},
                                    {QStringLiteral("layout"), layout}, {QStringLiteral("muted"), bus.muted},
                                    {QStringLiteral("soloed"), bus.soloed}});
        destinations.push_back(entry(bus.id, bus.name, layout, true));
    }
    destinations.push_back(entry(AudioMixer::masterId(), channelName(AudioMixer::masterId()), QStringLiteral("Stereo"), true));
    destinations += outputPorts();
    for (const AudioMixerGroup &group : m_content.groups)
        groups.push_back(QVariantMap{{QStringLiteral("id"), group.id}, {QStringLiteral("name"), group.name},
                                     {QStringLiteral("members"), group.members}, {QStringLiteral("muted"), group.muted},
                                     {QStringLiteral("soloed"), group.soloed}});
    const bool different = force || channelsChanged || rows != m_rows || sources != m_sources
        || connections != m_connections || buses != m_buses || destinations != m_destinations || groups != m_groups;
    m_rows = rows;
    m_sources = sources;
    m_connections = connections;
    m_buses = buses;
    m_destinations = destinations;
    m_groups = groups;
    m_strips->update(rows);
    m_routing->update(inputs);
    if (different) {
        prune();
        ++m_revision;
        emit changed();
    }
    if (held != m_held) {
        m_held = held;
        emit heldChanged();
    }
}

void AudioMixerModel::refreshHeld()
{
    QStringList held;
    for (const QString &id : std::as_const(m_channels))
        if (isHeld(id)) held.push_back(id);
    if (held != m_held) refresh();
}

void AudioMixerModel::apply(const AudioMixerContent &next)
{
    m_content = next;
    for (auto it = m_hidden.begin(); it != m_hidden.end();)
        it = AudioMixer::isBus(*it) && !AudioMixer::bus(m_content, *it) ? m_hidden.erase(it) : std::next(it);
    emit contentChanged();
    refresh(true);
}

QVariantMap AudioMixerModel::commit(const QString &label, AudioMixerContent next, const QVariantMap &extra)
{
    AudioMixer::normalize(next);
    if (next == m_content) {
        QVariantMap result = extra;
        result.insert(QStringLiteral("unchanged"), true);
        return accepted(result);
    }
    if (const QString problem = AudioMixer::validate(next); !problem.isEmpty()) return reject(problem);
    m_history.resize(m_cursor);
    m_history.push_back({label, m_content, next});
    ++m_serial;
    if (m_history.size() > kHistoryLimit) m_history.removeFirst();
    m_cursor = int(m_history.size());
    apply(next);
    emit historyChanged();
    return accepted(extra);
}

bool AudioMixerModel::undo()
{
    if (!canUndo()) return false;
    const AudioMixerContent before = m_history.at(--m_cursor).before;
    apply(before);
    emit historyChanged();
    return true;
}

bool AudioMixerModel::redo()
{
    if (!canRedo()) return false;
    const AudioMixerContent after = m_history.at(m_cursor++).after;
    apply(after);
    emit historyChanged();
    return true;
}

void AudioMixerModel::clearHistory()
{
    if (m_history.isEmpty()) return;
    m_history.clear();
    m_cursor = 0;
    emit historyChanged();
}

QVariantMap AudioMixerModel::accepted(const QVariantMap &extra)
{
    if (!m_lastError.isEmpty()) {
        m_lastError.clear();
        emit lastErrorChanged();
    }
    QVariantMap result = extra;
    result.insert(QStringLiteral("ok"), true);
    return result;
}

QVariantMap AudioMixerModel::reject(const QString &message)
{
    m_lastError = message;
    emit lastErrorChanged();
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

} // namespace QindaTK
