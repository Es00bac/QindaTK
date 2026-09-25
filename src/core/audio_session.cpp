// SPDX-License-Identifier: LGPL-3.0-or-later
#include "audio_session.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUrl>
#include <QUuid>

namespace QindaTK {
namespace {

constexpr qint64 kMaximumSessionBytes = qint64(64) * 1024 * 1024;

QString localPath(const QString &file)
{
    const QUrl url(file);
    return url.isLocalFile() ? url.toLocalFile() : file;
}

bool isAudio(const QString &kind)
{
    return kind.isEmpty() || kind == QLatin1String("audio");
}

QVariantMap failure(const QString &message)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QString resultError(const QVariantMap &result)
{
    const QVariant error = result.value(QStringLiteral("error"));
    return error.typeId() == QMetaType::QVariantMap ? error.toMap().value(QStringLiteral("message")).toString()
                                                    : error.toString();
}

} // namespace

AudioSession::AudioSession(QObject *parent)
    : QObject(parent),
      m_timeMap(new TimeMap(this)),
      m_timeline(new TimelineModel(this)),
      m_tempo(new TempoMapModel(this)),
      m_automation(new AutomationModel(this)),
      m_schedule(new EngineSchedule(this)),
      m_resources(new ResourceResolver(this)),
      m_mixer(new AudioMixerModel(this, m_timeline, m_automation)),
      m_edits(new AudioSourceEditModel(this)),
      m_midi(new MidiSequenceModel(this)),
      m_launcher(new MidiLauncherModel(m_midi, this)),
      m_comps(new AudioTakeCompModel(this)),
      m_plugins(new AudioPluginHost(this, m_automation, m_mixer))
{
    m_comps->setSourceLookup([this](const QString &id) { return m_decoded.contains(id) ? m_edits->buffer(id) : nullptr; });
    connect(m_comps, &AudioTakeCompModel::changed, this, &AudioSession::rebuild);
    connect(this, &AudioSession::mediaChanged, m_comps, &AudioTakeCompModel::sourcesChanged);
    m_midi->setTicksPerQuarter(m_timeMap->ticksPerQuarter());
    connect(m_timeMap, &TimeMap::changed, m_midi, [this] { m_midi->setTicksPerQuarter(m_timeMap->ticksPerQuarter()); });
    connect(m_midi, &MidiSequenceModel::contentChanged, this, &AudioSession::rebuild);
    m_timeline->setTimeMap(m_timeMap);
    m_tempo->setTimeMap(m_timeMap);
    m_tempo->setTimeline(m_timeline);
    m_schedule->setTempoModel(m_tempo);
    m_schedule->setAutomationModel(m_automation);
    const auto changed = [this] {
        updateRecordTarget();
        rebuild();
    };
    connect(m_timeline, &TimelineModel::revisionChanged, this, changed);
    connect(m_timeline, &TimelineModel::contentReset, this, changed);
    connect(m_schedule, &EngineSchedule::published, this, &AudioSession::rebuild);
    connect(m_mixer, &AudioMixerModel::contentChanged, this, &AudioSession::rebuild);
    connect(m_plugins, &AudioPluginHost::contentChanged, this, &AudioSession::rebuild);
    connect(m_edits, &AudioSourceEditModel::sourceChanged, this,
            [this](const QString &resourceId, const QString &) { sourceEdited(resourceId); });
}

AudioSession::~AudioSession()
{
    // AGENT-GUARD: the mixer's shared bindings release open gestures through
    // the automation model, so the mixer goes before the models it uses.
    // Plugins go first: their processes quit and their bindings let go (WP-066).
    disconnect(m_plugins, nullptr, this, nullptr);
    delete std::exchange(m_plugins, nullptr);
    delete m_mixer;
    m_mixer = nullptr;
    for (const auto &connection : std::as_const(m_engineConnections)) disconnect(connection);
}

void AudioSession::setEngine(AudioEngine *engine)
{
    if (m_engine == engine) return;
    for (const auto &connection : std::as_const(m_engineConnections)) disconnect(connection);
    m_engineConnections.clear();
    if (m_engine) {
        // AGENT-GUARD (WP-064): nothing this session started keeps sounding
        // on an engine it leaves: audition notes and launched clips stop.
        m_engine->allNotesOff();
        m_engine->postLaunchStop(0, MidiQuantize::None, nullptr);
        m_engine->setGraph({});
    }
    m_launcher->setEngine(engine);
    // A detached engine no longer runs this session's automation clock.
    if (m_automation->transportRunning()) m_automation->transportStopped(m_forwardedTicks);
    m_engine = engine;
    m_mixer->setEngine(engine);
    if (engine) {
        m_engineConnections << connect(engine, &AudioEngine::formatChanged, this, &AudioSession::engineFormatChanged)
                            << connect(engine, &AudioEngine::recordingFinished, this, &AudioSession::placeTake)
                            << connect(engine->transport(), &TransportModel::changed, this, &AudioSession::forwardTransport);
        engine->transport()->setTimeMap(m_timeMap);
        if (!m_mediaDirectory.isEmpty()) engine->setRecordDirectory(m_mediaDirectory);
    }
    emit engineChanged();
    updateRecordTarget();
    engineFormatChanged();
    forwardTransport();
}

void AudioSession::setMediaDirectory(const QString &directory)
{
    if (directory == m_mediaDirectory) return;
    m_mediaDirectory = directory;
    if (m_engine && !directory.isEmpty()) m_engine->setRecordDirectory(directory);
    emit mediaDirectoryChanged();
}

int AudioSession::sampleRate() const
{
    return m_engine && m_engine->sampleRate() > 0 ? m_engine->sampleRate() : m_timeMap->sampleRate();
}

void AudioSession::engineFormatChanged()
{
    // Session clock: the TimeMap (timecode, sample readouts, schedule
    // snapshots) follows the rate the device actually runs at.
    if (m_engine && m_engine->sampleRate() > 0 && m_timeMap->sampleRate() != m_engine->sampleRate())
        m_timeMap->setSampleRate(m_engine->sampleRate());
    emit sampleRateChanged();
    rebuild();
}

void AudioSession::rebuild()
{
    if (!m_mixer) return; // destruction
    const AudioRenderGraphPtr graph = compileGraph(sampleRate());
    if (m_engine) m_engine->setGraph(graph);
    emit graphCompiled(graph->clipCount(), graph->missingResources());
}

QVariantList AudioSession::media() const
{
    QVariantList list;
    for (const QVariant &value : m_resources->resources()) {
        QVariantMap entry = value.toMap();
        const QString id = entry.value(QStringLiteral("resourceId")).toString();
        // Frames, rate and channels of the samples that play: the edited source.
        const AudioBufferPtr buffer = m_decoded.contains(id) ? m_edits->buffer(id) : nullptr;
        entry.insert(QStringLiteral("decoded"), bool(buffer));
        if (buffer) {
            entry.insert(QStringLiteral("sampleRate"), buffer->sampleRate);
            entry.insert(QStringLiteral("channels"), buffer->channels);
            entry.insert(QStringLiteral("frames"), QString::number(buffer->frames));
        }
        list.push_back(entry);
    }
    return list;
}

bool AudioSession::decode(const QString &resourceId)
{
    for (auto it = m_resampled.begin(); it != m_resampled.end();)
        it = it.key().endsWith(QLatin1Char('/') + resourceId) ? m_resampled.erase(it) : std::next(it);
    const QString uri = m_resources->lookup(resourceId).value(QStringLiteral("uri")).toString();
    const AudioBufferPtr buffer = uri.isEmpty() ? nullptr : AudioFile::readWav(localPath(uri));
    if (buffer) m_decoded.insert(resourceId, buffer);
    else m_decoded.remove(resourceId);
    m_edits->setOriginal(resourceId, buffer);
    return bool(buffer);
}

void AudioSession::sourceEdited(const QString &resourceId)
{
    // AGENT-CONTRACT (D-134): a new source revision invalidates its resampled
    // copies and recompiles, so playback and renders use the edited samples.
    for (auto it = m_resampled.begin(); it != m_resampled.end();)
        it = it.key().endsWith(QLatin1Char('/') + resourceId) ? m_resampled.erase(it) : std::next(it);
    if (m_opening) return;
    emit mediaChanged();
    rebuild();
}

QString AudioSession::insertClip(TimelineClip clip, const QString &trackId, qint64 at, QString *error)
{
    TimelineEditRequest request;
    request.kind = TimelineEditKind::InsertClips;
    request.trackId = QUuid::fromString(trackId);
    request.label = clip.kind == QLatin1String("midi") ? tr("Add MIDI clip") : tr("Add audio clip");
    clip.start = 0;
    request.templates = {{clip, 0}};
    OperationError problem;
    const QString operation = m_timeline->begin(request, &problem);
    if (operation.isEmpty()) {
        *error = problem.message;
        return {};
    }
    TimelineEditInput input;
    input.pointerTicks = at;
    input.snap = false;
    const TimelinePlan plan = m_timeline->update(operation, input);
    if (plan.error) {
        m_timeline->cancel(operation);
        *error = plan.error.message;
        return {};
    }
    QStringList created;
    if (const OperationError committed = m_timeline->commit(operation, nullptr, &created)) {
        *error = committed.message;
        return {};
    }
    return created.value(0);
}

QString AudioSession::addTrack(const QString &name)
{
    clearError();
    TimelineEditRequest request;
    request.kind = TimelineEditKind::AddTrack;
    TimelineTrack track;
    track.id = QUuid::createUuid();
    track.name = name;
    track.kind = QStringLiteral("audio");
    request.track = track;
    OperationError problem;
    const QString operation = m_timeline->begin(request, &problem);
    QStringList created;
    if (operation.isEmpty() || m_timeline->update(operation, {}).error
        || (problem = m_timeline->commit(operation, nullptr, &created))) {
        if (!operation.isEmpty()) m_timeline->cancel(operation);
        fail(problem.message.isEmpty() ? tr("The track could not be added") : problem.message);
        return {};
    }
    return created.value(0);
}

bool AudioSession::setTrackArmed(const QString &trackId, bool armed)
{
    clearError();
    TimelineEditRequest request;
    request.kind = TimelineEditKind::TrackProperty;
    request.trackId = QUuid::fromString(trackId);
    request.property = QStringLiteral("armed");
    request.value = armed;
    if (const OperationError problem = m_timeline->apply(request, {})) return fail(problem.message);
    return true;
}

QString AudioSession::ensureGainParameter(const QString &trackId)
{
    const QString id = AudioRenderGraph::gainParameterId(trackId);
    if (m_automation->lane(id)) return id;
    AutomationParameter parameter;
    parameter.id = id;
    const TimelineTrack *track = m_timeline->trackRecord(QUuid::fromString(trackId));
    parameter.name = tr("%1 gain").arg(track ? track->name : trackId);
    parameter.range = {UnitDimension::Gain, QStringLiteral("dB"), -60.0, 12.0};
    parameter.defaultValue = 0.0;
    OperationError problem;
    if (!m_automation->addParameter(parameter, &problem)) {
        fail(problem.message);
        return {};
    }
    return id;
}

QVariantMap AudioSession::importMedia(const QString &file, const QString &trackId, const QString &startTicks)
{
    clearError();
    const QString path = QFileInfo(localPath(file)).absoluteFilePath();
    const auto start = TimelineTime::parseDecimal(startTicks);
    QString error;
    const AudioBufferPtr buffer = AudioFile::readWav(path, &error);
    const auto duration = buffer ? TimelineTime::rescale(buffer->frames, {buffer->sampleRate, 1},
                                                         m_timeline->timebase(), TimeMap::NearestAway)
                                 : std::nullopt;
    if (!start) error = tr("The start position must be decimal ticks");
    else if (buffer && (!duration || *duration < 1)) error = tr("The audio file has no samples");
    if (!error.isEmpty() || !buffer) {
        fail(error);
        return failure(error);
    }
    const QString resourceId = QStringLiteral("media:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!m_resources->registerResource(resourceId, QUrl::fromLocalFile(path).toString())) {
        fail(m_resources->lastError());
        return failure(m_lastError);
    }
    m_decoded.insert(resourceId, buffer);
    m_edits->setOriginal(resourceId, buffer);
    TimelineClip clip;
    clip.id = QUuid::createUuid();
    clip.duration = *duration;
    clip.sourceDuration = *duration;
    clip.resourceId = resourceId;
    clip.kind = QStringLiteral("audio");
    clip.label = QFileInfo(path).completeBaseName();
    const QString clipId = insertClip(clip, trackId, *start, &error);
    if (clipId.isEmpty()) {
        m_decoded.remove(resourceId);
        m_edits->setOriginal(resourceId, nullptr);
        m_resources->removeResource(resourceId);
        fail(error);
        return failure(error);
    }
    emit mediaChanged();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("clipId"), clipId}, {QStringLiteral("resourceId"), resourceId}};
}

QString AudioSession::armedTrack() const
{
    for (const QUuid &id : m_timeline->trackOrder())
        if (const TimelineTrack *track = m_timeline->trackRecord(id); track && track->armed && isAudio(track->kind))
            return id.toString(QUuid::WithoutBraces);
    return {};
}

void AudioSession::updateRecordTarget()
{
    if (m_engine) m_engine->setRecordTarget(!armedTrack().isEmpty(), tr("Arm an audio track to record"));
}

bool AudioSession::relinkMedia(const QString &resourceId, const QString &file)
{
    clearError();
    const QString path = QFileInfo(localPath(file)).absoluteFilePath();
    QString error;
    if (!AudioFile::readWav(path, &error)) return fail(error);
    if (!m_resources->relink(resourceId, QUrl::fromLocalFile(path).toString())) return fail(m_resources->lastError());
    decode(resourceId);
    emit mediaChanged();
    rebuild();
    return true;
}

bool AudioSession::save(const QString &file)
{
    clearError();
    const QString path = QFileInfo(localPath(file)).absoluteFilePath();
    const QDir base = QFileInfo(path).absoluteDir();
    QJsonArray media;
    for (const QVariant &value : m_resources->resources()) {
        const QVariantMap entry = value.toMap();
        QString uri = entry.value(QStringLiteral("uri")).toString();
        if (QUrl(uri).isLocalFile()) uri = base.relativeFilePath(QUrl(uri).toLocalFile());
        const QString expected = entry.value(QStringLiteral("expectedSha256")).toString();
        media.push_back(QJsonObject{{QStringLiteral("resourceId"), entry.value(QStringLiteral("resourceId")).toString()},
                                    {QStringLiteral("uri"), uri},
                                    {QStringLiteral("sha256"), expected.isEmpty()
                                         ? entry.value(QStringLiteral("sha256")).toString() : expected}});
    }
    const QJsonObject root{{QStringLiteral("schema"), schemaId()},
                           {QStringLiteral("sampleRate"), sampleRate()},
                           {QStringLiteral("tempo"), QJsonObject::fromVariantMap(m_tempo->contentMap())},
                           {QStringLiteral("automation"), QJsonObject::fromVariantMap(m_automation->contentMap())},
                           {QStringLiteral("timeline"), QJsonObject::fromVariantMap(m_timeline->contentMap())},
                           {QStringLiteral("mixer"), QJsonObject::fromVariantMap(m_mixer->contentMap())},
                           {QStringLiteral("edits"), QJsonObject::fromVariantMap(m_edits->contentMap())},
                           {QStringLiteral("midi"), QJsonObject::fromVariantMap(m_midi->contentMap())},
                           {QStringLiteral("comps"), QJsonObject::fromVariantMap(m_comps->contentMap())},
                           {QStringLiteral("plugins"), QJsonObject::fromVariantMap(m_plugins->saveContent())},
                           {QStringLiteral("media"), media}};
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(QJsonDocument(root).toJson()) < 0 || !out.commit())
        return fail(tr("The session could not be saved: %1").arg(out.errorString()));
    if (m_filePath != path) {
        m_filePath = path;
        emit filePathChanged();
    }
    return true;
}

bool AudioSession::open(const QString &file)
{
    clearError();
    const QString path = QFileInfo(localPath(file)).absoluteFilePath();
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) return fail(tr("The session cannot be opened: %1").arg(in.errorString()));
    if (in.size() > kMaximumSessionBytes) return fail(tr("The session file is too large"));
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(in.readAll(), &parse);
    const QVariantMap root = document.object().toVariantMap();
    if (!document.isObject()) return fail(tr("The session is not valid JSON: %1").arg(parse.errorString()));
    if (root.value(QStringLiteral("schema")).toString() != schemaId())
        return fail(tr("Unsupported audio session schema"));
    // AGENT-GUARD: opening is all or nothing. Each model validates before it
    // replaces its content, so when a later part is rejected the parts already
    // replaced are restored; a bad file never leaves a half-loaded session.
    // The mixer part is last: its routes name buses it defines itself and
    // tracks the timeline part just loaded. A session without one (saved
    // before WP-062) opens with the default mix. The source edits (WP-063)
    // come next and are empty in older files, then MIDI (WP-064) and take comps (WP-065).
    static constexpr const char *kParts[] = {"tempo", "automation", "timeline", "mixer", "edits", "midi", "comps"};
    const QVariantMap before[] = {m_tempo->contentMap(), m_automation->contentMap(), m_timeline->contentMap(),
                                  m_mixer->contentMap(), m_edits->contentMap(), m_midi->contentMap(),
                                  m_comps->contentMap()};
    const auto loadPart = [this](int part, const QVariantMap &content) {
        return part == 0 ? m_tempo->loadContent(content)
               : part == 1 ? m_automation->loadContent(content)
               : part == 2 ? m_timeline->loadContent(content)
               : part == 3 ? m_mixer->loadContent(content)
               : part == 4 ? m_edits->loadContent(content)
               : part == 5 ? m_midi->loadContent(content) : m_comps->loadContent(content);
    };
    // Plugins (WP-066) are validated with the other parts and loaded last.
    if (QString problem; !AudioPluginHost::validateContent(root.value(QStringLiteral("plugins")).toMap(), &problem))
        return fail(tr("The session plugins could not be loaded: %1").arg(problem));
    m_opening = true; // source edits and their originals arrive below; one recompile at the end
    for (int part = 0; part < 7; ++part) {
        const QString key = QString::fromLatin1(kParts[part]);
        const QVariantMap result = loadPart(part, root.value(key).toMap());
        if (!result.value(QStringLiteral("ok")).toBool()) {
            for (int loaded = part - 1; loaded >= 0; --loaded) loadPart(loaded, before[loaded]);
            m_opening = false;
            return fail(tr("The session %1 could not be loaded: %2").arg(key, resultError(result)));
        }
    }
    for (const QVariant &value : m_resources->resources())
        m_resources->removeResource(value.toMap().value(QStringLiteral("resourceId")).toString());
    m_decoded.clear();
    m_resampled.clear();
    const QDir base = QFileInfo(path).absoluteDir();
    for (const QVariant &value : root.value(QStringLiteral("media")).toList()) {
        const QVariantMap entry = value.toMap();
        const QString id = entry.value(QStringLiteral("resourceId")).toString();
        QString uri = entry.value(QStringLiteral("uri")).toString();
        if (QUrl(uri).scheme().isEmpty()) uri = QUrl::fromLocalFile(base.absoluteFilePath(uri)).toString();
        if (m_resources->registerResource(id, uri, entry.value(QStringLiteral("sha256")).toString().toLatin1()))
            decode(id);
    }
    if (!m_engine || m_engine->sampleRate() <= 0) {
        const int saved = root.value(QStringLiteral("sampleRate")).toInt();
        if (saved > 0) m_timeMap->setSampleRate(saved);
    }
    m_plugins->loadContent(root.value(QStringLiteral("plugins")).toMap());
    m_opening = false;
    if (m_filePath != path) {
        m_filePath = path;
        emit filePathChanged();
    }
    emit mediaChanged();
    emit sampleRateChanged();
    updateRecordTarget();
    rebuild();
    return true;
}

bool AudioSession::fail(const QString &message)
{
    m_lastError = message;
    emit lastErrorChanged();
    return false;
}

void AudioSession::clearError()
{
    if (m_lastError.isEmpty()) return;
    m_lastError.clear();
    emit lastErrorChanged();
}

} // namespace QindaTK
