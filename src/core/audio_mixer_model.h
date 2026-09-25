// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "audio_engine.h"
#include "audio_meter_model.h"
#include "audio_mixer_strip_model.h"
#include "audio_mixer_types.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

class AudioParameterBinding;
class AudioSession;
class AutomationModel;
class TimelineModel;

// AGENT-CONTRACT (WP-062, docs/audio-mixer.md, D-110): the mixer of one
// AudioSession (session.mixer). It owns buses, per-channel routing (output,
// layout, sends, solo safe), mute/solo groups and the pan law, validates
// every change against the routing constraints (D-112) and records it as one
// step of its own history. Track mute, solo, arm and name stay on the
// TimelineTrack (one source with TrackHeader); gain, pan, width and send
// levels are AutomationModel parameters, so AudioParameterBinding, automation
// and their undo apply unchanged. Views (MixerView, ChannelStrip,
// RoutingMatrix) request operations by stable channel ID and never keep
// routing or engine state; the session recompiles its AudioRenderGraph on
// contentChanged() and the engine adopts it at the next period boundary.
class AudioMixerModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("AudioMixerModel belongs to an AudioSession (session.mixer)")
    Q_MOC_INCLUDE("audio_parameter_binding.h")
    Q_PROPERTY(QString revision READ revisionText NOTIFY changed)
    Q_PROPERTY(QStringList channelIds READ channelIds NOTIFY changed)
    Q_PROPERTY(QVariantList buses READ buses NOTIFY changed)
    Q_PROPERTY(QVariantList groups READ groups NOTIFY changed)
    Q_PROPERTY(QindaTK::AudioMixerStripModel *strips READ strips CONSTANT)
    Q_PROPERTY(QindaTK::AudioMixerStripModel *routingInputs READ routingInputs CONSTANT)
    Q_PROPERTY(QString panLaw READ panLaw NOTIFY changed)
    Q_PROPERTY(QStringList panLaws READ panLaws CONSTANT)
    Q_PROPERTY(QStringList layouts READ layouts CONSTANT)
    Q_PROPERTY(QStringList trackLayouts READ trackLayouts CONSTANT)
    Q_PROPERTY(QVariantList routingSources READ routingSources NOTIFY changed)
    Q_PROPERTY(QVariantList routingDestinations READ routingDestinations NOTIFY changed)
    Q_PROPERTY(QVariantList connections READ connections NOTIFY changed)
    Q_PROPERTY(QStringList heldChannels READ heldChannels NOTIFY heldChanged)
    Q_PROPERTY(QStringList hiddenChannels READ hiddenChannels NOTIFY changed)
    Q_PROPERTY(QindaTK::AudioEngine *engine READ engine NOTIFY engineChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY historyChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY historyChanged)
    Q_PROPERTY(int historyCount READ historyCount NOTIFY historyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
public:
    static constexpr int kHistoryLimit = 256;

    AudioMixerModel(AudioSession *session, TimelineModel *timeline, AutomationModel *automation);
    ~AudioMixerModel() override;

    const AudioMixerContent &content() const { return m_content; }
    AudioEngine *engine() const { return m_engine; }
    void setEngine(AudioEngine *engine);
    QString revisionText() const { return QString::number(m_revision); }
    // Audio tracks in timeline order, then buses, then "master".
    QStringList channelIds() const { return m_channels; }
    QVariantList buses() const { return m_buses; }
    QVariantList groups() const { return m_groups; }
    AudioMixerStripModel *strips() const { return m_strips; }
    // RoutingMatrix rows {portId, label, layout, available}, one per channel
    // in channelIds order. Like `strips`, rows move and update in place (never
    // reset), so the matrix keeps its current row and a focused editor by ID.
    AudioMixerStripModel *routingInputs() const { return m_routing; }
    QString panLaw() const { return AudioMixer::panLawName(m_content.panLaw); }
    QStringList panLaws() const { return AudioMixer::panLawNames(); }
    QStringList layouts() const { return AudioMixer::layoutNames(); } // buses
    QStringList trackLayouts() const { return AudioMixer::trackLayoutNames(); }
    QVariantList routingSources() const { return m_sources; }
    QVariantList routingDestinations() const { return m_destinations; }
    QVariantList connections() const { return m_connections; }
    QStringList heldChannels() const { return m_held; }
    QStringList hiddenChannels() const;
    bool canUndo() const { return m_cursor > 0; }
    bool canRedo() const { return m_cursor < m_history.size(); }
    QString undoText() const { return canUndo() ? m_history.at(m_cursor - 1).label : QString(); }
    QString redoText() const { return canRedo() ? m_history.at(m_cursor).label : QString(); }
    int historyCount() const { return int(m_history.size()); }
    // AudioSessionHistory: steps that undo() and redo() can reach, and a
    // count of steps ever recorded (it never goes back).
    int undoDepth() const { return m_cursor; }
    int redoDepth() const { return int(m_history.size()) - m_cursor; }
    quint64 historySerial() const { return m_serial; }
    QString lastError() const { return m_lastError; }

    // ---- reading ----
    Q_INVOKABLE QString channelName(const QString &channelId) const;
    // {channelId, channelName, channelKind, layout, output, outputLabel,
    //  outputAvailable, muted, soloed, armed, soloSafe, implicitlyMuted,
    //  effectiveMuted, groupNames, sends, held, hidden, color, outputLayout}
    Q_INVOKABLE QVariantMap channel(const QString &channelId) const;
    // Every destination of `channelId`: {id, label, layout, available, allowed, reason}.
    Q_INVOKABLE QVariantList destinations(const QString &channelId) const;
    // Output ports: "main", the running device's channels, and every port a
    // route still names (unavailable, with the reason; never replaced).
    Q_INVOKABLE QVariantList outputPorts() const;

    // ---- operations: {ok, error, ...}; each accepted change is one undo step ----
    Q_INVOKABLE QVariantMap addBus(const QString &name, const QString &layout = QStringLiteral("Stereo"));
    Q_INVOKABLE QVariantMap removeBus(const QString &busId);
    Q_INVOKABLE QVariantMap renameChannel(const QString &channelId, const QString &name);
    Q_INVOKABLE QVariantMap setLayout(const QString &channelId, const QString &layout);
    Q_INVOKABLE QVariantMap setOutput(const QString &channelId, const QString &destinationId);
    Q_INVOKABLE QVariantMap addSend(const QString &channelId, const QString &busId, bool preFader = false);
    Q_INVOKABLE QVariantMap removeSend(const QString &sendId);
    Q_INVOKABLE QVariantMap setSendPreFader(const QString &sendId, bool preFader);
    // Tracks: the TimelineTrack flag (timeline history); buses and master: the mixer.
    Q_INVOKABLE QVariantMap setMuted(const QString &channelId, bool muted);
    Q_INVOKABLE QVariantMap setSoloed(const QString &channelId, bool soloed);
    Q_INVOKABLE QVariantMap setArmed(const QString &channelId, bool armed);
    Q_INVOKABLE QVariantMap setSoloSafe(const QString &channelId, bool safe);
    // View state: saved with the session, not an undo step. A held channel
    // stays in `strips` until its gesture ends (D-113).
    Q_INVOKABLE QVariantMap setHidden(const QString &channelId, bool hidden);
    Q_INVOKABLE QVariantMap addGroup(const QString &name, const QStringList &members);
    Q_INVOKABLE QVariantMap removeGroup(const QString &groupId);
    Q_INVOKABLE QVariantMap setGroupMembers(const QString &groupId, const QStringList &members);
    Q_INVOKABLE QVariantMap setGroupMuted(const QString &groupId, bool muted);
    Q_INVOKABLE QVariantMap setGroupSoloed(const QString &groupId, bool soloed);
    Q_INVOKABLE QVariantMap setPanLaw(const QString &law);
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE void clearHistory();

    // ---- parameters and meters ----
    // "<owner>/<kind>" after adding the automation parameter when missing:
    // gain, pan, width of a channel; level of a send. "" (and lastError) otherwise.
    Q_INVOKABLE QString ensureParameter(const QString &owner, const QString &kind);
    // One shared binding per parameter, owned by the mixer, so a gesture never
    // belongs to a recycled delegate and every view (or a control surface
    // adapter) edits and follows the same parameter.
    Q_INVOKABLE QindaTK::AudioParameterBinding *parameterBinding(const QString &owner, const QString &kind);
    // The engine's post-fader meter of the channel; null without an engine.
    Q_INVOKABLE QindaTK::AudioMeterModel *meter(const QString &channelId);
    // AGENT-CONTRACT (D-113, ChannelStrip): a view that shows a channel's
    // meter watches it (counted) and unwatches it when it shows another
    // channel or goes. With more channels than meter slots, the master and
    // the watched channels are metered first; meterProblem explains a
    // channel left out ("" when it is metered).
    Q_INVOKABLE QindaTK::AudioMeterModel *watchMeter(const QString &channelId);
    Q_INVOKABLE void unwatchMeter(const QString &channelId);
    Q_INVOKABLE QString meterProblem(const QString &channelId) const;
    QSet<QString> watchedMeters() const;

    // ---- persistence (the AudioSession "mixer" part), all or nothing ----
    Q_INVOKABLE QVariantMap contentMap() const;
    Q_INVOKABLE QVariantMap loadContent(const QVariantMap &content);
    // What loadContent would refuse, without changing anything ("" if none):
    // AudioSession::open checks it before it replaces any part.
    static QString contentProblem(const QVariantMap &content);
    // The part AudioSession::save writes: contentMap without the dormant
    // settings of deleted tracks, which no reopened session can reach.
    QVariantMap savedContentMap() const;
    // False for a mixer parameter ("<owner>/<kind>") whose channel or send is
    // gone from the saved mix; the session leaves such lanes out of the file.
    bool keepsParameter(const QString &parameterId) const;

signals:
    void changed();
    void contentChanged(); // the compiled mix changed: the session recompiles
    void contentReset();
    void historyChanged();
    void heldChanged();
    void engineChanged();
    void lastErrorChanged();
    // The watched set changed while more channels exist than meter slots:
    // the session recompiles so the graph meters what is on screen.
    void meterWatchChanged();

private:
    struct Step {
        QString label;
        AudioMixerContent before;
        AudioMixerContent after;
    };
    QVariantMap commit(const QString &label, AudioMixerContent next, const QVariantMap &extra = {});
    void apply(const AudioMixerContent &next);
    QVariantMap accepted(const QVariantMap &extra = {});
    QVariantMap reject(const QString &message);
    QVariantMap applyTrackProperty(const QString &channelId, const QString &property, const QVariant &value);
    QString routeProblem(const QString &from, const QString &to) const;
    QString portProblem(const QString &portId) const;
    QString loopText(const QString &from, const QStringList &loop) const;
    QString destinationLabel(const QString &id) const;
    QString outputLayout(const QString &destinationId) const;
    QString activeDeviceId() const;
    int activeOutputChannels() const;
    const AudioMixerSend *findSend(const QString &sendId, QString *channelId = nullptr) const;
    QStringList parametersOf(const QString &channelId) const;
    bool isHeld(const QString &channelId) const;
    QVariantList stripRow(const QString &channelId, const AudioMixer::Audibility &audibility) const;
    // Derived state from the content, the timeline tracks and the engine;
    // emits changed() when anything readable differs (always when forced).
    void refresh(bool force = false);
    void refreshHeld();
    // Releases bindings and engine meters of channels and sends that are gone.
    void prune();
    bool liveTrack(const QString &channelId) const;

    AudioSession *m_session = nullptr;
    TimelineModel *m_timeline = nullptr;
    AutomationModel *m_automation = nullptr;
    QPointer<AudioEngine> m_engine;
    QList<QMetaObject::Connection> m_engineConnections;
    AudioMixerContent m_content;
    QSet<QString> m_hidden;
    QVector<Step> m_history;
    int m_cursor = 0;
    quint64 m_revision = 0;
    QStringList m_channels;
    QVariantList m_buses;
    QVariantList m_groups;
    QVariantList m_sources;
    QVariantList m_destinations;
    QVariantList m_connections;
    QStringList m_held;
    QVector<QVariantList> m_rows;
    AudioMixerStripModel *m_strips = nullptr;
    AudioMixerStripModel *m_routing = nullptr;
    QHash<QString, AudioParameterBinding *> m_bindings;
    QHash<QString, int> m_meterWatch;
    quint64 m_serial = 0;
    QString m_lastError;
};

} // namespace QindaTK
