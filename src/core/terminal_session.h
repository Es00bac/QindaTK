// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QColor>
#include <deque>
#include <QSocketNotifier>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
// AGENT-CONTRACT (D-354, src/CMakeLists.txt): QINDATK_NO_LIBVTERM is a PUBLIC
// compile definition of qindatk when it was built without libvterm (Android).
// The Q_PROPERTY/Q_INVOKABLE surface is identical either way; only the private
// libvterm state differs, and terminal_session_unavailable.cpp implements it.
#ifndef QINDATK_NO_LIBVTERM
#include <vterm.h>
struct winsize;
#endif

namespace QindaTK {

// AGENT-CONTRACT: the application owns the decision to start a local process;
// TerminalSession never starts a shell when instantiated by QML.
class TerminalSession : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool running READ running NOTIFY processChanged)
    Q_PROPERTY(int exitCode READ exitCode NOTIFY processChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY processChanged)
    Q_PROPERTY(int rows READ rows NOTIFY screenChanged)
    Q_PROPERTY(int columns READ columns NOTIFY screenChanged)
    Q_PROPERTY(int cursorRow READ cursorRow NOTIFY screenChanged)
    Q_PROPERTY(int cursorColumn READ cursorColumn NOTIFY screenChanged)
    Q_PROPERTY(bool cursorVisible READ cursorVisible NOTIFY screenChanged)
    Q_PROPERTY(QVariantList screen READ screen NOTIFY screenChanged)
    Q_PROPERTY(QStringList scrollback READ scrollback NOTIFY screenChanged)
    Q_PROPERTY(int historyColumns READ historyColumns NOTIFY screenChanged)
    Q_PROPERTY(bool alternateScreen READ alternateScreen NOTIFY screenChanged)
    Q_PROPERTY(bool reverseScreen READ reverseScreen NOTIFY screenChanged)
    Q_PROPERTY(bool cursorBlink READ cursorBlink NOTIFY screenChanged)
    Q_PROPERTY(int cursorShape READ cursorShape NOTIFY screenChanged)
    Q_PROPERTY(int mouseMode READ mouseMode NOTIFY screenChanged)
    Q_PROPERTY(QString title READ title NOTIFY screenChanged)
    Q_PROPERTY(qulonglong scrollbackOffset READ scrollbackOffset NOTIFY screenChanged)
    Q_PROPERTY(bool foregroundProcessRunning READ foregroundProcessRunning NOTIFY processChanged)
    Q_PROPERTY(QString workingDirectory READ workingDirectory NOTIFY processChanged)
    Q_PROPERTY(int maxScrollback READ maxScrollback WRITE setMaxScrollback NOTIFY screenChanged)
    // Negotiated by the program: bracketed paste (mode 2004) and the kitty
    // keyboard protocol's progressive-enhancement flags for the active screen.
    Q_PROPERTY(bool bracketedPaste READ bracketedPaste NOTIFY modesChanged)
    Q_PROPERTY(int keyboardFlags READ keyboardFlags NOTIFY modesChanged)
    // Mode 1007: on the alternate screen, a host turns the wheel into arrow
    // keys unless the program reads the mouse or switched this off.
    Q_PROPERTY(bool alternateScroll READ alternateScroll NOTIFY modesChanged)
    // "#rrggbb" set by the program with OSC 12, or empty for the host's own.
    Q_PROPERTY(QString cursorColor READ cursorColor NOTIFY screenChanged)
    // The OSC 7501 record a single indicator should show, or an empty map:
    // state, kind, app, title, message, progress (-1 when unknown), records.
    Q_PROPERTY(QVariantMap programStatus READ programStatus NOTIFY programStatusChanged)
public:
    explicit TerminalSession(QObject *parent = nullptr);
    ~TerminalSession() override;

    bool running() const { return m_pid > 0; }
    int exitCode() const { return m_exitCode; }
    QString errorString() const { return m_error; }
    int rows() const { return m_rows; }
    int columns() const { return m_columns; }
    int cursorRow() const { return m_cursorRow; }
    int cursorColumn() const { return m_cursorColumn; }
    bool cursorVisible() const { return m_cursorVisible; }
    QVariantList screen() const { return m_screenRows; }
    QStringList scrollback() const { return m_alternateScreen ? QStringList{} : m_scrollback; }
    bool alternateScreen() const { return m_alternateScreen; }
    bool reverseScreen() const { return m_reverseScreen; }
    bool cursorBlink() const { return m_cursorBlink; }
    int cursorShape() const { return m_cursorShape; }
    int mouseMode() const { return m_mouseMode; }
    QString title() const { return m_title; }
    qulonglong scrollbackOffset() const { return m_scrollbackOffset; }
    bool foregroundProcessRunning() const;
    QString workingDirectory() const;
    int historyColumns() const;
    int maxScrollback() const { return m_maxScrollback; }
    void setMaxScrollback(int value);
    bool bracketedPaste() const;
    int keyboardFlags() const;
    bool alternateScroll() const;
    QString cursorColor() const;
    QVariantMap programStatus() const;

    Q_INVOKABLE bool start(const QString &program, const QStringList &arguments = {},
                           const QString &workingDirectory = {});
    Q_INVOKABLE bool sendText(const QString &text);
    // Qt key/modifier values are translated by libvterm, which owns negotiated
    // application-cursor/keypad, bracketed-paste and mouse protocol modes.
    Q_INVOKABLE bool sendKey(int key, int modifiers = 0, const QString &text = {});
    // The same, for hosts that also see repeats and releases: eventType is
    // 1 press, 2 repeat, 3 release. Releases are only reported to programs
    // that asked for them; true means the event was consumed.
    Q_INVOKABLE bool sendKeyEvent(int key, int modifiers, const QString &text, int eventType);
    Q_INVOKABLE bool pasteText(const QString &text);
    Q_INVOKABLE bool sendMouse(int row, int column, int button, bool pressed,
                               int modifiers = 0);
    Q_INVOKABLE void setFocused(bool focused);
    Q_INVOKABLE void setColors(const QColor &foreground, const QColor &background,
                               const QVariantList &palette = {});
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE QVariantMap rowData(int row) const;
    Q_INVOKABLE QString copyTextRange(int firstRow, int firstColumn,
                                      int lastRow, int lastColumn) const;
    Q_INVOKABLE bool resize(int rows, int columns);
    Q_INVOKABLE void stop();
    Q_INVOKABLE QString copyText(int firstRow, int lastRow) const;
    // Cell size in pixels, for programs that ask (CSI 14 t, CSI 16 t).
    Q_INVOKABLE void setCellSize(int width, int height);
    Q_INVOKABLE QVariantList programStatusRecords() const;
    // The user has seen the finished work: drops done and error records.
    Q_INVOKABLE void acknowledgeProgramStatus();
signals:
    void screenChanged();
    void processChanged();
    void exited(int exitCode);
    void bell();
    void modesChanged();
    void programStatusChanged();
    // AGENT-CONTRACT: untrusted requests from the program in the terminal.
    // The host decides whether to honour them; text is free of controls but
    // must still be shown as plain text and never interpreted.
    void clipboardWriteRequested(const QString &text, bool primarySelection);
    void notificationRequested(const QString &title, const QString &body);
private:
#ifndef QINDATK_NO_LIBVTERM
    static int pushScrollback(int columns, const VTermScreenCell *cells, void *user);
    static int popScrollback(int columns, VTermScreenCell *cells, void *user);
    static int termProperty(VTermProp prop, VTermValue *value, void *user);
    static int ringBell(void *user);
    static int clearScrollback(void *user);
    static int cursorMoved(VTermPos pos, VTermPos oldPos,
                           int visible, void *user);
    static void outputToPty(const char *bytes, size_t length, void *user);
    bool readAvailable(int budget = 262144);
    void updateProcessInfo();
    bool flushInput();
    void pollProcess();
    void rebuildScreen();
    void closePty();
    void setError(const QString &message);
    static QString cellText(const VTermScreenCell &cell);
    QList<VTermScreenCell> rowCells(int row) const;
    QVariantMap formatRow(const QList<VTermScreenCell> &cells, bool includeCells) const;
    QString colorName(VTermColor color, bool foreground) const;
    bool queueInput(const QByteArray &bytes);
    bool sendLegacyKey(int key, int modifiers, const QString &text);
    struct winsize windowSize() const;
    void applyWindowSize();

    VTerm *m_vterm = nullptr;
    VTermScreen *m_vscreen = nullptr;
    VTermScreenCallbacks m_callbacks {};
#endif
    QSocketNotifier *m_readNotifier = nullptr;
    QSocketNotifier *m_writeNotifier = nullptr;
    QTimer m_pollTimer;
    QTimer m_killTimer;
    QByteArray m_pendingInput;
#ifndef QINDATK_NO_LIBVTERM
    // Raw cells preserve wide characters, SGR and palette indices in history.
    // The old plain-text property stays available for existing toolkit clients.
    std::deque<QList<VTermScreenCell>> m_history;
    QList<VTermColor> m_defaultPalette;
#endif
    QStringList m_scrollback;
    qulonglong m_scrollbackOffset = 0;
    QByteArray m_titleBytes;
    QString m_title;
    QString m_workingDirectory;
    int m_exitedStatus = 0;
    bool m_childExited = false;
    bool m_foregroundProcessRunning = false;
    bool m_alternateScreen = false;
    bool m_reverseScreen = false;
    bool m_cursorBlink = true;
#ifndef QINDATK_NO_LIBVTERM
    int m_cursorShape = VTERM_PROP_CURSORSHAPE_BLOCK;
    int m_mouseMode = VTERM_PROP_MOUSE_NONE;
#else
    int m_cursorShape = 1;  // VTERM_PROP_CURSORSHAPE_BLOCK
    int m_mouseMode = 0;    // VTERM_PROP_MOUSE_NONE
#endif
    QVariantList m_screenRows;
    QString m_error;
    int m_master = -1;
    int m_pid = -1;
    int m_exitCode = -1;
    int m_rows = 24;
    int m_columns = 80;
    int m_cursorRow = 0;
    int m_cursorColumn = 0;
    int m_maxScrollback = 2000;
    bool m_cursorVisible = true;
    // AGENT-GUARD: hosts inline the accessors above, so members are only ever
    // appended. Protocol state stays behind this pointer for the same reason.
    class Protocol;
    Protocol *m_protocol = nullptr;
};

} // namespace QindaTK
