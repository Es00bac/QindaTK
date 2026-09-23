// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "file_dialog_adapter.h"
#include <QHash>
#include <QPointer>

class QFileDialog;
namespace QindaTK {
// Optional Qt Widgets backend. Construct only under QApplication; platform
// native file dialogs are used where the Qt platform plugin supplies them.
class NativeFileDialogBackend final : public FileDialogBackend {
public:
    ~NativeFileDialogBackend() override;
    bool available() const override;
    bool supportsRemoteUrls() const override { return false; }
    void start(const QString &requestId, const FileDialogRequest &request,
               QWindow *parent, std::function<void(FileDialogResult)> completion) override;
    void cancel(const QString &requestId) override;
private:
    QHash<QString, QPointer<QFileDialog>> m_dialogs;
};
} // namespace QindaTK
