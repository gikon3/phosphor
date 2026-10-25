#pragma once

#include <QPoint>
#include <QString>
#include <QWidget>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "core/view.hpp"
#include "wasafe/core/ids.hpp"

class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QScrollBar;
class QSplitter;
class QWheelEvent;

namespace Phosphor {

class Renderer;
class RowColumn;
class Session;
class WaveCanvas;
struct FrameResult;

// Панель волн: колонка имён, колонка значений под курсорами A/B и холст с линейкой времени;
// все три прокручиваются по строкам вместе. Панель запрашивает у рендерера кадры видимых строк
// и показывает пришедшие. Значения в потоке панели не читаются: строки разрешаются в NodeId
// только по иерархии источника, а всё остальное приходит в кадре. Сессия, вид и рендерер должны
// пережить панель.
class WavePanel : public QWidget {
    Q_OBJECT

public:
    WavePanel(Session& session, View& view, Renderer& renderer, QWidget* parent = nullptr);

    // Запрос кадра видимых строк под текущие окно, ширину холста и курсоры отправляется сразу.
    // Поколение каждого запроса больше предыдущего, поэтому пришедший кадр сопоставляется своему
    // запросу.
    void requestFrame();

    // Видимые тексты колонок; пустая строка для строки вне вида. Значение — только для строк,
    // данные которых есть в показанном кадре.
    [[nodiscard]] QString nameText(std::size_t row) const;
    [[nodiscard]] QString valueText(std::size_t row) const;

protected:
    // Принимаются только сигналы, перетаскиваемые в этом же процессе: номер источника
    // действителен лишь в своей сессии.
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    // Строки вида [first, last), хотя бы частично видимые на холсте.
    struct RowRange {
        std::size_t first = 0;
        std::size_t last = 0;
    };

private:
    void createActions();
    // Откладывает запрос до цикла событий: несколько изменений подряд дают один запрос. К моменту
    // построения запроса источник должен быть у рендерера; отложенная отправка обеспечивает это
    // при любом порядке подключения к сигналам сессии.
    void scheduleFrame();

    void handleRowsChanged();
    void handleSourcesChanged();
    void handleFrame(const FrameResult& frame);

    void resolveRows();
    [[nodiscard]] RowRange visibleRows() const;
    // Строка показанного кадра для каждой видимой строки вида; размер — число строк вида.
    [[nodiscard]] std::vector<std::optional<std::size_t>> rowMapping() const;
    void updateRowMapping();
    void updateNames();
    void updateValues();
    void updateVerticalScroll();
    // Колесо прокручивает строки так же, как над самой полосой прокрутки.
    void scrollRowsByWheel(QWheelEvent* event);
    void updateHorizontalScroll();
    void scrollHorizontally(int value);
    // Шаги полосы двигают окно на долю его ширины, а не на единицы полосы: на глубоком зуме
    // единица полосы шире окна.
    void stepHorizontally(int action);

    void zoomBy(double factor);
    void zoomFit();
    // Единственный путь, которым панель меняет окно: начало окна не уходит левее начала данных,
    // а окно, которое ушло бы, сдвигается вправо с прежней шириной. Без готовых источников окно
    // применяется как есть.
    void applyWindow(TimeWindow window);
    void removeSelectedRows();
    void selectOnly(std::optional<std::size_t> row);
    void updateSelection();

    // Мышь в колонке имён: выделение, перестановка строк и меню формата.
    void pressName(QPoint pos, Qt::KeyboardModifiers modifiers);
    void moveName(QPoint pos);
    void releaseName(QPoint pos);
    void showFormatMenu(QPoint pos, QPoint globalPos);

    // y — в координатах колонок и холста: у всех троих строки начинаются на одной высоте.
    [[nodiscard]] std::optional<std::size_t> rowAt(int y) const;
    // Позиция вставки перед строкой, в верхнюю половину которой попадает y; в [0, число строк].
    [[nodiscard]] std::size_t insertionRow(int y) const;
    [[nodiscard]] bool acceptsDrag(const QDropEvent& event) const;
    [[nodiscard]] int canvasY(QPoint panelPos) const;

private:
    Session* session_;
    View* view_;
    Renderer* renderer_;
    QSplitter* splitter_ = nullptr;
    RowColumn* names_ = nullptr;
    RowColumn* values_ = nullptr;
    WaveCanvas* canvas_ = nullptr;
    QScrollBar* vScroll_ = nullptr;
    QScrollBar* hScroll_ = nullptr;

    std::uint64_t generation_ = 0;
    bool frameScheduled_ = false;
    // По строке вида; невалидный NodeId — сигнала нет в иерархии источника.
    std::vector<WaSafe::NodeId> nodes_;
    // Строки запросов, кадры которых ещё не пришли: по ним видимые строки сопоставляются
    // строкам кадра, даже если вид успел измениться.
    std::map<std::uint64_t, std::vector<Row>> requested_;
    std::vector<Row> frameRows_;

    std::vector<bool> selected_;
    std::optional<std::size_t> anchor_;
    // Строка под нажатой кнопкой в колонке имён и где нажали.
    std::optional<std::pair<std::size_t, QPoint>> pressed_;
    bool reordering_ = false;
};

}  // namespace Phosphor
