#pragma once

#include <QAbstractItemModel>
#include <QModelIndex>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

#include "core/session.hpp"
#include "core/source_id.hpp"
#include "core/view.hpp"
#include "wasafe/model/scope.hpp"
#include "wasafe/model/signal.hpp"

class QMimeData;

namespace Phosphor {

// Иерархия всех источников сессии одним деревом из одной колонки. Корни — источники в порядке
// открытия, под ними scope'ы и сигналы дампа; композит без собственного потока раскрывается в
// свои элементы. Иерархия готового дампа неизменна, поэтому число детей узла известно сразу, а
// сами узлы создаются при первом обращении к детям: дерево большого дампа целиком не нужно.
// Модель ходит только по иерархии и следует за сигналами сессии, поэтому живёт в её потоке, а
// сессия должна пережить модель.
class HierarchyModel : public QAbstractItemModel {
    Q_OBJECT

public:
    explicit HierarchyModel(Session& session, QObject* parent = nullptr);

    // Значение есть только у сигнала с собственным потоком: только такой можно показать строкой.
    [[nodiscard]] std::optional<SignalRef> signalRef(const QModelIndex& index) const;

    [[nodiscard]] QModelIndex index(int row, int column, const QModelIndex& parent) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex& child) const override;
    [[nodiscard]] int rowCount(const QModelIndex& parent) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;

    [[nodiscard]] QStringList mimeTypes() const override;
    // Сигналы без signalRef пропускаются; nullptr, если не осталось ни одного.
    [[nodiscard]] QMimeData* mimeData(const QModelIndexList& indexes) const override;
    [[nodiscard]] Qt::DropActions supportedDragActions() const override;

private:
    // Хэндл, по которому строятся дети: Scope, Signal или ничего (корень, пока источник не готов).
    using Handle = std::variant<std::monostate, WaSafe::Scope, WaSafe::Signal>;

    // Хэндл указывает в БД источника; узлы источника удаляются по sourceAboutToBeRemoved, пока
    // БД ещё жива. Имя хранится готовой строкой: его спрашивают при каждой отрисовке строки.
    struct Node {
        Node* parent = nullptr;
        int row = 0;
        SourceId source = 0;
        QString name;
        Handle handle;
        // Пусты, пока не built.
        std::vector<std::unique_ptr<Node>> children;
        bool built = false;
    };

    struct Root {
        SourceState state = SourceState::LOADING;
        QString error;
        std::unique_ptr<Node> node;
    };

private:
    static Node* nodeOf(const QModelIndex& index);
    static std::size_t childCount(const Handle& handle);
    // Создаёт детей узла при первом вызове.
    static std::vector<std::unique_ptr<Node>>& childrenOf(Node& node);
    // Есть только у сигнала: только его можно показать строкой вида.
    static const WaSafe::Signal* ownStreamSignal(const Node& node);

    void addRoot(SourceId id);
    void markReady(SourceId id);
    void markFailed(SourceId id, const QString& error);
    // Строки удаляются между sourceAboutToBeRemoved и sourceRemoved: узлы уходят, пока БД ещё жива.
    void beginRemoveRoot(SourceId id);
    void endRemoveRoot(SourceId id);
    [[nodiscard]] std::optional<int> rootRow(SourceId id) const;

private:
    Session* session_;
    std::vector<Root> roots_;
    // Источник, для которого вызван beginRemoveRows и ещё не вызван endRemoveRows.
    std::optional<SourceId> removing_;
};

}  // namespace Phosphor
