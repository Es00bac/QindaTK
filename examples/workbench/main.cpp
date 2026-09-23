// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickView>
#include <QQmlContext>
#include <QQmlEngine>
#include <QPointer>
#include <QDebug>
#include <QStandardItemModel>
#include <QTextStream>
#include <QTimer>

#include <qindatk/density.h>
#include <qindatk/document_manager.h>
#include <qindatk/editable_grid_model.h>
#include <qindatk/property_document.h>
#include <qindatk/command_context.h>
#include <qindatk/command_registry.h>
#include <qindatk/property_binding.h>
#include <qindatk/shortcut_map.h>

#include <memory>
#include <vector>

using namespace QindaTK;

namespace {
const QStringList columns{QStringLiteral("name"), QStringLiteral("kind"),
                          QStringLiteral("owner"), QStringLiteral("state")};
struct Record {
    const char *id;
    const char *name;
    const char *kind;
    const char *owner;
    const char *state;
};
const QVector<Record> boardRows{
    {"00000000-0000-0000-0000-000000000101", "Opening shot", "Video", "Editorial", "Ready"},
    {"00000000-0000-0000-0000-000000000102", "Title card", "Graphic", "Design", "Review"},
    {"00000000-0000-0000-0000-000000000103", "Interview", "Video", "Editorial", "Ready"},
    {"00000000-0000-0000-0000-000000000104", "End frame", "Graphic", "Design", "Draft"}};
const QVector<Record> sheetRows{
    {"00000000-0000-0000-0000-000000000201", "Dialogue", "Audio", "Mix", "Ready"},
    {"00000000-0000-0000-0000-000000000202", "Room tone", "Audio", "Mix", "Review"},
    {"00000000-0000-0000-0000-000000000203", "Music", "Audio", "Mix", "Draft"},
    {"00000000-0000-0000-0000-000000000204", "Master", "Bus", "Mix", "Ready"}};

class DocumentCellSource : public GridCellSource {
public:
    explicit DocumentCellSource(PropertyDocument *document, QObject *parent = nullptr)
        : GridCellSource(parent), m_document(document) {}
    QVariant value(const QString &rowId, const QString &columnId) const override
    { return m_document ? m_document->valueFor(rowId, columnId) : QVariant{}; }
private:
    QPointer<PropertyDocument> m_document;
};

void dumpItems(QTextStream &out, QQuickItem *item)
{
    if (!item) return;
    if (item->objectName().startsWith(QStringLiteral("workbench")))
        out << item->objectName() << ' ' << item->x() << ',' << item->y() << ' '
            << item->width() << 'x' << item->height() << '\n';
    for (QQuickItem *child : item->childItems()) dumpItems(out, child);
}
} // namespace

class WorkbenchHost : public QObject {
    Q_OBJECT
    Q_PROPERTY(QindaTK::DocumentManager *manager READ manager CONSTANT)
    Q_PROPERTY(QindaTK::DocumentSession *boardSession READ boardSession CONSTANT)
    Q_PROPERTY(QindaTK::DocumentSession *sheetSession READ sheetSession CONSTANT)
    Q_PROPERTY(QindaTK::EditableGridModel *boardGrid READ boardGrid CONSTANT)
    Q_PROPERTY(QindaTK::EditableGridModel *sheetGrid READ sheetGrid CONSTANT)
    Q_PROPERTY(QAbstractItemModel *boardAssets READ boardAssets CONSTANT)
    Q_PROPERTY(QAbstractItemModel *sheetAssets READ sheetAssets CONSTANT)
    Q_PROPERTY(QindaTK::CommandRegistry *commands READ commands CONSTANT)
    Q_PROPERTY(QindaTK::CommandContext *commandContext READ commandContext CONSTANT)
public:
    explicit WorkbenchHost(QObject *parent = nullptr) : QObject(parent)
    {
        m_manager.setObjectName(QStringLiteral("workbenchDocumentManager"));
        m_boardGrid.setObjectName(QStringLiteral("workbenchBoardGrid"));
        m_sheetGrid.setObjectName(QStringLiteral("workbenchSheetGrid"));
        m_boardAssets.setObjectName(QStringLiteral("workbenchBoardAssets"));
        m_sheetAssets.setObjectName(QStringLiteral("workbenchSheetAssets"));
        m_board = seed(boardRows, &m_boardGrid, &m_boardAssets);
        m_sheet = seed(sheetRows, &m_sheetGrid, &m_sheetAssets);
        m_commands.setContext(&m_commandContext);
        const QString appScope = m_commandContext.pushScope(QStringLiteral("workbench"), this);
        m_boardScope = m_commandContext.pushScope(QStringLiteral("storyboard"), this, appScope);
        m_sheetScope = m_commandContext.pushScope(QStringLiteral("mix-sheet"), this, appScope);
        m_commands.registerCommand({QStringLiteral("workbench.rename"), QStringLiteral("Apply name"),
                                    QStringLiteral("check"), QStringLiteral("Inspector"),
                                    QStringLiteral("rename name edit"), QStringLiteral("Ctrl+Shift+R"), false});
        if (m_board) registerNameHandler(m_board, m_boardScope);
        if (m_sheet) registerNameHandler(m_sheet, m_sheetScope);
        connect(&m_manager, &DocumentManager::changed, this, &WorkbenchHost::syncCommandScope);
        if (m_board) m_manager.activate(m_board->documentId());
        syncCommandScope();
    }
    DocumentManager *manager() { return &m_manager; }
    DocumentSession *boardSession() { return m_board; }
    DocumentSession *sheetSession() { return m_sheet; }
    EditableGridModel *boardGrid() { return &m_boardGrid; }
    EditableGridModel *sheetGrid() { return &m_sheetGrid; }
    QAbstractItemModel *boardAssets() { return &m_boardAssets; }
    QAbstractItemModel *sheetAssets() { return &m_sheetAssets; }
    CommandRegistry *commands() { return &m_commands; }
    CommandContext *commandContext() { return &m_commandContext; }

    void setNameBinding(PropertyBinding *binding) { m_nameBinding = binding; }

    Q_INVOKABLE bool stageNameEdit(const QString &entityId, const QString &value)
    {
        auto *session = m_manager.activeSession();
        if (!session || entityId.isEmpty() || !session->document()->containsId(entityId))
            return false;
        const QString scope = session == m_board ? m_boardScope : m_sheetScope;
        if (scope.isEmpty() || !m_commandContext.activate(scope)) return false;
        m_pendingEntityId = entityId;
        m_pendingName = value;
        const QStringList views = m_manager.viewsFor(session->documentId());
        return m_commandContext.updateScope(scope,
            {{QStringLiteral("snapshotHeader"), ContractCodec::toQml(session->document()->header())},
             {QStringLiteral("selectionIds"), QStringList{entityId}},
             {QStringLiteral("focusedViewId"), views.isEmpty() ? QString() : views.first()}});
    }

    Q_INVOKABLE bool triggerRename() { return m_commands.trigger(QStringLiteral("workbench.rename")); }

private:
    void registerNameHandler(DocumentSession *session, const QString &scope)
    {
        m_commands.registerHandler(scope, QStringLiteral("workbench.rename"), this,
            [this, session](const CommandInvocationContext &invocation, const QVariantMap &) {
                PropertyDocument *document = session->document();
                const SnapshotHeader current = document->header();
                if (invocation.request.expected.documentId != current.documentId
                    || invocation.request.expected.revision != current.revision
                    || invocation.request.expected.resetGeneration != current.resetGeneration
                    || invocation.selection != QVector<EntityId>{EntityId::parse(m_pendingEntityId)})
                    return OperationError{ErrorCode::StaleRevision,
                                          QStringLiteral("The selected row changed before rename")};
                if (!m_nameBinding || m_nameBinding->document() != document
                    || !m_nameBinding->targetIds().contains(m_pendingEntityId))
                    return OperationError{ErrorCode::Unavailable,
                                          QStringLiteral("The inspector is not bound to this document row")};
                if (m_pendingName.trimmed().isEmpty())
                    return OperationError{ErrorCode::Validation, QStringLiteral("Name cannot be empty")};
                if (!m_nameBinding->begin() || !m_nameBinding->update(m_pendingName.trimmed())) {
                    m_nameBinding->cancel();
                    return OperationError{ErrorCode::Validation,
                                          QStringLiteral("The name edit was rejected")};
                }
                if (!m_nameBinding->commit())
                    return OperationError{ErrorCode::Validation,
                                          QStringLiteral("The name edit could not commit")};
                return OperationError{};
            });
    }

    void syncCommandScope()
    {
        auto *session = m_manager.activeSession();
        const QString scope = session == m_board ? m_boardScope
                              : session == m_sheet ? m_sheetScope : QString{};
        if (!scope.isEmpty()) m_commandContext.activate(scope);
        m_manager.undoService()->setActiveDocument(session ? session->document() : nullptr);
    }

    DocumentSession *seed(const QVector<Record> &rows, EditableGridModel *grid,
                          QStandardItemModel *assets)
    {
        auto *session = m_manager.createReferenceDocument();
        if (!session) return nullptr;
        PropertyDocument *document = session->document();
        for (const QString &column : columns)
            if (!document->defineProperty({column, QMetaType::fromType<QString>(),
                                           QString(), {}, true, false})) return nullptr;
        QStringList ids;
        assets->setItemRoleNames({{Qt::UserRole + 1, "id"},
                                 {Qt::UserRole + 2, "resourceId"},
                                 {Qt::UserRole + 3, "label"},
                                 {Qt::UserRole + 4, "caption"}});
        for (const Record &row : rows) {
            const QString id = QString::fromLatin1(row.id);
            const QString name = QString::fromLatin1(row.name);
            const QString kind = QString::fromLatin1(row.kind);
            const QString state = QString::fromLatin1(row.state);
            if (!document->addEntity(EntityId::parse(id),
                                     {{columns[0], name}, {columns[1], kind},
                                      {columns[2], QString::fromLatin1(row.owner)},
                                      {columns[3], state}})) return nullptr;
            ids.append(id);
            auto *item = new QStandardItem;
            item->setData(id, Qt::UserRole + 1);
            item->setData(QString(), Qt::UserRole + 2);
            item->setData(name, Qt::UserRole + 3);
            item->setData(name + QStringLiteral(" · ") + state, Qt::UserRole + 4);
            assets->appendRow(item);
        }
        grid->setRowIds(ids);
        grid->setColumnIds(columns);
        auto source = std::make_unique<DocumentCellSource>(document, grid);
        grid->setSource(source.get());
        m_sources.push_back(std::move(source));
        grid->setReadOnly(true);
        connect(document, &PropertyDocument::propertyChanged, this,
                [document, grid, assets, ids](const QString &entityId, const QString &propertyId) {
            const int row = ids.indexOf(entityId);
            const int column = columns.indexOf(propertyId);
            if (row < 0 || column < 0) return;
            emit grid->dataChanged(grid->index(row, column), grid->index(row, column));
            if (column == 0) assets->item(row)->setData(document->valueFor(entityId, propertyId),
                                                      Qt::UserRole + 3);
            if (column == 0 || column == 3)
                assets->item(row)->setData(document->valueFor(entityId, columns[0]).toString()
                                              + QStringLiteral(" · ")
                                              + document->valueFor(entityId, columns[3]).toString(),
                                          Qt::UserRole + 4);
        });
        return session;
    }
    DocumentManager m_manager;
    DocumentSession *m_board = nullptr;
    DocumentSession *m_sheet = nullptr;
    EditableGridModel m_boardGrid;
    EditableGridModel m_sheetGrid;
    QStandardItemModel m_boardAssets;
    QStandardItemModel m_sheetAssets;
    CommandContext m_commandContext;
    CommandRegistry m_commands;
    QString m_boardScope;
    QString m_sheetScope;
    QString m_pendingEntityId;
    QString m_pendingName;
    QPointer<PropertyBinding> m_nameBinding;
    std::vector<std::unique_ptr<DocumentCellSource>> m_sources;
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    const int densityOption = arguments.indexOf(QStringLiteral("--density"));
    if (densityOption >= 0 && densityOption + 1 < arguments.size())
        Density::instance()->setModeName(arguments.at(densityOption + 1));
    WorkbenchHost host;
    QQuickView window;
    window.engine()->addImportPath(QStringLiteral(QINDATK_QML_IMPORT_ROOT));
    window.rootContext()->setContextProperty(QStringLiteral("host"), &host);
    window.setResizeMode(QQuickView::SizeRootObjectToView);
    window.setSource(QUrl::fromLocalFile(QStringLiteral(WORKBENCH_QML_FILE)));
    if (window.status() != QQuickView::Ready || !window.rootObject()) return 1;
    host.setNameBinding(window.rootObject()->findChild<PropertyBinding *>(
        QStringLiteral("workbenchNameBinding")));
    if (!host.commands()->resolve(QStringLiteral("workbench.rename")).enabled
        || !host.commands()->resolve(QStringLiteral("workbench.rename")).visible) return 1;
    window.resize(1280, 800);
    window.show();
    if (arguments.contains(QStringLiteral("--check"))) {
        QCoreApplication::processEvents();
        auto *board = host.boardSession();
        auto *sheet = host.sheetSession();
        if (!board || !sheet || host.manager()->sessions().size() != 2
            || !host.boardGrid()->readOnly()
            || host.boardGrid()->cellValue(QString::fromLatin1(boardRows[0].id),
                                           QStringLiteral("name"))
                   != QStringLiteral("Opening shot")) return 2;
        QVariant staged;
        QVariant activated;
        const QString firstName = QStringLiteral("Opening revised");
        const quint64 boardRevisionBefore = board->document()->revision().value;
        if (!QMetaObject::invokeMethod(window.rootObject(), "stageNameValue",
                                       Q_RETURN_ARG(QVariant, staged),
                                       Q_ARG(QVariant, QVariant(firstName)))
            || !staged.toBool()
            || !QMetaObject::invokeMethod(window.rootObject(), "invokeRenameFromToolbar",
                                          Q_RETURN_ARG(QVariant, activated))
            || !activated.toBool()
            || board->document()->valueFor(QString::fromLatin1(boardRows[0].id),
                                            QStringLiteral("name")) != firstName
            || host.boardAssets()->data(host.boardAssets()->index(0, 0), Qt::UserRole + 4)
                   != QStringLiteral("Opening revised · Ready")
            || board->document()->revision().value != boardRevisionBefore + 1) return 3;
        if (!host.manager()->undoService()->undo()
            || board->document()->valueFor(QString::fromLatin1(boardRows[0].id),
                                            QStringLiteral("name"))
                   != QStringLiteral("Opening shot")) return 4;

        const QString menuName = QStringLiteral("Opening from menu");
        if (!QMetaObject::invokeMethod(window.rootObject(), "stageNameValue",
                                       Q_RETURN_ARG(QVariant, staged),
                                       Q_ARG(QVariant, QVariant(menuName)))
            || !staged.toBool()
            || !QMetaObject::invokeMethod(window.rootObject(), "invokeRenameFromMenu",
                                          Q_RETURN_ARG(QVariant, activated))
            || !activated.toBool()
            || board->document()->valueFor(QString::fromLatin1(boardRows[0].id),
                                            QStringLiteral("name")) != menuName
            || !host.manager()->undoService()->undo()) return 5;

        host.manager()->activate(sheet->documentId());
        QCoreApplication::processEvents();
        if (window.rootObject()->property("activeWorkspaceName") != QStringLiteral("Mix sheet")
            || window.rootObject()->property("selectedName") != QStringLiteral("Dialogue"))
            return 6;
        const QString sheetName = QStringLiteral("Dialogue via shortcut");
        if (!QMetaObject::invokeMethod(window.rootObject(), "stageNameValue",
                                       Q_RETURN_ARG(QVariant, staged),
                                       Q_ARG(QVariant, QVariant(sheetName)))
            || !staged.toBool()) return 7;
        auto *shortcuts = window.rootObject()->findChild<ShortcutMap *>(
            QStringLiteral("workbenchShortcutMap"));
        if (!shortcuts || !shortcuts->dispatchPortable(QStringLiteral("Ctrl+Shift+R"))
            || sheet->document()->valueFor(QString::fromLatin1(sheetRows[0].id),
                                            QStringLiteral("name")) != sheetName
            || board->document()->valueFor(QString::fromLatin1(boardRows[0].id),
                                            QStringLiteral("name")) != QStringLiteral("Opening shot")
            || !host.manager()->undoService()->undo()
            || sheet->document()->valueFor(QString::fromLatin1(sheetRows[0].id),
                                            QStringLiteral("name")) != QStringLiteral("Dialogue"))
            return 8;
        host.manager()->activate(board->documentId());
        qInfo("WP-033 workbench: toolbar, context menu and shortcut rename routes, projection, undo and document retarget passed");
    }
    const int grabOption = arguments.indexOf(QStringLiteral("--grab"));
    const bool dump = arguments.contains(QStringLiteral("--dump"));
    if (grabOption >= 0 || dump || arguments.contains(QStringLiteral("--check"))) {
        QTimer::singleShot(250, &app, [&] {
            if (dump) {
                QTextStream out(stdout);
                dumpItems(out, window.rootObject());
                out << "documents=" << host.manager()->sessions().size()
                    << " active=" << host.manager()->activeSession()->documentId() << '\n';
                out.flush();
            }
            if (grabOption >= 0 && grabOption + 1 < arguments.size())
                if (!window.grabWindow().save(arguments.at(grabOption + 1)))
                    qCritical("Could not save workbench capture");
            app.quit();
        });
    }
    return app.exec();
}

#include "main.moc"
