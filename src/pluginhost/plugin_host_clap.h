// SPDX-License-Identifier: LGPL-3.0-or-later
// qindatk-plugin-host: the out-of-process CLAP host (D-190, docs/audio-plugins.md).
#pragma once

#include "audio_plugin_ipc_p.h"

#include <clap/clap.h>

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class QSocketNotifier;
class QTimer;

namespace QindaTK::PluginHost {

// One CLAP binary: dlopen, clap_entry->init, the plugin factory. The entry is
// deinitialized and the library closed on destruction.
class ClapLibrary {
public:
    ClapLibrary() = default;
    ClapLibrary(const ClapLibrary &) = delete;
    ClapLibrary &operator=(const ClapLibrary &) = delete;
    ~ClapLibrary();
    bool open(const QString &path, QString *error);
    const clap_plugin_factory_t *factory() const { return m_factory; }
    // Scan: every descriptor with its ports, parameters and editor support.
    // Each plugin is created and initialized with a host that offers nothing.
    QJsonArray describe() const;

private:
    void *m_handle = nullptr;
    const clap_plugin_entry_t *m_entry = nullptr;
    const clap_plugin_factory_t *m_factory = nullptr;
    bool m_initialized = false;
};

struct PortSet {
    int count = 0;
    int main = -1;            // the port the block's lanes feed/receive
    int mainChannels = 0;
    std::vector<clap_audio_buffer_t> buffers;
    std::vector<float *> pointers;
    std::vector<std::vector<float>> scratch; // non-main channels (never allocated on the audio thread)
};

// One plugin instance in this process. Main-thread calls come from the
// control channel; processBlock() runs on the audio thread (audioLoop) when
// the library posts a block. Output events (parameter changes and gestures
// made in the plugin's editor) travel to the main thread and out as JSON.
class ClapInstance {
public:
    using Emit = std::function<void(const QJsonObject &)>;
    ClapInstance(const clap_plugin_factory_t *factory, Emit emitter);
    ClapInstance(const ClapInstance &) = delete;
    ClapInstance &operator=(const ClapInstance &) = delete;
    ~ClapInstance();

    bool create(const QString &pluginId, QString *error);
    QJsonObject describe() const;
    bool activate(double sampleRate, quint32 maxFrames, QString *error);
    void deactivate();
    quint32 latency() const;
    int inputChannels() const { return m_inputs.mainChannels; }
    int outputChannels() const { return m_outputs.mainChannels; }
    QJsonArray parameters() const;
    bool saveState(QByteArray *out, QString *error);
    bool loadState(const QByteArray &data, QString *error);
    // Parameter values outside process(): params->flush on the main thread
    // while inactive, on the audio thread (between blocks) while active.
    void setParameters(const QVector<QPair<quint32, double>> &values);
    QJsonObject openEditor(const QJsonObject &request, QString *error);
    bool closeEditor();
    bool setEditorSize(quint32 width, quint32 height);
    void attach(PluginIpc::Block *block);
    void tick(); // main thread, every 10 ms
    void shutdown();

    // Host callbacks (C ABI trampolines use these).
    const void *hostExtension(const char *id) const;
    void requestRestart() { m_restartRequested.store(true, std::memory_order_release); }
    void requestCallback() { m_callbackRequested.store(true, std::memory_order_release); }
    void requestFlush();
    bool isMainThread() const { return std::this_thread::get_id() == m_mainThread; }
    bool isAudioThread() const { return std::this_thread::get_id() == m_audioThreadId.load(std::memory_order_acquire); }
    void emitEvent(const QJsonObject &event) { m_emit(event); }
    bool registerTimer(quint32 periodMs, clap_id *id);
    bool unregisterTimer(clap_id id);
    bool registerFd(int fd, clap_posix_fd_flags_t flags);
    bool unregisterFd(int fd);
    void pushOutput(const PluginIpc::Event &event);

private:
    void queryPorts(bool input, PortSet &ports) const;
    void preparePorts(PortSet &ports, quint32 maxFrames) const;
    void audioLoop();
    void processBlock() noexcept;
    void flushQueued() noexcept;
    void buildEvents(const PluginIpc::Block &block) noexcept;
    void drainOutput();

    const clap_plugin_factory_t *m_factory = nullptr;
    Emit m_emit;
    clap_host_t m_host{};
    const clap_plugin_t *m_plugin = nullptr;
    const clap_plugin_params_t *m_params = nullptr;
    const clap_plugin_state_t *m_state = nullptr;
    const clap_plugin_latency_t *m_latency = nullptr;
    const clap_plugin_audio_ports_t *m_audioPorts = nullptr;
    const clap_plugin_note_ports_t *m_notePorts = nullptr;
    const clap_plugin_gui_t *m_gui = nullptr;
    const clap_plugin_timer_support_t *m_timerExt = nullptr;
    const clap_plugin_posix_fd_support_t *m_fdExt = nullptr;
    int m_noteInputs = 0;
    PortSet m_inputs;
    PortSet m_outputs;
    bool m_guiCreated = false;
    bool m_guiFloating = false;

    std::thread::id m_mainThread;
    std::atomic<std::thread::id> m_audioThreadId{};
    std::thread m_audioThread;
    PluginIpc::Block *m_block = nullptr;
    std::atomic<bool> m_active{false};
    std::atomic<bool> m_quit{false};
    std::atomic<bool> m_stopRequest{false};
    std::atomic<std::uint32_t> m_stopDone{0};
    std::atomic<bool> m_flushPending{false};
    std::atomic<std::uint32_t> m_flushDone{0}; // flushes the audio thread completed
    std::atomic<bool> m_restartRequested{false};
    std::atomic<bool> m_callbackRequested{false};
    bool m_processing = false; // audio thread only

    // Input events of one block: the block's events plus note-offs for
    // notes held across a discontinuity. Sized once; never grows on the audio thread.
    union EventStorage {
        clap_event_header_t header;
        clap_event_note_t note;
        clap_event_param_value_t param;
        clap_event_param_gesture_t gesture;
    };
    std::vector<EventStorage> m_eventStorage;
    std::vector<const clap_event_header_t *> m_eventList;
    clap_input_events_t m_inEvents{};
    clap_output_events_t m_outEvents{};
    std::uint8_t m_held[16][128] = {}; // notes sounding in the plugin (audio thread)

    std::mutex m_flushMutex;
    QVector<QPair<quint32, double>> m_flushValues;
    std::mutex m_outputMutex;
    std::vector<PluginIpc::Event> m_output;   // reserved; the audio thread never grows it
    std::vector<PluginIpc::Event> m_draining; // main thread swap partner

    QHash<clap_id, QTimer *> m_timers;
    clap_id m_nextTimer = 1;
    QHash<int, QVector<QSocketNotifier *>> m_fds;
};

} // namespace QindaTK::PluginHost
