#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QStandardPaths>
#include <QString>
#include <QtLogging>
#include <cstdlib>
#include <exception>
#include <filesystem>

#include "app/main_window.hpp"
#include "core/renderer.hpp"
#include "core/session.hpp"
#include "core/store_cache.hpp"
#include "core/view.hpp"
#include "phosphor/version.hpp"

namespace {

std::filesystem::path toPath(const QString& s) {
    return std::filesystem::path{s.toStdU16String()};
}

// Без каталога кэша индексы дампов легли бы в текущий каталог процесса.
std::filesystem::path cacheDirectory() {
    const auto location = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (!location.isEmpty())
        return toPath(location);
    const auto fallback = QDir::tempPath() + QStringLiteral("/phosphor");
    qWarning("No writable cache location, using %s", qPrintable(fallback));
    return toPath(fallback);
}

int run(int& argc, char** argv) {
    const QApplication app{argc, argv};
    QApplication::setApplicationName(QStringLiteral("phosphor"));
    QApplication::setApplicationVersion(
            QString::fromUtf8(Phosphor::Version::kString.data(), Phosphor::Version::kString.size()));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("dump"), QStringLiteral("Dump files to open."),
            QStringLiteral("[dump...]"));
    parser.process(app);

    // Окно объявлено последним: оно ссылается на сессию, вид и рендерер и разрушается первым.
    Phosphor::Session session{Phosphor::StoreCache{cacheDirectory() / "stores"}};
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
        qCritical("phosphor: %s", e.what());
    }
    catch (...) {
        qCritical("phosphor: unknown error");
    }
    return EXIT_FAILURE;
}
