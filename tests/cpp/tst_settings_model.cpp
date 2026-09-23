// SPDX-License-Identifier: LGPL-3.0-or-later
#include "settings_model.h"

#include <QtTest>

using namespace QindaTK;

class TstSettingsModel final : public QObject {
    Q_OBJECT
private:
    static SettingDescriptor textSetting(
        const QString &key, SettingScope scope, const QString &initial,
        bool restart = false,
        std::function<OperationError(const QVariant &)> validator = {})
    {
        return {key, scope, QMetaType::fromType<QString>(), initial, restart,
                std::move(validator)};
    }

    static SettingDescriptor intSetting(const QString &key, int initial,
                                        SettingScope scope = SettingScope::App,
                                        bool restart = false)
    {
        return {key, scope, QMetaType::fromType<int>(), initial, restart, {}};
    }

private slots:
    void stagedValuesApplyOrCancel()
    {
        SettingsModel model;
        QVERIFY(model.registerSetting(textSetting(QStringLiteral("appearance.theme"),
                                                  SettingScope::App, QStringLiteral("dark"))));
        QVERIFY(model.beginStage());
        QVERIFY(model.staging());
        QVERIFY(model.setStagedValue(QStringLiteral("appearance.theme"),
                                    QStringLiteral("light")));
        QCOMPARE(model.value(QStringLiteral("appearance.theme")).toString(), QStringLiteral("dark"));
        QCOMPARE(model.stagedValue(QStringLiteral("appearance.theme")).toString(), QStringLiteral("light"));
        model.cancel();
        QVERIFY(!model.staging());
        QCOMPARE(model.value(QStringLiteral("appearance.theme")).toString(), QStringLiteral("dark"));

        QVERIFY(model.beginStage());
        QVERIFY(model.setStagedValue(QStringLiteral("appearance.theme"), QStringLiteral("light")));
        QSignalSpy applied(&model, &SettingsModel::applied);
        QVERIFY(model.apply());
        QVERIFY(!model.staging());
        QCOMPARE(model.value(QStringLiteral("appearance.theme")).toString(), QStringLiteral("light"));
        QCOMPARE(applied.size(), 1);
    }

    void scopesAreIndependentAndRequireInstanceIds()
    {
        SettingsModel model;
        QVERIFY(model.registerSetting(intSetting(QStringLiteral("zoom"), 100,
                                                 SettingScope::Document), QStringLiteral("doc-a")));
        QVERIFY(model.registerSetting(intSetting(QStringLiteral("zoom"), 100,
                                                 SettingScope::Document), QStringLiteral("doc-b")));
        QVERIFY(!model.registerSetting(intSetting(QStringLiteral("missing-instance"), 1,
                                                  SettingScope::Workspace)));
        QVERIFY(!model.registerSetting(intSetting(QStringLiteral("app-instance"), 1),
                                       QStringLiteral("not-allowed")));
        QVERIFY(model.beginStage());
        QVERIFY(model.setStagedValue(QStringLiteral("zoom"), 125, SettingScope::Document,
                                    QStringLiteral("doc-a")));
        QVERIFY(model.apply());
        QCOMPARE(model.value(QStringLiteral("zoom"), SettingScope::Document,
                             QStringLiteral("doc-a")).toInt(), 125);
        QCOMPARE(model.value(QStringLiteral("zoom"), SettingScope::Document,
                             QStringLiteral("doc-b")).toInt(), 100);
        QCOMPARE(model.exportValues().value(QStringLiteral("document:doc-a:zoom")).toInt(), 125);
        QVERIFY(model.resetScope(SettingScope::Document, QStringLiteral("doc-a")));
        QCOMPARE(model.value(QStringLiteral("zoom"), SettingScope::Document,
                             QStringLiteral("doc-a")).toInt(), 100);
    }

    void defaultsResetIndividuallyOrByScope()
    {
        SettingsModel model;
        QVERIFY(model.registerSetting(intSetting(QStringLiteral("one"), 1)));
        QVERIFY(model.registerSetting(intSetting(QStringLiteral("two"), 2)));
        QVERIFY(model.beginStage());
        QVERIFY(model.setStagedValue(QStringLiteral("one"), 10));
        QVERIFY(model.setStagedValue(QStringLiteral("two"), 20));
        QVERIFY(model.apply());
        QVERIFY(model.resetKey(QStringLiteral("one")));
        QCOMPARE(model.value(QStringLiteral("one")).toInt(), 1);
        QCOMPARE(model.value(QStringLiteral("two")).toInt(), 20);
        QVERIFY(model.resetScope(SettingScope::App));
        QCOMPARE(model.value(QStringLiteral("two")).toInt(), 2);
    }

    void validatorRejectsInvalidValuesWithoutChangingCommittedValue()
    {
        SettingsModel model;
        const auto validateName = [](const QVariant &value) {
            if (value.toString().trimmed().isEmpty())
                return OperationError{ErrorCode::Validation,
                    QStringLiteral("Name cannot be empty"), QStringLiteral("profile.name"), {}};
            return OperationError{};
        };
        QVERIFY(model.registerSetting(textSetting(QStringLiteral("profile.name"),
            SettingScope::App, QStringLiteral("Studio"), false, validateName)));
        QVERIFY(model.beginStage());
        QSignalSpy invalid(&model, &SettingsModel::validationFailed);
        QVERIFY(!model.setStagedValue(QStringLiteral("profile.name"), QStringLiteral("  ")));
        QCOMPARE(invalid.size(), 1);
        QVERIFY(!model.valid());
        QCOMPARE(model.value(QStringLiteral("profile.name")).toString(), QStringLiteral("Studio"));
        QVERIFY(model.apply());
        QCOMPARE(model.value(QStringLiteral("profile.name")).toString(), QStringLiteral("Studio"));
    }

    void restartRequiredTracksAppliedNonDefaultValues()
    {
        SettingsModel model;
        QVERIFY(model.registerSetting(intSetting(QStringLiteral("engine.threads"), 8,
                                                 SettingScope::App, true)));
        QVERIFY(!model.needsRestart());
        QVERIFY(model.beginStage());
        QVERIFY(model.setStagedValue(QStringLiteral("engine.threads"), 12));
        QVERIFY(!model.needsRestart());
        QVERIFY(model.apply());
        QVERIFY(model.needsRestart());
        QVERIFY(model.resetKey(QStringLiteral("engine.threads")));
        QVERIFY(!model.needsRestart());
    }

    void unknownKeysAreReportedAndRoundTripAsOrphans()
    {
        SettingsModel model;
        QVERIFY(model.registerSetting(textSetting(QStringLiteral("ui.language"),
                                                  SettingScope::App, QStringLiteral("en"))));
        const QVariantMap input{
            {QStringLiteral("app:ui.language"), QStringLiteral("fr")},
            {QStringLiteral("app:future.setting"), 23},
            {QStringLiteral("vendor:extension"), QByteArray("opaque")}};
        QSignalSpy unsupported(&model, &SettingsModel::unsupportedKeysImported);
        QVERIFY(model.importValues(input));
        QCOMPARE(model.value(QStringLiteral("ui.language")).toString(), QStringLiteral("fr"));
        QCOMPARE(model.orphanedValues().size(), 2);
        QCOMPARE(unsupported.size(), 1);
        QCOMPARE(unsupported.first().first().toStringList(),
                 (QStringList{QStringLiteral("app:future.setting"), QStringLiteral("vendor:extension")}));
        QCOMPARE(model.exportValues(), input);

        SettingsModel nextVersion;
        QVERIFY(nextVersion.registerSetting(textSetting(QStringLiteral("ui.language"),
            SettingScope::App, QStringLiteral("en"))));
        QVERIFY(nextVersion.importValues(model.exportValues()));
        QCOMPARE(nextVersion.orphanedValues(), model.orphanedValues());
        QCOMPARE(nextVersion.exportValues(), input);
    }

    void failedImportIsAtomic()
    {
        SettingsModel model;
        QVERIFY(model.registerSetting(intSetting(QStringLiteral("audio.buffer"), 256)));
        QVERIFY(model.importValues({{QStringLiteral("app:audio.buffer"), 512},
                                   {QStringLiteral("app:unknown"), QStringLiteral("keep")}}));
        const auto before = model.exportValues();
        QVERIFY(!model.importValues({{QStringLiteral("app:audio.buffer"), QStringLiteral("wrong type")},
                                    {QStringLiteral("app:new-unknown"), 9}}));
        QCOMPARE(model.exportValues(), before);
        QCOMPARE(model.value(QStringLiteral("audio.buffer")).toInt(), 512);
        QCOMPARE(model.orphanedValues().value(QStringLiteral("app:unknown")).toString(),
                 QStringLiteral("keep"));
    }
};

QTEST_GUILESS_MAIN(TstSettingsModel)
#include "tst_settings_model.moc"
