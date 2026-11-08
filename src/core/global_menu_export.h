// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
// Desktop global menu for a Tk.MenuBar (D-361, docs/native-integration.md).
// AGENT-CONTRACT: Tk.MenuBar always owns one of these; there is no switch to
// turn it off. The menu bar's menus are published to the desktop as
// com.canonical.dbusmenu and announced for the bar's window (Wayland:
// Qt's KDE app-menu hook; X11: com.canonical.AppMenu.Registrar). `hosted`
// becomes true only when the QindaQt registrar confirms the panel shows this
// menu (IsMenuHosted / MenuHostedChanged); Tk.MenuBar then collapses. Every
// other outcome is fail-closed: the in-window bar stays.
// An application that already exports its window through the QindaQt
// AppShell (/org/qindaqt/AppShell/Menu) keeps that export; this one stands
// aside (status "app-shell") so a window never has two desktop menus.
#include <QObject>
#include <QString>
#include <QtQml/qqmlparserstatus.h>
#include <QtQml/qqmlregistration.h>
#include <memory>

namespace QindaTK {

class GlobalMenuExport : public QObject, public QQmlParserStatus {
    Q_OBJECT
    QML_ELEMENT
    Q_INTERFACES(QQmlParserStatus)
    Q_PROPERTY(QObject *menuBar READ menuBar WRITE setMenuBar NOTIFY menuBarChanged)
    Q_PROPERTY(bool hosted READ hosted NOTIFY hostedChanged)
    // "unavailable" (no D-Bus build), "idle", "no-session-bus", "app-shell",
    // "duplicate", "waiting-for-registrar", "published", "unsupported-platform",
    // "register-failed"
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString objectPath READ objectPath NOTIFY statusChanged)
public:
    explicit GlobalMenuExport(QObject *parent = nullptr);
    ~GlobalMenuExport() override;

    QObject *menuBar() const;
    void setMenuBar(QObject *menuBar);
    bool hosted() const;
    QString status() const;
    QString objectPath() const;

    // Clicks the menu item with a published id (tests and accessibility).
    Q_INVOKABLE bool activate(const QString &stableId);

    void classBegin() override {}
    void componentComplete() override;

signals:
    void menuBarChanged();
    void hostedChanged();
    void statusChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace QindaTK
