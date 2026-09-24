// SPDX-License-Identifier: LGPL-3.0-or-later
#include "pdf_annotation_document.h"

#include <QFileInfo>
#include <QSaveFile>
#include <QUrl>
#include <QUuid>
#include <cmath>

#ifdef QINDATK_HAVE_POPPLER_QT6
#include <poppler-qt6.h>
#include <poppler-converter.h>
#endif

namespace QindaTK {

struct PdfAnnotationDocument::Private {
    QString path;
    QString error;
    QVariantList annotations;
    int pages = 0;
    bool dirty = false;
#ifdef QINDATK_HAVE_POPPLER_QT6
    std::unique_ptr<Poppler::Document> document;
#endif
};

PdfAnnotationDocument::PdfAnnotationDocument(QObject *parent)
    : QObject(parent), d(std::make_unique<Private>()) {}
PdfAnnotationDocument::~PdfAnnotationDocument() = default;

QString PdfAnnotationDocument::sourcePath() const { return d->path; }
int PdfAnnotationDocument::pageCount() const { return d->pages; }
QVariantList PdfAnnotationDocument::annotations() const { return d->annotations; }
bool PdfAnnotationDocument::dirty() const { return d->dirty; }
QString PdfAnnotationDocument::errorString() const { return d->error; }

QVariantMap PdfAnnotationDocument::capabilities() const
{
#ifdef QINDATK_HAVE_POPPLER_QT6
    const bool writable = d->document && d->document->okToAddNotes();
    return {{QStringLiteral("annotationKinds"), writable
                ? QStringList{QStringLiteral("note"), QStringLiteral("highlight")} : QStringList{}},
        {QStringLiteral("canDelete"), writable}, {QStringLiteral("canSave"), writable},
        {QStringLiteral("canAccept"), false}, {QStringLiteral("canReject"), false},
        {QStringLiteral("canEditForms"), false}, {QStringLiteral("canApplyRedaction"), false}};
#else
    return {{QStringLiteral("annotationKinds"), QStringList{}},
        {QStringLiteral("canDelete"), false}, {QStringLiteral("canSave"), false},
        {QStringLiteral("canAccept"), false}, {QStringLiteral("canReject"), false},
        {QStringLiteral("canEditForms"), false}, {QStringLiteral("canApplyRedaction"), false}};
#endif
}

bool PdfAnnotationDocument::fail(const QString &message)
{
    d->error = message;
    emit changed();
    return false;
}

bool PdfAnnotationDocument::open(const QString &path)
{
#ifdef QINDATK_HAVE_POPPLER_QT6
    const QUrl url(path);
    const QFileInfo info(url.isLocalFile() ? url.toLocalFile() : path);
    if (!info.isFile()) return fail(QStringLiteral("PDF file does not exist"));
    auto candidate = Poppler::Document::load(info.absoluteFilePath());
    if (!candidate || candidate->isLocked())
        return fail(QStringLiteral("PDF could not be opened or requires a password"));
    d->document = std::move(candidate);
    d->path = info.absoluteFilePath();
    d->pages = d->document->numPages();
    d->dirty = false;
    d->error.clear();
    refresh();
    return true;
#else
    Q_UNUSED(path)
    return fail(QStringLiteral("This QindaTK build has no Poppler Qt6 annotation writer"));
#endif
}

void PdfAnnotationDocument::refresh()
{
    d->annotations.clear();
#ifdef QINDATK_HAVE_POPPLER_QT6
    if (d->document) {
        for (int pageIndex = 0; pageIndex < d->pages; ++pageIndex) {
            auto page = d->document->page(pageIndex);
            if (!page) continue;
            const QSizeF size = page->pageSizeF();
            auto records = page->annotations();
            for (size_t index = 0; index < records.size(); ++index) {
                const auto &annotation = records[index];
                QString kind;
                if (annotation->subType() == Poppler::Annotation::AText)
                    kind = QStringLiteral("note");
                else if (annotation->subType() == Poppler::Annotation::AHighlight)
                    kind = QStringLiteral("highlight");
                else continue;
                const QRectF normalized = annotation->boundary();
                const QString nativeId = annotation->uniqueName().isEmpty()
                    ? QStringLiteral("#%1").arg(index) : annotation->uniqueName();
                d->annotations.append(QVariantMap{
                    {QStringLiteral("id"), QStringLiteral("p%1:%2").arg(pageIndex).arg(nativeId)},
                    {QStringLiteral("pageIndex"), pageIndex}, {QStringLiteral("kind"), kind},
                    {QStringLiteral("text"), annotation->contents()},
                    {QStringLiteral("author"), annotation->author()},
                    {QStringLiteral("status"), QStringLiteral("in-document")},
                    {QStringLiteral("bounds"), QVariantMap{
                        {QStringLiteral("x"), normalized.x() * size.width()},
                        {QStringLiteral("y"), normalized.y() * size.height()},
                        {QStringLiteral("width"), normalized.width() * size.width()},
                        {QStringLiteral("height"), normalized.height() * size.height()}}}});
            }
        }
    }
#endif
    emit changed();
}

QString PdfAnnotationDocument::addAnnotation(int pageIndex, const QString &kind,
                                              const QVariantMap &boundsPoints,
                                              const QString &text, const QString &author)
{
#ifdef QINDATK_HAVE_POPPLER_QT6
    if (!d->document || !d->document->okToAddNotes()) {
        fail(QStringLiteral("PDF annotations are unavailable or prohibited"));
        return {};
    }
    if (pageIndex < 0 || pageIndex >= d->pages) {
        fail(QStringLiteral("PDF page index is out of range"));
        return {};
    }
    auto page = d->document->page(pageIndex);
    if (!page) { fail(QStringLiteral("PDF page could not be loaded")); return {}; }
    const QSizeF size = page->pageSizeF();
    bool xOk = false, yOk = false, wOk = false, hOk = false;
    const double x = boundsPoints.value(QStringLiteral("x")).toDouble(&xOk);
    const double y = boundsPoints.value(QStringLiteral("y")).toDouble(&yOk);
    const double w = boundsPoints.value(QStringLiteral("width")).toDouble(&wOk);
    const double h = boundsPoints.value(QStringLiteral("height")).toDouble(&hOk);
    if (!xOk || !yOk || !wOk || !hOk || !std::isfinite(x) || !std::isfinite(y)
        || !std::isfinite(w) || !std::isfinite(h) || x < 0 || y < 0 || w <= 0 || h <= 0
        || x + w > size.width() || y + h > size.height()) {
        fail(QStringLiteral("Annotation bounds must fit the unrotated page in points"));
        return {};
    }
    std::unique_ptr<Poppler::Annotation> annotation;
    if (kind == QStringLiteral("note")) {
        annotation = std::make_unique<Poppler::TextAnnotation>(Poppler::TextAnnotation::Linked);
    } else if (kind == QStringLiteral("highlight")) {
        auto highlight = std::make_unique<Poppler::HighlightAnnotation>();
        Poppler::HighlightAnnotation::Quad quad{};
        quad.points[0] = QPointF(x / size.width(), y / size.height());
        quad.points[1] = QPointF((x + w) / size.width(), y / size.height());
        quad.points[2] = QPointF(x / size.width(), (y + h) / size.height());
        quad.points[3] = QPointF((x + w) / size.width(), (y + h) / size.height());
        quad.feather = 0.1;
        highlight->setHighlightQuads({quad});
        annotation = std::move(highlight);
    } else {
        fail(QStringLiteral("Unsupported PDF annotation kind"));
        return {};
    }
    const QString uniqueName = QUuid::createUuid().toString(QUuid::WithoutBraces);
    annotation->setUniqueName(uniqueName);
    annotation->setBoundary(QRectF(x / size.width(), y / size.height(),
                                    w / size.width(), h / size.height()));
    annotation->setContents(text);
    annotation->setAuthor(author);
    page->addAnnotation(annotation.get());
    d->dirty = true;
    d->error.clear();
    refresh();
    return QStringLiteral("p%1:%2").arg(pageIndex).arg(uniqueName);
#else
    Q_UNUSED(pageIndex) Q_UNUSED(kind) Q_UNUSED(boundsPoints) Q_UNUSED(text) Q_UNUSED(author)
    fail(QStringLiteral("This QindaTK build has no Poppler Qt6 annotation writer"));
    return {};
#endif
}

bool PdfAnnotationDocument::removeAnnotation(const QString &id)
{
#ifdef QINDATK_HAVE_POPPLER_QT6
    if (!d->document || !d->document->okToAddNotes())
        return fail(QStringLiteral("PDF annotations are unavailable or prohibited"));
    for (int pageIndex = 0; pageIndex < d->pages; ++pageIndex) {
        auto page = d->document->page(pageIndex);
        if (!page) continue;
        auto records = page->annotations();
        for (size_t index = 0; index < records.size(); ++index) {
            const QString nativeId = records[index]->uniqueName().isEmpty()
                ? QStringLiteral("#%1").arg(index) : records[index]->uniqueName();
            if (id != QStringLiteral("p%1:%2").arg(pageIndex).arg(nativeId)) continue;
            // Poppler takes ownership and destroys the wrapper on removal.
            page->removeAnnotation(records[index].release());
            d->dirty = true;
            d->error.clear();
            refresh();
            return true;
        }
    }
    return fail(QStringLiteral("PDF annotation ID was not found"));
#else
    Q_UNUSED(id)
    return fail(QStringLiteral("This QindaTK build has no Poppler Qt6 annotation writer"));
#endif
}

bool PdfAnnotationDocument::saveAs(const QString &path)
{
#ifdef QINDATK_HAVE_POPPLER_QT6
    if (!d->document || !d->document->okToAddNotes())
        return fail(QStringLiteral("PDF annotations are unavailable or prohibited"));
    if (path.isEmpty()) return fail(QStringLiteral("PDF output path is empty"));
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) return fail(output.errorString());
    auto converter = d->document->pdfConverter();
    converter->setPDFOptions(Poppler::PDFConverter::WithChanges);
    converter->setOutputDevice(&output);
    if (!converter->convert()) return fail(QStringLiteral("PDF annotation export failed"));
    if (!output.commit()) return fail(output.errorString());
    return open(path);
#else
    Q_UNUSED(path)
    return fail(QStringLiteral("This QindaTK build has no Poppler Qt6 annotation writer"));
#endif
}

} // namespace QindaTK
