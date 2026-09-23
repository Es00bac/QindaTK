// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Application-owned scope tree. The active path, not creation order, decides
// command precedence; owners are weak and disappearing subtrees are pruned.
class CommandContext : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString activeToken READ activeToken NOTIFY changed)
    Q_PROPERTY(QString activeScopeId READ activeScopeId NOTIFY changed)
    Q_PROPERTY(QString generation READ generation NOTIFY changed)
public:
    explicit CommandContext(QObject *parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE QString pushScope(const QString &scopeId, QObject *owner,
                                  const QString &parentToken = {});
    Q_INVOKABLE bool activate(const QString &token);
    Q_INVOKABLE bool removeScope(const QString &token);
    Q_INVOKABLE bool contains(const QString &token) const;
    Q_INVOKABLE bool updateScope(const QString &token, const QVariantMap &values);
    QString activeToken() const { return m_activeToken; }
    QString activeScopeId() const;
    QString generation() const { return QString::number(m_generation); }
    QStringList activePath() const; // most specific token first
    CommandInvocationContext snapshot() const;
signals:
    void changed();
    void scopeRemoved(const QString &token);
private:
    struct Frame {
        QString token;
        QString parentToken;
        QString scopeId;
        QPointer<QObject> owner;
        CommandInvocationContext values;
        CancellationSource cancellation;
        bool hasSelection = false;
        bool hasTool = false;
        bool hasFocus = false;
    };
    qsizetype indexOf(const QString &token) const;
    void publishChange();
    QVector<Frame> m_frames;
    QString m_activeToken;
    quint64 m_generation = 0;
};

} // namespace QindaTK
