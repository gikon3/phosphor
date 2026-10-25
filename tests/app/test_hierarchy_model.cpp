#include <gtest/gtest.h>

#include <QAbstractItemModelTester>
#include <QAbstractItemView>
#include <QCoreApplication>
#include <QItemSelectionModel>
#include <QMimeData>
#include <QModelIndex>
#include <QPersistentModelIndex>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTreeView>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "app/hierarchy_model.hpp"
#include "app/signal_mime.hpp"
#include "core/session.hpp"
#include "core/store_cache.hpp"
#include "core/view.hpp"
#include "support/temp_files.hpp"

namespace {

constexpr int kWaitMs = 5000;
constexpr auto kMimeType = "application/x-phosphor-signals";

// Модель вместе с проверкой Qt: любое нарушение контракта QAbstractItemModel обрывает тест.
struct CheckedModel {
    explicit CheckedModel(Phosphor::Session& session) :
            model{session}, tester{&model, QAbstractItemModelTester::FailureReportingMode::Fatal} {}

    Phosphor::HierarchyModel model;
    QAbstractItemModelTester tester;
};

QString text(const QModelIndex& index) {
    return index.data(Qt::DisplayRole).toString();
}

QStringList childTexts(const Phosphor::HierarchyModel& model, const QModelIndex& parent) {
    QStringList result;
    for (int row = 0; row < model.rowCount(parent); ++row)
        result << text(model.index(row, 0, parent));
    return result;
}

QModelIndex child(const Phosphor::HierarchyModel& model, const QModelIndex& parent, const QString& name) {
    for (int row = 0; row < model.rowCount(parent); ++row) {
        const auto index = model.index(row, 0, parent);
        if (text(index) == name)
            return index;
    }
    return {};
}

Phosphor::SourceId openAndWait(Phosphor::Session& session, const std::filesystem::path& path) {
    QSignalSpy ready{&session, &Phosphor::Session::sourceReady};
    const auto id = session.openDump(path);
    EXPECT_TRUE(ready.wait(kWaitMs));
    return id;
}

}  // namespace

TEST(HierarchyModel, ShowsSourceScopesSignals) {
    const Phosphor::Test::TempDir tmp;
    const auto dump = Phosphor::Test::writeFile(tmp.path() / "small.vcd", Phosphor::Test::kSmallVcd);
    Phosphor::Session session{Phosphor::StoreCache{tmp.path() / "cache"}};
    const CheckedModel checked{session};
    const auto& model = checked.model;
    QSignalSpy ready{&session, &Phosphor::Session::sourceReady};

    session.openDump(dump);

    ASSERT_EQ(model.rowCount({}), 1);
    EXPECT_EQ(text(model.index(0, 0, {})), QString{"small.vcd (loading…)"});

    ASSERT_TRUE(ready.wait(kWaitMs));

    ASSERT_EQ(model.rowCount({}), 1);
    const auto root = model.index(0, 0, {});
    EXPECT_EQ(text(root), QString{"small.vcd"});
    EXPECT_EQ(childTexts(model, root), QStringList{"top"});
    const auto top = child(model, root, "top");
    ASSERT_TRUE(top.isValid());
    EXPECT_EQ(childTexts(model, top), (QStringList{"clk", "data", "vout"}));
}

TEST(HierarchyModel, SignalRefForLeaf) {
    const Phosphor::Test::TempDir tmp;
    const auto dump = Phosphor::Test::writeFile(tmp.path() / "small.vcd", Phosphor::Test::kSmallVcd);
    Phosphor::Session session{Phosphor::StoreCache{tmp.path() / "cache"}};
    const CheckedModel checked{session};
    const auto& model = checked.model;

    const auto id = openAndWait(session, dump);

    const auto root = model.index(0, 0, {});
    const auto top = child(model, root, "top");
    const auto data = child(model, top, "data");
    ASSERT_TRUE(data.isValid());
    EXPECT_EQ(model.signalRef(data), (Phosphor::SignalRef{id, "top.data"}));
    EXPECT_EQ(model.signalRef(top), std::nullopt);
    EXPECT_EQ(model.signalRef(root), std::nullopt);
}

TEST(HierarchyModel, MimeRoundTrip) {
    const std::vector<Phosphor::SignalRef> refs{{1, "top.clk"}, {42, "top.u_cpu.regs[3]"}};
    EXPECT_EQ(Phosphor::decodeSignalRefs(Phosphor::encodeSignalRefs(refs)), refs);

    const Phosphor::Test::TempDir tmp;
    const auto dump = Phosphor::Test::writeFile(tmp.path() / "small.vcd", Phosphor::Test::kSmallVcd);
    Phosphor::Session session{Phosphor::StoreCache{tmp.path() / "cache"}};
    const CheckedModel checked{session};
    const auto& model = checked.model;
    const auto id = openAndWait(session, dump);

    const auto top = child(model, model.index(0, 0, {}), "top");
    const auto clk = child(model, top, "clk");
    const auto data = child(model, top, "data");
    ASSERT_TRUE(clk.isValid());
    ASSERT_TRUE(data.isValid());

    EXPECT_TRUE(model.mimeTypes().contains(kMimeType));
    const std::unique_ptr<QMimeData> mime{model.mimeData({clk, data})};
    ASSERT_NE(mime, nullptr);
    ASSERT_TRUE(mime->hasFormat(kMimeType));
    EXPECT_EQ(Phosphor::decodeSignalRefs(mime->data(kMimeType)),
            (std::vector<Phosphor::SignalRef>{{id, "top.clk"}, {id, "top.data"}}));
}

TEST(HierarchyModel, MalformedMimeDecodesToNothing) {
    EXPECT_TRUE(Phosphor::decodeSignalRefs("not json").empty());
    EXPECT_TRUE(Phosphor::decodeSignalRefs(R"({"source": 1, "path": "top.clk"})").empty());
    // Номер источника: отрицательный, дробный, больше SourceId, строкой или отсутствует; пустой путь.
    EXPECT_EQ(Phosphor::decodeSignalRefs(R"([
                {"source": -1, "path": "a"},
                {"source": 3.5, "path": "b"},
                {"source": 4294967296, "path": "c"},
                {"source": "3", "path": "d"},
                {"path": "e"},
                {"source": 3, "path": ""},
                {"source": 4294967295, "path": "f"},
                {"source": 2, "path": "g"}
            ])"),
            (std::vector<Phosphor::SignalRef>{{4'294'967'295U, "f"}, {2, "g"}}));
}

TEST(HierarchyModel, FailedSourceShowsError) {
    const Phosphor::Test::TempDir tmp;
    const auto dump = Phosphor::Test::writeFile(tmp.path() / "bad.vcd", Phosphor::Test::kBrokenVcd);
    Phosphor::Session session{Phosphor::StoreCache{tmp.path() / "cache"}};
    const CheckedModel checked{session};
    const auto& model = checked.model;
    QSignalSpy failed{&session, &Phosphor::Session::sourceFailed};

    session.openDump(dump);
    ASSERT_TRUE(failed.wait(kWaitMs));

    ASSERT_EQ(model.rowCount({}), 1);
    const auto root = model.index(0, 0, {});
    EXPECT_EQ(text(root), QString{"bad.vcd (error)"});
    EXPECT_EQ(root.data(Qt::ToolTipRole).toString(), failed.at(0).at(1).toString());
    EXPECT_TRUE(childTexts(model, root).isEmpty());
}

TEST(HierarchyModel, RemovedSourceDropsItsSubtree) {
    const Phosphor::Test::TempDir tmp;
    const auto first = Phosphor::Test::writeFile(tmp.path() / "first.vcd", Phosphor::Test::kSmallVcd);
    const auto second = Phosphor::Test::writeFile(tmp.path() / "second.vcd", Phosphor::Test::kSmallVcd);
    Phosphor::Session session{Phosphor::StoreCache{tmp.path() / "cache"}};
    const CheckedModel checked{session};
    const auto& model = checked.model;
    const auto firstId = openAndWait(session, first);
    const auto secondId = openAndWait(session, second);
    // Раскрытые узлы первого источника должны исчезнуть вместе с ним.
    const QPersistentModelIndex firstData{child(model, child(model, model.index(0, 0, {}), "top"), "data")};
    ASSERT_TRUE(firstData.isValid());

    session.removeSource(firstId);

    EXPECT_FALSE(firstData.isValid());
    ASSERT_EQ(model.rowCount({}), 1);
    const auto root = model.index(0, 0, {});
    EXPECT_EQ(text(root), QString{"second.vcd"});
    const auto data = child(model, child(model, root, "top"), "data");
    EXPECT_EQ(model.signalRef(data), (Phosphor::SignalRef{secondId, "top.data"}));
}

TEST(HierarchyModel, CancelledLoadingSourceDisappears) {
    const Phosphor::Test::TempDir tmp;
    const auto dump = Phosphor::Test::writeFile(tmp.path() / "small.vcd", Phosphor::Test::kSmallVcd);
    Phosphor::Session session{Phosphor::StoreCache{tmp.path() / "cache"}};
    const CheckedModel checked{session};
    const auto& model = checked.model;
    QSignalSpy removed{&session, &Phosphor::Session::sourceRemoved};

    const auto id = session.openDump(dump);
    ASSERT_EQ(model.rowCount({}), 1);
    session.cancelLoad(id);

    ASSERT_TRUE(removed.wait(kWaitMs));
    EXPECT_EQ(model.rowCount({}), 0);
}

// Представление держит раскрытые узлы, выделение и текущий индекс удаляемого поддерева.
TEST(HierarchyModel, RemoveWithExpandedTreeView) {
    const Phosphor::Test::TempDir tmp;
    const auto first = Phosphor::Test::writeFile(tmp.path() / "first.vcd", Phosphor::Test::kSmallVcd);
    const auto second = Phosphor::Test::writeFile(tmp.path() / "second.vcd", Phosphor::Test::kSmallVcd);
    Phosphor::Session session{Phosphor::StoreCache{tmp.path() / "cache"}};
    CheckedModel checked{session};
    const auto firstId = openAndWait(session, first);
    openAndWait(session, second);
    QTreeView tree;
    tree.setModel(&checked.model);
    tree.setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree.show();
    tree.expandAll();
    const auto data = child(checked.model, child(checked.model, checked.model.index(0, 0, {}), "top"), "data");
    ASSERT_TRUE(data.isValid());
    tree.setCurrentIndex(data);
    tree.selectionModel()->select(data, QItemSelectionModel::ClearAndSelect);
    QCoreApplication::processEvents();

    session.removeSource(firstId);
    QCoreApplication::processEvents();

    ASSERT_EQ(checked.model.rowCount({}), 1);
    EXPECT_EQ(text(checked.model.index(0, 0, {})), QString{"second.vcd"});
    EXPECT_TRUE(tree.selectionModel()->selectedIndexes().isEmpty());
}
