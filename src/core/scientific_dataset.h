// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "uncertainty_model.h"

#include <QAbstractListModel>
#include <QHash>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// One float64 observation. Offsets are nonnegative distances from the
// estimate; `hasSigmaY`/`hasSigmaX` say whether the host supplied them.
struct ScientificSample {
    QString id;
    double x = 0, y = 0, z = 0;
    double sigmaYLow = 0, sigmaYHigh = 0, sigmaXLow = 0, sigmaXHigh = 0;
    bool missingY = false, hasSigmaY = false, hasSigmaX = false;
    bool symmetricY = true, symmetricX = true;
};

// A bounded series of float64 samples. The host owns measurement and
// uncertainty semantics; this adapter validates and preserves values and
// never estimates statistics.
// AGENT-CONTRACT: Trace series have strictly increasing X (binary search,
// line segments and the UncertaintyModel projection rely on it); Scatter
// series accept any finite X and never project intervals. ScientificPlot-
// Renderer and ScientificScene3D read samples() directly (docs/scientific-plot.md).
class ScientificDataset : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString sourceId READ sourceId WRITE setSourceId NOTIFY changed)
    Q_PROPERTY(QString label READ label WRITE setLabel NOTIFY changed)
    Q_PROPERTY(Kind kind READ kind WRITE setKind NOTIFY changed)
    Q_PROPERTY(int dimensions READ dimensions WRITE setDimensions NOTIFY changed)
    Q_PROPERTY(QString xUnit READ xUnit WRITE setXUnit NOTIFY changed)
    Q_PROPERTY(QString yUnit READ yUnit WRITE setYUnit NOTIFY changed)
    Q_PROPERTY(QString zUnit READ zUnit WRITE setZUnit NOTIFY changed)
    Q_PROPERTY(QVariantMap parameters READ parameters WRITE setParameters NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int missingCount READ missingCount NOTIFY changed)
    Q_PROPERTY(QString precision READ precision CONSTANT)
    Q_PROPERTY(QString shape READ shape NOTIFY changed)
    Q_PROPERTY(UncertaintyModel *intervals READ intervals CONSTANT)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    enum Kind { Trace, Scatter };
    Q_ENUM(Kind)
    static constexpr int SampleLimit = 100000;
    explicit ScientificDataset(QObject *parent = nullptr);
    QString sourceId() const { return m_sourceId; }
    void setSourceId(const QString &value);
    QString label() const { return m_label.isEmpty() ? m_sourceId : m_label; }
    void setLabel(const QString &value);
    Kind kind() const { return m_kind; }
    void setKind(Kind value);
    int dimensions() const { return m_dimensions; }
    void setDimensions(int value);
    QString xUnit() const { return m_xUnit; }
    void setXUnit(const QString &value);
    QString yUnit() const { return m_yUnit; }
    void setYUnit(const QString &value);
    QString zUnit() const { return m_zUnit; }
    void setZUnit(const QString &value);
    QVariantMap parameters() const { return m_parameters; }
    void setParameters(const QVariantMap &value);
    int count() const { return m_samples.size(); }
    int missingCount() const { return m_missingCount; }
    QString precision() const { return QStringLiteral("float64"); }
    QString shape() const { return QStringLiteral("[%1,%2]").arg(count()).arg(m_dimensions); }
    UncertaintyModel *intervals() { return &m_intervals; }
    QString lastError() const { return m_lastError; }
    const QVector<ScientificSample> &samples() const { return m_samples; }
    int indexOf(const QString &id) const { return m_ids.value(id, -1); }
    // Trace only: first index with x >= value / first index with x > value.
    int lowerBound(double x) const;
    int upperBound(double x) const;
    // Rows: {id, x, y | missingY:true, sigmaY | sigmaYLow+sigmaYHigh,
    // sigmaX | sigmaXLow+sigmaXHigh, z (dimensions 3)}. All or nothing.
    Q_INVOKABLE bool replaceSamples(const QVariantList &rows);
    Q_INVOKABLE QVariantMap sample(int index) const;
    Q_INVOKABLE QVariantMap sampleById(const QString &id) const;
    // AGENT-CONTRACT: Tk.DataTable reads each row with get(index) (DataTable.qml
    // recordAt); without it ScientificPlot/ScientificPlot3D tables have empty
    // cells. sample() plus xUnit, yUnit (and zUnit in 3D).
    Q_INVOKABLE QVariantMap get(int index) const;
    // Bounded read: at most `limit` (<= 10000) rows with x in [xMin, xMax].
    Q_INVOKABLE QVariantList samplesInRange(double xMin, double xMax, int limit) const;
    // Finite extent of present samples, including uncertainty endpoints.
    Q_INVOKABLE QVariantMap extent() const;
    Q_INVOKABLE QString csvData() const;
    Q_INVOKABLE QString metadataJson() const;
    QVariantMap metadata() const { return m_metadata; }
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index,int role) const override;
    QHash<int,QByteArray> roleNames() const override;
signals:
    void changed();
    void errorChanged();
private:
    // AGENT-GUARD: MissingRole must stay Qt::UserRole+5 (tst_scientific_plot).
    enum Role { IdRole=Qt::UserRole+1, XRole, YRole, SigmaRole, MissingRole, XUnitRole, YUnitRole,
                SigmaYLowRole, SigmaYHighRole, SigmaXLowRole, SigmaXHighRole, ZRole, ZUnitRole };
    bool parseRow(const QVariantMap &row, ScientificSample *sample, QString *error) const;
    void updateMetadata();
    void setError(const QString &error);
    QString m_sourceId, m_label, m_xUnit, m_yUnit, m_zUnit, m_lastError;
    Kind m_kind = Trace;
    int m_dimensions = 2;
    QVariantMap m_parameters, m_metadata;
    QVector<ScientificSample> m_samples;
    QHash<QString,int> m_ids;
    UncertaintyModel m_intervals;
    int m_missingCount = 0;
};

} // namespace QindaTK
