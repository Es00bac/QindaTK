// SPDX-License-Identifier: LGPL-3.0-or-later
// AudioPluginHost: chains, instance lifecycle, parameter mapping, sample
// rate, hang detection and offline renders. Rows, the rack protocol and the
// session part are in audio_plugin_host_content.cpp.
#include "audio_plugin_host.h"

#include "audio_parameter_binding.h"
#include "audio_session.h"

#include <QTimer>
#include <QWindow>
#include <cmath>
#include <limits>

namespace QindaTK {

AudioPluginOfflineClaim::~AudioPluginOfflineClaim()
{
    for (const Held &held : std::as_const(m_held)) {
        if (held.restoreRate > 0 && held.instance && held.instance->live()) {
            held.instance->activate(held.restoreRate, AudioPluginProcessor::Device);
            continue;
        }
        held.processor->requestReset(); // playback starts clean after the render
        held.processor->release(AudioPluginProcessor::Device);
    }
}

AudioPluginHost::AudioPluginHost(AudioSession *session, AutomationModel *automation, AudioMixerModel *mixer)
    : QObject(session), m_session(session), m_automation(automation), m_mixer(mixer),
      m_registry(new AudioPluginRegistry(this)), m_poll(new QTimer(this))
{
    connect(m_registry, &AudioPluginRegistry::pluginsChanged, this, &AudioPluginHost::browserChanged);
    connect(session, &AudioSession::sampleRateChanged, this, &AudioPluginHost::sampleRateChanged);
    connect(session->schedule(), &EngineSchedule::published, this, &AudioPluginHost::pushParameters);
    m_poll->setInterval(100);
    connect(m_poll, &QTimer::timeout, this, &AudioPluginHost::pollHung);
}

AudioPluginHost::~AudioPluginHost()
{
    unloadAll();
}

void AudioPluginHost::setRequestTimeoutMs(int milliseconds)
{
    m_timeouts.requestMs = std::max(1, milliseconds);
    for (const Slot &slot : std::as_const(m_slots)) slot.instance->setTimeouts(m_timeouts);
    emit timeoutsChanged();
}

void AudioPluginHost::setRealtimeTimeoutMs(int milliseconds)
{
    m_timeouts.realtimeMs = std::max(1, milliseconds);
    for (const Slot &slot : std::as_const(m_slots)) slot.instance->setTimeouts(m_timeouts);
    emit timeoutsChanged();
}

void AudioPluginHost::setOfflineTimeoutMs(int milliseconds)
{
    m_timeouts.offlineMs = std::max(1, milliseconds);
    for (const Slot &slot : std::as_const(m_slots)) slot.instance->setTimeouts(m_timeouts);
    emit timeoutsChanged();
}

void AudioPluginHost::setHangTimeoutMs(int milliseconds)
{
    m_timeouts.hangMs = std::max(1, milliseconds);
    for (const Slot &slot : std::as_const(m_slots)) slot.instance->setTimeouts(m_timeouts);
    emit timeoutsChanged();
}

AudioPluginInstance *AudioPluginHost::instanceObject(const QString &instanceId) const
{
    return m_slots.value(instanceId).instance;
}

QStringList AudioPluginHost::instanceIds() const
{
    QStringList ids;
    for (const QString &channel : m_channels) ids += m_chains.value(channel);
    return ids;
}

QStringList AudioPluginHost::chain(const QString &channelId) const
{
    return m_chains.value(channelId);
}

int AudioPluginHost::sampleRate() const
{
    return m_session ? m_session->sampleRate() : 48000;
}

bool AudioPluginHost::channelExists(const QString &channelId) const
{
    return m_mixer && m_mixer->channelIds().contains(channelId);
}

QVariantMap AudioPluginHost::fail(const QString &message)
{
    m_lastError = message;
    emit lastErrorChanged();
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

void AudioPluginHost::bump(bool graph)
{
    ++m_revision;
    emit changed();
    if (graph) emit contentChanged();
}

AudioPluginInstance *AudioPluginHost::createInstance(const QString &instanceId)
{
    auto *instance = new AudioPluginInstance(instanceId, this);
    instance->setTimeouts(m_timeouts);
    connect(instance, &AudioPluginInstance::changed, this, [this] { bump(false); });
    connect(instance, &AudioPluginInstance::failed, this, [this, instanceId](const QString &message) {
        emit instanceFailed(instanceId, message);
    });
    connect(instance, &AudioPluginInstance::restartRequested, this, [this, instance] { restartInstance(instance); });
    connect(instance, &AudioPluginInstance::parameterEdited, this,
            [this, instance](quint32 id, double value) { instanceEdited(instance, id, value); });
    connect(instance, &AudioPluginInstance::gestureEdited, this,
            [this, instance](quint32 id, bool begin) { instanceGesture(instance, id, begin); });
    connect(instance, &AudioPluginInstance::editorChanged, this,
            [this, instance] { emit editorChanged(instance->instanceId(), instance->editorOpen()); });
    return instance;
}

// AGENT-CONTRACT (D-191): every parameter a host may write is the automation
// parameter "<instance>/<CLAP id>" (range = the plugin's plain range, unit
// "plain", stepped parameters step 1). A new one starts at the plugin's
// current value; an existing one (a recalled session) keeps its value, which
// the engine sends to the plugin, so the automation is the source of truth.
void AudioPluginHost::ensureParameters(AudioPluginInstance *instance)
{
    const QString name = instance->descriptor().name.isEmpty() ? instance->pluginId() : instance->descriptor().name;
    for (const AudioPluginInstance::Parameter &parameter : instance->parameters()) {
        if (!parameter.mapped) continue;
        const QString id = AudioPlugins::parameterId(instance->instanceId(), parameter.id);
        if (m_automation->lane(id)) continue;
        AutomationParameter entry;
        entry.id = id;
        entry.name = tr("%1: %2").arg(name, parameter.name);
        entry.range = {UnitDimension::Scalar, QStringLiteral("plain"), parameter.minimum, parameter.maximum};
        entry.defaultValue = std::clamp(parameter.value, parameter.minimum, parameter.maximum);
        entry.step = parameter.stepped ? 1.0 : 0.0;
        entry.interpolation = parameter.stepped ? CurveInterpolation::Step : CurveInterpolation::Linear;
        OperationError problem;
        if (!m_automation->addParameter(entry, &problem)) {
            m_lastError = problem.message;
            emit lastErrorChanged();
        }
    }
}

QVariantMap AudioPluginHost::addPlugin(const QString &channelId, const QString &pluginId, int index)
{
    if (!channelExists(channelId)) return fail(tr("%1 is not a channel of this mix").arg(channelId));
    const AudioPluginDescriptor *descriptor = m_registry->descriptor(pluginId);
    if (!descriptor) return fail(tr("%1 is not an installed plugin; scan the plugin folders first").arg(pluginId));
    if (!descriptor->available) return fail(tr("%1 cannot be loaded: %2").arg(descriptor->name, descriptor->reason));
    if (descriptor->audioOutputs < 1) return fail(tr("%1 has no audio output").arg(descriptor->name));
    const QString id = AudioPlugins::newInstanceId();
    AudioPluginInstance *instance = createInstance(id);
    if (!instance->start(*descriptor, sampleRate())) {
        // An add that fails leaves nothing behind.
        const QString error = instance->error();
        delete instance;
        return fail(error);
    }
    ensureParameters(instance);
    m_slots.insert(id, {instance, channelId, false});
    if (!m_channels.contains(channelId)) m_channels.push_back(channelId);
    QStringList &list = m_chains[channelId];
    list.insert(index < 0 || index > list.size() ? list.size() : index, id);
    m_poll->start();
    bump(true);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("instanceId"), id}};
}

QVariantMap AudioPluginHost::removePlugin(const QString &instanceId)
{
    if (!m_slots.contains(instanceId)) return fail(tr("%1 is not a plugin instance of this session").arg(instanceId));
    const Slot slot = m_slots.take(instanceId);
    m_chains[slot.channelId].removeAll(instanceId);
    slot.instance->beginUnload();
    slot.instance->finishUnload(1000);
    slot.instance->disconnect(this);
    slot.instance->deleteLater();
    // AGENT-NOTE: its automation parameters stay (like the mixer's, D-110):
    // removing a parameter clears automation history.
    bump(true);
    return {{QStringLiteral("ok"), true}};
}

QVariantMap AudioPluginHost::movePlugin(const QString &instanceId, int index, const QString &channelId)
{
    if (!m_slots.contains(instanceId)) return fail(tr("%1 is not a plugin instance of this session").arg(instanceId));
    Slot &slot = m_slots[instanceId];
    const QString target = channelId.isEmpty() ? slot.channelId : channelId;
    if (!channelExists(target)) return fail(tr("%1 is not a channel of this mix").arg(target));
    QStringList &from = m_chains[slot.channelId];
    const qsizetype position = from.indexOf(instanceId);
    if (target == slot.channelId && position == std::clamp<qsizetype>(index, 0, from.size() - 1))
        return {{QStringLiteral("ok"), true}};
    from.removeAt(position);
    if (!m_channels.contains(target)) m_channels.push_back(target);
    QStringList &to = m_chains[target];
    to.insert(std::clamp<qsizetype>(index, 0, to.size()), instanceId);
    slot.channelId = target;
    bump(true);
    return {{QStringLiteral("ok"), true}};
}

QVariantMap AudioPluginHost::setBypassed(const QString &instanceId, bool bypassed)
{
    if (!m_slots.contains(instanceId)) return fail(tr("%1 is not a plugin instance of this session").arg(instanceId));
    Slot &slot = m_slots[instanceId];
    if (slot.bypassed != bypassed) {
        slot.bypassed = bypassed;
        bump(true);
    }
    return {{QStringLiteral("ok"), true}};
}

QVariantMap AudioPluginHost::reload(const QString &instanceId)
{
    if (!m_slots.contains(instanceId)) return fail(tr("%1 is not a plugin instance of this session").arg(instanceId));
    AudioPluginInstance *instance = m_slots.value(instanceId).instance;
    if (instance->live()) return {{QStringLiteral("ok"), true}};
    const AudioPluginDescriptor *descriptor = m_registry->descriptor(instance->pluginId());
    if (!descriptor || !descriptor->available) {
        const QString reason = descriptor ? descriptor->reason : tr("%1 is not installed").arg(instance->pluginId());
        instance->markMissing(reason);
        return fail(reason);
    }
    if (!instance->start(*descriptor, sampleRate())) return fail(instance->error());
    ensureParameters(instance);
    m_poll->start();
    bump(true);
    return {{QStringLiteral("ok"), true}};
}

QString AudioPluginHost::parameterId(const QString &instanceId, int clapParameterId) const
{
    return AudioPlugins::parameterId(instanceId, quint32(clapParameterId));
}

AudioParameterBinding *AudioPluginHost::parameterBinding(const QString &instanceId, int clapParameterId)
{
    const QString id = parameterId(instanceId, clapParameterId);
    if (!m_automation->lane(id)) return nullptr;
    AudioParameterBinding *&binding = m_bindings[id];
    if (!binding) {
        binding = new AudioParameterBinding(this); // parented: QML never deletes it
        binding->setSession(m_session);
        binding->setParameterId(id);
    }
    return binding;
}

QVariantMap AudioPluginHost::openEditor(const QString &instanceId, QWindow *owner)
{
    AudioPluginInstance *instance = instanceObject(instanceId);
    if (!instance) return fail(tr("%1 is not a plugin instance of this session").arg(instanceId));
    QString error;
    if (!instance->openEditor(owner, &error)) return fail(error);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("floating"), instance->editorFloating()}};
}

QVariantMap AudioPluginHost::closeEditor(const QString &instanceId)
{
    AudioPluginInstance *instance = instanceObject(instanceId);
    if (!instance) return fail(tr("%1 is not a plugin instance of this session").arg(instanceId));
    instance->closeEditor();
    return {{QStringLiteral("ok"), true}};
}

// The plugin's own editor changed a parameter: it becomes an automation
// transaction, exactly like a knob (one gesture, one history step).
void AudioPluginHost::instanceEdited(AudioPluginInstance *instance, quint32 id, double value)
{
    const QString key = AudioPlugins::parameterId(instance->instanceId(), id);
    m_pushed.insert(key, value); // the plugin has this value already
    AudioParameterBinding *binding = parameterBinding(instance->instanceId(), int(id));
    if (!binding) return; // read-only or unmapped: the plugin's own business
    if (m_gestures.contains(key)) {
        m_gestures[key] = value;
        binding->preview(value);
        return;
    }
    binding->begin(value);
    binding->commit(value);
}

void AudioPluginHost::instanceGesture(AudioPluginInstance *instance, quint32 id, bool begin)
{
    const QString key = AudioPlugins::parameterId(instance->instanceId(), id);
    AudioParameterBinding *binding = parameterBinding(instance->instanceId(), int(id));
    if (!binding) return;
    if (begin) {
        if (m_gestures.contains(key)) return;
        m_gestures.insert(key, binding->value());
        binding->begin(binding->value());
        return;
    }
    if (!m_gestures.contains(key)) return;
    binding->commit(m_gestures.take(key));
}

void AudioPluginHost::restartInstance(AudioPluginInstance *instance)
{
    if (!instance->live()) return;
    instance->activate(sampleRate()); // a new latency recompiles the graph
    bump(true);
}

void AudioPluginHost::sampleRateChanged()
{
    const int rate = sampleRate();
    bool any = false;
    for (const Slot &slot : std::as_const(m_slots)) {
        if (!slot.instance->live() || slot.instance->activeSampleRate() == rate) continue;
        slot.instance->activate(rate);
        any = true;
    }
    if (any) bump(true);
}

// While nothing processes, parameter changes reach plugins through a params
// flush in their process, so an open editor and a saved state follow knobs
// and automation edits. During playback the blocks carry them (per block).
void AudioPluginHost::pushParameters()
{
    if (!m_session || m_slots.isEmpty()) return;
    if (AudioEngine *engine = m_session->engine(); engine && engine->transport()->playing()) return;
    const EngineScheduleSnapshotPtr snapshot = m_session->schedule()->acquire();
    if (!snapshot) return;
    const qint64 ticks = m_session->transportTicks();
    for (const Slot &slot : std::as_const(m_slots)) {
        if (!slot.instance->live()) continue;
        QList<QPair<quint32, double>> values;
        for (const AudioPluginInstance::Parameter &parameter : slot.instance->parameters()) {
            if (!parameter.mapped) continue;
            const QString id = AudioPlugins::parameterId(slot.instance->instanceId(), parameter.id);
            const EngineScheduleSnapshot::Value value = snapshot->valueAtTicks(id, ticks);
            if (!value.known || m_pushed.value(id, std::numeric_limits<double>::quiet_NaN()) == value.value) continue;
            m_pushed.insert(id, value.value);
            values.push_back({parameter.id, value.value});
        }
        slot.instance->sendParameters(values);
    }
}

void AudioPluginHost::pollHung()
{
    bool any = false;
    for (const Slot &slot : std::as_const(m_slots)) {
        AudioPluginInstance *instance = slot.instance;
        if (!instance->live()) continue;
        any = true;
        const AudioPluginProcessorPtr processor = instance->processor();
        if (processor && processor->hung() && !processor->dead())
            instance->fail(tr("%1 stopped responding (no answer within %2 ms); its saved state is kept")
                               .arg(instance->descriptor().name)
                               .arg(m_timeouts.hangMs));
    }
    if (!any) m_poll->stop();
}

AudioPluginInsertsPtr AudioPluginHost::inserts() const
{
    auto out = std::make_shared<AudioPluginInserts>();
    for (const QString &channel : m_channels) {
        for (const QString &id : m_chains.value(channel)) {
            const Slot slot = m_slots.value(id);
            if (!slot.instance) continue;
            AudioPluginInsertSpec spec;
            spec.channelId = channel;
            spec.instanceId = id;
            spec.bypassed = slot.bypassed;
            spec.notes = slot.instance->descriptor().acceptsNotes();
            const AudioPluginProcessorPtr processor = slot.instance->processor();
            const bool running = slot.instance->live() || slot.instance->state() == AudioPluginInstance::State::Failed;
            if (processor && running && processor->configured()) {
                spec.processor = processor;
                spec.latency = processor->latency();
            }
            for (const AudioPluginInstance::Parameter &parameter : slot.instance->parameters())
                if (parameter.mapped && parameter.index >= 0)
                    spec.parameters.push_back({parameter.index, AudioPlugins::parameterId(id, parameter.id)});
            out->entries.push_back(spec);
        }
    }
    return out;
}

std::unique_ptr<AudioPluginOfflineClaim> AudioPluginHost::beginOffline(int sampleRate)
{
    auto claim = std::make_unique<AudioPluginOfflineClaim>();
    for (const Slot &slot : std::as_const(m_slots)) {
        AudioPluginInstance *instance = slot.instance;
        const AudioPluginProcessorPtr processor = instance->processor();
        if (!processor) continue;
        AudioPluginOfflineClaim::Held held{instance, processor, 0};
        if (instance->live() && instance->activeSampleRate() != sampleRate) {
            held.restoreRate = instance->activeSampleRate();
            instance->activate(sampleRate, AudioPluginProcessor::Offline);
        } else {
            processor->claim(AudioPluginProcessor::Offline, std::int64_t(m_timeouts.realtimeMs) * 1000000);
        }
        processor->requestReset();
        claim->m_held.push_back(held);
    }
    return claim;
}

bool AudioPluginHost::refreshStates()
{
    bool all = true;
    for (const Slot &slot : std::as_const(m_slots))
        if (slot.instance->live() && slot.instance->descriptor().state) all = slot.instance->refreshState() && all;
    return all;
}

void AudioPluginHost::unloadAll()
{
    // AGENT-GUARD: every process is asked first and waited for afterwards,
    // so shutdown takes about one second however many plugins run.
    for (const Slot &slot : std::as_const(m_slots)) slot.instance->beginUnload();
    for (const Slot &slot : std::as_const(m_slots)) slot.instance->finishUnload(1000);
    if (m_poll) m_poll->stop();
}

void AudioPluginHost::clearAll()
{
    unloadAll();
    for (const Slot &slot : std::as_const(m_slots)) {
        slot.instance->disconnect(this);
        slot.instance->deleteLater();
    }
    m_slots.clear();
    m_chains.clear();
    m_channels.clear();
    m_pushed.clear();
    m_gestures.clear();
}

} // namespace QindaTK
