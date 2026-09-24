// SPDX-License-Identifier: LGPL-3.0-or-later
#include "pdf_annotation_document.h"

#include <QPainter>
#include <QPdfWriter>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

class PdfAnnotationDocumentTest : public QObject {
    Q_OBJECT
private slots:
    void annotationRoundtrip()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString original = directory.filePath(QStringLiteral("original.pdf"));
        {
            QPdfWriter writer(original);
            writer.setResolution(72);
            QPainter painter(&writer);
            QVERIFY(painter.isActive());
            painter.drawText(QPointF(72, 80), QStringLiteral("QindaTK PDF annotation test"));
            painter.end();
        }
        PdfAnnotationDocument document;
        if (!document.capabilities().contains(QStringLiteral("annotationKinds")))
            QSKIP("PDF annotation backend is unavailable");
        if (!document.open(original)) {
            if (document.errorString().contains(QStringLiteral("no Poppler")))
                QSKIP("Poppler Qt6 is unavailable");
            QFAIL(qPrintable(document.errorString()));
        }
        QCOMPARE(document.pageCount(), 1);
        const QVariantMap bounds{{QStringLiteral("x"), 72}, {QStringLiteral("y"), 68},
            {QStringLiteral("width"), 180}, {QStringLiteral("height"), 18}};
        const QString note = document.addAnnotation(0, QStringLiteral("note"), bounds,
            QStringLiteral("Check this phrase"), QStringLiteral("Editor"));
        QVERIFY2(!note.isEmpty(), qPrintable(document.errorString()));
        const QString highlight = document.addAnnotation(0, QStringLiteral("highlight"), bounds,
            QStringLiteral("Marked text"));
        QVERIFY2(!highlight.isEmpty(), qPrintable(document.errorString()));
        QCOMPARE(document.annotations().size(), 2);
        QVERIFY(document.dirty());
        const QString saved = directory.filePath(QStringLiteral("annotated.pdf"));
        QVERIFY2(document.saveAs(saved), qPrintable(document.errorString()));
        QVERIFY(!document.dirty());
        QCOMPARE(document.annotations().size(), 2);
        PdfAnnotationDocument reopened;
        QVERIFY2(reopened.open(saved), qPrintable(reopened.errorString()));
        QCOMPARE(reopened.annotations().size(), 2);
        QCOMPARE(reopened.annotations().at(0).toMap().value(QStringLiteral("id")).toString(), note);
        QCOMPARE(reopened.annotations().at(1).toMap().value(QStringLiteral("id")).toString(), highlight);
        QVERIFY(reopened.removeAnnotation(note));
        const QString edited = directory.filePath(QStringLiteral("edited.pdf"));
        QVERIFY2(reopened.saveAs(edited), qPrintable(reopened.errorString()));
        PdfAnnotationDocument finalDocument;
        QVERIFY2(finalDocument.open(edited), qPrintable(finalDocument.errorString()));
        QCOMPARE(finalDocument.annotations().size(), 1);
        QCOMPARE(finalDocument.annotations().first().toMap().value(QStringLiteral("id")).toString(), highlight);
    }
};

QTEST_MAIN(PdfAnnotationDocumentTest)
#include "tst_pdf_annotation_document.moc"
