// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "canvas_document.h"

#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
// Applies AlignDistributePanel requests to a document in one CanvasEdit.
// All coordinates are document units; IDs arrive as decimal strings to keep
// 64-bit object identity exact across QML's Number representation.
class CanvasAlignmentModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::CanvasDocumentAdapter *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    explicit CanvasAlignmentModel(QObject *parent = nullptr) : QObject(parent) {}
    CanvasDocumentAdapter *document() const { return m_document; }
    void setDocument(CanvasDocumentAdapter *document);
    QString lastError() const { return m_lastError; }
    Q_INVOKABLE bool apply(const QString &operation, const QVariantList &selectedIds,
                           const QString &referenceMode = QStringLiteral("selection"),
                           const QString &keyObjectId = {}, qreal spacing = 0);
signals:
    void documentChanged();
    void errorChanged();
    void applied(const QString &operation, int changedObjects);
private:
    bool fail(const QString &message);
    QPointer<CanvasDocumentAdapter> m_document;
    QString m_lastError;
};
} // namespace QindaTK
