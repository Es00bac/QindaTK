// SPDX-License-Identifier: LGPL-3.0-or-later
#include "flex.h"
#include "grid.h"
#include "stack.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QtTest>

using namespace QindaTK;

// Builds a QQuickView around an inline QML snippet with QindaTK imported
// and waits for the first frame, so polish-driven layouts have run.
class Scene final {
public:
    explicit Scene(const QString &qml, QSize size = QSize(400, 300))
    {
        m_view.engine()->addImportPath(QStringLiteral(QINDATK_BUILD_QML_DIR));
        m_view.setResizeMode(QQuickView::SizeRootObjectToView);
        m_view.resize(size);
        QQmlComponent component(m_view.engine());
        component.setData(("import QtQuick\nimport QindaTK as Tk\n" + qml).toUtf8(), QUrl(QStringLiteral("inline.qml")));
        if (component.isError()) {
            for (const QQmlError &error : component.errors()) {
                qWarning("%s", qPrintable(error.toString()));
            }
        }
        m_root = qobject_cast<QQuickItem *>(component.create());
        if (m_root != nullptr) {
            m_root->setParentItem(m_view.contentItem());
            m_root->setSize(QSizeF(size));
        }
        m_view.show();
    }
    [[nodiscard]] QQuickItem *root() const { return m_root; }
    // Searches the item tree (not the QObject tree): Repeater delegates are
    // item children of the container but not QObject children of it.
    [[nodiscard]] QQuickItem *item(const char *name) const
    {
        return m_root ? findItem(m_root, QString::fromLatin1(name)) : nullptr;
    }
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
    [[nodiscard]] bool exposed() { return QTest::qWaitForWindowExposed(&m_view); }

private:
    QQuickView m_view;
    QQuickItem *m_root = nullptr;
};

class TstLayout final : public QObject {
    Q_OBJECT
private slots:
    void flexRowGrow()
    {
        Scene scene(QStringLiteral(R"qml(
            Tk.Flex { direction: Tk.Flex.Row; gap: 10; padding: 5
                Item { objectName: "a"; implicitWidth: 50; implicitHeight: 20 }
                Item { objectName: "b"; implicitWidth: 50; implicitHeight: 30; Tk.Flex.grow: 1 }
                Item { objectName: "c"; implicitWidth: 50; implicitHeight: 10; Tk.Flex.grow: 3 }
            })qml"));
        QVERIFY(scene.exposed());
        QQuickItem *a = scene.item("a");
        QQuickItem *b = scene.item("b");
        QQuickItem *c = scene.item("c");
        QTRY_COMPARE(c->width(), 50 + (400 - 10 - 20 - 150) * 3 / 4.0);
        QCOMPARE(a->width(), 50.0);
        QCOMPARE(b->width(), 50 + (400 - 10 - 20 - 150) / 4.0);
        QCOMPARE(a->x(), 5.0);
        QCOMPARE(b->x(), 65.0);
        // Stretch is the default cross alignment.
        QCOMPARE(a->height(), 290.0);
        QCOMPARE(scene.root()->implicitWidth(), 180.0);
        QCOMPARE(scene.root()->implicitHeight(), 40.0);
    }
    void flexShrinkAndMin()
    {
        Scene scene(QStringLiteral(R"qml(
            Tk.Flex { direction: Tk.Flex.Row
                Item { objectName: "a"; implicitWidth: 300; implicitHeight: 10 }
                Item { objectName: "b"; implicitWidth: 300; implicitHeight: 10; Tk.Flex.minWidth: 250 }
            })qml"));
        QVERIFY(scene.exposed());
        QTRY_COMPARE(scene.item("b")->width(), 250.0);
        QCOMPARE(scene.item("a")->width(), 150.0);
    }
    void flexWrapAndJustify()
    {
        Scene scene(QStringLiteral(R"qml(
            Tk.Flex { direction: Tk.Flex.Row; wrap: Tk.Flex.Wrap; justify: Tk.Flex.SpaceBetween; alignContent: Tk.Flex.Start; gap: 0
                Repeater { model: 5; Item { objectName: "i" + index; implicitWidth: 150; implicitHeight: 20 } }
            })qml"));
        QVERIFY(scene.exposed());
        QTRY_COMPARE(scene.item("i2")->y(), 20.0);
        QCOMPARE(scene.item("i1")->x(), 250.0);
        QCOMPARE(qobject_cast<Flex *>(scene.root())->lineCount(), 3);
        QCOMPARE(scene.root()->implicitHeight(), 60.0);
    }
    void flexColumnAlignAndOrder()
    {
        Scene scene(QStringLiteral(R"qml(
            Tk.Flex { direction: Tk.Flex.Column; align: Tk.Flex.Center; gap: 4
                Item { objectName: "a"; implicitWidth: 100; implicitHeight: 20; Tk.Flex.order: 2 }
                Item { objectName: "b"; implicitWidth: 200; implicitHeight: 20; Tk.Flex.alignSelf: Tk.Flex.Stretch }
                Item { objectName: "c"; implicitWidth: 50; implicitHeight: 20; Tk.Flex.flex: 1 }
            })qml"));
        QVERIFY(scene.exposed());
        QTRY_COMPARE(scene.item("a")->y(), 280.0);
        QCOMPARE(scene.item("a")->x(), 150.0);
        QCOMPARE(scene.item("b")->width(), 400.0);
        QCOMPARE(scene.item("c")->height(), 300 - 8 - 40.0);
    }
    void gridTracksAndPlacement()
    {
        Scene scene(QStringLiteral(R"qml(
            Tk.Grid { columns: "100 1fr auto"; rows: "auto 1fr"; gap: 10
                Item { objectName: "a"; implicitWidth: 10; implicitHeight: 30 }
                Item { objectName: "b"; implicitWidth: 10; implicitHeight: 10 }
                Item { objectName: "c"; implicitWidth: 60; implicitHeight: 10 }
                Item { objectName: "d"; implicitWidth: 10; implicitHeight: 10; Tk.Grid.columnSpan: 3 }
            })qml"));
        QVERIFY(scene.exposed());
        QTRY_COMPARE(scene.item("d")->y(), 40.0);
        QCOMPARE(scene.item("a")->width(), 100.0);
        QCOMPARE(scene.item("b")->x(), 110.0);
        QCOMPARE(scene.item("b")->width(), 400 - 100 - 60 - 20.0);
        QCOMPARE(scene.item("c")->x(), 340.0);
        QCOMPARE(scene.item("c")->width(), 60.0);
        QCOMPARE(scene.item("a")->height(), 30.0);
        QCOMPARE(scene.item("d")->width(), 400.0);
        QCOMPARE(scene.item("d")->height(), 260.0);
        QCOMPARE(qobject_cast<Grid *>(scene.root())->rowCount(), 2);
    }
    void gridAreasAndAlignment()
    {
        Scene scene(QStringLiteral(R"qml(
            Tk.Grid { columns: "1fr 1fr"; rows: "50 1fr"; areas: ["head head", "side main"]; alignItems: Tk.Grid.Center
                Item { objectName: "head"; Tk.Grid.area: "head"; implicitHeight: 20 }
                Item { objectName: "side"; Tk.Grid.area: "side"; implicitHeight: 20; Tk.Grid.alignSelf: Tk.Grid.Stretch }
                Item { objectName: "main"; Tk.Grid.area: "main"; implicitWidth: 40; implicitHeight: 20; Tk.Grid.justifySelf: Tk.Grid.End }
            })qml"));
        QVERIFY(scene.exposed());
        QTRY_COMPARE(scene.item("side")->height(), 250.0);
        QCOMPARE(scene.item("head")->width(), 400.0);
        QCOMPARE(scene.item("head")->y(), 15.0);
        QCOMPARE(scene.item("main")->x(), 360.0);
        QCOMPARE(scene.item("main")->y(), 50 + (250 - 20) / 2.0);
    }
    void gridAutoFlowColumnAndDense()
    {
        Scene scene(QStringLiteral(R"qml(
            Tk.Grid { columns: "repeat(3, 1fr)"; autoFlow: Tk.Grid.RowDense; autoRows: "20"
                Item { objectName: "a"; Tk.Grid.columnSpan: 2 }
                Item { objectName: "b"; Tk.Grid.columnSpan: 2 }
                Item { objectName: "c" }
            })qml"));
        QVERIFY(scene.exposed());
        QTRY_COMPARE(scene.item("b")->y(), 20.0);
        // Dense packing pulls the single cell back into row 0.
        QCOMPARE(scene.item("c")->y(), 0.0);
        QCOMPARE(scene.item("c")->x(), 400 / 3.0 * 2);
    }
    void stackInsets()
    {
        Scene scene(QStringLiteral(R"qml(
            Tk.Stack { padding: 10
                Item { objectName: "fill" }
                Item { objectName: "corner"; implicitWidth: 30; implicitHeight: 20; Tk.Stack.top: 4; Tk.Stack.right: 6 }
                Item { objectName: "bar"; implicitHeight: 12; Tk.Stack.left: 0; Tk.Stack.right: 0; Tk.Stack.bottom: 0 }
                Item { objectName: "mid"; implicitWidth: 50; implicitHeight: 50; Tk.Stack.fill: false; Tk.Stack.centerX: true; Tk.Stack.centerY: true }
            })qml"));
        QVERIFY(scene.exposed());
        QTRY_COMPARE(scene.item("fill")->width(), 380.0);
        QCOMPARE(scene.item("corner")->x(), 400 - 10 - 6 - 30.0);
        QCOMPARE(scene.item("corner")->y(), 14.0);
        QCOMPARE(scene.item("bar")->y(), 300 - 10 - 12.0);
        QCOMPARE(scene.item("bar")->width(), 380.0);
        QCOMPARE(scene.item("mid")->x(), 10 + (380 - 50) / 2.0);
    }
    void nestedImplicitSizes()
    {
        Scene scene(QStringLiteral(R"qml(
            Tk.Flex { direction: Tk.Flex.Column; justify: Tk.Flex.Start; align: Tk.Flex.Start
                Tk.Box { objectName: "box"; padding: 6; borderWidth: 1
                    Tk.Flex { direction: Tk.Flex.Row; gap: 4
                        Item { implicitWidth: 40; implicitHeight: 16 }
                        Item { implicitWidth: 40; implicitHeight: 16 }
                    }
                }
            })qml"));
        QVERIFY(scene.exposed());
        QTRY_COMPARE(scene.item("box")->width(), 84 + 14.0);
        QCOMPARE(scene.item("box")->height(), 16 + 14.0);
    }
};

QTEST_MAIN(TstLayout)
#include "tst_layout.moc"
