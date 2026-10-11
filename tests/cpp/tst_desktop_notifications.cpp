// SPDX-License-Identifier: LGPL-3.0-or-later
#include "desktop_notifications.h"
#include <QDBusConnection>
#include <QSignalSpy>
#include <QtTest>
using namespace QindaTK;

class FakeNotifications : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")
public:
    QString summary;
    QStringList actions;
    quint32 closed = 0;
public slots:
    quint32 Notify(const QString &, quint32, const QString &, const QString &title, const QString &,
                   const QStringList &buttons, const QVariantMap &, int)
    { summary = title; actions = buttons; return 42; }
    void CloseNotification(quint32 id) { closed = id; emit NotificationClosed(id, 3); }
signals:
    void ActionInvoked(quint32 id, const QString &action);
    void NotificationClosed(quint32 id, quint32 reason);
};
class DesktopNotificationsTest : public QObject {
    Q_OBJECT
private slots:
    void privateBusDeliveryAndUnavailableFallback()
    {
        // CTest launches this executable under dbus-run-session. It must never
        // replace or send notifications through the user's desktop bus.
        auto bus = QDBusConnection::sessionBus();
        QVERIFY(bus.isConnected());
        DesktopNotifications client;
        QTest::qWait(20);
        QVERIFY(!client.available());
        QVERIFY(!client.post("absent", "No server"));
        FakeNotifications server;
        QVERIFY(bus.registerObject("/org/freedesktop/Notifications", &server,
                                    QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals));
        QVERIFY(bus.registerService("org.freedesktop.Notifications"));
        QTRY_VERIFY(client.available());
        QSignalSpy delivered(&client, &DesktopNotifications::delivered);
        QVERIFY(client.post("request", "Task completed", "Details", "", {"open","Open"}));
        QTRY_COMPARE(delivered.size(), 1);
        QCOMPARE(delivered.first()[0].toString(), QStringLiteral("request"));
        QCOMPARE(delivered.first()[1].toUInt(), 42U);
        QCOMPARE(server.summary, QStringLiteral("Task completed"));
        QCOMPARE(server.actions, QStringList({"open","Open"}));
        QSignalSpy invoked(&client, &DesktopNotifications::actionInvoked);
        emit server.ActionInvoked(42, "open");
        QTRY_COMPARE(invoked.size(), 1);
        client.close(42);
        QTRY_COMPARE(server.closed, 42U);
        QVERIFY(bus.unregisterService("org.freedesktop.Notifications"));
        QTRY_VERIFY(!client.available());
        QVERIFY(!client.post("gone", "No server"));
    }
};
QTEST_GUILESS_MAIN(DesktopNotificationsTest)
#include "tst_desktop_notifications.moc"
