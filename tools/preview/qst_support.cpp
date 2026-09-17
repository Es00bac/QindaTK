// SPDX-License-Identifier: LGPL-3.0-or-later
#include "qst_support.h"

#include <QQmlEngine>

#ifdef QINDATK_HAVE_QINDAQT
#include "qindaqt/design_tokens/accessibility_inputs.h"
#include "qindaqt/design_tokens/token_facade.h"
#include "qindaqt/themes/theme_loader.h"
#include "qindaqt/themes/theme_spec.h"

#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QQmlComponent>
#include <QStandardPaths>
#include <QTimer>

#include <memory>
#endif

namespace QindaTK::PreviewQst {

#ifdef QINDATK_HAVE_QINDAQT

namespace {

QString themeDirectory()
{
    QString directory = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                               QStringLiteral("qindaqt/themes"),
                                               QStandardPaths::LocateDirectory);
    return directory.isEmpty() ? QStringLiteral("/usr/share/qindaqt/themes") : directory;
}

// Naming the Tokens singleton from an inline component loads the installed
// QML module (and its backing library) so singletonInstance() can find it
// — the same trick QindaStudio and the desktop's Welcome app use.
QindaQt::DesignTokens::TokenFacade *facadeFor(QQmlEngine &engine, QString *error)
{
    QQmlComponent registration(&engine);
    registration.setData("import QtQuick\nimport QindaQt.Tokens 1.0\nQtObject { property int r: Tokens.qstRevision }\n",
                         QUrl(QStringLiteral("inline:qtk-preview-token-registration.qml")));
    // AGENT-GUARD: the module's plugin may resolve on the type-loader thread,
    // leaving the component Loading for a moment; creating it then fails
    // with "Component is not ready". Wait, bounded, like QindaStudio does.
    if (registration.status() == QQmlComponent::Loading) {
        QEventLoop loop;
        QTimer deadline;
        deadline.setSingleShot(true);
        QObject::connect(&registration, &QQmlComponent::statusChanged, &loop,
                         [&loop](QQmlComponent::Status status) {
                             if (status != QQmlComponent::Loading) loop.quit();
                         });
        QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
        deadline.start(5000);
        loop.exec();
    }
    if (!registration.isReady()) {
        if (error) *error = registration.errorString().trimmed();
        return nullptr;
    }
    std::unique_ptr<QObject> object(registration.create());
    if (!object) {
        if (error) *error = registration.errorString().trimmed();
        return nullptr;
    }
    auto *facade = engine.singletonInstance<QindaQt::DesignTokens::TokenFacade *>(
        QStringLiteral("QindaQt.Tokens"), QStringLiteral("Tokens"));
    if (facade == nullptr && error) {
        *error = QStringLiteral("QindaQt.Tokens singleton was not registered");
    }
    return facade;
}

} // namespace

bool available()
{
    return true;
}

QStringList themeIds()
{
    QStringList ids;
    for (const auto &result : QindaQt::Themes::ThemeLoader::fromDirectory(themeDirectory())) {
        if (result.ok) {
            ids.append(result.theme.id);
        }
    }
    ids.sort();
    return ids;
}

bool publish(QQmlEngine &engine, const QString &themeIdOrPath, double textScale, QString *error)
{
    QString path = themeIdOrPath;
    if (!path.endsWith(QLatin1String(".json"))) {
        path = QDir(themeDirectory()).filePath(themeIdOrPath + QStringLiteral(".json"));
    }
    if (!QFileInfo::exists(path)) {
        if (error) *error = QStringLiteral("no such QindaQt theme: %1 (known: %2)")
                                .arg(themeIdOrPath, themeIds().join(QLatin1String(", ")));
        return false;
    }
    const auto loaded = QindaQt::Themes::ThemeLoader::fromFile(path);
    if (!loaded.ok) {
        if (error) *error = loaded.error;
        return false;
    }
    QindaQt::DesignTokens::TokenFacade *facade = facadeFor(engine, error);
    if (facade == nullptr) {
        return false;
    }
    QindaQt::DesignTokens::AccessibilityInputs inputs;
    inputs.textScale = textScale;
    return facade->publish(loaded.theme, inputs, error);
}

#else

bool available()
{
    return false;
}

QStringList themeIds()
{
    return {};
}

bool publish(QQmlEngine &, const QString &, double, QString *error)
{
    if (error) {
        *error = QStringLiteral("qtk-preview was built without the QindaQt desktop libraries");
    }
    return false;
}

#endif

} // namespace QindaTK::PreviewQst
