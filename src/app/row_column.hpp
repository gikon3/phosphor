#pragma once

#include <QPoint>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <Qt>
#include <cstddef>
#include <optional>
#include <vector>

class QContextMenuEvent;
class QMouseEvent;
class QPaintEvent;
class QWheelEvent;

namespace Phosphor {

// Колонка текстов по строкам вида. Заголовок высотой с линейку холста, строки — высотой строки
// холста, поэтому тексты идут вровень с волнами. Колонка только рисует и сообщает о мыши
// сигналами: выделение, порядок строк и прокрутку решает владелец. Координаты в сигналах — в
// системе колонки.
class RowColumn : public QWidget {
    Q_OBJECT

public:
    RowColumn(QString header, Qt::TextElideMode elide, QWidget* parent = nullptr);

    void setTexts(QStringList texts);
    // Пустая строка для строки вне колонки.
    [[nodiscard]] QString text(std::size_t row) const;
    void setSelection(std::vector<bool> selected);
    void setFirstRow(int row);
    // Линия перед строкой row; nullopt убирает её.
    void setInsertMark(std::optional<std::size_t> row);

Q_SIGNALS:
    void leftPressed(QPoint pos, Qt::KeyboardModifiers modifiers);
    // Движение мыши с нажатой кнопкой.
    void mouseDragged(QPoint pos);
    void leftReleased(QPoint pos);
    void contextMenuRequested(QPoint pos, QPoint globalPos);
    // Колесо без Ctrl и Shift. Событие действительно только на время испускания сигнала.
    void verticalWheel(QWheelEvent* event);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    QString header_;
    Qt::TextElideMode elide_;
    QStringList texts_;
    std::vector<bool> selected_;
    int firstRow_ = 0;
    std::optional<std::size_t> insertMark_;
};

}  // namespace Phosphor
