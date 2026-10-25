#include "app/wave_panel.hpp"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMenu>
#include <QMetaObject>
#include <QMimeData>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStringList>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <memory>
#include <utility>

#include "app/row_column.hpp"
#include "app/shortcuts.hpp"
#include "app/signal_mime.hpp"
#include "app/wave_canvas.hpp"
#include "core/renderer.hpp"
#include "core/session.hpp"

namespace Phosphor {

namespace {

constexpr int kNamesWidth = 200;
constexpr int kValuesWidth = 120;
constexpr int kCanvasWidth = 800;
// Полоса прокрутки целочисленная, а время — int64 фемтосекунд: вся область прокрутки делится
// на столько единиц.
constexpr int kScrollUnits = 1'000'000;
// Кадры приходят только для последних запросов; предел держит память ограниченной, даже если
// рендерер долго не отдаёт кадров.
constexpr std::size_t kMaxPendingRequests = 256;

constexpr int kRowHeight = WaveCanvas::kRowHeight;
constexpr int kRulerHeight = WaveCanvas::kRulerHeight;

double spanOf(TimeWindow w) noexcept {
    return static_cast<double>(static_cast<std::uint64_t>(w.end) - static_cast<std::uint64_t>(w.begin));
}

GlobalTime toTimeSaturating(double v) noexcept {
    // 2^63 точно представимо в double, а максимум int64 как double округляется до него же.
    constexpr double kLimit = 9223372036854775808.0;
    if (std::isnan(v))
        return 0;
    if (v >= kLimit)
        return std::numeric_limits<GlobalTime>::max();
    if (v <= -kLimit)
        return std::numeric_limits<GlobalTime>::min();
    return static_cast<GlobalTime>(std::llround(v));
}

bool sameRow(const Row& a, const Row& b) {
    return a.signal == b.signal && a.format == b.format;
}

}  // namespace

WavePanel::WavePanel(Session& session, View& view, Renderer& renderer, QWidget* parent) :
        QWidget{parent}, session_{&session}, view_{&view}, renderer_{&renderer},
        splitter_{new QSplitter{Qt::Horizontal, this}} {
    auto* layout = new QHBoxLayout{this};
    layout->setContentsMargins(0, 0, 0, 0);
    splitter_->setChildrenCollapsible(false);
    layout->addWidget(splitter_);

    // Длинный путь обрезается слева: важнее всего имя самого сигнала.
    names_ = new RowColumn{tr("Name"), Qt::ElideLeft, splitter_};
    names_->setObjectName(QStringLiteral("waveNames"));
    names_->setFocusPolicy(Qt::ClickFocus);
    values_ = new RowColumn{tr("@A / @B"), Qt::ElideRight, splitter_};
    values_->setObjectName(QStringLiteral("waveValues"));

    auto* waveArea = new QWidget{splitter_};
    auto* grid = new QGridLayout{waveArea};
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(0);
    canvas_ = new WaveCanvas{view, waveArea};
    canvas_->setFocusPolicy(Qt::ClickFocus);
    vScroll_ = new QScrollBar{Qt::Vertical, waveArea};
    hScroll_ = new QScrollBar{Qt::Horizontal, waveArea};
    grid->addWidget(canvas_, 0, 0);
    grid->addWidget(vScroll_, 0, 1);
    grid->addWidget(hScroll_, 1, 0);

    splitter_->addWidget(names_);
    splitter_->addWidget(values_);
    splitter_->addWidget(waveArea);
    splitter_->setStretchFactor(2, 1);
    splitter_->setSizes({kNamesWidth, kValuesWidth, kCanvasWidth});

    connect(names_, &RowColumn::leftPressed, this, &WavePanel::pressName);
    connect(names_, &RowColumn::mouseDragged, this, &WavePanel::moveName);
    connect(names_, &RowColumn::leftReleased, this, &WavePanel::releaseName);
    connect(names_, &RowColumn::contextMenuRequested, this, &WavePanel::showFormatMenu);
    // Колесо без модификаторов над любой из колонок прокручивает строки.
    connect(names_, &RowColumn::verticalWheel, this, &WavePanel::scrollRowsByWheel);
    connect(values_, &RowColumn::verticalWheel, this, &WavePanel::scrollRowsByWheel);
    connect(canvas_, &WaveCanvas::verticalWheel, this, &WavePanel::scrollRowsByWheel);
    setAcceptDrops(true);
    createActions();

    connect(&view, &View::rowsChanged, this, &WavePanel::handleRowsChanged);
    connect(&view, &View::windowChanged, this, [this] {
        updateHorizontalScroll();
        scheduleFrame();
    });
    connect(&view, &View::cursorsChanged, this, &WavePanel::scheduleFrame);
    connect(canvas_, &WaveCanvas::resized, this, [this] {
        updateVerticalScroll();
        updateRowMapping();
        scheduleFrame();
    });
    connect(vScroll_, &QScrollBar::valueChanged, this, [this](int row) {
        names_->setFirstRow(row);
        values_->setFirstRow(row);
        canvas_->setFirstRow(row);
        updateRowMapping();
        scheduleFrame();
    });
    connect(canvas_, &WaveCanvas::windowRequested, this, &WavePanel::applyWindow);
    connect(hScroll_, &QScrollBar::valueChanged, this, &WavePanel::scrollHorizontally);
    connect(hScroll_, &QScrollBar::actionTriggered, this, &WavePanel::stepHorizontally);
    // Имена зависят от числа источников и их состояния, а не только от строк.
    connect(&session, &Session::sourceAdded, this, &WavePanel::updateNames);
    connect(&session, &Session::sourceFailed, this, &WavePanel::updateNames);
    connect(&session, &Session::sourceReady, this, &WavePanel::handleSourcesChanged);
    connect(&session, &Session::sourceRemoved, this, &WavePanel::handleSourcesChanged);
    connect(&renderer, &Renderer::frameReady, this, &WavePanel::handleFrame);

    handleRowsChanged();
    updateHorizontalScroll();
}

void WavePanel::requestFrame() {
    frameScheduled_ = false;
    const auto rows = view_->rows();
    const auto range = visibleRows();
    FrameRequest request;
    request.generation = ++generation_;
    request.window = view_->window();
    request.widthPx = canvas_->width();
    request.cursorA = view_->cursorA();
    request.cursorB = view_->cursorB();
    std::vector<Row> requested;
    for (auto i = range.first; i < range.last; ++i) {
        const auto node = i < nodes_.size() ? nodes_[i] : WaSafe::NodeId{};
        request.rows.push_back(RowRequest{rows[i].signal.source, node, rows[i].format});
        requested.push_back(rows[i]);
    }
    requested_.emplace(request.generation, std::move(requested));
    while (requested_.size() > kMaxPendingRequests)
        requested_.erase(requested_.begin());
    renderer_->request(std::move(request));
}

QString WavePanel::nameText(std::size_t row) const {
    return names_->text(row);
}

QString WavePanel::valueText(std::size_t row) const {
    return values_->text(row);
}

QList<QAction*> WavePanel::timeActions() const {
    return timeActions_;
}

void WavePanel::dragEnterEvent(QDragEnterEvent* event) {
    if (acceptsDrag(*event))
        event->acceptProposedAction();
    else
        event->ignore();
}

void WavePanel::dragMoveEvent(QDragMoveEvent* event) {
    if (!acceptsDrag(*event)) {
        event->ignore();
        return;
    }
    names_->setInsertMark(insertionRow(canvasY(event->position().toPoint())));
    event->acceptProposedAction();
}

void WavePanel::dragLeaveEvent(QDragLeaveEvent* /*event*/) {
    names_->setInsertMark(std::nullopt);
}

void WavePanel::dropEvent(QDropEvent* event) {
    names_->setInsertMark(std::nullopt);
    if (!acceptsDrag(*event)) {
        event->ignore();
        return;
    }
    std::vector<Row> rows;
    for (auto& ref : decodeSignalRefs(event->mimeData()->data(kSignalRefsMimeType)))
        rows.push_back(Row{std::move(ref), RowFormat{}});
    const auto at = insertionRow(canvasY(event->position().toPoint()));
    const auto count = rows.size();
    view_->insertRows(at, std::move(rows));
    // Вставленные строки становятся выделением.
    std::ranges::fill(selected_, false);
    for (auto i = at; i < at + count && i < selected_.size(); ++i)
        selected_[i] = true;
    anchor_ = at;
    updateSelection();
    event->acceptProposedAction();
}

void WavePanel::createActions() {
    const auto add = [this](const QString& text, const QList<QKeySequence>& shortcuts, Qt::ShortcutContext context) {
        auto* action = new QAction{text, this};
        action->setShortcuts(shortcuts);
        action->setShortcutContext(context);
        addAction(action);
        return action;
    };
    // Масштаб и Delete действуют, только когда фокус в панели: клавиши цифрового блока без Ctrl и
    // Delete в других виджетах окна значат своё. Курсоры в начало окна — во всём окне. Из меню
    // действия срабатывают при любом фокусе.
    const auto addTime = [&](const QString& text, const QList<QKeySequence>& shortcuts, Qt::ShortcutContext context) {
        auto* action = add(text, shortcuts, context);
        timeActions_.push_back(action);
        return action;
    };
    connect(addTime(tr("Zoom In"),
                    {Shortcuts::kZoomIn, Shortcuts::kZoomInPlus, Shortcuts::kZoomInShiftPlus, Shortcuts::kZoomInKeypad},
                    Qt::WidgetWithChildrenShortcut),
            &QAction::triggered, this, [this] { zoomBy(WaveCanvas::kZoomStep); });
    connect(addTime(tr("Zoom Out"), {Shortcuts::kZoomOut, Shortcuts::kZoomOutKeypad}, Qt::WidgetWithChildrenShortcut),
            &QAction::triggered, this, [this] { zoomBy(1.0 / WaveCanvas::kZoomStep); });
    connect(addTime(tr("Zoom Fit"), {Shortcuts::kZoomFit}, Qt::WidgetWithChildrenShortcut), &QAction::triggered, this,
            &WavePanel::zoomFit);
    connect(addTime(tr("Cursor A to Window Start"), {Shortcuts::kCursorAToWindowStart}, Qt::WindowShortcut),
            &QAction::triggered, this, &WavePanel::moveCursorAToWindowStart);
    connect(addTime(tr("Cursor B to Window Start"), {Shortcuts::kCursorBToWindowStart}, Qt::WindowShortcut),
            &QAction::triggered, this, &WavePanel::moveCursorBToWindowStart);
    connect(add(tr("Remove Rows"), {Shortcuts::kRemoveRows}, Qt::WidgetWithChildrenShortcut), &QAction::triggered, this,
            &WavePanel::removeSelectedRows);
}

void WavePanel::scheduleFrame() {
    if (frameScheduled_)
        return;
    frameScheduled_ = true;
    QMetaObject::invokeMethod(
            this,
            [this] {
                if (frameScheduled_)
                    requestFrame();
            },
            Qt::QueuedConnection);
}

void WavePanel::handleRowsChanged() {
    resolveRows();
    selected_.resize(view_->rows().size(), false);
    if (anchor_ && *anchor_ >= selected_.size())
        anchor_.reset();
    updateSelection();
    updateNames();
    updateVerticalScroll();
    updateRowMapping();
    scheduleFrame();
}

void WavePanel::handleSourcesChanged() {
    resolveRows();
    updateNames();
    updateHorizontalScroll();
    scheduleFrame();
}

void WavePanel::handleFrame(const FrameResult& frame) {
    const auto it = requested_.find(frame.generation);
    // Кадр не по запросу панели.
    if (it == requested_.end())
        return;
    frameRows_ = std::move(it->second);
    requested_.erase(requested_.begin(), std::next(it));
    // Сигнал отдаёт кадр по ссылке на время вызова: копия снимается один раз и дальше разделяется.
    canvas_->setFrame(std::make_shared<const FrameResult>(frame), rowMapping());
    updateValues();
}

void WavePanel::resolveRows() {
    const auto rows = view_->rows();
    nodes_.clear();
    nodes_.reserve(rows.size());
    for (const auto& row : rows) {
        const auto* db = session_->hierarchyDb(row.signal.source);
        const auto signal = db != nullptr ? db->find(row.signal.path) : std::nullopt;
        nodes_.push_back(signal ? signal->node() : WaSafe::NodeId{});
    }
}

WavePanel::RowRange WavePanel::visibleRows() const {
    const auto count = view_->rows().size();
    const int area = std::max(canvas_->height() - kRulerHeight, 0);
    const int visible = (area + kRowHeight - 1) / kRowHeight;
    const auto first = std::min(static_cast<std::size_t>(std::max(vScroll_->value(), 0)), count);
    return {first, std::min(first + static_cast<std::size_t>(visible), count)};
}

std::vector<std::optional<std::size_t>> WavePanel::rowMapping() const {
    const auto rows = view_->rows();
    const auto range = visibleRows();
    std::vector<std::optional<std::size_t>> mapping(rows.size());
    // Одинаковые строки сопоставляются по порядку: k-я такая видимая строка — k-й в кадре.
    std::vector<bool> used(frameRows_.size(), false);
    for (auto i = range.first; i < range.last; ++i) {
        for (std::size_t j = 0; j < frameRows_.size(); ++j) {
            if (!used[j] && sameRow(rows[i], frameRows_[j])) {
                used[j] = true;
                mapping[i] = j;
                break;
            }
        }
    }
    return mapping;
}

void WavePanel::updateRowMapping() {
    canvas_->setRowMapping(rowMapping());
    updateValues();
}

void WavePanel::updateNames() {
    QStringList texts;
    const auto rows = view_->rows();
    // С одним дампом метка источника — лишний шум; с несколькими без неё одинаковые пути не различить.
    const bool labelled = session_->sourceIds().size() > 1;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto* info = session_->info(rows[i].signal.source);
        auto text = QString::fromStdString(rows[i].signal.path);
        if (labelled && info != nullptr)
            text = tr("%1 [%2]").arg(text, info->label);
        if (info != nullptr && info->state == SourceState::LOADING)
            text = tr("%1 (loading\u2026)").arg(text);
        else if (i >= nodes_.size() || !nodes_[i].valid())
            text = tr("%1 (not in dump)").arg(text);
        texts.push_back(text);
    }
    names_->setTexts(std::move(texts));
}

void WavePanel::updateValues() {
    QStringList texts;
    for (std::size_t i = 0; i < view_->rows().size(); ++i) {
        const auto* result = canvas_->rowResult(i);
        if (result == nullptr) {
            texts.push_back(QString{});
            continue;
        }
        const auto a = QString::fromStdString(result->valueA);
        texts.push_back(
                result->valueB.empty() ? a : a + QStringLiteral(" / ") + QString::fromStdString(result->valueB));
    }
    values_->setTexts(std::move(texts));
}

void WavePanel::updateVerticalScroll() {
    const int area = std::max(canvas_->height() - kRulerHeight, 0);
    const int fullRows = area / kRowHeight;
    const auto count = static_cast<int>(std::min<std::size_t>(view_->rows().size(), std::numeric_limits<int>::max()));
    vScroll_->setRange(0, std::max(count - fullRows, 0));
    vScroll_->setPageStep(std::max(fullRows, 1));
    vScroll_->setSingleStep(1);
}

void WavePanel::scrollRowsByWheel(QWheelEvent* event) {
    // Событие отдаётся самой полосе, а не пересчитывается: шаг, инверсия, сенсорные панели и
    // настройки платформы остаются за ней.
    QCoreApplication::sendEvent(vScroll_, event);
}

void WavePanel::updateHorizontalScroll() {
    const QSignalBlocker blocker{hScroll_};
    const auto window = view_->window();
    const auto total = session_->totalRange();
    if (!total || window.end <= window.begin) {
        hScroll_->setRange(0, 0);
        return;
    }
    // Окно может выходить за дампы; тогда область прокрутки расширяется до него.
    const TimeWindow area{std::min(total->begin, window.begin), std::max(total->end, window.end)};
    const double units = kScrollUnits / spanOf(area);
    const int page = std::clamp(static_cast<int>(std::lround(spanOf(window) * units)), 1, kScrollUnits);
    hScroll_->setRange(0, kScrollUnits - page);
    hScroll_->setPageStep(page);
    hScroll_->setSingleStep(std::max(page / 10, 1));
    hScroll_->setValue(static_cast<int>(std::lround(spanOf({area.begin, window.begin}) * units)));
}

void WavePanel::scrollHorizontally(int value) {
    const auto window = view_->window();
    const auto total = session_->totalRange();
    if (!total || window.end <= window.begin)
        return;
    const TimeWindow area{std::min(total->begin, window.begin), std::max(total->end, window.end)};
    const double offset = spanOf(area) * value / kScrollUnits;
    applyWindow(scrollBy(window, toTimeSaturating(offset - spanOf({area.begin, window.begin}))));
}

void WavePanel::stepHorizontally(int action) {
    double fraction = 0.0;
    switch (action) {
        case QAbstractSlider::SliderSingleStepAdd:
            fraction = WaveCanvas::kScrollStep;
            break;
        case QAbstractSlider::SliderSingleStepSub:
            fraction = -WaveCanvas::kScrollStep;
            break;
        case QAbstractSlider::SliderPageStepAdd:
            fraction = 1.0;
            break;
        case QAbstractSlider::SliderPageStepSub:
            fraction = -1.0;
            break;
        default:
            return;
    }
    const auto window = view_->window();
    applyWindow(scrollBy(window, toTimeSaturating(fraction * spanOf(window))));
    // Позиция полосы уже пересчитана по новому окну; шаг в её единицах отменяется.
    hScroll_->setSliderPosition(hScroll_->value());
}

void WavePanel::zoomBy(double factor) {
    const auto window = view_->window();
    if (window.end <= window.begin)
        return;
    const auto half = (static_cast<std::uint64_t>(window.end) - static_cast<std::uint64_t>(window.begin)) / 2;
    applyWindow(zoomAround(window, window.begin + static_cast<GlobalTime>(half), factor));
}

void WavePanel::zoomFit() {
    if (const auto total = session_->totalRange())
        applyWindow(*total);
}

void WavePanel::moveCursorAToWindowStart() {
    view_->setCursorA(view_->window().begin);
}

void WavePanel::moveCursorBToWindowStart() {
    view_->setCursorB(view_->window().begin);
}

void WavePanel::applyWindow(TimeWindow window) {
    const auto total = session_->totalRange();
    if (total && window.begin < total->begin) {
        // Сдвиг вправо с сохранением ширины; у верхней границы int64 ширина урезается.
        const auto span = static_cast<std::uint64_t>(window.end) - static_cast<std::uint64_t>(window.begin);
        const auto room = static_cast<std::uint64_t>(std::numeric_limits<GlobalTime>::max()) -
                static_cast<std::uint64_t>(total->begin);
        // Сложение в беззнаковых: ширина бывает больше максимума int64, а сумма не выходит за room.
        const auto end = static_cast<std::uint64_t>(total->begin) + std::min(span, room);
        window = {total->begin, static_cast<GlobalTime>(end)};
    }
    view_->setWindow(window);
}

void WavePanel::removeSelectedRows() {
    std::vector<std::size_t> indices;
    for (std::size_t i = 0; i < selected_.size(); ++i) {
        if (selected_[i])
            indices.push_back(i);
    }
    if (indices.empty())
        return;
    std::ranges::fill(selected_, false);
    anchor_.reset();
    view_->removeRows(indices);
}

void WavePanel::selectOnly(std::optional<std::size_t> row) {
    std::ranges::fill(selected_, false);
    if (row && *row < selected_.size())
        selected_[*row] = true;
    anchor_ = row;
    updateSelection();
}

void WavePanel::updateSelection() {
    names_->setSelection(selected_);
}

void WavePanel::pressName(QPoint pos, Qt::KeyboardModifiers modifiers) {
    names_->setFocus(Qt::MouseFocusReason);
    const auto row = rowAt(pos.y());
    if (row && (modifiers & Qt::ControlModifier)) {
        selected_[*row] = !selected_[*row];
        anchor_ = row;
        updateSelection();
    }
    else if (row && anchor_ && (modifiers & Qt::ShiftModifier)) {
        std::ranges::fill(selected_, false);
        for (auto i = std::min(*anchor_, *row); i <= std::max(*anchor_, *row); ++i)
            selected_[i] = true;
        updateSelection();
    }
    else {
        selectOnly(row);
    }
    pressed_.reset();
    if (row)
        pressed_ = std::pair{*row, pos};
    reordering_ = false;
}

void WavePanel::moveName(QPoint pos) {
    if (!pressed_)
        return;
    if (!reordering_ && (pos - pressed_->second).manhattanLength() >= QApplication::startDragDistance())
        reordering_ = true;
    if (reordering_)
        names_->setInsertMark(insertionRow(pos.y()));
}

void WavePanel::releaseName(QPoint pos) {
    if (pressed_ && reordering_) {
        const auto from = pressed_->first;
        const auto target = insertionRow(pos.y());
        // Позиция вставки считается до изъятия строки, а moveRow ждёт итоговый индекс.
        const auto to = target > from ? target - 1 : target;
        names_->setInsertMark(std::nullopt);
        pressed_.reset();
        reordering_ = false;
        view_->moveRow(from, to);
        selectOnly(to);
        return;
    }
    pressed_.reset();
    reordering_ = false;
}

void WavePanel::showFormatMenu(QPoint pos, QPoint globalPos) {
    const auto row = rowAt(pos.y());
    if (!row)
        return;
    if (!selected_[*row])
        selectOnly(row);
    const auto current = view_->rows()[*row].format;

    // Меню одноразовое: отметка показывает текущий формат, а выбор закрывает меню.
    auto* menu = new QMenu{this};
    menu->setAttribute(Qt::WA_DeleteOnClose);
    const std::array<std::pair<QString, RowFormat>, 5> choices{{
            {tr("Bin"), RowFormat{WaSafe::Radix::BIN, false}},
            {tr("Oct"), RowFormat{WaSafe::Radix::OCT, false}},
            {tr("Hex"), RowFormat{WaSafe::Radix::HEX, false}},
            {tr("Dec"), RowFormat{WaSafe::Radix::DEC, false}},
            {tr("Signed Dec"), RowFormat{WaSafe::Radix::DEC, true}},
    }};
    for (const auto& [text, format] : choices) {
        auto* action = menu->addAction(text);
        action->setCheckable(true);
        action->setChecked(format == current);
        connect(action, &QAction::triggered, this, [this, format] {
            for (std::size_t i = 0; i < selected_.size(); ++i) {
                if (selected_[i])
                    view_->setFormat(i, format);
            }
        });
    }
    // popup, а не exec: вложенный цикл событий посреди обработчика события колонки не нужен.
    menu->popup(globalPos);
}

std::optional<std::size_t> WavePanel::rowAt(int y) const {
    if (y < kRulerHeight)
        return std::nullopt;
    const auto row = static_cast<std::size_t>(std::max(vScroll_->value(), 0) + (y - kRulerHeight) / kRowHeight);
    if (row >= view_->rows().size())
        return std::nullopt;
    return row;
}

std::size_t WavePanel::insertionRow(int y) const {
    const int offset = (y - kRulerHeight + kRowHeight / 2) / kRowHeight;
    const auto row = std::max(vScroll_->value(), 0) + std::max(offset, 0);
    return std::min(static_cast<std::size_t>(row), view_->rows().size());
}

bool WavePanel::acceptsDrag(const QDropEvent& event) const {
    return event.source() != nullptr && event.mimeData() != nullptr && event.mimeData()->hasFormat(kSignalRefsMimeType);
}

int WavePanel::canvasY(QPoint panelPos) const {
    return canvas_->mapFrom(this, panelPos).y();
}

}  // namespace Phosphor
