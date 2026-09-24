// SPDX-License-Identifier: LGPL-3.0-or-later
#include "render_queue_model.h"
#include "media_export.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QRegularExpression>
#include <QThread>
#include <QUuid>
#include <algorithm>
#include <exception>

namespace QindaTK {
RenderProduct renderPcm16WavGain(const QJsonObject &, const QString &,
    const CancellationToken &, const JobProgress &);
namespace {
QString string(const QJsonObject &object, const char *key)
{ return object.value(QLatin1String(key)).toString(); }

QString hashBytes(const QByteArray &bytes)
{ return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()); }

QString hashFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return {};
    return QString::fromLatin1(hash.result().toHex());
}

bool safeId(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_-]{1,128}$"));
    return pattern.match(id).hasMatch();
}

QString snapshotHash(const QJsonObject &snapshot)
{ return hashBytes(QJsonDocument(snapshot).toJson(QJsonDocument::Compact)); }

QString verifyProduct(const QJsonObject &snapshot)
{
    const QDir output(string(snapshot, "outputPath"));
    QFile manifestFile(output.filePath(QStringLiteral("render-manifest.json")));
    if (!manifestFile.open(QIODevice::ReadOnly)) return QStringLiteral("Output manifest missing");
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(manifestFile.readAll(), &parse);
    const QJsonObject manifest = document.object();
    if (parse.error != QJsonParseError::NoError || !document.isObject()
        || manifest.value(QStringLiteral("version")).toInt() != 1
        || string(manifest, "jobId") != string(snapshot, "id")
        || string(manifest, "snapshotSha256") != snapshotHash(snapshot))
        return QStringLiteral("Output manifest does not match job snapshot");
    const QJsonArray files = manifest.value(QStringLiteral("files")).toArray();
    if (files.isEmpty()) return QStringLiteral("Output manifest has no files");
    for (const QJsonValue &value : files) {
        const QJsonObject item = value.toObject();
        const QString name = string(item, "file");
        if (name.isEmpty() || QFileInfo(name).fileName() != name
            || hashFile(output.filePath(name)) != string(item, "sha256")
            || QFileInfo(output.filePath(name)).size() != item.value(QStringLiteral("size")).toVariant().toLongLong())
            return QStringLiteral("Output file missing or corrupt: %1").arg(name);
    }
    return {};
}

JobResult runRender(const QJsonObject &snapshot, const RenderProvider &provider,
                    const CancellationToken &cancel, const JobProgress &progress)
{
    const QString target = string(snapshot, "outputPath");
    const QFileInfo targetInfo(target);
    QDir parent(targetInfo.absolutePath());
    const QString stageName = QStringLiteral(".%1.pending-%2").arg(targetInfo.fileName(), string(snapshot, "id"));
    const QString stagePath = parent.filePath(stageName);
    auto fail = [&](const QString &message) {
        QDir(stagePath).removeRecursively();
        return JobResult{{}, {ErrorCode::BackendFailure, message, {}, {}}, false};
    };
    if (QFileInfo::exists(target)) return fail(QStringLiteral("Output path already exists"));
    if (!parent.mkpath(QStringLiteral("."))) return fail(QStringLiteral("Cannot create output parent"));
    QDir(stagePath).removeRecursively(); // AGENT-GUARD: only this job's deterministic private stage.
    if (!parent.mkdir(stageName)) return fail(QStringLiteral("Cannot create staging directory"));
    RenderProduct product;
    try {
        product = provider(snapshot, stagePath, cancel, progress);
    } catch (const std::exception &exception) {
        return fail(QString::fromUtf8(exception.what()));
    } catch (...) {
        return fail(QStringLiteral("Unknown render provider failure"));
    }
    if (!product.error.isEmpty()) return fail(product.error);
    if (cancel.isCancelled()) return fail(QStringLiteral("Cancelled"));
    if (product.files.isEmpty()) return fail(QStringLiteral("Provider produced no files"));
    QJsonArray files;
    for (const QJsonValue &value : product.files) {
        QJsonObject file = value.toObject();
        const QString name = string(file, "file");
        if (name.isEmpty() || QFileInfo(name).fileName() != name
            || name == QLatin1String("render-manifest.json"))
            return fail(QStringLiteral("Provider returned unsafe output name"));
        const QString path = QDir(stagePath).filePath(name);
        const QString digest = hashFile(path);
        if (digest.isEmpty()) return fail(QStringLiteral("Provider output missing: %1").arg(name));
        file.insert(QStringLiteral("sha256"), digest);
        file.insert(QStringLiteral("size"), QJsonValue::fromVariant(QFileInfo(path).size()));
        files.append(file);
    }
    QJsonObject manifest{{QStringLiteral("version"), 1},
        {QStringLiteral("jobId"), string(snapshot, "id")},
        {QStringLiteral("snapshotSha256"), snapshotHash(snapshot)},
        {QStringLiteral("providerId"), string(snapshot, "providerId")},
        {QStringLiteral("providerVersion"), string(snapshot, "providerVersion")},
        {QStringLiteral("format"), string(snapshot, "format")},
        {QStringLiteral("capabilityId"), string(snapshot, "capabilityId")},
        {QStringLiteral("sourceId"), string(snapshot, "sourceId")},
        {QStringLiteral("sourceRevision"), string(snapshot, "sourceRevision")},
        {QStringLiteral("settingsSha256"), hashBytes(QJsonDocument(snapshot.value(QStringLiteral("settings")).toObject())
            .toJson(QJsonDocument::Compact))},
        {QStringLiteral("files"), files}};
    if (string(snapshot, "providerId") == QLatin1String("qindatk.caption-png-sequence")) {
        const QJsonObject settings = snapshot.value(QStringLiteral("settings")).toObject();
        manifest.insert(QStringLiteral("sourceFrames"), settings.value(QStringLiteral("frames")));
        manifest.insert(QStringLiteral("captionTrackSha256"), hashBytes(settings.value(QStringLiteral("captionTrackJson"))
            .toString().toUtf8()));
    }
    QSaveFile manifestFile(QDir(stagePath).filePath(QStringLiteral("render-manifest.json")));
    manifestFile.setDirectWriteFallback(false);
    const QByteArray encoded = QJsonDocument(manifest).toJson(QJsonDocument::Indented);
    if (!manifestFile.open(QIODevice::WriteOnly) || manifestFile.write(encoded) != encoded.size()
        || !manifestFile.commit()) return fail(QStringLiteral("Cannot write output manifest"));
    if (cancel.isCancelled()) return fail(QStringLiteral("Cancelled"));
    if (QFileInfo::exists(target) || !parent.rename(stageName, targetInfo.fileName()))
        return fail(QStringLiteral("Cannot publish output; target already exists or rename failed"));
    const QString verifyError = verifyProduct(snapshot);
    if (!verifyError.isEmpty())
        return {{}, {ErrorCode::BackendFailure, verifyError, {}, {}}, true};
    return {{{QStringLiteral("outputPath"), target},
             {QStringLiteral("manifestPath"), QDir(target).filePath(QStringLiteral("render-manifest.json"))},
             {QStringLiteral("fileCount"), files.size()}}, {}, true};
}

bool terminal(const QString &state)
{ return state == QLatin1String("succeeded") || state == QLatin1String("failed")
      || state == QLatin1String("cancelled") || state == QLatin1String("interrupted"); }
}

RenderQueueModel::RenderQueueModel(QObject *parent) : QAbstractListModel(parent), m_jobs(std::make_unique<JobModel>())
{
    registerProvider(QStringLiteral("qindatk.caption-png-sequence"), QStringLiteral("1"),
                     {QStringLiteral("png-sequence")}, renderCaptionPngSequence);
    registerProvider(QStringLiteral("qindatk.rich-document-pdf"), QStringLiteral("1"),
                     {QStringLiteral("pdf")}, renderRichDocumentPdf);
    registerProvider(QStringLiteral("qindatk.pcm16-wav-gain"), QStringLiteral("1"),
                     {QStringLiteral("wav")}, renderPcm16WavGain);
    if (MediaEncoder::available()) // FFV1/PCM Matroska programs (docs/media-playback.md)
        registerProvider(MediaExport::providerId(), QStringLiteral("1"), {QStringLiteral("mkv")},
                         MediaExport::renderProvider);
    connect(m_jobs.get(), &JobModel::jobFinished, this, &RenderQueueModel::finish);
    connect(m_jobs.get(), &JobModel::changed, this, [this]() {
        for (int row = 0; row < m_entries.size(); ++row) {
            Entry &entry = m_entries[row];
            if (entry.state != QLatin1String("running") && entry.state != QLatin1String("cancelling")) continue;
            const QVariantMap work = m_jobs->job(string(entry.snapshot, "id") + QLatin1Char('/')
                + QString::number(entry.attempt));
            const int progress = work.value(QStringLiteral("progress")).toInt();
            if (progress != entry.progress) { entry.progress = progress; update(row); }
        }
    });
}

RenderQueueModel::~RenderQueueModel() = default;
int RenderQueueModel::rowCount(const QModelIndex &parent) const
{ return parent.isValid() ? 0 : m_entries.size(); }

QHash<int, QByteArray> RenderQueueModel::roleNames() const
{
    return {{IdRole, "jobId"}, {LabelRole, "label"}, {StateRole, "jobState"},
        {ProgressRole, "progress"}, {ErrorRole, "error"}, {ResultRole, "result"},
        {OutputPathRole, "outputPath"}, {ProviderRole, "providerId"},
        {FormatRole, "format"}, {SourceIdRole, "sourceId"},
        {DependenciesRole, "dependencies"}, {AttemptRole, "attempt"}};
}

QVariant RenderQueueModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) return {};
    const Entry &entry = m_entries.at(index.row());
    const QJsonObject &snapshot = entry.snapshot;
    switch (role) {
    case Qt::DisplayRole: case LabelRole: return string(snapshot, "label");
    case IdRole: return string(snapshot, "id");
    case StateRole: return entry.state;
    case ProgressRole: return entry.progress;
    case ErrorRole: return entry.error;
    case ResultRole: return entry.result.toVariantMap();
    case OutputPathRole: return string(snapshot, "outputPath");
    case ProviderRole: return string(snapshot, "providerId");
    case FormatRole: return string(snapshot, "format");
    case SourceIdRole: return string(snapshot, "sourceId");
    case DependenciesRole: return snapshot.value(QStringLiteral("dependencies")).toArray().toVariantList();
    case AttemptRole: return entry.attempt;
    default: return {};
    }
}

void RenderQueueModel::update(int row)
{ if (row >= 0) emit dataChanged(index(row), index(row)); emit changed(); }

int RenderQueueModel::indexOf(const QString &id) const
{ for (int i = 0; i < m_entries.size(); ++i) if (string(m_entries.at(i).snapshot, "id") == id) return i; return -1; }

void RenderQueueModel::setMaxResourceSlots(int value)
{
    if (value < 1 || value > 16 || value == m_maxResourceSlots) return;
    for (const Entry &entry : m_entries)
        if (entry.snapshot.value(QStringLiteral("resourceSlots")).toInt() > value) return;
    m_maxResourceSlots = value;
    m_jobs->setMaxConcurrent(value);
    emit changed();
    pump();
}

void RenderQueueModel::registerProvider(const QString &id, const QString &version,
    const QStringList &formats, RenderProvider provider)
{
    if (!id.isEmpty() && !version.isEmpty() && !formats.isEmpty() && provider)
        m_providers.insert(id, {version, formats, std::move(provider)});
    pump();
}

bool RenderQueueModel::persist()
{
    if (!m_open) return false;
    QJsonArray rows;
    for (const Entry &entry : m_entries)
        rows.append(QJsonObject{{QStringLiteral("snapshot"), entry.snapshot},
            {QStringLiteral("state"), entry.state}, {QStringLiteral("error"), entry.error},
            {QStringLiteral("result"), entry.result}, {QStringLiteral("attempt"), entry.attempt}});
    QSaveFile file(m_storagePath);
    file.setDirectWriteFallback(false);
    const QByteArray bytes = QJsonDocument(QJsonObject{{QStringLiteral("version"), 1},
        {QStringLiteral("maxResourceSlots"), m_maxResourceSlots},
        {QStringLiteral("jobs"), rows}}).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        m_lastError = QStringLiteral("Could not save queue journal: %1").arg(file.errorString());
        emit changed();
        return false;
    }
    return true;
}

QString RenderQueueModel::stagePath(const Entry &entry) const
{
    const QFileInfo target(string(entry.snapshot, "outputPath"));
    return QDir(target.absolutePath()).filePath(QStringLiteral(".%1.pending-%2")
        .arg(target.fileName(), string(entry.snapshot, "id")));
}

bool RenderQueueModel::open(const QString &path)
{
    if (m_open || path.trimmed().isEmpty() || !QFileInfo(path).isAbsolute()) {
        m_lastError = QStringLiteral("Open one absolute queue journal path per model"); emit changed(); return false;
    }
    QVector<Entry> loaded;
    QFile file(path);
    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly)) { m_lastError = file.errorString(); emit changed(); return false; }
        QJsonParseError parse;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse);
        const QJsonObject root = document.object();
        if (parse.error != QJsonParseError::NoError || !document.isObject()
            || root.value(QStringLiteral("version")).toInt() != 1 || !root.value(QStringLiteral("jobs")).isArray()) {
            m_lastError = QStringLiteral("Invalid queue journal"); emit changed(); return false;
        }
        const int savedLimit = root.value(QStringLiteral("maxResourceSlots")).toInt(4);
        if (savedLimit < 1 || savedLimit > 16) {
            m_lastError = QStringLiteral("Invalid queue resource limit"); emit changed(); return false;
        }
        m_maxResourceSlots = savedLimit;
        m_jobs->setMaxConcurrent(savedLimit);
        QSet<QString> ids;
        QSet<QString> outputs;
        for (const QJsonValue &value : root.value(QStringLiteral("jobs")).toArray()) {
            const QJsonObject row = value.toObject();
            Entry entry;
            entry.snapshot = row.value(QStringLiteral("snapshot")).toObject();
            entry.state = string(row, "state");
            entry.error = string(row, "error");
            entry.result = row.value(QStringLiteral("result")).toObject();
            entry.attempt = row.value(QStringLiteral("attempt")).toInt();
            const QString id = string(entry.snapshot, "id");
            const QString output = string(entry.snapshot, "outputPath");
            if (!safeId(id) || ids.contains(id) || outputs.contains(output)
                || (entry.state != QLatin1String("queued") && entry.state != QLatin1String("running")
                    && entry.state != QLatin1String("cancelling") && !terminal(entry.state))
                || !QFileInfo(output).isAbsolute() || QFileInfo(output).fileName().isEmpty()
                || entry.attempt < 0) {
                m_lastError = QStringLiteral("Invalid queue row"); emit changed(); return false;
            }
            ids.insert(id);
            outputs.insert(output);
            loaded.append(entry);
        }
    } else if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        m_lastError = QStringLiteral("Cannot create queue journal directory"); emit changed(); return false;
    }
    beginResetModel(); m_entries = std::move(loaded); endResetModel();
    m_storagePath = path;
    m_open = true;
    for (Entry &entry : m_entries) {
        if (entry.state == QLatin1String("running") || entry.state == QLatin1String("cancelling")) {
            QDir(stagePath(entry)).removeRecursively();
            const QString verified = verifyProduct(entry.snapshot);
            entry.state = verified.isEmpty() ? QStringLiteral("succeeded") : QStringLiteral("interrupted");
            entry.error = verified.isEmpty() ? QString() : QStringLiteral("Interrupted before verified publish; retry explicitly");
        } else if (entry.state == QLatin1String("succeeded")) {
            const QString verified = verifyProduct(entry.snapshot);
            if (!verified.isEmpty()) { entry.state = QStringLiteral("failed"); entry.error = verified; }
        }
    }
    m_lastError.clear();
    if (!persist()) return false;
    emit changed(); pump(); return true;
}

bool RenderQueueModel::validate(const QJsonObject &s, QString *error) const
{
    const QString id = string(s, "id"), providerId = string(s, "providerId");
    const auto provider = m_providers.constFind(providerId);
    const QString output = string(s, "outputPath");
    if (!safeId(id) || indexOf(id) >= 0 || !m_open
        || provider == m_providers.cend() || string(s, "providerVersion") != provider->version
        || !provider->formats.contains(string(s, "format"))
        || string(s, "capabilityId").isEmpty() || string(s, "sourceId").isEmpty()
        || string(s, "sourceRevision").isEmpty() || !s.value(QStringLiteral("settings")).isObject()
        || output.isEmpty() || !QFileInfo(output).isAbsolute()
        || QFileInfo(output).fileName().isEmpty() || QFileInfo::exists(output)
        || s.value(QStringLiteral("resourceSlots")).toInt() < 1
        || s.value(QStringLiteral("resourceSlots")).toInt() > m_maxResourceSlots) {
        *error = QStringLiteral("Invalid snapshot, provider, output path or resource limit"); return false;
    }
    for (const Entry &existing : m_entries)
        if (string(existing.snapshot, "outputPath") == output) {
            *error = QStringLiteral("Output path is already reserved by a queue job"); return false;
        }
    const QJsonArray dependencies = s.value(QStringLiteral("dependencies")).toArray();
    for (const QJsonValue &dependency : dependencies)
        if (dependency.toString().isEmpty() || indexOf(dependency.toString()) < 0
            || dependency.toString() == id) {
            *error = QStringLiteral("Dependencies must name previously enqueued jobs"); return false;
        }
    return true;
}

QString RenderQueueModel::enqueueJob(const QVariantMap &input)
{
    QJsonObject snapshot = QJsonObject::fromVariantMap(input);
    if (string(snapshot, "id").isEmpty())
        snapshot.insert(QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!snapshot.contains(QStringLiteral("dependencies"))) snapshot.insert(QStringLiteral("dependencies"), QJsonArray{});
    if (!snapshot.contains(QStringLiteral("resourceSlots"))) snapshot.insert(QStringLiteral("resourceSlots"), 1);
    QString error;
    if (!validate(snapshot, &error)) { m_lastError = error; emit changed(); return {}; }
    const int row = m_entries.size();
    beginInsertRows({}, row, row); Entry inserted; inserted.snapshot = snapshot; m_entries.append(inserted); endInsertRows();
    if (!persist()) { beginRemoveRows({}, row, row); m_entries.removeAt(row); endRemoveRows(); return {}; }
    m_lastError.clear(); emit changed(); pump(); return string(snapshot, "id");
}

void RenderQueueModel::pump()
{
    if (!m_open || QThread::currentThread() != thread()) return;
    for (int i = 0; i < m_entries.size(); ++i) {
        Entry &entry = m_entries[i];
        if (entry.state != QLatin1String("queued")) continue;
        bool ready = true;
        for (const QJsonValue &dependency : entry.snapshot.value(QStringLiteral("dependencies")).toArray()) {
            const int dep = indexOf(dependency.toString());
            if (dep < 0 || m_entries.at(dep).state != QLatin1String("succeeded")) {
                ready = false;
                if (dep >= 0 && terminal(m_entries.at(dep).state)) {
                    entry.state = QStringLiteral("failed");
                    entry.error = QStringLiteral("Dependency did not succeed: %1").arg(dependency.toString());
                    persist(); update(i); emit jobFinished(string(entry.snapshot, "id"), entry.state);
                }
                break;
            }
        }
        if (!ready) continue;
        const int requiredSlots = entry.snapshot.value(QStringLiteral("resourceSlots")).toInt();
        const auto provider = m_providers.constFind(string(entry.snapshot, "providerId"));
        if (provider == m_providers.cend() || provider->version != string(entry.snapshot, "providerVersion")
            || m_usedSlots + requiredSlots > m_maxResourceSlots) continue;
        entry.state = QStringLiteral("running"); entry.error.clear(); entry.progress = 0; ++entry.attempt;
        if (!persist()) { entry.state = QStringLiteral("queued"); --entry.attempt; continue; }
        m_usedSlots += requiredSlots; update(i);
        const QJsonObject snapshot = entry.snapshot;
        JobSpec spec;
        spec.id = string(snapshot, "id") + QLatin1Char('/') + QString::number(entry.attempt);
        spec.label = string(snapshot, "label");
        spec.work = [snapshot, worker = provider->work](const CancellationToken &token, const JobProgress &progress) {
            return runRender(snapshot, worker, token, progress);
        };
        if (m_jobs->enqueue(std::move(spec)).isEmpty()) {
            m_usedSlots -= requiredSlots; entry.state = QStringLiteral("failed");
            entry.error = m_jobs->lastError(); persist(); update(i);
        }
    }
}

void RenderQueueModel::finish(const QString &workerId, JobModel::JobState state,
    const QVariantMap &result, const QString &error)
{
    const int slash = workerId.lastIndexOf(QLatin1Char('/'));
    const QString id = workerId.left(slash);
    const int row = indexOf(id);
    if (row < 0) return;
    Entry &entry = m_entries[row];
    m_usedSlots -= entry.snapshot.value(QStringLiteral("resourceSlots")).toInt();
    const QString verifyError = QFileInfo::exists(string(entry.snapshot, "outputPath"))
        ? verifyProduct(entry.snapshot) : QStringLiteral("No published output");
    if (verifyError.isEmpty()) {
        entry.state = QStringLiteral("succeeded"); entry.error.clear(); entry.progress = 100;
        entry.result = QJsonObject::fromVariantMap(result);
        entry.result.insert(QStringLiteral("outputPath"), string(entry.snapshot, "outputPath"));
        entry.result.insert(QStringLiteral("manifestPath"), QDir(string(entry.snapshot, "outputPath"))
            .filePath(QStringLiteral("render-manifest.json")));
    } else if (state == JobModel::Cancelled || state == JobModel::Cancelling) {
        entry.state = QStringLiteral("cancelled"); entry.error = QStringLiteral("Cancelled");
    } else {
        entry.state = QStringLiteral("failed"); entry.error = error.isEmpty() ? verifyError : error;
    }
    persist(); update(row); emit jobFinished(id, entry.state); pump();
}

bool RenderQueueModel::cancelJob(const QString &id)
{
    const int row = indexOf(id);
    if (row < 0) return false;
    Entry &entry = m_entries[row];
    if (entry.state == QLatin1String("queued")) {
        entry.state = QStringLiteral("cancelled"); entry.error = QStringLiteral("Cancelled");
        persist(); update(row); emit jobFinished(id, entry.state); pump(); return true;
    }
    if (entry.state != QLatin1String("running")) return false;
    entry.state = QStringLiteral("cancelling"); persist(); update(row);
    return m_jobs->cancelJob(id + QLatin1Char('/') + QString::number(entry.attempt));
}

bool RenderQueueModel::retryJob(const QString &id)
{
    const int row = indexOf(id);
    if (row < 0 || !terminal(m_entries[row].state) || m_entries[row].state == QLatin1String("succeeded")) return false;
    Entry &entry = m_entries[row];
    if (QFileInfo::exists(string(entry.snapshot, "outputPath"))) {
        m_lastError = QStringLiteral("Output path already exists; cannot overwrite on retry"); emit changed(); return false;
    }
    entry.state = QStringLiteral("queued"); entry.error.clear(); entry.result = {};
    if (!persist()) return false;
    update(row); pump(); return true;
}

QVariantMap RenderQueueModel::job(const QString &id) const
{
    const int row = indexOf(id);
    if (row < 0) return {};
    const Entry &entry = m_entries.at(row);
    return {{QStringLiteral("snapshot"), entry.snapshot.toVariantMap()},
        {QStringLiteral("state"), entry.state}, {QStringLiteral("error"), entry.error},
        {QStringLiteral("result"), entry.result.toVariantMap()},
        {QStringLiteral("attempt"), entry.attempt}, {QStringLiteral("progress"), entry.progress}};
}

bool RenderQueueModel::verifyOutput(const QString &id)
{
    const int row = indexOf(id);
    if (row < 0) return false;
    const QString error = verifyProduct(m_entries.at(row).snapshot);
    if (!error.isEmpty()) { m_lastError = error; emit changed(); return false; }
    return true;
}

QVariantList RenderQueueModel::jobs() const
{
    QVariantList rows;
    for (const Entry &entry : m_entries) {
        const QJsonObject &snapshot = entry.snapshot;
        const QString outputPath = string(snapshot, "outputPath");
        rows.append(QVariantMap{
            {QStringLiteral("jobId"), string(snapshot, "id")},
            {QStringLiteral("label"), string(snapshot, "label")},
            {QStringLiteral("state"), entry.state},
            {QStringLiteral("progress"), entry.progress},
            {QStringLiteral("sourceSnapshot"), string(snapshot, "sourceId")},
            {QStringLiteral("sourceVersion"), string(snapshot, "sourceRevision")},
            {QStringLiteral("sourceRange"), snapshot.value(QStringLiteral("sourceRange")).toVariant()},
            {QStringLiteral("outputFormat"), string(snapshot, "format")},
            {QStringLiteral("outputPath"), outputPath},
            {QStringLiteral("error"), entry.error},
            {QStringLiteral("outputManifest"), QVariantMap{
                {QStringLiteral("validated"), entry.state == QLatin1String("succeeded")},
                {QStringLiteral("path"), QDir(outputPath).filePath(QStringLiteral("render-manifest.json"))}}}});
    }
    return rows;
}

void RenderQueueModel::start() { pump(); }
} // namespace QindaTK
