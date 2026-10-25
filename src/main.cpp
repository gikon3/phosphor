#include <QApplication>
#include <QCommandLineParser>
#include <QString>
#include <QtLogging>
#include <cstdlib>
#include <exception>
#include <filesystem>

#include "app/main_window.hpp"
#include "core/error.hpp"
#include "core/renderer.hpp"
#include "core/session.hpp"
#include "core/store_cache.hpp"
#include "core/view.hpp"
#include "phosphor/config.hpp"
#include "phosphor/version.hpp"

namespace {

std::filesystem::path toPath(const QString& s) {
    return std::filesystem::path{s.toStdU16String()};
}

int run(int& argc, char** argv) {
    // Имена задают место настроек приложения.
    const auto name = QString::fromUtf8(Phosphor::kName.data(), Phosphor::kName.size());
    QApplication::setOrganizationName(name);
    QApplication::setApplicationName(name);
    const QApplication app{argc, argv};
    QApplication::setApplicationVersion(
            QString::fromUtf8(Phosphor::Version::kString.data(), Phosphor::Version::kString.size()));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("dump"), QStringLiteral("Dump files to open."),
            QStringLiteral("[dump...]"));
    parser.process(app);

    // Окно объявлено последним: оно ссылается на сессию, вид и рендерер и разрушается первым.
    Phosphor::Session session{Phosphor::StoreCache{Phosphor::defaultCacheDirectory() / "stores"}};
    Phosphor::View view;
    Phosphor::Renderer renderer;
    Phosphor::MainWindow window{session, view, renderer};
    window.show();

    for (const auto& dump : parser.positionalArguments())
        session.openDump(toPath(dump));
    return QApplication::exec();
}

}  // namespace

int main(int argc, char* argv[]) {
    // Исключение, дошедшее до main, становится сообщением и кодом ошибки, а не std::terminate.
    try {
        return run(argc, argv);
    }
    catch (const std::exception& e) {
        qCritical("%.*s: %s", static_cast<int>(Phosphor::kName.size()), Phosphor::kName.data(),
                qUtf8Printable(Phosphor::errorText(e)));
    }
    catch (...) {
        qCritical("%.*s: unknown error", static_cast<int>(Phosphor::kName.size()), Phosphor::kName.data());
    }
    return EXIT_FAILURE;
}
