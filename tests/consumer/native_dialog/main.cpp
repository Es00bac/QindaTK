// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QApplication>
#include <file_dialog_adapter.h>
#include <native_file_dialog_backend.h>
#include <memory>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QindaTK::FileDialogAdapter dialogs;
    dialogs.setBackend(std::make_shared<QindaTK::NativeFileDialogBackend>());
    return dialogs.capabilities().value(QStringLiteral("nativeDialog")).toBool() ? 0 : 1;
}
