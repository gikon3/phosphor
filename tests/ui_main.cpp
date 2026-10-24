#include <gtest/gtest.h>

#include <QApplication>
#include <QtGlobal>

int main(int argc, char* argv[]) {
    // Без заданной платформы Qt на хосте без дисплея аварийно завершается ещё при создании QApplication.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    const QApplication app{argc, argv};
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
