// SPDX-License-Identifier: LGPL-3.0-or-later
#include "canvas_document_format.h"
#include "format_io.h"
#include "format_registry.h"
#include <QSet>

namespace QindaTK {
namespace {
constexpr auto formatId = "qindatk.canvas.v1";

OperationError problem(ErrorCode code, const QString &message, const QString &field = {})
{
    return {code, message, field, QString::fromLatin1(formatId)};
}

bool allowedOptions(const QVariantMap &options)
{
    const QSet<QString> allowed{QStringLiteral("conflictPolicy"),
        QStringLiteral("expectedSha256"), QStringLiteral("expectedSourceSha256"),
        QStringLiteral("allowExtensionMismatch"), QStringLiteral("expectedSourceRevision"),
        QStringLiteral("expectedSnapshotSha256")};
    for (auto it = options.cbegin(); it != options.cend(); ++it)
        if (!allowed.contains(it.key())) return false;
    return true;
}

// Full validation of the bytes a job would hand to DocumentManager.
FormatPlan planFor(const QByteArray &bytes, const QVariantMap &options)
{
    FormatPlan plan;
    plan.options = options;
    if (!allowedOptions(options)) {
        plan.error = problem(ErrorCode::InvalidArgument, QStringLiteral("Unknown canvas format option"));
        return plan;
    }
    OperationError error;
    const auto state = CanvasDocumentFormat::decode(bytes, CanvasDocumentLimits{}, &error);
    if (!state) {
        plan.error = error;
        return plan;
    }
    plan.preview = {{"documentId", state->documentId.toString()},
                    {"objectCount", int(state->objects.size())},
                    {"layerCount", int(state->layers.size())},
                    {"imageCount", int(state->images.size())}};
    return plan;
}

FormatPayload cancelled(const QString &message)
{
    return {{}, {}, {}, problem(ErrorCode::Cancelled, message)};
}
} // namespace

QVariantMap CanvasNativeFormat::metadata() const
{
    return {{"formatId", QString::fromLatin1(formatId)},
            {"title", QStringLiteral("QindaTK canvas document")},
            {"mimeTypes", QStringList{QStringLiteral("application/vnd.qindatk.canvas")}},
            {"extensions", QStringList{QStringLiteral("qtkc")}},
            {"version", int(CanvasDocumentFormat::Version)}, // the binary version export writes
            {"operations", QStringList{QStringLiteral("import"), QStringLiteral("export"),
                                        QStringLiteral("roundtrip")}},
            {"editableImport", true}, {"renderOnly", false}, {"roundtrip", true},
            {"features", QStringList{QStringLiteral("spatial-objects"), QStringLiteral("tiled-images"),
                                      QStringLiteral("opaque-extensions")}},
            {"schemas", QStringList{QString::fromLatin1(CanvasDocumentFormat::SchemaId)}},
            {"maximumBytes", qlonglong(CanvasDocumentLimits{}.maximumEncodedBytes)},
            {"optionKeys", QStringList{QStringLiteral("conflictPolicy"), QStringLiteral("expectedSha256"),
                                        QStringLiteral("expectedSourceSha256"),
                                        QStringLiteral("expectedSourceRevision"),
                                        QStringLiteral("expectedSnapshotSha256")}}};
}

bool CanvasNativeFormat::recognizes(const QByteArray &prefix, const QUrl &) const
{
    // The signature alone decides; a .qtkc name never establishes support.
    return CanvasDocumentFormat::recognizes(prefix);
}

FormatPlan CanvasNativeFormat::preflightImport(const QByteArray &bytes, const QVariantMap &options) const
{
    return planFor(bytes, options);
}

FormatPayload CanvasNativeFormat::importDocument(const QByteArray &bytes, const QVariantMap &options,
                                                 const CancellationToken &cancel,
                                                 const JobProgress &progress) const
{
    if (cancel.isCancelled()) return cancelled(QStringLiteral("Import cancelled"));
    const auto plan = planFor(bytes, options);
    if (plan.error) return {{}, plan.warnings, plan.losses, plan.error};
    progress(0.6);
    if (cancel.isCancelled()) return cancelled(QStringLiteral("Import cancelled"));
    return {bytes, plan.warnings, {}, {}};
}

FormatPlan CanvasNativeFormat::preflightExport(const SnapshotPtr &snapshot, const QVariantMap &options) const
{
    if (!snapshot) {
        FormatPlan plan;
        plan.options = options;
        plan.error = problem(ErrorCode::InvalidArgument, QStringLiteral("Missing canvas document snapshot"));
        return plan;
    }
    if (snapshot->header.schemaId != QString::fromLatin1(CanvasDocumentFormat::SchemaId)) {
        FormatPlan plan;
        plan.options = options;
        plan.error = problem(ErrorCode::Unsupported, QStringLiteral("Canvas export supports canvas documents only"),
                             QStringLiteral("schemaId"));
        return plan;
    }
    return planFor(snapshot->payload, options);
}

FormatPayload CanvasNativeFormat::exportDocument(const SnapshotPtr &snapshot, const QVariantMap &options,
                                                 const CancellationToken &cancel,
                                                 const JobProgress &progress) const
{
    if (cancel.isCancelled()) return cancelled(QStringLiteral("Export cancelled"));
    const auto plan = preflightExport(snapshot, options);
    if (plan.error) return {{}, plan.warnings, plan.losses, plan.error};
    progress(0.6);
    return {snapshot->payload, plan.warnings, {}, {}};
}

} // namespace QindaTK
