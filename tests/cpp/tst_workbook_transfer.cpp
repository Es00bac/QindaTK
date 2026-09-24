// SPDX-License-Identifier: LGPL-3.0-or-later
#include "clipboard_service.h"
#include "transfer_payload.h"
#include "workbook.h"
#include <QMimeData>
#include <QtTest>

using namespace QindaTK;

namespace {
class TestClipboard final : public ClipboardAdapter {
public:
    ~TestClipboard() override { delete m_data; }
    const QMimeData *mimeData() const override { return m_data; }
    bool setMimeData(QMimeData *data) override
    {
        delete m_data;
        m_data = data;
        emit changed();
        return true;
    }
private:
    QMimeData *m_data = nullptr;
};

bool put(Workbook &wb, const QString &sheet, int row, int column, const QString &input)
{
    const bool ok = wb.setCellInput(sheet, row, column, input);
    if (!ok) qWarning() << row << column << input << wb.lastError();
    return ok;
}
}

class WorkbookTransferTest : public QObject {
    Q_OBJECT
private slots:
    void rangeCarriesInputsFormatsAndText();
    void crossWorkbookPasteRemapsSheetsInOneStep();
    void sameDocumentPasteKeepsSheetIdentity();
    void pasteHonorsPolicyAndRejectsDamage();
    void clipboardPublishesRangeAndText();
};

void WorkbookTransferTest::rangeCarriesInputsFormatsAndText()
{
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    QVERIFY(put(wb, s, 0, 0, "1234.5"));
    QVERIFY(put(wb, s, 0, 1, "=A1*2"));
    QVERIFY(put(wb, s, 1, 0, "tab\there"));
    QVERIFY(wb.applyStyle(s, 0, 0, 0, 1, {{"numberFormat", "#,##0.00"}, {"bold", true}}));
    QVERIFY(wb.mergeCells(s, 2, 0, 2, 1));
    const QVariantMap transfer = wb.rangeTransfer(s, 2, 1, 0, 0); // any corner order
    QVERIFY(transfer.value("ok").toBool());
    QCOMPARE(transfer.value("text").toString(), QStringLiteral("1,234.50\t2,469.00\ntab here\t\n\t\n"));
    const QVariantMap range = transfer.value("range").toMap();
    QCOMPARE(range.value("schema").toString(), QStringLiteral("qindatk.workbook.range"));
    QCOMPARE(range.value("rows").toInt(), 3);
    QCOMPARE(range.value("columns").toInt(), 2);
    QCOMPARE(range.value("cells").toList().size(), 9); // three stored cells
    QCOMPARE(range.value("cells").toList().at(5).toString(), QStringLiteral("=A1*2"));
    QCOMPARE(range.value("merges").toStringList(), QStringList{"A3:B3"});
    QVERIFY(!wb.rangeTransfer(s, 0, 0, 5000, 0).value("ok").toBool());
}

void WorkbookTransferTest::crossWorkbookPasteRemapsSheetsInOneStep()
{
    Workbook source;
    const QString report = source.sheetIdAt(0);
    QVERIFY(source.renameSheet(report, "Report"));
    const QString data = source.addSheet("Data");
    source.addSheet("Only1");
    QVERIFY(source.pasteInputs(data, 0, 0, {QVariant(QVariantList{"1"}), QVariant(QVariantList{"2"}),
                                            QVariant(QVariantList{"3"}), QVariant(QVariantList{"4"})}));
    QVERIFY(put(source, report, 0, 0, "=SUM(Data!A1:A3)"));
    QVERIFY(put(source, report, 0, 1, "=A1*2"));
    QVERIFY(put(source, report, 0, 2, "=Only1!$A$1"));
    QVERIFY(put(source, report, 0, 3, "'=literal text"));
    QVERIFY(source.applyStyle(report, 0, 0, 0, 0, {{"fillColor", "#ffeeaa"}}));
    const QVariantMap range = source.rangeTransfer(report, 0, 0, 0, 3).value("range").toMap();

    Workbook target;
    const QString sheet = target.sheetIdAt(0);
    const QString targetData = target.addSheet("data"); // names match without case
    QVERIFY(target.pasteInputs(targetData, 0, 0, {QVariant(QVariantList{"10"}), QVariant(QVariantList{"20"}),
                                                  QVariant(QVariantList{"30"}), QVariant(QVariantList{"40"})}));
    QVERIFY(put(target, sheet, 1, 1, "old"));
    QVERIFY(target.applyStyle(sheet, 1, 1, 1, 4, {{"italic", true}}));
    const qint64 steps = target.statistics().value("historySteps").toLongLong();
    QVERIFY2(target.pasteTransfer(sheet, 1, 1, range), qPrintable(target.lastError()));
    QCOMPARE(target.statistics().value("historySteps").toLongLong(), steps + 1);
    QCOMPARE(target.undoText(), QStringLiteral("Paste cells"));
    // Relative references moved by the offset (+1 row, +1 column); sheets by name.
    QCOMPARE(target.cellInput(sheet, 1, 1), QStringLiteral("=SUM(data!B2:B4)"));
    QCOMPARE(target.cellInput(sheet, 1, 2), QStringLiteral("=B2*2"));
    QCOMPARE(target.cellInput(sheet, 1, 3), QStringLiteral("=#REF!")); // Only1 is not in this workbook
    QCOMPARE(target.cellDisplay(sheet, 1, 3), QStringLiteral("#REF!"));
    QCOMPARE(target.cellInput(sheet, 1, 4), QStringLiteral("'=literal text"));
    QCOMPARE(target.cellFormat(sheet, 1, 1).value("fillColor").toString(), QStringLiteral("#ffeeaa"));
    QVERIFY(!target.cellFormat(sheet, 1, 2).contains("italic")); // the range's formats replace the target's
    QVERIFY(target.undo());
    QCOMPARE(target.cellInput(sheet, 1, 1), QStringLiteral("old"));
    QCOMPARE(target.cellFormat(sheet, 1, 2).value("italic").toBool(), true);
    QVERIFY(target.redo());
    QCOMPARE(target.cellInput(sheet, 1, 2), QStringLiteral("=B2*2"));
}

void WorkbookTransferTest::sameDocumentPasteKeepsSheetIdentity()
{
    Workbook wb;
    const QString report = wb.sheetIdAt(0);
    const QString data = wb.addSheet("Data");
    QVERIFY(put(wb, report, 0, 0, "=SUM(Data!A1:A3)"));
    const QVariantMap range = wb.rangeTransfer(report, 0, 0, 0, 0).value("range").toMap();
    QVERIFY(wb.renameSheet(data, "Inputs")); // after the copy
    const QString other = wb.addSheet("Data"); // a new sheet takes the old name
    QVERIFY(wb.pasteTransfer(report, 4, 0, range));
    QCOMPARE(wb.cellInput(report, 4, 0), QStringLiteral("=SUM(Inputs!A5:A7)"));
    QVERIFY(wb.removeSheet(other));
    QVERIFY(wb.removeSheet(data));
    QVERIFY(wb.pasteTransfer(report, 6, 0, range)); // its sheet is gone
    QCOMPARE(wb.cellInput(report, 6, 0), QStringLiteral("=SUM(#REF!)"));
}

void WorkbookTransferTest::pasteHonorsPolicyAndRejectsDamage()
{
    Workbook source;
    const QString s = source.sheetIdAt(0);
    QVERIFY(put(source, s, 0, 0, "5"));
    QVERIFY(put(source, s, 1, 0, "50"));
    QVERIFY(source.applyStyle(s, 0, 0, 1, 0, {{"bold", true}}));
    const QVariantMap range = source.rangeTransfer(s, 0, 0, 1, 0).value("range").toMap();
    Workbook target;
    const QString t = target.sheetIdAt(0);
    QVERIFY(!target.setValidation(t, {{"ranges", "A1:A10"}, {"type", "wholeNumber"}, {"operator", "lessOrEqual"},
                                      {"value1", "10"}}).isEmpty());
    const QString revision = target.revision();
    QVERIFY(!target.pasteTransfer(t, 0, 0, range));
    QCOMPARE(target.revision(), revision); // nothing applied, formats included
    QVERIFY(!target.cellFormat(t, 0, 0).contains("bold"));
    QCOMPARE(target.lastPasteReport().value("invalid").toList().first().toMap().value("address").toString(),
             QStringLiteral("A2"));
    QVERIFY(target.pasteTransfer(t, 0, 0, range, QStringLiteral("skipInvalid")));
    QCOMPARE(target.cellInput(t, 0, 0), QStringLiteral("5"));
    QCOMPARE(target.cellInput(t, 1, 0), QString());
    QCOMPARE(target.cellFormat(t, 0, 0).value("bold").toBool(), true);

    QVariantMap damaged = range;
    damaged.insert("cells", QVariantList{0, 0});
    QVERIFY(!target.pasteTransfer(t, 3, 0, damaged));
    damaged = range;
    damaged.insert("schema", "something.else");
    QVERIFY(!target.pasteTransfer(t, 3, 0, damaged));
    QVERIFY(!target.pasteTransfer(t, 999, 0, range)); // outside the sheet
}

void WorkbookTransferTest::clipboardPublishesRangeAndText()
{
    Workbook wb;
    const QString s = wb.sheetIdAt(0);
    QVERIFY(put(wb, s, 0, 0, "=1+1"));
    ClipboardService clipboard;
    auto *adapter = new TestClipboard;
    clipboard.setAdapter(adapter);
    QCOMPARE(clipboard.rangePasteData().value("kind").toString(), QString());
    const QVariantMap transfer = wb.rangeTransfer(s, 0, 0, 0, 0);
    QVERIFY(clipboard.copyRange(wb.documentId(), wb.revision(), transfer.value("range").toMap(),
                                transfer.value("text").toString()));
    QVERIFY(adapter->mimeData()->hasFormat(QString::fromLatin1(TransferPayloadCodec::InternalMime)));
    QCOMPARE(adapter->mimeData()->text(), QStringLiteral("2\n")); // other applications get display text
    QVERIFY(clipboard.canPaste());
    const QVariantMap data = clipboard.rangePasteData();
    QCOMPARE(data.value("kind").toString(), QStringLiteral("range"));
    QCOMPARE(data.value("sourceDocumentId").toString(), wb.documentId());
    QVERIFY(wb.pasteTransfer(s, 2, 2, data.value("range").toMap()));
    QCOMPARE(wb.cellInput(s, 2, 2), QStringLiteral("=1+1"));
    auto *plain = new QMimeData;
    plain->setText(QStringLiteral("7\t8"));
    adapter->setMimeData(plain);
    QCOMPARE(clipboard.rangePasteData().value("kind").toString(), QStringLiteral("text"));
    QVERIFY(!clipboard.copyRange(QStringLiteral("not-a-uuid"), wb.revision(), transfer.value("range").toMap(), {}));
}

QTEST_MAIN(WorkbookTransferTest)
#include "tst_workbook_transfer.moc"
