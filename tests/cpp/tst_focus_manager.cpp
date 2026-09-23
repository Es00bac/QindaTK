// SPDX-License-Identifier: LGPL-3.0-or-later
#include "focus_manager.h"

#include <QtTest>
#include <memory>

using namespace QindaTK;

class TstFocusManager final : public QObject {
    Q_OBJECT
private:
    static void focus(QQuickItem *item)
    {
        item->forceActiveFocus(Qt::OtherFocusReason);
        QTRY_COMPARE(item->window()->activeFocusItem(), item);
    }

private slots:
    void restoresFocusAcrossNestedTransientSurfaces()
    {
        QQuickWindow window;
        QQuickItem original(window.contentItem());
        QQuickItem menu(window.contentItem());
        QQuickItem submenu(&menu);
        original.setSize({20, 20});
        menu.setSize({20, 20});
        submenu.setSize({10, 10});
        window.resize(100, 100);
        window.show();
        QTRY_VERIFY(window.isExposed());
        focus(&original);

        FocusManager manager;
        QObject menuOwner;
        QObject submenuOwner;
        const auto menuToken = manager.enterSurface(&menuOwner, &menu, &window);
        QVERIFY(!menuToken.isEmpty());
        QVERIFY(manager.activate(menuToken));
        QCOMPARE(window.activeFocusItem(), &menu);
        const auto submenuToken = manager.enterSurface(&submenuOwner, &submenu, &window,
                                                       menuToken);
        QVERIFY(!submenuToken.isEmpty());
        QVERIFY(manager.activate(submenuToken));
        QCOMPARE(manager.activeToken(), submenuToken);
        QCOMPARE(window.activeFocusItem(), &submenu);

        QVERIFY(manager.closeSurface(submenuToken));
        // A nested surface restores the focus captured when it opened (menu).
        QCOMPARE(window.activeFocusItem(), &menu);
        QVERIFY(manager.closeSurface(menuToken));
        QCOMPARE(window.activeFocusItem(), &original);
        QVERIFY(manager.activeToken().isEmpty());
    }

    void doesNotStealFocusAfterNewExplicitTarget()
    {
        QQuickWindow window;
        QQuickItem original(window.contentItem());
        QQuickItem popup(window.contentItem());
        QQuickItem newer(window.contentItem());
        window.resize(100, 100);
        window.show();
        QTRY_VERIFY(window.isExposed());
        focus(&original);

        FocusManager manager;
        QObject owner;
        const auto token = manager.enterSurface(&owner, &popup, &window);
        QVERIFY(manager.activate(token));
        QCOMPARE(window.activeFocusItem(), &popup);
        focus(&newer);
        QVERIFY(manager.closeSurface(token));
        QCOMPARE(window.activeFocusItem(), &newer);
    }

    void deactivationRestoresWithoutDiscardingToken()
    {
        QQuickWindow window;
        QQuickItem original(window.contentItem());
        QQuickItem popup(window.contentItem());
        window.resize(100, 100);
        window.show();
        QTRY_VERIFY(window.isExposed());
        focus(&original);
        FocusManager manager;
        QObject owner;
        const auto token = manager.enterSurface(&owner, &popup, &window);
        QVERIFY(manager.activate(token));
        QVERIFY(manager.deactivate(token));
        QCOMPARE(window.activeFocusItem(), &original);
        QVERIFY(manager.contains(token));
        QVERIFY(manager.activeToken().isEmpty());
        QVERIFY(manager.activate(token));
        QCOMPARE(window.activeFocusItem(), &popup);
        QVERIFY(manager.closeSurface(token));
    }

    void destroyedTargetsAreNeverRestored()
    {
        QQuickWindow window;
        auto *original = new QQuickItem(window.contentItem());
        auto *popup = new QQuickItem(window.contentItem());
        window.resize(100, 100);
        window.show();
        QTRY_VERIFY(window.isExposed());
        focus(original);

        FocusManager manager;
        QObject owner;
        const auto token = manager.enterSurface(&owner, popup, &window);
        QVERIFY(manager.activate(token));
        delete original;
        delete popup;
        auto *focusAfterDisposal = window.activeFocusItem();
        QVERIFY(!manager.contains(token));
        QCOMPARE(window.activeFocusItem(), focusAfterDisposal);
    }

    void ownerDisposalClosesSurface()
    {
        QQuickWindow window;
        QQuickItem original(window.contentItem());
        QQuickItem popup(window.contentItem());
        window.resize(100, 100);
        window.show();
        QTRY_VERIFY(window.isExposed());
        focus(&original);
        FocusManager manager;
        auto owner = std::make_unique<QObject>();
        const auto token = manager.enterSurface(owner.get(), &popup, &window);
        QVERIFY(manager.activate(token));
        owner.reset();
        QTRY_VERIFY(!manager.contains(token));
        QCOMPARE(window.activeFocusItem(), &original);
    }
};

QTEST_MAIN(TstFocusManager)
#include "tst_focus_manager.moc"
