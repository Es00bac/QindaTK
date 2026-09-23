// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QSet>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
// Queued URI inspection. Remote schemes stay opaque: only local file://
// locations are inspected. Callers match requestId and their draft revision.
class UriValidator : public QObject {
    Q_OBJECT
    QML_ELEMENT
public:
    explicit UriValidator(QObject *parent = nullptr);
    Q_INVOKABLE QString request(const QString &input, const QString &mode = QStringLiteral("open"));
    Q_INVOKABLE bool cancel(const QString &requestId);
    Q_INVOKABLE QVariantMap inspect(const QString &input, const QString &mode = QStringLiteral("open")) const;
signals:
    void finished(const QString &requestId, const QVariantMap &result);
private:
    QSet<QString> m_pending;
};
} // namespace QindaTK
