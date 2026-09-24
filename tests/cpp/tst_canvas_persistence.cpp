// SPDX-License-Identifier: LGPL-3.0-or-later
// Canvas native format, commit atomicity and history contract risks (WP-033/WP-040).
#include "canvas_document.h"
#include "canvas_document_format.h"
#include "document_codec.h"
#include "document_manager.h"
#include "tool_controller.h"
#include "undo_service.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#include <cmath>
#include <functional>

using namespace QindaTK;

namespace {
// Edge tiles are stored at their clipped size, interior tiles at full size;
// alpha 128 exercises exact premultiplied storage.
CanvasTiledImage makeImage(QSize size, int tileSize, int seed)
{
    CanvasTiledImage image;
    image.size = size;
    image.tileSize = tileSize;
    for (int row = 0; row < image.rows(); ++row) {
        for (int column = 0; column < image.columns(); ++column) {
            const QRect r = image.tileRect(column, row);
            QImage tile(r.size(), QImage::Format_ARGB32_Premultiplied);
            for (int y = 0; y < r.height(); ++y)
                for (int x = 0; x < r.width(); ++x)
                    tile.setPixel(x, y, qPremultiply(qRgba((x + seed) & 255, (y * 3) & 255,
                                                           (column * 40 + row) & 255, 128)));
            image.tiles << tile;
        }
    }
    return image;
}

CanvasObject shaped(CanvasShape shape, const QRectF &rect, CanvasLayerId layer)
{
    CanvasObject o;
    o.shape = shape;
    o.rect = QRectF(QPointF(0, 0), rect.size());
    o.transform = QTransform::fromTranslate(rect.x(), rect.y());
    o.fill = QColor(10, 20, 30);
    o.stroke = QColor(200, 100, 50, 77);
    o.strokeWidth = 1.5;
    o.layer = layer;
    return o;
}

QByteArray encoded(const CanvasDocument &doc)
{
    return CanvasDocumentFormat::encode(doc.state());
}

// Everything a rejected edit must leave untouched: content, revision,
// history cursor and every notification the model or its history emits.
struct Observed {
    explicit Observed(CanvasDocument &doc)
        : doc(doc), bytes(encoded(doc)), revision(doc.revision()),
          steps(doc.undoService()->stepCount(&doc)), undoText(doc.undoText()),
          damaged(&doc, &CanvasDocumentAdapter::damaged), edited(&doc, &CanvasDocumentAdapter::edited),
          reset(&doc, &CanvasDocumentAdapter::reset), changed(&doc, &CanvasDocument::changed),
          history(doc.undoService(), &UndoService::historyChanged)
    {
    }
    bool unchanged() const
    {
        return encoded(doc) == bytes && doc.revision() == revision
            && doc.undoService()->stepCount(&doc) == steps && doc.undoText() == undoText
            && damaged.isEmpty() && edited.isEmpty() && reset.isEmpty() && changed.isEmpty()
            && history.isEmpty();
    }
    CanvasDocument &doc;
    const QByteArray bytes;
    const quint64 revision;
    const int steps;
    const QString undoText;
    QSignalSpy damaged, edited, reset, changed, history;
};
} // namespace

class TstCanvasPersistence final : public QObject {
    Q_OBJECT
private slots:
    void nativeRoundtripIsExact();
    void decoderRejectsInvalidFilesBeforeAdoption();
    void rejectedCommitsAndUndoChangeNothing();
    void pageLayerAndImageMutationsAreRecorded();
    void groupedEditsThatCancelOutLeaveNoStep();
    void encodedBudgetKeepsEveryCommitSaveable();
    void oneObjectGestureInLargeCanvasRetainsOnlyTheDelta();
};

void TstCanvasPersistence::nativeRoundtripIsExact()
{
    CanvasDocument doc;
    doc.populateFixture(3, 5, false); // a load: reset generation 1
    doc.setPageRect(QRectF(-20.25, 10.5, 640.125, 480.75));
    doc.setPageColor(QColor::fromRgbF(0.2f, 0.3f, 0.4f, 0.9f));
    // UTF-8 would drop a leading U+FEFF and an unpaired surrogate.
    const CanvasLayerId top = doc.addLayer(QString(QChar(0xFEFF)) + QStringLiteral("Top ✓"));
    QVERIFY(top != 0);
    const CanvasImageId image = doc.addImage(makeImage(QSize(300, 170), 128, 7));
    QVERIFY(image != 0);
    CanvasObject picture = shaped(CanvasShape::Image, QRectF(5, 6, 300, 170), top);
    picture.image = image;
    picture.transform = QTransform(0.5, 0.1, 0.0, -0.2, 0.75, 0.0, 12.5, -3.25, 1.0);
    picture.name = QStringLiteral("picture") + QChar(0xD800);
    QVERIFY(doc.insert(picture) != 0);
    CanvasObject line = shaped(CanvasShape::Polyline, QRectF(), doc.activeLayer());
    line.points = {QPointF(0.1, 0.2), QPointF(1e-9, -7.5), QPointF(123456.789, 1.0 / 3.0)};
    line.fill = {};
    line.visible = false;
    QVERIFY(doc.insert(line) != 0);
    // Identity and paint order beyond 2^53 must survive bit for bit.
    CanvasObject far = shaped(CanvasShape::Ellipse, QRectF(1, 2, 3, 4), top);
    far.id = (quint64(1) << 60) + 3;
    far.z = (qint64(1) << 55) + 7;
    far.locked = true;
    CanvasEdit edit;
    edit.label = QStringLiteral("Far");
    edit.changes.append({far.id, std::nullopt, far});
    QVERIFY(!doc.commitEdit(edit));
    QVERIFY(doc.setLayerLocked(top, true));
    QCOMPARE(doc.resetGeneration(), quint64(1));

    const DocumentCodecPtr codec = canvasDocumentCodec();
    const QJsonObject extensions{{QStringLiteral("app"), QStringLiteral("value")}};
    OperationError error;
    const DocumentCapturePtr capture = codec->capture(doc, extensions, &error);
    QVERIFY2(capture, qPrintable(error.message));
    const SnapshotPtr snapshot = encodeCapture(*codec, *capture, &error);
    QVERIFY2(snapshot, qPrintable(error.message));
    QVERIFY(!compareSnapshotHeaders(snapshot->header, doc.modelHeader()));
    QCOMPARE(encodeCapture(*codec, *codec->capture(doc, extensions))->payload, snapshot->payload);
    const auto inspected = codec->inspect(snapshot->payload);
    QVERIFY(inspected && !compareSnapshotHeaders(*inspected, snapshot->header));

    const DecodedDocumentPtr decoded = codec->decode(snapshot->payload, &error);
    QVERIFY2(decoded, qPrintable(error.message));
    QCOMPARE(decoded->extensions(), extensions);
    std::unique_ptr<DocumentModel> model = codec->adopt(*decoded, &error);
    auto *copy = dynamic_cast<CanvasDocument *>(model.get());
    QVERIFY(copy);
    const CanvasDocumentState a = doc.state();
    const CanvasDocumentState b = copy->state();
    QVERIFY(b.documentId == a.documentId);
    QCOMPARE(b.revision, a.revision);
    QCOMPARE(b.resetGeneration, a.resetGeneration);
    QCOMPARE(b.pageRect, a.pageRect);
    QCOMPARE(b.pageColor, a.pageColor);
    QCOMPARE(b.nextObject, far.id + 1);
    QCOMPARE(b.nextObject, a.nextObject);
    QCOMPARE(b.nextLayer, a.nextLayer);
    QCOMPARE(b.nextImage, a.nextImage);
    QCOMPARE(b.nextZ, a.nextZ);
    QVERIFY(b.objects == a.objects);
    QVERIFY(b.layers == a.layers);
    QCOMPARE(b.images.keys(), a.images.keys());
    const CanvasTiledImagePtr original = a.images.value(image);
    const CanvasTiledImagePtr restored = b.images.value(image);
    QCOMPARE(restored->size, original->size);
    QCOMPARE(restored->tileSize, original->tileSize);
    QCOMPARE(restored->tiles, original->tiles); // exact premultiplied pixels
    QCOMPARE(copy->objectInfo(far.id).value(QStringLiteral("id")).toULongLong(), far.id);
    QCOMPARE(copy->activeLayer(), top); // not saved: a loaded canvas starts on its top layer
    QCOMPARE(encodeCapture(*codec, *codec->capture(*copy, extensions))->payload, snapshot->payload);
}

void TstCanvasPersistence::decoderRejectsInvalidFilesBeforeAdoption()
{
    CanvasDocument doc;
    const CanvasImageId image = doc.addImage(makeImage(QSize(64, 40), 32, 1));
    CanvasObject picture = shaped(CanvasShape::Image, QRectF(0, 0, 64, 40), doc.activeLayer());
    picture.image = image;
    const CanvasObjectId pictureId = doc.insert(picture);
    const CanvasObjectId boxId = doc.insert(shaped(CanvasShape::Rect, QRectF(10, 10, 5, 5), doc.activeLayer()));
    QVERIFY(pictureId && boxId);
    const CanvasDocumentState valid = doc.state();
    const auto resealed = [](QByteArray bytes) {
        const qsizetype body = bytes.size() - 32;
        bytes.replace(body, 32, QCryptographicHash::hash(QByteArrayView(bytes).first(body),
                                                         QCryptographicHash::Sha256));
        return bytes;
    };
    struct Case {
        const char *name;
        std::function<QByteArray()> bytes;
        ErrorCode code;
        QString field;
        CanvasDocumentLimits limits = {};
    };
    const QVector<Case> cases{
        {"duplicate object id", [&] {
             CanvasDocumentState s = valid;
             CanvasObject twin = s.objects.value(boxId);
             s.objects.insert(boxId + 1000, twin); // a second record with the same id
             s.nextObject = boxId + 2000;
             return CanvasDocumentFormat::encode(s);
         }, ErrorCode::InvalidArgument, QStringLiteral("objects.id")},
        {"id beyond counter", [&] {
             CanvasDocumentState s = valid;
             s.nextObject = 1;
             return CanvasDocumentFormat::encode(s);
         }, ErrorCode::InvalidArgument, QStringLiteral("objects.id")},
        {"image reference", [&] {
             CanvasDocumentState s = valid;
             s.objects[pictureId].image = image + 40;
             return CanvasDocumentFormat::encode(s);
         }, ErrorCode::Validation, QStringLiteral("objects.image")},
        {"layer reference", [&] {
             CanvasDocumentState s = valid;
             s.objects[boxId].layer = 555;
             return CanvasDocumentFormat::encode(s);
         }, ErrorCode::Validation, QStringLiteral("objects.layer")},
        {"non-finite transform", [&] {
             CanvasDocumentState s = valid;
             s.objects[boxId].transform = QTransform(1, 0, 0, 1, std::nan(""), 0);
             return CanvasDocumentFormat::encode(s);
         }, ErrorCode::Validation, QStringLiteral("objects.geometry")},
        {"image dimensions", [&] {
             CanvasDocumentState s = valid;
             CanvasTiledImage huge = *s.images.value(image);
             huge.size = QSize(70000, 40);
             huge.tiles.clear();
             s.images.insert(image, std::make_shared<const CanvasTiledImage>(huge));
             return CanvasDocumentFormat::encode(s);
         }, ErrorCode::Unsupported, QStringLiteral("images.size")},
        {"declared pixels before tiles", [&] {
             CanvasDocumentState s = valid;
             CanvasTiledImage vast = *s.images.value(image);
             vast.size = QSize(30000, 30000); // each side allowed, total far over budget
             vast.tiles.clear();
             s.images.insert(image, std::make_shared<const CanvasTiledImage>(vast));
             return CanvasDocumentFormat::encode(s);
         }, ErrorCode::Unsupported, QStringLiteral("images")},
        {"incomplete tile grid", [&] {
             CanvasDocumentState s = valid;
             CanvasTiledImage partial = *s.images.value(image);
             partial.tiles.removeLast();
             s.images.insert(image, std::make_shared<const CanvasTiledImage>(partial));
             return CanvasDocumentFormat::encode(s);
         }, ErrorCode::Validation, QStringLiteral("images.tiles")},
        {"decoded pixel budget", [&] { return CanvasDocumentFormat::encode(valid); },
         ErrorCode::Unsupported, QStringLiteral("images"), CanvasDocumentLimits{.maximumImageBytes = 4096}},
        {"future version", [&] {
             QByteArray bytes = CanvasDocumentFormat::encode(valid);
             qToBigEndian<quint32>(4, bytes.data() + CanvasDocumentFormat::SignatureBytes);
             return resealed(bytes);
         }, ErrorCode::Unsupported, QStringLiteral("version")},
        {"checksum", [&] {
             QByteArray bytes = CanvasDocumentFormat::encode(valid);
             bytes[bytes.size() / 2] = char(bytes[bytes.size() / 2] ^ 0x5a);
             return bytes;
         }, ErrorCode::InvalidArgument, QString{}},
        {"truncated", [&] { return resealed(CanvasDocumentFormat::encode(valid).chopped(40)); },
         ErrorCode::InvalidArgument, QString{}},
    };
    OperationError error;
    QVERIFY2(CanvasDocumentFormat::decode(CanvasDocumentFormat::encode(valid), {}, &error),
             qPrintable(error.message));
    for (const Case &c : cases) {
        OperationError failure;
        const auto decoded = CanvasDocumentFormat::decode(c.bytes(), c.limits, &failure);
        QVERIFY2(!decoded, c.name);
        QVERIFY2(failure.code == c.code, qPrintable(QStringLiteral("%1: %2").arg(QLatin1String(c.name), failure.message)));
        QVERIFY2(failure.fieldPath == c.field, qPrintable(QStringLiteral("%1: %2").arg(QLatin1String(c.name), failure.fieldPath)));
    }
}

void TstCanvasPersistence::rejectedCommitsAndUndoChangeNothing()
{
    CanvasDocument doc;
    doc.clear(); // reset generation 1, so a generation-0 edit is stale
    const CanvasLayerId base = doc.activeLayer();
    const CanvasImageId image = doc.addImage(makeImage(QSize(40, 40), 32, 2));
    CanvasObject picture = shaped(CanvasShape::Image, QRectF(0, 0, 40, 40), base);
    picture.image = image;
    const CanvasObjectId pictureId = doc.insert(picture);
    const CanvasObjectId boxId = doc.insert(shaped(CanvasShape::Rect, QRectF(50, 0, 10, 10), base));
    QVERIFY(pictureId && boxId);
    const CanvasObject box = *doc.object(boxId);
    CanvasObject moved = box;
    moved.transform.translate(5, 5);

    CanvasEdit duplicate;
    duplicate.changes = {{boxId, box, moved}, {boxId, box, moved}};
    CanvasEdit danglingImage;
    CanvasObject orphan = shaped(CanvasShape::Image, QRectF(0, 0, 4, 4), base);
    orphan.id = doc.allocateId();
    orphan.image = image + 9;
    danglingImage.changes = {{orphan.id, std::nullopt, orphan}};
    CanvasEdit referencedImage;
    referencedImage.images = {{image, doc.image(image), {}}};
    CanvasEdit staleGeneration;
    staleGeneration.changes = {{boxId, box, moved}};
    staleGeneration.baseResetGeneration = 0;
    CanvasEdit infinite;
    CanvasObject far = box;
    far.rect.setWidth(std::numeric_limits<qreal>::infinity());
    infinite.changes = {{boxId, box, far}};
    CanvasEdit stale;
    CanvasObject old = box;
    old.name = QStringLiteral("not the current object");
    stale.changes = {{boxId, old, moved}};
    CanvasEdit lastLayer;
    lastLayer.layers = {{base, *doc.layer(base), std::nullopt}};
    const QVector<QPair<CanvasEdit, ErrorCode>> rejected{
        {duplicate, ErrorCode::InvalidArgument}, {danglingImage, ErrorCode::Validation},
        {referencedImage, ErrorCode::Validation}, {staleGeneration, ErrorCode::StaleGeneration},
        {infinite, ErrorCode::Validation}, {stale, ErrorCode::StaleRevision},
        {lastLayer, ErrorCode::Validation}};
    for (const auto &[edit, code] : rejected) {
        const Observed observed(doc);
        const OperationError error = doc.commitEdit(edit);
        QVERIFY2(error.code == code, qPrintable(error.message));
        QVERIFY2(observed.unchanged(), qPrintable(error.message));
    }

    // Undo is prepared the same way: an unrecorded object on a recorded
    // layer makes that layer's removal invalid, so undo refuses whole.
    const CanvasLayerId extra = doc.addLayer(QStringLiteral("Extra"));
    QVERIFY(doc.insert(shaped(CanvasShape::Rect, QRectF(0, 90, 5, 5), extra), false) != 0);
    const Observed observed(doc);
    QVERIFY(!doc.undo());
    QVERIFY(!doc.lastError().isEmpty());
    QCOMPARE(encoded(doc), observed.bytes);
    QCOMPARE(doc.revision(), observed.revision);
    QVERIFY(observed.damaged.isEmpty() && observed.edited.isEmpty() && observed.changed.isEmpty());
    QCOMPARE(doc.undoService()->stepCount(&doc), observed.steps);
    QCOMPARE(doc.undoText(), QStringLiteral("Add layer"));
}

void TstCanvasPersistence::pageLayerAndImageMutationsAreRecorded()
{
    CanvasDocument doc;
    UndoService *undo = doc.undoService();
    undo->markModelClean(&doc);
    const QRectF page = doc.pageRect();
    const quint64 r0 = doc.revision();
    doc.setPageRect(QRectF(0, 0, 300, 200));
    doc.setPageColor(QColor(Qt::black));
    const CanvasLayerId layer = doc.addLayer(QStringLiteral("Ink"));
    QVERIFY(doc.setLayerVisible(layer, false));
    QVERIFY(doc.setLayerLocked(layer, true));
    const CanvasImageId image = doc.addImage(makeImage(QSize(33, 17), 16, 3));
    QVERIFY(image != 0);
    const CanvasTiledImagePtr pixels = doc.image(image);
    QVERIFY(doc.removeImage(image));
    QCOMPARE(doc.revision(), r0 + 7);
    QCOMPARE(undo->stepCount(&doc), 7);
    QVERIFY(!undo->isModelClean(&doc));
    // The removal step shares the image rather than copying the scene; it
    // is charged for the pixels only it still keeps alive.
    QVERIFY(undo->retainedBytes(&doc) >= quint64(33 * 17 * 4));
    QVERIFY(doc.undo());
    QCOMPARE(doc.image(image).get(), pixels.get());
    for (int i = 0; i < 6; ++i) QVERIFY(doc.undo());
    QCOMPARE(doc.pageRect(), page);
    QCOMPARE(doc.pageColor(), QColor(Qt::white));
    QVERIFY(!doc.layer(layer));
    QVERIFY(!doc.image(image));
    QVERIFY(undo->isModelClean(&doc));
    // Unrecorded content makes the saved state unreachable by undo/redo.
    QVERIFY(doc.insert(shaped(CanvasShape::Rect, QRectF(1, 1, 2, 2), doc.activeLayer()), false) != 0);
    QVERIFY(!undo->isModelClean(&doc));
    QVERIFY(doc.redo());
    QVERIFY(doc.undo());
    QVERIFY(!undo->isModelClean(&doc));

    // A declared initial value is document setup, not an undoable edit.
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(QINDATK_BUILD_QML_DIR));
    QQmlComponent component(&engine);
    component.setData("import QindaTK as Tk\nTk.CanvasDocument { pageRect: Qt.rect(0, 0, 320, 240) }", {});
    std::unique_ptr<QObject> declared(component.create());
    QVERIFY2(declared, qPrintable(component.errorString()));
    auto *canvas = qobject_cast<CanvasDocument *>(declared.get());
    QCOMPARE(canvas->pageRect(), QRectF(0, 0, 320, 240));
    QVERIFY(!canvas->canUndo());
}

void TstCanvasPersistence::groupedEditsThatCancelOutLeaveNoStep()
{
    CanvasDocument doc;
    UndoService *undo = doc.undoService();
    QVERIFY(doc.addRect(0, 0, 4, 4, Qt::red) != 0);
    const int steps = undo->stepCount(&doc);
    QVERIFY(undo->beginMacro(QStringLiteral("Try")));
    const CanvasObjectId temporary = doc.addEllipse(10, 10, 3, 3, Qt::blue);
    QVERIFY(temporary != 0);
    QVERIFY(doc.removeObject(temporary));
    QVERIFY(undo->endMacro());
    QCOMPARE(undo->stepCount(&doc), steps); // no unundoable empty step
    QVERIFY(doc.undo());
    QCOMPARE(doc.objectCount(), 0);
    QVERIFY(undo->beginMacro(QStringLiteral("Toggle")));
    QVERIFY(doc.setLayerVisible(doc.activeLayer(), false));
    QVERIFY(doc.setLayerVisible(doc.activeLayer(), true));
    QVERIFY(undo->cancelMacro()); // closes although the group nets to nothing
    QVERIFY(doc.canRedo());
}

void TstCanvasPersistence::encodedBudgetKeepsEveryCommitSaveable()
{
    CanvasDocumentLimits limits;
    limits.maximumEncodedBytes = 64 * 1024;
    limits.maximumExtensionBytes = 1024;
    CanvasDocument doc;
    doc.setLimits(limits);
    int committed = 0;
    OperationError refusal;
    for (int i = 0; i < 1000 && !refusal; ++i) {
        CanvasObject o = shaped(CanvasShape::Rect, QRectF(i, 0, 1, 1), doc.activeLayer());
        o.id = doc.allocateId();
        o.z = doc.reserveZ();
        CanvasEdit edit;
        edit.label = QStringLiteral("Add");
        edit.changes = {{o.id, std::nullopt, o}};
        refusal = doc.commitEdit(edit);
        if (!refusal) ++committed;
    }
    QCOMPARE(refusal.code, ErrorCode::Unsupported);
    QCOMPARE(refusal.fieldPath, QStringLiteral("encodedBytes"));
    QVERIFY(committed > 100);
    // The fullest accepted canvas still saves, with the largest allowed
    // extensions, and reopens under the same limits.
    const DocumentCodecPtr codec = makeCanvasDocumentCodec(limits);
    const QJsonObject extensions{{QStringLiteral("pad"), QString(900, QLatin1Char('x'))}};
    OperationError error;
    const SnapshotPtr snapshot = encodeCapture(*codec, *codec->capture(doc, extensions), &error);
    QVERIFY2(snapshot, qPrintable(error.message));
    QVERIFY(snapshot->payload.size() <= limits.maximumEncodedBytes);
    QVERIFY2(codec->decode(snapshot->payload, &error), qPrintable(error.message));
    QVERIFY(doc.removeObject(doc.objectIds().first())); // shrinking stays possible
}

void TstCanvasPersistence::oneObjectGestureInLargeCanvasRetainsOnlyTheDelta()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    auto owned = std::make_unique<CanvasDocument>();
    CanvasDocument *doc = owned.get();
    doc->populateFixture(100000, 7, false);
    const QByteArray scene = encoded(*doc);
    QVERIFY(scene.size() > 10 * 1024 * 1024);
    DocumentManager manager;
    manager.recoveryManager()->setDirectory(temp.path());
    DocumentSession *session = manager.create(std::move(owned));
    QVERIFY(session);
    UndoService *undo = manager.undoService();
    QCOMPARE(doc->undoService(), undo); // the one live history moved into the session
    QCOMPARE(undo->memoryLimit(), quint64(8 * 1024 * 1024));
    CanvasObjectId target = 0;
    for (CanvasObjectId id : doc->objectIds()) {
        const QVariantMap info = doc->objectInfo(id);
        if (id > 5000 && info.value(QStringLiteral("kind")) == QStringLiteral("rect")
            && info.value(QStringLiteral("effectiveVisible")).toBool()
            && !info.value(QStringLiteral("effectiveLocked")).toBool()) {
            target = id;
            break;
        }
    }
    QVERIFY(target != 0);
    ToolController tools;
    tools.setDocument(doc);
    tools.setSelection({target});
    QElapsedTimer timer;
    timer.start();
    constexpr int gestures = 300;
    for (int i = 0; i < gestures; ++i) {
        const QPointF centre = doc->objectInfo(target).value(QStringLiteral("bounds")).toRectF().center();
        const qreal step = i % 2 ? -3.0 : 3.0;
        QVERIFY(tools.begin(QStringLiteral("view"), centre.x(), centre.y()));
        QVERIFY(tools.update(QStringLiteral("view"), centre.x() + step, centre.y() + step));
        QVERIFY(tools.end(QStringLiteral("view"), centre.x() + step, centre.y() + step));
        QCOMPARE(tools.lastOutcome(), QStringLiteral("committed"));
    }
    const double perGesture = double(timer.nsecsElapsed()) / 1e6 / gestures;
    const quint64 retained = undo->retainedBytes(doc);
    QCOMPARE(undo->stepCount(doc), CanvasDocument::DefaultHistoryLimit);
    // A whole-scene snapshot per gesture would need scene.size() per step and
    // could not fit the default budget even once.
    QVERIFY(quint64(scene.size()) > undo->memoryLimit());
    QVERIFY(retained < quint64(CanvasDocument::DefaultHistoryLimit) * 1024);
    qInfo("100000 objects, %lld encoded bytes; %d gestures, %.3f ms each; %llu bytes retained "
          "by %d history steps", qlonglong(scene.size()), gestures, perGesture,
          static_cast<unsigned long long>(retained), undo->stepCount(doc));
    QVERIFY(session->isDirty());
}

QTEST_MAIN(TstCanvasPersistence)
#include "tst_canvas_persistence.moc"
