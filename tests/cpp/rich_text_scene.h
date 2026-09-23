// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "rich_text_area.h"
#include "rich_text_fixtures.h"
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QtTest>
#include <memory>

// An offscreen window hosting a Tk.RichTextArea created from QML (so the
// Accessible attached properties are real) over a C++-owned document.
namespace QindaTK::RichTextScene {

struct Scene {
    QQmlEngine engine;
    std::unique_ptr<QQuickWindow> window = std::make_unique<QQuickWindow>();
    std::unique_ptr<QObject> root;
    RichTextArea *area = nullptr;
    RichTextDocument document;

    bool create(bool paged, const QSize &size = {900, 700})
    {
        engine.addImportPath(QStringLiteral(QINDATK_BUILD_QML_DIR));
        QQmlComponent component(&engine);
        component.setData(QByteArrayLiteral(
            "import QtQuick\nimport QindaTK as Tk\n"
            "Tk.RichTextArea { objectName: 'area'; focus: true;"
            " inkColor: 'black'; pageColor: 'white'; linkColor: 'blue'; selectionColor: '#8080ff';"
            " borderColor: 'gray'; objectFillColor: '#eeeeee'; objectTextColor: 'gray';"
            " Accessible.role: Accessible.EditableText; Accessible.name: 'Document'; Accessible.multiLine: true }"),
            QUrl(QStringLiteral("qrc:/scene.qml")));
        root.reset(component.create());
        area = qobject_cast<RichTextArea *>(root.get());
        if (!area) {
            qWarning() << component.errors();
            return false;
        }
        window->resize(size);
        area->setParentItem(window->contentItem());
        area->setSize(QSizeF(size));
        area->setPaged(paged);
        area->setDocument(&document);
        window->show();
        window->requestActivate();
        if (!QTest::qWaitForWindowExposed(window.get())) return false;
        if (!QTest::qWaitForWindowActive(window.get(), 1000)) qWarning("window not active; focus is forced");
        area->forceActiveFocus();
        return area->hasActiveFocus();
    }

    // QTest::keyClicks only takes widgets; type ASCII text into the window.
    void type(const QString &text) const
    {
        for (const QChar c : text) QTest::keyClick(window.get(), c.toLatin1());
    }

    // Item coordinates of the centre of a logical position's caret box.
    QPointF pointAt(const RichText::Position &position) const
    {
        const RichText::CaretBox caret = area->layout()->caretBox(position);
        return area->pageOrigin(caret.page) + caret.rect.center() * area->zoom();
    }
};

} // namespace QindaTK::RichTextScene
