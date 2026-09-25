// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "audio_engine.h"
#include "audio_graph.h"
#include "audio_media.h"
#include "audio_mixer_model.h"
#include "audio_session_history.h"
#include "audio_source_edit_model.h"
#include "audio_take_comp_model.h"
#include "automation_model.h"
#include "midi_launcher_model.h"
#include "midi_sequence_model.h"
#include "engine_schedule.h"
#include "resource_resolver.h"
#include "tempo_map_model.h"
#include "time_map.h"
#include "timeline_model.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <optional>
#include <utility>

namespace QindaTK {

// AGENT-CONTRACT: the reference audio session (WP-060, docs/audio-engine.md).
// It owns the accepted domain models — TimelineModel (clips, edits, undo),
// TempoMapModel/AutomationModel through EngineSchedule, and ResourceResolver
// (media identity, availability, relink) — and AudioMixerModel (buses,
// routing, sends, groups; WP-062) and AudioSourceEditModel (sample, fade
// and spectral edits of each source; WP-063), MidiSequenceModel (patterns,
// scenes and slots; WP-064) with its MidiLauncherModel, AudioTakeCompModel
// (take comps; WP-065, docs/audio-comping.md), and compiles them into an
// AudioRenderGraph for its engine and for offline renders. It persists as
// "qindatk.audio-session/1" JSON with media paths relative to the session.
class AudioSession : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::TimelineModel *timeline READ timeline CONSTANT)
    Q_PROPERTY(QindaTK::TimeMap *timeMap READ timeMap CONSTANT)
    Q_PROPERTY(QindaTK::TempoMapModel *tempo READ tempo CONSTANT)
    Q_PROPERTY(QindaTK::AutomationModel *automation READ automation CONSTANT)
    Q_PROPERTY(QindaTK::EngineSchedule *schedule READ schedule CONSTANT)
    Q_PROPERTY(QindaTK::ResourceResolver *resources READ resources CONSTANT)
    Q_PROPERTY(QindaTK::AudioMixerModel *mixer READ mixer CONSTANT)
    // One Undo/Redo over the timeline, automation and mixer histories (D-110).
    Q_PROPERTY(QindaTK::AudioSessionHistory *history READ history CONSTANT)
    Q_PROPERTY(QindaTK::AudioSourceEditModel *edits READ edits CONSTANT)
    Q_PROPERTY(QindaTK::MidiSequenceModel *midi READ midi CONSTANT)
    Q_PROPERTY(QindaTK::MidiLauncherModel *launcher READ launcher CONSTANT)
    Q_PROPERTY(QindaTK::AudioTakeCompModel *comps READ comps CONSTANT)
    // A recording that wrapped a loop becomes one take per pass in a comp
    // (default); false keeps the whole recording as one linear clip.
    Q_PROPERTY(bool passTakes READ passTakes WRITE setPassTakes NOTIFY passTakesChanged)
    Q_PROPERTY(QindaTK::AudioEngine *engine READ engine WRITE setEngine NOTIFY engineChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY filePathChanged)
    Q_PROPERTY(QString mediaDirectory READ mediaDirectory WRITE setMediaDirectory NOTIFY mediaDirectoryChanged)
    Q_PROPERTY(QVariantList media READ media NOTIFY mediaChanged)
    Q_PROPERTY(QStringList missingMedia READ missingMedia NOTIFY mediaChanged)
    Q_PROPERTY(int sampleRate READ sampleRate NOTIFY sampleRateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
public:
    explicit AudioSession(QObject *parent = nullptr);
    ~AudioSession() override;
    static QString schemaId() { return QStringLiteral("qindatk.audio-session/1"); }

    TimelineModel *timeline() const { return m_timeline; }
    TimeMap *timeMap() const { return m_timeMap; }
    TempoMapModel *tempo() const { return m_tempo; }
    AutomationModel *automation() const { return m_automation; }
    EngineSchedule *schedule() const { return m_schedule; }
    ResourceResolver *resources() const { return m_resources; }
    AudioMixerModel *mixer() const { return m_mixer; }
    AudioSessionHistory *history() const { return m_history; }
    AudioSourceEditModel *edits() const { return m_edits; }
    MidiSequenceModel *midi() const { return m_midi; }
    MidiLauncherModel *launcher() const { return m_launcher; }
    AudioTakeCompModel *comps() const { return m_comps; }
    bool passTakes() const { return m_passTakes; }
    void setPassTakes(bool enabled);
    AudioEngine *engine() const { return m_engine; }
    void setEngine(AudioEngine *engine);
    QString filePath() const { return m_filePath; }
    QString mediaDirectory() const { return m_mediaDirectory; }
    void setMediaDirectory(const QString &directory);
    QVariantList media() const;
    QStringList missingMedia() const;
    int sampleRate() const;
    QString lastError() const { return m_lastError; }

    // Graph at `sampleRate` from the committed timeline, media, schedule and
    // mix. `offline`: the master feeds "main" (the file), device ports are silent.
    AudioRenderGraphPtr compileGraph(int sampleRate, bool offline = false);
    // The edited samples (edits.buffer) at `sampleRate`, resampled once per
    // source revision.
    AudioBufferPtr mediaBuffer(const QString &resourceId, int sampleRate);
    // The engine clock in timeline ticks: the device thread's live transport
    // sample when an engine is attached, else the automation transport.
    qint64 transportTicks() const;

    Q_INVOKABLE QString addTrack(const QString &name);
    Q_INVOKABLE bool setTrackArmed(const QString &trackId, bool armed);
    // Adds "track:<id>/gain" (dB, -60..+12, default 0) when missing; returns its ID.
    Q_INVOKABLE QString ensureGainParameter(const QString &trackId);
    // Decodes a WAV file, registers it and inserts one undoable clip.
    Q_INVOKABLE QVariantMap importMedia(const QString &file, const QString &trackId,
                                        const QString &startTicks = QStringLiteral("0"));
    Q_INVOKABLE bool save(const QString &file);
    Q_INVOKABLE bool open(const QString &file);
    // Missing media stays referenced by ID; relink points the ID at a new file.
    Q_INVOKABLE bool relinkMedia(const QString &resourceId, const QString &file);
    // Offline render of the main output (the master mix) through the same
    // graph code the device plays; float32 WAV, 1 or 2 channels (a mono file
    // averages the stereo master). An empty endTicks renders to the end of
    // the last clip.
    // ---- MIDI (WP-064, docs/audio-midi.md; audio_session_midi.cpp) ----
    // A MIDI track plays its pattern clips through the built-in test
    // instrument and is a mixer channel like an audio track.
    Q_INVOKABLE QString addMidiTrack(const QString &name);
    // One undoable beat-locked clip of `patternId` at `startTicks` (timeline
    // ticks); its length is `lengthTicks` musical ticks (empty: the pattern's).
    Q_INVOKABLE QVariantMap insertPattern(const QString &trackId, const QString &patternId,
                                          const QString &startTicks = QStringLiteral("0"),
                                          const QString &lengthTicks = {});
    // Standard MIDI File of one pattern with the session tempo map.
    Q_INVOKABLE QVariantMap exportMidi(const QString &patternId, const QString &file);
    // Patterns from a MIDI file (one per track); tempo and meter are reported.
    Q_INVOKABLE QVariantMap importMidi(const QString &file);
    // ---- Take comps (WP-065, docs/audio-comping.md; audio_session_comp.cpp) ----
    // Places a finished recording (AudioEngine::recordingFinished's map; the
    // engine's takes arrive here). Loop passes become takes of a comp; with
    // punch the new takes are selected only inside the punch range, over the
    // comp or clip already there (D-177). {ok, error, clipId, resourceId, compId}.
    QVariantMap placeTake(const QVariantMap &take);
    // The comp a timeline clip plays, or "" for a media clip.
    Q_INVOKABLE QString compForClip(const QString &clipId) const;
    Q_INVOKABLE QVariantMap renderToFile(const QString &file, const QString &startTicks = QStringLiteral("0"),
                                         const QString &endTicks = {}, int sampleRate = 0, int channels = 2);

signals:
    void engineChanged();
    void filePathChanged();
    void mediaDirectoryChanged();
    void mediaChanged();
    void sampleRateChanged();
    void passTakesChanged();
    void lastErrorChanged();
    void graphCompiled(int clips, const QStringList &missing);
    void takeRecorded(const QString &clipId, const QString &resourceId, const QVariantMap &take);

private:
    void rebuild();
    void engineFormatChanged();
    void forwardTransport();
    void updateRecordTarget();
    struct TakePass { qint64 fileStart = 0; qint64 fileEnd = 0; qint64 heard = 0; };
    QVariantMap placeTakeInComp(const QString &trackId, const QString &resourceId, int rate,
                                const QVector<TakePass> &passes, std::optional<std::pair<qint64, qint64>> punch);
    QString compClipOver(const QString &trackId, int rate, qint64 from, qint64 to, bool media) const;
    bool decode(const QString &resourceId);
    void sourceEdited(const QString &resourceId);
    QString insertClip(TimelineClip clip, const QString &trackId, qint64 at, QString *error);
    QString armedTrack() const;
    bool fail(const QString &message);
    void clearError();

    TimeMap *m_timeMap = nullptr;
    TimelineModel *m_timeline = nullptr;
    TempoMapModel *m_tempo = nullptr;
    AutomationModel *m_automation = nullptr;
    EngineSchedule *m_schedule = nullptr;
    ResourceResolver *m_resources = nullptr;
    AudioMixerModel *m_mixer = nullptr;
    AudioSessionHistory *m_history = nullptr;
    AudioSourceEditModel *m_edits = nullptr;
    MidiSequenceModel *m_midi = nullptr;
    MidiLauncherModel *m_launcher = nullptr;
    bool m_meterRebuildQueued = false;
    AudioTakeCompModel *m_comps = nullptr;
    bool m_passTakes = true;
    QPointer<AudioEngine> m_engine;
    QList<QMetaObject::Connection> m_engineConnections;
    QHash<QString, AudioBufferPtr> m_decoded;
    QHash<QString, AudioBufferPtr> m_resampled; // "<rate>/<resource id>"
    QString m_filePath;
    QString m_mediaDirectory;
    QString m_lastError;
    qint64 m_forwardedTicks = 0;
    bool m_opening = false; // open() recompiles once at the end
};

} // namespace QindaTK
