// SPDX-License-Identifier: LGPL-3.0-or-later
#include "rich_text_native_format.h"
#include "format_io.h"
#include "rich_text_codec.h"
#include "rich_text_session_adapter.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace QindaTK {
namespace {
const QString FormatId = QStringLiteral("qindatk.richtext.v1");
const QString ExtensionKey = RichTextSessionAdapter::defaultExtensionKey();

OperationError failure(ErrorCode code, const QString &message, const QString &field = {})
{ return {code, message, field, FormatId}; }

class RichTextNativeFormat final : public FormatProvider {
public:
    QVariantMap metadata() const override
    {
        return {{QStringLiteral("formatId"), FormatId},
            {QStringLiteral("title"), QStringLiteral("QindaTK rich text document")},
            {QStringLiteral("mimeTypes"), QStringList{QStringLiteral("application/vnd.qindatk.richtext+json")}},
            {QStringLiteral("extensions"), QStringList{QStringLiteral("qtkrich")}},
            {QStringLiteral("version"), RichTextCodec::CurrentVersion},
            {QStringLiteral("operations"), QStringList{QStringLiteral("import"),
                QStringLiteral("export"), QStringLiteral("roundtrip")}},
            {QStringLiteral("editableImport"), true}, {QStringLiteral("renderOnly"), false},
            {QStringLiteral("roundtrip"), true},
            {QStringLiteral("features"), QStringList{QStringLiteral("rich-text"),
                QStringLiteral("pagination"), QStringLiteral("native-objects")}},
            {QStringLiteral("schemas"), QStringList{QString::fromLatin1(PropertyDocument::SchemaId)}},
            {QStringLiteral("maximumBytes"), RichTextCodec::MaximumBytes},
            {QStringLiteral("optionKeys"), QStringList{QStringLiteral("conflictPolicy"),
                QStringLiteral("expectedSha256"), QStringLiteral("expectedSourceSha256"),
                QStringLiteral("expectedSourceRevision"), QStringLiteral("expectedSnapshotSha256"),
                QStringLiteral("allowExtensionMismatch"), QStringLiteral("allowLossy")}}};
    }

    bool recognizes(const QByteArray &prefix, const QUrl &url) const override
    {
        if (!url.path().endsWith(QStringLiteral(".qtkrich"), Qt::CaseInsensitive)
            || !prefix.trimmed().startsWith('{')) return false;
        // Large documents may place the schema after the 4096-byte detection
        // prefix. Preflight always validates the complete file before import.
        const QJsonDocument parsed = QJsonDocument::fromJson(prefix);
        return parsed.isObject()
            ? parsed.object().value(QStringLiteral("schema")).toString() == RichTextCodec::schemaId()
            : prefix.contains("\"blocks\"");
    }

    FormatPlan preflightImport(const QByteArray &bytes, const QVariantMap &options) const override
    {
        FormatPlan plan;
        plan.options = options;
        RichTextDocument document;
        QStringList warnings;
        if (!RichTextCodec::decode(bytes, document, &plan.error, &warnings)) return plan;
        for (const QString &message : warnings)
            plan.warnings << FormatIO::issue(QStringLiteral("unknownField"), QStringLiteral("warning"),
                                               {}, message);
        plan.preview = {{QStringLiteral("documentId"), document.documentIdString()},
            {QStringLiteral("paragraphCount"), document.paragraphCount()},
            {QStringLiteral("characterCount"), document.characterCount()}};
        return plan;
    }

    FormatPayload importDocument(const QByteArray &bytes, const QVariantMap &options,
                                 const CancellationToken &cancel,
                                 const JobProgress &progress) const override
    {
        if (cancel.isCancelled()) return {{}, {}, {}, failure(ErrorCode::Cancelled,
            QStringLiteral("Rich text import cancelled"))};
        const FormatPlan plan = preflightImport(bytes, options);
        if (plan.error) return {{}, plan.warnings, plan.losses, plan.error};
        RichTextDocument rich;
        OperationError error;
        if (!RichTextCodec::decode(bytes, rich, &error)) return {{}, {}, {}, error};
        if (progress) progress(0.5);
        PropertyDocument wrapper;
        const auto snapshot = ReferenceDocumentCodec::capture(wrapper,
            QJsonObject{{ExtensionKey, RichTextCodec::toJsonObject(rich)}}, &error);
        if (!snapshot) return {{}, {}, {}, error};
        if (cancel.isCancelled()) return {{}, {}, {}, failure(ErrorCode::Cancelled,
            QStringLiteral("Rich text import cancelled"))};
        if (progress) progress(1.0);
        return {snapshot->payload, plan.warnings, {}, {}};
    }

    FormatPlan preflightExport(const SnapshotPtr &snapshot,
                               const QVariantMap &options) const override
    {
        FormatPlan plan;
        plan.options = options;
        if (!snapshot || snapshot->header.schemaId != QString::fromLatin1(PropertyDocument::SchemaId)) {
            plan.error = failure(ErrorCode::Unsupported,
                QStringLiteral("Rich text export requires a rich-text reference session"),
                QStringLiteral("schemaId"));
            return plan;
        }
        OperationError error;
        auto decoded = ReferenceDocumentCodec::decode(snapshot->payload, &error);
        if (!decoded) { plan.error = error; return plan; }
        const QJsonObject richJson = decoded->extensions.value(ExtensionKey).toObject();
        if (richJson.isEmpty()) {
            plan.error = failure(ErrorCode::Unsupported,
                QStringLiteral("Session has no rich-text document"), ExtensionKey);
            return plan;
        }
        RichTextDocument rich;
        QStringList warnings;
        if (!RichTextCodec::fromJsonObject(richJson, rich, &plan.error, &warnings)) return plan;
        for (const QString &message : warnings)
            plan.warnings << FormatIO::issue(QStringLiteral("unknownField"), QStringLiteral("warning"),
                                               {}, message);
        if (!decoded->extensions.keys().isEmpty() && decoded->extensions.size() > 1)
            plan.losses << FormatIO::issue(QStringLiteral("extensionsDropped"), QStringLiteral("loss"),
                QStringLiteral("extensions"), QStringLiteral("Other session extensions are not in .qtkrich"));
        const QJsonObject root = QJsonDocument::fromJson(snapshot->payload).object();
        if (!root.value(QStringLiteral("entities")).toArray().isEmpty()
            || !root.value(QStringLiteral("properties")).toArray().isEmpty())
            plan.losses << FormatIO::issue(QStringLiteral("propertiesDropped"), QStringLiteral("loss"),
                QStringLiteral("properties"), QStringLiteral("Reference properties are not in .qtkrich"));
        if (!plan.losses.isEmpty() && !options.value(QStringLiteral("allowLossy")).toBool()) {
            plan.error = failure(ErrorCode::Validation,
                QStringLiteral("Confirm loss of non-rich-text session data"),
                QStringLiteral("allowLossy"));
            return plan;
        }
        plan.preview = {{QStringLiteral("documentId"), rich.documentIdString()},
            {QStringLiteral("paragraphCount"), rich.paragraphCount()},
            {QStringLiteral("characterCount"), rich.characterCount()}};
        return plan;
    }

    FormatPayload exportDocument(const SnapshotPtr &snapshot, const QVariantMap &options,
                                 const CancellationToken &cancel,
                                 const JobProgress &progress) const override
    {
        if (cancel.isCancelled()) return {{}, {}, {}, failure(ErrorCode::Cancelled,
            QStringLiteral("Rich text export cancelled"))};
        const FormatPlan plan = preflightExport(snapshot, options);
        if (plan.error) return {{}, plan.warnings, plan.losses, plan.error};
        const QJsonObject root = QJsonDocument::fromJson(snapshot->payload).object();
        RichTextDocument rich;
        OperationError error;
        if (!RichTextCodec::fromJsonObject(root.value(QStringLiteral("extensions")).toObject()
                                           .value(ExtensionKey).toObject(), rich, &error))
            return {{}, plan.warnings, plan.losses, error};
        if (progress) progress(0.5);
        const QByteArray bytes = RichTextCodec::encode(rich);
        if (cancel.isCancelled()) return {{}, {}, {}, failure(ErrorCode::Cancelled,
            QStringLiteral("Rich text export cancelled"))};
        if (progress) progress(1.0);
        return {bytes, plan.warnings, plan.losses, {}};
    }
};
} // namespace

FormatProviderPtr makeRichTextNativeFormat()
{ return std::make_shared<RichTextNativeFormat>(); }
} // namespace QindaTK
