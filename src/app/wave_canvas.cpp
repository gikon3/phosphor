#include "app/wave_canvas.hpp"

#include <QApplication>
#include <QColor>
#include <QFontMetrics>
#include <QLineF>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QResizeEvent>
#include <QRgb>
#include <QString>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

#include "core/time_axis.hpp"
#include "core/view.hpp"

namespace Phosphor {

namespace {

constexpr QRgb kBackgroundColor = 0xff10'1418;
constexpr QRgb kRulerColor = 0xff1e'252c;
constexpr QRgb kRulerTextColor = 0xffc8'd0d8;
constexpr QRgb kTickColor = 0xff6a'7580;
constexpr QRgb kWaveColor = 0xff3d'dc84;
constexpr QRgb kUnknownColor = 0xffff'4040;
constexpr QRgb kHighImpedanceColor = 0xfff0'c040;
constexpr QRgb kCursorAColor = 0xffff'd84a;
constexpr QRgb kCursorBColor = 0xff4a'd8ff;
constexpr QRgb kCursorTextColor = 0xff00'0000;
// Заливка X полупрозрачная, чтобы подпись поверх неё читалась.
constexpr int kUnknownFillAlpha = 0x60;

// Отступ уровней 0 и 1 от краёв строки.
constexpr int kWaveMarginPx = 4;
// Ширина скоса шестигранника шины.
constexpr double kSlantPx = 3.0;
constexpr double kLabelPaddingPx = 2.0;
// Подпись не рисуется, если в спан не помещаются два символа.
constexpr int kMinLabelChars = 2;
// Подписи линейки не ближе этого расстояния.
constexpr int kMinTickSpacingPx = 90;
constexpr int kTickLengthPx = 5;
constexpr int kTickLabelOffsetPx = 3;
constexpr int kCursorMarkPaddingPx = 3;
constexpr int kWheelStepDelta = 120;

constexpr GlobalTime kMaxTime = std::numeric_limits<GlobalTime>::max();

// Ширина корректного окна; в double, потому что она может не поместиться в int64.
double spanOf(TimeWindow w) noexcept {
    return static_cast<double>(static_cast<std::uint64_t>(w.end) - static_cast<std::uint64_t>(w.begin));
}

GlobalTime toTimeSaturating(double v) noexcept {
    // 2^63 точно представимо в double, а kMaxTime как double округляется до него же.
    constexpr double kLimit = 9223372036854775808.0;
    if (std::isnan(v))
        return 0;
    if (v >= kLimit)
        return kMaxTime;
    if (v <= -kLimit)
        return std::numeric_limits<GlobalTime>::min();
    return static_cast<GlobalTime>(std::llround(v));
}

GlobalTime saturatingSub(GlobalTime a, GlobalTime b) noexcept {
    if (b < 0 && a > kMaxTime + b)
        return kMaxTime;
    if (b > 0 && a < std::numeric_limits<GlobalTime>::min() + b)
        return std::numeric_limits<GlobalTime>::min();
    return a - b;
}

// Шаг делений линейки вида 1, 2 или 5·10^k fs, не меньше minStep.
GlobalTime tickStep(double minStep) noexcept {
    if (!(minStep > 1.0))
        return 1;
    const double power = std::pow(10.0, std::floor(std::log10(minStep)));
    for (const double mantissa : {1.0, 2.0, 5.0, 10.0}) {
        if (mantissa * power >= minStep)
            return toTimeSaturating(mantissa * power);
    }
    return toTimeSaturating(10.0 * power);
}

// Первое кратное step время не раньше t.
GlobalTime firstTick(GlobalTime t, GlobalTime step) noexcept {
    // Деление округляет к нулю: для отрицательного t частное уже не раньше t.
    const GlobalTime tick = t / step * step;
    if (tick < t && tick <= kMaxTime - step)
        return tick + step;
    return tick;
}

// Вертикальная разметка строки и правый край холста.
struct RowGeometry {
    int top = 0;
    double high = 0.0;
    double mid = 0.0;
    double low = 0.0;
    double right = 0.0;
};

QColor withAlpha(QRgb rgb, int alpha) {
    QColor color{rgb};
    color.setAlpha(alpha);
    return color;
}

std::optional<double> bitLevel(SpanKind kind, const RowGeometry& row) {
    switch (kind) {
        case SpanKind::LOW:
            return row.low;
        case SpanKind::HIGH:
            return row.high;
        case SpanKind::Z:
            return row.mid;
        default:
            return std::nullopt;
    }
}

// Бит: 0 и 1 — уровни со ступенькой на смене, Z — линия посередине, X — красная полоса.
void paintBitSpan(QPainter& painter, SpanKind kind, double x0, double x1, const RowGeometry& row,
        std::optional<double> previousLevel) {
    const QColor wave{kWaveColor};
    if (const auto level = bitLevel(kind, row)) {
        painter.setPen(kind == SpanKind::Z ? QColor{kHighImpedanceColor} : wave);
        painter.drawLine(QLineF{x0, *level, x1, *level});
        if (previousLevel && *previousLevel != *level) {
            painter.setPen(wave);
            painter.drawLine(QLineF{x0, *previousLevel, x0, *level});
        }
        return;
    }
    if (kind == SpanKind::X) {
        painter.fillRect(QRectF{x0, row.high, x1 - x0, row.low - row.high},
                withAlpha(kUnknownColor, kUnknownFillAlpha));
        painter.setPen(QColor{kUnknownColor});
        painter.drawLine(QLineF{x0, row.high, x1, row.high});
        painter.drawLine(QLineF{x0, row.low, x1, row.low});
    }
}

// Подпись шины по центру видимой части [left, right); не рисуется, если не влезают два символа.
void paintLabel(QPainter& painter, const std::string& label, double left, double right, const RowGeometry& row) {
    const auto metrics = painter.fontMetrics();
    const double available = right - left;
    if (label.empty() || available < kMinLabelChars * metrics.averageCharWidth())
        return;
    const auto text = metrics.elidedText(QString::fromStdString(label), Qt::ElideRight, static_cast<int>(available));
    const QRectF box{left, static_cast<double>(row.top), available, static_cast<double>(WaveCanvas::kRowHeight)};
    if (!text.isEmpty())
        painter.drawText(box, Qt::AlignCenter, text);
}

// Шина: шестигранник с подписью; X — красный. Скосы задаёт вызывающий: на краю кадра их нет.
void paintBusSpan(QPainter& painter, const DigitalSpan& span, double x0, double x1, double leftSlant, double rightSlant,
        const RowGeometry& row) {
    if (span.kind == SpanKind::Z) {
        painter.setPen(QColor{kHighImpedanceColor});
        painter.drawLine(QLineF{x0, row.mid, x1, row.mid});
        return;
    }
    const std::array<QPointF, 6> hexagon{QPointF{x0, row.mid}, QPointF{x0 + leftSlant, row.high},
            QPointF{x1 - rightSlant, row.high}, QPointF{x1, row.mid}, QPointF{x1 - rightSlant, row.low},
            QPointF{x0 + leftSlant, row.low}};
    const bool isUnknown = span.kind == SpanKind::X;
    const QColor color{isUnknown ? kUnknownColor : kWaveColor};
    painter.setPen(color);
    painter.setBrush(isUnknown ? QBrush{withAlpha(kUnknownColor, kUnknownFillAlpha)} : QBrush{Qt::NoBrush});
    painter.drawPolygon(hexagon.data(), static_cast<int>(hexagon.size()));
    painter.setBrush(Qt::NoBrush);
    paintLabel(painter, span.label, std::max(x0 + leftSlant + kLabelPaddingPx, 0.0),
            std::min(x1 - rightSlant - kLabelPaddingPx, row.right), row);
}

}  // namespace

WaveCanvas::WaveCanvas(View& view, QWidget* parent) : QWidget{parent}, view_{&view} {
    setAttribute(Qt::WA_OpaquePaintEvent);
    connect(&view, &View::windowChanged, this, qOverload<>(&QWidget::update));
    connect(&view, &View::cursorsChanged, this, qOverload<>(&QWidget::update));
}

void WaveCanvas::setFrame(std::shared_ptr<const FrameResult> frame,
        std::vector<std::optional<std::size_t>> rowToFrame) {
    frame_ = std::move(frame);
    setRowMapping(std::move(rowToFrame));
}

void WaveCanvas::setRowMapping(std::vector<std::optional<std::size_t>> rowToFrame) {
    rowToFrame_ = std::move(rowToFrame);
    update();
}

const RowResult* WaveCanvas::rowResult(std::size_t row) const {
    if (row >= rowToFrame_.size())
        return nullptr;
    const auto index = rowToFrame_[row];
    if (!frame_ || !index || *index >= frame_->rows.size())
        return nullptr;
    return &frame_->rows[*index];
}

void WaveCanvas::setFirstRow(int row) {
    firstRow_ = std::max(row, 0);
    update();
}

void WaveCanvas::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter{this};
    painter.fillRect(rect(), QColor{kBackgroundColor});
    const auto marks = rulerMarks(painter.fontMetrics());
    paintRuler(painter, marks);

    if (const auto transform = frameTransform()) {
        painter.save();
        painter.setClipRect(0, kRulerHeight, width(), std::max(height() - kRulerHeight, 0));
        for (auto row = static_cast<std::size_t>(firstRow_); row < rowToFrame_.size(); ++row) {
            const auto top = kRulerHeight + static_cast<int>(row - firstRow_) * kRowHeight;
            if (top >= height())
                break;
            if (const auto* result = rowResult(row))
                paintRow(painter, result->frame, *transform, top);
        }
        painter.restore();
    }

    paintCursors(painter, marks);
}

void WaveCanvas::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    Q_EMIT resized();
}

void WaveCanvas::wheelEvent(QWheelEvent* event) {
    const auto modifiers = event->modifiers();
    // Часть платформ превращает Shift+колесо в горизонтальную прокрутку.
    const auto angle = event->angleDelta();
    const int delta = angle.y() != 0 ? angle.y() : angle.x();
    if (!(modifiers & (Qt::ControlModifier | Qt::ShiftModifier))) {
        Q_EMIT verticalWheel(event);
        return;
    }
    if (delta == 0) {
        event->ignore();
        return;
    }
    const double steps = static_cast<double>(delta) / kWheelStepDelta;
    const auto window = view_->window();
    if (modifiers & Qt::ControlModifier) {
        const auto pivot = pixelToTime(window, width(), event->position().x());
        Q_EMIT windowRequested(zoomAround(window, pivot, std::pow(kZoomStep, steps)));
    }
    else {
        Q_EMIT windowRequested(scrollBy(window, toTimeSaturating(-steps * kScrollStep * spanOf(window))));
    }
    event->accept();
}

void WaveCanvas::mousePressEvent(QMouseEvent* event) {
    const auto button = event->button();
    if (button != Qt::LeftButton && button != Qt::MiddleButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const bool toB = button == Qt::MiddleButton || (event->modifiers() & Qt::ShiftModifier);
    const auto cursor = toB ? Cursor::B : Cursor::A;
    const double x = event->position().x();
    drag_ = Drag{cursor, x, isNear(cursor, x), false};
    if (!drag_->grabbed)
        moveCursor(cursor, x);
}

void WaveCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (!drag_) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    const double x = event->position().x();
    // Дрожание руки при щелчке по курсору не должно его сдвигать.
    if (drag_->grabbed && !drag_->moved && std::abs(x - drag_->pressX) < QApplication::startDragDistance())
        return;
    drag_->moved = true;
    moveCursor(drag_->cursor, x);
}

void WaveCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (!drag_) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    const auto drag = *drag_;
    drag_.reset();
    // Повторный щелчок по B убирает его; A щелчком не убирается.
    if (drag.cursor == Cursor::B && drag.grabbed && !drag.moved)
        view_->setCursorB(std::nullopt);
}

std::optional<WaveCanvas::Transform> WaveCanvas::frameTransform() const {
    const auto window = view_->window();
    if (!frame_)
        return std::nullopt;
    const auto& frame = *frame_;
    if (frame.widthPx <= 0 || width() <= 0 || frame.window.end <= frame.window.begin || window.end <= window.begin)
        return std::nullopt;
    // Граница кадра в пикселях текущего окна; для совпадающих окна и ширины это ровно 0 и width.
    const double begin = timeToPixel(window, width(), frame.window.begin);
    const double end = timeToPixel(window, width(), frame.window.end);
    const double scale = (end - begin) / frame.widthPx;
    if (!std::isfinite(scale) || !std::isfinite(begin))
        return std::nullopt;
    return Transform{scale, begin};
}

WaveCanvas::RulerMarks WaveCanvas::rulerMarks(const QFontMetrics& metrics) const {
    RulerMarks marks;
    const auto window = view_->window();
    if (width() <= 0 || window.end <= window.begin)
        return marks;
    const auto a = view_->cursorA();
    const auto b = view_->cursorB();

    const auto addCursor = [&](std::optional<GlobalTime> t, QRgb color, const QString& name) {
        if (!t || *t < window.begin || *t > window.end)
            return;
        const double x = timeToPixel(window, width(), *t);
        const double markWidth = metrics.horizontalAdvance(name) + 2.0 * kCursorMarkPaddingPx;
        marks.cursors.push_back(CursorMark{x,
                QRectF{x - markWidth / 2.0, 0, markWidth, static_cast<double>(kRulerHeight)}, name, color});
    };
    addCursor(a, kCursorAColor, tr("A"));
    addCursor(b, kCursorBColor, tr("B"));

    // Подпись — по центру видимой части отрезка; отрезка целиком за краем окна не видно.
    const auto clamped = [&](GlobalTime t) {
        return std::clamp(timeToPixel(window, width(), t), 0.0, static_cast<double>(width()));
    };
    if (a && b && (!marks.cursors.empty() || clamped(*a) != clamped(*b))) {
        const double xa = clamped(*a);
        const double xb = clamped(*b);
        auto text = tr("\u0394 %1").arg(QString::fromStdString(formatTime(saturatingSub(*b, *a))));
        const double textWidth = metrics.horizontalAdvance(text) + 2.0 * kCursorMarkPaddingPx;
        const QRectF box{(xa + xb - textWidth) / 2.0, 0, textWidth, static_cast<double>(kRulerHeight)};
        marks.delta = DeltaMark{xa, xb, box, std::move(text)};
    }
    return marks;
}

void WaveCanvas::paintRuler(QPainter& painter, const RulerMarks& marks) const {
    painter.fillRect(0, 0, width(), kRulerHeight, QColor{kRulerColor});
    const auto window = view_->window();
    if (width() <= 0 || window.end <= window.begin)
        return;
    const auto metrics = painter.fontMetrics();
    const auto covered = [&](const QRectF& label) {
        if (marks.delta && marks.delta->box.intersects(label))
            return true;
        return std::ranges::any_of(marks.cursors, [&](const CursorMark& mark) { return mark.box.intersects(label); });
    };
    const auto step = tickStep(spanOf(window) * kMinTickSpacingPx / width());
    for (GlobalTime t = firstTick(window.begin, step); t < window.end;) {
        const double x = timeToPixel(window, width(), t);
        painter.setPen(QColor{kTickColor});
        painter.drawLine(
                QLineF{x, static_cast<double>(kRulerHeight - kTickLengthPx), x, static_cast<double>(kRulerHeight)});
        const auto text = QString::fromStdString(formatTime(t, step));
        const QRectF label{x + kTickLabelOffsetPx, 0, static_cast<double>(metrics.horizontalAdvance(text)),
                static_cast<double>(kRulerHeight)};
        if (!covered(label)) {
            painter.setPen(QColor{kRulerTextColor});
            painter.drawText(label, Qt::AlignLeft | Qt::AlignVCenter, text);
        }
        // Разность в беззнаковых, чтобы шаг у границы int64 не переполнился.
        if (static_cast<std::uint64_t>(window.end) - static_cast<std::uint64_t>(t) <= static_cast<std::uint64_t>(step))
            break;
        t += step;
    }
}

void WaveCanvas::paintRow(QPainter& painter, const DigitalFrame& frame, Transform transform, int top) const {
    const double high = top + kWaveMarginPx;
    const double low = top + kRowHeight - kWaveMarginPx;
    const RowGeometry row{top, high, (high + low) / 2.0, low, static_cast<double>(width())};

    std::optional<double> previousLevel;
    const auto& spans = frame.spans;
    for (std::size_t i = 0; i < spans.size(); ++i) {
        const auto& span = spans[i];
        const double rawX0 = transform.scale * span.x0 + transform.offset;
        const double rawX1 = transform.scale * span.x1 + transform.offset;
        const auto level = frame.isBit ? bitLevel(span.kind, row) : std::nullopt;
        const bool visible = rawX1 > 0.0 && rawX0 < row.right;
        if (!visible || span.kind == SpanKind::NONE) {
            previousLevel = level;
            continue;
        }
        // Растянутый промежуточный кадр даёт координаты в триллионы пикселей, а такие фигуры
        // QPainter молча не рисует; поэтому спан обрезается чуть шире холста, а обрезанный край
        // уходит за холст без скоса.
        const double left = -kSlantPx - 1.0;
        const double right = row.right + kSlantPx + 1.0;
        const bool leftClipped = rawX0 < left;
        const bool rightClipped = rawX1 > right;
        const double x0 = std::max(rawX0, left);
        const double x1 = std::min(rawX1, right);
        if (span.kind == SpanKind::DENSE) {
            painter.fillRect(QRectF{x0, row.high, x1 - x0, row.low - row.high}, QColor{kWaveColor});
        }
        else if (frame.isBit) {
            paintBitSpan(painter, span.kind, x0, x1, row, previousLevel);
        }
        else {
            // Скос — только на смене значения внутри кадра, а не на его краях.
            const double slant = std::min(kSlantPx, (x1 - x0) / 2.0);
            const double leftSlant = i > 0 && !leftClipped ? slant : 0.0;
            const double rightSlant = i + 1 < spans.size() && !rightClipped ? slant : 0.0;
            paintBusSpan(painter, span, x0, x1, leftSlant, rightSlant, row);
        }
        previousLevel = level;
    }
}

void WaveCanvas::paintCursors(QPainter& painter, const RulerMarks& marks) const {
    if (marks.delta) {
        // Отрезок — по нижнему краю линейки, чтобы не перечёркивать подписи.
        const double y = kRulerHeight - 1.5;
        painter.setPen(QColor{kRulerTextColor});
        painter.drawLine(QLineF{marks.delta->from, y, marks.delta->to, y});
        painter.fillRect(marks.delta->box, QColor{kRulerColor});
        painter.drawText(marks.delta->box, Qt::AlignCenter, marks.delta->text);
    }
    for (const auto& mark : marks.cursors) {
        painter.setPen(QColor{mark.color});
        painter.drawLine(QLineF{mark.x, 0, mark.x, static_cast<double>(height())});
        painter.fillRect(mark.box, QColor{mark.color});
        painter.setPen(QColor{kCursorTextColor});
        painter.drawText(mark.box, Qt::AlignCenter, mark.name);
    }
}

bool WaveCanvas::isNear(Cursor cursor, double x) const {
    const auto t = cursor == Cursor::A ? view_->cursorA() : view_->cursorB();
    return t && std::abs(timeToPixel(view_->window(), width(), *t) - x) <= kGrabDistancePx;
}

void WaveCanvas::moveCursor(Cursor cursor, double x) {
    const auto t = pixelToTime(view_->window(), width(), std::clamp(x, 0.0, static_cast<double>(width())));
    if (cursor == Cursor::A)
        view_->setCursorA(t);
    else
        view_->setCursorB(t);
}

}  // namespace Phosphor
