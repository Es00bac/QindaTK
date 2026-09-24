// SPDX-License-Identifier: LGPL-3.0-or-later
#include "code_document.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using QindaTK::CodeDocument;

class CodeDocumentTest : public QObject {
    Q_OBJECT
private slots:
    void unicodeEditHistoryAndFind();
    void rejectsStaleProviderResults();
    void utf8SaveReopen();
};

void CodeDocumentTest::unicodeEditHistoryAndFind()
{
    CodeDocument document;
    document.setText(QStringLiteral("int café = 1;\nconst 🦊 = café;\n"));
    QCOMPARE(document.lineCount(), 3);
    const int originalRevision = document.revision();
    const QVariantMap hit = document.findNext(QStringLiteral("café"), 5);
    QCOMPARE(hit.value(QStringLiteral("start")).toInt(),
             document.text().lastIndexOf(QStringLiteral("café")));
    QVERIFY(document.replace(0, 3, QStringLiteral("let")));
    QCOMPARE(document.text().left(3), QStringLiteral("let"));
    QVERIFY(document.revision() > originalRevision);
    QVERIFY(document.undo());
    QCOMPARE(document.text().left(3), QStringLiteral("int"));
    QVERIFY(document.redo());
    QCOMPARE(document.text().left(3), QStringLiteral("let"));
    QCOMPARE(document.lineColumn(4).value(QStringLiteral("column")).toInt(), 5);
}

void CodeDocumentTest::rejectsStaleProviderResults()
{
    CodeDocument document;
    document.setText(QStringLiteral("let answer = 42"));
    const int revision = document.revision();
    const QVariantMap token {{QStringLiteral("start"), 0},
                             {QStringLiteral("length"), 3},
                             {QStringLiteral("kind"), QStringLiteral("keyword")}};
    const QVariantList tokens {token};
    QVERIFY(document.publishSyntaxTokens(revision, tokens));
    const QVariantMap diagnostic {{QStringLiteral("start"), 4},
                                  {QStringLiteral("length"), 6},
                                  {QStringLiteral("message"), QStringLiteral("sample")}};
    QVERIFY(document.publishDiagnostics(revision, {diagnostic}));
    QCOMPARE(document.diagnostics().first().toMap().value(QStringLiteral("line")).toInt(), 1);
    QVERIFY(document.replace(0, 0, QStringLiteral("// ")));
    QVERIFY(document.syntaxTokens().isEmpty());
    QVERIFY(document.diagnostics().isEmpty());
    QVERIFY(!document.publishSyntaxTokens(revision, tokens));
    QVERIFY(!document.publishDiagnostics(revision, {}));
}

void CodeDocumentTest::utf8SaveReopen()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("sample.cpp"));
    CodeDocument document;
    document.setText(QStringLiteral("// 日本語 café 🦊\nint main() {}\n"));
    QVERIFY(document.save(path));
    QVERIFY(!document.modified());
    QVERIFY(document.replace(0, 2, QStringLiteral("/*")));
    QVERIFY(document.modified());
    QVERIFY(document.load(path));
    QCOMPARE(document.text(), QStringLiteral("// 日本語 café 🦊\nint main() {}\n"));
    QVERIFY(!document.modified());
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(file.readAll()), document.text());
}

QTEST_MAIN(CodeDocumentTest)
#include "tst_code_document.moc"
