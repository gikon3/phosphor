#pragma once

#include <QRectF>
#include <QRgb>
#include <QString>
#include <QWidget>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "core/renderer.hpp"
#include "core/time_mapper.hpp"

class QFontMetrics;
class QMouseEvent;
class QPaintEvent;
class QPainter;
class QResizeEvent;
class QWheelEvent;

namespace Phosphor {

class View;

// Холст волн: линейка времени сверху и строки под ней. Рисует только спаны последнего кадра,
// поэтому цена отрисовки не зависит от числа изменений в окне. Кадр, построенный для другого
// окна или ширины, растягивается и сдвигается по x до прихода свежего: сначала грубая картинка,
// но не замерший экран. Каждая кнопка мыши ведёт свой курсор вида: левая — A, средняя и Shift+левая —
// B. Новое окно холст сам не применяет, а предлагает сигналом windowRequested: допустимые границы
// окна решает владелец. Колесо без модификаторов уходит сигналом verticalWheel: строки прокручивает
// владелец. Вид должен пережить холст.
class WaveCanvas : public QWidget {
    Q_OBJECT

public:
    static constexpr int kRowHeight = 22;
    static constexpr int kRulerHeight = 20;
    // Множитель ширины окна на шаг колеса.
    static constexpr double kZoomStep = 0.8;
    // Доля окна, на которую сдвигает шаг колеса.
    static constexpr double kScrollStep = 0.1;
    // Курсор ближе этого расстояния захватывается своей кнопкой, а не ставится заново.
    static constexpr int kGrabDistancePx = 4;

public:
    explicit WaveCanvas(View& view, QWidget* parent = nullptr);

    // Кадр разделяется с его владельцем, а не копируется. rowToFrame[i] — строка кадра для строки
    // вида i или nullopt, если для неё в кадре нет данных; размер равен числу строк вида.
    void setFrame(std::shared_ptr<const FrameResult> frame, std::vector<std::optional<std::size_t>> rowToFrame);
    void setRowMapping(std::vector<std::optional<std::size_t>> rowToFrame);
    // nullptr, если у строки вида нет данных в кадре.
    [[nodiscard]] const RowResult* rowResult(std::size_t row) const;

    void setFirstRow(int row);

Q_SIGNALS:
    void resized();
    // Окно, которого просит колесо мыши.
    void windowRequested(Phosphor::TimeWindow window);
    // Колесо без Ctrl и Shift. Событие действительно только на время испускания сигнала.
    void verticalWheel(QWheelEvent* event);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    enum class Cursor : std::uint8_t { A, B };

    // Курсор, который ведёт нажатая кнопка.
    struct Drag {
        Cursor cursor = Cursor::A;
        double pressX = 0.0;
        // Нажатие пришлось на уже стоящий курсор: без сдвига это щелчок по нему, а не перетаскивание.
        bool grabbed = false;
        bool moved = false;
    };

    // Пересчёт пикселя кадра в пиксель холста: x' = scale·x + offset.
    struct Transform {
        double scale = 1.0;
        double offset = 0.0;
    };

    // Метка курсора на линейке.
    struct CursorMark {
        double x = 0.0;
        QRectF box;
        QString name;
        QRgb color = 0;
    };

    // Отрезок между курсорами и подпись Δt на нём.
    struct DeltaMark {
        double from = 0.0;
        double to = 0.0;
        QRectF box;
        QString text;
    };

    // Всё, что линейка показывает поверх делений: подписи делений под метки не попадают.
    struct RulerMarks {
        std::vector<CursorMark> cursors;
        std::optional<DeltaMark> delta;
    };

private:
    // nullopt, если кадр нельзя наложить на текущее окно: пустое окно или ширина.
    [[nodiscard]] std::optional<Transform> frameTransform() const;
    [[nodiscard]] RulerMarks rulerMarks(const QFontMetrics& metrics) const;
    void paintRuler(QPainter& painter, const RulerMarks& marks) const;
    void paintRow(QPainter& painter, const DigitalFrame& frame, Transform transform, int top) const;
    void paintCursors(QPainter& painter, const RulerMarks& marks) const;

    [[nodiscard]] bool isNear(Cursor cursor, double x) const;
    void moveCursor(Cursor cursor, double x);

private:
    View* view_;
    // nullptr, пока кадров не было.
    std::shared_ptr<const FrameResult> frame_;
    std::vector<std::optional<std::size_t>> rowToFrame_;
    int firstRow_ = 0;
    std::optional<Drag> drag_;
};

}  // namespace Phosphor
