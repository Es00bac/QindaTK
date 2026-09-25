// SPDX-License-Identifier: LGPL-3.0-or-later
// ClapInstance: the main-thread side of one hosted CLAP plugin: creation,
// activation, parameters, state and shutdown, and the host extensions the
// plugin may call. The editor, timers and file descriptors are in
// plugin_host_editor.cpp.
#include "plugin_host_clap.h"

#include <QSocketNotifier>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace QindaTK::PluginHost {
namespace {

ClapInstance *self(const clap_host_t *host)
{
    return static_cast<ClapInstance *>(host->host_data);
}

// ---- host extensions --------------------------------------------------------
void logMessage(const clap_host_t *host, clap_log_severity severity, const char *message)
{
    self(host)->emitEvent({{QStringLiteral("event"), QStringLiteral("log")},
                           {QStringLiteral("severity"), int(severity)},
                           {QStringLiteral("message"), QString::fromUtf8(message ? message : "")}});
}
const clap_host_log_t kLog{logMessage};

bool checkMain(const clap_host_t *host) { return self(host)->isMainThread(); }
bool checkAudio(const clap_host_t *host) { return self(host)->isAudioThread(); }
const clap_host_thread_check_t kThreadCheck{checkMain, checkAudio};

void paramsRescan(const clap_host_t *host, clap_param_rescan_flags flags)
{
    self(host)->emitEvent({{QStringLiteral("event"), QStringLiteral("params-rescan")}, {QStringLiteral("flags"), int(flags)}});
}
void paramsClear(const clap_host_t *, clap_id, clap_param_clear_flags) {}
void paramsFlush(const clap_host_t *host) { self(host)->requestFlush(); }
const clap_host_params_t kParams{paramsRescan, paramsClear, paramsFlush};

void stateDirty(const clap_host_t *host)
{
    self(host)->emitEvent({{QStringLiteral("event"), QStringLiteral("state-dirty")}});
}
const clap_host_state_t kState{stateDirty};

void latencyChanged(const clap_host_t *) {} // read again after every activate
const clap_host_latency_t kLatency{latencyChanged};

bool portsFlagSupported(const clap_host_t *, uint32_t) { return false; }
void portsRescan(const clap_host_t *host, uint32_t)
{
    self(host)->emitEvent({{QStringLiteral("event"), QStringLiteral("ports-rescan")}});
}
const clap_host_audio_ports_t kAudioPorts{portsFlagSupported, portsRescan};

uint32_t noteDialects(const clap_host_t *) { return CLAP_NOTE_DIALECT_CLAP; }
void notePortsRescan(const clap_host_t *, uint32_t) {}
const clap_host_note_ports_t kNotePorts{noteDialects, notePortsRescan};

void guiHintsChanged(const clap_host_t *) {}
bool guiRequestResize(const clap_host_t *host, uint32_t width, uint32_t height)
{
    self(host)->emitEvent({{QStringLiteral("event"), QStringLiteral("gui-resize")},
                           {QStringLiteral("width"), int(width)},
                           {QStringLiteral("height"), int(height)}});
    return true;
}
bool guiRequestShow(const clap_host_t *host)
{
    self(host)->emitEvent({{QStringLiteral("event"), QStringLiteral("gui-show")}});
    return true;
}
bool guiRequestHide(const clap_host_t *host)
{
    self(host)->emitEvent({{QStringLiteral("event"), QStringLiteral("gui-hide")}});
    return true;
}
void guiClosed(const clap_host_t *host, bool destroyed)
{
    self(host)->emitEvent({{QStringLiteral("event"), QStringLiteral("gui-closed")}, {QStringLiteral("destroyed"), destroyed}});
}
const clap_host_gui_t kGui{guiHintsChanged, guiRequestResize, guiRequestShow, guiRequestHide, guiClosed};

bool timerRegister(const clap_host_t *host, uint32_t period, clap_id *id) { return self(host)->registerTimer(period, id); }
bool timerUnregister(const clap_host_t *host, clap_id id) { return self(host)->unregisterTimer(id); }
const clap_host_timer_support_t kTimer{timerRegister, timerUnregister};

bool fdRegister(const clap_host_t *host, int fd, clap_posix_fd_flags_t flags) { return self(host)->registerFd(fd, flags); }
bool fdModify(const clap_host_t *host, int fd, clap_posix_fd_flags_t flags)
{
    return self(host)->unregisterFd(fd) && self(host)->registerFd(fd, flags);
}
bool fdUnregister(const clap_host_t *host, int fd) { return self(host)->unregisterFd(fd); }
const clap_host_posix_fd_support_t kFd{fdRegister, fdModify, fdUnregister};

const void *hostGetExtension(const clap_host_t *host, const char *id) { return self(host)->hostExtension(id); }
void hostRequestRestart(const clap_host_t *host) { self(host)->requestRestart(); }
void hostRequestProcess(const clap_host_t *) {} // the library decides when blocks run
void hostRequestCallback(const clap_host_t *host) { self(host)->requestCallback(); }

// State streams over a QByteArray.
int64_t writeStream(const clap_ostream_t *stream, const void *buffer, uint64_t size)
{
    static_cast<QByteArray *>(stream->ctx)->append(static_cast<const char *>(buffer), qsizetype(size));
    return int64_t(size);
}
struct ReadCursor {
    const QByteArray *data = nullptr;
    qsizetype offset = 0;
};
int64_t readStream(const clap_istream_t *stream, void *buffer, uint64_t size)
{
    auto *cursor = static_cast<ReadCursor *>(stream->ctx);
    const qsizetype count = std::min<qsizetype>(qsizetype(size), cursor->data->size() - cursor->offset);
    if (count <= 0) return 0;
    std::memcpy(buffer, cursor->data->constData() + cursor->offset, std::size_t(count));
    cursor->offset += count;
    return count;
}

} // namespace

ClapInstance::ClapInstance(const clap_plugin_factory_t *factory, Emit emitter)
    : m_factory(factory), m_emit(std::move(emitter)), m_mainThread(std::this_thread::get_id())
{
    m_host.clap_version = CLAP_VERSION;
    m_host.host_data = this;
    m_host.name = "QindaTK";
    m_host.vendor = "QindaTK";
    m_host.url = "https://sloom.studio";
    m_host.version = "0.1";
    m_host.get_extension = hostGetExtension;
    m_host.request_restart = hostRequestRestart;
    m_host.request_process = hostRequestProcess;
    m_host.request_callback = hostRequestCallback;
    m_eventStorage.resize(std::size_t(PluginIpc::kMaxEvents) + 16 * 128);
    m_eventList.reserve(m_eventStorage.size());
    m_output.reserve(4096);
    m_draining.reserve(4096);
}

ClapInstance::~ClapInstance()
{
    shutdown();
}

const void *ClapInstance::hostExtension(const char *id) const
{
    if (!id) return nullptr;
    const auto is = [id](const char *name) { return std::strcmp(id, name) == 0; };
    if (is(CLAP_EXT_LOG)) return &kLog;
    if (is(CLAP_EXT_THREAD_CHECK)) return &kThreadCheck;
    if (is(CLAP_EXT_PARAMS)) return &kParams;
    if (is(CLAP_EXT_STATE)) return &kState;
    if (is(CLAP_EXT_LATENCY)) return &kLatency;
    if (is(CLAP_EXT_AUDIO_PORTS)) return &kAudioPorts;
    if (is(CLAP_EXT_NOTE_PORTS)) return &kNotePorts;
    if (is(CLAP_EXT_GUI)) return &kGui;
    if (is(CLAP_EXT_TIMER_SUPPORT)) return &kTimer;
    if (is(CLAP_EXT_POSIX_FD_SUPPORT)) return &kFd;
    return nullptr;
}

bool ClapInstance::create(const QString &pluginId, QString *error)
{
    const QByteArray id = pluginId.toUtf8();
    m_plugin = m_factory ? m_factory->create_plugin(m_factory, &m_host, id.constData()) : nullptr;
    if (!m_plugin) {
        *error = QStringLiteral("The binary does not create a plugin %1").arg(pluginId);
        return false;
    }
    if (!m_plugin->init(m_plugin)) {
        m_plugin->destroy(m_plugin);
        m_plugin = nullptr;
        *error = QStringLiteral("The plugin %1 failed to initialise").arg(pluginId);
        return false;
    }
    const auto ext = [this](const char *name) { return m_plugin->get_extension(m_plugin, name); };
    m_params = static_cast<const clap_plugin_params_t *>(ext(CLAP_EXT_PARAMS));
    m_state = static_cast<const clap_plugin_state_t *>(ext(CLAP_EXT_STATE));
    m_latency = static_cast<const clap_plugin_latency_t *>(ext(CLAP_EXT_LATENCY));
    m_audioPorts = static_cast<const clap_plugin_audio_ports_t *>(ext(CLAP_EXT_AUDIO_PORTS));
    m_notePorts = static_cast<const clap_plugin_note_ports_t *>(ext(CLAP_EXT_NOTE_PORTS));
    m_gui = static_cast<const clap_plugin_gui_t *>(ext(CLAP_EXT_GUI));
    m_timerExt = static_cast<const clap_plugin_timer_support_t *>(ext(CLAP_EXT_TIMER_SUPPORT));
    m_fdExt = static_cast<const clap_plugin_posix_fd_support_t *>(ext(CLAP_EXT_POSIX_FD_SUPPORT));
    m_noteInputs = m_notePorts ? int(m_notePorts->count(m_plugin, true)) : 0;
    queryPorts(true, m_inputs);
    queryPorts(false, m_outputs);
    return true;
}

void ClapInstance::queryPorts(bool input, PortSet &ports) const
{
    ports = {};
    ports.count = m_audioPorts ? int(m_audioPorts->count(m_plugin, input)) : 0;
    ports.buffers.resize(std::size_t(ports.count));
    for (int i = 0; i < ports.count; ++i) {
        clap_audio_port_info_t info{};
        if (!m_audioPorts->get(m_plugin, quint32(i), input, &info)) info.channel_count = 0;
        ports.buffers[std::size_t(i)].channel_count = info.channel_count;
        if (ports.main < 0 && ((info.flags & CLAP_AUDIO_PORT_IS_MAIN) != 0)) ports.main = i;
    }
    if (ports.main < 0 && ports.count > 0) ports.main = 0;
    ports.mainChannels = ports.main < 0 ? 0 : int(ports.buffers[std::size_t(ports.main)].channel_count);
}

// Every declared port gets buffers (CLAP expects all of them); the main port's
// first two channels are the block's lanes, every other channel scratch.
void ClapInstance::preparePorts(PortSet &ports, quint32 maxFrames) const
{
    std::size_t total = 0;
    for (const auto &buffer : ports.buffers) total += buffer.channel_count;
    ports.pointers.assign(total, nullptr);
    ports.scratch.assign(total, std::vector<float>(maxFrames, 0.0f));
    std::size_t next = 0;
    for (int i = 0; i < ports.count; ++i) {
        clap_audio_buffer_t &buffer = ports.buffers[std::size_t(i)];
        buffer.data32 = ports.pointers.data() + next;
        buffer.data64 = nullptr;
        buffer.latency = 0;
        buffer.constant_mask = 0;
        for (quint32 c = 0; c < buffer.channel_count; ++c) ports.pointers[next + c] = ports.scratch[next + c].data();
        next += buffer.channel_count;
    }
}

QJsonObject ClapInstance::describe() const
{
    return {{QStringLiteral("audioInputs"), m_inputs.mainChannels},
            {QStringLiteral("audioOutputs"), m_outputs.mainChannels},
            {QStringLiteral("noteInputs"), m_noteInputs},
            {QStringLiteral("state"), m_state != nullptr},
            {QStringLiteral("editor"), m_gui != nullptr},
            {QStringLiteral("parameters"), parameters()}};
}

bool ClapInstance::activate(double sampleRate, quint32 maxFrames, QString *error)
{
    deactivate();
    preparePorts(m_inputs, maxFrames);
    preparePorts(m_outputs, maxFrames);
    if (!m_plugin->activate(m_plugin, sampleRate, 1, maxFrames)) {
        *error = QStringLiteral("The plugin refused to activate at %1 Hz").arg(sampleRate);
        return false;
    }
    m_active.store(true, std::memory_order_release);
    return true;
}

void ClapInstance::deactivate()
{
    if (!m_plugin || !m_active.load(std::memory_order_acquire)) return;
    // AGENT-GUARD: CLAP forbids deactivate() while processing. Inactive
    // first, so a block that still arrives is answered NotActive without
    // process(); then the audio thread stops processing (it owns
    // start/stop_processing). The library normally posts no block meanwhile
    // (AudioPluginProcessor::claim).
    m_active.store(false, std::memory_order_release);
    if (m_audioThread.joinable()) {
        m_stopDone.store(0, std::memory_order_release);
        m_stopRequest.store(true, std::memory_order_release);
        PluginIpc::futexWake(&m_block->request);
        const std::int64_t deadline = PluginIpc::monotonicNs() + 2000000000;
        while (m_stopDone.load(std::memory_order_acquire) == 0) {
            const std::int64_t left = deadline - PluginIpc::monotonicNs();
            if (left <= 0) break;
            const timespec wait = PluginIpc::relative(std::min<std::int64_t>(left, 50000000));
            PluginIpc::futexWait(&m_stopDone, 0, &wait);
        }
    }
    m_plugin->deactivate(m_plugin);
}

quint32 ClapInstance::latency() const
{
    return m_latency && m_active.load(std::memory_order_acquire) ? m_latency->get(m_plugin) : 0;
}

QJsonArray ClapInstance::parameters() const
{
    QJsonArray list;
    const quint32 count = m_params ? m_params->count(m_plugin) : 0;
    for (quint32 i = 0; i < count; ++i) {
        clap_param_info_t info{};
        if (!m_params->get_info(m_plugin, i, &info)) continue;
        double value = info.default_value;
        m_params->get_value(m_plugin, info.id, &value);
        char text[256] = {};
        const bool formatted = m_params->value_to_text(m_plugin, info.id, value, text, sizeof text);
        list.push_back(QJsonObject{
            {QStringLiteral("id"), qint64(info.id)},
            {QStringLiteral("name"), QString::fromUtf8(info.name)},
            {QStringLiteral("module"), QString::fromUtf8(info.module)},
            {QStringLiteral("min"), info.min_value},
            {QStringLiteral("max"), info.max_value},
            {QStringLiteral("default"), info.default_value},
            {QStringLiteral("value"), value},
            {QStringLiteral("text"), formatted ? QString::fromUtf8(text) : QString()},
            {QStringLiteral("automatable"), (info.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0},
            {QStringLiteral("hidden"), (info.flags & CLAP_PARAM_IS_HIDDEN) != 0},
            {QStringLiteral("readonly"), (info.flags & CLAP_PARAM_IS_READONLY) != 0},
            {QStringLiteral("stepped"), (info.flags & CLAP_PARAM_IS_STEPPED) != 0},
            {QStringLiteral("bypass"), (info.flags & CLAP_PARAM_IS_BYPASS) != 0}});
    }
    return list;
}

bool ClapInstance::saveState(QByteArray *out, QString *error)
{
    if (!m_state) {
        *error = QStringLiteral("The plugin has no state extension");
        return false;
    }
    out->clear();
    clap_ostream_t stream{out, writeStream};
    if (!m_state->save(m_plugin, &stream)) {
        *error = QStringLiteral("The plugin could not save its state");
        return false;
    }
    return true;
}

bool ClapInstance::loadState(const QByteArray &data, QString *error)
{
    if (!m_state) {
        *error = QStringLiteral("The plugin has no state extension");
        return false;
    }
    ReadCursor cursor{&data, 0};
    clap_istream_t stream{&cursor, readStream};
    if (!m_state->load(m_plugin, &stream)) {
        *error = QStringLiteral("The plugin refused the saved state");
        return false;
    }
    return true;
}

void ClapInstance::requestFlush()
{
    m_flushPending.store(true, std::memory_order_release);
    if (m_block && m_active.load(std::memory_order_acquire)) PluginIpc::futexWake(&m_block->request);
}

void ClapInstance::setParameters(const QVector<QPair<quint32, double>> &values)
{
    if (!m_params || values.isEmpty()) return;
    {
        std::lock_guard lock(m_flushMutex);
        m_flushValues += values;
    }
    if (m_active.load(std::memory_order_acquire)) {
        // The audio thread flushes between blocks; the answer waits for it,
        // so a state saved next already holds these values.
        const std::uint32_t before = m_flushDone.load(std::memory_order_acquire);
        requestFlush();
        const std::int64_t deadline = PluginIpc::monotonicNs() + 1000000000;
        while (m_flushDone.load(std::memory_order_acquire) == before) {
            const std::int64_t left = deadline - PluginIpc::monotonicNs();
            if (left <= 0) break;
            const timespec wait = PluginIpc::relative(std::min<std::int64_t>(left, 20000000));
            PluginIpc::futexWait(&m_flushDone, before, &wait);
        }
        drainOutput();
        return;
    }
    flushQueued(); // inactive: CLAP wants the main thread
    drainOutput();
}

void ClapInstance::tick()
{
    if (!m_plugin) return;
    if (m_callbackRequested.exchange(false, std::memory_order_acq_rel)) m_plugin->on_main_thread(m_plugin);
    if (m_restartRequested.exchange(false, std::memory_order_acq_rel))
        m_emit({{QStringLiteral("event"), QStringLiteral("restart")}});
    // A flush the plugin asked for while inactive belongs to the main thread.
    if (!m_active.load(std::memory_order_acquire) && m_flushPending.exchange(false, std::memory_order_acq_rel))
        flushQueued();
    drainOutput();
}

void ClapInstance::drainOutput()
{
    {
        std::lock_guard lock(m_outputMutex);
        m_draining.swap(m_output);
    }
    for (const PluginIpc::Event &event : m_draining) {
        const bool value = event.type == PluginIpc::ParamValue;
        if (!value && event.type != PluginIpc::GestureBegin && event.type != PluginIpc::GestureEnd) continue;
        QJsonObject message{{QStringLiteral("event"), value ? QStringLiteral("param") : QStringLiteral("gesture")},
                            {QStringLiteral("param"), qint64(event.param)}};
        if (value) message.insert(QStringLiteral("value"), event.value);
        else message.insert(QStringLiteral("begin"), event.type == PluginIpc::GestureBegin);
        m_emit(message);
    }
    m_draining.clear();
}

void ClapInstance::shutdown()
{
    if (!m_plugin) return;
    closeEditor();
    deactivate();
    m_quit.store(true, std::memory_order_release);
    if (m_block) PluginIpc::futexWake(&m_block->request);
    if (m_audioThread.joinable()) m_audioThread.join();
    for (QTimer *timer : std::as_const(m_timers)) delete timer;
    m_timers.clear();
    for (const auto &notifiers : std::as_const(m_fds))
        for (QSocketNotifier *notifier : notifiers) delete notifier;
    m_fds.clear();
    m_plugin->destroy(m_plugin);
    m_plugin = nullptr;
}

} // namespace QindaTK::PluginHost
