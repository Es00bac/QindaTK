// SPDX-License-Identifier: LGPL-3.0-or-later
#include "format_registry.h"
#include "format_io.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

namespace QindaTK {
namespace {
constexpr auto formatId = "qindatk.reference.v2";
OperationError invalid(const QString &message, const QString &field = {})
{
    return {ErrorCode::InvalidArgument, message, field, QString::fromLatin1(formatId)};
}
bool allowedOptions(const QVariantMap &options)
{
    const QSet<QString> allowed{QStringLiteral("conflictPolicy"),
        QStringLiteral("expectedSha256"), QStringLiteral("expectedSourceSha256"),
        QStringLiteral("allowExtensionMismatch"),
        QStringLiteral("expectedSourceRevision"),
        QStringLiteral("expectedSnapshotSha256")};
    for (auto it = options.cbegin(); it != options.cend(); ++it)
        if (!allowed.contains(it.key())) return false;
    return true;
}
QVariantList missingResources(const QByteArray &bytes)
{
    QVariantList warnings;
    const QJsonObject root = QJsonDocument::fromJson(bytes).object();
    const QJsonArray resources = root.value(QStringLiteral("extensions")).toObject()
                                     .value(QStringLiteral("resources")).toArray();
    for (const auto &item : resources) {
        const QString uri = item.toObject().value(QStringLiteral("uri")).toString();
        const QUrl url(uri);
        if (url.isLocalFile() && !QFileInfo::exists(url.toLocalFile()))
            warnings << FormatIO::issue(QStringLiteral("missingLinkedResource"),
                QStringLiteral("warning"), QStringLiteral("extensions.resources"),
                QStringLiteral("Linked resource is missing: %1").arg(uri));
    }
    return warnings;
}
}
QVariantMap NativeReferenceFormat::metadata() const
{
    return {{"formatId", QString::fromLatin1(formatId)}, {"title", QStringLiteral("QindaTK reference document")},
            {"mimeTypes", QStringList{QStringLiteral("application/vnd.qindatk.reference+json")}},
            {"extensions", QStringList{QStringLiteral("qtk")}}, {"version", 2},
            {"operations", QStringList{QStringLiteral("import"), QStringLiteral("export"),
                                        QStringLiteral("roundtrip")}},
            {"editableImport", true}, {"renderOnly", false}, {"roundtrip", true},
            {"features", QStringList{QStringLiteral("reference-properties"),
                                      QStringLiteral("opaque-extensions")}},
            {"schemas", QStringList{QString::fromLatin1(PropertyDocument::SchemaId)}},
            {"optionKeys", QStringList{QStringLiteral("conflictPolicy"),
                                        QStringLiteral("expectedSha256"),
                                        QStringLiteral("expectedSourceSha256"),
                                        QStringLiteral("expectedSourceRevision"),
                                        QStringLiteral("expectedSnapshotSha256")}}};
}
bool NativeReferenceFormat::recognizes(const QByteArray &prefix, const QUrl &url) const
{
    return prefix.contains("qindatk.reference-property-document")
        || url.path().endsWith(QStringLiteral(".qtk"), Qt::CaseInsensitive);
}
FormatPlan NativeReferenceFormat::preflightImport(const QByteArray &bytes,
                                                   const QVariantMap &options) const
{
    FormatPlan plan;
    plan.options = options;
    if (!allowedOptions(options)) { plan.error = invalid(QStringLiteral("Unknown native format option")); return plan; }
    OperationError decodeError;
    if (!ReferenceDocumentCodec::decode(bytes, &decodeError)) {
        plan.error = decodeError;
        return plan;
    }
    plan.warnings = missingResources(bytes);
    const QJsonObject root = QJsonDocument::fromJson(bytes).object();
    plan.preview = {{"documentId", root.value(QStringLiteral("documentId")).toString()},
                    {"entityCount", root.value(QStringLiteral("entities")).toArray().size()},
                    {"propertyCount", root.value(QStringLiteral("properties")).toArray().size()}};
    return plan;
}
FormatPayload NativeReferenceFormat::importDocument(const QByteArray &bytes,
                                                     const QVariantMap &options,
                                                     const CancellationToken &cancel,
                                                     const JobProgress &progress) const
{
    if (cancel.isCancelled()) return {{}, {}, {}, {ErrorCode::Cancelled, QStringLiteral("Import cancelled"), {}, {}}};
    const auto plan = preflightImport(bytes, options);
    if (plan.error) return {{}, plan.warnings, plan.losses, plan.error};
    progress(0.6);
    if (cancel.isCancelled()) return {{}, {}, {}, {ErrorCode::Cancelled, QStringLiteral("Import cancelled"), {}, {}}};
    return {bytes, plan.warnings, {}, {}};
}
FormatPlan NativeReferenceFormat::preflightExport(const SnapshotPtr &snapshot,
                                                   const QVariantMap &options) const
{
    FormatPlan plan;
    plan.options = options;
    if (!allowedOptions(options)) { plan.error = invalid(QStringLiteral("Unknown native format option")); return plan; }
    if (snapshot && snapshot->header.schemaId != QString::fromLatin1(PropertyDocument::SchemaId)) {
        plan.error = {ErrorCode::Unsupported, QStringLiteral("Reference export supports property documents only"),
                      QStringLiteral("schemaId"), QString::fromLatin1(formatId)};
        return plan;
    }
    OperationError decodeError;
    if (!snapshot || !ReferenceDocumentCodec::decode(snapshot->payload, &decodeError)) {
        plan.error = decodeError ? decodeError : invalid(QStringLiteral("Missing native document snapshot"));
        return plan;
    }
    plan.warnings = missingResources(snapshot->payload);
    const QJsonObject root = QJsonDocument::fromJson(snapshot->payload).object();
    plan.preview = {{"documentId", root.value(QStringLiteral("documentId")).toString()},
                    {"entityCount", root.value(QStringLiteral("entities")).toArray().size()},
                    {"propertyCount", root.value(QStringLiteral("properties")).toArray().size()}};
    return plan;
}
FormatPayload NativeReferenceFormat::exportDocument(const SnapshotPtr &snapshot,
                                                     const QVariantMap &options,
                                                     const CancellationToken &cancel,
                                                     const JobProgress &progress) const
{
    if (cancel.isCancelled()) return {{}, {}, {}, {ErrorCode::Cancelled, QStringLiteral("Export cancelled"), {}, {}}};
    const auto plan = preflightExport(snapshot, options);
    if (plan.error) return {{}, plan.warnings, plan.losses, plan.error};
    progress(0.6);
    return {snapshot->payload, plan.warnings, {}, {}};
}
} // namespace QindaTK
