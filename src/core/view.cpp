#include "core/view.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <utility>

namespace Phosphor {

View::View(QObject* parent) : QObject{parent} {
}

std::span<const Row> View::rows() const noexcept {
    return rows_;
}

void View::insertRows(std::size_t at, std::vector<Row> rows) {
    if (rows.empty())
        return;
    at = std::min(at, rows_.size());
    rows_.insert(rows_.begin() + static_cast<std::ptrdiff_t>(at), std::make_move_iterator(rows.begin()),
            std::make_move_iterator(rows.end()));
    Q_EMIT rowsChanged();
}

void View::removeRows(std::span<const std::size_t> indices) {
    std::vector<bool> doomed(rows_.size(), false);
    for (const std::size_t i : indices) {
        if (i < doomed.size())
            doomed[i] = true;
    }
    std::size_t kept = 0;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (doomed[i])
            continue;
        if (kept != i)
            rows_[kept] = std::move(rows_[i]);
        ++kept;
    }
    const std::size_t removed = rows_.size() - kept;
    rows_.resize(kept);
    if (removed != 0)
        Q_EMIT rowsChanged();
}

void View::moveRow(std::size_t from, std::size_t to) {
    if (from >= rows_.size())
        return;
    to = std::min(to, rows_.size() - 1);
    if (from == to)
        return;
    const auto lo = static_cast<std::ptrdiff_t>(std::min(from, to));
    const auto hi = static_cast<std::ptrdiff_t>(std::max(from, to));
    const auto first = rows_.begin() + lo;
    const auto last = rows_.begin() + hi + 1;
    // Вперёд — первый элемент диапазона уходит в конец, назад — последний встаёт в начало.
    std::rotate(first, from < to ? first + 1 : last - 1, last);
    Q_EMIT rowsChanged();
}

void View::setFormat(std::size_t row, RowFormat f) {
    if (row >= rows_.size() || rows_[row].format == f)
        return;
    rows_[row].format = f;
    Q_EMIT rowsChanged();
}

TimeWindow View::window() const noexcept {
    return window_;
}

void View::setWindow(TimeWindow w) {
    // Разность считается в беззнаковых, чтобы не переполниться на предельных границах.
    const auto span = static_cast<std::uint64_t>(w.end) - static_cast<std::uint64_t>(w.begin);
    if (w.end <= w.begin || span < static_cast<std::uint64_t>(kMinWindowSpan)) {
        constexpr GlobalTime kMax = std::numeric_limits<GlobalTime>::max();
        if (w.begin > kMax - kMinWindowSpan)
            w = {kMax - kMinWindowSpan, kMax};
        else
            w.end = w.begin + kMinWindowSpan;
    }
    if (w == window_)
        return;
    window_ = w;
    Q_EMIT windowChanged();
}

std::optional<GlobalTime> View::cursorA() const noexcept {
    return cursorA_;
}

std::optional<GlobalTime> View::cursorB() const noexcept {
    return cursorB_;
}

void View::setCursorA(std::optional<GlobalTime> t) {
    if (t == cursorA_)
        return;
    cursorA_ = t;
    Q_EMIT cursorsChanged();
}

void View::setCursorB(std::optional<GlobalTime> t) {
    if (t == cursorB_)
        return;
    cursorB_ = t;
    Q_EMIT cursorsChanged();
}

}  // namespace Phosphor
