// SPDX-License-Identifier: LGPL-3.0-or-later
// What agent CLIs (Claude Code, Codex, Kimi Code, OpenCode) ask of a terminal,
// exercised through a real PTY: capability probes, negotiated key encodings,
// faint text, synchronized output, clipboard writes, notifications and status.
#include "terminal_session.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

class TestTerminalAgents : public QObject {
    Q_OBJECT
    // Runs the raw PTY peer: it writes `codes`, then records `expected.size()`
    // bytes of whatever the terminal sends back.
    QByteArray capture(const QByteArray &codes, const QByteArray &expected,
                       const std::function<void(TerminalSession &)> &send = {})
    {
        QTemporaryDir directory;
        TerminalSession session;
        session.resize(5, 60);
        session.setColors(QColor(QStringLiteral("#e0e0e0")), QColor(QStringLiteral("#101018")));
        const QString path = directory.filePath(QStringLiteral("input"));
        const QString probe = QFINDTESTDATA("../fixtures/terminal/probe.py");
        if (!session.start(QStringLiteral("/usr/bin/python3"),
                           {probe, QString::fromLatin1(codes.toHex()), QString::number(expected.size()), path}))
            return {};
        QElapsedTimer timeout;
        timeout.start();
        while (!session.copyText(0, session.scrollback().size() + 4).contains(QStringLiteral("READY"))
               && timeout.elapsed() < 3000) QTest::qWait(10);
        if (send) send(session);
        timeout.restart();
        while (session.running() && timeout.elapsed() < 6000) QTest::qWait(10);
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        return file.readAll();
    }
    static QString screenText(const TerminalSession &session)
    {
        return session.copyText(0, session.scrollback().size() + session.rows() - 1);
    }
    static bool shell(TerminalSession &session, const QString &script)
    {
        return session.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), script});
    }
private slots:
    void probesAreAnsweredInOrder_data()
    {
        QTest::addColumn<QByteArray>("codes");
        QTest::addColumn<QByteArray>("expected");
        const QByteArray deviceAttributes("\033[?1;2c");
        // Programs end a probe with DA1 and read "unsupported" if it arrives first.
        QTest::newRow("kitty keyboard") << QByteArray("\033[?u\033[c") << "\033[?0u" + deviceAttributes;
        QTest::newRow("kitty keyboard after push") << QByteArray("\033[>5u\033[?u\033[c") << "\033[?5u" + deviceAttributes;
        QTest::newRow("kitty pop and set") << QByteArray("\033[>7u\033[>1u\033[<u\033[=8;2u\033[?u\033[<9u\033[?u")
                                           << QByteArray("\033[?15u\033[?0u");
        QTest::newRow("version") << QByteArray("\033[>0q\033[c")
                                 << "\033P>|tst_terminal_agents\033\\" + deviceAttributes;
        QTest::newRow("synchronized output mode") << QByteArray("\033[?2026$p\033[c")
                                                  << "\033[?2026;2$y" + deviceAttributes;
        QTest::newRow("mode asked inside a frame") << QByteArray("\033[?2026h\033[?2026$p\033[?2026l")
                                                   << QByteArray("\033[?2026;1$y");
        QTest::newRow("known mode stays with libvterm") << QByteArray("\033[?2004$p") << QByteArray("\033[?2004;2$y");
        QTest::newRow("colours") << QByteArray("\033]10;?\033\\\033]11;?\007\033[c")
                                 << "\033]10;rgb:e0e0/e0e0/e0e0\033\\\033]11;rgb:1010/1010/1818\007" + deviceAttributes;
        QTest::newRow("foreground and background in one") << QByteArray("\033]10;?;?\007")
            << QByteArray("\033]10;rgb:e0e0/e0e0/e0e0\007\033]11;rgb:1010/1010/1818\007");
        QTest::newRow("cursor colour") << QByteArray("\033]12;#eeeeee\007\033]12;?\007")
                                       << QByteArray("\033]12;rgb:eeee/eeee/eeee\007");
        QTest::newRow("palette") << QByteArray("\033]4;1;?\007") << QByteArray("\033]4;1;rgb:e0e0/0000/0000\007");
        QTest::newRow("colour scheme") << QByteArray("\033[?996n") << QByteArray("\033[?997;1n");
        QTest::newRow("scheme notification mode") << QByteArray("\033[?2031h\033[?2031$p") << QByteArray("\033[?2031;1$y");
        QTest::newRow("program status") << QByteArray("\033]7501;?\033\\\033[c")
            << "\033]7501;?:states=idle,working,done,blocked,error:kinds=permission,question,auth\033\\" + deviceAttributes;
        QTest::newRow("clipboard is never read") << QByteArray("\033]52;c;?\007") << QByteArray("\033]52;c;\007");
        QTest::newRow("clipboard capability") << QByteArray("\033P+q4d73\033\\")
            << QByteArray("\033P1+r4d73=1b5d35323b25703125733b2570322573" "07\033\\");
        QTest::newRow("unknown capability") << QByteArray("\033P+q7a7a\033\\") << QByteArray("\033P0+r7a7a\033\\");
        QTest::newRow("notification support") << QByteArray("\033]99;i=x:p=?;\033\\")
            << QByteArray("\033]99;i=x:p=?;o=always,unfocused,invisible:p=title,body\033\\");
        QTest::newRow("text area") << QByteArray("\033[18t") << QByteArray("\033[8;5;60t");
        QTest::newRow("modify other keys") << QByteArray("\033[>4;2m\033[?4m") << QByteArray("\033[>4;2m");
    }
    void probesAreAnsweredInOrder()
    {
        QFETCH(QByteArray, codes);
        QFETCH(QByteArray, expected);
        QCOMPARE(capture(codes, expected), expected);
    }
    void keysFollowTheNegotiatedProtocol_data()
    {
        QTest::addColumn<QByteArray>("codes");
        QTest::addColumn<int>("key");
        QTest::addColumn<int>("modifiers");
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("type");
        QTest::addColumn<QByteArray>("expected");
        const int shift = Qt::ShiftModifier, ctrl = Qt::ControlModifier;
        const QByteArray none, kitty("\033[>1u"), events("\033[>3u"), other("\033[>4;1m");
        // With nothing negotiated a shell must see the traditional bytes.
        QTest::newRow("legacy ctrl j") << none << int(Qt::Key_J) << ctrl << QStringLiteral("\n") << 1 << QByteArray("\n");
        QTest::newRow("legacy ctrl i") << none << int(Qt::Key_I) << ctrl << QStringLiteral("\t") << 1 << QByteArray("\t");
        QTest::newRow("legacy ctrl m") << none << int(Qt::Key_M) << ctrl << QStringLiteral("\r") << 1 << QByteArray("\r");
        QTest::newRow("legacy ctrl shift c") << none << int(Qt::Key_C) << (ctrl | shift) << QString() << 1 << QByteArray("\003");
        QTest::newRow("legacy ctrl 1") << none << int(Qt::Key_1) << ctrl << QStringLiteral("1") << 1 << QByteArray("1");
        QTest::newRow("legacy ctrl slash") << none << int(Qt::Key_Slash) << ctrl << QString() << 1 << QByteArray("\037");
        QTest::newRow("legacy shift enter") << none << int(Qt::Key_Return) << shift << QStringLiteral("\r") << 1 << QByteArray("\r");
        QTest::newRow("legacy alt enter") << none << int(Qt::Key_Return) << int(Qt::AltModifier) << QStringLiteral("\r") << 1 << QByteArray("\033\r");
        QTest::newRow("kitty shift enter") << kitty << int(Qt::Key_Return) << shift << QStringLiteral("\r") << 1 << QByteArray("\033[13;2u");
        QTest::newRow("kitty ctrl c") << kitty << int(Qt::Key_C) << ctrl << QStringLiteral("\003") << 1 << QByteArray("\033[99;5u");
        QTest::newRow("kitty escape") << kitty << int(Qt::Key_Escape) << 0 << QString() << 1 << QByteArray("\033[27u");
        QTest::newRow("kitty ignores cursor key mode") << QByteArray("\033[?1h\033[>1u") << int(Qt::Key_Up) << 0 << QString() << 1 << QByteArray("\033[A");
        QTest::newRow("kitty release") << events << int(Qt::Key_Up) << 0 << QString() << 3 << QByteArray("\033[1;1:3A");
        QTest::newRow("kitty popped") << QByteArray("\033[>1u\033[<u") << int(Qt::Key_Return) << shift << QStringLiteral("\r") << 1 << QByteArray("\r");
        QTest::newRow("alternate screen has its own stack") << QByteArray("\033[>1u\033[?1049h") << int(Qt::Key_Return) << shift << QStringLiteral("\r") << 1 << QByteArray("\r");
        QTest::newRow("leaving the alternate screen restores") << QByteArray("\033[>1u\033[?1049h\033[>8u\033[?1049l") << int(Qt::Key_Return) << shift << QStringLiteral("\r") << 1 << QByteArray("\033[13;2u");
        QTest::newRow("full reset clears the protocol") << QByteArray("\033[>1u\033c") << int(Qt::Key_Return) << shift << QStringLiteral("\r") << 1 << QByteArray("\r");
        QTest::newRow("modify other keys shift enter") << other << int(Qt::Key_Return) << shift << QStringLiteral("\r") << 1 << QByteArray("\033[27;2;13~");
        QTest::newRow("modify other keys leaves ctrl c") << other << int(Qt::Key_C) << ctrl << QString() << 1 << QByteArray("\003");
    }
    void keysFollowTheNegotiatedProtocol()
    {
        QFETCH(QByteArray, codes); QFETCH(int, key); QFETCH(int, modifiers); QFETCH(QString, text);
        QFETCH(int, type); QFETCH(QByteArray, expected);
        QCOMPARE(capture(codes, expected, [&](TerminalSession &session) {
            QVERIFY(session.sendKeyEvent(key, modifiers, text, type));
        }), expected);
    }
    void modesAreReported()
    {
        TerminalSession session;
        QSignalSpy modes(&session, &TerminalSession::modesChanged);
        QVERIFY(shell(session, QStringLiteral("printf '\\033[?2004h\\033[?1007l\\033[>5uREADY'; read x; printf '\\033[?2004l\\033[?1007h\\033[<uDONE'; read x")));
        QTRY_VERIFY(screenText(session).contains(QStringLiteral("READY")));
        QVERIFY(session.bracketedPaste());
        QVERIFY(!session.alternateScroll());
        QCOMPARE(session.keyboardFlags(), 5);
        QVERIFY(modes.size() >= 2);
        QVERIFY(session.sendText(QStringLiteral("\n")));
        QTRY_VERIFY(screenText(session).contains(QStringLiteral("DONE")));
        QVERIFY(!session.bracketedPaste());
        QVERIFY(session.alternateScroll());
        QCOMPARE(session.keyboardFlags(), 0);
        session.stop();
        QTRY_VERIFY(!session.running());
    }
    void faintTextAndUnsupportedAttributes()
    {
        TerminalSession session;
        QVERIFY(session.resize(4, 40));
        // 58;5;1 is an underline colour; read naively it is blink plus bold.
        QVERIFY(shell(session, QStringLiteral(
            "printf 'a\\033[2mb\\033[1mc\\033[22md\\033[2;31me\\033[0mf\\033[58;5;1mg\\033[58;2;9;9;9mh"
            "\\033[38;2;1;2;3mi\\033[38;5;2mj\\033[0;2mk\\033[mL\\033[4:3;2mm\\033[0;11mn\\033[0m'; read x")));
        QTRY_VERIFY(screenText(session).contains(QStringLiteral("abcdefghijkLmn")));
        const auto cells = session.rowData(0).value(QStringLiteral("cells")).toList();
        const auto flag = [&cells](int column, const char *name) {
            return cells.at(column).toMap().value(QLatin1String(name)).toBool();
        };
        const auto dim = [&flag](int column) { return flag(column, "dim"); };
        QVERIFY(!dim(0)); QVERIFY(dim(1)); QVERIFY(dim(2)); QVERIFY(flag(2, "bold"));
        QVERIFY(!dim(3)); QVERIFY(!flag(3, "bold"));
        QVERIFY(dim(4)); QVERIFY(!dim(5));
        for (int column : {6, 7}) {
            QVERIFY(!flag(column, "bold"));
            QVERIFY(!dim(column));
        }
        QCOMPARE(cells.at(8).toMap().value(QStringLiteral("foreground")).toString(), QStringLiteral("#010203"));
        QVERIFY(!dim(8)); QVERIFY(!dim(9));
        QVERIFY(dim(10)); QVERIFY(!dim(11));
        QVERIFY(dim(12)); QVERIFY(flag(12, "underline"));
        QVERIFY(!dim(13)); // a program cannot reach the carrier attribute itself
        const auto spans = session.screen().first().toMap().value(QStringLiteral("spans")).toList();
        QVERIFY(spans.at(1).toMap().value(QStringLiteral("dim")).toBool());
        session.stop();
        QTRY_VERIFY(!session.running());
    }
    void synchronizedFramesAppearWhole()
    {
        TerminalSession session;
        QVERIFY(session.resize(4, 40));
        QVERIFY(shell(session, QStringLiteral(
            "printf 'before\\033[?2026hfirst'; sleep 0.4; printf 'second\\033[?2026lafter'; read x")));
        QTRY_VERIFY(screenText(session).contains(QStringLiteral("before")));
        QTest::qWait(150);
        QVERIFY(!screenText(session).contains(QStringLiteral("first")));
        QSignalSpy changed(&session, &TerminalSession::screenChanged);
        QTRY_VERIFY(screenText(session).contains(QStringLiteral("beforefirstsecondafter")));
        QTRY_VERIFY(!changed.isEmpty());
        session.stop();
        QTRY_VERIFY(!session.running());
    }
    void abandonedFrameIsShown()
    {
        TerminalSession session;
        // A program that dies inside a frame must not leave the screen frozen.
        QVERIFY(shell(session, QStringLiteral("printf '\\033[?2026hlost'; read x")));
        QTest::qWait(300);
        QVERIFY(!screenText(session).contains(QStringLiteral("lost")));
        QTRY_VERIFY_WITH_TIMEOUT(screenText(session).contains(QStringLiteral("lost")), 3000);
        session.stop();
        QTRY_VERIFY(!session.running());
        // Exit ends a frame as well.
        TerminalSession exiting;
        QVERIFY(shell(exiting, QStringLiteral("printf '\\033[?2026hfinal'")));
        QSignalSpy exited(&exiting, &TerminalSession::exited);
        QTRY_VERIFY(!exited.isEmpty());
        QVERIFY(screenText(exiting).contains(QStringLiteral("final")));
        QVERIFY(exiting.screen().first().toMap().value(QStringLiteral("text")).toString().contains(QStringLiteral("final")));
    }
    void outputIsPresentedTogether()
    {
        TerminalSession session;
        QVERIFY(session.resize(4, 40));
        QSignalSpy changed(&session, &TerminalSession::screenChanged);
        QVERIFY(shell(session, QStringLiteral(
            "i=0; while [ $i -lt 200 ]; do printf 'x\\r'; i=$((i+1)); done; printf 'END'; read x")));
        QTRY_VERIFY(session.screen().first().toMap().value(QStringLiteral("text")).toString().contains(QStringLiteral("END")));
        // 200 separate writes must not become 200 repaints of the whole screen.
        QVERIFY2(changed.size() < 60, qPrintable(QString::number(changed.size())));
        session.stop();
        QTRY_VERIFY(!session.running());
    }
    void clipboardWritesAreRequests()
    {
        TerminalSession session;
        QSignalSpy clipboard(&session, &TerminalSession::clipboardWriteRequested);
        QVERIFY(shell(session, QStringLiteral(
            "printf '\\033]52;c;aGVsbG8g4pyTCg==\\007\\033]52;p;cHJpbWFyeQ==\\033\\\\\\033]52;;ZGVmYXVsdA==\\007"
            "\\033]52;c;!!!!\\007\\033]52;c;\\007DONE'; read x")));
        QTRY_VERIFY(screenText(session).contains(QStringLiteral("DONE")));
        QCOMPARE(clipboard.size(), 3);
        QCOMPARE(clipboard.at(0).at(0).toString(), QStringLiteral("hello \u2713\n"));
        QCOMPARE(clipboard.at(0).at(1).toBool(), false);
        QCOMPARE(clipboard.at(1).at(0).toString(), QStringLiteral("primary"));
        QCOMPARE(clipboard.at(1).at(1).toBool(), true);
        QCOMPARE(clipboard.at(2).at(0).toString(), QStringLiteral("default"));
        session.stop();
        QTRY_VERIFY(!session.running());
    }
    void notificationsAreRequests()
    {
        TerminalSession session;
        QSignalSpy notified(&session, &TerminalSession::notificationRequested);
        session.setFocused(true);
        QVERIFY(shell(session, QStringLiteral(
            "printf '\\033]9;Turn complete\\007\\033]777;notify;Codex;Needs\\001 approval\\007"
            "\\033]99;i=1:d=0;Title\\033\\\\\\033]99;i=1:p=body;Body\\033\\\\"
            "\\033]99;i=2:o=unfocused;Hidden\\033\\\\\\033]99;i=3:e=1;QmFzZTY0\\033\\\\\\033]9;9;ignored\\007DONE'; read x")));
        QTRY_VERIFY(screenText(session).contains(QStringLiteral("DONE")));
        QCOMPARE(notified.size(), 4);
        QCOMPARE(notified.at(0).at(1).toString(), QStringLiteral("Turn complete"));
        QCOMPARE(notified.at(1).at(0).toString(), QStringLiteral("Codex"));
        // Text shown outside the grid carries no control characters.
        QCOMPARE(notified.at(1).at(1).toString(), QStringLiteral("Needs approval"));
        QCOMPARE(notified.at(2).at(0).toString(), QStringLiteral("Title"));
        QCOMPARE(notified.at(2).at(1).toString(), QStringLiteral("Body"));
        QCOMPARE(notified.at(3).at(0).toString(), QStringLiteral("Base64"));
        session.stop();
        QTRY_VERIFY(!session.running());
    }
    void programStatusFollowsTheProgram()
    {
        TerminalSession session;
        QSignalSpy changed(&session, &TerminalSession::programStatusChanged);
        QVERIFY(shell(session, QStringLiteral(
            "printf '\\033]7501;state=working:app=codex:progress=40\\033\\\\A'; read x; "
            "printf '\\033]7501;state=blocked:kind=permission:msg=UnVuIG5wbSB0ZXN0Pw==\\007"
            "\\033]7501;state=done:id=sub/task\\007\\033]7501;state=later\\007\\033]9;4;1;90\\007B'; read x")));
        QTRY_VERIFY(screenText(session).contains(QLatin1Char('A')));
        QCOMPARE(session.programStatus().value(QStringLiteral("state")).toString(), QStringLiteral("working"));
        QCOMPARE(session.programStatus().value(QStringLiteral("app")).toString(), QStringLiteral("codex"));
        QCOMPARE(session.programStatus().value(QStringLiteral("progress")).toInt(), 40);
        QVERIFY(session.sendText(QStringLiteral("\n")));
        QTRY_VERIFY(screenText(session).contains(QLatin1Char('B')));
        QCOMPARE(session.programStatus().value(QStringLiteral("state")).toString(), QStringLiteral("blocked"));
        QCOMPARE(session.programStatus().value(QStringLiteral("kind")).toString(), QStringLiteral("permission"));
        QCOMPARE(session.programStatus().value(QStringLiteral("message")).toString(), QStringLiteral("Run npm test?"));
        QCOMPARE(session.programStatusRecords().size(), 2);
        // Exit ends the blocked record; the finished one waits to be seen.
        QVERIFY(session.sendText(QStringLiteral("\n")));
        QTRY_VERIFY(!session.running());
        QCOMPARE(session.programStatus().value(QStringLiteral("state")).toString(), QStringLiteral("done"));
        session.acknowledgeProgramStatus();
        QVERIFY(session.programStatus().isEmpty());
        QVERIFY(changed.size() >= 4);
    }
    void progressReportsBecomeStatus()
    {
        TerminalSession session;
        QVERIFY(shell(session, QStringLiteral(
            "printf '\\033]9;4;1;30\\007A'; read x; printf '\\033]133;A\\007B'; read x")));
        QTRY_VERIFY(screenText(session).contains(QLatin1Char('A')));
        QCOMPARE(session.programStatus().value(QStringLiteral("state")).toString(), QStringLiteral("working"));
        QCOMPARE(session.programStatus().value(QStringLiteral("progress")).toInt(), 30);
        QVERIFY(session.sendText(QStringLiteral("\n")));
        // A new shell prompt means the work is over.
        QTRY_VERIFY(screenText(session).contains(QLatin1Char('B')));
        QVERIFY(session.programStatus().isEmpty());
        session.stop();
        QTRY_VERIFY(!session.running());
    }
    void colourSchemeChangeIsAnnounced()
    {
        const QByteArray expected("\033[?997;2n");
        QCOMPARE(capture(QByteArray("\033[?2031h"), expected, [](TerminalSession &session) {
            session.setColors(QColor(Qt::black), QColor(Qt::white));
        }), expected);
    }
    void childSeesThisTerminal()
    {
        qputenv("KONSOLE_VERSION", "250000");
        qputenv("TERM_PROGRAM", "vscode");
        qputenv("TMUX", "/tmp/tmux-1000/default,1,0");
        TerminalSession session;
        QVERIFY(session.resize(4, 70));
        session.setCellSize(9, 18);
        QVERIFY(shell(session, QStringLiteral(
            "echo \"[$TERM|$COLORTERM|$TERM_PROGRAM|$KONSOLE_VERSION|$TMUX]\"; stty size; read x")));
        QTRY_VERIFY(screenText(session).contains(QStringLiteral("4 70")));
        QVERIFY2(screenText(session).contains(QStringLiteral("[xterm-256color|truecolor|tst_terminal_agents||]")),
                 qPrintable(screenText(session)));
        session.stop();
        QTRY_VERIFY(!session.running());
    }
    void cellSizeIsReported()
    {
        TerminalSession session;
        session.setCellSize(9, 18);
        QVERIFY(session.resize(5, 60));
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("input"));
        const QByteArray expected("\033[6;18;9t\033[4;90;540t");
        QVERIFY(session.start(QStringLiteral("/usr/bin/python3"),
            {QFINDTESTDATA("../fixtures/terminal/probe.py"), QString::fromLatin1(QByteArray("\033[16t\033[14t").toHex()),
             QString::number(expected.size()), path}));
        QTRY_VERIFY_WITH_TIMEOUT(!session.running(), 6000);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), expected);
    }
};

QTEST_GUILESS_MAIN(TestTerminalAgents)
#include "tst_terminal_agents.moc"
