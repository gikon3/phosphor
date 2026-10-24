#pragma once

#include <QObject>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/session.hpp"
#include "core/time_axis.hpp"
#include "core/time_mapper.hpp"
#include "core/value_format.hpp"

namespace Phosphor {

// Сигнал как пара «источник + полный путь». Путь, в отличие от NodeId, переживает
// переиндексацию дампа.
struct SignalRef {
    SourceId source = 0;
    std::string path;

    friend bool operator==(const SignalRef&, const SignalRef&) = default;
};

struct Row {
    SignalRef signal;
    RowFormat format;
};

// Что пользователь видит: строки, окно времени и курсоры A/B. Мутаторы — единственный
// способ изменить вид; сигналы испускаются только при фактическом изменении.
class View : public QObject {
    Q_OBJECT

public:
    explicit View(QObject* parent = nullptr);

    // Представление действительно до следующего изменения строк (insertRows, removeRows,
    // moveRow, setFormat); чтобы сохранить снимок, его нужно скопировать.
    [[nodiscard]] std::span<const Row> rows() const noexcept;

    // Позиция at обрезается до размера списка; пустой набор ничего не меняет.
    void insertRows(std::size_t at, std::vector<Row> rows);

    // Индексы могут идти в любом порядке, повторяться и выходить за границы: лишние
    // игнорируются.
    void removeRows(std::span<const std::size_t> indices);

    // to — итоговый индекс перемещаемой строки; он обрезается до последнего. Неверный from
    // ничего не меняет.
    void moveRow(std::size_t from, std::size_t to);

    // Неверный индекс строки игнорируется.
    void setFormat(std::size_t row, RowFormat f);

    [[nodiscard]] TimeWindow window() const noexcept;

    // Окно шире kMinWindowSpan: при end <= begin остаётся begin, а end отодвигается на
    // kMinWindowSpan; у границы int64 вместо этого сдвигается begin.
    void setWindow(TimeWindow w);

    [[nodiscard]] std::optional<GlobalTime> cursorA() const noexcept;
    [[nodiscard]] std::optional<GlobalTime> cursorB() const noexcept;
    void setCursorA(std::optional<GlobalTime> t);
    void setCursorB(std::optional<GlobalTime> t);

Q_SIGNALS:
    void rowsChanged();
    void windowChanged();
    void cursorsChanged();

private:
    std::vector<Row> rows_;
    TimeWindow window_{0, kMinWindowSpan};
    std::optional<GlobalTime> cursorA_;
    std::optional<GlobalTime> cursorB_;
};

}  // namespace Phosphor
