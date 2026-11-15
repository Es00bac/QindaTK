// SPDX-License-Identifier: LGPL-3.0-or-later
// AGENT-NOTE (D-354, docs/android.md): TerminalSession for builds without
// libvterm (QINDATK_ENABLE_LIBVTERM=OFF, the Android default; there is no
// libvterm in the NDK and an app cannot offer a pty shell anyway). The QML
// surface of terminal_session.cpp is kept; nothing ever starts, and start()
// leaves a readable errorString. Header side: QINDATK_NO_LIBVTERM in
// terminal_session.h, set PUBLIC by src/CMakeLists.txt.
// AGENT-GUARD: keep this file free of libvterm and <pty.h>; it must compile
// against the NDK.
#include "terminal_session.h"

#include <QVariantMap>

namespace QindaTK {

namespace {
const QString &unavailableText()
{
    static const QString text = QStringLiteral("Terminal sessions are not built into this QindaTK (no libvterm)");
    return text;
}
} // namespace

TerminalSession::TerminalSession(QObject *parent) : QObject(parent) {}
TerminalSession::~TerminalSession() = default;

void TerminalSession::setMaxScrollback(int value) { m_maxScrollback = qBound(0, value, 100000); }
bool TerminalSession::foregroundProcessRunning() const { return false; }
QString TerminalSession::workingDirectory() const { return m_workingDirectory; }
int TerminalSession::historyColumns() const { return 0; }

bool TerminalSession::start(const QString &, const QStringList &, const QString &)
{
    if (m_error != unavailableText()) {
        m_error = unavailableText();
        emit processChanged();
    }
    return false;
}
bool TerminalSession::sendText(const QString &) { return false; }
bool TerminalSession::sendKey(int, int, const QString &) { return false; }
QVariantList TerminalSession::screen() const { return m_screenRows; }
bool TerminalSession::sendKeyEvent(int, int, const QString &, int) { return false; }
bool TerminalSession::bracketedPaste() const { return false; }
int TerminalSession::keyboardFlags() const { return 0; }
bool TerminalSession::alternateScroll() const { return true; }
QString TerminalSession::cursorColor() const { return {}; }
QVariantMap TerminalSession::programStatus() const { return {}; }
QVariantList TerminalSession::programStatusRecords() const { return {}; }
void TerminalSession::acknowledgeProgramStatus() {}
void TerminalSession::setCellSize(int, int) {}
bool TerminalSession::pasteText(const QString &) { return false; }
bool TerminalSession::sendMouse(int, int, int, bool, int) { return false; }
void TerminalSession::setFocused(bool) {}
void TerminalSession::setColors(const QColor &, const QColor &, const QVariantList &) {}
void TerminalSession::clearHistory() {}
QVariantMap TerminalSession::rowData(int) const { return {}; }
QString TerminalSession::copyTextRange(int, int, int, int) const { return {}; }
bool TerminalSession::resize(int, int) { return false; }
void TerminalSession::stop() {}
QString TerminalSession::copyText(int, int) const { return {}; }

} // namespace QindaTK
