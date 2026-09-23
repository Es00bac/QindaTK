// SPDX-License-Identifier: LGPL-3.0-or-later
#include "desktop_integration.h"
#include <QCoreApplication>
#include <QDesktopServices>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QGuiApplication>
#include <QProcess>
#include <QStandardPaths>

namespace QindaTK {
DesktopIntegration::DesktopIntegration(QObject *parent) : QObject(parent)
{
    if (qApp) qApp->installEventFilter(this);
}
bool DesktopIntegration::fail(const QString &message)
{
    m_lastError = message;
    emit changed();
    return false;
}
bool DesktopIntegration::registerRecent(const QUrl &url)
{
    if (!url.isValid() || url.scheme().isEmpty()) return fail(QStringLiteral("Invalid URI"));
    if (url.isLocalFile() && !QFileInfo::exists(url.toLocalFile()))
        return fail(QStringLiteral("Recent file is unavailable"));
    for (int i = 0; i < m_recent.size(); ++i)
        if (m_recent.at(i).toUrl() == url) { m_recent.removeAt(i); break; }
    m_recent.prepend(url);
    while (m_recent.size() > 20) m_recent.removeLast();
    m_lastError.clear();
    emit changed();
    return true;
}
bool DesktopIntegration::openUrl(const QUrl &url)
{
    if (!url.isValid() || url.scheme().isEmpty()) return fail(QStringLiteral("Invalid URI"));
    if (url.isLocalFile() && !QFileInfo::exists(url.toLocalFile()))
        return fail(QStringLiteral("File is unavailable"));
    if (!QDesktopServices::openUrl(url)) return fail(QStringLiteral("Desktop open failed"));
    m_lastError.clear(); emit changed(); return true;
}
bool DesktopIntegration::reveal(const QUrl &url)
{
    if (!url.isLocalFile()) return fail(QStringLiteral("Reveal supports local file URIs only"));
    const QFileInfo info(url.toLocalFile());
    if (!info.exists()) return fail(QStringLiteral("File is unavailable"));
    const QString opener = QStandardPaths::findExecutable(QStringLiteral("xdg-open"));
    if (opener.isEmpty()) return fail(QStringLiteral("Desktop reveal is unavailable"));
    if (!QProcess::startDetached(opener, {info.isDir() ? info.absoluteFilePath() : info.absolutePath()}))
        return fail(QStringLiteral("Desktop reveal failed"));
    m_lastError.clear(); emit changed(); return true;
}
bool DesktopIntegration::openWith(const QUrl &url, const QString &application)
{
    if (application.isEmpty()) return openUrl(url);
    if (!url.isLocalFile()) return fail(QStringLiteral("Explicit open-with supports local files only"));
    const QFileInfo info(url.toLocalFile());
    if (!info.isFile()) return fail(QStringLiteral("File is unavailable"));
    const QString executable = QStandardPaths::findExecutable(application);
    if (executable.isEmpty()) return fail(QStringLiteral("Requested application is unavailable"));
    if (!QProcess::startDetached(executable, {info.absoluteFilePath()}))
        return fail(QStringLiteral("Open-with failed"));
    m_lastError.clear(); emit changed(); return true;
}
bool DesktopIntegration::activate(QQuickWindow *window)
{
    if (!window) return fail(QStringLiteral("No window to activate"));
    window->show(); window->raise(); window->requestActivate();
    m_lastError.clear(); emit changed(); return true;
}
QVariantMap DesktopIntegration::capabilities() const
{
    return {{"urlOpen", true},
#ifdef Q_OS_LINUX
            {"revealLocal", !QStandardPaths::findExecutable(QStringLiteral("xdg-open")).isEmpty()},
#else
            {"revealLocal", false},
#endif
            {"applicationRecentList", true}, {"osRecentRegistration", false},
            {"externalFileOpenEvents", true}, {"secondInstanceIPC", false},
            {"activationRequest", true}};
}
bool DesktopIntegration::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == qApp && event->type() == QEvent::FileOpen) {
        auto *fileEvent = static_cast<QFileOpenEvent *>(event);
        emit openDocumentRequested(fileEvent->url());
        return true;
    }
    return QObject::eventFilter(watched, event);
}
} // namespace QindaTK
