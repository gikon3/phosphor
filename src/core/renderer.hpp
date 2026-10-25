#pragma once

#include <QEvent>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "core/digital_frame.hpp"
#include "core/source_id.hpp"
#include "core/time_axis.hpp"
#include "core/time_mapper.hpp"
#include "core/value_format.hpp"
#include "wasafe/model/database.hpp"

namespace Phosphor {

// Строка кадра. NodeId переносим между копиями БД одного источника; невалидный NodeId —
// строка без данных.
struct RowRequest {
    SourceId source = 0;
    WaSafe::NodeId node;
    RowFormat format;
};

struct FrameRequest {
    std::uint64_t generation = 0;
    TimeWindow window;
    int widthPx = 0;
    std::vector<RowRequest> rows;
    std::optional<GlobalTime> cursorA;
    std::optional<GlobalTime> cursorB;
};

// Значения курсоров — текст значения в момент курсора; пустой, если курсор не задан или у
// строки нет данных.
struct RowResult {
    DigitalFrame frame;
    std::string valueA;
    std::string valueB;
};

// Строки идут в порядке строк запроса.
struct FrameResult {
    std::uint64_t generation = 0;
    TimeWindow window;
    int widthPx = 0;
    std::vector<RowResult> rows;
};

// Строит кадры в одном рабочем потоке. Копии БД источников передаются в поток и используются
// только в нём, поэтому значения никогда не читаются в потоке объекта. Команды источников
// выполняются в порядке поступления и до кадра, который поток возьмёт следующим. Готовый кадр
// отдаётся через очередь событий сигналом frameReady в потоке объекта; если поток объекта не
// забрал кадр до появления следующего, отдаётся только следующий.
class Renderer : public QObject {
    Q_OBJECT

public:
    explicit Renderer(QObject* parent = nullptr);
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;
    // Останавливает поток и дожидается его: поток обращается к полям объекта. Недоставленные
    // кадры пропадают.
    ~Renderer() override;

    // Источник с тем же id заменяется.
    void addSource(SourceId id, WaSafe::Database db, TimeAxis axis);

    // Неизвестный id ничего не меняет.
    void removeSource(SourceId id);

    // Побеждает последний запрос: ещё не взятый в работу заменяется новым, а строящийся кадр
    // бросается между строками и не отдаётся. Номер поколения должен расти; запрос с номером
    // не больше уже принятого отбрасывается, поэтому кадры приходят в порядке поколений.
    // Строка неизвестного источника, с невалидным узлом или со сбоем чтения значений остаётся
    // пустой, а кадр всё равно отдаётся.
    void request(FrameRequest r);

Q_SIGNALS:
    void frameReady(const Phosphor::FrameResult& frame);

protected:
    // Уведомление о готовом кадре — событие без данных, а сам кадр передаётся под mutex_. Так вся
    // передача между потоками упорядочена этим мьютексом и видна анализатору гонок, даже когда
    // сам Qt собран без инструментирования.
    void customEvent(QEvent* event) override;

private:
    struct Source {
        WaSafe::Database db;
        TimeAxis axis;
        // Сбой чтения сообщается один раз на источник, а не на каждый кадр.
        bool failureReported = false;
    };

    struct AddSource {
        SourceId id = 0;
        Source source;
    };

    struct RemoveSource {
        SourceId id = 0;
    };

    using Command = std::variant<AddSource, RemoveSource>;
    using Sources = std::map<SourceId, Source>;

private:
    static void execute(Sources& sources, Command& command);
    // Сбой чтения строки ловится: он не должен ни ронять поток, ни терять кадр.
    static RowResult renderRow(Sources& sources, const FrameRequest& r, const RowRequest& row);
    static void reportFailure(SourceId id, Source& source, const QString& what);

    void run(const std::stop_token& stop);
    // nullopt, если кадр брошен ради более нового запроса или остановки.
    std::optional<FrameResult> render(Sources& sources, const FrameRequest& r, const std::stop_token& stop);
    [[nodiscard]] bool superseded(const std::stop_token& stop);
    void deliver(FrameResult frame);

private:
    std::mutex mutex_;
    std::condition_variable_any wakeup_;
    std::deque<Command> commands_;
    std::optional<FrameRequest> pending_;
    std::optional<std::uint64_t> lastGeneration_;
    // Непуст ровно тогда, когда в очереди событий стоит уведомление о нём.
    std::optional<FrameResult> ready_;
    // Последним: поток запускается, когда остальные поля уже созданы.
    std::jthread worker_;
};

}  // namespace Phosphor

Q_DECLARE_METATYPE(Phosphor::FrameResult)
