// SPDX-License-Identifier: LGPL-3.0-or-later
#include "notification_model.h"
#include <QSignalSpy>
#include <QtTest>

using namespace QindaTK;

class TstNotificationModel final : public QObject {
    Q_OBJECT
private slots:
    void boundedIdentityReadAndDismiss()
    {
        NotificationModel model;
        model.setMaxEntries(2);
        QSignalSpy posted(&model, &NotificationModel::posted);
        const QString first = model.post(QStringLiteral("Build"), QStringLiteral("Started"));
        const QString second = model.post(QStringLiteral("Export"), QStringLiteral("Finished"),
                                           QStringLiteral("success"), QStringLiteral("render"));
        QVERIFY(!first.isEmpty());
        QVERIFY(!second.isEmpty());
        QVERIFY(first != second);
        QCOMPARE(model.count(), 2);
        QCOMPARE(model.unreadCount(), 2);
        QVERIFY(model.markRead(first));
        QCOMPARE(model.unreadCount(), 1);
        QCOMPARE(model.get(0).value(QStringLiteral("notificationId")).toString(), first);
        QVERIFY(model.get(0).value(QStringLiteral("read")).toBool());
        const QString third = model.post(QStringLiteral("Error"), QStringLiteral("Disk full"),
                                          QStringLiteral("error"));
        QCOMPARE(model.count(), 2);
        QCOMPARE(model.get(0).value(QStringLiteral("notificationId")).toString(), second);
        QCOMPARE(model.get(1).value(QStringLiteral("notificationId")).toString(), third);
        QVERIFY(!model.markRead(first));
        QVERIFY(model.dismiss(second));
        QCOMPARE(model.count(), 1);
        QCOMPARE(model.unreadCount(), 1);
        QCOMPARE(posted.size(), 3);
        model.clear();
        QCOMPARE(model.count(), 0);
        QCOMPARE(model.unreadCount(), 0);
    }

    void invalidActionsAndSeverityDoNotPublish()
    {
        NotificationModel model;
        QVERIFY(model.post({}, {}).isEmpty());
        QVERIFY(model.post(QStringLiteral("Unknown"), {}, QStringLiteral("fatal")).isEmpty());
        QVERIFY(model.post(QString(257, QLatin1Char('x')), QStringLiteral("Body")).isEmpty());
        const QVariantList duplicate{{QVariantMap{{QStringLiteral("id"), QStringLiteral("retry")},
                                                 {QStringLiteral("label"), QStringLiteral("Retry")}}},
                                     {QVariantMap{{QStringLiteral("id"), QStringLiteral("retry")},
                                                 {QStringLiteral("label"), QStringLiteral("Again")}}}};
        QVERIFY(model.post(QStringLiteral("Failed"), {}, QStringLiteral("error"), {}, duplicate).isEmpty());
        const QVariantList actions{QVariantMap{{QStringLiteral("id"), QStringLiteral("retry")},
                                                {QStringLiteral("label"), QStringLiteral("Retry")}}};
        const QString id = model.post(QStringLiteral("Failed"), QStringLiteral("Open details"),
                                       QStringLiteral("error"), QStringLiteral("job"), actions);
        QVERIFY(!id.isEmpty());
        QCOMPARE(model.get(0).value(QStringLiteral("actions")).toList(), actions);
        QCOMPARE(model.get(0).value(QStringLiteral("source")).toString(), QStringLiteral("job"));
        QVERIFY(!model.get(0).value(QStringLiteral("timestamp")).toString().isEmpty());
        QVERIFY(!model.dismiss(QStringLiteral("missing")));
        model.setMaxEntries(0);
        QCOMPARE(model.maxEntries(), 200);
        model.setMaxEntries(1001);
        QCOMPARE(model.maxEntries(), 200);
    }

    void repeatedFailuresKeepIdentityAndBoundTheToastQueue()
    {
        NotificationModel model;
        model.setMaxEntries(2);
        const QString first = model.post(QStringLiteral("Render failed"),
            QStringLiteral("Codec unavailable"), QStringLiteral("error"),
            QStringLiteral("render"));
        QVERIFY(!first.isEmpty());
        QCOMPARE(model.pendingToastCount(), 1);
        QCOMPARE(model.takeNextToast().value(QStringLiteral("notificationId")).toString(), first);
        QCOMPARE(model.pendingToastCount(), 0);
        QVERIFY(model.markRead(first));
        QCOMPARE(model.unreadCount(), 0);
        const QString second = model.post(QStringLiteral("Unrelated"),
            QStringLiteral("Done"), QStringLiteral("success"));
        QCOMPARE(model.count(), 2);
        QCOMPARE(model.post(QStringLiteral("Render failed"),
            QStringLiteral("Codec unavailable"), QStringLiteral("error"),
            QStringLiteral("render")), first);
        QCOMPARE(model.count(), 2);
        QCOMPARE(model.get(1).value(QStringLiteral("notificationId")).toString(), first);
        QCOMPARE(model.get(1).value(QStringLiteral("repeatCount")).toInt(), 2);
        QCOMPARE(model.unreadCount(), 2);
        QCOMPARE(model.pendingToastCount(), 2);
        QCOMPARE(model.takeNextToast().value(QStringLiteral("notificationId")).toString(), second);
        QCOMPARE(model.takeNextToast().value(QStringLiteral("notificationId")).toString(), first);
        QCOMPARE(model.pendingToastCount(), 0);
        QVERIFY(model.dismiss(first));
        QCOMPARE(model.count(), 1);
    }
};

QTEST_MAIN(TstNotificationModel)
#include "tst_notification_model.moc"
