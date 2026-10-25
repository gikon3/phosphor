#include "app/row_column.hpp"

#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QRect>
#include <QWheelEvent>
#include <algorithm>
#include <utility>

#include "app/wave_canvas.hpp"

namespace Phosphor {

namespace {

constexpr int kMinColumnWidth = 40;
constexpr int kTextPaddingPx = 4;
constexpr int kInsertMarkWidthPx = 2;

constexpr int kRowHeight = WaveCanvas::kRowHeight;
constexpr int kRulerHeight = WaveCanvas::kRulerHeight;

}  // namespace

RowColumn::RowColumn(QString header, Qt::TextElideMode elide, QWidget* parent) :
        QWidget{parent}, header_{std::move(header)}, elide_{elide} {
    setMinimumWidth(kMinColumnWidth);
}

void RowColumn::setTexts(QStringList texts) {
    texts_ = std::move(texts);
    update();
}

QString RowColumn::text(std::size_t row) const {
    return row < static_cast<std::size_t>(texts_.size()) ? texts_[static_cast<qsizetype>(row)] : QString{};
}

void RowColumn::setSelection(std::vector<bool> selected) {
    selected_ = std::move(selected);
    update();
}

void RowColumn::setFirstRow(int row) {
    firstRow_ = std::max(row, 0);
    update();
}

void RowColumn::setInsertMark(std::optional<std::size_t> row) {
    insertMark_ = row;
    update();
}

void RowColumn::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter{this};
    const auto& pal = palette();
    painter.fillRect(rect(), pal.base());
    painter.fillRect(0, 0, width(), kRulerHeight, pal.button());
    painter.setPen(pal.buttonText().color());
    const auto metrics = painter.fontMetrics();
    const int textWidth = std::max(width() - 2 * kTextPaddingPx, 0);
    painter.drawText(QRect{kTextPaddingPx, 0, textWidth, kRulerHeight}, Qt::AlignLeft | Qt::AlignVCenter,
            metrics.elidedText(header_, Qt::ElideRight, textWidth));

    painter.setClipRect(0, kRulerHeight, width(), std::max(height() - kRulerHeight, 0));
    for (auto row = static_cast<std::size_t>(firstRow_); row < static_cast<std::size_t>(texts_.size()); ++row) {
        const int top = kRulerHeight + static_cast<int>(row - firstRow_) * kRowHeight;
        if (top >= height())
            break;
        const bool selected = row < selected_.size() && selected_[row];
        if (selected)
            painter.fillRect(0, top, width(), kRowHeight, pal.highlight());
        painter.setPen(selected ? pal.highlightedText().color() : pal.text().color());
        painter.drawText(QRect{kTextPaddingPx, top, textWidth, kRowHeight}, Qt::AlignLeft | Qt::AlignVCenter,
                metrics.elidedText(texts_[static_cast<qsizetype>(row)], elide_, textWidth));
    }
    if (insertMark_ && *insertMark_ >= static_cast<std::size_t>(firstRow_)) {
        const int y = kRulerHeight + static_cast<int>(*insertMark_ - firstRow_) * kRowHeight;
        painter.fillRect(0, y - kInsertMarkWidthPx / 2, width(), kInsertMarkWidthPx, pal.highlight());
    }
}

void RowColumn::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    Q_EMIT leftPressed(event->position().toPoint(), event->modifiers());
}

void RowColumn::mouseMoveEvent(QMouseEvent* event) {
    Q_EMIT mouseDragged(event->position().toPoint());
}

void RowColumn::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    Q_EMIT leftReleased(event->position().toPoint());
}

void RowColumn::contextMenuEvent(QContextMenuEvent* event) {
    Q_EMIT contextMenuRequested(event->pos(), event->globalPos());
}

void RowColumn::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) {
        event->ignore();
        return;
    }
    Q_EMIT verticalWheel(event);
}

}  // namespace Phosphor
