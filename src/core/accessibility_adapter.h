// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "command_registry.h"
#include "selection_model.h"
#include <QAccessible>
#include <QQuickItem>
#include <QHash>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
// A semantic child is independent of visual delegate lifetime. Rectangles are
// adapter-local; supply all live model IDs, including offscreen entities.
struct AccessibleNode {
    QString id;
    QString name;
    QString description;
    QAccessible::Role role = QAccessible::ListItem;
    QRectF bounds;
    QVariant value;
    QVariant minimum;
    QVariant maximum;
    QVariant step;
    bool enabled = true;
    bool selected = false;
    bool focused = false;
    bool offscreen = false;
    bool readOnly = true;
    QMap<QString, QString> commands; // accessible action name -> command ID
};

class AccessibilityAdapter : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString accessibleName READ accessibleName WRITE setAccessibleName NOTIFY semanticsChanged)
    Q_PROPERTY(QindaTK::CommandRegistry *commands READ commands WRITE setCommands NOTIFY commandsChanged)
    Q_PROPERTY(QindaTK::SelectionModel *selection READ selection WRITE setSelection NOTIFY selectionChanged)
public:
    explicit AccessibilityAdapter(QQuickItem *parent = nullptr);
    ~AccessibilityAdapter() override;
    QString accessibleName() const { return m_name; }
    void setAccessibleName(const QString &name);
    CommandRegistry *commands() const { return m_commands; }
    void setCommands(CommandRegistry *commands);
    SelectionModel *selection() const { return m_selection; }
    void setSelection(SelectionModel *selection);
    // Atomic replacement rejects empty/duplicate IDs; cache bounded by live IDs.
    bool replaceNodes(const QVector<AccessibleNode> &nodes);
    Q_INVOKABLE bool setNodes(const QVariantList &nodes);
    Q_INVOKABLE bool invoke(const QString &id, const QString &action,
                            const QVariant &value = {});
    Q_INVOKABLE void announce(const QString &message);
    const AccessibleNode *node(const QString &id) const;
    QStringList nodeIds() const { return m_order; }
    QAccessibleInterface *interfaceFor(const QString &id) const;
signals:
    void semanticsChanged();
    void commandsChanged();
    void selectionChanged();
    void actionRejected(const QString &id, const QString &reason);
    void announcementDelivered(const QString &message);
private:
    QString m_name;
    QPointer<CommandRegistry> m_commands;
    QPointer<SelectionModel> m_selection;
    QHash<QString, AccessibleNode> m_nodes;
    QStringList m_order;
    mutable QHash<QString, QAccessible::Id> m_interfaces;
    QStringList m_announcements;
    bool m_announcementPending = false;
};
} // namespace QindaTK
