// SPDX-License-Identifier: LGPL-3.0-or-later
#include "command_context.h"
#include "command_registry.h"
#include "property_binding.h"
#include "property_document.h"
#include "undo_service.h"

#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>

using namespace QindaTK;

class TstEditWorkflow final : public QObject {
    Q_OBJECT
private:
    static void defineDocument(PropertyDocument &document, EntityId id)
    {
        QVERIFY(document.defineProperty({QStringLiteral("gain"), QMetaType::fromType<double>(),
                                         0.0, QStringLiteral("dB"), true, true}));
        QVERIFY(document.addEntity(id, {{QStringLiteral("gain"), 0.0}}));
    }

    static QObject *named(QObject *root, const char *name)
    {
        return root->findChild<QObject *>(QString::fromLatin1(name));
    }

private slots:
    void numberFieldSliderAndCommandShareDocumentTransactions()
    {
        const EntityId entity = EntityId::create();
        PropertyDocument firstDocument;
        PropertyDocument secondDocument;
        defineDocument(firstDocument, entity);
        defineDocument(secondDocument, entity);

        UndoService undo;
        QVERIFY(undo.addDocument(&firstDocument));
        QVERIFY(undo.addDocument(&secondDocument));
        undo.setActiveDocument(&firstDocument);

        PropertyBinding firstBinding;
        firstBinding.setDocument(&firstDocument);
        firstBinding.setUndoService(&undo);
        firstBinding.setPropertyId(QStringLiteral("gain"));
        firstBinding.setTargetIds({entity.toString()});

        PropertyBinding secondBinding;
        secondBinding.setDocument(&secondDocument);
        secondBinding.setUndoService(&undo);
        secondBinding.setPropertyId(QStringLiteral("gain"));
        secondBinding.setTargetIds({entity.toString()});

        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(QINDATK_BUILD_QML_DIR));
        engine.rootContext()->setContextProperty(QStringLiteral("editBinding"), &firstBinding);
        QQmlComponent component(&engine);
        component.setData(R"qml(
import QtQuick
import QtQuick.Window
import QindaTK as Tk
Window {
    width: 360; height: 120; visible: true
    Item {
        anchors.fill: parent
        Tk.NumberField {
            id: numberField
            objectName: "workflowNumberField"
            x: 12; y: 12; width: 140
            value: editBinding.value
            onValueModified: function(candidate) {
                if (editBinding.begin()) {
                    if (editBinding.update(candidate)) editBinding.commit()
                    else editBinding.cancel()
                }
            }
        }
        Tk.Slider {
            id: slider
            objectName: "workflowSlider"
            x: 12; y: 64; width: 260
            from: 0; to: 10; stepSize: 0.1; value: editBinding.value
            onPressedChanged: {
                if (pressed) editBinding.begin()
                else if (editBinding.editing) editBinding.commit()
            }
            onValueModified: function(candidate) {
                if (editBinding.editing) editBinding.update(candidate)
            }
        }
    }
    function editNumberFromUser(candidate) { numberField.commit(candidate) }
}
)qml", QUrl(QStringLiteral("inline-edit-workflow.qml")));
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(component.create());
        QVERIFY(window != nullptr);
        QTRY_VERIFY(window->isVisible());
        auto *numberField = named(window, "workflowNumberField");
        auto *slider = qobject_cast<QQuickItem *>(named(window, "workflowSlider"));
        QVERIFY(numberField != nullptr);
        QVERIFY(slider != nullptr);

        // NumberField.commit is its actual user operation path: it emits the
        // same valueModified signal as text, step, reset and scrub gestures.
        QVERIFY(QMetaObject::invokeMethod(window, "editNumberFromUser",
                                          Q_ARG(QVariant, QVariant(2.0))));
        QCOMPARE(firstDocument.committedValue(entity, QStringLiteral("gain")).toDouble(), 2.0);
        QVERIFY(undo.canUndo());
        QVERIFY(undo.undo());
        QCOMPARE(firstDocument.value(entity, QStringLiteral("gain")).toDouble(), 0.0);
        QVERIFY(!undo.canUndo());

        // Use the real template pointer gesture. Each moved notification is a
        // preview update under the press/release transaction boundary.
        const QPoint from = slider->mapToScene(QPointF(slider->width() * 0.15,
                                                        slider->height() / 2)).toPoint();
        const QPoint to = slider->mapToScene(QPointF(slider->width() * 0.82,
                                                      slider->height() / 2)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(window, to, 30);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, to);
        QTRY_VERIFY(firstDocument.committedValue(entity, QStringLiteral("gain")).toDouble() > 5.0);
        QVERIFY(undo.canUndo());
        QVERIFY(undo.undo());
        QCOMPARE(firstDocument.value(entity, QStringLiteral("gain")).toDouble(), 0.0);
        QVERIFY(!undo.canUndo()); // several move previews produced one history step

        CommandContext context;
        CommandRegistry registry;
        registry.setContext(&context);
        QObject appOwner;
        QObject firstOwner;
        QObject secondOwner;
        const QString appScope = context.pushScope(QStringLiteral("application"), &appOwner);
        const QString firstScope = context.pushScope(QStringLiteral("first-document"), &firstOwner, appScope);
        const QString secondScope = context.pushScope(QStringLiteral("second-document"), &secondOwner, appScope);
        QVERIFY(context.activate(firstScope));
        QVERIFY(registry.registerCommand({QStringLiteral("gain.set"), QStringLiteral("Set gain"),
                                          {}, QStringLiteral("Document"), {}, {}, false}));
        auto handler = [&undo](PropertyBinding *binding, PropertyDocument *document) {
            return [binding, document, &undo](const CommandInvocationContext &,
                                               const QVariantMap &arguments) {
                undo.setActiveDocument(document);
                const QVariant candidate = arguments.value(QStringLiteral("value"));
                if (!binding->begin() || !binding->update(candidate) || !binding->commit())
                    return OperationError{ErrorCode::Validation,
                                          QStringLiteral("Gain edit was rejected"),
                                          QStringLiteral("gain"), {}};
                return OperationError{};
            };
        };
        QVERIFY(registry.registerHandler(firstScope, QStringLiteral("gain.set"), &firstOwner,
                                         handler(&firstBinding, &firstDocument)));
        QVERIFY(registry.registerHandler(secondScope, QStringLiteral("gain.set"), &secondOwner,
                                         handler(&secondBinding, &secondDocument)));

        // Closing a scope removes its handler. A stale command cannot mutate
        // the old document after the context is switched or closed.
        QVERIFY(context.activate(secondScope));
        QVERIFY(!registry.invoke(QStringLiteral("gain.set"), {{QStringLiteral("value"), 7.0}}));
        QCOMPARE(secondDocument.committedValue(entity, QStringLiteral("gain")).toDouble(), 7.0);
        QCOMPARE(firstDocument.committedValue(entity, QStringLiteral("gain")).toDouble(), 0.0);
        QVERIFY(undo.canUndo());
        QVERIFY(undo.undo());
        QCOMPARE(secondDocument.value(entity, QStringLiteral("gain")).toDouble(), 0.0);

        QVERIFY(context.activate(firstScope));
        QVERIFY(!registry.invoke(QStringLiteral("gain.set"), {{QStringLiteral("value"), 4.0}}));
        QCOMPARE(firstDocument.committedValue(entity, QStringLiteral("gain")).toDouble(), 4.0);
        QVERIFY(context.removeScope(firstScope));
        QVERIFY(registry.invoke(QStringLiteral("gain.set"), {{QStringLiteral("value"), 9.0}}));
        QCOMPARE(firstDocument.committedValue(entity, QStringLiteral("gain")).toDouble(), 4.0);

        // Invalid typed input and cancellation roll back and never add history.
        QVERIFY(context.activate(secondScope));
        QVERIFY(secondBinding.begin());
        QVERIFY(!secondBinding.update(QStringLiteral("invalid")));
        QCOMPARE(secondDocument.value(entity, QStringLiteral("gain")).toDouble(), 0.0);
        undo.setActiveDocument(&secondDocument);
        QVERIFY(!undo.canUndo());

        delete window;
    }
};

QTEST_MAIN(TstEditWorkflow)
#include "tst_edit_workflow.moc"
