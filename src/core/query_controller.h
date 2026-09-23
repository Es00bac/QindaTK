// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "editable_grid_model.h"
#include "query_core.h"
#include <QPointer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <memory>

namespace QindaTK {

// QML-facing, bounded refresh adapter. Grid snapshots are gathered in event
// loop chunks on the GUI thread; QueryEngine runs against the immutable copy
// on a worker. Only a still-current request may publish its complete result.
class QueryController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::EditableGridModel *source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QVariantMap schema READ schema WRITE setSchema NOTIFY schemaChanged)
    Q_PROPERTY(QVariantMap query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(QindaTK::EditableGridModel *resultModel READ resultModel NOTIFY resultChanged)
    Q_PROPERTY(QVariantMap rowLabels READ rowLabels NOTIFY resultChanged)
    Q_PROPERTY(QVariantMap columnLabels READ columnLabels NOTIFY resultChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString diagnostic READ diagnostic NOTIFY statusChanged)
    Q_PROPERTY(QString requestId READ requestId NOTIFY statusChanged)
    Q_PROPERTY(int maxSnapshotValues READ maxSnapshotValues WRITE setMaxSnapshotValues
                   NOTIFY maxSnapshotValuesChanged)
public:
    explicit QueryController(QObject *parent = nullptr);
    ~QueryController() override;
    EditableGridModel *source() const { return m_source; }
    void setSource(EditableGridModel *source);
    QVariantMap schema() const { return m_schemaMap; }
    void setSchema(const QVariantMap &schema);
    QVariantMap query() const { return m_queryMap; }
    void setQuery(const QVariantMap &query);
    EditableGridModel *resultModel() const { return m_resultModel; }
    QVariantMap rowLabels() const { return m_rowLabels; }
    QVariantMap columnLabels() const { return m_columnLabels; }
    QString status() const { return m_status; }
    QString diagnostic() const { return m_diagnostic; }
    QString requestId() const { return m_requestId; }
    int maxSnapshotValues() const { return m_maxSnapshotValues; }
    void setMaxSnapshotValues(int count);
    Q_INVOKABLE bool refresh();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE QVariant cellValue(const QString &rowId, const QString &columnId) const;
    Q_INVOKABLE QStringList sourceRows(const QString &rowId, const QString &columnId) const;
signals:
    void sourceChanged();
    void schemaChanged();
    void queryChanged();
    void resultChanged();
    void statusChanged();
    void maxSnapshotValuesChanged();
    void completed(const QString &requestId);
    void rejected(const QString &requestId, const QString &reason);
private:
    struct Run;
    void collect(quint64 generation);
    void publish(quint64 generation, const QueryResult &result);
    void invalidate(const QString &reason);
    void clearResult();
    void setStatus(const QString &status, const QString &diagnostic = {});
    static bool parseSchema(const QVariantMap &map, QuerySchema *schema, QString *error);
    static QStringList requestedFields(const QuerySpec &spec);
    QPointer<EditableGridModel> m_source;
    QPointer<EditableGridModel> m_resultModel;
    QVariantMap m_schemaMap, m_queryMap, m_rowLabels, m_columnLabels;
    QueryResult m_result;
    std::shared_ptr<Run> m_run;
    QString m_status = QStringLiteral("idle"), m_diagnostic, m_requestId;
    quint64 m_generation = 0;
    int m_maxSnapshotValues = 500000;
};

} // namespace QindaTK
