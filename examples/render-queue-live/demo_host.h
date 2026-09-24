// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "../../src/core/render_queue_model.h"
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QUrl>

class DemoHost : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList jobs READ jobs NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    Q_PROPERTY(QString outputPath READ outputPath CONSTANT)
    Q_PROPERTY(QString statusTrail READ statusTrail NOTIFY changed)
    Q_PROPERTY(QUrl previewUrl READ previewUrl NOTIFY changed)
public:
    explicit DemoHost(const QString &root, QObject *parent = nullptr);
    bool ready() const { return m_ready; }
    QVariantList jobs() const;
    QString lastError() const { return m_error; }
    QString outputPath() const { return m_outputPath; }
    QString statusTrail() const { return m_history.join(QStringLiteral(" → ")); }
    QUrl previewUrl() const;
    QString state() const;
    bool verify() { return m_queue.verifyOutput(QStringLiteral("demo-caption")); }
    Q_INVOKABLE bool validate(const QString &id);
    Q_INVOKABLE bool submit(const QString &id);
    Q_INVOKABLE bool cancel(const QString &id);
    Q_INVOKABLE bool retry(const QString &id);
    Q_INVOKABLE bool openOutput(const QString &id, const QVariantMap &manifest);
signals:
    void changed();
private:
    bool createSources();
    QString m_root;
    QString m_outputPath;
    QString m_error;
    QStringList m_history;
    QString m_draftState = QStringLiteral("draft");
    QVariantMap m_snapshot;
    QindaTK::RenderQueueModel m_queue;
    bool m_ready = false;
};
