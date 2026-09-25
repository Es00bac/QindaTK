// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "audio_plugin_processor.h"
#include "audio_plugin_types.h"

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSize>
#include <optional>

class QProcess;
class QWindow;

namespace QindaTK {

class AudioPluginEditorWindow;

// AGENT-CONTRACT (WP-066, D-190/D-191, docs/audio-plugins.md): one plugin
// instance of a session and the qindatk-plugin-host process that runs it.
// GUI thread only. Control requests are JSON lines answered within a time
// limit (plugin_host_main.cpp); a request that is not answered in time, a
// crash, or a hang reported by the processor fails the instance: its process
// is killed, its processor passes dry audio, and its identity, bypass flag,
// parameter IDs and last saved state stay for a later reload (PLT-11).
// Parameter values belong to the session's automation (D-191); the instance
// only reports what the plugin's own editor changes.
class AudioPluginInstance : public QObject {
    Q_OBJECT
public:
    enum class State { Missing, Loading, Ready, Failed, Unloaded };
    struct Parameter {
        quint32 id = 0;
        QString name;
        QString module;
        double minimum = 0.0;
        double maximum = 1.0;
        double defaultValue = 0.0;
        double value = 0.0;
        QString text;
        bool stepped = false;
        bool mapped = false; // automatable, visible, writable: an automation parameter
        int index = -1;      // index in the processor's parameter list when mapped
    };
    struct Timeouts {
        int requestMs = 5000;
        int realtimeMs = 50;
        int offlineMs = 5000;
        int hangMs = 2000;
    };

    AudioPluginInstance(const QString &instanceId, QObject *parent = nullptr);
    ~AudioPluginInstance() override;

    QString instanceId() const { return m_instanceId; }
    QString pluginId() const { return m_pluginId; }
    void setPluginId(const QString &pluginId) { m_pluginId = pluginId; }
    const AudioPluginDescriptor &descriptor() const { return m_descriptor; }
    void setDescriptor(const AudioPluginDescriptor &descriptor) { m_descriptor = descriptor; }
    State state() const { return m_state; }
    QString stateName() const;
    QString error() const { return m_error; }
    bool live() const { return m_state == State::Ready; }
    int latency() const { return m_latency; }
    int activeSampleRate() const { return m_activeRate; }
    qint64 processId() const { return m_processId; }
    QByteArray savedState() const { return m_savedState; }
    void setSavedState(const QByteArray &state) { m_savedState = state; }
    const QList<Parameter> &parameters() const { return m_parameters; }
    void setParameters(const QList<Parameter> &parameters) { m_parameters = parameters; }
    const Parameter *parameter(quint32 id) const;
    AudioPluginProcessorPtr processor() const { return m_processor; }
    void setTimeouts(const Timeouts &timeouts) { m_timeouts = timeouts; }
    void markMissing(const QString &reason);

    // Starts the host process, loads the plugin, restores the saved state and
    // activates it at `sampleRate`. False (with error()) leaves it Failed.
    bool start(const AudioPluginDescriptor &descriptor, int sampleRate);
    // Deactivates and activates again (a new rate, or the plugin asked for a
    // restart); the processor is handed to `releaseTo` afterwards.
    bool activate(int sampleRate, int releaseTo = AudioPluginProcessor::Device);
    // Asks the plugin for its state (savedState() on success).
    bool refreshState();
    // Values outside processing (params flush in the plugin process).
    void sendParameters(const QList<QPair<quint32, double>> &values);
    // Shutdown: beginUnload() asks every process to quit; finishUnload()
    // waits for this one until `deadlineMs` and kills it otherwise.
    void beginUnload();
    void finishUnload(int deadlineMs);
    void fail(const QString &message);

    // Editor (D-193). `owner` becomes the transient parent of the editor window.
    bool editorAvailable() const;
    bool editorOpen() const { return m_editorOpen; }
    bool editorFloating() const { return m_editorFloating; }
    AudioPluginEditorWindow *editorWindow() const;
    bool openEditor(QWindow *owner, QString *error);
    void closeEditor();

signals:
    void changed();
    void failed(const QString &message);
    void restartRequested();
    void parameterEdited(quint32 id, double value);
    void gestureEdited(quint32 id, bool begin);
    void stateDirty();
    void editorChanged();

private:
    std::optional<QJsonObject> request(QJsonObject message, int timeoutMs, QString *error);
    void readLines();
    void scheduleEvents();
    void dispatchEvents();
    void handleEvent(const QJsonObject &event);
    void editorEvent(const QJsonObject &event);
    void processFinished();
    void applyParameters(const QJsonArray &list);
    void dropEditorWindow();

    QString m_instanceId;
    QString m_pluginId;
    AudioPluginDescriptor m_descriptor;
    State m_state = State::Missing;
    QString m_error;
    QByteArray m_savedState;
    QList<Parameter> m_parameters;
    int m_latency = 0;
    int m_inputs = 0;
    int m_outputs = 0;
    int m_activeRate = 0;
    qint64 m_processId = 0;
    bool m_hasEditor = false;
    Timeouts m_timeouts;
    AudioPluginProcessorPtr m_processor;
    QProcess *m_process = nullptr;
    QByteArray m_buffer;
    int m_nextRequest = 0;
    QHash<int, QJsonObject> m_replies;
    QList<QJsonObject> m_events;
    bool m_eventsScheduled = false;
    bool m_unloading = false;
    bool m_timedOut = false; // the last request failed for lack of an answer
    QPointer<AudioPluginEditorWindow> m_editor;
    bool m_editorOpen = false;
    bool m_editorFloating = false;
    bool m_editorResizable = false;
};

} // namespace QindaTK
