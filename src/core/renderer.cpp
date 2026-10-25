#include "core/renderer.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QEvent>
#include <QString>
#include <deque>
#include <exception>
#include <memory>
#include <utility>

#include "core/error.hpp"

namespace Phosphor {

namespace { const auto kReadyEvent = static_cast<QEvent::Type>(QEvent::registerEventType()); }

Renderer::Renderer(QObject* parent) : QObject{parent}, worker_{[this](const std::stop_token& stop) { run(stop); }} {
    qRegisterMetaType<FrameResult>();
}

Renderer::~Renderer() {
    // Поток держит ссылки на мьютекс и очередь, поэтому дожидаться его надо здесь, пока поля
    // живы. Уведомления, которые он успел поставить в очередь событий, Qt отбрасывает вместе с
    // объектом.
    worker_.request_stop();
    worker_.join();
}

void Renderer::addSource(SourceId id, WaSafe::Database db, TimeAxis axis) {
    {
        const std::scoped_lock lock{mutex_};
        commands_.emplace_back(AddSource{id, Source{std::move(db), axis}});
    }
    wakeup_.notify_one();
}

void Renderer::removeSource(SourceId id) {
    {
        const std::scoped_lock lock{mutex_};
        commands_.emplace_back(RemoveSource{id});
    }
    wakeup_.notify_one();
}

void Renderer::request(FrameRequest r) {
    {
        const std::scoped_lock lock{mutex_};
        if (lastGeneration_ && r.generation <= *lastGeneration_) {
            qWarning() << "renderer dropped stale frame request" << r.generation << "after" << *lastGeneration_;
            return;
        }
        lastGeneration_ = r.generation;
        pending_ = std::move(r);
    }
    wakeup_.notify_one();
}

void Renderer::customEvent(QEvent* event) {
    std::optional<FrameResult> frame;
    bool isReady = false;
    {
        // Наш код обращается к событию только под мьютексом: событие создано в рабочем потоке под
        // ним же, и захват упорядочивает это создание с чтением здесь и удалением события в Qt.
        const std::scoped_lock lock{mutex_};
        isReady = event->type() == kReadyEvent;
        if (isReady)
            frame = std::exchange(ready_, std::nullopt);
    }
    if (!isReady)
        QObject::customEvent(event);
    else if (frame)
        Q_EMIT frameReady(*frame);
}

void Renderer::execute(Sources& sources, Command& command) {
    if (auto* add = std::get_if<AddSource>(&command))
        sources.insert_or_assign(add->id, std::move(add->source));
    else
        sources.erase(std::get<RemoveSource>(command).id);
}

RowResult Renderer::renderRow(Sources& sources, const FrameRequest& r, const RowRequest& row) {
    const auto it = sources.find(row.source);
    if (it == sources.end() || !row.node.valid())
        return {};
    auto& source = it->second;
    const auto valueAt = [&](const std::optional<GlobalTime>& cursor) {
        return cursor ? formatValue(source.db.valueAt(row.node, source.axis.toLocalFloor(*cursor)), row.format)
                      : std::string{};
    };
    // Ленивый store читает файл и может бросить на вводе-выводе; кроме того, узел может не
    // принадлежать иерархии источника.
    try {
        return {buildDigitalFrame(source.db, row.node, source.axis, r.window, r.widthPx, row.format),
                valueAt(r.cursorA), valueAt(r.cursorB)};
    }
    catch (const std::exception& e) {
        reportFailure(row.source, source, errorText(e));
    }
    catch (...) {
        reportFailure(row.source, source, QStringLiteral("unknown error"));
    }
    return {};
}

void Renderer::reportFailure(SourceId id, Source& source, const QString& what) {
    if (source.failureReported)
        return;
    source.failureReported = true;
    qWarning().noquote() << "renderer failed to read values of source" << id << ":" << what
                         << "(further failures of this source are not reported)";
}

void Renderer::run(const std::stop_token& stop) {
    // Источники — локальное состояние потока: их БД ни в каком другом потоке не используются.
    Sources sources;
    std::unique_lock lock{mutex_};
    while (true) {
        wakeup_.wait(lock, stop, [this] { return !commands_.empty() || pending_.has_value(); });
        if (stop.stop_requested())
            return;
        // Ничто не должно выйти из функции потока: исключение там вызывает std::terminate. Сбой
        // команды или сборки кадра (по сути, нехватка памяти) теряет только их. Локальные живут
        // внутри try и уничтожаются до повторного захвата мьютекса: запрос и удаляемые БД
        // освобождаются вне него.
        try {
            std::deque<Command> commands;
            std::optional<FrameRequest> request;
            commands.swap(commands_);
            request.swap(pending_);
            lock.unlock();
            for (auto& command : commands)
                execute(sources, command);
            if (request) {
                if (auto frame = render(sources, *request, stop))
                    deliver(std::move(*frame));
            }
        }
        catch (const std::exception& e) {
            qWarning().noquote() << "renderer dropped a command or frame:" << errorText(e);
        }
        catch (...) {
            qWarning() << "renderer dropped a command or frame: unknown error";
        }
        // Сбой мог случиться ещё до освобождения мьютекса.
        if (!lock.owns_lock())
            lock.lock();
    }
}

std::optional<FrameResult> Renderer::render(Sources& sources, const FrameRequest& r, const std::stop_token& stop) {
    FrameResult frame{r.generation, r.window, r.widthPx, {}};
    frame.rows.reserve(r.rows.size());
    for (const auto& row : r.rows) {
        if (superseded(stop))
            return std::nullopt;
        frame.rows.push_back(renderRow(sources, r, row));
    }
    return frame;
}

bool Renderer::superseded(const std::stop_token& stop) {
    if (stop.stop_requested())
        return true;
    // Принимаются только запросы с бо́льшим номером, поэтому любой ожидающий новее текущего.
    const std::scoped_lock lock{mutex_};
    return pending_.has_value();
}

void Renderer::deliver(FrameResult frame) {
    // Объявлен до блокировки, чтобы вытесненный кадр уничтожался уже после неё.
    std::optional<FrameResult> replaced;
    const std::scoped_lock lock{mutex_};
    replaced = std::exchange(ready_, std::move(frame));
    // Прежний кадр ещё не забран — уведомление о нём уже стоит в очереди.
    if (replaced)
        return;
    // Уведомление ставится под mutex_: событие создаётся здесь, а удаляется в потоке объекта после
    // customEvent, и только захват mutex_ в customEvent упорядочивает эти действия для
    // анализатора гонок. Если поставить его не удалось, кадр снимается: иначе ready_ остался бы
    // непустым без уведомления, и все следующие кадры застревали бы в нём.
    try {
        QCoreApplication::postEvent(this, std::make_unique<QEvent>(kReadyEvent).release());
    }
    catch (...) {
        ready_.reset();
        throw;
    }
}

}  // namespace Phosphor
