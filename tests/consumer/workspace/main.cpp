// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QTemporaryDir>
#include <QUrl>
#include <qindatk/settings_model.h>
#include <qindatk/settings_persistence.h>
#include <qindatk/workspace_session.h>

using namespace QindaTK;

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    const SettingDescriptor descriptor{QStringLiteral("zoom"), SettingScope::App,
        QMetaType::fromType<int>(), 100, false, {}};
    SettingsModel settings;
    if (!settings.registerSetting(descriptor)
        || !settings.importValues({{QStringLiteral("app:zoom"), 125}})) return 2;
    SettingsPersistence preferences;
    preferences.setPath(directory.filePath(QStringLiteral("settings.json")));
    if (!preferences.save(&settings)) return 3;
    SettingsModel restored;
    if (!restored.registerSetting(descriptor) || !preferences.load(&restored)
        || restored.value(QStringLiteral("zoom")).toInt() != 125) return 4;
    WorkspaceSession workspace;
    workspace.setPath(directory.filePath(QStringLiteral("workspace.json")));
    if (!workspace.save() || !workspace.load() || !workspace.windows().isEmpty()) return 5;
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral(QINDATK_QML_IMPORT_ROOT));
    engine.load(QUrl::fromLocalFile(QStringLiteral(CONSUMER_QML_FILE)));
    if (engine.rootObjects().size() != 1
        || !engine.rootObjects().first()->findChild<SettingsModel *>(QStringLiteral("installedSettings"))
        || !engine.rootObjects().first()->findChild<WorkspaceSession *>(QStringLiteral("installedWorkspace")))
        return 6;
    return 0;
}
