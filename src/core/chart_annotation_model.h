// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

struct ChartAnnotation {
    enum Kind { Point, VerticalLine, HorizontalLine } kind = Point;
    QString id, label, xUnit, yUnit, category;
    double x = 0, y = 0;
    qint64 utcMs = 0;
    bool hasUtc = false, hasCategory = false;
};

// AGENT-GUARD: records retain their stable IDs and exact UTC decimal strings.
// Replacement rejects the whole batch before changing the accessible model.
class ChartAnnotationModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ count NOTIFY annotationsChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    enum Kind { Point = ChartAnnotation::Point, VerticalLine = ChartAnnotation::VerticalLine,
                HorizontalLine = ChartAnnotation::HorizontalLine };
    Q_ENUM(Kind)
    enum Role { IdRole = Qt::UserRole + 1, LabelRole, KindRole, XRole, UtcRole,
                CategoryRole, YRole, XUnitRole, YUnitRole };
    Q_ENUM(Role)
    explicit ChartAnnotationModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return m_annotations.size(); }
    QString lastError() const { return m_error; }
    const QVector<ChartAnnotation> &annotations() const { return m_annotations; }
    Q_INVOKABLE bool replaceAnnotations(const QVariantList &rows);
    Q_INVOKABLE QVariantMap get(int row) const;
signals:
    void annotationsChanged();
    void errorChanged();
private:
    void setError(const QString &error);
    QVector<ChartAnnotation> m_annotations;
    QString m_error;
};

} // namespace QindaTK
