// SPDX-License-Identifier: LGPL-3.0-or-later
#include "format_registry.h"
#include "format_io.h"
#include "rich_text_native_format.h"
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QThread>

namespace QindaTK {
namespace {
constexpr qsizetype nativeMaximum = ReferenceDocumentCodec::MaximumBytes;
FormatPlan failure(ErrorCode code, const QString &message, const QString &field = {})
{
    FormatPlan result;
    result.error = {code, message, field, QStringLiteral("format.registry")};
    return result;
}
JobResult failedJob(const OperationError &error, const QString &operation,
                    const QString &formatId)
{
    return {{{"operation", operation}, {"formatId", formatId},
             {"error", FormatIO::errorMap(error)}}, error};
}
bool operationSupported(const FormatProviderPtr &provider, const QString &operation)
{
    return provider->metadata().value(QStringLiteral("operations")).toStringList().contains(operation);
}
qsizetype maximumFor(const FormatProviderPtr &provider)
{
    return provider->metadata().value(QStringLiteral("maximumBytes"), nativeMaximum).toLongLong();
}
bool extensionMatches(const FormatProviderPtr &provider, const QUrl &url)
{
    const QString suffix = QFileInfo(url.toLocalFile()).suffix();
    for (const QString &extension : provider->metadata().value(QStringLiteral("extensions")).toStringList())
        if (suffix.compare(extension, Qt::CaseInsensitive) == 0) return true;
    return false;
}
FormatPlan destinationPlan(const FormatProviderPtr &provider, const QUrl &destination,
                           const QVariantMap &options)
{
    FormatPlan plan;
    plan.options = options;
    QString path;
    if (auto error = FormatIO::localPath(destination, &path)) { plan.error = error; return plan; }
    if (!extensionMatches(provider, destination)) {
        plan.warnings << FormatIO::issue(QStringLiteral("extensionMismatch"), QStringLiteral("warning"),
            QStringLiteral("destination"), QStringLiteral("Destination extension does not match format"));
        if (!options.value(QStringLiteral("allowExtensionMismatch")).toBool()) {
            plan.error = {ErrorCode::Validation, QStringLiteral("Confirm destination extension mismatch"),
                          QStringLiteral("allowExtensionMismatch"), QStringLiteral("format.registry")};
            return plan;
        }
    }
    if (options.value(QStringLiteral("conflictPolicy")) == QStringLiteral("replaceIfHash")
        && !options.contains(QStringLiteral("expectedSha256"))) {
        QFile current(path);
        if (!current.open(QIODevice::ReadOnly)) {
            plan.error = {ErrorCode::Conflict, QStringLiteral("Destination is unavailable for replacement"),
                          path, QStringLiteral("format.registry")};
            return plan;
        }
        QCryptographicHash digest(QCryptographicHash::Sha256);
        if (!digest.addData(&current)) {
            plan.error = {ErrorCode::BackendFailure, QStringLiteral("Could not hash destination"),
                          path, QStringLiteral("format.registry")};
            return plan;
        }
        plan.options.insert(QStringLiteral("expectedSha256"),
                            QString::fromLatin1(digest.result().toHex()));
    }
    plan.error = FormatIO::destinationCheck(path, plan.options);
    return plan;
}
}
FormatRegistry::FormatRegistry(QObject *parent)
    : QObject(parent), m_jobs(new JobModel(this))
{
    registerProvider(std::make_shared<NativeReferenceFormat>());
    registerProvider(std::make_shared<PlainTextFormat>());
    registerProvider(makeRichTextNativeFormat());
}
bool FormatRegistry::registerProvider(FormatProviderPtr provider)
{
    if (!provider) return false;
    const QString id = provider->metadata().value(QStringLiteral("formatId")).toString();
    if (id.isEmpty() || this->provider(id)) return false;
    const QStringList operations = provider->metadata().value(QStringLiteral("operations")).toStringList();
    if (operations.isEmpty()) return false;
    for (const QString &operation : operations)
        if (operation != QStringLiteral("import") && operation != QStringLiteral("export")
            && operation != QStringLiteral("roundtrip") && operation != QStringLiteral("render")) return false;
    const QVariantMap metadata = provider->metadata();
    if (operations.contains(QStringLiteral("roundtrip"))
        && (!operations.contains(QStringLiteral("import"))
            || !operations.contains(QStringLiteral("export"))
            || !metadata.value(QStringLiteral("editableImport")).toBool())) return false;
    if (metadata.value(QStringLiteral("renderOnly")).toBool()
        && (operations.contains(QStringLiteral("import"))
            || operations.contains(QStringLiteral("roundtrip"))
            || metadata.value(QStringLiteral("editableImport")).toBool())) return false;
    if (metadata.value(QStringLiteral("roundtrip")).toBool()
        != operations.contains(QStringLiteral("roundtrip"))) return false;
    if (metadata.contains(QStringLiteral("schemas"))
        && metadata.value(QStringLiteral("schemas")).metaType() != QMetaType::fromType<QStringList>())
        return false;
    m_providers.append(std::move(provider));
    emit formatsChanged();
    return true;
}
FormatProviderPtr FormatRegistry::provider(const QString &id) const
{
    for (const auto &entry : m_providers)
        if (entry->metadata().value(QStringLiteral("formatId")).toString() == id) return entry;
    return {};
}
QVariantList FormatRegistry::formats() const
{
    QVariantList result;
    for (const auto &entry : m_providers) result << entry->metadata();
    return result;
}
QVariantList FormatRegistry::formatsForOperation(const QString &operation) const
{
    QVariantList result;
    for (const auto &entry : m_providers)
        if (operationSupported(entry, operation)) result << entry->metadata();
    return result;
}
QString FormatRegistry::detectImport(const QUrl &source) const
{
    QString path;
    if (FormatIO::localPath(source, &path)) return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QByteArray prefix = file.read(4096);
    for (const auto &entry : m_providers)
        if (operationSupported(entry, QStringLiteral("import"))
            && entry->recognizes(prefix, source))
            return entry->metadata().value(QStringLiteral("formatId")).toString();
    return {};
}
QVariantMap FormatRegistry::preflightImport(const QUrl &source, const QString &formatId,
                                             const QVariantMap &options) const
{
    QString path;
    if (auto error = FormatIO::localPath(source, &path))
        return FormatIO::planMap(formatId, QStringLiteral("import"), {options, {}, {}, error});
    const QString selectedId = formatId.isEmpty() ? detectImport(source) : formatId;
    const auto selected = provider(selectedId);
    if (!selected || !operationSupported(selected, QStringLiteral("import")))
        return FormatIO::planMap(selectedId, QStringLiteral("import"),
            failure(ErrorCode::Unsupported, QStringLiteral("No editable import provider for format")));
    QByteArray bytes;
    if (auto error = FormatIO::readBounded(path, maximumFor(selected), &bytes, nullptr,
                                          selectedId == QStringLiteral("text.utf8")))
        return FormatIO::planMap(selectedId, QStringLiteral("import"), {options, {}, {}, error});
    if (options.contains(QStringLiteral("expectedSourceSha256"))
        && QByteArray::fromHex(options.value(QStringLiteral("expectedSourceSha256")).toString().toLatin1())
            != FormatIO::hash(bytes))
        return FormatIO::planMap(selectedId, QStringLiteral("import"),
            failure(ErrorCode::Conflict, QStringLiteral("Source changed since preflight")));
    auto plan = selected->preflightImport(bytes, options);
    plan.options.insert(QStringLiteral("expectedSourceSha256"),
                        QString::fromLatin1(FormatIO::hash(bytes).toHex()));
    const QVariantMap extra{{"source", source}, {"sourceSha256", QString::fromLatin1(FormatIO::hash(bytes).toHex())},
                            {"sizeBytes", bytes.size()},
                            {"provider", selected->metadata().value(QStringLiteral("formatId"))}};
    return FormatIO::planMap(selectedId, QStringLiteral("import"), plan, extra);
}
QVariantMap FormatRegistry::preflightSnapshot(const SnapshotPtr &snapshot,
                                                const QUrl &destination,
                                                const QString &formatId,
                                                const QVariantMap &options) const
{
    const auto selected = provider(formatId);
    if (!selected || !operationSupported(selected, QStringLiteral("export")))
        return FormatIO::planMap(formatId, QStringLiteral("export"),
            failure(ErrorCode::Unsupported, QStringLiteral("No export provider for format")));
    if (!snapshot)
        return FormatIO::planMap(formatId, QStringLiteral("export"),
            failure(ErrorCode::InvalidArgument, QStringLiteral("No document for export")));
    const QStringList schemas = selected->metadata().value(QStringLiteral("schemas")).toStringList();
    if (!schemas.isEmpty() && !schemas.contains(snapshot->header.schemaId))
        return FormatIO::planMap(formatId, QStringLiteral("export"),
            failure(ErrorCode::Unsupported, QStringLiteral("Format does not support this document schema"),
                    QStringLiteral("schemaId")));
    const QString snapshotHash = QString::fromLatin1(FormatIO::hash(snapshot->payload).toHex());
    if ((options.contains(QStringLiteral("expectedSourceRevision"))
         && options.value(QStringLiteral("expectedSourceRevision")).toString()
             != snapshot->header.revision.toDecimal())
        || (options.contains(QStringLiteral("expectedSnapshotSha256"))
            && options.value(QStringLiteral("expectedSnapshotSha256")).toString() != snapshotHash))
        return FormatIO::planMap(formatId, QStringLiteral("export"),
            failure(ErrorCode::StaleRevision, QStringLiteral("Document changed since preflight")));
    auto plan = selected->preflightExport(snapshot, options);
    plan.options.insert(QStringLiteral("expectedSourceRevision"),
                        snapshot->header.revision.toDecimal());
    plan.options.insert(QStringLiteral("expectedSnapshotSha256"), snapshotHash);
    const auto destinationCheck = destinationPlan(selected, destination, options);
    plan.warnings += destinationCheck.warnings;
    for (auto it = destinationCheck.options.cbegin(); it != destinationCheck.options.cend(); ++it)
        plan.options.insert(it.key(), it.value());
    if (!plan.error) plan.error = destinationCheck.error;
    const QVariantMap extra{{"destination", destination},
                            {"sourceRevision", snapshot->header.revision.toDecimal()},
                            {"provider", selected->metadata().value(QStringLiteral("formatId"))}};
    return FormatIO::planMap(formatId, QStringLiteral("export"), plan, extra);
}
QVariantMap FormatRegistry::preflightExport(PropertyDocument *document, const QUrl &destination,
                                             const QString &formatId, const QVariantMap &options) const
{
    if (!document) return preflightSnapshot({}, destination, formatId, options);
    OperationError error;
    const auto snapshot = ReferenceDocumentCodec::capture(*document, {}, &error);
    return snapshot ? preflightSnapshot(snapshot, destination, formatId, options)
                    : FormatIO::planMap(formatId, QStringLiteral("export"),
                                        {options, {}, {}, error});
}
QVariantMap FormatRegistry::preflightExportSession(DocumentSession *session,
                                                    const QUrl &destination,
                                                    const QString &formatId,
                                                    const QVariantMap &options) const
{
    if (!session) return preflightSnapshot({}, destination, formatId, options);
    OperationError error;
    const auto snapshot = session->captureSnapshot(&error);
    return snapshot ? preflightSnapshot(snapshot, destination, formatId, options)
                    : FormatIO::planMap(formatId, QStringLiteral("export"),
                                        {options, {}, {}, error, {}});
}
bool FormatRegistry::fail(const QString &message)
{
    m_lastError = message;
    emit changed();
    return false;
}
QString FormatRegistry::startImport(const QUrl &source, const QString &formatId,
                                     const QVariantMap &options)
{
    const auto plan = preflightImport(source, formatId, options);
    if (!plan.value(QStringLiteral("ok")).toBool()) {
        fail(plan.value(QStringLiteral("error")).toMap().value(QStringLiteral("message")).toString());
        return {};
    }
    const QString selectedId = plan.value(QStringLiteral("formatId")).toString();
    const auto selected = provider(selectedId);
    const QString path = source.toLocalFile();
    const QByteArray expected = QByteArray::fromHex(plan.value(QStringLiteral("sourceSha256")).toString().toLatin1());
    JobSpec spec;
    spec.label = QStringLiteral("Import %1").arg(QFileInfo(path).fileName());
    spec.work = [selected, selectedId, path, expected, options](const CancellationToken &cancel,
                                                                const JobProgress &progress) {
        QByteArray bytes;
        if (auto error = FormatIO::readBounded(path, maximumFor(selected), &bytes, &cancel,
                                               selectedId == QStringLiteral("text.utf8")))
            return failedJob(error, QStringLiteral("import"), selectedId);
        if (FormatIO::hash(bytes) != expected)
            return failedJob({ErrorCode::Conflict, QStringLiteral("Source changed since preflight"), path, {}},
                             QStringLiteral("import"), selectedId);
        progress(0.2);
        const auto imported = selected->importDocument(bytes, options, cancel, progress);
        if (imported.error) return failedJob(imported.error, QStringLiteral("import"), selectedId);
        return JobResult{{{"operation", QStringLiteral("import")}, {"formatId", selectedId},
                          {"documentBytes", imported.bytes}, {"warnings", imported.warnings},
                          {"losses", imported.losses}, {"sourceSha256", QString::fromLatin1(expected.toHex())}}, {}};
    };
    const QString id = m_jobs->enqueue(std::move(spec));
    if (id.isEmpty()) fail(m_jobs->lastError()); else { m_lastError.clear(); emit changed(); }
    return id;
}
QString FormatRegistry::startExport(PropertyDocument *document, const QUrl &destination,
                                     const QString &formatId, const QVariantMap &options)
{
    if (!document) { fail(QStringLiteral("No document for export")); return {}; }
    OperationError error;
    const auto snapshot = ReferenceDocumentCodec::capture(*document, {}, &error);
    if (!snapshot) { fail(error.message); return {}; }
    return startExportSnapshot(snapshot, destination, formatId, options);
}
QString FormatRegistry::startExportSession(DocumentSession *session, const QUrl &destination,
                                            const QString &formatId, const QVariantMap &options)
{
    if (!session) { fail(QStringLiteral("No document session for export")); return {}; }
    OperationError error;
    const auto snapshot = session->captureSnapshot(&error);
    if (!snapshot) { fail(error.message); return {}; }
    return startExportSnapshot(snapshot, destination, formatId, options);
}
QString FormatRegistry::startExportSnapshot(const SnapshotPtr &snapshot, const QUrl &destination,
                                             const QString &formatId, const QVariantMap &options)
{
    const auto plan = preflightSnapshot(snapshot, destination, formatId, options);
    if (!plan.value(QStringLiteral("ok")).toBool()) {
        fail(plan.value(QStringLiteral("error")).toMap().value(QStringLiteral("message")).toString());
        return {};
    }
    const auto selected = provider(formatId);
    const QVariantMap normalizedOptions = plan.value(QStringLiteral("options")).toMap();
    const QString path = destination.toLocalFile();
    JobSpec spec;
    spec.label = QStringLiteral("Export %1").arg(QFileInfo(path).fileName());
    spec.work = [selected, formatId, snapshot, path, normalizedOptions](const CancellationToken &cancel,
                                                              const JobProgress &progress) {
        const auto providerPlan = selected->preflightExport(snapshot, normalizedOptions);
        if (providerPlan.error) return failedJob(providerPlan.error, QStringLiteral("export"), formatId);
        const auto exported = selected->exportDocument(snapshot, normalizedOptions, cancel, progress);
        if (exported.error) return failedJob(exported.error, QStringLiteral("export"), formatId);
        QByteArray sha256;
        if (auto error = FormatIO::writeAtomic(path, exported.bytes, normalizedOptions, cancel, progress, &sha256))
            return failedJob(error, QStringLiteral("export"), formatId);
        JobResult result;
        result.output = {{"operation", QStringLiteral("export")}, {"formatId", formatId},
                         {"destination", QUrl::fromLocalFile(path)},
                         {"sha256", QString::fromLatin1(sha256.toHex())},
                         {"sourceRevision", snapshot->header.revision.toDecimal()},
                         {"warnings", exported.warnings}, {"losses", exported.losses},
                         {"committed", true}};
        result.committed = true;
        return result;
    };
    const QString id = m_jobs->enqueue(std::move(spec));
    if (id.isEmpty()) fail(m_jobs->lastError()); else { m_lastError.clear(); emit changed(); }
    return id;
}
DocumentSession *FormatRegistry::adoptImport(DocumentManager *manager, const QString &jobId)
{
    if (!manager) { fail(QStringLiteral("No document manager")); return nullptr; }
    const auto record = m_jobs->job(jobId);
    if (record.value(QStringLiteral("state")) != QStringLiteral("succeeded")) {
        fail(QStringLiteral("Import job has not succeeded")); return nullptr;
    }
    const auto output = record.value(QStringLiteral("result")).toMap();
    if (output.value(QStringLiteral("operation")) != QStringLiteral("import")) {
        fail(QStringLiteral("Job is not an import")); return nullptr;
    }
    OperationError error;
    auto *session = manager->adoptBytes(output.value(QStringLiteral("documentBytes")).toByteArray(), &error);
    if (!session) { fail(error.message); return nullptr; }
    m_lastError.clear();
    emit changed();
    return session;
}
} // namespace QindaTK
