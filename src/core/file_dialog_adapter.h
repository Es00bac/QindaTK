// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPointer>
#include <QHash>
#include <QUrl>
#include <QVariantMap>
#include <QWindow>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <memory>

namespace QindaTK {
struct FileDialogRequest {
    enum class Mode { Open, OpenMultiple, Save, Folder };
    Mode mode = Mode::Open;
    QStringList mimeTypes;
    QUrl initialUrl;
    QString title;
};
struct FileDialogResult {
    enum class Status { Accepted, Canceled, Error, Unsupported };
    Status status = Status::Unsupported;
    QList<QUrl> urls;
    QString message;
};
class FileDialogBackend {
public:
    virtual ~FileDialogBackend() = default;
    virtual bool available() const = 0;
    virtual bool supportsRemoteUrls() const = 0;
    virtual void start(const QString &requestId, const FileDialogRequest &request,
                       QWindow *parent, std::function<void(FileDialogResult)> completion) = 0;
    virtual void cancel(const QString &requestId) = 0;
};
using FileDialogBackendPtr = std::shared_ptr<FileDialogBackend>;

// Async URI-based dialog facade. A platform backend is injected; base QindaTK
// does not require Qt Widgets. An absent backend is an explicit result.
class FileDialogAdapter : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool supportsRemoteUrls READ supportsRemoteUrls NOTIFY changed)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
public:
    explicit FileDialogAdapter(QObject *parent = nullptr);
    void setBackend(FileDialogBackendPtr backend);
    bool available() const { return m_backend && m_backend->available(); }
    bool supportsRemoteUrls() const { return available() && m_backend->supportsRemoteUrls(); }
    int pendingCount() const { return m_pending.size(); }
    QString lastError() const { return m_lastError; }
    Q_INVOKABLE QString request(const QString &mode, const QStringList &mimeTypes = {},
                                const QUrl &initialUrl = {}, const QString &title = {},
                                QWindow *parentWindow = nullptr);
    Q_INVOKABLE bool cancel(const QString &requestId);
    Q_INVOKABLE QVariantMap capabilities() const;
signals:
    void changed();
    void finished(const QString &requestId, const QVariantMap &result);
private:
    void complete(const QString &requestId, FileDialogResult result);
    static QVariantMap resultMap(const FileDialogResult &result);
    FileDialogBackendPtr m_backend;
    QHash<QString, FileDialogRequest::Mode> m_pending;
    QString m_lastError;
};
} // namespace QindaTK
