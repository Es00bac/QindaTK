// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "audio_device.h"
#include "audio_graph.h"
#include "transport_model.h"

#include <QFutureWatcher>
#include <QHash>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <memory>
#include <optional>
#include <utility>

namespace QindaTK {

class AudioEngineProcessor;
class AudioTakeWriter;
struct EngineEvent;

// AGENT-CONTRACT: the real audio engine seam (WP-060, docs/audio-engine.md).
// GUI thread: device list, acknowledged reconfiguration, transport commands
// and telemetry. Worker thread: opening, stopping and closing devices.
// Device thread: AudioEngineProcessor only. The engine is the
// TransportAdapter of its own `transport` model, renders the AudioRenderGraph
// offered by its session, and reports finished takes; it never selects a
// device by index and never replaces a lost device on its own.
class AudioEngine : public TransportAdapter {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(QVariantMap activeConfiguration READ activeConfiguration NOTIFY stateChanged)
    Q_PROPERTY(QString state READ stateName NOTIFY stateChanged)
    Q_PROPERTY(QString stateMessage READ stateMessage NOTIFY stateChanged)
    Q_PROPERTY(int sampleRate READ sampleRate NOTIFY formatChanged)
    Q_PROPERTY(int bufferSize READ bufferSize NOTIFY formatChanged)
    Q_PROPERTY(QVariantMap statistics READ statistics NOTIFY statisticsChanged)
    Q_PROPERTY(QindaTK::TransportModel *transport READ transport CONSTANT)
    Q_PROPERTY(QString recordDirectory READ recordDirectory WRITE setRecordDirectory NOTIFY recordDirectoryChanged)
public:
    enum class State { Stopped, Configuring, Running, Lost, Error };
    Q_ENUM(State)

    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine() override;

    // ---- TransportAdapter ----
    TransportSnapshot snapshot() const override;
    void submit(const TransportCommand &command, ReplyCallback completion) override;
    void setSnapshotSink(SnapshotCallback sink) override;

    // ---- C++ seam ----
    // Replaces the device backends (default: built-in, then ALSA when built).
    void setBackends(const QList<AudioDeviceBackendPtr> &backends);
    BuiltinAudioBackend *builtinBackend() const;
    // Graphs whose rate differs from the running stream are held, not played.
    void setGraph(AudioRenderGraphPtr graph);
    AudioRenderGraphPtr graph() const { return m_graph; }
    void setRecordTarget(bool available, const QString &reason);
    AudioStreamFormat streamFormat() const;
    quint64 realtimeThreadToken() const;
    State stateValue() const { return m_state; }

    QVariantList devices() const { return m_devices; }
    QVariantMap activeConfiguration() const;
    QString stateName() const;
    QString stateMessage() const { return m_stateMessage; }
    int sampleRate() const { return m_rate; }
    int bufferSize() const { return m_bufferSize; }
    QVariantMap statistics() const { return m_statistics; }
    TransportModel *transport() const { return m_transport; }
    QString recordDirectory() const { return m_recordDirectory; }
    void setRecordDirectory(const QString &directory);

    Q_INVOKABLE void refreshDevices();
    // Always answers with configurationApplied(requestId, "accepted" |
    // "rejected" | "error", activeConfiguration, message); returns false when
    // the answer was an immediate rejection.
    Q_INVOKABLE bool applyConfiguration(const QString &requestId, const QVariantMap &configuration);
    Q_INVOKABLE void stop();
    // Simulated hot-unplug/replug of a built-in reference device.
    Q_INVOKABLE bool setBuiltinDeviceConnected(const QString &deviceId, bool connected);

signals:
    void devicesChanged();
    void stateChanged();
    void formatChanged();
    void statisticsChanged();
    void recordDirectoryChanged();
    void configurationApplied(const QString &requestId, const QString &state,
                              const QVariantMap &activeConfiguration, const QString &message);
    // {path, startSample, recordedSample, frames, sampleRate, channels,
    //  droppedFrames, latencyFrames, deviceLost, error}
    void recordingFinished(const QVariantMap &take);

private:
    struct Run;
    struct Job;
    struct Take;
    struct SharedDirectory;
    enum class JobKind { Apply, Stop, Lost };
    AudioDeviceBackendPtr backendFor(const QString &deviceId) const;
    void startJob(JobKind kind, const QString &requestId, std::optional<AudioDeviceConfig> next);
    void finishJob();
    QString startRun(AudioDeviceStreamPtr stream, const AudioDeviceConfig &config, int previousRate);
    void drain(Run &run);
    void poll();
    void handleEvent(Run &run, const EngineEvent &event);
    void takeWritten(const QString &key, const QString &path, qint64 frames, const QString &error);
    void completeTake(const QString &key);
    void rejectPending(const QString &reason);
    void offerGraph();
    void retireGraphs();
    void setState(State state, const QString &message);
    void publishSnapshot(bool force);
    void updateStatistics();
    TransportSnapshot compose() const;
    static QString eventText(int code);

    QList<AudioDeviceBackendPtr> m_backends;
    std::shared_ptr<BuiltinAudioBackend> m_builtin;
    QVariantList m_devices;
    std::optional<AudioDeviceConfig> m_active;
    QString m_activeName;
    State m_state = State::Stopped;
    QString m_stateMessage;
    int m_rate = 0;
    int m_bufferSize = 0;
    QVariantMap m_statistics;
    TransportModel *m_transport = nullptr;
    QString m_recordDirectory;
    std::shared_ptr<SharedDirectory> m_sharedDirectory; // read by the take writer thread
    bool m_recordAvailable = false;
    QString m_recordReason;

    std::shared_ptr<Run> m_run;
    std::unique_ptr<Job> m_job;
    std::optional<std::pair<QString, AudioDeviceConfig>> m_queuedApply; // waits for a teardown
    QFutureWatcher<void> m_watcher;
    QTimer m_poll;
    QTimer m_deviceWatch;
    int m_statisticsTicks = 0;
    quint64 m_runSerial = 0;
    bool m_lost = false;

    AudioRenderGraphPtr m_graph;
    QList<AudioRenderGraphPtr> m_live; // offered to the running processor

    struct Idle { qint64 position = 0; bool loopEnabled = false; qint64 loopStart = 0; qint64 loopEnd = 0; };
    Idle m_idle;
    struct Pending { QString id; TransportCommand command; ReplyCallback callback; };
    QHash<quint64, Pending> m_pending;
    quint64 m_commandSerial = 0;
    QHash<QString, std::shared_ptr<Take>> m_takes;
    SnapshotCallback m_sink;
    TransportSnapshot m_last;
    quint64 m_revision = 0;
};

} // namespace QindaTK
