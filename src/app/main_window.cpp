#include "app/main_window.hpp"

namespace Phosphor {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Phosphor"));
}

}  // namespace Phosphor
