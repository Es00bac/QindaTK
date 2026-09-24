// SPDX-License-Identifier: LGPL-3.0-or-later
#include "code_document.h"
#include "code_language_service.h"

#include <QSignalSpy>
#include <QStandardPaths>
#include <QtTest>

using QindaTK::CodeDocument;
using QindaTK::CodeLanguageService;

class CodeLanguageServiceTest : public QObject {
    Q_OBJECT
private slots:
    void clangdDiagnosticsCompletionAndCleanup();
};

void CodeLanguageServiceTest::clangdDiagnosticsCompletionAndCleanup()
{
    const QString clangd = QStandardPaths::findExecutable(QStringLiteral("clangd"));
    if (clangd.isEmpty()) QSKIP("clangd is not installed");

    CodeDocument document;
    document.setText(QStringLiteral("// 🦊 UTF-16 source offset\nint main() { return unknownName; }\n"));
    CodeLanguageService service;
    service.setDocument(&document);
    service.setProgram(clangd);
    service.setArguments({QStringLiteral("--background-index=false"), QStringLiteral("--log=error")});
    service.setLanguageId(QStringLiteral("cpp"));
    QCOMPARE(service.status(), QStringLiteral("stopped"));
    QVERIFY(service.start());
    QTRY_COMPARE_WITH_TIMEOUT(service.status(), QStringLiteral("ready"), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!document.diagnostics().isEmpty(), 15000);
    QCOMPARE(document.diagnostics().first().toMap().value(QStringLiteral("severity")).toString(),
             QStringLiteral("error"));
    QCOMPARE(document.diagnostics().first().toMap().value(QStringLiteral("start")).toInt(),
             document.text().indexOf(QStringLiteral("unknownName")));

    QSignalSpy diagnostics(&document, &CodeDocument::diagnosticsChanged);
    document.setText(QStringLiteral("int alphaSymbol;\nint main() { alp }\n"));
    const int revised = document.revision();
    QTRY_VERIFY_WITH_TIMEOUT(!document.diagnostics().isEmpty() || diagnostics.count() > 1, 15000);
    for (const QVariant &entry : document.diagnostics())
        QVERIFY(!entry.toMap().value(QStringLiteral("message")).toString()
                    .contains(QStringLiteral("unknownName")));
    const int completionEnd = document.text().indexOf(QStringLiteral("alp }")) + 3;
    QVERIFY(completionEnd >= 3);
    QVERIFY(service.requestCompletion(completionEnd, completionEnd - 3));
    QTRY_VERIFY_WITH_TIMEOUT(!document.completions().isEmpty(), 15000);
    QCOMPARE(document.revision(), revised);
    bool found = false;
    for (const QVariant &entry : document.completions()) {
        if (entry.toMap().value(QStringLiteral("label")).toString().contains(QStringLiteral("alphaSymbol"))) {
            found = true;
            break;
        }
    }
    QVERIFY(found);
    QVERIFY(service.requestCompletion(completionEnd, completionEnd - 3));
    QVERIFY(document.replace(0, 0, QStringLiteral("// newer edit\n")));
    QVERIFY(document.completions().isEmpty());
    QTest::qWait(150);
    QVERIFY(document.completions().isEmpty());
    service.stop();
    QTRY_COMPARE_WITH_TIMEOUT(service.status(), QStringLiteral("stopped"), 5000);
    QVERIFY(document.completions().isEmpty());
    QVERIFY(document.diagnostics().isEmpty());
}

QTEST_MAIN(CodeLanguageServiceTest)
#include "tst_code_language_service.moc"
