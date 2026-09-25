// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-056: native audience and presenter windows (docs/slides.md#windows):
// separate outputs, notes only on the presenter, and an audience display
// that is unplugged and plugged back in (simulated QPA screens).
#include "presentation_windows.h"
#include "slide_deck.h"
#include "slide_frame_view.h"
#include "slide_show.h"
#include "window_host.h"

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QScreen>
#include <QtTest>
#include <qpa/qplatformscreen.h>
#include <qpa/qwindowsysteminterface.h>

using namespace QindaTK;

namespace {
class TestScreen : public QPlatformScreen {
public:
    explicit TestScreen(QString name, QRect bounds) : m_name(std::move(name)), m_bounds(bounds) {}
    QRect geometry() const override { return m_bounds; }
    QRect availableGeometry() const override { return m_bounds; }
    int depth() const override { return 32; }
    QImage::Format format() const override { return QImage::Format_RGB32; }
    QString name() const override { return m_name; }
    QList<QPlatformScreen *> virtualSiblings() const override { return {const_cast<TestScreen *>(this)}; }
private:
    QString m_name;
    QRect m_bounds;
};

const QString Notes = QStringLiteral("Secret presenter notes 42");

// A desk with a laptop panel and a projector: the headless platform's own
// screen cannot be unplugged, so placement is told it does not exist.
class DeskWindows : public PresentationWindows {
public:
    explicit DeskWindows(QString ignored) : m_ignored(std::move(ignored)) {}
protected:
    QList<QScreen *> availableScreens() const override
    {
        QList<QScreen *> out;
        for (QScreen *screen : QGuiApplication::screens()) if (screen->name() != m_ignored) out << screen;
        return out;
    }
private:
    QString m_ignored;
};

// Every "text" property in an item tree (labels, fields, captions).
QStringList texts(QQuickItem *root)
{
    QStringList out;
    if (!root) return out;
    const QVariant text = root->property("text");
    if (text.isValid()) out << text.toString();
    for (QQuickItem *child : root->childItems()) out << texts(child);
    return out;
}
bool containsNotes(QQuickItem *root)
{
    for (const QString &t : texts(root)) if (t.contains(Notes)) return true;
    return false;
}
QQuickItem *byName(QQuickItem *root, const QString &name)
{
    if (!root) return nullptr;
    if (root->objectName() == name) return root;
    for (QQuickItem *child : root->childItems())
        if (QQuickItem *found = byName(child, name)) return found;
    return nullptr;
}
void press(QQuickItem *item, Qt::Key key)
{
    QKeyEvent down(QEvent::KeyPress, key, Qt::NoModifier);
    QKeyEvent up(QEvent::KeyRelease, key, Qt::NoModifier);
    QCoreApplication::sendEvent(item, &down);
    QCoreApplication::sendEvent(item, &up);
}
} // namespace

class PresentationWindowsTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        m_engine.addImportPath(QStringLiteral(QINDATK_BUILD_QML_DIR));
        m_delegate.reset(new QQmlComponent(&m_engine));
        m_delegate->setData("import QtQuick\nimport QindaTK as Tk\nTk.PresenterConsole {}\n", QUrl());
        QVERIFY2(m_delegate->isReady(), qPrintable(m_delegate->errorString()));
        const QString first = m_deck.slideIds().first();
        QVERIFY(m_deck.setNotes(first, Notes));
        QVERIFY(!m_deck.insertSlide(first).isEmpty());
        m_show.setLiveClock(false);
        m_show.setDeck(&m_deck);
    }

    void singleDisplaySharesThePresenterScreen()
    {
        PresentationWindows windows;
        windows.setShow(&m_show);
        windows.setPresenterDelegate(m_delegate.data());
        QVERIFY2(windows.open(), qPrintable(windows.lastError()));
        QVERIFY(m_show.running());
        QVERIFY(windows.audienceWindow() && windows.presenterWindow());
        QVERIFY(windows.audienceWindow() != windows.presenterWindow());
        QCOMPARE(windows.audienceStatus(), QStringLiteral("shared"));
        QCOMPARE(windows.windowHost()->windows().size(), 2);
        windows.close();
        QVERIFY(!m_show.running());
        QCOMPARE(windows.audienceStatus(), QStringLiteral("closed"));
        QVERIFY(!windows.audienceWindow());
    }

    void audienceDisplayDisconnectsAndReconnects()
    {
        auto *hdmi = new TestScreen(QStringLiteral("HDMI-A-1"), QRect(5000, 0, 1920, 1080));
        QWindowSystemInterface::handleScreenAdded(hdmi);
        QTRY_VERIFY(QGuiApplication::screens().size() >= 2);

        PresentationWindows windows;
        QSignalSpy disconnected(&windows, &PresentationWindows::audienceDisconnected);
        QSignalSpy reconnected(&windows, &PresentationWindows::audienceReconnected);
        windows.setShow(&m_show);
        windows.setPresenterDelegate(m_delegate.data());
        windows.setAudienceScreenId(QStringLiteral("HDMI-A-1"));
        QVERIFY2(windows.open(), qPrintable(windows.lastError()));
        QCOMPARE(windows.audienceStatus(), QStringLiteral("connected"));
        QCOMPARE(windows.audienceScreen(), QStringLiteral("HDMI-A-1"));
        QVERIFY(windows.presenterWindow()->screen()->name() != QStringLiteral("HDMI-A-1"));

        // Separate outputs: the same frame, but notes only on the presenter.
        QVERIFY(windows.audienceView());
        QCOMPARE(windows.audienceView()->grabFrame(320, 180), m_show.renderFrame(320, 180));
        QVERIFY(!containsNotes(windows.audienceWindow()->contentItem()));
        QTRY_VERIFY(containsNotes(windows.presenterWindow()->contentItem()));

        // Presenter keys drive the show that both windows display.
        QQuickItem *console = windows.presenterContent();
        QVERIFY(console);
        const QString first = m_deck.slideIds().first();
        press(console, Qt::Key_Right);
        QCOMPARE(m_show.currentSlideId(), m_deck.slideIds().at(1));
        QCOMPARE(windows.audienceView()->grabFrame(320, 180), m_show.renderFrame(320, 180));
        press(console, Qt::Key_Left);
        QCOMPARE(m_show.currentSlideId(), first);
        press(console, Qt::Key_B);
        QVERIFY(m_show.blackedOut());
        press(console, Qt::Key_B);
        QVERIFY(!m_show.blackedOut());

        // Unplug the audience display: the audience window survives as a
        // window on the presenter's display and still never shows notes.
        QWindowSystemInterface::handleScreenRemoved(hdmi);
        QTRY_COMPARE(windows.audienceStatus(), QStringLiteral("waiting"));
        QCOMPARE(disconnected.count(), 1);
        QVERIFY(windows.audienceWindow());
        QVERIFY(windows.audienceScreen() != QStringLiteral("HDMI-A-1"));
        QVERIFY(!containsNotes(windows.audienceWindow()->contentItem()));
        bool listedOffline = false;
        for (const QVariant &d : windows.displays()) {
            const QVariantMap display = d.toMap();
            if (display.value("id").toString() == QStringLiteral("HDMI-A-1"))
                listedOffline = !display.value("connected").toBool();
        }
        QVERIFY(listedOffline);
        QVERIFY(m_show.next()); // timing and navigation stay controllable
        QVERIFY(m_show.previous());

        // Plug it back in: the audience window returns to it automatically.
        auto *again = new TestScreen(QStringLiteral("HDMI-A-1"), QRect(5000, 0, 1920, 1080));
        QWindowSystemInterface::handleScreenAdded(again);
        QTRY_COMPARE(windows.audienceStatus(), QStringLiteral("connected"));
        QCOMPARE(windows.audienceScreen(), QStringLiteral("HDMI-A-1"));
        QCOMPARE(reconnected.count(), 1);

        // A user-closed audience window reopens on request.
        QVERIFY(windows.windowHost()->closeWindow(windows.audienceWindowId(), true));
        QCOMPARE(windows.audienceStatus(), QStringLiteral("closed"));
        QVERIFY(windows.presenterWindow());
        QVERIFY(windows.reconnect());
        QCOMPARE(windows.audienceStatus(), QStringLiteral("connected"));
        QVERIFY(!containsNotes(windows.audienceWindow()->contentItem()));

        windows.close();
        QCOMPARE(windows.windowHost()->windows().size(), 0);
        QWindowSystemInterface::handleScreenRemoved(again);
        QTRY_VERIFY(QGuiApplication::screens().size() == 1);
    }

    void presenterDisplayRemovalNeverShowsNotesToTheRoom()
    {
        const QString headless = QGuiApplication::primaryScreen()->name();
        auto *laptop = new TestScreen(QStringLiteral("eDP-1"), QRect(-3000, 0, 1600, 1000));
        auto *projector = new TestScreen(QStringLiteral("HDMI-A-1"), QRect(5000, 0, 1920, 1080));
        QWindowSystemInterface::handleScreenAdded(laptop);
        QWindowSystemInterface::handleScreenAdded(projector);
        QTRY_VERIFY(QGuiApplication::screens().size() >= 3);

        // With and without an explicit audience choice.
        for (const bool chosen : {true, false}) {
            DeskWindows windows(headless);
            QSignalSpy hidden(&windows, &PresentationWindows::presenterHidden);
            QSignalSpy restored(&windows, &PresentationWindows::presenterRestored);
            windows.setShow(&m_show);
            windows.setPresenterDelegate(m_delegate.data());
            windows.setPresenterScreenId(QStringLiteral("eDP-1"));
            if (chosen) windows.setAudienceScreenId(QStringLiteral("HDMI-A-1"));
            QVERIFY2(windows.open(), qPrintable(windows.lastError()));
            QCOMPARE(windows.audienceStatus(), QStringLiteral("connected"));
            QCOMPARE(windows.audienceScreen(), QStringLiteral("HDMI-A-1"));
            QCOMPARE(windows.presenterStatus(), QStringLiteral("shown"));
            QCOMPARE(windows.presenterWindow()->screen()->name(), QStringLiteral("eDP-1"));

            // Unplug the laptop panel: only the projector is left. The
            // presenter console (with the notes) must not move onto it.
            QWindowSystemInterface::handleScreenRemoved(laptop);
            QTRY_COMPARE(windows.presenterStatus(), QStringLiteral("hidden"));
            QCOMPARE(hidden.count(), 1);
            QVERIFY(!windows.presenterWindow()->isVisible());
            QCOMPARE(windows.audienceStatus(), QStringLiteral("connected"));
            QCOMPARE(windows.audienceScreen(), QStringLiteral("HDMI-A-1"));
            QVERIFY(!containsNotes(windows.audienceWindow()->contentItem()));
            // Navigation stays controllable from the audience window.
            const QString first = m_show.currentSlideId();
            press(windows.audienceView(), Qt::Key_Right);
            QVERIFY(m_show.currentSlideId() != first || m_show.buildStep() > 0 || m_show.ended());
            press(windows.audienceView(), Qt::Key_Left);
            QCOMPARE(m_show.currentSlideId(), first);

            // Plug it back in: the presenter returns to it.
            laptop = new TestScreen(QStringLiteral("eDP-1"), QRect(-3000, 0, 1600, 1000));
            QWindowSystemInterface::handleScreenAdded(laptop);
            QTRY_COMPARE(windows.presenterStatus(), QStringLiteral("shown"));
            QCOMPARE(restored.count(), 1);
            QVERIFY(windows.presenterWindow()->isVisible());
            QCOMPARE(windows.presenterWindow()->screen()->name(), QStringLiteral("eDP-1"));
            QCOMPARE(windows.audienceScreen(), QStringLiteral("HDMI-A-1"));
            windows.close();
        }
        QWindowSystemInterface::handleScreenRemoved(laptop);
        QWindowSystemInterface::handleScreenRemoved(projector);
        QTRY_VERIFY(QGuiApplication::screens().size() == 1);
    }

private:
    QQmlEngine m_engine;
    QScopedPointer<QQmlComponent> m_delegate;
    SlideDeck m_deck;
    SlideShow m_show;
};

QTEST_MAIN(PresentationWindowsTest)
#include "tst_presentation_windows.moc"
