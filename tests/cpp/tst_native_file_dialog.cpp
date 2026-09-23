// SPDX-License-Identifier: LGPL-3.0-or-later
#include "file_dialog_adapter.h"
#include "native_file_dialog_backend.h"
#include <QApplication>
#include <QSignalSpy>
#include <QtTest>

using namespace QindaTK;
class TstNativeFileDialog final : public QObject {
    Q_OBJECT
private slots:
    void nativeDialogCancellation()
    {
        FileDialogAdapter adapter;
        adapter.setBackend(std::make_shared<NativeFileDialogBackend>());
        QVERIFY(adapter.available());
        const QString id = adapter.request(QStringLiteral("open"));
        QVERIFY(!id.isEmpty());
        QCOMPARE(adapter.pendingCount(), 1);
        QSignalSpy finished(&adapter, &FileDialogAdapter::finished);
        QVERIFY(adapter.cancel(id));
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.first().at(1).toMap().value(QStringLiteral("status")).toString(),
                 QStringLiteral("canceled"));
        QCOMPARE(adapter.pendingCount(), 0);
        QCoreApplication::processEvents();
        QCOMPARE(finished.size(), 1);
    }
};
QTEST_MAIN(TstNativeFileDialog)
#include "tst_native_file_dialog.moc"
