// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QLineF>
#include <QQmlEngine>
#include <QQuickView>
#include <QThread>
#include <QUrl>

#include <qindatk/canvas_document.h>
#include <qindatk/canvas_surface.h>
#include <qindatk/tool_controller.h>

namespace {
bool check(bool good, const char *what)
{
    if (!good) qCritical("WP-040 installed consumer: %s", what);
    return good;
}

bool pixelMatches(const QColor &pixel, const QColor &expected)
{
    return qAbs(pixel.red() - expected.red()) <= 3
        && qAbs(pixel.green() - expected.green()) <= 3
        && qAbs(pixel.blue() - expected.blue()) <= 3;
}

bool awaitRenderedPixel(QQuickView &window, QindaTK::CanvasSurface &canvas,
                        const QindaTK::CanvasDocument &document, QPointF documentPoint,
                        const QColor &expected)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 3000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
        const QImage image = window.grabWindow();
        const QPointF viewPoint = canvas.mapFromDocument(documentPoint);
        const QPoint sample(qRound(viewPoint.x() * image.devicePixelRatio()),
                            qRound(viewPoint.y() * image.devicePixelRatio()));
        const QVariantMap stats = canvas.renderStats();
        if (!image.isNull() && image.rect().contains(sample)
            && stats.value(QStringLiteral("visibleTiles")).toInt() > 0
            && stats.value(QStringLiteral("pendingTiles")).toInt() == 0
            && stats.value(QStringLiteral("renderedRevision")).toULongLong()
                   == document.revision()
            && pixelMatches(image.pixelColor(sample), expected))
            return true;
        QThread::msleep(10);
    }
    qCritical("WP-040 installed consumer: render boundary timed out at revision %llu",
              static_cast<unsigned long long>(document.revision()));
    return false;
}
} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QQuickView window;
    window.engine()->addImportPath(QStringLiteral(QINDATK_QML_IMPORT_ROOT));
    window.setResizeMode(QQuickView::SizeRootObjectToView);
    window.setSource(QUrl::fromLocalFile(QStringLiteral(CONSUMER_QML_FILE)));
    if (!check(window.status() == QQuickView::Ready && window.rootObject(),
               "installed QML import")) return 1;
    window.resize(480, 360);
    window.show();

    auto *root = window.rootObject();
    auto *document = root->findChild<QindaTK::CanvasDocument *>(
        QStringLiteral("installedCanvasDocument"));
    auto *tools = root->findChild<QindaTK::ToolController *>(
        QStringLiteral("installedToolController"));
    auto *canvas = root->findChild<QindaTK::CanvasSurface *>(
        QStringLiteral("installedCanvasView"));
    if (!check(document && tools && canvas, "installed public canvas types")) return 2;
    if (!check(canvas->document() == document && canvas->toolController() == tools
                   && tools->document() == document
                   && canvas->renderSurface()->effectiveBackend() == QStringLiteral("software"),
               "document, tool and rendering boundary bindings")) return 3;

    const QPointF sample(60, 60);
    if (!check(awaitRenderedPixel(window, *canvas, *document, sample, Qt::white),
               "initial page render")) return 4;
    const quint64 originalRevision = document->revision();
    if (!check(tools->begin(QStringLiteral("installed"), 20, 20)
                   && tools->update(QStringLiteral("installed"), 100, 100)
                   && document->objectCount() == 0 && document->revision() == originalRevision,
               "gesture preview does not edit the document")) return 5;
    if (!check(tools->end(QStringLiteral("installed"), 100, 100)
                   && document->objectCount() == 1
                   && document->revision() == originalRevision + 1
                   && tools->lastOutcome() == QStringLiteral("committed"),
               "rectangle gesture commits exactly once")) return 6;
    const QPointF viewPoint = canvas->mapFromDocument(sample);
    if (!check(QLineF(canvas->mapToDocument(viewPoint), sample).length() < 1e-6,
               "installed view coordinate transform")) return 7;
    if (!check(awaitRenderedPixel(window, *canvas, *document, sample, Qt::red),
               "edited document reaches installed render surface")) return 8;

    if (!check(document->undo() && document->objectCount() == 0,
               "installed document undo")) return 9;
    if (!check(awaitRenderedPixel(window, *canvas, *document, sample, Qt::white),
               "undo damage invalidates installed render surface")) return 10;
    return 0;
}
