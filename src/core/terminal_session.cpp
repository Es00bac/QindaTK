// SPDX-License-Identifier: LGPL-3.0-or-later
#include "terminal_session.h"

#include "terminal_session_protocol_p.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QVariantMap>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <pty.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace QindaTK {
namespace {
constexpr int maxRows = 512;
constexpr int maxColumns = 1024;
constexpr int maxPendingInput = 1024 * 1024;
// How long a destroyed session waits for its program to act on the hangup.
constexpr int hangupGraceMs = 500;

// Set by other terminals for the programs they start.
constexpr const char *foreignTerminalVariables[] = {
    "TERM_PROGRAM", "TERM_PROGRAM_VERSION", "TERMINFO", "TERMCAP", "COLORFGBG", "LINES", "COLUMNS",
    "WINDOWID", "KONSOLE_VERSION", "KONSOLE_DBUS_SERVICE", "KONSOLE_DBUS_SESSION",
    "KONSOLE_DBUS_WINDOW", "VTE_VERSION", "KITTY_WINDOW_ID", "KITTY_PID", "KITTY_LISTEN_ON",
    "KITTY_INSTALLATION_DIR", "WEZTERM_EXECUTABLE", "WEZTERM_PANE", "WEZTERM_UNIX_SOCKET",
    "ALACRITTY_WINDOW_ID", "ALACRITTY_SOCKET", "ALACRITTY_LOG", "GHOSTTY_RESOURCES_DIR",
    "GHOSTTY_BIN_DIR", "ITERM_SESSION_ID", "LC_TERMINAL", "LC_TERMINAL_VERSION", "WT_SESSION",
    "WT_PROFILE_ID", "TERMINATOR_UUID", "TILIX_ID", "GNOME_TERMINAL_SCREEN",
    "GNOME_TERMINAL_SERVICE", "TMUX", "TMUX_PANE", "STY", "ZELLIJ", "ZELLIJ_SESSION_NAME",
};

QByteArray sanitizedIdentity(const QString &text)
{
    QByteArray bytes = text.toLatin1();
    bytes.removeIf([](char byte) { return byte <= 0x20 || byte > 0x7e; });
    return bytes.left(64);
}

void signalProcessGroup(int pid, int signal)
{
    if (pid <= 0)
        return;
    if (::kill(-pid, signal) < 0 && errno == ESRCH)
        ::kill(pid, signal);
}

} // namespace

TerminalSession::TerminalSession(QObject *parent) : QObject(parent)
{
    m_pollTimer.setInterval(50);
    connect(&m_pollTimer, &QTimer::timeout, this, &TerminalSession::pollProcess);
    m_killTimer.setSingleShot(true);
    connect(&m_killTimer, &QTimer::timeout, this, [this] {
        if (m_pid > 0 && !m_childExited)
            signalProcessGroup(m_pid, SIGKILL);
    });
    // Created before anything can fail: every accessor may rely on it.
    m_protocol = new Protocol(this);
    m_vterm = vterm_new(m_rows, m_columns);
    if (!m_vterm) {
        setError(QStringLiteral("libvterm could not allocate a terminal"));
        return;
    }
    vterm_set_utf8(m_vterm, 1);
    vterm_output_set_callback(m_vterm, &TerminalSession::outputToPty, this);
    m_vscreen = vterm_obtain_screen(m_vterm);
    m_callbacks.movecursor = &TerminalSession::cursorMoved;
    m_callbacks.settermprop = &TerminalSession::termProperty;
    m_callbacks.bell = &TerminalSession::ringBell;
    m_callbacks.sb_popline = &TerminalSession::popScrollback;
    m_callbacks.sb_pushline = &TerminalSession::pushScrollback;
    m_callbacks.sb_clear = &TerminalSession::clearScrollback;
    vterm_screen_set_callbacks(m_vscreen, &m_callbacks, this);
    vterm_screen_enable_altscreen(m_vscreen, 1);
    vterm_screen_reset(m_vscreen, 1);
    for (int index = 0; index < 16; ++index) {
        VTermColor color{};
        vterm_state_get_palette_color(vterm_obtain_state(m_vterm), index, &color);
        m_defaultPalette.append(color);
    }
    rebuildScreen();
}

TerminalSession::~TerminalSession()
{
    m_pollTimer.stop();
    m_killTimer.stop();
    if (m_pid > 0 && !m_childExited) {
        // AGENT-GUARD: hang up before killing. A program that is killed
        // outright cannot save its session, restore the terminal modes it
        // changed or stop the helpers it started; agent CLIs also record a
        // death in their first seconds as a failed start. Only a program that
        // ignores the hangup is killed, and it is always reaped here.
        signalProcessGroup(m_pid, SIGHUP);
        closePty();
        int status = 0;
        bool reaped = false;
        for (int waited = 0; waited < hangupGraceMs && !reaped; waited += 5) {
            const pid_t result = ::waitpid(m_pid, &status, WNOHANG);
            reaped = result == m_pid || (result < 0 && errno != EINTR);
            if (!reaped) ::usleep(5000);
        }
        if (!reaped) {
            signalProcessGroup(m_pid, SIGKILL);
            while (::waitpid(m_pid, &status, 0) < 0 && errno == EINTR) {}
        }
        m_pid = -1;
    }
    closePty();
    delete m_protocol;
    if (m_vterm)
        vterm_free(m_vterm);
}

void TerminalSession::setError(const QString &message)
{
    m_error = message;
    emit processChanged();
}

void TerminalSession::setMaxScrollback(int value)
{
    value = qBound(0, value, 10000);
    if (m_maxScrollback == value)
        return;
    m_maxScrollback = value;
    while (m_scrollback.size() > m_maxScrollback) {
        m_scrollback.removeFirst();
        m_history.pop_front();
        ++m_scrollbackOffset;
    }
    emit screenChanged();
}

bool TerminalSession::start(const QString &program, const QStringList &arguments,
                            const QString &workingDirectory)
{
    if (running()) {
        setError(QStringLiteral("a terminal process is already running"));
        return false;
    }
    if (!m_vterm || program.isEmpty() || program.contains(QChar::Null)
        || workingDirectory.contains(QChar::Null)
        || arguments.size() > 256 || (workingDirectory.size() && !QDir(workingDirectory).exists())) {
        setError(QStringLiteral("invalid terminal process request"));
        return false;
    }
    QByteArray executable = QFile::encodeName(program);
    QList<QByteArray> encoded;
    encoded.reserve(arguments.size() + 1);
    encoded.append(executable);
    for (const QString &argument : arguments) {
        if (argument.contains(QChar::Null)) {
            setError(QStringLiteral("NUL in process argument"));
            return false;
        }
        encoded.append(argument.toUtf8());
    }
    QList<char *> argv;
    argv.reserve(encoded.size() + 1);
    for (QByteArray &part : encoded)
        argv.append(part.data());
    argv.append(nullptr);
    QByteArray directory = QFile::encodeName(workingDirectory);
    const QByteArray programName = sanitizedIdentity(QCoreApplication::applicationName());
    const QByteArray programVersion = sanitizedIdentity(QCoreApplication::applicationVersion());
    struct winsize size = windowSize();
    // AGENT-GUARD: CLOEXEC prevents later sessions and exec'd applications
    // from retaining earlier sessions' masters and keeping their PTYs alive.
    int errors[2];
    if (::pipe2(errors, O_CLOEXEC) != 0) {
        setError(QString::fromLocal8Bit(std::strerror(errno)));
        return false;
    }
    int master = -1;
    const pid_t child = ::forkpty(&master, nullptr, nullptr, &size);
    if (child == 0) {
        ::close(errors[0]);
        if (!directory.isEmpty() && ::chdir(directory.constData()) != 0) {
            const int failure = errno;
            (void)::write(errors[1], &failure, sizeof(failure));
            _exit(126);
        }
        // AGENT-GUARD: programs pick escape sequences by these variables. One
        // inherited from the terminal that launched this host would make them
        // speak that terminal's dialect to this one.
        for (const char *name : foreignTerminalVariables)
            ::unsetenv(name);
        ::setenv("TERM", "xterm-256color", 1);
        ::setenv("COLORTERM", "truecolor", 1);
        if (!programName.isEmpty()) {
            ::setenv("TERM_PROGRAM", programName.constData(), 1);
            if (!programVersion.isEmpty())
                ::setenv("TERM_PROGRAM_VERSION", programVersion.constData(), 1);
        }
        ::execvp(executable.constData(), argv.data());
        const int failure = errno;
        (void)::write(errors[1], &failure, sizeof(failure));
        _exit(127);
    }
    ::close(errors[1]);
    if (child < 0) {
        const int failure = errno;
        ::close(errors[0]);
        errno = failure;
        setError(QString::fromLocal8Bit(std::strerror(errno)));
        return false;
    }
    int failure = 0;
    ssize_t count;
    do { count = ::read(errors[0], &failure, sizeof(failure)); } while (count < 0 && errno == EINTR);
    ::close(errors[0]);
    if (count > 0) {
        ::close(master);
        while (::waitpid(child, nullptr, 0) < 0 && errno == EINTR) {}
        setError(QStringLiteral("could not start %1: %2").arg(program, QString::fromLocal8Bit(std::strerror(failure))));
        return false;
    }
    m_pid = child;
    m_master = master;
    m_exitCode = -1;
    m_childExited = false;
    m_foregroundProcessRunning = false;
    m_workingDirectory = workingDirectory.isEmpty() ? QDir::currentPath()
                                                   : QDir(workingDirectory).absolutePath();
    m_title.clear();
    m_titleBytes.clear();
    m_error.clear();
    clearHistory();
    m_pendingInput.clear();
    vterm_screen_reset(m_vscreen, 1);
    m_protocol->processStarted();
    const int flags = ::fcntl(m_master, F_GETFL);
    ::fcntl(m_master, F_SETFL, flags | O_NONBLOCK);
    ::fcntl(m_master, F_SETFD, FD_CLOEXEC);
    m_readNotifier = new QSocketNotifier(m_master, QSocketNotifier::Read, this);
    connect(m_readNotifier, &QSocketNotifier::activated, this, [this] { readAvailable(); });
    m_writeNotifier = new QSocketNotifier(m_master, QSocketNotifier::Write, this);
    m_writeNotifier->setEnabled(false);
    connect(m_writeNotifier, &QSocketNotifier::activated, this, &TerminalSession::flushInput);
    m_pollTimer.start();
    rebuildScreen();
    emit processChanged();
    return true;
}

bool TerminalSession::sendText(const QString &text)
{
    if (!running() || m_master < 0)
        return false;
    return queueInput(text.toUtf8());
}

bool TerminalSession::queueInput(const QByteArray &bytes)
{
    if (!running() || m_master < 0)
        return false;
    if (bytes.size() > maxPendingInput - m_pendingInput.size()) {
        setError(QStringLiteral("terminal input buffer is full"));
        return false;
    }
    m_pendingInput += bytes;
    return flushInput();
}

bool TerminalSession::flushInput()
{
    bool succeeded = true;
    while (m_master >= 0 && !m_pendingInput.isEmpty()) {
        const ssize_t n = ::write(m_master, m_pendingInput.constData(), m_pendingInput.size());
        if (n > 0) {
            m_pendingInput.remove(0, n);
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
        setError(QString::fromLocal8Bit(std::strerror(errno)));
        m_pendingInput.clear();
        succeeded = false;
        break;
    }
    if (m_writeNotifier)
        m_writeNotifier->setEnabled(!m_pendingInput.isEmpty());
    return succeeded;
}

bool TerminalSession::resize(int rows, int columns)
{
    if (rows < 2 || rows > maxRows || columns < 2 || columns > maxColumns || !m_vterm)
        return false;
    if (rows == m_rows && columns == m_columns)
        return true;
    m_rows = rows;
    m_columns = columns;
    // A frame drawn for the old size must not be applied to the new one late.
    m_protocol->endSynchronizedUpdate();
    flushInput();
    vterm_set_size(m_vterm, rows, columns);
    vterm_screen_flush_damage(m_vscreen);
    applyWindowSize();
    rebuildScreen();
    return true;
}

struct winsize TerminalSession::windowSize() const
{
    struct winsize size {};
    size.ws_row = static_cast<unsigned short>(m_rows);
    size.ws_col = static_cast<unsigned short>(m_columns);
    size.ws_xpixel = static_cast<unsigned short>(qMin(65535, m_columns * m_protocol->cellWidth));
    size.ws_ypixel = static_cast<unsigned short>(qMin(65535, m_rows * m_protocol->cellHeight));
    return size;
}

void TerminalSession::applyWindowSize()
{
    if (m_master < 0)
        return;
    const struct winsize size = windowSize();
    ::ioctl(m_master, TIOCSWINSZ, &size);
    if (m_pid > 0)
        signalProcessGroup(m_pid, SIGWINCH);
}

void TerminalSession::setCellSize(int width, int height)
{
    width = qBound(0, width, 512);
    height = qBound(0, height, 512);
    if (width == m_protocol->cellWidth && height == m_protocol->cellHeight)
        return;
    m_protocol->cellWidth = width;
    m_protocol->cellHeight = height;
    applyWindowSize();
}

void TerminalSession::stop()
{
    if (m_pid <= 0 || m_childExited)
        return;
    signalProcessGroup(m_pid, SIGHUP);
    m_killTimer.start(2000);
}

void TerminalSession::closePty()
{
    delete m_readNotifier;
    m_readNotifier = nullptr;
    delete m_writeNotifier;
    m_writeNotifier = nullptr;
    if (m_master >= 0) {
        ::close(m_master);
        m_master = -1;
    }
    m_pendingInput.clear();
}

bool TerminalSession::foregroundProcessRunning() const
{
    const pid_t foreground = m_master >= 0 ? ::tcgetpgrp(m_master) : -1;
    return foreground > 0 && foreground != m_pid;
}

QString TerminalSession::workingDirectory() const
{
    const pid_t foreground = m_master >= 0 ? ::tcgetpgrp(m_master) : -1;
    const QString directory = QFileInfo(QStringLiteral("/proc/%1/cwd").arg(
        foreground > 0 ? foreground : m_pid)).symLinkTarget();
    return directory.isEmpty() ? m_workingDirectory : directory;
}

void TerminalSession::updateProcessInfo()
{
    const bool active = foregroundProcessRunning();
    const QString directory = workingDirectory();
    if (active != m_foregroundProcessRunning || directory != m_workingDirectory) {
        m_foregroundProcessRunning = active;
        m_workingDirectory = directory;
        emit processChanged();
    }
}

void TerminalSession::pollProcess()
{
    if (m_pid <= 0)
        return;
    if (!m_childExited) {
        const pid_t result = ::waitpid(m_pid, &m_exitedStatus, WNOHANG);
        if (result == 0) {
            updateProcessInfo();
            return;
        }
        if (result < 0 && errno == EINTR)
            return;
        if (result < 0)
            m_exitedStatus = -1;
        m_childExited = true;
    }
    // Drain output in bounded event-loop turns even after waitpid succeeds.
    // Closing after one read budget silently discarded large final output.
    if (readAvailable())
        return;
    // Hosts read the final screen from exited(); nothing may still be held.
    m_protocol->processEnded();
    m_exitCode = m_exitedStatus < 0 ? -1 : WIFEXITED(m_exitedStatus)
        ? WEXITSTATUS(m_exitedStatus) : 128 + WTERMSIG(m_exitedStatus);
    m_pid = -1;
    m_foregroundProcessRunning = false;
    m_pollTimer.stop();
    m_killTimer.stop();
    closePty();
    emit processChanged();
    emit exited(m_exitCode);
}

bool TerminalSession::readAvailable(int budget)
{
    if (m_master < 0 || !m_vterm)
        return false;
    char bytes[8192];
    bool changed = false;
    while (budget > 0) {
        const ssize_t n = ::read(m_master, bytes, qMin<int>(sizeof(bytes), budget));
        if (n > 0) {
            m_protocol->input(QByteArrayView(bytes, n));
            budget -= static_cast<int>(n);
            changed = true;
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if ((n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) && m_readNotifier)
            m_readNotifier->setEnabled(false);
        break;
    }
    if (changed) {
        flushInput();
        // AGENT-NOTE: output is presented after a short quiet gap rather than
        // per read. A frame written in several pieces, or held as a
        // synchronized update, is then shown whole instead of half drawn.
        m_protocol->schedulePresent();
    }
    return budget == 0;
}

} // namespace QindaTK
