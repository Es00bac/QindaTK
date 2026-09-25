// SPDX-License-Identifier: LGPL-3.0-or-later
// AudioPluginHost: view rows (PluginBrowser, PluginRack), the rack request
// protocol, and the session part "qindatk.audio-plugins/1".
#include "audio_plugin_host.h"

#include "audio_parameter_binding.h"
#include "audio_session.h"

#include <QSet>
#include <QWindow>

namespace QindaTK {
namespace {

QVariantMap answer(const QVariantMap &result)
{
    const bool ok = result.value(QStringLiteral("ok")).toBool();
    return {{QStringLiteral("state"), ok ? QStringLiteral("accepted") : QStringLiteral("rejected")},
            {QStringLiteral("message"), result.value(QStringLiteral("error")).toString()},
            {QStringLiteral("result"), result}};
}

QVariantMap parameterMap(const AudioPluginInstance::Parameter &parameter)
{
    return {{QStringLiteral("id"), qint64(parameter.id)},
            {QStringLiteral("name"), parameter.name},
            {QStringLiteral("module"), parameter.module},
            {QStringLiteral("min"), parameter.minimum},
            {QStringLiteral("max"), parameter.maximum},
            {QStringLiteral("default"), parameter.defaultValue},
            {QStringLiteral("stepped"), parameter.stepped},
            {QStringLiteral("mapped"), parameter.mapped}};
}

} // namespace

QVariantList AudioPluginHost::browserPlugins() const
{
    QVariantList rows = m_registry->plugins();
    // Plugins the session names but this machine lacks stay identifiable.
    QSet<QString> listed;
    for (const QVariant &row : std::as_const(rows)) listed.insert(row.toMap().value(QStringLiteral("pluginId")).toString());
    for (const Slot &slot : m_slots) {
        const AudioPluginInstance *instance = slot.instance;
        if (instance->state() != AudioPluginInstance::State::Missing || listed.contains(instance->pluginId())) continue;
        listed.insert(instance->pluginId());
        AudioPluginDescriptor missing = instance->descriptor();
        missing.pluginId = instance->pluginId();
        missing.available = false;
        missing.scanState = QStringLiteral("missing");
        missing.reason = tr("Not installed; the session keeps its settings");
        rows.push_back(missing.browserRow());
    }
    return rows;
}

QVariantMap AudioPluginHost::describeSlot(const QString &instanceId, const Slot &slot) const
{
    const AudioPluginInstance *instance = slot.instance;
    const AudioPluginDescriptor &descriptor = instance->descriptor();
    QString state = instance->stateName();
    if (instance->live() && slot.bypassed) state = QStringLiteral("bypassed");
    return {{QStringLiteral("instanceId"), instanceId},
            {QStringLiteral("pluginId"), instance->pluginId()},
            {QStringLiteral("channelId"), slot.channelId},
            {QStringLiteral("format"), descriptor.format},
            {QStringLiteral("name"), descriptor.name.isEmpty() ? instance->pluginId() : descriptor.name},
            {QStringLiteral("vendor"), descriptor.vendor},
            {QStringLiteral("version"), descriptor.version},
            {QStringLiteral("kind"), descriptor.kind()},
            {QStringLiteral("state"), state},
            {QStringLiteral("bypassed"), slot.bypassed},
            {QStringLiteral("available"), instance->live()},
            {QStringLiteral("latencyFrames"), instance->latency()},
            {QStringLiteral("sampleRate"), instance->activeSampleRate()},
            {QStringLiteral("error"), instance->error()},
            {QStringLiteral("editorAvailable"), instance->editorAvailable()},
            {QStringLiteral("editorOpen"), instance->editorOpen()},
            {QStringLiteral("processId"), instance->processId()},
            {QStringLiteral("savedStateBytes"), int(instance->savedState().size())}};
}

QVariantMap AudioPluginHost::instance(const QString &instanceId) const
{
    const auto found = m_slots.constFind(instanceId);
    return found == m_slots.cend() ? QVariantMap() : describeSlot(instanceId, *found);
}

QVariantMap AudioPluginHost::row(const QString &instanceId)
{
    const Slot slot = m_slots.value(instanceId);
    QVariantMap out = describeSlot(instanceId, slot);
    QVariantList parameters;
    for (const AudioPluginInstance::Parameter &parameter : slot.instance->parameters()) {
        if (!parameter.mapped) continue;
        AudioParameterBinding *binding = parameterBinding(instanceId, int(parameter.id));
        parameters.push_back(QVariantMap{
            {QStringLiteral("parameterId"), AudioPlugins::parameterId(instanceId, parameter.id)},
            {QStringLiteral("clapId"), qint64(parameter.id)},
            {QStringLiteral("name"), parameter.name},
            {QStringLiteral("displayValue"), binding ? binding->valueText() : parameter.text},
            {QStringLiteral("available"), slot.instance->live() && binding},
            {QStringLiteral("binding"), QVariant::fromValue<QObject *>(binding)}});
    }
    out.insert(QStringLiteral("parameters"), parameters);
    return out;
}

QVariantList AudioPluginHost::instances(const QString &channelId)
{
    QVariantList rows;
    for (const QString &id : m_chains.value(channelId)) rows.push_back(row(id));
    return rows;
}

QVariantMap AudioPluginHost::rackAction(const QString &action, const QString &instanceId, const QVariantMap &payload,
                                        QWindow *owner)
{
    if (action == QLatin1String("add"))
        return answer(addPlugin(payload.value(QStringLiteral("channelId")).toString(),
                                payload.value(QStringLiteral("pluginId")).toString(),
                                payload.value(QStringLiteral("index"), -1).toInt()));
    if (action == QLatin1String("reorder"))
        return answer(movePlugin(instanceId, payload.value(QStringLiteral("targetIndex")).toInt(),
                                 payload.value(QStringLiteral("channelId")).toString()));
    if (action == QLatin1String("bypass"))
        return answer(setBypassed(instanceId, payload.value(QStringLiteral("bypassed")).toBool()));
    if (action == QLatin1String("remove")) return answer(removePlugin(instanceId));
    if (action == QLatin1String("reload")) return answer(reload(instanceId));
    if (action == QLatin1String("open-editor")) return answer(openEditor(instanceId, owner));
    if (action == QLatin1String("close-editor")) return answer(closeEditor(instanceId));
    if (action == QLatin1String("edit-parameter")) {
        // {parameterId: "<instance>/<CLAP id>", value} or a numeric displayValue.
        const QString id = payload.value(QStringLiteral("parameterId")).toString();
        const QString prefix = instanceId + QLatin1Char('/');
        bool numeric = false;
        const int clapId = id.startsWith(prefix) ? id.mid(prefix.size()).toInt(&numeric) : 0;
        AudioParameterBinding *binding = numeric ? parameterBinding(instanceId, clapId) : nullptr;
        if (!binding) return answer(fail(tr("%1 is not a parameter of %2").arg(id, instanceId)));
        bool ok = payload.contains(QStringLiteral("value"));
        const double value = ok ? payload.value(QStringLiteral("value")).toDouble()
                                : payload.value(QStringLiteral("displayValue")).toString().trimmed().toDouble(&ok);
        if (!ok) return answer(fail(tr("Enter a number for %1").arg(binding->name())));
        if (!binding->begin(value) || !binding->commit(value)) return answer(fail(binding->lastError()));
        return answer({{QStringLiteral("ok"), true}});
    }
    return answer(fail(tr("Unknown plugin action %1").arg(action)));
}

// ---- session part ------------------------------------------------------------

QVariantMap AudioPluginHost::saveContent()
{
    refreshStates();
    QVariantList chains;
    for (const QString &channel : std::as_const(m_channels)) {
        const QStringList ids = m_chains.value(channel);
        if (ids.isEmpty()) continue;
        QVariantList inserts;
        for (const QString &id : ids) {
            const Slot slot = m_slots.value(id);
            const AudioPluginInstance *instance = slot.instance;
            QVariantList parameters;
            for (const AudioPluginInstance::Parameter &parameter : instance->parameters())
                parameters.push_back(parameterMap(parameter));
            inserts.push_back(QVariantMap{{QStringLiteral("instanceId"), id},
                                          {QStringLiteral("format"), instance->descriptor().format},
                                          {QStringLiteral("pluginId"), instance->pluginId()},
                                          {QStringLiteral("name"), instance->descriptor().name},
                                          {QStringLiteral("vendor"), instance->descriptor().vendor},
                                          {QStringLiteral("version"), instance->descriptor().version},
                                          {QStringLiteral("bypassed"), slot.bypassed},
                                          {QStringLiteral("state"), QString::fromLatin1(instance->savedState().toBase64())},
                                          {QStringLiteral("parameters"), parameters}});
        }
        chains.push_back(QVariantMap{{QStringLiteral("channelId"), channel}, {QStringLiteral("inserts"), inserts}});
    }
    return {{QStringLiteral("schema"), schemaId()}, {QStringLiteral("chains"), chains}};
}

bool AudioPluginHost::validateContent(const QVariantMap &content, QString *error)
{
    if (content.isEmpty()) return true; // a session saved before WP-066: no plugins
    if (content.value(QStringLiteral("schema")).toString() != schemaId()) {
        *error = tr("Unsupported plugin schema");
        return false;
    }
    QSet<QString> ids;
    for (const QVariant &chainValue : content.value(QStringLiteral("chains")).toList()) {
        const QVariantMap chain = chainValue.toMap();
        if (chain.value(QStringLiteral("channelId")).toString().isEmpty()) {
            *error = tr("A plugin chain names no channel");
            return false;
        }
        for (const QVariant &insertValue : chain.value(QStringLiteral("inserts")).toList()) {
            const QVariantMap insert = insertValue.toMap();
            const QString id = insert.value(QStringLiteral("instanceId")).toString();
            if (!AudioPlugins::isInstanceId(id) || ids.contains(id)) {
                *error = tr("Plugin instance IDs must be unique plugin:<uuid> IDs");
                return false;
            }
            ids.insert(id);
            if (insert.value(QStringLiteral("pluginId")).toString().isEmpty()) {
                *error = tr("The plugin instance %1 names no plugin").arg(id);
                return false;
            }
        }
    }
    return true;
}

// AGENT-GUARD (PLT-11): loading never drops an instance. One whose plugin
// is not installed is Missing, one that fails to start is Failed; both keep
// their saved state and parameter IDs and are saved again unchanged.
void AudioPluginHost::loadContent(const QVariantMap &content)
{
    clearAll();
    if (m_registry->scanState() == QLatin1String("idle") && m_registry->plugins().isEmpty()) m_registry->scanNow();
    for (const QVariant &chainValue : content.value(QStringLiteral("chains")).toList()) {
        const QVariantMap chain = chainValue.toMap();
        const QString channel = chain.value(QStringLiteral("channelId")).toString();
        for (const QVariant &insertValue : chain.value(QStringLiteral("inserts")).toList()) {
            const QVariantMap insert = insertValue.toMap();
            const QString id = insert.value(QStringLiteral("instanceId")).toString();
            AudioPluginInstance *instance = createInstance(id);
            instance->setPluginId(insert.value(QStringLiteral("pluginId")).toString());
            AudioPluginDescriptor saved;
            saved.pluginId = instance->pluginId();
            saved.format = insert.value(QStringLiteral("format")).toString();
            saved.name = insert.value(QStringLiteral("name")).toString();
            saved.vendor = insert.value(QStringLiteral("vendor")).toString();
            saved.version = insert.value(QStringLiteral("version")).toString();
            instance->setDescriptor(saved);
            instance->setSavedState(QByteArray::fromBase64(insert.value(QStringLiteral("state")).toString().toLatin1()));
            QList<AudioPluginInstance::Parameter> parameters;
            int index = 0;
            for (const QVariant &value : insert.value(QStringLiteral("parameters")).toList()) {
                const QVariantMap entry = value.toMap();
                AudioPluginInstance::Parameter parameter;
                parameter.id = quint32(entry.value(QStringLiteral("id")).toLongLong());
                parameter.name = entry.value(QStringLiteral("name")).toString();
                parameter.module = entry.value(QStringLiteral("module")).toString();
                parameter.minimum = entry.value(QStringLiteral("min")).toDouble();
                parameter.maximum = entry.value(QStringLiteral("max")).toDouble();
                parameter.defaultValue = entry.value(QStringLiteral("default")).toDouble();
                parameter.stepped = entry.value(QStringLiteral("stepped")).toBool();
                parameter.mapped = entry.value(QStringLiteral("mapped")).toBool();
                parameter.index = parameter.mapped ? index++ : -1;
                parameters.push_back(parameter);
            }
            instance->setParameters(parameters);
            m_slots.insert(id, {instance, channel, insert.value(QStringLiteral("bypassed")).toBool()});
            if (!m_channels.contains(channel)) m_channels.push_back(channel);
            m_chains[channel].push_back(id);
            const AudioPluginDescriptor *descriptor = m_registry->descriptor(instance->pluginId());
            if (!descriptor || !descriptor->available) {
                instance->markMissing(descriptor ? descriptor->reason
                                                 : tr("%1 is not installed").arg(saved.name.isEmpty() ? saved.pluginId : saved.name));
                continue;
            }
            if (instance->start(*descriptor, sampleRate())) ensureParameters(instance);
        }
    }
    if (!m_slots.isEmpty()) m_poll->start();
    emit browserChanged();
    bump(true);
}

} // namespace QindaTK
