// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "audio_device.h"
#include "audio_graph.h"
#include "audio_loudness_model.h"
#include "audio_meter_model.h"
#include "transport_model.h"

#include <QFutureWatcher>
#include <QHash>
#include <QSet>
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
// device by index and never replaces a lost device on its own. Its output is
// measured on the device thread (AudioMeterTap) and drained each poll into
// `outputMeter` (peak/RMS) and `outputLoudness` (BS.1770) on the GUI thread;
// every mixer channel's post-fader level reaches channelMeter(id) the same
// way (AudioChannelMeterSink, WP-062, D-113).
class AudioEngine : public TransportAdapter {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    // [{id: "builtin" | "alsa", available, reason}]: which device backends
    // this build and this engine offer, and why one is absent (WP-088, D-203).
    Q_PROPERTY(QVariantList backends READ backends NOTIFY backendsChanged)
    Q_PROPERTY(QindaTK::AudioMeterModel *outputMeter READ outputMeter CONSTANT)
    Q_PROPERTY(QindaTK::AudioLoudnessModel *outputLoudness READ outputLoudness CONSTANT)
    Q_PROPERTY(QVariantMap activeConfiguration READ activeConfiguration NOTIFY stateChanged)
    Q_PROPERTY(QString state READ stateName NOTIFY stateChanged)
    Q_PROPERTY(QString stateMessage READ stateMessage NOTIFY stateChanged)
    Q_PROPERTY(int sampleRate READ sampleRate NOTIFY formatChanged)
    Q_PROPERTY(int bufferSize READ bufferSize NOTIFY formatChanged)
    Q_PROPERTY(QVariantMap statistics READ statistics NOTIFY statisticsChanged)
    Q_PROPERTY(QindaTK::TransportModel *transport READ transport CONSTANT)
    Q_PROPERTY(QString recordDirectory READ recordDirectory WRITE setRecordDirectory NOTIFY recordDirectoryChanged)
    Q_PROPERTY(int auditionVoices READ auditionVoices NOTIFY midiStateChanged)
    Q_PROPERTY(int launchedClips READ launchedClips NOTIFY midiStateChanged)
public:
    // Launcher commands waiting for their answer from the device thread.
    static constexpr int kMidiRequestLimit = 32;
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
    QVariantList backends() const;
    AudioMeterModel *outputMeter() const { return m_outputMeter; }
    AudioLoudnessModel *outputLoudness() const { return m_outputLoudness; }
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
    // Post-fader peak/RMS of one mixer channel ("track:<uuid>", "bus:<uuid>",
    // "master"), created on first request and owned by the engine. It has no
    // channels while the channel is not in the playing graph or no stream runs.
    Q_INVOKABLE QindaTK::AudioMeterModel *channelMeter(const QString &channelId);

    // ---- MIDI (WP-064, docs/audio-midi.md; audio_engine_midi.cpp) ----
    // Audition notes of the built-in test instrument on the first two
    // outputs, while a device runs. A transport stop, a device stop or loss
    // and allNotesOff() release every note (no note is ever left sounding).
    Q_INVOKABLE bool auditionNoteOn(int pitch, double velocity = 0.8);
    Q_INVOKABLE bool auditionNoteOff(int pitch);
    Q_INVOKABLE bool allNotesOff();
    int auditionVoices() const;
    int launchedClips() const;
    // Launcher commands (MidiLauncherModel): lane = Midi::key(track id), slot
    // = Midi::key(slot id), lane 0 stops every lane. Returns the command
    // serial the answer carries (midiLaunchEvent), or 0 with `error` set.
    quint64 postLaunch(quint64 lane, quint64 slot, MidiQuantize quantize, QString *error);
    quint64 postLaunchStop(quint64 lane, MidiQuantize quantize, QString *error);

signals:
    void devicesChanged();
    void backendsChanged();
    void stateChanged();
    void formatChanged();
    void statisticsChanged();
    void recordDirectoryChanged();
    void configurationApplied(const QString &requestId, const QString &state,
                              const QVariantMap &activeConfiguration, const QString &message);
    // {path, startSample, recordedSample, frames, sampleRate, channels,
    //  droppedFrames, latencyFrames, deviceLost, error}
    void recordingFinished(const QVariantMap &take);
    void midiStateChanged();
    // kind: MidiEngineEvent::Kind, code: MidiEngineEvent::Code.
    void midiLaunchEvent(int kind, quint64 serial, quint64 lane, quint64 slot, qint64 sample, int code);
    // The stream ended (stop, loss, reconfiguration): nothing plays any more
    // and every launcher request still waiting has no answer coming.
    void midiReset(const QString &reason);

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
    void drainMeters(Run &run);
    void drainChannelMeters(Run &run);
    void drainMidi(Run &run);
    quint64 postMidiCommand(int kind, quint64 lane, quint64 slot, MidiQuantize quantize, QString *error);
    void clearChannelMeters();
    void registerMeterMap(const AudioRenderGraph &graph);
    void switchMeterGeneration(quint64 generation);
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
    AudioMeterModel *m_outputMeter = nullptr;
    AudioLoudnessModel *m_outputLoudness = nullptr;
    quint64 m_meterDropped = 0; // blocks the current run's tap reported dropped
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

    // Channel meters (D-113): slot layouts of offered graphs by generation.
    struct MeterMap { quint64 generation = 0; QStringList channels; QVector<int> lanes; };
    QHash<QString, AudioMeterModel *> m_channelMeters;
    QList<MeterMap> m_meterMaps;
    quint64 m_meterGeneration = 0;
    quint64 m_channelMetersDropped = 0;

    struct Idle { qint64 position = 0; bool loopEnabled = false; qint64 loopStart = 0; qint64 loopEnd = 0; };
    Idle m_idle;
    struct Pending { QString id; TransportCommand command; ReplyCallback callback; };
    QHash<quint64, Pending> m_pending;
    quint64 m_commandSerial = 0;
    QHash<QString, std::shared_ptr<Take>> m_takes;
    SnapshotCallback m_sink;
    TransportSnapshot m_last;
    quint64 m_revision = 0;
    // MIDI launcher requests in flight (serials) and the last published counts.
    QSet<quint64> m_midiRequests;
    quint64 m_midiSerial = 0;
    int m_lastAuditionVoices = 0;
    int m_lastLaunchedClips = 0;
};

} // namespace QindaTK
