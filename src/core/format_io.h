// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "job_model.h"
#include <QUrl>
#include <QVariantList>

namespace QindaTK { struct FormatPlan; }
namespace QindaTK::FormatIO {
QVariantMap issue(const QString &code, const QString &severity,
                  const QString &fieldPath, const QString &message);
QVariantMap errorMap(const OperationError &error);
QVariantMap planMap(const QString &formatId, const QString &operation,
                    const FormatPlan &plan, const QVariantMap &extra = {});
OperationError localPath(const QUrl &url, QString *path);
OperationError readBounded(const QString &path, qsizetype maximum,
                           QByteArray *bytes, const CancellationToken *cancel = nullptr,
                           bool allowEmpty = false);
OperationError destinationCheck(const QString &path, const QVariantMap &options,
                                QByteArray *currentSha256 = nullptr);
OperationError writeAtomic(const QString &path, const QByteArray &bytes,
                           const QVariantMap &options, const CancellationToken &cancel,
                           const JobProgress &progress, QByteArray *sha256);
QByteArray hash(const QByteArray &bytes);
} // namespace QindaTK::FormatIO
