// SPDX-License-Identifier: LGPL-3.0-or-later
#include "terminal_session.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <sys/ioctl.h>
#include <unistd.h>

using namespace QindaTK;

class TestTerminalModes : public QObject {
    Q_OBJECT
    QByteArray capture(const QString &codes, const QByteArray &expected,
                       const std::function<void(TerminalSession &)> &send)
    {
        QTemporaryDir directory;
        TerminalSession session;
        session.resize(5, 60);
        const QString path = directory.filePath(QStringLiteral("input"));
        const QString probe = QFINDTESTDATA("../fixtures/terminal/probe.py");
        if (!session.start(QStringLiteral("/usr/bin/python3"),
                           {probe, QString::fromLatin1(codes.toUtf8().toHex()),
                            QString::number(expected.size()), path}))
            return {};
        QTest::qWait(20);
        QElapsedTimer timeout;
        timeout.start();
        while (!session.copyText(0, session.scrollback().size() + 4).contains(QStringLiteral("READY"))
               && timeout.elapsed() < 3000) QTest::qWait(10);
        send(session);
        timeout.restart();
        while (session.running() && timeout.elapsed() < 6000) QTest::qWait(10);
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        return file.readAll();
    }
private slots:
    void negotiatedKeys_data()
    {
        QTest::addColumn<QString>("codes");
        QTest::addColumn<int>("key");
        QTest::addColumn<int>("modifiers");
        QTest::addColumn<QString>("text");
        QTest::addColumn<QByteArray>("expected");
        QTest::newRow("normal cursor") << QString() << int(Qt::Key_Up) << 0 << QString() << QByteArray("\033[A");
        QTest::newRow("application cursor") << QStringLiteral("\033[?1h") << int(Qt::Key_Up) << 0 << QString() << QByteArray("\033OA");
        QTest::newRow("shift enter") << QString() << int(Qt::Key_Return) << int(Qt::ShiftModifier) << QString() << QByteArray("\r");
        QTest::newRow("backtab") << QString() << int(Qt::Key_Backtab) << 0 << QString() << QByteArray("\033[Z");
        QTest::newRow("ctrl shift left") << QString() << int(Qt::Key_Left) << int(Qt::ControlModifier | Qt::ShiftModifier) << QString() << QByteArray("\033[1;6D");
        QTest::newRow("alt ctrl c") << QString() << int(Qt::Key_C) << int(Qt::ControlModifier | Qt::AltModifier) << QString() << QByteArray("\033\003");
        QTest::newRow("ctrl v goes to child") << QString() << int(Qt::Key_V) << int(Qt::ControlModifier) << QString() << QByteArray("\026");
        QTest::newRow("ctrl backspace") << QString() << int(Qt::Key_Backspace) << int(Qt::ControlModifier) << QString() << QByteArray("\010");
        QTest::newRow("shift backspace") << QString() << int(Qt::Key_Backspace) << int(Qt::ShiftModifier) << QString() << QByteArray("\177");
        QTest::newRow("ctrl space") << QString() << int(Qt::Key_Space) << int(Qt::ControlModifier) << QString() << QByteArray(1, '\0');
        QTest::newRow("ctrl bracket") << QString() << int(Qt::Key_BracketLeft) << int(Qt::ControlModifier) << QString() << QByteArray("\033");
        QTest::newRow("ctrl underscore") << QString() << int(Qt::Key_Underscore) << int(Qt::ControlModifier | Qt::ShiftModifier) << QString() << QByteArray("\037");
        QTest::newRow("ctrl caret") << QString() << int(Qt::Key_AsciiCircum) << int(Qt::ControlModifier | Qt::ShiftModifier) << QString() << QByteArray("\036");
        QTest::newRow("shift f5") << QString() << int(Qt::Key_F5) << int(Qt::ShiftModifier) << QString() << QByteArray("\033[15;2~");
        QTest::newRow("application keypad") << QStringLiteral("\033=") << int(Qt::Key_2) << int(Qt::KeypadModifier) << QStringLiteral("2") << QByteArray("\033Or");
    }
    void negotiatedKeys()
    {
        QFETCH(QString, codes); QFETCH(int, key); QFETCH(int, modifiers);
        QFETCH(QString, text); QFETCH(QByteArray, expected);
        QCOMPARE(capture(codes, expected, [&](TerminalSession &s) { QVERIFY(s.sendKey(key, modifiers, text)); }), expected);
    }
    void bracketedPaste()
    {
        const QByteArray expected("\033[200~one\rtwo\033[201~");
        QCOMPARE(capture(QStringLiteral("\033[?2004h"), expected,
                         [](TerminalSession &s) { QVERIFY(s.pasteText(QStringLiteral("one\r\ntwo"))); }), expected);
    }
    void pasteSanitizesControls()
    {
        const QByteArray expected("one[201~two");
        QCOMPARE(capture({}, expected, [](TerminalSession &s) {
            QVERIFY(s.pasteText(QStringLiteral("one\033[201~\003two")));
        }), expected);
    }
    void mouseAndFocus()
    {
        const QByteArray expected("\033[<0;5;3M\033[<0;5;3m\033[I\033[O");
        QCOMPARE(capture(QStringLiteral("\033[?1000h\033[?1006h\033[?1004h"), expected,
                         [](TerminalSession &s) {
            QCOMPARE(s.mouseMode(), int(VTERM_PROP_MOUSE_CLICK));
            QVERIFY(s.sendMouse(2, 4, 1, true));
            QVERIFY(s.sendMouse(2, 4, 1, false));
            s.setFocused(true); s.setFocused(false);
        }), expected);
    }
    void terminalPropertiesAndHistory()
    {
        TerminalSession session;
        QVERIFY(session.resize(3, 25));
        const QString script = QStringLiteral(
            "printf 'old1\r\nold2\r\nold3\r\n'; "
            "printf '\033[?1049h\033]2;Test title\007\033[?25l\033[5 qALT'; read x; "
            "printf '\033[?1049l'; read x");
        QVERIFY(session.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), script}));
        QTRY_VERIFY(session.alternateScreen());
        QCOMPARE(session.title(), QStringLiteral("Test title"));
        QVERIFY(!session.cursorVisible());
        QCOMPARE(session.cursorShape(), int(VTERM_PROP_CURSORSHAPE_BAR_LEFT));
        QVERIFY(session.scrollback().isEmpty());
        QVERIFY(session.copyText(0, 2).contains(QStringLiteral("ALT")));
        QVERIFY(!session.copyText(0, 2).contains(QStringLiteral("old")));
        QVERIFY(session.sendText(QStringLiteral("x\n")));
        QTRY_VERIFY(!session.alternateScreen());
        QVERIFY(session.copyText(0, session.scrollback().size() + 2).contains(QStringLiteral("old1")));
        session.stop();
        QTRY_VERIFY(!session.running());
    }
    void wideCombiningAndColoredHistory()
    {
        TerminalSession session;
        QVERIFY(session.resize(3, 20));
        QVERIFY(session.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"),
            QStringLiteral("printf '\033[31m界é\033[0m\r\nline2\r\nline3\r\nline4'; read x")}));
        QTRY_VERIFY(session.scrollback().size() > 0);
        QCOMPARE(session.scrollback().first(), QStringLiteral("界é"));
        const auto cells = session.rowData(0).value(QStringLiteral("cells")).toList();
        QCOMPARE(cells.at(0).toMap().value(QStringLiteral("width")).toInt(), 2);
        QCOMPARE(cells.at(1).toMap().value(QStringLiteral("column")).toInt(), 2);
        QCOMPARE(session.copyTextRange(0, 0, 0, 3), QStringLiteral("界é"));
        QCOMPARE(session.copyTextRange(0, 1, 0, 2), QStringLiteral("界"));
        QVERIFY(!cells.at(0).toMap().value(QStringLiteral("foreground")).toString().isEmpty());
        session.setColors(QColor(Qt::white), QColor(Qt::black),
                          {QColor(Qt::black), QColor(QStringLiteral("#123456"))});
        QCOMPARE(session.rowData(0).value(QStringLiteral("cells")).toList().first().toMap()
                     .value(QStringLiteral("foreground")).toString(), QStringLiteral("#123456"));
        const int total = session.scrollback().size() + session.rows();
        const auto previous = session.copyText(0, total - 1);
        QVERIFY(session.resize(5, 20));
        QCOMPARE(session.scrollback().size() + session.rows(), qMax(total, 5));
        QCOMPARE(session.copyText(0, total - 1), previous);
        session.stop();
        QTRY_VERIFY(!session.running());
    }
    void foregroundJobAndDirectory()
    {
        QTemporaryDir directory;
        TerminalSession session;
        QVERIFY(session.start(QStringLiteral("/bin/bash"), {QStringLiteral("--noprofile"), QStringLiteral("--norc"), QStringLiteral("-i")}, directory.path()));
        QTRY_COMPARE(session.workingDirectory(), directory.path());
        QVERIFY(!session.foregroundProcessRunning());
        QVERIFY(session.sendText(QStringLiteral("sleep 30\n")));
        QTRY_VERIFY(session.foregroundProcessRunning());
        QVERIFY(session.sendKey(Qt::Key_C, Qt::ControlModifier));
        QTRY_VERIFY(!session.foregroundProcessRunning());
        QVERIFY(session.sendText(QStringLiteral("cd /tmp\n")));
        QTRY_COMPARE(session.workingDirectory(), QStringLiteral("/tmp"));
        session.stop();
        QTRY_VERIFY(!session.running());
    }
    void historyTrimAndLargeExitOutput()
    {
        TerminalSession session;
        QVERIFY(session.resize(3, 40));
        session.setMaxScrollback(10000);
        QVERIFY(session.start(QStringLiteral("/usr/bin/python3"), {QStringLiteral("-c"),
            QStringLiteral("import os; os.write(1, b'line\\r\\n'*70000 + b'FINAL_MARKER')")}));
        QTRY_VERIFY_WITH_TIMEOUT(!session.running(), 15000);
        QCOMPARE(session.exitCode(), 0);
        QCOMPARE(session.scrollback().size(), 10000);
        QVERIFY(session.scrollbackOffset() > 50000);
        QVERIFY(session.copyText(0, session.scrollback().size() + 2).contains(QStringLiteral("FINAL_MARKER")));
        const auto offset = session.scrollbackOffset();
        session.setMaxScrollback(3);
        QCOMPARE(session.scrollback().size(), 3);
        QCOMPARE(session.scrollbackOffset(), offset + 9997);
        session.clearHistory();
        QVERIFY(session.scrollback().isEmpty());
    }
    void rejectInvalidRequestsAndSupportLargeGrid()
    {
        TerminalSession session;
        QVERIFY(session.resize(180, 320));
        QCOMPARE(session.rows(), 180);
        QCOMPARE(session.columns(), 320);
        QVERIFY(!session.resize(1000, 5000));
        QVERIFY(!session.start(QStringLiteral("/bin/sh"), {}, QStringLiteral("/tmp\0suffix")));
        QVERIFY(!session.start(QStringLiteral("/bin/sh"), {}, QStringLiteral("/directory/that/does/not/exist")));
        QVERIFY(!session.start(QStringLiteral("/nonexistent/program")));
        QVERIFY(!session.errorString().isEmpty());
        QVERIFY(!session.running());
    }
};

QTEST_GUILESS_MAIN(TestTerminalModes)
#include "tst_terminal_modes.moc"
