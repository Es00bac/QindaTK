// SPDX-License-Identifier: LGPL-3.0-or-later
// Pure parts of the terminal protocol layer: the stream tokenizer, the kitty
// and modifyOtherKeys key encoders, and the OSC 7501 parser and record store.
#include "terminal_keyboard_p.h"
#include "terminal_program_status_p.h"
#include "terminal_stream_filter_p.h"

#include <QtTest>

using namespace QindaTK;
using TerminalProtocol::Sequence;
using TerminalProtocol::StreamFilter;

namespace {
// Renders what the filter delivered as one comparable line per event.
class Recorder : public StreamFilter::Sink {
public:
    void text(QByteArrayView bytes) override
    {
        if (!events.isEmpty() && events.last().startsWith("T:")) events.last() += bytes;
        else events.append(QByteArrayLiteral("T:") + bytes);
    }
    void sequence(const Sequence &s) override
    {
        QByteArray event;
        switch (s.kind) {
        case Sequence::Csi:
            event = QByteArrayLiteral("CSI ") + (s.leader ? QByteArray(1, s.leader) : QByteArray()) + '|'
                + s.parameters + '|' + s.intermediates + '|' + s.final;
            break;
        case Sequence::Osc:
            event = QByteArrayLiteral("OSC ") + QByteArray::number(s.command) + '|' + s.payload
                + (s.bell ? "|BEL" : "|ST");
            break;
        case Sequence::Dcs: event = QByteArrayLiteral("DCS ") + s.payload; break;
        case Sequence::Reset: event = QByteArrayLiteral("RESET"); break;
        }
        events.append(event);
        raw += s.raw;
    }
    bool wantsOsc(int command) const override { return command == 11 || command == 52 || command == 7501; }
    QList<QByteArray> events;
    QByteArray raw;
};

QList<QByteArray> tokenize(const QByteArray &input, qsizetype chunk)
{
    Recorder recorder;
    StreamFilter filter;
    for (qsizetype offset = 0; offset < input.size(); offset += chunk)
        filter.feed(QByteArrayView(input).sliced(offset, qMin(chunk, input.size() - offset)), recorder);
    return recorder.events;
}
} // namespace

class TestTerminalProtocol : public QObject {
    Q_OBJECT
private slots:
    void filterTokenizes_data()
    {
        QTest::addColumn<QByteArray>("input");
        QTest::addColumn<QList<QByteArray>>("expected");
        const auto row = [](const char *name, const QByteArray &input, const QList<QByteArray> &expected) {
            QTest::newRow(name) << input << expected;
        };
        row("plain text and utf8", QByteArrayLiteral("a\xe2\x94\x80z\r\n"), {"T:a\xe2\x94\x80z\r\n"});
        row("csi with leader", QByteArrayLiteral("x\x1b[?2026hy"), {"T:x", "CSI ?|2026||h", "T:y"});
        row("csi with intermediate", QByteArrayLiteral("\x1b[?2026$p\x1b[1 q"),
            {"CSI ?|2026|$|p", "CSI |1| |q"});
        row("sgr with subparameters", QByteArrayLiteral("\x1b[1;4:3;38;2;1;2;3m"), {"CSI |1;4:3;38;2;1;2;3||m"});
        row("wanted osc, both terminators", QByteArrayLiteral("\x1b]11;?\x07\x1b]7501;state=idle\x1b\\"),
            {"OSC 11|?|BEL", "OSC 7501|state=idle|ST"});
        row("wanted osc without payload", QByteArrayLiteral("\x1b]11\x07"), {"OSC 11||BEL"});
        row("other osc stays text", QByteArrayLiteral("\x1b]0;title [1m\x07ok"), {"T:\x1b]0;title [1m\x07ok"});
        row("escape inside a passed string", QByteArrayLiteral("\x1b]2;t\x1b\\\x1b[5n"),
            {"T:\x1b]2;t\x1b\\", "CSI |5||n"});
        row("termcap request", QByteArrayLiteral("\x1bP+q4d73\x1b\\"), {"DCS 4d73"});
        row("other dcs stays text", QByteArrayLiteral("\x1bP$qm\x1b\\"), {"T:\x1bP$qm\x1b\\"});
        row("apc stays text", QByteArrayLiteral("\x1b_Gi=31;AAAA\x1b\\\x1b[c"), {"T:\x1b_Gi=31;AAAA\x1b\\", "CSI |||c"});
        row("two byte escapes", QByteArrayLiteral("\x1b" "7\x1b(B\x1b" "8"), {"T:\x1b" "7\x1b(B\x1b" "8"});
        row("full reset", QByteArrayLiteral("a\x1b" "cb"), {"T:a", "RESET", "T:b"});
        row("control inside csi is left to libvterm", QByteArrayLiteral("\x1b[1\n;2H"), {"T:\x1b[1\n;2H"});
        row("escape restarts a sequence", QByteArrayLiteral("\x1b[12\x1b[3m"), {"T:\x1b[12", "CSI |3||m"});
        row("unterminated osc is dropped by a new escape", QByteArrayLiteral("\x1b]52;c;QUJD\x1b[0m"),
            {"CSI |0||m"});
        row("cancelled osc", QByteArrayLiteral("\x1b]7501;state\x18z"), {"T:z"});
    }
    void filterTokenizes()
    {
        QFETCH(QByteArray, input);
        QFETCH(QList<QByteArray>, expected);
        // The split point of a read must never change what is recognised.
        for (const qsizetype chunk : {qsizetype(4096), qsizetype(1), qsizetype(2), qsizetype(3)})
            QCOMPARE(tokenize(input, chunk), expected);
    }
    void filterBoundsStrings()
    {
        Recorder recorder;
        StreamFilter filter;
        const QByteArray huge = QByteArrayLiteral("\x1b]7501;") + QByteArray(StreamFilter::maxOscBytes + 10, 'a')
            + QByteArrayLiteral("\x07" "after");
        filter.feed(huge, recorder);
        QCOMPARE(recorder.events, QList<QByteArray>{"T:after"});
        recorder.events.clear();
        const QByteArray clipboard = QByteArrayLiteral("\x1b]52;c;") + QByteArray(100000, 'A') + '\x07';
        filter.feed(clipboard, recorder);
        QCOMPARE(recorder.events.size(), 1);
        QVERIFY(recorder.events.first().startsWith("OSC 52|c;AAAA"));
    }

    void kittyKeys_data()
    {
        QTest::addColumn<int>("flags");
        QTest::addColumn<int>("key");
        QTest::addColumn<int>("modifiers");
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("type");
        QTest::addColumn<bool>("handled");
        QTest::addColumn<QByteArray>("expected");
        const auto row = [](const char *name, int flags, int key, int modifiers, const char *text, int type,
                            bool handled, const char *expected) {
            QTest::newRow(name) << flags << key << modifiers << QString::fromUtf8(text) << type << handled
                                << QByteArray(expected);
        };
        const int shift = Qt::ShiftModifier, ctrl = Qt::ControlModifier, alt = Qt::AltModifier;
        row("enter", 1, Qt::Key_Return, 0, "\r", 1, true, "\r");
        row("shift enter", 1, Qt::Key_Return, shift, "\r", 1, true, "\x1b[13;2u");
        row("ctrl enter", 1, Qt::Key_Return, ctrl, "\r", 1, true, "\x1b[13;5u");
        row("alt enter", 1, Qt::Key_Return, alt, "\r", 1, true, "\x1b[13;3u");
        row("tab", 1, Qt::Key_Tab, 0, "\t", 1, true, "\t");
        row("backtab", 1, Qt::Key_Backtab, shift, "", 1, true, "\x1b[9;2u");
        row("backspace", 1, Qt::Key_Backspace, 0, "\b", 1, true, "\x7f");
        row("ctrl backspace", 1, Qt::Key_Backspace, ctrl, "", 1, true, "\x1b[127;5u");
        row("escape", 1, Qt::Key_Escape, 0, "\x1b", 1, true, "\x1b[27u");
        row("ctrl c", 1, Qt::Key_C, ctrl, "\x03", 1, true, "\x1b[99;5u");
        row("ctrl shift a", 1, Qt::Key_A, ctrl | shift, "\x01", 1, true, "\x1b[97;6u");
        row("ctrl shift a with alternates", 5, Qt::Key_A, ctrl | shift, "\x01", 1, true, "\x1b[97:65;6u");
        row("alt x", 1, Qt::Key_X, alt, "x", 1, true, "\x1b[120;3u");
        row("ctrl j", 1, Qt::Key_J, ctrl, "\n", 1, true, "\x1b[106;5u");
        row("shifted letter is text", 1, Qt::Key_A, shift, "A", 1, true, "A");
        row("unicode text", 1, 0xe9, 0, "\xc3\xa9", 1, true, "\xc3\xa9");
        row("altgr text is not a shortcut", 1, Qt::Key_At, ctrl | alt | int(Qt::GroupSwitchModifier), "@", 1, true, "@");
        row("up", 1, Qt::Key_Up, 0, "", 1, true, "\x1b[A");
        row("ctrl up", 1, Qt::Key_Up, ctrl, "", 1, true, "\x1b[1;5A");
        row("delete", 1, Qt::Key_Delete, 0, "", 1, true, "\x1b[3~");
        row("shift f5", 1, Qt::Key_F5, shift, "", 1, true, "\x1b[15;2~");
        row("f3", 1, Qt::Key_F3, 0, "", 1, true, "\x1b[13~");
        row("f13", 1, Qt::Key_F13, 0, "", 1, true, "\x1b[57376u");
        row("keypad enter", 1, Qt::Key_Enter, int(Qt::KeypadModifier), "\r", 1, true, "\x1b[57414u");
        row("keypad digit types", 1, Qt::Key_5, int(Qt::KeypadModifier), "5", 1, true, "5");
        row("modifier key alone", 1, Qt::Key_Shift, shift, "", 1, true, "");
        row("release unreported", 1, Qt::Key_Up, 0, "", 3, true, "");
        row("repeat with events", 3, Qt::Key_Up, 0, "", 2, true, "\x1b[1;1:2A");
        row("release with events", 3, Qt::Key_Up, ctrl, "", 3, true, "\x1b[1;5:3A");
        row("release of ctrl c", 3, Qt::Key_C, ctrl, "", 3, true, "\x1b[99;5:3u");
        row("enter release stays silent", 3, Qt::Key_Return, shift, "", 3, true, "");
        row("text release stays silent", 3, Qt::Key_A, 0, "a", 3, true, "");
        row("all keys: letter", 8, Qt::Key_A, 0, "a", 1, true, "\x1b[97u");
        row("all keys: enter", 8, Qt::Key_Return, 0, "\r", 1, true, "\x1b[13u");
        row("all keys: shift alone", 8, Qt::Key_Shift, shift, "", 1, true, "\x1b[57441;2u");
        row("all keys: keypad digit", 8, Qt::Key_5, int(Qt::KeypadModifier), "5", 1, true, "\x1b[57404u");
        row("associated text", 24, Qt::Key_A, shift, "A", 1, true, "\x1b[97;2;65u");
        row("associated text without modifiers", 24, Qt::Key_A, 0, "a", 1, true, "\x1b[97;;97u");
        row("enter release with all keys", 10, Qt::Key_Return, 0, "", 3, true, "\x1b[13;1:3u");
        row("extras only keep legacy presses", 2, Qt::Key_Up, 0, "", 1, false, "");
        row("extras only swallow releases", 2, Qt::Key_Up, 0, "", 3, true, "");
        row("unknown key", 1, Qt::Key_VolumeUp, 0, "", 1, false, "");
    }
    void kittyKeys()
    {
        QFETCH(int, flags); QFETCH(int, key); QFETCH(int, modifiers); QFETCH(QString, text);
        QFETCH(int, type); QFETCH(bool, handled); QFETCH(QByteArray, expected);
        const auto encoding = TerminalKeyboard::encodeKitty(flags, key, modifiers, text, type);
        QCOMPARE(encoding.handled, handled);
        QCOMPARE(encoding.bytes, expected);
    }
    void modifyOtherKeys_data()
    {
        QTest::addColumn<int>("level");
        QTest::addColumn<int>("key");
        QTest::addColumn<int>("modifiers");
        QTest::addColumn<QByteArray>("expected");
        const int shift = Qt::ShiftModifier, ctrl = Qt::ControlModifier, alt = Qt::AltModifier;
        QTest::newRow("off") << 0 << int(Qt::Key_Return) << shift << QByteArray();
        QTest::newRow("shift enter") << 1 << int(Qt::Key_Return) << shift << QByteArray("\x1b[27;2;13~");
        QTest::newRow("ctrl enter") << 1 << int(Qt::Key_Return) << ctrl << QByteArray("\x1b[27;5;13~");
        QTest::newRow("alt enter stays escape cr") << 1 << int(Qt::Key_Return) << alt << QByteArray();
        QTest::newRow("ctrl tab") << 1 << int(Qt::Key_Tab) << ctrl << QByteArray("\x1b[27;5;9~");
        QTest::newRow("ctrl shift tab") << 1 << int(Qt::Key_Backtab) << (ctrl | shift) << QByteArray("\x1b[27;6;9~");
        QTest::newRow("shift tab stays") << 1 << int(Qt::Key_Backtab) << shift << QByteArray();
        QTest::newRow("ctrl c stays at 1") << 1 << int(Qt::Key_C) << ctrl << QByteArray();
        QTest::newRow("ctrl shift c") << 1 << int(Qt::Key_C) << (ctrl | shift) << QByteArray("\x1b[27;6;67~");
        QTest::newRow("ctrl comma") << 1 << int(Qt::Key_Comma) << ctrl << QByteArray("\x1b[27;5;44~");
        QTest::newRow("ctrl bracket stays") << 1 << int(Qt::Key_BracketLeft) << ctrl << QByteArray();
        QTest::newRow("ctrl c at 2") << 2 << int(Qt::Key_C) << ctrl << QByteArray("\x1b[27;5;99~");
        QTest::newRow("alt x at 2") << 2 << int(Qt::Key_X) << alt << QByteArray("\x1b[27;3;120~");
        QTest::newRow("plain letter") << 2 << int(Qt::Key_X) << 0 << QByteArray();
    }
    void modifyOtherKeys()
    {
        QFETCH(int, level); QFETCH(int, key); QFETCH(int, modifiers); QFETCH(QByteArray, expected);
        const auto encoding = TerminalKeyboard::encodeModifyOtherKeys(level, key, modifiers, {});
        QCOMPARE(encoding.handled, !expected.isEmpty());
        QCOMPARE(encoding.bytes, expected);
    }

    void statusParses_data()
    {
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<QString>("expected"); // type|id|state|kind|app|progress|title|message
        const auto row = [](const char *body, const char *expected) {
            QTest::newRow(body) << QByteArray(body) << QString::fromUtf8(expected);
        };
        const char *discard = "discard";
        row("state=blocked:kind=permission:app=terraform:msg=QXBwbHkgMyB0byBhZGQsIDEgdG8gY2hhbmdlLCAwIHRvIGRlc3Ryb3k/",
            "status||blocked|permission|terraform|-1||Apply 3 to add, 1 to change, 0 to destroy?");
        row(" state = working : progress = 40 ", "status||working|||40||");
        row("\tstate\t=\tidle\t", "status||idle|||-1||");
        row("state=idle:app=my app", "status||idle|||-1||");
        row("state=idle:garbage:app=x", "status||idle||x|-1||");
        row("state=idle:=x:app=x", "status||idle||x|-1||");
        row("state=idle:App=x", "status||idle|||-1||");
        row("state=idle:app2=x", "status||idle|||-1||");
        row("::state=idle::", "status||idle|||-1||");
        row("state=done:msg=hello world", "status||done|||-1||");
        row("state=idle:app=a;b", "status||idle|||-1||");
        row("state=idle!", discard);
        row("state=idle:state=done", "status||done|||-1||");
        row("state=idle:state=paused", discard);
        row("state=idle:app=a:app=b", "status||idle||b|-1||");
        row("state=idle:msg=b25l:msg=dHdv", "status||idle|||-1||two");
        row("state=idle:msg=A:msg=b2s=", "status||idle|||-1||ok");
        row("state=idle:id=a//b:id=ok", discard);
        row("state=idle:msg=SGk=", "status||idle|||-1||Hi");
        row("state=idle:msg=SGk", "status||idle|||-1||Hi");
        row("state=idle:msg=SA", "status||idle|||-1||H");
        row("state=idle:msg=", "status||idle|||-1||");
        row("state=idle:title=", "status||idle|||-1||");
        for (const char *bad : {"state=idle:msg=SGk=S", "state=idle:msg=S", "state=idle:msg=SGk==",
                                "state=idle:msg=SG=k", "state=idle:msg=-_8=", "state=idle:title=A",
                                "state=idle:msg=QR==", "state=idle:msg=QUJ", "state=idle:msg=//4=",
                                "state=done:msg=YQpi", "state=done:msg=YQli", "state=done:msg=YQBi",
                                "state=done:msg=YRti", "state=done:msg=YX9i", "state=done:msg=YcKFYg==",
                                "state=done:msg=YcKbYg==", "state=done:msg=YR9i", "state=done:msg=YcKfYg==",
                                "state=done:title=YQo="})
            row(bad, discard);
        row("state=done:msg=YcKgYg==", "status||done|||-1||a\xc2\xa0" "b");
        // A direction override is accepted by the protocol and disarmed for display.
        row("state=done:msg=YeKArmI=", "status||done|||-1||ab");
        row("state=working:id=build/test", "status|build/test|working|||-1||");
        row("state=idle:id=Az09_.+-/x", "status|Az09_.+-/x|idle|||-1||");
        for (const char *bad : {"state=idle:id=a//b", "state=idle:id=/a", "state=idle:id=a/", "state=idle:id=a,b",
                                "state=idle:id=a=b", "state=idle:id=/", "state=idle:id=a b",
                                "state=idle:id=caf\xc3\xa9", "state=idle:id=", "app=x", "state=", "state=paused",
                                "state=IDLE", "", "state=clear:id=a//b", "state=clear:msg=S",
                                "state=idle:KKKKKKKKKKKKKKKKK=1", "state=idle:kkkkkkkkkkkkkkkkk=1",
                                "state=idle:app=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                                "state=idle:app=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa!"})
            row(bad, discard);
        row("state=idle:kkkkkkkkkkkkkkkk=1", "status||idle|||-1||");
        row("state=idle:app=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "status||idle||aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa|-1||");
        row("state=idle:app=a!", "status||idle|||-1||");
        row("state=clear", "clear|||||-1||");
        row("state=clear:id=us-east", "clear|us-east||||-1||");
        row("state=clear:app=x:progress=4", "clear|||||-1||");
        row("state=working:kind=auth", "status||working|||-1||");
        row("state=blocked:kind=payment", "status||blocked|||-1||");
        row("state=blocked:kind=Auth", "status||blocked|||-1||");
        row("state=blocked:kind=question:progress=50", "status||blocked|question||50||");
        row("state=working:progress=0", "status||working|||0||");
        row("state=working:progress=100", "status||working|||100||");
        row("state=working:progress=007", "status||working|||7||");
        row("state=idle:progress=40", "status||idle|||-1||");
        for (const char *absent : {"101", "-1", "+5", "4.5", "", "abc", "1e2", "99999999999999999999"})
            QTest::newRow(qPrintable(QStringLiteral("progress %1").arg(QLatin1String(absent))))
                << QByteArray("state=working:progress=") + absent << QStringLiteral("status||working|||-1||");
        row("state=idle:app=a/b", "status||idle|||-1||");
        row("state=idle:app=Az09_.+-", "status||idle||Az09_.+-|-1||");
        row("?", "query|||||-1||");
        row(" ? ", "query|||||-1||");
        row("?:states=idle,done", "query|||||-1||");
    }
    void statusParses()
    {
        QFETCH(QByteArray, body);
        QFETCH(QString, expected);
        const auto report = ProgramStatus::parse(body, body.size() + 9);
        static const char *types[] = {"discard", "query", "clear", "status"};
        QString actual = QLatin1String(types[report.type]);
        if (report.type != ProgramStatus::Report::Discard)
            actual += QStringLiteral("|%1|%2|%3|%4|%5|%6|%7").arg(QLatin1String(report.id), QLatin1String(report.state),
                QLatin1String(report.kind), QLatin1String(report.app)).arg(report.progress).arg(report.title, report.message);
        QCOMPARE(actual, expected);
    }
    void statusLimits()
    {
        using ProgramStatus::Report;
        const auto message = [](int bytes) {
            return QByteArrayLiteral("state=done:msg=") + QByteArray(bytes, 'a').toBase64();
        };
        QCOMPARE(ProgramStatus::parse(message(2048), 100).type, Report::Status);
        QCOMPARE(ProgramStatus::parse(message(2049), 100).type, Report::Discard);
        const auto title = [](int bytes) {
            return QByteArrayLiteral("state=done:title=") + QByteArray(bytes, 'a').toBase64();
        };
        QCOMPARE(ProgramStatus::parse(title(192), 100).type, Report::Status);
        QCOMPARE(ProgramStatus::parse(title(193), 100).type, Report::Discard);
        QCOMPARE(ProgramStatus::parse("state=idle", 4097).type, Report::Discard);
        QCOMPARE(ProgramStatus::parse("state=idle:id=" + QByteArray(32, 'a'), 100).type, Report::Status);
        QCOMPARE(ProgramStatus::parse("state=idle:id=" + QByteArray(33, 'a'), 100).type, Report::Discard);
        QCOMPARE(ProgramStatus::parse("state=idle:id=a/b/c/d/e/f/g/h", 100).type, Report::Status);
        QCOMPARE(ProgramStatus::parse("state=idle:id=a/b/c/d/e/f/g/h/i", 100).type, Report::Discard);
    }
    void statusStore()
    {
        ProgramStatus::Store store;
        const auto apply = [&store](const char *body) { return store.apply(ProgramStatus::parse(body, 64)); };
        const auto states = [&store] {
            QStringList list;
            for (const auto &record : store.records()) {
                const auto map = record.toMap();
                list.append(map.value(QStringLiteral("id")).toString() + QLatin1Char('=')
                            + map.value(QStringLiteral("state")).toString());
            }
            list.sort();
            return list.join(QLatin1Char(' '));
        };
        QVERIFY(store.summary().isEmpty());
        // A report replaces its record completely.
        QVERIFY(apply("state=working:app=brew:progress=10:msg=SGk="));
        QVERIFY(apply("state=done"));
        QCOMPARE(store.summary().value(QStringLiteral("app")).toString(), QString());
        QCOMPARE(store.summary().value(QStringLiteral("message")).toString(), QString());
        // Children inherit the nearest ancestor's app; a parent need not exist.
        QVERIFY(apply("state=idle:app=deploy"));
        QVERIFY(apply("state=working:id=us/a:app=pusher"));
        QVERIFY(apply("state=blocked:id=us/a/b:kind=question"));
        QVERIFY(apply("state=working:id=eu/x"));
        QHash<QString, QString> apps;
        for (const auto &record : store.records())
            apps.insert(record.toMap().value(QStringLiteral("id")).toString(),
                        record.toMap().value(QStringLiteral("app")).toString());
        QCOMPARE(apps.value(QStringLiteral("us/a/b")), QStringLiteral("pusher"));
        QCOMPARE(apps.value(QStringLiteral("eu/x")), QStringLiteral("deploy"));
        // One indicator shows what most needs the user.
        QCOMPARE(store.summary().value(QStringLiteral("state")).toString(), QStringLiteral("blocked"));
        QCOMPARE(store.summary().value(QStringLiteral("kind")).toString(), QStringLiteral("question"));
        QCOMPARE(store.summary().value(QStringLiteral("records")).toInt(), 4);
        // Clearing a record takes everything beneath it and nothing beside it.
        QVERIFY(apply("state=idle:id=usa"));
        QVERIFY(apply("state=clear:id=us"));
        QCOMPARE(states(), QStringLiteral("=idle eu/x=working usa=idle"));
        QVERIFY(!apply("state=clear:id=missing"));
        QVERIFY(!apply("state=paused"));
        // A prompt or exit ends live work; finished work waits to be seen.
        QVERIFY(apply("state=error:id=job"));
        QVERIFY(store.dropTransient(false));
        QCOMPARE(states(), QStringLiteral("=idle job=error usa=idle"));
        QVERIFY(store.dropTransient(true));
        QCOMPARE(states(), QStringLiteral("job=error"));
        QVERIFY(store.acknowledge());
        QCOMPARE(store.size(), 0);
        // At capacity the least recently updated record makes room.
        for (int index = 0; index < ProgramStatus::Store::capacity; ++index)
            QVERIFY(apply(qPrintable(QStringLiteral("state=idle:id=r%1").arg(index))));
        QVERIFY(apply("state=done:id=r0"));
        QVERIFY(apply("state=idle:id=new"));
        QCOMPARE(store.size(), ProgramStatus::Store::capacity);
        QVERIFY(states().contains(QStringLiteral("r0=done")));
        QVERIFY(!states().contains(QStringLiteral("r1=idle ")));
        QVERIFY(store.clear());
        QVERIFY(!store.clear());
    }
};

QTEST_GUILESS_MAIN(TestTerminalProtocol)
#include "tst_terminal_protocol.moc"
