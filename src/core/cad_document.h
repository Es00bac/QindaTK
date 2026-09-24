// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT (docs/cad.md): all geometry and constraints are stored in
// millimeters; CadViewport only converts screen gestures and displays snapshots.
class CadDocument : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList points READ points NOTIFY changed)
    Q_PROPERTY(QVariantList segments READ segments NOTIFY changed)
    Q_PROPERTY(QVariantList circles READ circles NOTIFY changed)
    Q_PROPERTY(QVariantList constraints READ constraints NOTIFY changed)
    Q_PROPERTY(QString units READ units NOTIFY changed)
    Q_PROPERTY(qulonglong revision READ revision NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    explicit CadDocument(QObject *parent = nullptr);

    QVariantList points() const;
    QVariantList segments() const;
    QVariantList circles() const;
    QVariantList constraints() const;
    QString units() const { return m_state.units; }
    qulonglong revision() const { return m_revision; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    QString lastError() const { return m_error; }

    Q_INVOKABLE QString addPoint(double x, double y);
    Q_INVOKABLE QString addSegment(const QString &startPointId, const QString &endPointId);
    Q_INVOKABLE QString addCircle(const QString &centerPointId, double radius);
    Q_INVOKABLE QString addConstraint(const QString &kind, const QString &firstId,
                                      const QString &secondId = {}, double value = 0);
    Q_INVOKABLE bool movePoint(const QString &pointId, double x, double y);
    Q_INVOKABLE bool setCircleRadius(const QString &circleId, double radius);
    Q_INVOKABLE bool removeEntity(const QString &entityId);
    Q_INVOKABLE bool changeUnits(const QString &units);
    Q_INVOKABLE QVariantMap measure(const QString &entityId) const;
    Q_INVOKABLE QVariantMap snap(double x, double y, double tolerance,
                                 double gridStep = 0, const QString &excludePointId = {}) const;
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE bool saveToFile(const QString &path);
    Q_INVOKABLE bool loadFromFile(const QString &path);

signals:
    void changed();
    void errorChanged();

private:
    struct Point { QString id; double x = 0; double y = 0; };
    struct Segment { QString id; QString start; QString end; };
    struct Circle { QString id; QString center; double radius = 0; };
    struct Constraint { QString id; QString kind; QString first; QString second; double value = 0; };
    struct State {
        QVector<Point> points;
        QVector<Segment> segments;
        QVector<Circle> circles;
        QVector<Constraint> constraints;
        QString units = QStringLiteral("mm");
    };

    int pointIndex(const State &state, const QString &id) const;
    int segmentIndex(const State &state, const QString &id) const;
    int circleIndex(const State &state, const QString &id) const;
    double unitScale() const;
    void rememberEdit();
    void setError(const QString &message);
    bool validGeometry(const State &state, QString *diagnostic) const;
    bool solve(State *state, const QString &lockedPointId, QString *diagnostic) const;
    bool decode(const QByteArray &bytes, State *result, QString *diagnostic) const;

    State m_state;
    QVector<State> m_undo;
    QVector<State> m_redo;
    QString m_error;
    quint64 m_revision = 0;
};

} // namespace QindaTK
