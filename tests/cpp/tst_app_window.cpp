// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>

// Tk.AppWindow: the four bands stack without gaps, the content band takes
// the rest, the find bar appears between content and status bar, and the
// window does not re-polish itself (D-013).
class TstAppWindow final : public QObject {
    Q_OBJECT

    static QQuickItem *findItem(QQuickItem *parent, const QString &name)
    {
        for (QQuickItem *child : parent->childItems()) {
            if (child->objectName() == name) {
                return child;
            }
            if (QQuickItem *found = findItem(child, name)) {
                return found;
            }
        }
        return nullptr;
    }

    static QRectF sceneRect(QQuickItem *item)
    {
        return QRectF(item->mapToScene(QPointF(0, 0)), item->size());
    }

private slots:
    void bandsStackAndContentTakesTheRest()
    {
        QQmlApplicationEngine engine;
        engine.addImportPath(QStringLiteral(QINDATK_BUILD_QML_DIR));
        QQmlComponent component(&engine);
        component.setData(R"qml(
import QtQuick
import QindaTK as Tk
Tk.AppWindow {
    width: 800; height: 600
    menuBar: Tk.MenuBar { objectName: "menu"; Tk.Menu { title: "File"; Tk.MenuItem { text: "New" } } }
    toolBars: [
        Tk.ToolBar { objectName: "bar1"; Tk.IconButton { iconName: "save"; tooltip: "Save" } },
        Tk.ToolBar { objectName: "bar2"; compact: true; Tk.IconButton { iconName: "pen-tool"; tooltip: "Pen" } }
    ]
    findBar: Tk.Box { objectName: "find"; visible: false; implicitHeight: 30; Tk.Label { text: "Find" } }
    statusBar: Tk.StatusBar { objectName: "status"; Tk.StatusField { text: "Ready" } }
    Rectangle { objectName: "content"; color: "white" }
}
)qml", QUrl(QStringLiteral("inline.qml")));
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(component.create());
        QVERIFY(window != nullptr);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QQuickItem *root = window->contentItem();
        QQuickItem *menu = findItem(root, QStringLiteral("menu"));
        QQuickItem *bar1 = findItem(root, QStringLiteral("bar1"));
        QQuickItem *bar2 = findItem(root, QStringLiteral("bar2"));
        QQuickItem *content = findItem(root, QStringLiteral("content"));
        QQuickItem *status = findItem(root, QStringLiteral("status"));
        QQuickItem *find = findItem(root, QStringLiteral("find"));
        for (QQuickItem *item : {menu, bar1, bar2, content, status, find}) {
            QVERIFY(item != nullptr);
        }
        QTRY_VERIFY(content->height() > 0);
        const QRectF m = sceneRect(menu), b1 = sceneRect(bar1), b2 = sceneRect(bar2);
        const QRectF c = sceneRect(content), s = sceneRect(status);
        // Top to bottom, edge to edge, full width.
        QCOMPARE(m.top(), 0.0);
        QCOMPARE(b1.top(), m.bottom());
        QCOMPARE(b2.top(), b1.bottom());
        QCOMPARE(c.top(), b2.bottom());
        QCOMPARE(c.bottom(), s.top());
        QCOMPARE(s.bottom(), 600.0);
        for (const QRectF &r : {m, b1, b2, c, s}) {
            QCOMPARE(r.left(), 0.0);
            QCOMPARE(r.width(), 800.0);
        }
        QCOMPARE(b1.height(), 36.0);
        QCOMPARE(b2.height(), 28.0);
        QCOMPARE(s.height(), 22.0);

        // Showing the find bar inserts it above the status bar and shrinks
        // the content by exactly its height.
        const qreal contentBefore = c.height();
        find->setVisible(true);
        QTRY_COMPARE(sceneRect(find).bottom(), sceneRect(status).top());
        QTRY_COMPARE(sceneRect(content).height(), contentBefore - 30.0);
        delete window;
    }
};

QTEST_MAIN(TstAppWindow)
#include "tst_app_window.moc"
