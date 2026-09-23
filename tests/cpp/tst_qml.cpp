// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QQmlEngine>
#include <QQmlContext>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtQuickTest/quicktest.h>
#include "property_binding.h"
#include "settings_model.h"
#include "shortcut_map.h"

// Test-only authority for QML editor tests. Production models intentionally
// keep schema/entity creation in C++; no fixture mutation API is shipped.
class InspectorFixture final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QindaTK::PropertyBinding *unitBinding READ unitBinding CONSTANT)
    Q_PROPERTY(QindaTK::PropertyBinding *vectorBinding READ vectorBinding CONSTANT)
    Q_PROPERTY(QindaTK::PropertyBinding *instantBinding READ instantBinding CONSTANT)
    Q_PROPERTY(QindaTK::PropertyBinding *dateBinding READ dateBinding CONSTANT)
    Q_PROPERTY(QindaTK::PropertyBinding *featureBinding READ featureBinding CONSTANT)
    Q_PROPERTY(QindaTK::UndoService *undoService READ undoService CONSTANT)
    Q_PROPERTY(QindaTK::PropertyDocument *document READ document CONSTANT)
    Q_PROPERTY(QString firstId READ firstId CONSTANT)
    Q_PROPERTY(QString secondId READ secondId CONSTANT)
    Q_PROPERTY(bool ready READ ready CONSTANT)
public:
    explicit InspectorFixture(QObject *parent = nullptr) : QObject(parent)
    {
        m_ready = m_document.defineProperty({QStringLiteral("length"),
            QMetaType::fromType<double>(), 0.0, QStringLiteral("mm"), true, false})
            && m_document.defineProperty({QStringLiteral("position"),
            QMetaType::fromType<QVariantList>(), QVariantList{0.0, 0.0, 0.0},
            QStringLiteral("px"), true, false})
            && m_document.defineProperty({QStringLiteral("instant"),
            QMetaType::fromType<QString>(), QString{}, QStringLiteral("utc"), true, false})
            && m_document.defineProperty({QStringLiteral("allDay"),
            QMetaType::fromType<QString>(), QString{}, QStringLiteral("date"), true, false})
            && m_document.defineProperty({QStringLiteral("liga"),
            QMetaType::fromType<bool>(), false, QString{}, true, false})
            && m_document.addEntity(m_first, {{QStringLiteral("length"), 25.4},
                {QStringLiteral("position"), QVariantList{1.0, 2.0, 3.0}},
                {QStringLiteral("instant"), QStringLiteral("2024-11-03T04:00:00.000Z")},
                {QStringLiteral("allDay"), QStringLiteral("2024-02-29")},
                {QStringLiteral("liga"), false}})
            && m_document.addEntity(m_second, {{QStringLiteral("length"), 50.8},
                {QStringLiteral("position"), QVariantList{4.0, 5.0, 6.0}},
                {QStringLiteral("instant"), QStringLiteral("2024-11-03T04:00:00.000Z")},
                {QStringLiteral("allDay"), QStringLiteral("2024-02-29")},
                {QStringLiteral("liga"), true}})
            && m_undo.addDocument(&m_document);
        if (!m_ready) return;
        for (auto *binding : {&m_unitBinding, &m_vectorBinding, &m_featureBinding}) {
            binding->setDocument(&m_document);
            binding->setUndoService(&m_undo);
            binding->setTargetIds({firstId(), secondId()});
        }
        m_unitBinding.setPropertyId(QStringLiteral("length"));
        m_vectorBinding.setPropertyId(QStringLiteral("position"));
        m_featureBinding.setPropertyId(QStringLiteral("liga"));
        for (auto *binding : {&m_instantBinding, &m_dateBinding}) {
            binding->setDocument(&m_document);
            binding->setUndoService(&m_undo);
            binding->setTargetIds({firstId()});
        }
        m_instantBinding.setPropertyId(QStringLiteral("instant"));
        m_dateBinding.setPropertyId(QStringLiteral("allDay"));
    }
    QindaTK::PropertyBinding *unitBinding() { return &m_unitBinding; }
    QindaTK::PropertyBinding *vectorBinding() { return &m_vectorBinding; }
    QindaTK::PropertyBinding *instantBinding() { return &m_instantBinding; }
    QindaTK::PropertyBinding *dateBinding() { return &m_dateBinding; }
    QindaTK::PropertyBinding *featureBinding() { return &m_featureBinding; }
    QindaTK::UndoService *undoService() { return &m_undo; }
    QindaTK::PropertyDocument *document() { return &m_document; }
    QString firstId() const { return m_first.toString(); }
    QString secondId() const { return m_second.toString(); }
    bool ready() const { return m_ready; }
private:
    QindaTK::PropertyDocument m_document;
    QindaTK::UndoService m_undo;
    QindaTK::PropertyBinding m_unitBinding;
    QindaTK::PropertyBinding m_vectorBinding;
    QindaTK::PropertyBinding m_instantBinding;
    QindaTK::PropertyBinding m_dateBinding;
    QindaTK::PropertyBinding m_featureBinding;
    QindaTK::EntityId m_first = QindaTK::EntityId::create();
    QindaTK::EntityId m_second = QindaTK::EntityId::create();
    bool m_ready = false;
};

class InspectorFixtures final : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE QObject *create()
    {
        auto *fixture = new InspectorFixture;
        QQmlEngine::setObjectOwnership(fixture, QQmlEngine::JavaScriptOwnership);
        return fixture;
    }
};

// Test-only production-service fixture for preferences and shortcut controls.
// Each create() call gets fresh C++ descriptors and active command handlers.
class PreferencesFixture final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QindaTK::SettingsModel *settingsModel READ settingsModel CONSTANT)
    Q_PROPERTY(QindaTK::ShortcutMap *shortcutMap READ shortcutMap CONSTANT)
    Q_PROPERTY(QindaTK::CommandRegistry *commandRegistry READ commandRegistry CONSTANT)
    Q_PROPERTY(bool ready READ ready CONSTANT)
public:
    explicit PreferencesFixture(QObject *parent = nullptr) : QObject(parent)
    {
        QindaTK::SettingDescriptor theme{QStringLiteral("appearance.theme"),
            QindaTK::SettingScope::App, QMetaType::fromType<QString>(),
            QStringLiteral("dark"), false, {}, QStringLiteral("Theme"),
            QStringLiteral("Appearance"), QStringLiteral("Application colour theme")};
        QindaTK::SettingDescriptor buffer{QStringLiteral("audio.buffer"),
            QindaTK::SettingScope::App, QMetaType::fromType<int>(), 256,
            true, {}, QStringLiteral("Buffer size"), QStringLiteral("Audio"),
            QStringLiteral("Audio callback buffer in frames")};
        QindaTK::SettingDescriptor shortcuts{QStringLiteral("shortcuts.overrides"),
            QindaTK::SettingScope::App, QMetaType::fromType<QString>(),
            QStringLiteral("{}"), false,
            [](const QVariant &value) {
                const auto parsed = QJsonDocument::fromJson(value.toString().toUtf8());
                if (!parsed.isObject())
                    return QindaTK::OperationError{QindaTK::ErrorCode::Validation,
                        QStringLiteral("Shortcut overrides must be a JSON object"),
                        QStringLiteral("shortcuts.overrides"), {}};
                const auto object = parsed.object();
                for (const auto &key : object.keys()) {
                    if (!object.value(key).isString())
                        return QindaTK::OperationError{QindaTK::ErrorCode::Validation,
                            QStringLiteral("Shortcut overrides must be strings"),
                            QStringLiteral("shortcuts.overrides"), {}};
                }
                return QindaTK::OperationError{};
            }, QStringLiteral("Shortcut overrides"), QStringLiteral("Keyboard"),
            QStringLiteral("Portable shortcut bindings")};
        m_ready = m_settings.registerSetting(theme) && m_settings.registerSetting(buffer)
            && m_settings.registerSetting(shortcuts);
        m_registry.setContext(&m_context);
        const auto scope = m_context.pushScope(QStringLiteral("application"), this);
        m_ready = m_ready && !scope.isEmpty() && m_context.activate(scope);
        for (const auto &descriptor : {
                 QindaTK::CommandDescriptor{QStringLiteral("file.save"),
                    QStringLiteral("Save"), {}, QStringLiteral("File"), {},
                    QStringLiteral("Ctrl+S"), false},
                 QindaTK::CommandDescriptor{QStringLiteral("edit.find"),
                    QStringLiteral("Find"), {}, QStringLiteral("Edit"), {},
                    QStringLiteral("Ctrl+F"), false}}) {
            m_ready = m_ready && m_registry.registerCommand(descriptor);
            m_ready = m_ready && m_registry.registerHandler(scope, descriptor.id, this,
                [](const QindaTK::CommandInvocationContext &, const QVariantMap &) {
                    return QindaTK::OperationError{};
                });
        }
        m_shortcuts.setRegistry(&m_registry);
        m_ready = m_ready && m_shortcuts.bindCommand(QStringLiteral("file.save"))
            && m_shortcuts.bindCommand(QStringLiteral("edit.find"));
    }
    QindaTK::SettingsModel *settingsModel() { return &m_settings; }
    QindaTK::ShortcutMap *shortcutMap() { return &m_shortcuts; }
    QindaTK::CommandRegistry *commandRegistry() { return &m_registry; }
    bool ready() const { return m_ready; }
private:
    QindaTK::SettingsModel m_settings;
    QindaTK::CommandContext m_context;
    QindaTK::CommandRegistry m_registry;
    QindaTK::ShortcutMap m_shortcuts;
    bool m_ready = false;
};

class PreferencesFixtures final : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE QObject *create()
    {
        auto *fixture = new PreferencesFixture;
        QQmlEngine::setObjectOwnership(fixture, QQmlEngine::JavaScriptOwnership);
        return fixture;
    }
};

// Adds the build-tree import path before any test file loads.
class Setup final : public QObject {
    Q_OBJECT
public slots:
    void qmlEngineAvailable(QQmlEngine *engine)
    {
        engine->addImportPath(QStringLiteral(QINDATK_BUILD_QML_DIR));
        engine->rootContext()->setContextProperty(QStringLiteral("inspectorFixtures"),
                                                   new InspectorFixtures(engine));
        engine->rootContext()->setContextProperty(QStringLiteral("preferencesFixtures"),
                                                   new PreferencesFixtures(engine));
    }
};

QUICK_TEST_MAIN_WITH_SETUP(qindatk_qml, Setup)

#include "tst_qml.moc"
