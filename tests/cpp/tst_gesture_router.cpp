// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../../src/core/gesture_router.h"

#include <QtTest>
#include <memory>

using namespace QindaTK;

class TstGestureRouter final : public QObject {
    Q_OBJECT
private:
    static void configureEdit(EditTransaction &edit, PropertyDocument &document,
                              UndoService &undo, EntityId target)
    {
        edit.setDocument(&document);
        edit.setUndoService(&undo);
        edit.setTargetIds({target.toString()});
        edit.setPropertyId(QStringLiteral("name"));
        edit.setLabel(QStringLiteral("Draw gesture"));
    }

private slots:
    void onePenGestureCommitsOneUndoStep()
    {
        PropertyDocument document;
        QVERIFY(document.defineProperty({QStringLiteral("name"),
            QMetaType::fromType<QString>(), QString{}, {}, true, false}));
        const EntityId target = EntityId::create();
        QVERIFY(document.addEntity(target, {{QStringLiteral("name"), QStringLiteral("before")}}));
        UndoService undo;
        QVERIFY(undo.addDocument(&document));
        undo.setActiveDocument(&document);
        EditTransaction edit;
        configureEdit(edit, document, undo, target);
        QObject owner;
        GestureRouter router;
        router.setToolId(QStringLiteral("pen"));
        const QString token = router.beginGesture(QStringLiteral("view"),
            QStringLiteral("pointer-1"), target.toString(), GestureRouter::Pen,
            GestureRouter::Edit, &owner, &edit);
        QVERIFY2(!token.isEmpty(), qPrintable(router.lastError()));
        QVERIFY(edit.active());
        QCOMPARE(router.activeCount(), 1);
        QVERIFY(!edit.updateValue(QStringLiteral("after")));
        QVERIFY(router.updateGesture(token, QStringLiteral("pointer-1")));
        QVERIFY(router.completeGesture(token, QStringLiteral("pointer-1")));
        QCOMPARE(document.committedValue(target, QStringLiteral("name")).toString(),
                 QStringLiteral("after"));
        QVERIFY(!router.completeGesture(token, QStringLiteral("pointer-1")));
        QVERIFY(undo.canUndo());
        QCOMPARE(undo.undoText(), QStringLiteral("Draw gesture"));
        QVERIFY(undo.undo());
        QCOMPARE(document.committedValue(target, QStringLiteral("name")).toString(),
                 QStringLiteral("before"));
        QVERIFY(!undo.canUndo());
    }

    void duplicatePointerAndLostGrabCannotCommitPreview()
    {
        PropertyDocument document;
        QVERIFY(document.defineProperty({QStringLiteral("name"),
            QMetaType::fromType<QString>(), QString{}, {}, true, false}));
        const EntityId target = EntityId::create();
        QVERIFY(document.addEntity(target, {{QStringLiteral("name"), QStringLiteral("before")}}));
        UndoService undo;
        QVERIFY(undo.addDocument(&document));
        EditTransaction edit;
        configureEdit(edit, document, undo, target);
        QObject owner;
        GestureRouter router;
        const QString token = router.beginGesture(QStringLiteral("view"),
            QStringLiteral("pen-1"), target.toString(), GestureRouter::Pen,
            GestureRouter::Edit, &owner, &edit);
        QVERIFY(!token.isEmpty());
        QVERIFY(!edit.updateValue(QStringLiteral("preview")));
        QVERIFY(router.beginGesture(QStringLiteral("view"), QStringLiteral("mouse-1"),
            target.toString(), GestureRouter::Mouse, GestureRouter::Edit,
            &owner, &edit, true).isEmpty());
        QVERIFY(router.beginGesture(QStringLiteral("view"), QStringLiteral("touch-1"),
            target.toString(), GestureRouter::Touch, GestureRouter::Navigate,
            &owner).isEmpty());
        QVERIFY(!router.updateGesture(token, QStringLiteral("other-pointer")));
        router.lostGrab(token);
        QCOMPARE(router.activeCount(), 0);
        QVERIFY(!edit.active());
        QCOMPARE(document.value(target, QStringLiteral("name")).toString(),
                 QStringLiteral("before"));
        QVERIFY(!undo.canUndo());
    }

    void viewTargetAndOwnerInvalidationCancelSessions()
    {
        GestureRouter router;
        QObject first;
        QObject second;
        const QString a = router.beginGesture(QStringLiteral("view-a"),
            QStringLiteral("touch-a"), QStringLiteral("target-a"),
            GestureRouter::Touch, GestureRouter::Navigate, &first);
        const QString b = router.beginGesture(QStringLiteral("view-b"),
            QStringLiteral("touch-b"), QStringLiteral("target-b"),
            GestureRouter::Touch, GestureRouter::Navigate, &second);
        QVERIFY(!a.isEmpty() && !b.isEmpty());
        QCOMPARE(router.activeCount(), 2);
        router.invalidateTarget(QStringLiteral("target-a"));
        QCOMPARE(router.activeCount(), 1);
        QVERIFY(!router.completeGesture(a, QStringLiteral("touch-a")));
        router.invalidateView(QStringLiteral("view-b"));
        QCOMPARE(router.activeCount(), 0);
        const QString c = router.beginGesture(QStringLiteral("view-b"),
            QStringLiteral("touch-c"), QStringLiteral("target-c"),
            GestureRouter::Touch, GestureRouter::Navigate, &second);
        QVERIFY(!c.isEmpty());
        QVERIFY(router.completeGesture(c, QStringLiteral("touch-c")));
    }

    void destroyingOwnerCancelsEditAndRestoresCommittedValue()
    {
        PropertyDocument document;
        QVERIFY(document.defineProperty({QStringLiteral("name"),
            QMetaType::fromType<QString>(), QString{}, {}, true, false}));
        const EntityId target = EntityId::create();
        QVERIFY(document.addEntity(target, {{QStringLiteral("name"), QStringLiteral("before")}}));
        UndoService undo;
        QVERIFY(undo.addDocument(&document));
        EditTransaction edit;
        configureEdit(edit, document, undo, target);
        GestureRouter router;
        auto owner = std::make_unique<QObject>();
        const QString token = router.beginGesture(QStringLiteral("view"),
            QStringLiteral("pointer"), target.toString(), GestureRouter::Mouse,
            GestureRouter::Edit, owner.get(), &edit);
        QVERIFY(!token.isEmpty());
        QVERIFY(!edit.updateValue(QStringLiteral("preview")));
        owner.reset();
        QCOMPARE(router.activeCount(), 0);
        QVERIFY(!edit.active());
        QCOMPARE(document.committedValue(target, QStringLiteral("name")).toString(),
                 QStringLiteral("before"));
        QVERIFY(!undo.canUndo());
    }

    void temporaryNavigationAndWheelPolicyRemainInspectible()
    {
        GestureRouter router;
        QObject owner;
        router.setTemporaryNavigation(true);
        const QString token = router.beginGesture(QStringLiteral("view"),
            QStringLiteral("mouse"), QStringLiteral("canvas"), GestureRouter::Mouse,
            GestureRouter::Edit, &owner);
        QVERIFY(!token.isEmpty());
        QCOMPARE(router.activeGestures().first().toMap().value(QStringLiteral("intent")).toInt(),
                 int(GestureRouter::Navigate));
        QCOMPARE(router.wheelIntent(int(Qt::ControlModifier)), GestureRouter::Navigate);
        QVERIFY(router.completeGesture(token, QStringLiteral("mouse")));
        router.setTemporaryNavigation(false);
        QCOMPARE(router.wheelIntent(int(Qt::ControlModifier)), GestureRouter::Zoom);
        QCOMPARE(router.wheelIntent(int(Qt::NoModifier)), GestureRouter::Scroll);
    }
};

QTEST_GUILESS_MAIN(TstGestureRouter)
#include "tst_gesture_router.moc"
