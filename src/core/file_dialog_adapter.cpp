// SPDX-License-Identifier: LGPL-3.0-or-later
#include "file_dialog_adapter.h"
#include <QFileInfo>
#include <QDir>
#include <QUuid>

namespace QindaTK {
FileDialogAdapter::FileDialogAdapter(QObject *parent) : QObject(parent) {}
void FileDialogAdapter::setBackend(FileDialogBackendPtr backend)
{
    if (!m_pending.isEmpty()) return; // Never strand live requests on backend replacement.
    m_backend = std::move(backend);
    emit changed();
}
QVariantMap FileDialogAdapter::resultMap(const FileDialogResult &result)
{
    QString status;
    switch (result.status) {
    case FileDialogResult::Status::Accepted: status = QStringLiteral("accepted"); break;
    case FileDialogResult::Status::Canceled: status = QStringLiteral("canceled"); break;
    case FileDialogResult::Status::Error: status = QStringLiteral("error"); break;
    case FileDialogResult::Status::Unsupported: status = QStringLiteral("unsupported"); break;
    }
    QVariantList urls;
    QStringList encodedUrls;
    for (const auto &url : result.urls) {
        urls << url;
        encodedUrls << url.toString(QUrl::FullyEncoded);
    }
    return {{"status", status}, {"urls", urls}, {"encodedUrls", encodedUrls},
            {"message", result.message}};
}
QString FileDialogAdapter::request(const QString &mode, const QStringList &mimeTypes,
                                   const QUrl &initialUrl, const QString &title,
                                   QWindow *parentWindow)
{
    FileDialogRequest request;
    if (mode == QStringLiteral("open")) request.mode = FileDialogRequest::Mode::Open;
    else if (mode == QStringLiteral("openMultiple")) request.mode = FileDialogRequest::Mode::OpenMultiple;
    else if (mode == QStringLiteral("save")) request.mode = FileDialogRequest::Mode::Save;
    else if (mode == QStringLiteral("folder")) request.mode = FileDialogRequest::Mode::Folder;
    else { m_lastError = QStringLiteral("Unknown dialog mode"); emit changed(); return {}; }
    request.mimeTypes = mimeTypes;
    request.initialUrl = initialUrl;
    request.title = title;
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!available()) {
        m_lastError = QStringLiteral("No native file dialog backend is installed");
        emit changed();
        emit finished(id, resultMap({FileDialogResult::Status::Unsupported, {}, m_lastError}));
        return id;
    }
    if (initialUrl.isValid() && !initialUrl.isEmpty() && !initialUrl.isLocalFile()
        && !supportsRemoteUrls()) {
        m_lastError = QStringLiteral("Remote locations are unavailable");
        emit changed();
        emit finished(id, resultMap({FileDialogResult::Status::Unsupported, {}, m_lastError}));
        return id;
    }
    m_pending.insert(id, request.mode);
    m_lastError.clear();
    emit changed();
    const QPointer<FileDialogAdapter> self(this);
    m_backend->start(id, request, parentWindow, [self, id](FileDialogResult result) {
        if (self) self->complete(id, std::move(result));
    });
    return id;
}
void FileDialogAdapter::complete(const QString &requestId, FileDialogResult result)
{
    if (!m_pending.contains(requestId)) return; // Late completion after cancellation.
    const auto mode = m_pending.take(requestId);
    if (result.status == FileDialogResult::Status::Accepted) {
        if (result.urls.isEmpty()) {
            result = {FileDialogResult::Status::Error, {}, QStringLiteral("No file was selected")};
        } else {
            for (const QUrl &url : result.urls) {
                if (!url.isValid() || url.scheme().isEmpty()) {
                    result = {FileDialogResult::Status::Error, {}, QStringLiteral("Invalid selected URI")};
                    break;
                }
                if (!url.isLocalFile() && !supportsRemoteUrls()) {
                    result = {FileDialogResult::Status::Unsupported, {},
                              QStringLiteral("Selected remote location is unavailable")};
                    break;
                }
                if (url.isLocalFile()) {
                    const QFileInfo info(url.toLocalFile());
                    if (!info.exists() && mode != FileDialogRequest::Mode::Save) {
                        result = {FileDialogResult::Status::Error, {},
                                  QStringLiteral("Selected location is unavailable")};
                        break;
                    }
                    if (mode == FileDialogRequest::Mode::Folder && !info.isDir()) {
                        result = {FileDialogResult::Status::Error, {},
                                  QStringLiteral("Selected location is not a folder")};
                        break;
                    }
                    if (mode == FileDialogRequest::Mode::Save && !info.exists()) {
                        const QFileInfo parent(info.absolutePath());
                        if (!parent.isDir() || !parent.isWritable()) {
                            result = {FileDialogResult::Status::Error, {},
                                      QStringLiteral("Save location is unavailable")};
                            break;
                        }
                        continue;
                    }
                    if (!info.isReadable()) {
                        result = {FileDialogResult::Status::Error, {},
                                  QStringLiteral("Selected location is not readable")};
                        break;
                    }
                }
            }
        }
    }
    m_lastError = result.status == FileDialogResult::Status::Error
                       || result.status == FileDialogResult::Status::Unsupported
                      ? result.message : QString();
    emit changed();
    emit finished(requestId, resultMap(result));
}
bool FileDialogAdapter::cancel(const QString &requestId)
{
    if (!m_pending.contains(requestId)) return false;
    m_pending.remove(requestId);
    if (m_backend) m_backend->cancel(requestId);
    emit changed();
    emit finished(requestId, resultMap({FileDialogResult::Status::Canceled, {}, {}}));
    return true;
}
QVariantMap FileDialogAdapter::capabilities() const
{
    return {{"nativeDialog", available()}, {"localUrls", available()},
            {"remoteUrls", supportsRemoteUrls()}, {"portalGrants", false}};
}
} // namespace QindaTK
