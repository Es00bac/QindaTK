// SPDX-License-Identifier: LGPL-3.0-or-later
#include "local_attachment_store.h"

#include <QTemporaryDir>
#include <QtTest>

class LocalAttachmentStoreTest : public QObject {
    Q_OBJECT
private slots:
    void byteRoundTripAndRemoval()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QindaTK::LocalAttachmentStore store;
        store.setRootPath(directory.path());
        const QByteArray bytes("header\0body\xff", 12);
        const QVariantMap imported = store.importBytes(QStringLiteral("draft/att-7"),
            QStringLiteral(" notes.txt "), QStringLiteral("text/plain"), bytes);
        QVERIFY2(imported.value(QStringLiteral("ok")).toBool(), qPrintable(imported.value(QStringLiteral("error")).toString()));
        QCOMPARE(imported.value(QStringLiteral("size")).toLongLong(), bytes.size());

        QindaTK::LocalAttachmentStore reopened;
        reopened.setRootPath(directory.path());
        const QVariantMap loaded = reopened.read(QStringLiteral("draft/att-7"));
        QVERIFY2(loaded.value(QStringLiteral("ok")).toBool(), qPrintable(loaded.value(QStringLiteral("error")).toString()));
        QCOMPARE(loaded.value(QStringLiteral("bytes")).toByteArray(), bytes);
        QCOMPARE(loaded.value(QStringLiteral("name")).toString(), QStringLiteral("notes.txt"));

        const QVariantMap removed = reopened.remove(QStringLiteral("draft/att-7"));
        QVERIFY(removed.value(QStringLiteral("ok")).toBool());
        QVERIFY(!reopened.read(QStringLiteral("draft/att-7")).value(QStringLiteral("ok")).toBool());
        const QVariantMap tooLarge = reopened.importBytes(QStringLiteral("large"), QStringLiteral("x.bin"),
                                                          QStringLiteral("application/octet-stream"),
                                                          QByteArray(static_cast<qsizetype>(reopened.maximumBytes() + 1), 'x'));
        QVERIFY(!tooLarge.value(QStringLiteral("ok")).toBool());
    }
};

QTEST_MAIN(LocalAttachmentStoreTest)
#include "tst_local_attachment_store.moc"
