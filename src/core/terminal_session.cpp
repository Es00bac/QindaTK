// SPDX-License-Identifier: LGPL-3.0-or-later
#include "terminal_session.h"

#include <QDir>
#include <QFile>
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
constexpr int maxRows = 100;
constexpr int maxColumns = 240;
constexpr int maxPendingInput = 1024 * 1024;

void signalProcessGroup(int pid, int signal)
{
    if (pid <= 0)
        return;
    if (::kill(-pid, signal) < 0 && errno == ESRCH)
        ::kill(pid, signal);
}

QString colorName(const VTermScreen *screen, VTermColor color, bool foreground)
{
    if (foreground ? VTERM_COLOR_IS_DEFAULT_FG(&color) : VTERM_COLOR_IS_DEFAULT_BG(&color))
        return {};
    vterm_screen_convert_color_to_rgb(screen, &color);
    return QStringLiteral("#%1%2%3")
        .arg(color.rgb.red, 2, 16, QLatin1Char('0'))
        .arg(color.rgb.green, 2, 16, QLatin1Char('0'))
        .arg(color.rgb.blue, 2, 16, QLatin1Char('0'));
}
} // namespace

TerminalSession::TerminalSession(QObject *parent) : QObject(parent)
{
    m_pollTimer.setInterval(50);
    connect(&m_pollTimer, &QTimer::timeout, this, &TerminalSession::pollProcess);
    m_killTimer.setSingleShot(true);
    connect(&m_killTimer, &QTimer::timeout, this, [this] {
        if (m_pid > 0)
            signalProcessGroup(m_pid, SIGKILL);
    });
    m_vterm = vterm_new(m_rows, m_columns);
    if (!m_vterm) {
        setError(QStringLiteral("libvterm could not allocate a terminal"));
        return;
    }
    vterm_set_utf8(m_vterm, 1);
    vterm_output_set_callback(m_vterm, &TerminalSession::outputToPty, this);
    m_vscreen = vterm_obtain_screen(m_vterm);
    m_callbacks.movecursor = &TerminalSession::cursorMoved;
    m_callbacks.sb_pushline = &TerminalSession::pushScrollback;
    m_callbacks.sb_clear = &TerminalSession::clearScrollback;
    vterm_screen_set_callbacks(m_vscreen, &m_callbacks, this);
    vterm_screen_enable_altscreen(m_vscreen, 1);
    vterm_screen_reset(m_vscreen, 1);
    rebuildScreen();
}

TerminalSession::~TerminalSession()
{
    m_pollTimer.stop();
    m_killTimer.stop();
    if (m_pid > 0) {
        signalProcessGroup(m_pid, SIGKILL);
        int status = 0;
        while (::waitpid(m_pid, &status, 0) < 0 && errno == EINTR) {}
        m_pid = -1;
    }
    closePty();
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
    while (m_scrollback.size() > m_maxScrollback)
        m_scrollback.removeFirst();
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
    struct winsize size {};
    size.ws_row = static_cast<unsigned short>(m_rows);
    size.ws_col = static_cast<unsigned short>(m_columns);
    int master = -1;
    const pid_t child = ::forkpty(&master, nullptr, nullptr, &size);
    if (child == 0) {
        if (!directory.isEmpty() && ::chdir(directory.constData()) != 0)
            _exit(126);
        ::setenv("TERM", "xterm-256color", 1);
        ::setenv("COLORTERM", "truecolor", 1);
        ::execvp(executable.constData(), argv.data());
        _exit(127);
    }
    if (child < 0) {
        setError(QString::fromLocal8Bit(std::strerror(errno)));
        return false;
    }
    m_pid = child;
    m_master = master;
    m_exitCode = -1;
    m_error.clear();
    m_scrollback.clear();
    m_pendingInput.clear();
    vterm_screen_reset(m_vscreen, 1);
    const int flags = ::fcntl(m_master, F_GETFL);
    ::fcntl(m_master, F_SETFL, flags | O_NONBLOCK);
    m_readNotifier = new QSocketNotifier(m_master, QSocketNotifier::Read, this);
    connect(m_readNotifier, &QSocketNotifier::activated, this, &TerminalSession::readAvailable);
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
    const QByteArray bytes = text.toUtf8();
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
    vterm_set_size(m_vterm, rows, columns);
    if (m_master >= 0) {
        struct winsize size {};
        size.ws_row = static_cast<unsigned short>(rows);
        size.ws_col = static_cast<unsigned short>(columns);
        ::ioctl(m_master, TIOCSWINSZ, &size);
        if (m_pid > 0)
            signalProcessGroup(m_pid, SIGWINCH);
    }
    rebuildScreen();
    return true;
}

void TerminalSession::stop()
{
    if (m_pid <= 0)
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

void TerminalSession::pollProcess()
{
    if (m_pid <= 0)
        return;
    int status = 0;
    const pid_t result = ::waitpid(m_pid, &status, WNOHANG);
    if (result == 0)
        return;
    if (result < 0 && errno == EINTR)
        return;
    readAvailable();
    m_exitCode = result < 0 ? -1 : WIFEXITED(status) ? WEXITSTATUS(status)
                                                : 128 + WTERMSIG(status);
    m_pid = -1;
    m_pollTimer.stop();
    m_killTimer.stop();
    closePty();
    emit processChanged();
    emit exited(m_exitCode);
}

void TerminalSession::readAvailable()
{
    if (m_master < 0 || !m_vterm)
        return;
    char bytes[8192];
    int budget = 262144;
    bool changed = false;
    while (budget > 0) {
        const ssize_t n = ::read(m_master, bytes, qMin<int>(sizeof(bytes), budget));
        if (n > 0) {
            vterm_input_write(m_vterm, bytes, n);
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
        vterm_screen_flush_damage(m_vscreen);
        flushInput();
        rebuildScreen();
    }
}

QString TerminalSession::cellText(const VTermScreenCell &cell)
{
    if (cell.width == 0)
        return {};
    QString text;
    for (uint32_t ch : cell.chars) {
        if (!ch)
            break;
        const char32_t scalar = static_cast<char32_t>(ch);
        text += QString::fromUcs4(&scalar, 1);
    }
    return text.isEmpty() ? QStringLiteral(" ") : text;
}

int TerminalSession::pushScrollback(int columns, const VTermScreenCell *cells, void *user)
{
    auto *self = static_cast<TerminalSession *>(user);
    if (self->m_maxScrollback == 0)
        return 0;
    QString line;
    for (int column = 0; column < columns; ++column)
        line += cellText(cells[column]);
    while (line.endsWith(QLatin1Char(' ')))
        line.chop(1);
    self->m_scrollback.append(line);
    while (self->m_scrollback.size() > self->m_maxScrollback)
        self->m_scrollback.removeFirst();
    return 0;
}

int TerminalSession::clearScrollback(void *user)
{
    static_cast<TerminalSession *>(user)->m_scrollback.clear();
    return 1;
}

int TerminalSession::cursorMoved(VTermPos pos, VTermPos, int visible, void *user)
{
    auto *self = static_cast<TerminalSession *>(user);
    self->m_cursorRow = pos.row;
    self->m_cursorColumn = pos.col;
    self->m_cursorVisible = visible;
    return 1;
}

void TerminalSession::outputToPty(const char *bytes, size_t length, void *user)
{
    auto *self = static_cast<TerminalSession *>(user);
    if (self->m_master < 0 || length > size_t(maxPendingInput - self->m_pendingInput.size()))
        return;
    self->m_pendingInput.append(bytes, qsizetype(length));
}

void TerminalSession::rebuildScreen()
{
    if (!m_vscreen)
        return;
    QVariantList rows;
    rows.reserve(m_rows);
    for (int row = 0; row < m_rows; ++row) {
        QString plain;
        QVariantList spans;
        QString spanText;
        QVariantMap style;
        auto flushSpan = [&] {
            if (spanText.isEmpty())
                return;
            QVariantMap span = style;
            span.insert(QStringLiteral("text"), spanText);
            spans.append(span);
            spanText.clear();
        };
        for (int column = 0; column < m_columns; ++column) {
            VTermScreenCell cell {};
            if (!vterm_screen_get_cell(m_vscreen, {row, column}, &cell))
                continue;
            const QString glyph = cellText(cell);
            if (glyph.isEmpty())
                continue;
            QVariantMap nextStyle {
                {QStringLiteral("foreground"), colorName(m_vscreen, cell.fg, true)},
                {QStringLiteral("background"), colorName(m_vscreen, cell.bg, false)},
                {QStringLiteral("bold"), bool(cell.attrs.bold)},
                {QStringLiteral("italic"), bool(cell.attrs.italic)},
                {QStringLiteral("underline"), bool(cell.attrs.underline)},
                {QStringLiteral("reverse"), bool(cell.attrs.reverse)}
            };
            if (nextStyle != style) {
                flushSpan();
                style = nextStyle;
            }
            spanText += glyph;
            plain += glyph;
        }
        flushSpan();
        rows.append(QVariantMap {{QStringLiteral("text"), plain},
                                 {QStringLiteral("spans"), spans}});
    }
    m_screenRows = rows;
    emit screenChanged();
}

QString TerminalSession::copyText(int firstRow, int lastRow) const
{
    const int total = m_scrollback.size() + m_screenRows.size();
    if (firstRow < 0 || lastRow < firstRow || lastRow >= total)
        return {};
    QStringList selected;
    for (int row = firstRow; row <= lastRow; ++row)
        selected.append(row < m_scrollback.size() ? m_scrollback.at(row)
                                                : m_screenRows.at(row - m_scrollback.size())
                                                      .toMap().value(QStringLiteral("text")).toString());
    return selected.join(QLatin1Char('\n'));
}

} // namespace QindaTK
