// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"
#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>
#include <QVector>

namespace QindaTK {

// Selection is stored as stable entity identity. Visible-order range semantics
// are supplied by the view on each extendTo call; indices are never retained.
class SelectionModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QStringList selectedIds READ selectedIds NOTIFY selectionChanged)
    Q_PROPERTY(QString currentId READ currentId NOTIFY currentChanged)
    Q_PROPERTY(QString anchorId READ anchorId NOTIFY anchorChanged)
    Q_PROPERTY(QString generation READ generation NOTIFY generationChanged)
public:
    explicit SelectionModel(QObject *parent = nullptr);

    QStringList selectedIds() const;
    QString currentId() const { return m_current.isValid() ? m_current.toString() : QString{}; }
    QString anchorId() const { return m_anchor.isValid() ? m_anchor.toString() : QString{}; }
    QString generation() const { return QString::number(m_generation); }

    Q_INVOKABLE bool select(const QString &id);
    Q_INVOKABLE bool toggle(const QString &id);
    Q_INVOKABLE bool extendTo(const QString &id, const QStringList &orderedVisibleIds);
    Q_INVOKABLE bool clear();
    Q_INVOKABLE bool reconcileLiveIds(const QStringList &allModelIds);
    // Source-model removal only. Filtered-out IDs remain selected.
    Q_INVOKABLE bool removeIds(const QStringList &removedIds);
    Q_INVOKABLE bool resetModel(const QStringList &allModelIds,
                                const QString &resetGeneration);

signals:
    void selectionChanged();
    void currentChanged();
    void anchorChanged();
    void generationChanged();
    // Emitted only after a user-facing selection operation changes selection.
    void selectionModified(const QStringList &selectedIds,
                           const QString &currentId, const QString &anchorId);

private:
    static std::optional<EntityId> parseId(const QString &text);
    static std::optional<QVector<EntityId>> parseUniqueIds(const QStringList &ids);
    bool onOwnerThread() const;
    bool publishUserChange(const QVector<EntityId> &selection,
                           const EntityId &current, const EntityId &anchor);
    void publishModelChange(const QVector<EntityId> &selection,
                            const EntityId &current, const EntityId &anchor,
                            quint64 generation, bool generationChanged);

    QVector<EntityId> m_selection;
    EntityId m_current;
    EntityId m_anchor;
    quint64 m_generation = 0;
};

} // namespace QindaTK
