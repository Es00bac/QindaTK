// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Local byte storage for attachment metadata owned by LocalProductivityStore.
class LocalAttachmentStore : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString rootPath READ rootPath WRITE setRootPath NOTIFY rootPathChanged)
    Q_PROPERTY(QString defaultPath READ defaultPath CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY statusChanged)
    Q_PROPERTY(qint64 maximumBytes READ maximumBytes CONSTANT)
public:
    explicit LocalAttachmentStore(QObject *parent = nullptr);
    QString rootPath() const { return m_rootPath; }
    void setRootPath(const QString &path);
    QString defaultPath() const;
    QString status() const { return m_status; }
    QString lastError() const { return m_lastError; }
    qint64 maximumBytes() const;

    Q_INVOKABLE QVariantMap importFile(const QString &attachmentId, const QString &sourcePath);
    Q_INVOKABLE QVariantMap importBytes(const QString &attachmentId, const QString &fileName,
                                       const QString &mimeType, const QByteArray &bytes);
    Q_INVOKABLE QVariantMap read(const QString &attachmentId) const;
    Q_INVOKABLE QVariantMap remove(const QString &attachmentId);

signals:
    void rootPathChanged();
    void statusChanged();

private:
    QString effectiveRoot() const;
    QString directoryFor(const QString &attachmentId) const;
    QVariantMap result(bool ok, const QString &error = {}) const;
    void setStatus(const QString &status, const QString &error = {});
    static QString safeFileName(const QString &name);

    QString m_rootPath, m_status = QStringLiteral("ready"), m_lastError;
};

} // namespace QindaTK
