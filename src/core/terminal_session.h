// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QSocketNotifier>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <vterm.h>

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
    Q_PROPERTY(int maxScrollback READ maxScrollback WRITE setMaxScrollback NOTIFY screenChanged)
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
    QStringList scrollback() const { return m_scrollback; }
    int maxScrollback() const { return m_maxScrollback; }
    void setMaxScrollback(int value);

    Q_INVOKABLE bool start(const QString &program, const QStringList &arguments = {},
                           const QString &workingDirectory = {});
    Q_INVOKABLE bool sendText(const QString &text);
    Q_INVOKABLE bool resize(int rows, int columns);
    Q_INVOKABLE void stop();
    Q_INVOKABLE QString copyText(int firstRow, int lastRow) const;
signals:
    void screenChanged();
    void processChanged();
    void exited(int exitCode);
private:
    static int pushScrollback(int columns, const VTermScreenCell *cells, void *user);
    static int clearScrollback(void *user);
    static int cursorMoved(VTermPos pos, VTermPos oldPos,
                           int visible, void *user);
    static void outputToPty(const char *bytes, size_t length, void *user);
    void readAvailable();
    bool flushInput();
    void pollProcess();
    void rebuildScreen();
    void closePty();
    void setError(const QString &message);
    static QString cellText(const VTermScreenCell &cell);

    VTerm *m_vterm = nullptr;
    VTermScreen *m_vscreen = nullptr;
    VTermScreenCallbacks m_callbacks {};
    QSocketNotifier *m_readNotifier = nullptr;
    QSocketNotifier *m_writeNotifier = nullptr;
    QTimer m_pollTimer;
    QTimer m_killTimer;
    QByteArray m_pendingInput;
    QStringList m_scrollback;
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
};

} // namespace QindaTK
