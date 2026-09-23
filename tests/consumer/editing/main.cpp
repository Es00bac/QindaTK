// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>

#include <qindatk/application_contracts.h>
#include <qindatk/property_binding.h>
#include <qindatk/property_document.h>
#include <qindatk/selection_model.h>
#include <qindatk/undo_service.h>
#include <qindatk/validation_model.h>

namespace {
bool check(bool condition, int code)
{
    if (condition)
        return true;
    qCritical("QindaTK editing consumer check failed: %d", code);
    return false;
}
} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    QindaTK::PropertyDocument document;
    const QindaTK::PropertyDescriptor gain{
        QStringLiteral("gain"), QMetaType::fromType<double>(), 0.0,
        QStringLiteral("dB"), true, true};
    const QindaTK::EntityId entity = QindaTK::EntityId::create();
    if (!check(document.defineProperty(gain)
                   && document.addEntity(entity, {{QStringLiteral("gain"), 1.5}}), 1))
        return 1;

    QindaTK::UndoService undo;
    if (!check(undo.addDocument(&document), 2))
        return 2;
    undo.setActiveDocument(&document);
    undo.markClean();

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral(QINDATK_QML_IMPORT_ROOT));
    engine.rootContext()->setContextProperty(QStringLiteral("appDocument"), &document);
    engine.rootContext()->setContextProperty(QStringLiteral("appUndo"), &undo);
    engine.rootContext()->setContextProperty(QStringLiteral("appEntityId"), entity.toString());
    engine.load(QUrl::fromLocalFile(QStringLiteral(CONSUMER_QML_FILE)));
    if (!check(engine.rootObjects().size() == 1, 3))
        return 3;

    QObject *root = engine.rootObjects().first();
    auto *selection = root->findChild<QindaTK::SelectionModel *>(QStringLiteral("selection"));
    auto *validation = root->findChild<QindaTK::ValidationModel *>(QStringLiteral("validation"));
    if (!check(selection != nullptr && validation != nullptr, 4))
        return 4;
    if (!check(root->property("projectedValue").toDouble() == 1.5
                   && !root->property("projectedMixed").toBool()
                   && root->property("projectedWritable").toBool(), 5))
        return 5;

    QVariant editResult;
    if (!check(QMetaObject::invokeMethod(root, "editGain", Q_RETURN_ARG(QVariant, editResult),
                                        Q_ARG(QVariant, QVariant(4.25)))
                   && editResult.toBool()
                   && document.committedValue(entity, QStringLiteral("gain")).toDouble() == 4.25
                   && document.revisionString() == QStringLiteral("2")
                   && undo.canUndo() && undo.isClean() == false, 5))
        return 6;
    if (!check(undo.undo()
                   && document.committedValue(entity, QStringLiteral("gain")).toDouble() == 1.5
                   && undo.isClean(), 6))
        return 7;
    if (!check(undo.redo()
                   && document.committedValue(entity, QStringLiteral("gain")).toDouble() == 4.25
                   && !undo.isClean(), 7))
        return 8;

    QVariant selectionResult;
    if (!check(QMetaObject::invokeMethod(root, "selectEntity",
                                        Q_RETURN_ARG(QVariant, selectionResult),
                                        Q_ARG(QVariant, QVariant(entity.toString())))
                   && selectionResult.toBool()
                   && selection->selectedIds() == QStringList{entity.toString()}
                   && selection->currentId() == entity.toString(), 9))
        return 9;

    const QindaTK::SnapshotHeader current = document.header();
    const quint64 run = validation->beginRun(current);
    if (!check(run != 0 && validation->pending(), 10))
        return 10;
    QindaTK::SnapshotHeader stale = current;
    ++stale.revision.value;
    const QindaTK::ValidationEntry issue{
        QStringLiteral("gain.range"), QindaTK::ValidationModel::Error, entity,
        QStringLiteral("gain"), QStringLiteral("Gain is outside the supported range"),
        QStringLiteral("Choose a value in range")};
    if (!check(!validation->completeRun(run, stale, {issue})
                   && validation->pending()
                   && validation->completeRun(run, current, {issue})
                   && !validation->pending() && validation->invalid()
                   && validation->documentId() == document.documentIdString()
                   && validation->revision() == document.revisionString()
                   && validation->firstIssueTargetId() == entity.toString()
                   && validation->firstIssuePath() == QStringLiteral("gain"), 11))
        return 11;

    return 0;
}
