#include "app/hierarchy_model.hpp"

#include <QMimeData>
#include <cstddef>
#include <utility>

#include "app/signal_mime.hpp"

namespace Phosphor {

namespace {

// Композит без собственного потока показывается своими элементами; у остального сигнала детей нет.
bool isExpandable(const WaSafe::Signal& signal) {
    return signal.isComposite() && !signal.hasOwnStream();
}

}  // namespace

HierarchyModel::HierarchyModel(Session& session, QObject* parent) : QAbstractItemModel{parent}, session_{&session} {
    for (const auto id : session.sourceIds()) {
        addRoot(id);
        const auto* info = session.info(id);
        auto& root = roots_.back();
        root.state = info->state;
        root.error = info->error;
        if (const auto* db = session.hierarchyDb(id))
            root.node->handle = db->root();
    }
    connect(&session, &Session::sourceAdded, this, &HierarchyModel::addRoot);
    connect(&session, &Session::sourceReady, this, &HierarchyModel::markReady);
    connect(&session, &Session::sourceFailed, this, &HierarchyModel::markFailed);
    connect(&session, &Session::sourceAboutToBeRemoved, this, &HierarchyModel::beginRemoveRoot);
    connect(&session, &Session::sourceRemoved, this, &HierarchyModel::endRemoveRoot);
}

std::optional<SignalRef> HierarchyModel::signalRef(const QModelIndex& index) const {
    if (!index.isValid())
        return std::nullopt;
    const auto* node = nodeOf(index);
    const auto* signal = ownStreamSignal(*node);
    if (signal == nullptr)
        return std::nullopt;
    return SignalRef{node->source, signal->fullPath()};
}

QModelIndex HierarchyModel::index(int row, int column, const QModelIndex& parent) const {
    if (!hasIndex(row, column, parent))
        return {};
    const auto r = static_cast<std::size_t>(row);
    const Node* node = parent.isValid() ? childrenOf(*nodeOf(parent))[r].get() : roots_[r].node.get();
    return createIndex(row, column, node);
}

QModelIndex HierarchyModel::parent(const QModelIndex& child) const {
    if (!child.isValid())
        return {};
    const Node* parent = nodeOf(child)->parent;
    if (parent == nullptr)
        return {};
    return createIndex(parent->row, 0, parent);
}

int HierarchyModel::rowCount(const QModelIndex& parent) const {
    if (parent.column() > 0)
        return 0;
    if (!parent.isValid())
        return static_cast<int>(roots_.size());
    return static_cast<int>(childCount(nodeOf(parent)->handle));
}

int HierarchyModel::columnCount(const QModelIndex& /*parent*/) const {
    return 1;
}

QVariant HierarchyModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid())
        return {};
    const auto* node = nodeOf(index);
    if (node->parent != nullptr)
        return role == Qt::DisplayRole ? QVariant{node->name} : QVariant{};

    const auto& root = roots_[static_cast<std::size_t>(node->row)];
    switch (role) {
        case Qt::DisplayRole:
            switch (root.state) {
                case SourceState::LOADING:
                    return node->name + tr(" (loading…)");
                case SourceState::FAILED:
                    return node->name + tr(" (error)");
                case SourceState::READY:
                    return node->name;
            }
            return node->name;
        case Qt::ToolTipRole:
            return root.state == SourceState::FAILED ? QVariant{root.error} : QVariant{};
        default:
            return {};
    }
}

Qt::ItemFlags HierarchyModel::flags(const QModelIndex& index) const {
    if (!index.isValid())
        return Qt::NoItemFlags;
    Qt::ItemFlags result = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (ownStreamSignal(*nodeOf(index)) != nullptr)
        result |= Qt::ItemIsDragEnabled;
    return result;
}

QStringList HierarchyModel::mimeTypes() const {
    return {kSignalRefsMimeType};
}

QMimeData* HierarchyModel::mimeData(const QModelIndexList& indexes) const {
    std::vector<SignalRef> refs;
    for (const auto& index : indexes) {
        if (auto ref = signalRef(index))
            refs.push_back(std::move(*ref));
    }
    if (refs.empty())
        return nullptr;
    auto mime = std::make_unique<QMimeData>();
    mime->setData(kSignalRefsMimeType, encodeSignalRefs(refs));
    return mime.release();
}

Qt::DropActions HierarchyModel::supportedDragActions() const {
    return Qt::CopyAction;
}

HierarchyModel::Node* HierarchyModel::nodeOf(const QModelIndex& index) {
    return static_cast<Node*>(index.internalPointer());
}

std::size_t HierarchyModel::childCount(const Handle& handle) {
    if (const auto* scope = std::get_if<WaSafe::Scope>(&handle))
        return scope->scopeCount() + scope->signalCount();
    if (const auto* signal = std::get_if<WaSafe::Signal>(&handle))
        return isExpandable(*signal) ? signal->childCount() : 0;
    return 0;
}

std::vector<std::unique_ptr<HierarchyModel::Node>>& HierarchyModel::childrenOf(Node& node) {
    if (node.built)
        return node.children;
    node.built = true;
    node.children.reserve(childCount(node.handle));
    const auto append = [&](auto handle) {
        auto child = std::make_unique<Node>();
        child->parent = &node;
        child->row = static_cast<int>(node.children.size());
        child->source = node.source;
        child->name = QString::fromUtf8(handle.name());
        child->handle = handle;
        node.children.push_back(std::move(child));
    };
    if (const auto* scope = std::get_if<WaSafe::Scope>(&node.handle)) {
        for (const auto nested : scope->scopes())
            append(nested);
        for (const auto signal : scope->signals())
            append(signal);
    }
    else if (const auto* signal = std::get_if<WaSafe::Signal>(&node.handle);
            signal != nullptr && isExpandable(*signal)) {
        for (const auto element : signal->children())
            append(element);
    }
    // Число детей у хэндла и при обходе должно совпадать: rowCount берёт первое, а index() — второе.
    Q_ASSERT(node.children.size() == childCount(node.handle));
    return node.children;
}

const WaSafe::Signal* HierarchyModel::ownStreamSignal(const Node& node) {
    const auto* signal = std::get_if<WaSafe::Signal>(&node.handle);
    return signal != nullptr && signal->hasOwnStream() ? signal : nullptr;
}

void HierarchyModel::addRoot(SourceId id) {
    const auto* info = session_->info(id);
    if (info == nullptr)
        return;
    auto node = std::make_unique<Node>();
    node->row = static_cast<int>(roots_.size());
    node->source = id;
    node->name = info->label;
    beginInsertRows({}, node->row, node->row);
    roots_.push_back(Root{SourceState::LOADING, {}, std::move(node)});
    endInsertRows();
}

void HierarchyModel::markReady(SourceId id) {
    const auto row = rootRow(id);
    const auto* db = session_->hierarchyDb(id);
    if (!row || db == nullptr)
        return;
    auto& root = roots_[static_cast<std::size_t>(*row)];
    root.state = SourceState::READY;
    root.error.clear();
    const auto rootIndex = index(*row, 0, {});
    Q_EMIT dataChanged(rootIndex, rootIndex, {Qt::DisplayRole, Qt::ToolTipRole});

    // У загружающегося корня детей нет, поэтому появление иерархии — вставка строк.
    const Handle top = db->root();
    const auto count = childCount(top);
    if (count == 0) {
        root.node->handle = top;
        return;
    }
    beginInsertRows(rootIndex, 0, static_cast<int>(count) - 1);
    root.node->handle = top;
    endInsertRows();
}

void HierarchyModel::markFailed(SourceId id, const QString& error) {
    const auto row = rootRow(id);
    if (!row)
        return;
    auto& root = roots_[static_cast<std::size_t>(*row)];
    root.state = SourceState::FAILED;
    root.error = error;
    const auto rootIndex = index(*row, 0, {});
    Q_EMIT dataChanged(rootIndex, rootIndex, {Qt::DisplayRole, Qt::ToolTipRole});
}

void HierarchyModel::beginRemoveRoot(SourceId id) {
    const auto row = rootRow(id);
    if (!row)
        return;
    Q_ASSERT(!removing_);
    // Пока идёт удаление строк, представления ещё обходят поддерево готового источника через его
    // хэндлы, поэтому его иерархия обязана быть жива. У загружающегося и сломанного иерархии нет.
    Q_ASSERT(
            roots_[static_cast<std::size_t>(*row)].state != SourceState::READY || session_->hierarchyDb(id) != nullptr);
    beginRemoveRows({}, *row, *row);
    removing_ = id;
    roots_.erase(roots_.begin() + *row);
    for (auto i = static_cast<std::size_t>(*row); i < roots_.size(); ++i)
        roots_[i].node->row = static_cast<int>(i);
}

void HierarchyModel::endRemoveRoot(SourceId id) {
    // Корень есть у каждого источника сессии, поэтому sourceRemoved всегда закрывает начатое удаление.
    Q_ASSERT(removing_ == id);
    if (removing_ != id)
        return;
    removing_.reset();
    endRemoveRows();
}

std::optional<int> HierarchyModel::rootRow(SourceId id) const {
    for (std::size_t i = 0; i < roots_.size(); ++i) {
        if (roots_[i].node->source == id)
            return static_cast<int>(i);
    }
    return std::nullopt;
}

}  // namespace Phosphor
