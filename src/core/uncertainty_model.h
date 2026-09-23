// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QAbstractListModel>
#include <QHash>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
// AGENT-CONTRACT: the host supplies interval endpoints and meaning. The model
// validates and preserves them; it never estimates confidence or credibility.
class UncertaintyModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(Meaning meaning READ meaning WRITE setMeaning NOTIFY changed)
    Q_PROPERTY(double coverage READ coverage WRITE setCoverage NOTIFY changed)
    Q_PROPERTY(QString meaningDetail READ meaningDetail WRITE setMeaningDetail NOTIFY changed)
    Q_PROPERTY(QString sourceDatasetId READ sourceDatasetId WRITE setSourceDatasetId NOTIFY changed)
    Q_PROPERTY(QString xUnit READ xUnit WRITE setXUnit NOTIFY changed)
    Q_PROPERTY(QString yUnit READ yUnit WRITE setYUnit NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int missingCount READ missingCount NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    enum Meaning { Confidence, Credible, Domain };
    Q_ENUM(Meaning)
    struct Interval {
        QString id, sourceId;
        double x=0, estimate=0, low=0, high=0;
        bool missing=false;
    };
    explicit UncertaintyModel(QObject *parent=nullptr);
    Meaning meaning() const { return m_meaning; }
    void setMeaning(Meaning value);
    double coverage() const { return m_coverage; }
    void setCoverage(double value);
    QString meaningDetail() const { return m_meaningDetail; }
    void setMeaningDetail(const QString &value);
    QString sourceDatasetId() const { return m_sourceDatasetId; }
    void setSourceDatasetId(const QString &value);
    QString xUnit() const { return m_xUnit; }
    void setXUnit(const QString &value);
    QString yUnit() const { return m_yUnit; }
    void setYUnit(const QString &value);
    int count() const { return m_rows.size(); }
    int missingCount() const { return m_missingCount; }
    QString lastError() const { return m_lastError; }
    QString meaningName() const;
    Q_INVOKABLE QString meaningLabel() const;
    Q_INVOKABLE bool replaceIntervals(const QVariantList &rows);
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE QVariantMap interval(const QString &id) const;
    Q_INVOKABLE QString csvData() const;
    Q_INVOKABLE QString metadataJson() const;
    int indexOf(const QString &id) const { return m_ids.value(id,-1); }
    int lowerBound(double x) const;
    int upperBound(double x) const;
    const QVector<Interval> &intervals() const { return m_rows; }
    int rowCount(const QModelIndex &parent={}) const override;
    QVariant data(const QModelIndex &index,int role) const override;
    QHash<int,QByteArray> roleNames() const override;
signals:
    void changed();
    void errorChanged();
private:
    void setError(const QString &value);
    void notifyMetadata();
    enum Role { IdRole=Qt::UserRole+1, SourceIdRole, XRole, EstimateRole,
                LowRole, HighRole, StatusRole, XUnitRole, YUnitRole,
                MeaningRole, CoverageRole, DetailRole };
    QVector<Interval> m_rows;
    QHash<QString,int> m_ids;
    Meaning m_meaning=Confidence;
    double m_coverage=0.95;
    QString m_meaningDetail,m_sourceDatasetId,m_xUnit,m_yUnit,m_lastError;
    int m_missingCount=0;
};
} // namespace QindaTK
