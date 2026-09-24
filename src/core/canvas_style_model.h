// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "canvas_document.h"
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Solid fill, stroke and width edits for the canvas reference document.
// Style panels may expose richer values; unsupported fields are rejected
// explicitly until a renderer and codec exist for them.
class CanvasStyleModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::CanvasDocumentAdapter *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    explicit CanvasStyleModel(QObject *parent = nullptr) : QObject(parent) {}
    CanvasDocumentAdapter *document() const { return m_document; }
    void setDocument(CanvasDocumentAdapter *document);
    QString lastError() const { return m_lastError; }
    // style keys: fill, stroke, strokeWidth. Null colour clears that channel.
    // Every selected object changes together in one reversible document edit.
    Q_INVOKABLE bool apply(const QVariantList &selectedIds, const QVariantMap &style);
signals:
    void documentChanged();
    void errorChanged();
    void applied(int changedObjects);
private:
    bool fail(const QString &message);
    QPointer<CanvasDocumentAdapter> m_document;
    QString m_lastError;
};

} // namespace QindaTK
