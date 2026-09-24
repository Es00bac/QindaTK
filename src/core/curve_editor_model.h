// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "curve_evaluator.h"
#include "edit_transaction.h"
#include <QJsonObject>
#include <QObject>
#include <QVariantList>
#include <functional>

namespace QindaTK {

// Host-owned typed curve state with transient previews and one undo step per gesture.
class CurveEditorModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantMap definition READ definition NOTIFY changed)
    Q_PROPERTY(QVariantList knots READ knots NOTIFY changed)
    Q_PROPERTY(QVariantList samples READ samples NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(bool editing READ editing NOTIFY changed)
public:
    explicit CurveEditorModel(QObject *parent = nullptr);
    QVariantMap definition() const;
    QVariantList knots() const;
    QVariantList samples() const;
    QString errorMessage() const { return m_error; }
    bool editing() const { return m_transaction.active(); }
    CurveDefinition curve() const;
    bool setCurve(const CurveDefinition &curve);
    void setDomainConstraint(std::function<OperationError(const CurveDefinition &)> constraint);
    Q_INVOKABLE bool beginKnotEdit(const QString &id);
    Q_INVOKABLE bool beginTangentEdit(const QString &id, const QString &side);
    Q_INVOKABLE bool previewKnot(const QString &id, double x, double y);
    Q_INVOKABLE bool previewTangent(const QString &id, const QString &side, double x, double y);
    Q_INVOKABLE bool commitEdit();
    Q_INVOKABLE void cancelEdit();
    Q_INVOKABLE bool undo() { return m_undo.undo(); }
    Q_INVOKABLE bool redo() { return m_undo.redo(); }
    Q_INVOKABLE QByteArray saveJson() const;
    Q_INVOKABLE bool loadJson(const QByteArray &json);
    static QVariantMap toMap(const CurveDefinition &curve);
    static std::optional<CurveDefinition> fromMap(const QVariantMap &map);
signals:
    void changed();
private:
    bool begin(const QString &id, const QString &label);
    bool applyPreview(const QVariantMap &candidate);
    QVariantMap currentMap() const;
    PropertyDocument m_document;
    UndoService m_undo;
    EditTransaction m_transaction;
    EntityId m_entity = EntityId::create();
    QString m_error;
    std::function<OperationError(const CurveDefinition &)> m_constraint;
};

} // namespace QindaTK
