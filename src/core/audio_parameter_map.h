// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Canonical audio parameter value <-> control travel. Controls send gesture
// requests; the owner supplies this mapping and commits through its engine.
class AudioParameterMap : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantMap descriptor READ descriptor WRITE setDescriptor NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    explicit AudioParameterMap(QObject *parent = nullptr) : QObject(parent) {}
    QVariantMap descriptor() const { return m_descriptor; }
    void setDescriptor(const QVariantMap &descriptor);
    QString lastError() const { return m_lastError; }
    Q_INVOKABLE double positionFor(double value, bool silent = false) const;
    Q_INVOKABLE QVariantMap valueAt(double position) const;
signals:
    void changed();
    void errorChanged();
private:
    QVariantMap m_descriptor{{QStringLiteral("taper"), QStringLiteral("linear")},
                             {QStringLiteral("minimum"), 0.0},
                             {QStringLiteral("maximum"), 1.0},
                             {QStringLiteral("unityPosition"), 0.75}};
    QString m_lastError;
};

} // namespace QindaTK
