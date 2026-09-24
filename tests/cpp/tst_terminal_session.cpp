// SPDX-License-Identifier: LGPL-3.0-or-later
#include "terminal_session.h"

#include <QColor>
#include <QtTest>

using namespace QindaTK;

class TestTerminalSession : public QObject {
    Q_OBJECT
private slots:
    void localPtyLifecycle()
    {
        TerminalSession session;
        QVERIFY(!session.running());
        QVERIFY(session.resize(5, 40));
        session.setMaxScrollback(3);
        const QString script = QStringLiteral(
            "stty size; printf '\\033[31mHéllo\\033[0m\\r\\n'; read line; stty size; exit 7");
        QVERIFY2(session.start(QStringLiteral("/bin/sh"),
                               {QStringLiteral("-c"), script}),
                 qPrintable(session.errorString()));
        QTRY_VERIFY_WITH_TIMEOUT(session.copyText(0, session.scrollback().size() + 4)
                                     .contains(QStringLiteral("Héllo")), 3000);
        bool red = false;
        for (const QVariant &row : session.screen()) {
            for (const QVariant &span : row.toMap().value(QStringLiteral("spans")).toList()) {
                const QVariantMap item = span.toMap();
                const QColor foreground(item.value(QStringLiteral("foreground")).toString());
                if (item.value(QStringLiteral("text")).toString().contains(QStringLiteral("Héllo"))
                    && foreground.isValid() && foreground.red() > 100
                    && foreground.green() < 20 && foreground.blue() < 20)
                    red = true;
            }
        }
        QVERIFY(red);
        QVERIFY(session.resize(6, 50));
        QVERIFY(session.sendText(QStringLiteral("continue\n")));
        QTRY_VERIFY_WITH_TIMEOUT(!session.running(), 3000);
        QCOMPARE(session.exitCode(), 7);
        const QString output = session.copyText(0, session.scrollback().size() + 5);
        QVERIFY(output.contains(QStringLiteral("5 40")));
        QVERIFY(output.contains(QStringLiteral("6 50")));

        QVERIFY(session.resize(3, 40));
        QVERIFY(session.start(QStringLiteral("/bin/sh"),
                              {QStringLiteral("-c"), QStringLiteral("i=0; while [ $i -lt 12 ]; do echo row$i; i=$((i+1)); done")}));
        QTRY_VERIFY_WITH_TIMEOUT(!session.running(), 3000);
        QCOMPARE(session.scrollback().size(), 3);
        QVERIFY(session.copyText(0, session.scrollback().size() + 2)
                    .contains(QStringLiteral("row11")));

        QVERIFY(session.start(QStringLiteral("/bin/sh"),
                              {QStringLiteral("-c"), QStringLiteral("sleep 30")}));
        session.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!session.running(), 3000);
        QVERIFY(session.exitCode() != 0);
    }
};

QTEST_GUILESS_MAIN(TestTerminalSession)
#include "tst_terminal_session.moc"
