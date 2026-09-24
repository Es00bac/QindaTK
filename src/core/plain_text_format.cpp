// SPDX-License-Identifier: LGPL-3.0-or-later
#include "format_registry.h"
#include "format_io.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QStringConverter>

namespace QindaTK {
namespace {
constexpr auto formatId = "text.utf8";
OperationError invalid(const QString &message, const QString &field = {})
{
    return {ErrorCode::Validation, message, field, QString::fromLatin1(formatId)};
}
bool allowedOptions(const QVariantMap &options, bool importing)
{
    QSet<QString> allowed{QStringLiteral("expectedSourceSha256")};
    if (!importing) allowed.unite({QStringLiteral("conflictPolicy"),
        QStringLiteral("expectedSha256"), QStringLiteral("allowExtensionMismatch"),
        QStringLiteral("lineEnding"), QStringLiteral("bom"),
        QStringLiteral("allowLossy"), QStringLiteral("entityId"),
        QStringLiteral("expectedSourceRevision"),
        QStringLiteral("expectedSnapshotSha256")});
    for (auto it = options.cbegin(); it != options.cend(); ++it)
        if (!allowed.contains(it.key())) return false;
    return true;
}
std::optional<QString> decodeText(const QByteArray &bytes)
{
    QByteArrayView view(bytes);
    if (view.startsWith("\xef\xbb\xbf")) view = view.sliced(3);
    if (view.contains('\0')) return std::nullopt;
    QStringDecoder decoder(QStringConverter::Utf8);
    const QString decoded = decoder(view);
    if (decoder.hasError()) return std::nullopt;
    return decoded;
}
struct TextSelection {
    QString text;
    QString entityId;
    QVariantList warnings;
    QVariantList losses;
    OperationError error;
};
TextSelection selectText(const SnapshotPtr &snapshot, const QVariantMap &options)
{
    TextSelection result;
    if (!snapshot) { result.error = invalid(QStringLiteral("Missing document snapshot")); return result; }
    if (snapshot->header.schemaId != QString::fromLatin1(PropertyDocument::SchemaId)) {
        result.error = {ErrorCode::Unsupported, QStringLiteral("Text export supports property documents only"),
                        QStringLiteral("schemaId"), QString::fromLatin1(formatId)};
        return result;
    }
    const QJsonObject root = QJsonDocument::fromJson(snapshot->payload).object();
    const QJsonArray entities = root.value(QStringLiteral("entities")).toArray();
    const QJsonArray properties = root.value(QStringLiteral("properties")).toArray();
    if (entities.isEmpty()) { result.error = invalid(QStringLiteral("Document has no text entity")); return result; }
    bool hasText = false;
    for (const auto &property : properties) {
        const auto entry = property.toObject();
        if (entry.value(QStringLiteral("id")).toString() == QStringLiteral("text")
            && entry.value(QStringLiteral("type")).toString() == QStringLiteral("string")) hasText = true;
        else result.losses << FormatIO::issue(QStringLiteral("propertyDropped"), QStringLiteral("loss"),
            QStringLiteral("properties.%1").arg(entry.value(QStringLiteral("id")).toString()),
            QStringLiteral("Plain text cannot preserve this property"));
    }
    if (!hasText) { result.error = invalid(QStringLiteral("Document has no string text property")); return result; }
    const QString requestedId = options.value(QStringLiteral("entityId")).toString();
    if (entities.size() > 1 && requestedId.isEmpty()) {
        result.error = invalid(QStringLiteral("Select an entity for plain text export"),
                               QStringLiteral("entityId"));
        return result;
    }
    QJsonObject selected;
    for (const auto &entry : entities) {
        const auto item = entry.toObject();
        if (requestedId.isEmpty() || item.value(QStringLiteral("id")).toString() == requestedId) {
            selected = item;
            break;
        }
    }
    if (selected.isEmpty()) {
        result.error = invalid(QStringLiteral("Selected text entity is unavailable"),
                               QStringLiteral("entityId"));
        return result;
    }
    result.entityId = selected.value(QStringLiteral("id")).toString();
    const QJsonObject value = selected.value(QStringLiteral("values")).toObject()
                                      .value(QStringLiteral("text")).toObject();
    if (value.value(QStringLiteral("kind")).toString() != QStringLiteral("string")
        || !value.value(QStringLiteral("value")).isString()) {
        result.error = invalid(QStringLiteral("Text entity is malformed")); return result;
    }
    result.text = value.value(QStringLiteral("value")).toString();
    if (entities.size() > 1)
        result.losses << FormatIO::issue(QStringLiteral("entitiesDropped"), QStringLiteral("loss"),
            QStringLiteral("entities"), QStringLiteral("Other entities are not exported"));
    if (!root.value(QStringLiteral("extensions")).toObject().isEmpty())
        result.losses << FormatIO::issue(QStringLiteral("extensionsDropped"), QStringLiteral("loss"),
            QStringLiteral("extensions"), QStringLiteral("Native extensions are not exported"));
    if (result.text.contains(QLatin1Char('\r')))
        result.warnings << FormatIO::issue(QStringLiteral("lineEndingsNormalized"),
            QStringLiteral("warning"), QStringLiteral("text"),
            QStringLiteral("Line endings are normalized for plain text output"));
    return result;
}
}
QVariantMap PlainTextFormat::metadata() const
{
    return {{"formatId", QString::fromLatin1(formatId)}, {"title", QStringLiteral("UTF-8 plain text")},
            {"mimeTypes", QStringList{QStringLiteral("text/plain")}},
            {"extensions", QStringList{QStringLiteral("txt")}}, {"version", 1},
            {"operations", QStringList{QStringLiteral("import"), QStringLiteral("export")}},
            {"editableImport", true}, {"renderOnly", false}, {"roundtrip", false},
            {"features", QStringList{QStringLiteral("single-text-entity"), QStringLiteral("utf8")}},
            {"schemas", QStringList{QString::fromLatin1(PropertyDocument::SchemaId)}},
            {"maximumBytes", 16 * 1024 * 1024},
            {"optionKeys", QStringList{QStringLiteral("lineEnding"), QStringLiteral("bom"),
                                        QStringLiteral("allowLossy"), QStringLiteral("entityId"),
                                        QStringLiteral("conflictPolicy"), QStringLiteral("expectedSha256"),
                                        QStringLiteral("expectedSourceRevision"),
                                        QStringLiteral("expectedSourceSha256"),
                                        QStringLiteral("expectedSnapshotSha256")}}};
}
bool PlainTextFormat::recognizes(const QByteArray &prefix, const QUrl &url) const
{
    return url.path().endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive)
        && decodeText(prefix).has_value();
}
FormatPlan PlainTextFormat::preflightImport(const QByteArray &bytes,
                                            const QVariantMap &options) const
{
    FormatPlan plan;
    plan.options = options;
    if (!allowedOptions(options, true)) { plan.error = invalid(QStringLiteral("Unknown text import option")); return plan; }
    const auto decoded = decodeText(bytes);
    if (!decoded) { plan.error = invalid(QStringLiteral("Source is not valid UTF-8 text")); return plan; }
    plan.preview = {{"excerpt", decoded->left(500)}, {"characterCount", decoded->size()},
                    {"lineCount", decoded->count(QLatin1Char('\n')) + 1}};
    if (bytes.startsWith("\xef\xbb\xbf"))
        plan.warnings << FormatIO::issue(QStringLiteral("utf8Bom"), QStringLiteral("info"),
            QStringLiteral("source"), QStringLiteral("UTF-8 byte order mark will be removed"));
    return plan;
}
FormatPayload PlainTextFormat::importDocument(const QByteArray &bytes,
                                               const QVariantMap &options,
                                               const CancellationToken &cancel,
                                               const JobProgress &progress) const
{
    if (cancel.isCancelled()) return {{}, {}, {}, {ErrorCode::Cancelled, QStringLiteral("Import cancelled"), {}, {}}};
    const auto plan = preflightImport(bytes, options);
    if (plan.error) return {{}, plan.warnings, {}, plan.error};
    const QString text = *decodeText(bytes);
    progress(0.45);
    PropertyDocument document;
    if (!document.defineProperty({QStringLiteral("text"), QMetaType::fromType<QString>(),
                                  QString(), {}, true, false})
        || !document.addEntity(EntityId::create(), {{QStringLiteral("text"), text}}))
        return {{}, {}, {}, invalid(QStringLiteral("Could not create text document"))};
    OperationError codecError;
    auto snapshot = ReferenceDocumentCodec::capture(document, {}, &codecError);
    if (!snapshot) return {{}, {}, {}, codecError};
    if (cancel.isCancelled()) return {{}, {}, {}, {ErrorCode::Cancelled, QStringLiteral("Import cancelled"), {}, {}}};
    progress(0.8);
    return {snapshot->payload, plan.warnings, {}, {}};
}
FormatPlan PlainTextFormat::preflightExport(const SnapshotPtr &snapshot,
                                            const QVariantMap &options) const
{
    FormatPlan plan;
    plan.options = options;
    if (!allowedOptions(options, false)) { plan.error = invalid(QStringLiteral("Unknown text export option")); return plan; }
    const QString lineEnding = options.value(QStringLiteral("lineEnding"), QStringLiteral("lf")).toString();
    if (lineEnding != QStringLiteral("lf") && lineEnding != QStringLiteral("crlf")) {
        plan.error = invalid(QStringLiteral("Invalid line ending option"), QStringLiteral("lineEnding"));
        return plan;
    }
    if (options.contains(QStringLiteral("bom"))
        && options.value(QStringLiteral("bom")).metaType() != QMetaType::fromType<bool>()) {
        plan.error = invalid(QStringLiteral("BOM option must be boolean"), QStringLiteral("bom"));
        return plan;
    }
    const auto selection = selectText(snapshot, options);
    plan.warnings = selection.warnings;
    plan.losses = selection.losses;
    plan.error = selection.error;
    if (!selection.error)
        plan.preview = {{"excerpt", selection.text.left(500)},
                        {"characterCount", selection.text.size()},
                        {"entityId", selection.entityId}};
    if (!plan.error && !plan.losses.isEmpty() && !options.value(QStringLiteral("allowLossy")).toBool())
        plan.error = {ErrorCode::Unsupported, QStringLiteral("Lossy text export requires acknowledgement"),
                      QStringLiteral("allowLossy"), QString::fromLatin1(formatId)};
    return plan;
}
FormatPayload PlainTextFormat::exportDocument(const SnapshotPtr &snapshot,
                                               const QVariantMap &options,
                                               const CancellationToken &cancel,
                                               const JobProgress &progress) const
{
    if (cancel.isCancelled()) return {{}, {}, {}, {ErrorCode::Cancelled, QStringLiteral("Export cancelled"), {}, {}}};
    const auto plan = preflightExport(snapshot, options);
    if (plan.error) return {{}, plan.warnings, plan.losses, plan.error};
    QString text = selectText(snapshot, options).text;
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    if (options.value(QStringLiteral("lineEnding"), QStringLiteral("lf")) == QStringLiteral("crlf"))
        text.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
    QByteArray output = text.toUtf8();
    if (options.value(QStringLiteral("bom")).toBool()) output.prepend("\xef\xbb\xbf");
    progress(0.6);
    return {output, plan.warnings, plan.losses, {}};
}
} // namespace QindaTK
