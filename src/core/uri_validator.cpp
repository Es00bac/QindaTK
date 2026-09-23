// SPDX-License-Identifier: LGPL-3.0-or-later
#include "uri_validator.h"

#include <QFileInfo>
#include <QTimer>
#include <QUrl>
#include <QUuid>

namespace QindaTK {
UriValidator::UriValidator(QObject *parent) : QObject(parent) {}
QVariantMap UriValidator::inspect(const QString &input, const QString &mode) const {
    if (mode != "open" && mode != "save" && mode != "folder")
        return {{"valid", false}, {"status", "error"}, {"message", "Unknown path mode"}};
    if (input.trimmed().isEmpty())
        return {{"valid", false}, {"status", "missing"}, {"message", "Location is empty"}};
    // A plain path is local; every explicit scheme remains a URI and is
    // serialized only by QUrl, never normalized via QFileInfo.
    const QUrl parsed(input, QUrl::StrictMode);
    const QUrl url = parsed.scheme().isEmpty() ? QUrl::fromLocalFile(input) : parsed;
    if (!url.isValid() || url.scheme().isEmpty())
        return {{"valid", false}, {"status", "error"}, {"message", "Invalid URI"}};
    const QString canonical = url.toString(QUrl::FullyEncoded);
    if (!url.isLocalFile())
        return {{"valid", true}, {"status", "unknown"}, {"uri", canonical},
                {"message", "Remote availability is not checked"}};
    const QFileInfo file(url.toLocalFile());
    if (mode == "save") {
        const QFileInfo destination = file.exists() ? file : QFileInfo(file.absolutePath());
        const bool valid = destination.exists() && (file.exists() ? file.isWritable() : destination.isDir() && destination.isWritable());
        return {{"valid", valid}, {"status", valid ? "ready" : "missing"},
                {"uri", canonical}, {"message", valid ? "" : "Save location is unavailable"}};
    }
    const bool valid = file.exists() && file.isReadable() && (mode != "folder" || file.isDir());
    return {{"valid", valid}, {"status", valid ? "ready" : "missing"},
            {"uri", canonical}, {"message", valid ? "" : "Location is unavailable"}};
}
QString UriValidator::request(const QString &input, const QString &mode) {
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_pending.insert(id);
    QTimer::singleShot(0, this, [this, id, input, mode] {
        if (!m_pending.remove(id)) return;
        emit finished(id, inspect(input, mode));
    });
    return id;
}
bool UriValidator::cancel(const QString &requestId) { return m_pending.remove(requestId); }
} // namespace QindaTK
