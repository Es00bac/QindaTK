// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "document_model.h"
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
class ClipboardService;
class ResourceResolver;
class ScriptRuntime;
class UndoService;

// Plain notebook values (docs/notebooks.md#model). Blocks of a page are
// stored flat in document order (depth-first); `parentId` names an earlier
// block of the same page, and every block between a parent and its child is
// also that parent's descendant.
namespace Notebook {
struct Block {
    QString id, type, parentId;
    QJsonObject data;
    bool locked = false;
    friend bool operator==(const Block &, const Block &) = default;
};
struct Page {
    QString id, sectionId, title;
    QStringList tags;
    QVector<Block> blocks;
    friend bool operator==(const Page &, const Page &) = default;
};
struct Section {
    QString id, title;
    friend bool operator==(const Section &, const Section &) = default;
};
struct State {
    QString title;
    QVector<Section> sections;
    QVector<Page> pages; // global order; a section lists its pages in this order
    friend bool operator==(const State &, const State &) = default;
};
struct Limits {
    static constexpr int MaxSections = 1000;
    static constexpr int MaxPages = 10000;
    static constexpr int MaxBlocksPerPage = 10000;
    static constexpr int MaxBlocks = 250000;
    static constexpr int MaxTextLength = 1 << 20; // UTF-16 units per block
    static constexpr qsizetype MaxFileBytes = qsizetype(64) * 1024 * 1024;
};
} // namespace Notebook

// Editable, persistent notebook: sections, pages and nested blocks with stable
// UUID identity. Every structural or text change is one DocumentCommand in
// the shared UndoService, so DocumentSession save, autosave and recovery work
// unchanged (schema qindatk.notebook.v1). GUI thread only.
class NotebookDocument : public QObject, public DocumentModel {
    Q_OBJECT
    QML_ELEMENT
    Q_MOC_INCLUDE("clipboard_service.h")
    Q_MOC_INCLUDE("command_registry.h")
    Q_MOC_INCLUDE("resource_resolver.h")
    Q_MOC_INCLUDE("undo_service.h")
    Q_MOC_INCLUDE("script_runtime.h")
    Q_PROPERTY(QString documentId READ documentId CONSTANT)
    Q_PROPERTY(QString revision READ revision NOTIFY committedChanged)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY changed)
    Q_PROPERTY(QVariantList sections READ sections NOTIFY changed)
    Q_PROPERTY(QVariantList pages READ pages NOTIFY changed)
    Q_PROPERTY(int blockCount READ blockCount NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QindaTK::UndoService *undoService READ undoService NOTIFY historyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    static constexpr const char *SchemaId = "qindatk.notebook.v1";
    explicit NotebookDocument(QObject *parent = nullptr);
    ~NotebookDocument() override;

    QString documentId() const { return m_documentId; }
    QString revision() const { return QString::number(m_revision); }
    QString title() const { return m_state.title; }
    void setTitle(const QString &title);
    QVariantList sections() const;
    QVariantList pages() const;
    int blockCount() const;
    bool canUndo() const;
    bool canRedo() const;
    UndoService *undoService() const;
    QString lastError() const { return m_error; }
    const Notebook::State &state() const { return m_state; }

    // Structure. Returned IDs are fresh UUIDs; empty on failure (lastError).
    Q_INVOKABLE QString addSection(const QString &title, int index = -1);
    Q_INVOKABLE bool renameSection(const QString &sectionId, const QString &title);
    Q_INVOKABLE bool removeSection(const QString &sectionId);
    // View state: saved with the file, never an undo step.
    Q_INVOKABLE bool setSectionCollapsed(const QString &sectionId, bool collapsed);
    Q_INVOKABLE QString addPage(const QString &sectionId, const QString &afterPageId = {},
                                const QString &title = {});
    Q_INVOKABLE bool renamePage(const QString &pageId, const QString &title);
    Q_INVOKABLE bool setPageTags(const QString &pageId, const QStringList &tags);
    // `index` counts the target section's pages after removal of this one.
    Q_INVOKABLE bool movePage(const QString &pageId, const QString &targetSectionId, int index);
    Q_INVOKABLE bool removePage(const QString &pageId);
    Q_INVOKABLE QVariantMap page(const QString &pageId) const;

    // Blocks (docs/notebooks.md#blocks).
    Q_INVOKABLE QVariantList blocks(const QString &pageId) const;
    Q_INVOKABLE QVariantMap block(const QString &blockId) const;
    Q_INVOKABLE QString pageOfBlock(const QString &blockId) const;
    Q_INVOKABLE QString insertBlock(const QString &pageId, const QString &afterBlockId,
                                    const QString &type, const QVariantMap &data = {});
    Q_INVOKABLE bool setBlockData(const QString &blockId, const QVariantMap &data);
    Q_INVOKABLE bool setBlockText(const QString &blockId, const QString &text);
    Q_INVOKABLE bool replaceText(const QString &blockId, int offset, int removed, const QString &text);
    Q_INVOKABLE bool formatText(const QString &blockId, int offset, int length, const QVariantMap &patch);
    Q_INVOKABLE bool setBlockType(const QString &blockId, const QString &type);
    Q_INVOKABLE bool setBlockLocked(const QString &blockId, bool locked);
    Q_INVOKABLE bool moveBlock(const QString &blockId, const QString &targetPageId,
                               const QString &targetParentId, int targetIndex);
    Q_INVOKABLE bool moveBlockBy(const QString &blockId, int delta);
    Q_INVOKABLE bool indentBlock(const QString &blockId);
    Q_INVOKABLE bool outdentBlock(const QString &blockId);
    Q_INVOKABLE QString splitBlock(const QString &blockId, int offset);
    // Returns the caret offset in `firstBlockId` (its old length), or -1.
    Q_INVOKABLE int mergeBlocks(const QString &firstBlockId, const QString &secondBlockId);
    Q_INVOKABLE bool removeBlock(const QString &blockId);
    Q_INVOKABLE bool setTableCell(const QString &blockId, int row, int column, const QString &text);
    Q_INVOKABLE bool insertTableRow(const QString &blockId, int index);
    Q_INVOKABLE bool removeTableRow(const QString &blockId, int index);
    Q_INVOKABLE bool insertTableColumn(const QString &blockId, int index, const QString &name);
    Q_INVOKABLE bool removeTableColumn(const QString &blockId, int index);

    // Cursor and cross-block selection (docs/notebooks.md#selection). A
    // selection is {anchorBlockId, anchorOffset, focusBlockId, focusOffset}
    // inside one page; offsets are UTF-16 positions snapped to graphemes.
    Q_INVOKABLE QVariantMap moveCaret(const QString &blockId, int offset, const QString &direction) const;
    Q_INVOKABLE QString selectionText(const QVariantMap &selection) const;
    Q_INVOKABLE QString selectionHtml(const QVariantMap &selection) const;
    Q_INVOKABLE QVariantMap selectionRange(const QVariantMap &selection) const;
    Q_INVOKABLE QVariantMap deleteSelection(const QVariantMap &selection);
    Q_INVOKABLE bool copySelection(QindaTK::ClipboardService *clipboard, const QVariantMap &selection);
    Q_INVOKABLE QVariantMap cutSelection(QindaTK::ClipboardService *clipboard, const QVariantMap &selection);
    Q_INVOKABLE QVariantMap paste(QindaTK::ClipboardService *clipboard, const QString &pageId,
                                  const QString &blockId, int offset);
    Q_INVOKABLE QVariantMap insertRange(const QString &pageId, const QString &blockId, int offset,
                                        const QVariantMap &range);
    Q_INVOKABLE QVariantMap insertText(const QString &pageId, const QString &blockId, int offset,
                                       const QString &text);
    Q_INVOKABLE QVariantMap insertHtml(const QString &pageId, const QString &blockId, int offset,
                                       const QString &html);

    // Search, links, assets, evaluation and chart data (docs/notebooks.md#services).
    Q_INVOKABLE QVariantList search(const QString &query, const QVariantMap &options = {}) const;
    Q_INVOKABLE QStringList searchPageIds(const QString &query, const QVariantMap &options = {}) const;
    Q_INVOKABLE QString linkTo(const QString &pageId, const QString &blockId = {}) const;
    Q_INVOKABLE QVariantMap resolveLink(const QString &href) const;
    Q_INVOKABLE QVariantList backlinks(const QString &pageId) const;
    Q_INVOKABLE QVariantList brokenLinks() const;
    Q_INVOKABLE QStringList resourceIds() const;
    Q_INVOKABLE QVariantList assetStatus(QindaTK::ResourceResolver *resolver) const;
    Q_INVOKABLE bool evaluateBlock(const QString &blockId, QindaTK::ScriptRuntime *runtime);
    Q_INVOKABLE QVariantMap chartSeries(const QString &blockId) const;

    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE QString toJson() const;
    // Replaces the content as one undoable step (the document ID is kept).
    Q_INVOKABLE bool loadJson(const QString &json);
    Q_INVOKABLE bool saveToFile(const QString &path);
    Q_INVOKABLE bool loadFromFile(const QString &path);

    // DocumentModel.
    QObject *modelObject() const override { return const_cast<NotebookDocument *>(this); }
    DocumentId modelId() const override { return DocumentId::parse(m_documentId); }
    QString modelKind() const override { return QStringLiteral("notebook"); }
    SnapshotHeader modelHeader() const override;
    QMetaObject::Connection connectModelChanged(QObject *receiver, std::function<void()> slot) override;
    OperationError prepareCommand(const DocumentCommand &command, bool forward) const override;
    void publishCommand(const DocumentCommand &command, bool forward) override;
    DocumentCommandPtr composeCommands(const DocumentCommand &first,
                                       const DocumentCommand &second) const override;

    // Native JSON (docs/notebooks.md#format). decodeNative validates identity,
    // tree order, runs and limits before anything is returned.
    static bool decodeNative(const QByteArray &bytes, Notebook::State *state, SnapshotHeader *header,
                             QJsonObject *extensions, QSet<QString> *collapsed, QString *problem);
    QByteArray encodeNative(const QJsonObject &extensions) const;

signals:
    void changed();
    void committedChanged();
    void historyChanged();
    void errorChanged();
    void blockEvaluated(const QString &blockId, bool success);

private:
    friend class NotebookDocumentCodec;
    struct NotebookCommand;
    struct Location { int page = -1; int block = -1; bool valid() const { return page >= 0; } };
    UndoService *history() const;
    void historyAttached(UndoService *service) override;
    bool commit(const Notebook::State &after, const QString &label, const QString &mergeKey = {});
    bool fail(const QString &message) const;
    void clearError() const;
    int pageIndex(const QString &pageId) const;
    int sectionIndex(const QString &sectionId) const;
    Location locate(const QString &blockId) const;
    bool editableBlock(const QString &blockId, Location *where) const;
    QVariantMap caret(const QString &blockId, int offset) const;
    QVariantMap insertBlocksAt(const QString &pageId, const QString &blockId, int offset,
                               QVector<Notebook::Block> blocks, bool openStart, bool openEnd,
                               const QString &label);
    QVector<Notebook::Block> selectedBlocks(const QVariantMap &selection, int *pageOut,
                                            bool *openStart, bool *openEnd) const;
    void finishEvaluation(const QString &blockId, quint64 stamp, bool success);

    Notebook::State m_state;
    QSet<QString> m_collapsed;
    UndoService *m_localHistory = nullptr;
    QMetaObject::Connection m_historyConnection;
    QString m_documentId;
    quint64 m_revision = 1;
    quint64 m_resetGeneration = 0;
    quint64 m_stamp = 1;
    quint64 m_nextStamp = 2;
    mutable QString m_error;
    mutable QHash<QString, Location> m_blockIndex;
    mutable bool m_indexValid = false;
    QPointer<ScriptRuntime> m_evaluationRuntime;
    QMetaObject::Connection m_evaluationConnection;
};

} // namespace QindaTK
