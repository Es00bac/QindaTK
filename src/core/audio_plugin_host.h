// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "audio_plugin_instance.h"
#include "audio_plugin_processor.h"
#include "audio_plugin_registry.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <memory>

class QTimer;
class QWindow;

namespace QindaTK {

class AudioMixerModel;
class AudioParameterBinding;
class AudioPluginHost;
class AudioSession;
class AutomationModel;

// An offline render's hold on every live instance: while it exists the
// device thread passes dry audio through them, and they run at the render's
// sample rate. Destroying it gives them back to the device (D-192).
class AudioPluginOfflineClaim {
public:
    ~AudioPluginOfflineClaim();

private:
    friend class AudioPluginHost;
    struct Held {
        QPointer<AudioPluginInstance> instance;
        AudioPluginProcessorPtr processor;
        int restoreRate = 0; // reactivated for the render: the rate to return to
    };
    QList<Held> m_held;
};

// AGENT-CONTRACT (WP-066, D-190..D-193, docs/audio-plugins.md): the plugin
// inserts of one AudioSession (session.plugins). Each mixer channel
// (track:<uuid>, bus:<uuid>, master) has an ordered chain of instances
// ("plugin:<uuid>"), processed pre-fader in chain order by the session's
// AudioRenderGraph, with the plugins' reported latency compensated across
// the mix. Every instance runs in its own qindatk-plugin-host process.
// Parameters that a host may write are AutomationModel parameters
// "<instance>/<CLAP id>" (D-191): knobs, automation lanes, undo and the
// plugin's own editor all go through them. The session saves the chains as
// its "plugins" part; a plugin that is not installed, or that fails, keeps
// its identity, bypass flag, parameter IDs and saved state (PLT-11), plays
// dry, and can be reloaded later. PluginBrowser and PluginRack are views:
// browserPlugins, instances(channel) and rackAction() are their host side.
class AudioPluginHost : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("AudioPluginHost belongs to an AudioSession (session.plugins)")
    Q_MOC_INCLUDE("audio_parameter_binding.h")
    Q_PROPERTY(QindaTK::AudioPluginRegistry *registry READ registry CONSTANT)
    Q_PROPERTY(QVariantList formats READ formats CONSTANT)
    Q_PROPERTY(QVariantList browserPlugins READ browserPlugins NOTIFY browserChanged)
    Q_PROPERTY(QStringList instanceIds READ instanceIds NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(int requestTimeoutMs READ requestTimeoutMs WRITE setRequestTimeoutMs NOTIFY timeoutsChanged)
    Q_PROPERTY(int realtimeTimeoutMs READ realtimeTimeoutMs WRITE setRealtimeTimeoutMs NOTIFY timeoutsChanged)
    Q_PROPERTY(int offlineTimeoutMs READ offlineTimeoutMs WRITE setOfflineTimeoutMs NOTIFY timeoutsChanged)
    Q_PROPERTY(int hangTimeoutMs READ hangTimeoutMs WRITE setHangTimeoutMs NOTIFY timeoutsChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
public:
    AudioPluginHost(AudioSession *session, AutomationModel *automation, AudioMixerModel *mixer);
    ~AudioPluginHost() override;
    static QString schemaId() { return QStringLiteral("qindatk.audio-plugins/1"); }

    AudioPluginRegistry *registry() const { return m_registry; }
    QVariantList formats() const { return AudioPlugins::formats(); }
    QVariantList browserPlugins() const;
    QStringList instanceIds() const;
    int revision() const { return m_revision; }
    int requestTimeoutMs() const { return m_timeouts.requestMs; }
    void setRequestTimeoutMs(int milliseconds);
    int realtimeTimeoutMs() const { return m_timeouts.realtimeMs; }
    void setRealtimeTimeoutMs(int milliseconds);
    int offlineTimeoutMs() const { return m_timeouts.offlineMs; }
    void setOfflineTimeoutMs(int milliseconds);
    int hangTimeoutMs() const { return m_timeouts.hangMs; }
    void setHangTimeoutMs(int milliseconds);
    QString lastError() const { return m_lastError; }
    AudioPluginInstance *instanceObject(const QString &instanceId) const;

    // Operations answer {ok, error, ...}; a refused one changes nothing.
    Q_INVOKABLE QVariantMap addPlugin(const QString &channelId, const QString &pluginId, int index = -1);
    Q_INVOKABLE QVariantMap removePlugin(const QString &instanceId);
    // `channelId` empty: within the instance's chain.
    Q_INVOKABLE QVariantMap movePlugin(const QString &instanceId, int index, const QString &channelId = {});
    Q_INVOKABLE QVariantMap setBypassed(const QString &instanceId, bool bypassed);
    // A failed or missing instance starts again with its saved state.
    Q_INVOKABLE QVariantMap reload(const QString &instanceId);
    Q_INVOKABLE QStringList chain(const QString &channelId) const;
    Q_INVOKABLE QVariantList instances(const QString &channelId);
    Q_INVOKABLE QVariantMap instance(const QString &instanceId) const;
    Q_INVOKABLE QString parameterId(const QString &instanceId, int clapParameterId) const;
    Q_INVOKABLE QindaTK::AudioParameterBinding *parameterBinding(const QString &instanceId, int clapParameterId);
    Q_INVOKABLE QVariantMap openEditor(const QString &instanceId, QWindow *owner = nullptr);
    Q_INVOKABLE QVariantMap closeEditor(const QString &instanceId);
    // PluginRack/PluginBrowser requests ("add", "reorder", "bypass", "remove",
    // "open-editor", "close-editor", "edit-parameter", "reload"): answers
    // {state: accepted|rejected|error, message} for acknowledgeAction().
    Q_INVOKABLE QVariantMap rackAction(const QString &action, const QString &instanceId, const QVariantMap &payload,
                                       QWindow *owner = nullptr);
    // Every live instance's state, as the next save would store it.
    Q_INVOKABLE bool refreshStates();
    // Shutdown: editors close, every process is asked to quit and is killed
    // after one second; processors pass dry audio from then on.
    Q_INVOKABLE void unloadAll();

    // Session part ("plugins"). saveContent() asks live plugins for their state.
    QVariantMap saveContent();
    static bool validateContent(const QVariantMap &content, QString *error);
    void loadContent(const QVariantMap &content);

    // Graph compilation (GUI thread) and offline renders (D-192).
    AudioPluginInsertsPtr inserts() const;
    std::unique_ptr<AudioPluginOfflineClaim> beginOffline(int sampleRate);

signals:
    void changed();
    void contentChanged();   // the session recompiles its graph
    void browserChanged();
    void timeoutsChanged();
    void lastErrorChanged();
    void instanceFailed(const QString &instanceId, const QString &message);
    void editorChanged(const QString &instanceId, bool open);

private:
    struct Slot {
        AudioPluginInstance *instance = nullptr;
        QString channelId;
        bool bypassed = false;
    };
    AudioPluginInstance *createInstance(const QString &instanceId);
    void ensureParameters(AudioPluginInstance *instance);
    void instanceEdited(AudioPluginInstance *instance, quint32 id, double value);
    void instanceGesture(AudioPluginInstance *instance, quint32 id, bool begin);
    void restartInstance(AudioPluginInstance *instance);
    void sampleRateChanged();
    void pushParameters();
    void pollHung();
    void bump(bool graph);
    int sampleRate() const;
    bool channelExists(const QString &channelId) const;
    QVariantMap fail(const QString &message);
    QVariantMap row(const QString &instanceId);
    QVariantMap describeSlot(const QString &instanceId, const Slot &slot) const;
    void clearAll();

    QPointer<AudioSession> m_session;
    AutomationModel *m_automation = nullptr;
    AudioMixerModel *m_mixer = nullptr;
    AudioPluginRegistry *m_registry = nullptr;
    AudioPluginInstance::Timeouts m_timeouts;
    QHash<QString, Slot> m_slots;          // by instance ID
    QHash<QString, QStringList> m_chains;  // channel ID -> instance IDs in slot order
    QStringList m_channels;                // channels with a chain, first-use order
    QHash<QString, AudioParameterBinding *> m_bindings;
    QHash<QString, double> m_pushed;       // automation ID -> value the plugin last got outside processing
    QHash<QString, double> m_gestures;     // automation ID -> value of an editor gesture in progress
    QTimer *m_poll = nullptr;
    QString m_lastError;
    int m_revision = 0;
};

} // namespace QindaTK
