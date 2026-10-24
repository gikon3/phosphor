#pragma once

#include <QMainWindow>

namespace Phosphor {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
};

}  // namespace Phosphor
