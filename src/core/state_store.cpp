// SPDX-License-Identifier: LGPL-3.0-or-later
#include "state_store.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace QindaTK {

StateReadResult AtomicStateStore::read(const QString &path, qsizetype maximumBytes)
{
    if (path.isEmpty() || maximumBytes < 1)
        return {{}, {ErrorCode::InvalidArgument, QStringLiteral("Invalid state path/limit"),
                     path, {}}};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {{}, {ErrorCode::Unavailable, file.errorString(), path,
                     QStringLiteral("state-file")}};
    if (file.size() < 1 || file.size() > maximumBytes)
        return {{}, {ErrorCode::Unsupported, QStringLiteral("State file exceeds limit"),
                     path, QStringLiteral("state-file")}};
    const QByteArray bytes = file.read(maximumBytes + 1);
    if (bytes.size() > maximumBytes)
        return {{}, {ErrorCode::Unsupported, QStringLiteral("State file exceeds limit"),
                     path, QStringLiteral("state-file")}};
    if (bytes.size() != file.size() || file.error() != QFileDevice::NoError)
        return {{}, {ErrorCode::BackendFailure, file.errorString(), path,
                     QStringLiteral("state-file")}};
    return {bytes, {}};
}

OperationError AtomicStateStore::write(const QString &path, const QByteArray &bytes)
{
    if (path.isEmpty() || bytes.isEmpty())
        return {ErrorCode::InvalidArgument, QStringLiteral("Invalid state write"), path, {}};
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath()))
        return {ErrorCode::BackendFailure, QStringLiteral("Cannot create state directory"),
                path, QStringLiteral("state-file")};
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()
        || !file.commit())
        return {ErrorCode::BackendFailure, file.errorString(), path,
                QStringLiteral("state-file")};
    return {};
}

} // namespace QindaTK
