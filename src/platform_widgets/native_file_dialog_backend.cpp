// SPDX-License-Identifier: LGPL-3.0-or-later
#include "native_file_dialog_backend.h"
#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>

namespace QindaTK {
NativeFileDialogBackend::~NativeFileDialogBackend()
{
    for (auto dialog : std::as_const(m_dialogs)) {
        if (!dialog) continue;
        QObject::disconnect(dialog, nullptr, dialog, nullptr);
        dialog->deleteLater();
    }
}
bool NativeFileDialogBackend::available() const
{
    return qobject_cast<QApplication *>(QCoreApplication::instance()) != nullptr;
}
void NativeFileDialogBackend::start(const QString &requestId, const FileDialogRequest &request,
                                    QWindow *parent, std::function<void(FileDialogResult)> completion)
{
    if (!available()) {
        completion({FileDialogResult::Status::Unsupported, {},
                    QStringLiteral("Native dialogs require QApplication")});
        return;
    }
    auto *dialog = new QFileDialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose, false);
    dialog->setWindowTitle(request.title);
    dialog->setMimeTypeFilters(request.mimeTypes);
    if (request.initialUrl.isValid()) dialog->setDirectoryUrl(request.initialUrl);
    switch (request.mode) {
    case FileDialogRequest::Mode::Open:
        dialog->setAcceptMode(QFileDialog::AcceptOpen);
        dialog->setFileMode(QFileDialog::ExistingFile);
        break;
    case FileDialogRequest::Mode::OpenMultiple:
        dialog->setAcceptMode(QFileDialog::AcceptOpen);
        dialog->setFileMode(QFileDialog::ExistingFiles);
        break;
    case FileDialogRequest::Mode::Save:
        dialog->setAcceptMode(QFileDialog::AcceptSave);
        dialog->setFileMode(QFileDialog::AnyFile);
        break;
    case FileDialogRequest::Mode::Folder:
        dialog->setAcceptMode(QFileDialog::AcceptOpen);
        dialog->setFileMode(QFileDialog::Directory);
        break;
    }
    if (parent) {
        dialog->winId();
        if (dialog->windowHandle()) dialog->windowHandle()->setTransientParent(parent);
    }
    m_dialogs.insert(requestId, dialog);
    QObject::connect(dialog, &QDialog::finished, dialog,
                     [this, requestId, dialog, request, completion = std::move(completion)](int result) {
        m_dialogs.remove(requestId);
        FileDialogResult outcome;
        if (result != QDialog::Accepted) outcome.status = FileDialogResult::Status::Canceled;
        else {
            outcome.status = FileDialogResult::Status::Accepted;
            outcome.urls = dialog->selectedUrls();
            for (const auto &url : outcome.urls) {
                if (!url.isLocalFile()) {
                    outcome = {FileDialogResult::Status::Unsupported, {},
                               QStringLiteral("Native backend supports local files only")};
                    break;
                }
                const QFileInfo info(url.toLocalFile());
                if (request.mode != FileDialogRequest::Mode::Save && !info.exists()) {
                    outcome = {FileDialogResult::Status::Error, {},
                               QStringLiteral("Selected location is unavailable")};
                    break;
                }
                if (request.mode == FileDialogRequest::Mode::Folder && !info.isDir()) {
                    outcome = {FileDialogResult::Status::Error, {},
                               QStringLiteral("Selected location is not a folder")};
                    break;
                }
                if (request.mode == FileDialogRequest::Mode::Save) {
                    const QFileInfo directory(info.absolutePath());
                    if (!directory.isDir() || !directory.isWritable()) {
                        outcome = {FileDialogResult::Status::Error, {},
                                   QStringLiteral("Save location is unavailable")};
                        break;
                    }
                }
            }
        }
        completion(std::move(outcome));
        dialog->deleteLater();
    });
    dialog->open();
}
void NativeFileDialogBackend::cancel(const QString &requestId)
{
    if (auto dialog = m_dialogs.value(requestId)) dialog->reject();
}
} // namespace QindaTK
