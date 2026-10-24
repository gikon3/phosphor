#include <gtest/gtest.h>

#include <QApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtGlobal>

int main(int argc, char* argv[]) {
    // Без заданной платформы Qt на хосте без дисплея аварийно завершается ещё при создании QApplication.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    // Виджеты читают и пишут настройки и стандартные каталоги пользователя. Тесты не должны ни
    // зависеть от них, ни портить их, поэтому пути уводятся в тестовый режим и во временный каталог.
    QStandardPaths::setTestModeEnabled(true);
    const QTemporaryDir settingsDir;
#ifndef Q_OS_WIN
    // На Windows родной формат — реестр, и путь к файлу у него не задаётся.
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDir.path());
#endif
    const QApplication app{argc, argv};
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
