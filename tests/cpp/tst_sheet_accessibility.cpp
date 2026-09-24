// SPDX-License-Identifier: LGPL-3.0-or-later
#include "editable_grid_model.h"
#include "workbook.h"

#include <QAccessible>
#include <QInputMethodEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QtTest>
#include <memory>

// The spreadsheet stack through QAccessibleInterface, the interface Qt's
// AT-SPI bridge serves to screen readers (docs/workbook.md#views-and-adapter):
// roles, names, descriptions and states, the assistive actions (each performs
// the same operation as a click), and input-method composition in the cell
// editor. evidence/WP-055.md records the same tree read over a real AT-SPI bus.
using namespace QindaTK;

namespace {
QAccessibleInterface *accessible(QObject *object)
{
    return object ? QAccessible::queryAccessibleInterface(object) : nullptr;
}
QString nameOf(QObject *object)
{
    QAccessibleInterface *access = accessible(object);
    return access ? access->text(QAccessible::Name) : QString();
}
QString descriptionOf(QObject *object)
{
    QAccessibleInterface *access = accessible(object);
    return access ? access->text(QAccessible::Description) : QString();
}
QStringList actionsOf(QObject *object)
{
    QAccessibleInterface *access = accessible(object);
    return access && access->actionInterface() ? access->actionInterface()->actionNames() : QStringList();
}
// Repeater delegates are visual children, not QObject children of the view.
QQuickItem *findItem(QQuickItem *root, const QString &objectName)
{
    if (!root) return nullptr;
    if (root->objectName() == objectName) return root;
    for (QQuickItem *child : root->childItems())
        if (QQuickItem *found = findItem(child, objectName)) return found;
    return nullptr;
}
// A QML function takes and returns QVariants.
QVariant call(QObject *object, const char *method, const QVariantList &arguments = {})
{
    QVariant result;
    QVariant a = arguments.value(0), b = arguments.value(1), c = arguments.value(2);
    switch (arguments.size()) {
    case 0: QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QVariant, result)); break;
    case 3: QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, a),
                                      Q_ARG(QVariant, b), Q_ARG(QVariant, c)); break;
    default: qFatal("call: unsupported argument count");
    }
    return result;
}
// Performs the assistive press action; false when the object does not offer it.
bool press(QObject *object)
{
    if (!actionsOf(object).contains(QAccessibleActionInterface::pressAction())) return false;
    accessible(object)->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
    return true;
}

struct Scene {
    QQmlEngine engine;
    std::unique_ptr<QObject> root;
    QQuickView window;
    Workbook *book = nullptr;
    QQuickItem *sheet = nullptr;
    QQuickItem *grid = nullptr;
    EditableGridModel *model = nullptr;
    QString budget, summary;

    // Cells are recreated when the visible records change: look them up each time.
    QQuickItem *cell(int row, int column) const
    {
        return findItem(grid, QStringLiteral("gridCell_%1_%2").arg(model->rowIdAt(row), model->columnIdAt(column)));
    }
};

bool build(Scene &scene)
{
    scene.engine.addImportPath(QString::fromUtf8(QINDATK_BUILD_QML_DIR));
    QQmlComponent component(&scene.engine);
    component.setData(R"(
        import QtQuick
        import QindaTK as Tk
        Item {
            width: 960; height: 600
            property alias book: book
            property alias sheet: sheet
            Tk.Workbook { id: book }
            Tk.Flex {
                anchors.fill: parent; direction: Tk.Flex.Column; gap: Tk.Theme.space.xs
                Tk.WorkbookTabs {
                    objectName: "tabs"
                    workbook: book
                    currentSheetId: sheet.currentSheetId
                    onSheetActivated: function(sheetId) { sheet.sheetId = sheetId }
                }
                Tk.FormulaBar { objectName: "formula"; sheetView: sheet; Tk.Flex.alignSelf: Tk.Flex.Stretch }
                Tk.SheetView {
                    id: sheet
                    objectName: "sheet"
                    workbook: book
                    tooltip: "Budget workbook"
                    Tk.Flex.grow: 1
                    Tk.Flex.alignSelf: Tk.Flex.Stretch
                }
            }
        }
    )", QUrl(QStringLiteral("inline:/SheetAccessibility.qml")));
    // The module's types load asynchronously.
    if (!QTest::qWaitFor([&] { return !component.isLoading(); }, 10000) || !component.isReady()) {
        qWarning() << component.errorString();
        return false;
    }
    scene.root.reset(component.create());
    if (!scene.root) {
        qWarning() << component.errorString();
        return false;
    }
    auto *rootItem = qobject_cast<QQuickItem *>(scene.root.get());
    scene.book = qobject_cast<Workbook *>(scene.root->property("book").value<QObject *>());
    scene.sheet = qobject_cast<QQuickItem *>(scene.root->property("sheet").value<QObject *>());
    if (!rootItem || !scene.book || !scene.sheet) return false;
    Workbook &book = *scene.book;
    scene.budget = book.sheetIdAt(0);
    if (!book.renameSheet(scene.budget, QStringLiteral("Budget"))) return false;
    scene.summary = book.addSheet(QStringLiteral("Summary"));
    if (scene.summary.isEmpty()
        || !book.pasteInputs(scene.budget, 0, 0, {QVariantList{QStringLiteral("Item"), QStringLiteral("Q1")},
                                                  QVariantList{QStringLiteral("Rent"), QStringLiteral("1200")},
                                                  QVariantList{QStringLiteral("Payroll"), QStringLiteral("5400")},
                                                  QVariantList{QStringLiteral("Total"), QStringLiteral("=SUM(B2:B3)")}})
        || !book.applyStyle(scene.budget, 1, 1, 3, 1, {{QStringLiteral("numberFormat"), QStringLiteral("#,##0.00")}}))
        return false;
    scene.sheet->setProperty("sheetId", scene.budget);
    scene.window.resize(960, 600);
    rootItem->setParentItem(scene.window.contentItem());
    scene.window.show();
    scene.window.requestActivate();
    if (!QTest::qWaitForWindowActive(&scene.window)) return false;
    scene.grid = qobject_cast<QQuickItem *>(scene.sheet->property("grid").value<QObject *>());
    scene.model = scene.grid ? qobject_cast<EditableGridModel *>(scene.grid->property("model").value<QObject *>()) : nullptr;
    return scene.grid && scene.model;
}
} // namespace

class SheetAccessibilityTest : public QObject {
    Q_OBJECT
private slots:
    void assistiveTreeAndActions();
    void inputMethodCompositionCommits();
};

void SheetAccessibilityTest::assistiveTreeAndActions()
{
    Scene scene;
    QVERIFY(build(scene));
    call(scene.sheet, "setCurrent", {3, 1, false}); // B4
    QTRY_VERIFY(scene.cell(2, 1) != nullptr);

    // The sheet pane, the table and a cell: roles, names and descriptions.
    QCOMPARE(accessible(scene.sheet)->role(), QAccessible::Pane);
    QCOMPARE(nameOf(scene.sheet), QStringLiteral("Budget workbook, sheet Budget"));
    QTRY_COMPARE(descriptionOf(scene.sheet), QStringLiteral("Cell B4. Formula or value: =SUM(B2:B3). Result: 6,600.00"));
    QCOMPARE(accessible(scene.grid)->role(), QAccessible::Table);
    QCOMPARE(nameOf(scene.grid), QStringLiteral("Sheet Budget"));
    QVERIFY(descriptionOf(scene.grid).startsWith(QStringLiteral("Row 4, column 2. Value: 6,600.00.")));
    QObject *b3 = scene.cell(2, 1);
    QCOMPARE(accessible(b3)->role(), QAccessible::Cell);
    QCOMPARE(nameOf(b3), QStringLiteral("Row 3, column 2: 5,400.00"));
    QVERIFY(!accessible(b3)->state().selected);
    QVERIFY(accessible(scene.cell(3, 1))->state().selected);
    QVERIFY(actionsOf(b3).contains(QAccessibleActionInterface::setFocusAction()));

    // An assistive press on B3 makes it current, as a click does; the formula
    // bar's result follows.
    QVERIFY(press(b3));
    QTRY_COMPARE(scene.sheet->property("activeAddress").toString(), QStringLiteral("B3"));
    QTRY_VERIFY(scene.cell(2, 1) && accessible(scene.cell(2, 1))->state().selected);
    QCOMPARE(descriptionOf(scene.sheet), QStringLiteral("Cell B3. Formula or value: 5400. Result: 5,400.00"));
    QObject *result = scene.root->findChild<QObject *>(QStringLiteral("formulaResult"));
    QCOMPARE(accessible(result)->role(), QAccessible::StaticText);
    QTRY_COMPARE(nameOf(result), QStringLiteral("Result of B3: 5,400.00"));

    // The fill handle names itself; keyboard users fill with Ctrl+D / Ctrl+R.
    QObject *handle = findItem(scene.grid, QStringLiteral("gridFillHandle"));
    QCOMPARE(accessible(handle)->role(), QAccessible::Grip);
    QCOMPARE(nameOf(handle), QStringLiteral("Fill handle"));
    QVERIFY(!descriptionOf(handle).isEmpty());

    // Sheet tabs: an assistive press switches sheets, as a click does.
    QObject *tabs = scene.root->findChild<QObject *>(QStringLiteral("tabs"));
    QCOMPARE(accessible(tabs)->role(), QAccessible::PageTabList);
    QObject *budgetTab = findItem(qobject_cast<QQuickItem *>(tabs), QStringLiteral("workbookTab_") + scene.budget);
    QObject *summaryTab = findItem(qobject_cast<QQuickItem *>(tabs), QStringLiteral("workbookTab_") + scene.summary);
    QCOMPARE(accessible(summaryTab)->role(), QAccessible::PageTab);
    QCOMPARE(nameOf(summaryTab), QStringLiteral("Summary"));
    QVERIFY(descriptionOf(summaryTab).startsWith(QStringLiteral("Sheet 2 of 2.")));
    QVERIFY(accessible(budgetTab)->state().selected);
    QVERIFY(!accessible(summaryTab)->state().selected);
    QVERIFY(press(summaryTab));
    QTRY_COMPARE(scene.sheet->property("currentSheetId").toString(), scene.summary);
    QCOMPARE(nameOf(scene.sheet), QStringLiteral("Budget workbook, sheet Summary"));
    QTRY_VERIFY(accessible(summaryTab)->state().selected);
    QVERIFY(!accessible(budgetTab)->state().selected);
}

void SheetAccessibilityTest::inputMethodCompositionCommits()
{
    Scene scene;
    QVERIFY(build(scene));
    call(scene.sheet, "setCurrent", {4, 0, false}); // A5, empty
    QVERIFY(call(scene.sheet, "beginEdit").toBool());
    QQuickItem *editor = findItem(scene.grid, QStringLiteral("gridCellEditor"));
    QVERIFY(editor);
    QTRY_VERIFY(editor->hasActiveFocus());

    // Composition shows as preedit and commits nothing until it is accepted.
    QInputMethodEvent preedit(QStringLiteral("ni"), {});
    QCoreApplication::sendEvent(editor, &preedit);
    QCOMPARE(editor->property("preeditText").toString(), QStringLiteral("ni"));
    QVERIFY(scene.book->cellInput(scene.budget, 4, 0).isEmpty());
    QInputMethodEvent commit;
    commit.setCommitString(QStringLiteral("你好"));
    QCoreApplication::sendEvent(editor, &commit);
    QCOMPARE(editor->property("text").toString(), QStringLiteral("你好"));
    QVERIFY(editor->property("preeditText").toString().isEmpty());

    // Enter commits the composed text to the workbook as one edit.
    QTest::keyClick(&scene.window, Qt::Key_Return);
    QTRY_COMPARE(scene.book->cellInput(scene.budget, 4, 0), QStringLiteral("你好"));
    QCOMPARE(scene.book->cellDisplay(scene.budget, 4, 0), QStringLiteral("你好"));
    QVERIFY(!scene.grid->property("cellEditing").toBool());
    QVERIFY(scene.book->undo());
    QVERIFY(scene.book->cellInput(scene.budget, 4, 0).isEmpty());
}

QTEST_MAIN(SheetAccessibilityTest)
#include "tst_sheet_accessibility.moc"
