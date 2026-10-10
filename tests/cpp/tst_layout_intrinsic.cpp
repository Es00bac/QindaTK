// SPDX-License-Identifier: LGPL-3.0-or-later
#include "flex.h"
#include "grid.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QFont>
#include <QQuickItem>
#include <QQuickView>
#include <QtTest>

using namespace QindaTK;

class IntrinsicScene final {
public:
    explicit IntrinsicScene(const QString &qml, QSize size)
    {
        m_view.engine()->addImportPath(QStringLiteral(QINDATK_BUILD_QML_DIR));
        m_view.resize(size);
        QQmlComponent component(m_view.engine());
        component.setData(("import QtQuick\nimport QindaTK as Tk\n" + qml).toUtf8(),
                          QUrl(QStringLiteral("intrinsic.qml")));
        if (component.isError()) {
            for (const QQmlError &error : component.errors())
                qWarning("%s", qPrintable(error.toString()));
        }
        m_root = qobject_cast<QQuickItem *>(component.create());
        if (m_root) {
            m_root->setParentItem(m_view.contentItem());
            m_root->setSize(QSizeF(size));
        }
        m_view.show();
    }
    QQuickItem *root() const { return m_root; }
    QQuickItem *item(const QString &name) const { return findItem(m_root, name); }
    bool exposed() { return QTest::qWaitForWindowExposed(&m_view); }

private:
    static QQuickItem *findItem(QQuickItem *parent, const QString &name)
    {
        if (!parent) return nullptr;
        for (QQuickItem *child : parent->childItems()) {
            if (child->objectName() == name) return child;
            if (QQuickItem *found = findItem(child, name)) return found;
        }
        return nullptr;
    }
    QQuickView m_view;
    QQuickItem *m_root = nullptr;
};

class TstLayoutIntrinsic final : public QObject {
    Q_OBJECT
private slots:
    // D-316: an implicit size assigned to a Flex is what it reports to its
    // parent; the content is still laid out in the real size, and resetting
    // to undefined goes back to the measured content size.
    void flexHonoursDeclaredImplicitSize()
    {
        IntrinsicScene scene(QStringLiteral(R"qml(
            Tk.Flex { direction: Tk.Flex.Row; align: Tk.Flex.Start
                Tk.Flex { objectName: "declared"; direction: Tk.Flex.Column; implicitWidth: 180; implicitHeight: 40
                    Item { objectName: "wide"; implicitWidth: 300; implicitHeight: 12 }
                }
                Tk.Flex { objectName: "measured"; direction: Tk.Flex.Column
                    Item { implicitWidth: 70; implicitHeight: 16 }
                }
            })qml"), QSize(400, 300));
        QVERIFY(scene.exposed());
        QQuickItem *declared = scene.item(QStringLiteral("declared"));
        QQuickItem *measured = scene.item(QStringLiteral("measured"));
        QTRY_COMPARE(measured->x(), 180.0);
        QCOMPARE(declared->implicitWidth(), 180.0);
        QCOMPARE(declared->implicitHeight(), 40.0);
        QCOMPARE(declared->width(), 180.0);
        QCOMPARE(declared->height(), 40.0);
        QCOMPARE(scene.item(QStringLiteral("wide"))->width(), 180.0);
        QCOMPARE(measured->implicitWidth(), 70.0);
        QCOMPARE(scene.root()->implicitWidth(), 250.0);
        // A later assignment wins again; undefined restores the measurement.
        QVERIFY(declared->setProperty("implicitWidth", 120.0));
        QTRY_COMPARE(measured->x(), 120.0);
        QQmlProperty(declared, QStringLiteral("implicitWidth")).reset();
        QTRY_COMPARE(declared->implicitWidth(), 300.0);
        QTRY_COMPARE(measured->x(), 300.0);
        QCOMPARE(declared->implicitHeight(), 40.0);
    }
    void nestedFlexAndBoxFollowWrappedTextAndFontMetrics()
    {
        IntrinsicScene scene(R"QML(
            Tk.Box {
                objectName: "outerBox"
                fill: false; padding: 6
                Tk.Flex {
                    objectName: "nestedFlex"
                    direction: Tk.Flex.Row; align: Tk.Flex.Start; gap: 4
                    width: 300
                    Text { objectName: "smallText"; text: "A dense layout must reflow when its available width changes."; wrapMode: Text.WordWrap; font.pixelSize: 12 }
                    Text { objectName: "largeText"; text: "Mixed font metrics"; wrapMode: Text.WordWrap; font.pixelSize: 24 }
                }
            })QML", QSize(400, 240));
        QVERIFY(scene.root());
        QVERIFY(scene.exposed());
        auto *box = scene.root();
        auto *flex = scene.item(QStringLiteral("nestedFlex"));
        auto *small = scene.item(QStringLiteral("smallText"));
        auto *large = scene.item(QStringLiteral("largeText"));
        QVERIFY(flex && small && large);
        QTRY_VERIFY(small->height() > 0);
        const qreal wideFlexHeight = flex->implicitHeight();
        const qreal wideBoxHeight = box->implicitHeight();
        flex->setWidth(92);
        QTRY_VERIFY(small->height() > wideFlexHeight);
        QTRY_VERIFY(flex->implicitHeight() > wideFlexHeight);
        QTRY_VERIFY(box->implicitHeight() > wideBoxHeight);
        QVERIFY(large->implicitHeight() > 24);

        const qreal narrowBoxHeight = box->implicitHeight();
        QFont largerFont = large->property("font").value<QFont>();
        largerFont.setPixelSize(34);
        large->setProperty("font", largerFont);
        QTRY_VERIFY(box->implicitHeight() > narrowBoxHeight);
    }

    void gridRowsFollowWrappedTextAfterWidthChange()
    {
        IntrinsicScene scene(R"QML(
            Item {
              Tk.Grid {
                objectName: "intrinsicGrid"
                width: 360; height: 0
                columns: "1fr"; rows: "auto"; padding: 4
                Text { objectName: "wrappedCell"; text: "Grid rows must follow wrapped text after the grid width changes."; wrapMode: Text.WordWrap; font.pixelSize: 16 }
              }
            })QML", QSize(360, 200));
        QVERIFY(scene.root());
        QVERIFY(scene.exposed());
        auto *grid = qobject_cast<Grid *>(scene.item(QStringLiteral("intrinsicGrid")));
        auto *text = scene.item(QStringLiteral("wrappedCell"));
        QVERIFY(grid && text);
        QTRY_VERIFY(text->height() > 0);
        const qreal wideHeight = text->height();
        grid->setWidth(100);
        QTRY_VERIFY(text->height() > wideHeight);
        QTRY_VERIFY(grid->contentHeight() >= text->height() + 8);
    }

    void scrollViewportReflowsNestedColumnOnResize()
    {
        IntrinsicScene scene(R"QML(
            Item {
                Tk.Scroll {
                    objectName: "intrinsicScroll"
                    width: 280; height: 72
                    overflowX: Tk.Scroll.Hidden
                    Tk.Flex {
                        objectName: "scrollColumn"
                        direction: Tk.Flex.Column; align: Tk.Flex.Stretch
                        Text { objectName: "scrollText"; text: "A scrolling column wraps its content to the viewport width and grows vertically."; wrapMode: Text.WordWrap; font.pixelSize: 16 }
                    }
                }
            })QML", QSize(320, 140));
        QVERIFY(scene.root());
        QVERIFY(scene.exposed());
        auto *scroll = scene.item(QStringLiteral("intrinsicScroll"));
        auto *text = scene.item(QStringLiteral("scrollText"));
        QVERIFY(scroll && text);
        const qreal wideHeight = text->height();
        scroll->setWidth(112);
        QTRY_VERIFY(text->height() > wideHeight);
        QTRY_VERIFY(scroll->property("contentHeight").toReal() > 72);
    }
};

QTEST_MAIN(TstLayoutIntrinsic)
#include "tst_layout_intrinsic.moc"
