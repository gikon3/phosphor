#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QtGlobal>

#include "phosphor/config.hpp"

int main(int argc, char* argv[]) {
    // Без заданной платформы Qt на хосте без дисплея аварийно завершается ещё при создании QApplication.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    // Виджеты читают и пишут настройки и стандартные каталоги пользователя. Тесты не должны ни
    // зависеть от них, ни портить их, поэтому пути уводятся в тестовый режим и во временный каталог.
    // Родной формат на Windows — реестр, путь к нему не задаётся, поэтому настройки по умолчанию
    // на всех платформах — INI-файл.
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QString::fromUtf8(Phosphor::kName.data(), Phosphor::kName.size()));
    QCoreApplication::setApplicationName(QStringLiteral("phosphor-ui-tests"));
    const QTemporaryDir settingsDir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    const QApplication app{argc, argv};
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
