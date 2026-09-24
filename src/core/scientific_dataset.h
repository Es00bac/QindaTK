// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "uncertainty_model.h"

#include <QAbstractListModel>
#include <QHash>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// A bounded, one-dimensional float64 series. The host owns measurement and
// uncertainty semantics; this adapter preserves values and makes a drawable
// interval projection without estimating statistics.
class ScientificDataset : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString sourceId READ sourceId WRITE setSourceId NOTIFY changed)
    Q_PROPERTY(QString xUnit READ xUnit WRITE setXUnit NOTIFY changed)
    Q_PROPERTY(QString yUnit READ yUnit WRITE setYUnit NOTIFY changed)
    Q_PROPERTY(QVariantMap parameters READ parameters WRITE setParameters NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int missingCount READ missingCount NOTIFY changed)
    Q_PROPERTY(QString precision READ precision CONSTANT)
    Q_PROPERTY(QString shape READ shape NOTIFY changed)
    Q_PROPERTY(UncertaintyModel *intervals READ intervals CONSTANT)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    explicit ScientificDataset(QObject *parent = nullptr);
    QString sourceId() const { return m_sourceId; }
    void setSourceId(const QString &value);
    QString xUnit() const { return m_xUnit; }
    void setXUnit(const QString &value);
    QString yUnit() const { return m_yUnit; }
    void setYUnit(const QString &value);
    QVariantMap parameters() const { return m_parameters; }
    void setParameters(const QVariantMap &value);
    int count() const { return m_samples.size(); }
    int missingCount() const { return m_missingCount; }
    QString precision() const { return QStringLiteral("float64"); }
    QString shape() const { return QStringLiteral("[%1,2]").arg(count()); }
    UncertaintyModel *intervals() { return &m_intervals; }
    QString lastError() const { return m_lastError; }
    Q_INVOKABLE bool replaceSamples(const QVariantList &rows);
    Q_INVOKABLE QVariantMap sample(int index) const;
    Q_INVOKABLE QVariantMap sampleById(const QString &id) const;
    Q_INVOKABLE QString csvData() const;
    Q_INVOKABLE QString metadataJson() const;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index,int role) const override;
    QHash<int,QByteArray> roleNames() const override;
signals:
    void changed();
    void errorChanged();
private:
    struct Sample { QString id; double x=0, y=0, sigmaY=0; bool missingY=false, missingSigma=false; };
    enum Role { IdRole=Qt::UserRole+1, XRole, YRole, SigmaRole, MissingRole, XUnitRole, YUnitRole };
    void updateMetadata();
    void setError(const QString &error);
    QString m_sourceId, m_xUnit, m_yUnit, m_lastError;
    QVariantMap m_parameters;
    QVector<Sample> m_samples;
    QHash<QString,int> m_ids;
    UncertaintyModel m_intervals;
    int m_missingCount = 0;
};

} // namespace QindaTK
