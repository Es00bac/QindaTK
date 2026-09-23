// SPDX-License-Identifier: LGPL-3.0-or-later
#include "core/dock_model.h"
#include "core/document_manager.h"
#include "core/settings_model.h"
#include "core/settings_persistence.h"
#include "core/workspace_session.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace QindaTK;

namespace {

class MemoryStateStore final : public StateStore {
public:
    QHash<QString, QByteArray> files;
    StateReadResult read(const QString &path, qsizetype maximumBytes) override
    {
        const auto it = files.constFind(path);
        if (it == files.cend())
            return {{}, {ErrorCode::Unavailable, QStringLiteral("missing"), path, {}}};
        if (it->size() > maximumBytes)
            return {{}, {ErrorCode::Validation, QStringLiteral("too large"), path, {}}};
        return {*it, {}};
    }
    OperationError write(const QString &path, const QByteArray &bytes) override
    {
        files.insert(path, bytes);
        return {};
    }
};

QVariantMap rect(double x, double y, double width, double height)
{
    return {{QStringLiteral("x"), x}, {QStringLiteral("y"), y},
            {QStringLiteral("width"), width}, {QStringLiteral("height"), height}};
}

QVariantMap workspaceState(const QString &documentId, const QString &viewId,
                           const QString &windowId, const QString &uri)
{
    return {{QStringLiteral("version"), 1},
            {QStringLiteral("documents"), QVariantList{QVariantMap{
                {QStringLiteral("documentId"), documentId}, {QStringLiteral("uri"), uri}}}},
            {QStringLiteral("views"), QVariantList{QVariantMap{
                {QStringLiteral("viewId"), viewId}, {QStringLiteral("documentId"), documentId},
                {QStringLiteral("windowId"), windowId}, {QStringLiteral("state"), QVariantMap{}}}}},
            {QStringLiteral("windows"), QVariantList{QVariantMap{
                {QStringLiteral("windowId"), windowId}, {QStringLiteral("screenId"), QStringLiteral("gone-screen")},
                {QStringLiteral("geometry"), rect(-800, 1200, 900, 700)},
                {QStringLiteral("activeViewId"), viewId}}}},
            {QStringLiteral("activeWindowId"), windowId},
            {QStringLiteral("activeDocumentId"), documentId},
            {QStringLiteral("dockState"), QString{}},
            {QStringLiteral("vendorField"), QStringLiteral("keep-me")}};
}

SettingDescriptor stringSetting(const QString &key, SettingScope scope,
                                const QString &initial)
{
    return {key, scope, QMetaType::fromType<QString>(), initial, false, {}};
}

} // namespace

class TstWorkspaceSession final : public QObject {
    Q_OBJECT
private slots:
    void settingsAndWorkspacePersistIndependently()
    {
        auto store = std::make_shared<MemoryStateStore>();
        SettingsModel settings;
        QVERIFY(settings.registerSetting(stringSetting(QStringLiteral("theme"),
            SettingScope::App, QStringLiteral("dark"))));
        QVERIFY(settings.setValue(QStringLiteral("app:theme"), QStringLiteral("light")));
        QVERIFY(settings.importValues({{QStringLiteral("app:theme"), QStringLiteral("light")},
                                      {QStringLiteral("app:future"), 17}}));
        SettingsPersistence preferences;
        preferences.setPath(QStringLiteral("settings"));
        preferences.setStore(store);
        QVERIFY(preferences.save(&settings));

        WorkspaceSession workspace;
        workspace.setPath(QStringLiteral("workspace"));
        workspace.setStore(store);
        const QString doc = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString view = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString window = QStringLiteral("main");
        const QVariantList screens{QVariantMap{{QStringLiteral("id"), QStringLiteral("primary")},
            {QStringLiteral("availableRect"), rect(0, 0, 1280, 800)}}};
        QVERIFY(workspace.setState(workspaceState(doc, view, window, {}), screens));
        QVERIFY(workspace.save());
        QVERIFY(store->files.contains(QStringLiteral("settings")));
        QVERIFY(store->files.contains(QStringLiteral("workspace")));

        SettingsModel restoredSettings;
        QVERIFY(restoredSettings.registerSetting(stringSetting(QStringLiteral("theme"),
            SettingScope::App, QStringLiteral("dark"))));
        SettingsPersistence settingsReader;
        settingsReader.setPath(QStringLiteral("settings"));
        settingsReader.setStore(store);
        QVERIFY(settingsReader.load(&restoredSettings));
        QCOMPARE(restoredSettings.value(QStringLiteral("theme")).toString(), QStringLiteral("light"));
        QCOMPARE(restoredSettings.orphanedValues().value(QStringLiteral("app:future")).toInt(), 17);

        WorkspaceSession restoredWorkspace;
        restoredWorkspace.setPath(QStringLiteral("workspace"));
        restoredWorkspace.setStore(store);
        QVERIFY(restoredWorkspace.load(screens));
        QCOMPARE(restoredWorkspace.views().size(), 1);
        QCOMPARE(restoredWorkspace.state().value(QStringLiteral("vendorField")).toString(),
                 QStringLiteral("keep-me"));
        QCOMPARE(store->files.size(), 2);
    }

    void settingsV1MigratesAndUnknownExtensionFieldsRoundTrip()
    {
        const QByteArray legacy = R"({"version":1,"values":{"app:enabled":true,"app:count":12,"app:name":"Edit"},"extensions":{"vendor":{"x":1}}})";
        QJsonObject extensions;
        OperationError error;
        const auto values = SettingsPersistence::decode(legacy, &extensions, &error);
        QVERIFY2(values.has_value(), qPrintable(error.message));
        QCOMPARE(values->value(QStringLiteral("app:enabled")).metaType(), QMetaType::fromType<bool>());
        QCOMPARE(values->value(QStringLiteral("app:count")).toInt(), 12);
        QCOMPARE(extensions.value(QStringLiteral("vendor")).toObject().value(QStringLiteral("x")).toInt(), 1);

        SettingsModel model;
        QVERIFY(model.registerSetting({QStringLiteral("enabled"), SettingScope::App,
            QMetaType::fromType<bool>(), false, false, {}}));
        QVERIFY(model.importValues(*values));
        auto store = std::make_shared<MemoryStateStore>();
        store->files.insert(QStringLiteral("legacy"), legacy);
        SettingsPersistence persistence;
        persistence.setPath(QStringLiteral("legacy"));
        persistence.setStore(store);
        QVERIFY(persistence.load(&model));
        QVERIFY(persistence.save(&model));
        const QJsonObject migrated = QJsonDocument::fromJson(store->files.value(QStringLiteral("legacy")))
                                         .object();
        QCOMPARE(migrated.value(QStringLiteral("version")).toInt(), 2);
        QCOMPARE(migrated.value(QStringLiteral("extensions")).toObject(), extensions);
        QVERIFY(migrated.value(QStringLiteral("values")).toObject().contains(QStringLiteral("app:count")));
    }

    void workspaceV1MigrationUnknownFieldsAndMonitorNormalization()
    {
        WorkspaceSession workspace;
        const QString doc = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString view = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QVariantList screens{QVariantMap{{QStringLiteral("id"), QStringLiteral("primary")},
            {QStringLiteral("availableRect"), rect(0, 0, 1280, 800)}}};
        QVERIFY(workspace.setState(workspaceState(doc, view, QStringLiteral("main"), {}), screens));
        const QVariantMap window = workspace.windows().first().toMap();
        QCOMPARE(window.value(QStringLiteral("screenId")).toString(), QStringLiteral("primary"));
        QCOMPARE(window.value(QStringLiteral("geometry")).toMap(), rect(0, 100, 900, 700));
        QCOMPARE(workspace.state().value(QStringLiteral("version")).toInt(), 2);
        QCOMPARE(workspace.state().value(QStringLiteral("vendorField")).toString(), QStringLiteral("keep-me"));
        const auto decoded = WorkspaceSession::decode(WorkspaceSession::encode(workspace.state()));
        QVERIFY(decoded.has_value());
        QCOMPARE(decoded->value(QStringLiteral("vendorField")).toString(), QStringLiteral("keep-me"));
    }

    void invalidWorkspaceRefusalIsAtomic()
    {
        WorkspaceSession workspace;
        const QString doc = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString view = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QVariantList screens{QVariantMap{{QStringLiteral("id"), QStringLiteral("primary")},
            {QStringLiteral("availableRect"), rect(0, 0, 1280, 800)}}};
        QVERIFY(workspace.setState(workspaceState(doc, view, QStringLiteral("main"), {}), screens));
        const QVariantMap before = workspace.state();
        QVariantMap invalid = before;
        QVariantList windows = invalid.value(QStringLiteral("windows")).toList();
        QVariantMap broken = windows.first().toMap();
        broken.insert(QStringLiteral("geometry"), rect(0, 0, -1, 100));
        windows[0] = broken;
        invalid.insert(QStringLiteral("windows"), windows);
        QVERIFY(!workspace.setState(invalid, screens));
        QCOMPARE(workspace.state(), before);
        QVERIFY(!workspace.lastError().isEmpty());
    }

    void restoreStableMultiViewIdsAndUnavailableDocumentWithoutMutation()
    {
        DocumentManager documents;
        DocumentSession *existing = documents.createReferenceDocument();
        QVERIFY(existing);
        const EntityId entity = EntityId::create();
        QVERIFY(existing->document()->defineProperty({QStringLiteral("label"),
            QMetaType::fromType<QString>(), QStringLiteral("before"), {}, true, false}));
        QVERIFY(existing->document()->addEntity(entity));
        const quint64 revision = existing->document()->revision().value;
        const QString existingId = existing->documentId();
        const QString missingId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString firstView = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString secondView = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString window = QStringLiteral("main");
        QVariantMap state = workspaceState(missingId, firstView, window,
                                           QStringLiteral("/no/such/document.qtk"));
        state.insert(QStringLiteral("documents"), QVariantList{QVariantMap{
            {QStringLiteral("documentId"), existingId}, {QStringLiteral("uri"), QString{}}},
            QVariantMap{{QStringLiteral("documentId"), missingId},
                        {QStringLiteral("uri"), QStringLiteral("/no/such/document.qtk")}}});
        state.insert(QStringLiteral("views"), QVariantList{
            QVariantMap{{QStringLiteral("viewId"), firstView}, {QStringLiteral("documentId"), existingId},
                        {QStringLiteral("windowId"), window}},
            QVariantMap{{QStringLiteral("viewId"), secondView}, {QStringLiteral("documentId"), existingId},
                        {QStringLiteral("windowId"), window}}});
        state.insert(QStringLiteral("activeDocumentId"), existingId);
        WorkspaceSession workspace;
        QVERIFY(workspace.setState(state));
        DockModel dock;
        QVERIFY(workspace.restore(&documents, &dock));
        QCOMPARE(documents.viewsFor(existingId), QStringList({firstView, secondView}));
        QCOMPARE(documents.sessions().size(), 1);
        QCOMPARE(workspace.unavailableDocuments(), QStringList({missingId}));
        QCOMPARE(existing->document()->revision().value, revision);
        QCOMPARE(existing->document()->value(entity, QStringLiteral("label")).toString(),
                 QStringLiteral("before"));
    }
};

QTEST_GUILESS_MAIN(TstWorkspaceSession)
#include "tst_workspace_session.moc"
