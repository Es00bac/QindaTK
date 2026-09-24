// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QImage>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class VideoScopesModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    explicit VideoScopesModel(QObject *parent = nullptr);

    QVariantMap snapshot() const { return m_snapshot; }
    QString error() const { return m_error; }

    Q_INVOKABLE QVariantMap analyzeFrame(const QImage &frame, const QString &frameId);
    Q_INVOKABLE QVariantMap analyzeColorBarsFixture(const QString &frameId = QStringLiteral("synthetic-color-bars"));
    Q_INVOKABLE void clear();

signals:
    void changed();

private:
    QVariantMap failed(const QString &message);
    QVariantMap m_snapshot;
    QString m_error;
};
