// SPDX-License-Identifier: LGPL-3.0-or-later
// AudioPluginInstance: the host process, its control channel and the
// instance lifecycle (start, activate, state, parameters, failure, unload).
#include "audio_plugin_instance.h"

#include "audio_plugin_editor.h"
#include "audio_plugin_ipc_p.h"

#include <QDeadlineTimer>
#include <QJsonDocument>
#include <QProcess>
#include <algorithm>
#include <fcntl.h>
#include <utility>

namespace QindaTK {

AudioPluginInstance::AudioPluginInstance(const QString &instanceId, QObject *parent)
    : QObject(parent), m_instanceId(instanceId)
{
}

AudioPluginInstance::~AudioPluginInstance()
{
    if (m_process) {
        beginUnload();
        finishUnload(1000);
    }
    // A process fail() killed may not be reaped yet: it is dead, so the wait is short.
    for (QProcess *killed : findChildren<QProcess *>(Qt::FindDirectChildrenOnly)) {
        killed->disconnect();
        if (killed->state() != QProcess::NotRunning) killed->waitForFinished(1000);
        delete killed;
    }
    dropEditorWindow();
    if (m_processor) m_processor->setDead();
}

QString AudioPluginInstance::stateName() const
{
    switch (m_state) {
    case State::Missing: return QStringLiteral("missing");
    case State::Loading: return QStringLiteral("loading");
    case State::Ready: return QStringLiteral("ready");
    case State::Failed: return QStringLiteral("failed");
    case State::Unloaded: return QStringLiteral("unloaded");
    }
    return {};
}

const AudioPluginInstance::Parameter *AudioPluginInstance::parameter(quint32 id) const
{
    for (const Parameter &entry : m_parameters)
        if (entry.id == id) return &entry;
    return nullptr;
}

void AudioPluginInstance::markMissing(const QString &reason)
{
    m_state = State::Missing;
    m_error = reason;
    emit changed();
}

bool AudioPluginInstance::start(const AudioPluginDescriptor &descriptor, int sampleRate)
{
    if (m_process) return false;
    m_descriptor = descriptor;
    m_pluginId = descriptor.pluginId;
    m_error.clear();
    m_state = State::Loading;
    m_unloading = false;
    const QString host = AudioPlugins::hostExecutable();
    if (host.isEmpty()) {
        fail(tr("qindatk-plugin-host is not installed"));
        return false;
    }
    auto processor = std::make_shared<AudioPluginProcessor>();
    if (!processor->valid()) {
        fail(processor->error());
        return false;
    }
    // A reload gets a new block; graphs still holding the old (dead) one pass
    // dry until they are replaced.
    m_processor = processor;
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    const int fd = processor->fd();
    // AGENT-GUARD: the block's memfd is close-on-exec so no other child of
    // the application inherits it; only this host process gets it, here.
    m_process->setChildProcessModifier([fd] { ::fcntl(fd, F_SETFD, 0); });
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
        readLines();
        scheduleEvents();
    });
    connect(m_process, &QProcess::finished, this, [this] { processFinished(); });
    m_process->start(host, {QStringLiteral("--serve"), descriptor.path, descriptor.pluginId, QStringLiteral("--fd"),
                            QString::number(fd)});
    if (!m_process->waitForStarted(m_timeouts.requestMs)) {
        fail(tr("The plugin host could not start: %1").arg(m_process ? m_process->errorString() : QString()));
        return false;
    }
    processor->watchProcess(m_process->processId());
    QString error;
    const auto loaded = request({{QStringLiteral("op"), QStringLiteral("load")}}, m_timeouts.requestMs, &error);
    if (!loaded || loaded->value(QStringLiteral("protocol")).toInt() != PluginIpc::kProtocol) {
        fail(loaded ? tr("The plugin host speaks another protocol") : error);
        return false;
    }
    m_processId = loaded->value(QStringLiteral("processId")).toInteger();
    m_hasEditor = loaded->value(QStringLiteral("editor")).toBool();
    if (!m_savedState.isEmpty()) {
        const auto restored = request({{QStringLiteral("op"), QStringLiteral("state-load")},
                                       {QStringLiteral("data"), QString::fromLatin1(m_savedState.toBase64())}},
                                      m_timeouts.requestMs, &error);
        if (!restored) {
            // AGENT-GUARD: never replace a saved state the plugin refused:
            // the instance fails and the saved state stays for a later plugin.
            fail(tr("%1 did not accept its saved state: %2").arg(m_descriptor.name, error));
            return false;
        }
    }
    const auto parameters = request({{QStringLiteral("op"), QStringLiteral("params")}}, m_timeouts.requestMs, &error);
    if (!parameters) {
        fail(error);
        return false;
    }
    applyParameters(parameters->value(QStringLiteral("parameters")).toArray());
    if (!activate(sampleRate)) return false;
    if (m_savedState.isEmpty()) refreshState(); // something to recover after a crash
    m_state = State::Ready;
    emit changed();
    return true;
}

void AudioPluginInstance::applyParameters(const QJsonArray &list)
{
    m_parameters.clear();
    int index = 0;
    for (const QJsonValue &value : list) {
        const QJsonObject entry = value.toObject();
        Parameter parameter;
        parameter.id = quint32(entry.value(QStringLiteral("id")).toInteger());
        parameter.name = entry.value(QStringLiteral("name")).toString();
        parameter.module = entry.value(QStringLiteral("module")).toString();
        parameter.minimum = entry.value(QStringLiteral("min")).toDouble();
        parameter.maximum = entry.value(QStringLiteral("max")).toDouble();
        parameter.defaultValue = entry.value(QStringLiteral("default")).toDouble();
        parameter.value = entry.value(QStringLiteral("value")).toDouble();
        parameter.text = entry.value(QStringLiteral("text")).toString();
        parameter.stepped = entry.value(QStringLiteral("stepped")).toBool();
        // AGENT-NOTE (D-191): only parameters a host may write become
        // automation parameters; read-only, hidden and bypass parameters stay
        // the plugin's (the host bypasses by itself).
        parameter.mapped = entry.value(QStringLiteral("automatable")).toBool()
                           && !entry.value(QStringLiteral("hidden")).toBool()
                           && !entry.value(QStringLiteral("readonly")).toBool()
                           && !entry.value(QStringLiteral("bypass")).toBool() && parameter.maximum > parameter.minimum;
        parameter.index = parameter.mapped ? index++ : -1;
        m_parameters.push_back(parameter);
    }
}

bool AudioPluginInstance::activate(int sampleRate, int releaseTo)
{
    if (!m_processor || !m_process) return false;
    m_processor->claim(AudioPluginProcessor::Control, std::int64_t(m_timeouts.realtimeMs) * 1000000);
    QString error;
    const auto reply = request({{QStringLiteral("op"), QStringLiteral("activate")},
                                {QStringLiteral("sampleRate"), sampleRate},
                                {QStringLiteral("maxFrames"), PluginIpc::kMaxFrames}},
                               m_timeouts.requestMs, &error);
    if (!reply) {
        fail(tr("%1 could not be activated: %2").arg(m_descriptor.name, error));
        return false; // the processor stays claimed: renderers pass dry audio
    }
    m_latency = int(std::max<qint64>(0, reply->value(QStringLiteral("latency")).toInteger()));
    m_inputs = reply->value(QStringLiteral("audioInputs")).toInt();
    m_outputs = reply->value(QStringLiteral("audioOutputs")).toInt();
    QVector<quint32> ids;
    for (const Parameter &parameter : std::as_const(m_parameters))
        if (parameter.mapped) ids.push_back(parameter.id);
    m_processor->setTimeouts(m_timeouts.realtimeMs, m_timeouts.offlineMs, m_timeouts.hangMs);
    m_processor->configure(sampleRate, m_inputs, m_outputs, m_latency, ids);
    m_processor->release(releaseTo);
    m_activeRate = sampleRate;
    emit changed();
    return true;
}

bool AudioPluginInstance::refreshState()
{
    if (!m_process || !m_descriptor.state) return false;
    QString error;
    const auto reply = request({{QStringLiteral("op"), QStringLiteral("state-save")}}, m_timeouts.requestMs, &error);
    if (!reply) {
        if (m_timedOut) fail(error);
        return false;
    }
    m_savedState = QByteArray::fromBase64(reply->value(QStringLiteral("data")).toString().toLatin1());
    return true;
}

void AudioPluginInstance::sendParameters(const QList<QPair<quint32, double>> &values)
{
    if (!live() || values.isEmpty()) return;
    QJsonArray list;
    for (const auto &[id, value] : values) list.push_back(QJsonArray{qint64(id), value});
    QString error;
    if (!request({{QStringLiteral("op"), QStringLiteral("set-params")}, {QStringLiteral("values"), list}},
                 m_timeouts.requestMs, &error)
        && m_timedOut)
        fail(error);
}

void AudioPluginInstance::beginUnload()
{
    m_unloading = true;
    if (m_editorOpen || m_editor) closeEditor();
    if (!m_process || m_process->state() != QProcess::Running) return;
    m_process->write(QJsonDocument(QJsonObject{{QStringLiteral("id"), ++m_nextRequest},
                                               {QStringLiteral("op"), QStringLiteral("quit")}})
                         .toJson(QJsonDocument::Compact)
                     + '\n');
    m_process->waitForBytesWritten(200);
    m_process->closeWriteChannel(); // the host also leaves on end of input
}

void AudioPluginInstance::finishUnload(int deadlineMs)
{
    m_unloading = true;
    if (m_processor) m_processor->setDead();
    if (m_process) {
        if (m_process->state() != QProcess::NotRunning && !m_process->waitForFinished(deadlineMs)) {
            m_process->kill();
            m_process->waitForFinished(2000);
        }
        m_process->disconnect(this);
        delete m_process;
        m_process = nullptr;
    }
    if (m_state != State::Missing && m_state != State::Failed) m_state = State::Unloaded;
    m_unloading = false;
}

void AudioPluginInstance::fail(const QString &message)
{
    if (m_state == State::Failed && !m_process) return;
    m_state = State::Failed;
    m_error = message;
    if (m_processor) m_processor->setDead();
    const bool hadEditor = m_editorOpen;
    dropEditorWindow();
    m_editorOpen = false;
    if (m_process) {
        // Possibly inside one of its waitFor calls: stop it now, delete later.
        // AGENT-NOTE: a killed child is deleted once it has been reaped
        // (finished); deleting it earlier makes ~QProcess warn and block on
        // the wait. It stays parented to this instance, so an instance that
        // goes first still takes it along.
        QProcess *process = m_process;
        m_process = nullptr;
        process->disconnect(this);
        if (process->state() != QProcess::NotRunning) {
            connect(process, &QProcess::finished, process, &QObject::deleteLater);
            process->kill();
        } else {
            process->deleteLater();
        }
    }
    m_replies.clear();
    m_buffer.clear();
    if (hadEditor) emit editorChanged();
    emit failed(message);
    emit changed();
}

bool AudioPluginInstance::exited()
{
    if (!m_process) return m_state == State::Failed;
    // Collects an exit the event loop has not delivered yet; processFinished()
    // then fails the instance with the real cause.
    if (m_process->state() != QProcess::NotRunning) m_process->waitForFinished(0);
    return !m_process || m_process->state() == QProcess::NotRunning;
}

void AudioPluginInstance::processFinished()
{
    if (m_unloading || !m_process) return;
    const bool crashed = m_process->exitStatus() == QProcess::CrashExit;
    fail(crashed ? tr("%1 crashed; its saved state is kept").arg(m_descriptor.name)
                 : tr("%1 stopped unexpectedly (exit code %2)").arg(m_descriptor.name).arg(m_process->exitCode()));
}

std::optional<QJsonObject> AudioPluginInstance::request(QJsonObject message, int timeoutMs, QString *error)
{
    m_timedOut = false;
    const QString op = message.value(QStringLiteral("op")).toString();
    if (!m_process || m_process->state() != QProcess::Running) {
        *error = tr("The plugin process is not running");
        return std::nullopt;
    }
    const int id = ++m_nextRequest;
    message.insert(QStringLiteral("id"), id);
    QProcess *process = m_process;
    process->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
    QDeadlineTimer deadline(timeoutMs);
    process->waitForBytesWritten(int(std::max<qint64>(1, deadline.remainingTime())));
    while (!m_replies.contains(id)) {
        // fail() (a crash seen inside waitFor) clears m_process and deletes
        // the QProcess later, so `process` stays valid for this loop.
        if (m_process != process || process->state() != QProcess::Running) {
            *error = tr("The plugin process ended during “%1”").arg(op);
            return std::nullopt;
        }
        if (deadline.hasExpired()) {
            m_timedOut = true;
            *error = tr("The plugin did not answer “%1” within %2 ms").arg(op).arg(timeoutMs);
            return std::nullopt;
        }
        process->waitForReadyRead(int(std::max<qint64>(1, deadline.remainingTime())));
        readLines();
    }
    const QJsonObject reply = m_replies.take(id);
    scheduleEvents();
    if (!reply.value(QStringLiteral("ok")).toBool()) {
        *error = reply.value(QStringLiteral("error")).toString(tr("The plugin refused “%1”").arg(op));
        return std::nullopt;
    }
    return reply;
}

void AudioPluginInstance::readLines()
{
    if (!m_process) return;
    m_buffer += m_process->readAllStandardOutput();
    for (qsizetype end = m_buffer.indexOf('\n'); end >= 0; end = m_buffer.indexOf('\n')) {
        const QJsonObject object = QJsonDocument::fromJson(m_buffer.left(end)).object();
        m_buffer.remove(0, end + 1);
        if (object.contains(QStringLiteral("id"))) m_replies.insert(object.value(QStringLiteral("id")).toInt(), object);
        else if (object.contains(QStringLiteral("event"))) m_events.push_back(object);
    }
}

void AudioPluginInstance::scheduleEvents()
{
    // Events never run inside request(): they may start requests of their own.
    if (m_events.isEmpty() || m_eventsScheduled) return;
    m_eventsScheduled = true;
    QMetaObject::invokeMethod(this, [this] { dispatchEvents(); }, Qt::QueuedConnection);
}

void AudioPluginInstance::dispatchEvents()
{
    m_eventsScheduled = false;
    const QList<QJsonObject> events = std::exchange(m_events, {});
    for (const QJsonObject &event : events) handleEvent(event);
}

void AudioPluginInstance::handleEvent(const QJsonObject &event)
{
    const QString kind = event.value(QStringLiteral("event")).toString();
    const auto param = quint32(event.value(QStringLiteral("param")).toInteger());
    if (kind == QLatin1String("param")) emit parameterEdited(param, event.value(QStringLiteral("value")).toDouble());
    else if (kind == QLatin1String("gesture")) emit gestureEdited(param, event.value(QStringLiteral("begin")).toBool());
    else if (kind == QLatin1String("restart")) emit restartRequested();
    else if (kind == QLatin1String("state-dirty")) emit stateDirty();
    else if (kind == QLatin1String("fatal")) fail(event.value(QStringLiteral("message")).toString());
    else if (kind.startsWith(QLatin1String("gui-"))) editorEvent(event);
}

} // namespace QindaTK
