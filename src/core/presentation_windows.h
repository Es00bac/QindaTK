// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QPointer>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class QScreen;

namespace QindaTK {
class SlideFrameView;
class SlideShow;
class WindowHost;

// Two real native windows for a running SlideShow (docs/slides.md#windows):
// an audience window holding only a SlideFrameView, and a presenter window
// holding `presenterDelegate` (typically Tk.PresenterConsole). Windows are
// created through WindowHost with the roles "slide-audience" and
// "slide-presenter". Displays are chosen by stable screen name (QScreen::name).
//
// audienceStatus:
//   closed     no audience window (not opened, or closed by the user)
//   connected  full screen on the chosen display (or, with none chosen, on a
//              display other than the presenter's)
//   shared     only one display: the audience window is a normal window
//              on the presenter's display
//   waiting    the chosen display is missing (never connected or unplugged);
//              the audience window stays open as a window on the presenter's
//              display and returns to the chosen display when it reappears
// presenterStatus:
//   closed     no presenter window
//   shown      on its chosen display, else the primary or any display the
//              audience does not use
//   hidden     only the audience's display is left (the presenter's display
//              was unplugged): the window is hidden, never moved onto the
//              projector, and is shown again when another display appears;
//              the audience window's keys keep the show controllable
// AGENT-CONTRACT (D-161): audience content never receives the notes; the
// presenter window is the only place speakerNotes are bound, and it is
// never placed on the audience's display.
class PresentationWindows : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_MOC_INCLUDE("slide_show.h")
    Q_MOC_INCLUDE("window_host.h")
    Q_PROPERTY(QindaTK::SlideShow *show READ show WRITE setShow NOTIFY configurationChanged)
    Q_PROPERTY(QindaTK::WindowHost *windowHost READ windowHost WRITE setWindowHost NOTIFY configurationChanged)
    Q_PROPERTY(QQmlComponent *presenterDelegate READ presenterDelegate WRITE setPresenterDelegate NOTIFY configurationChanged)
    Q_PROPERTY(QString audienceScreenId READ audienceScreenId WRITE setAudienceScreenId NOTIFY changed)
    Q_PROPERTY(QString presenterScreenId READ presenterScreenId WRITE setPresenterScreenId NOTIFY changed)
    Q_PROPERTY(bool fullScreen READ fullScreen WRITE setFullScreen NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString audienceStatus READ audienceStatus NOTIFY changed)
    Q_PROPERTY(QString presenterStatus READ presenterStatus NOTIFY changed)
    Q_PROPERTY(QString audienceWindowId READ audienceWindowId NOTIFY changed)
    Q_PROPERTY(QString presenterWindowId READ presenterWindowId NOTIFY changed)
    Q_PROPERTY(QString audienceScreen READ audienceScreen NOTIFY changed)
    Q_PROPERTY(QVariantList displays READ displays NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
public:
    explicit PresentationWindows(QObject *parent = nullptr);
    ~PresentationWindows() override;
    SlideShow *show() const { return m_show; }
    void setShow(SlideShow *show);
    WindowHost *windowHost() const;
    void setWindowHost(WindowHost *host);
    QQmlComponent *presenterDelegate() const { return m_presenterDelegate; }
    void setPresenterDelegate(QQmlComponent *component);
    QString audienceScreenId() const { return m_audienceScreenId; }
    void setAudienceScreenId(const QString &id);
    QString presenterScreenId() const { return m_presenterScreenId; }
    void setPresenterScreenId(const QString &id);
    bool fullScreen() const { return m_fullScreen; }
    void setFullScreen(bool fullScreen);
    bool active() const { return m_active; }
    QString audienceStatus() const { return m_status; }
    QString presenterStatus() const { return m_presenterStatus; }
    QString audienceWindowId() const { return m_audienceId; }
    QString presenterWindowId() const { return m_presenterId; }
    // Screen name the audience window is on now; empty when closed.
    QString audienceScreen() const;
    QVariantList displays() const;
    QString lastError() const { return m_error; }
    QQuickWindow *audienceWindow() const;
    QQuickWindow *presenterWindow() const;
    SlideFrameView *audienceView() const { return m_audienceView; }
    QQuickItem *presenterContent() const { return m_presenterContent; }

    // Opens both windows and starts the show when it is not running.
    Q_INVOKABLE bool open();
    Q_INVOKABLE void close();
    // Reopens a closed audience window and places it again; a nonempty
    // screenId also becomes the chosen audience display.
    Q_INVOKABLE bool reconnect(const QString &screenId = {});
    // Swaps the audience and presenter displays.
    Q_INVOKABLE bool swapDisplays();
    // Re-applies placement after screens were added or removed (connected
    // to QGuiApplication automatically; public for hosts and tests).
    Q_INVOKABLE void reconcile();

protected:
    // AGENT-NOTE: the displays placement may use (QGuiApplication::screens()).
    // A seam for headless tests, whose offscreen platform screen cannot be
    // unplugged: a test subclass leaves it out to model a two-display desk.
    virtual QList<QScreen *> availableScreens() const;

signals:
    void configurationChanged();
    void changed();
    void audienceDisconnected(const QString &screenId);
    void audienceReconnected(const QString &screenId);
    void presenterHidden();
    void presenterRestored();

private:
    bool openAudience();
    bool openPresenter();
    void placeAudience();
    void placePresenter();
    void fill(QQuickWindow *window, QQuickItem *content);
    void windowClosed(const QString &windowId);
    void setStatus(const QString &status);
    void setPresenterStatus(const QString &status);
    // The display the audience uses: the chosen one, else where it was last connected.
    QString audienceClaim() const;
    bool fail(const QString &message);
    QScreen *screenNamed(const QString &name) const;
    QScreen *presenterScreen() const;
    QScreen *primaryScreen() const;

    QPointer<SlideShow> m_show;
    QPointer<WindowHost> m_host;
    WindowHost *m_ownHost = nullptr;
    QPointer<QQmlComponent> m_presenterDelegate;
    QPointer<SlideFrameView> m_audienceView;
    QPointer<QQuickItem> m_presenterContent;
    QString m_audienceScreenId;
    QString m_presenterScreenId;
    QString m_audienceId;
    QString m_presenterId;
    QString m_status = QStringLiteral("closed");
    QString m_presenterStatus = QStringLiteral("closed");
    QString m_lastAudienceScreen;
    QString m_error;
    QMetaObject::Connection m_hostConnection;
    bool m_fullScreen = true;
    bool m_active = false;
    bool m_closing = false;
    bool m_reconcileQueued = false;
};

} // namespace QindaTK
