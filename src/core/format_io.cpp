// SPDX-License-Identifier: LGPL-3.0-or-later
#include "format_registry.h"
#include "format_io.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <algorithm>

namespace QindaTK::FormatIO {
namespace {
OperationError error(ErrorCode code, const QString &message, const QString &path = {})
{
    return {code, message, path, QStringLiteral("format.io")};
}
}
QVariantMap issue(const QString &code, const QString &severity,
                  const QString &fieldPath, const QString &message)
{
    return {{"code", code}, {"severity", severity}, {"fieldPath", fieldPath},
            {"message", message}};
}
QVariantMap errorMap(const OperationError &value)
{
    return {{"code", int(value.code)}, {"message", value.message},
            {"fieldPath", value.fieldPath}, {"backendId", value.backendId}};
}
QVariantMap planMap(const QString &formatId, const QString &operation,
                    const FormatPlan &plan, const QVariantMap &extra)
{
    QVariantMap result{{"ok", !plan.error}, {"formatId", formatId},
                       {"operation", operation}, {"options", plan.options},
                       {"warnings", plan.warnings}, {"losses", plan.losses},
                       {"preview", plan.preview},
                       {"requiresAcknowledgement", !plan.losses.isEmpty()
                           && !plan.options.value(QStringLiteral("allowLossy")).toBool()},
                       {"error", errorMap(plan.error)}};
    for (auto it = extra.cbegin(); it != extra.cend(); ++it) result.insert(it.key(), it.value());
    return result;
}
QByteArray hash(const QByteArray &bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}
OperationError localPath(const QUrl &url, QString *path)
{
    if (!url.isValid() || !url.isLocalFile() || url.toLocalFile().isEmpty())
        return error(ErrorCode::Unsupported, QStringLiteral("Only local file URIs are supported"), url.toString());
    *path = url.toLocalFile();
    return {};
}
OperationError readBounded(const QString &path, qsizetype maximum,
                           QByteArray *bytes, const CancellationToken *cancel, bool allowEmpty)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return error(ErrorCode::BackendFailure, file.errorString(), path);
    const qint64 size = file.size();
    if ((size < 1 && !allowEmpty) || size > maximum)
        return error(ErrorCode::Validation, QStringLiteral("Source size is unsupported"), path);
    if (cancel && cancel->isCancelled())
        return error(ErrorCode::Cancelled, QStringLiteral("Import cancelled"), path);
    *bytes = file.readAll();
    if (bytes->size() != size || file.error() != QFileDevice::NoError)
        return error(ErrorCode::BackendFailure, file.errorString(), path);
    if (cancel && cancel->isCancelled())
        return error(ErrorCode::Cancelled, QStringLiteral("Import cancelled"), path);
    return {};
}
OperationError destinationCheck(const QString &path, const QVariantMap &options,
                                QByteArray *currentSha256)
{
    const QString policy = options.value(QStringLiteral("conflictPolicy"),
                                         QStringLiteral("fail")).toString();
    if (policy != QStringLiteral("fail") && policy != QStringLiteral("replaceIfHash"))
        return error(ErrorCode::InvalidArgument, QStringLiteral("Invalid conflict policy"), path);
    const QFileInfo info(path);
    if (info.isSymLink())
        return error(ErrorCode::Conflict, QStringLiteral("Symlink destinations are unsupported"), path);
    if (policy == QStringLiteral("fail")) {
        if (info.exists()) return error(ErrorCode::Conflict, QStringLiteral("Destination already exists"), path);
        return {};
    }
    const QByteArray baseline = options.value(QStringLiteral("expectedSha256")).toString().toLatin1();
    const bool validHex = baseline.size() == 64 && std::all_of(baseline.cbegin(), baseline.cend(),
        [](char value) { return (value >= '0' && value <= '9')
            || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F'); });
    if (!validHex)
        return error(ErrorCode::InvalidArgument, QStringLiteral("Replacement requires a SHA-256 baseline"), path);
    const QByteArray expected = QByteArray::fromHex(baseline);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return error(ErrorCode::Conflict, QStringLiteral("Destination disappeared or is unreadable"), path);
    QCryptographicHash digest(QCryptographicHash::Sha256);
    if (!digest.addData(&file))
        return error(ErrorCode::BackendFailure, QStringLiteral("Could not hash destination"), path);
    const QByteArray actual = digest.result();
    if (currentSha256) *currentSha256 = actual;
    if (actual != expected)
        return error(ErrorCode::Conflict, QStringLiteral("Destination changed since preflight"), path);
    return {};
}
OperationError writeAtomic(const QString &path, const QByteArray &bytes,
                           const QVariantMap &options, const CancellationToken &cancel,
                           const JobProgress &progress, QByteArray *sha256)
{
    if (cancel.isCancelled()) return error(ErrorCode::Cancelled, QStringLiteral("Export cancelled"), path);
    if (auto problem = destinationCheck(path, options)) return problem;
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return error(ErrorCode::BackendFailure, file.errorString(), path);
    constexpr qsizetype chunk = 256 * 1024;
    for (qsizetype offset = 0; offset < bytes.size(); offset += chunk) {
        if (cancel.isCancelled()) {
            file.cancelWriting();
            return error(ErrorCode::Cancelled, QStringLiteral("Export cancelled"), path);
        }
        const qsizetype count = qMin(chunk, bytes.size() - offset);
        if (file.write(bytes.constData() + offset, count) != count) {
            const auto problem = error(ErrorCode::BackendFailure, file.errorString(), path);
            file.cancelWriting();
            return problem;
        }
        progress(0.65 + 0.25 * (double(offset + count) / double(bytes.size())));
    }
    if (cancel.isCancelled()) { file.cancelWriting(); return error(ErrorCode::Cancelled, QStringLiteral("Export cancelled"), path); }
    if (auto problem = destinationCheck(path, options)) { file.cancelWriting(); return problem; }
    if (!file.commit()) return error(ErrorCode::BackendFailure, file.errorString(), path);
    if (sha256) *sha256 = hash(bytes);
    progress(1.0);
    return {};
}
} // namespace QindaTK::FormatIO
