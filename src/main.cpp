#include <QApplication>
#include <QCommandLineParser>
#include <QString>

#include "app/main_window.hpp"
#include "phosphor/version.hpp"

int main(int argc, char* argv[]) {
    const QApplication app{argc, argv};
    QApplication::setApplicationName(QStringLiteral("phosphor"));
    QApplication::setApplicationVersion(
            QString::fromUtf8(Phosphor::Version::kString.data(), Phosphor::Version::kString.size()));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("dump"), QStringLiteral("Dump file to open."));
    parser.process(app);

    Phosphor::MainWindow window;
    window.show();
    return QApplication::exec();
}
