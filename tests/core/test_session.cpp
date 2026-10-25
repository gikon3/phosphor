#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string_view>
#include <tuple>
#include <vector>

#include "core/error.hpp"
#include "core/session.hpp"
#include "core/store_cache.hpp"
#include "support/temp_files.hpp"

namespace {

constexpr int kWaitMs = 5000;

// Дамп в пикосекундах, который начинается позже kSmallVcd и заканчивается позже него.
constexpr std::string_view kPicosecondVcd =
        "$timescale 1ps $end\n$scope module top $end\n$var wire 1 ! a $end\n$upscope $end\n"
        "$enddefinitions $end\n#5000\n1!\n#50000\n0!\n";

// Ждёт, пока сигналов в сумме по spies не станет не меньше count, в каком бы порядке они ни пришли.
bool waitForTotal(const std::vector<const QSignalSpy*>& spies, qsizetype count) {
    return QTest::qWaitFor(
            [&] {
                qsizetype sum = 0;
                for (const auto* spy : spies)
                    sum += spy->count();
                return sum >= count;
            },
            kWaitMs);
}

// Открывает дамп и ждёт итога его загрузки.
Phosphor::SourceId openAndWait(Phosphor::Session& session, const std::filesystem::path& path) {
    const QSignalSpy ready{&session, &Phosphor::Session::sourceReady};
    const QSignalSpy failed{&session, &Phosphor::Session::sourceFailed};
    const auto id = session.openDump(path);
    EXPECT_TRUE(waitForTotal({&ready, &failed}, 1));
    return id;
}

Phosphor::SourceId idArg(const QSignalSpy& spy, qsizetype i) {
    return spy.at(i).at(0).value<Phosphor::SourceId>();
}

}  // namespace

TEST(Session, LoadsDump) {
    const Phosphor::Test::TempDir tmp;
    const auto dump = Phosphor::Test::writeFile(tmp.path() / "a.vcd", Phosphor::Test::kSmallVcd);
    Phosphor::Session session{Phosphor::StoreCache{tmp.path() / "cache"}};
    const QSignalSpy added{&session, &Phosphor::Session::sourceAdded};
    QSignalSpy ready{&session, &Phosphor::Session::sourceReady};

    const auto id = session.openDump(dump);

    ASSERT_EQ(added.count(), 1);
    EXPECT_EQ(idArg(added, 0), id);
    EXPECT_EQ(session.sourceIds(), std::vector{id});
    ASSERT_NE(session.info(id), nullptr);
    EXPECT_EQ(session.info(id)->state, Phosphor::SourceState::LOADING);
    EXPECT_EQ(session.hierarchyDb(id), nullptr);
    EXPECT_FALSE(session.totalRange().has_value());

    ASSERT_TRUE(ready.wait(kWaitMs));

    EXPECT_EQ(idArg(ready, 0), id);
    const auto* info = session.info(id);
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->id, id);
    EXPECT_EQ(info->state, Phosphor::SourceState::READY);
    EXPECT_EQ(info->label, QString{"a.vcd"});
    EXPECT_EQ(info->path, dump);
    EXPECT_TRUE(info->error.isEmpty());
    const auto* db = session.hierarchyDb(id);
    ASSERT_NE(db, nullptr);
    EXPECT_TRUE(db->find("top.data").has_value());
    const auto axis = session.axis(id);
    ASSERT_TRUE(axis.has_value());
    const auto range = session.totalRange();
    ASSERT_TRUE(range.has_value());
    EXPECT_EQ(range->begin, 0);
    EXPECT_GE(range->end, axis->toGlobal(20));
}

TEST(Session, FailedSourceDoesNotAffectOthers) {
    const Phosphor::Test::TempDir tmp;
    const auto goodPath = Phosphor::Test::writeFile(tmp.path() / "good.vcd", Phosphor::Test::kSmallVcd);
    const auto badPath = Phosphor::Test::writeFile(tmp.path() / "bad.vcd", Phosphor::Test::kBrokenVcd);
    Phosphor::Session session{Phosphor::StoreCache{tmp.path() / "cache"}};
    const QSignalSpy ready{&session, &Phosphor::Session::sourceReady};
    const QSignalSpy failed{&session, &Phosphor::Session::sourceFailed};

    const auto good = session.openDump(goodPath);
    const auto bad = session.openDump(badPath);

    ASSERT_TRUE(waitForTotal({&ready, &failed}, 2));
    ASSERT_EQ(failed.count(), 1);
    EXPECT_EQ(idArg(failed, 0), bad);
    EXPECT_FALSE(failed.at(0).at(1).toString().isEmpty());
    ASSERT_EQ(ready.count(), 1);
    EXPECT_EQ(idArg(ready, 0), good);

    EXPECT_EQ(session.sourceIds(), (std::vector{good, bad}));
    ASSERT_NE(session.info(bad), nullptr);
    EXPECT_EQ(session.info(bad)->state, Phosphor::SourceState::FAILED);
    EXPECT_FALSE(session.info(bad)->error.isEmpty());
    EXPECT_EQ(session.hierarchyDb(bad), nullptr);
    EXPECT_FALSE(session.axis(bad).has_value());
    EXPECT_THROW(std::ignore = session.takeRenderDatabase(bad), Phosphor::Error);

    ASSERT_NE(session.info(good), nullptr);
    EXPECT_EQ(session.info(good)->state, Phosphor::SourceState::READY);
    EXPECT_TRUE(session.totalRange().has_value());
    const auto render = session.takeRenderDatabase(good);
    EXPECT_TRUE(render.find("top.data").has_value());
    EXPECT_THROW(std::ignore = session.takeRenderDatabase(good), Phosphor::Error);
    ASSERT_NE(session.hierarchyDb(good), nullptr);
    EXPECT_TRUE(session.hierarchyDb(good)->find("top.clk").has_value());
}

TEST(Session, CancelRemovesSource) {
    const Phosphor::Test::TempDir tmp;
    const auto dump = Phosphor::Test::writeFile(tmp.path() / "a.vcd", Phosphor::Test::kSmallVcd);
    Phosphor::Session session{Phosphor::StoreCache{tmp.path() / "cache"}};
    const QSignalSpy ready{&session, &Phosphor::Session::sourceReady};
    QSignalSpy removed{&session, &Phosphor::Session::sourceRemoved};

    const auto id = session.openDump(dump);
    session.cancelLoad(id);

    ASSERT_TRUE(removed.wait(kWaitMs));
    EXPECT_EQ(idArg(removed, 0), id);
    EXPECT_EQ(ready.count(), 0);
    EXPECT_EQ(session.info(id), nullptr);
    const auto ids = session.sourceIds();
    EXPECT_EQ(std::ranges::find(ids, id), ids.end());
}

TEST(Session, RemoveReadySourceIsImmediate) {
    const Phosphor::Test::TempDir tmp;
    const auto dump = Phosphor::Test::writeFile(tmp.path() / "a.vcd", Phosphor::Test::kSmallVcd);
    Phosphor::Session session{Phosphor::StoreCache{tmp.path() / "cache"}};
    const auto id = openAndWait(session, dump);
    ASSERT_NE(session.info(id), nullptr);
    ASSERT_EQ(session.info(id)->state, Phosphor::SourceState::READY);
    const QSignalSpy removed{&session, &Phosphor::Session::sourceRemoved};

    session.removeSource(id);

    ASSERT_EQ(removed.count(), 1);
    EXPECT_EQ(idArg(removed, 0), id);
    EXPECT_TRUE(session.sourceIds().empty());
    EXPECT_EQ(session.info(id), nullptr);
    EXPECT_EQ(session.hierarchyDb(id), nullptr);
    EXPECT_FALSE(session.totalRange().has_value());
}

TEST(Session, RemoveFailedSourceIsImmediate) {
    const Phosphor::Test::TempDir tmp;
    const auto dump = Phosphor::Test::writeFile(tmp.path() / "bad.vcd", Phosphor::Test::kBrokenVcd);
    Phosphor::Session session{Phosphor::StoreCache{tmp.path() / "cache"}};
    const auto id = openAndWait(session, dump);
    ASSERT_NE(session.info(id), nullptr);
    ASSERT_EQ(session.info(id)->state, Phosphor::SourceState::FAILED);
    const QSignalSpy removed{&session, &Phosphor::Session::sourceRemoved};

    session.removeSource(id);

    ASSERT_EQ(removed.count(), 1);
    EXPECT_EQ(idArg(removed, 0), id);
    EXPECT_TRUE(session.sourceIds().empty());
    EXPECT_EQ(session.info(id), nullptr);
}

namespace {

// Сигнал удаления вместе с тем, что сессия отдаёт по источнику в момент сигнала.
struct RemovalEvent {
    bool aboutToBeRemoved = false;
    Phosphor::SourceId id = 0;
    bool hasInfo = false;
    bool hasHierarchy = false;

    friend bool operator==(const RemovalEvent&, const RemovalEvent&) = default;
};

}  // namespace

TEST(Session, AboutToBeRemovedSeesSourceData) {
    const Phosphor::Test::TempDir tmp;
    const auto dump = Phosphor::Test::writeFile(tmp.path() / "a.vcd", Phosphor::Test::kSmallVcd);
    Phosphor::Session session{Phosphor::StoreCache{tmp.path() / "cache"}};
    const auto id = openAndWait(session, dump);
    std::vector<RemovalEvent> events;
    std::size_t topSignals = 0;
    QObject::connect(&session, &Phosphor::Session::sourceAboutToBeRemoved, &session, [&](Phosphor::SourceId removed) {
        const auto* db = session.hierarchyDb(removed);
        events.push_back({true, removed, session.info(removed) != nullptr, db != nullptr});
        if (db != nullptr)
            topSignals = db->root().scope("top").signalCount();
    });
    QObject::connect(&session, &Phosphor::Session::sourceRemoved, &session, [&](Phosphor::SourceId removed) {
        events.push_back({false, removed, session.info(removed) != nullptr, session.hierarchyDb(removed) != nullptr});
    });
    // Через очередь обработчик выполняется уже после удаления: источника в нём нет.
    std::vector<RemovalEvent> queued;
    QObject::connect(
            &session, &Phosphor::Session::sourceAboutToBeRemoved, &session,
            [&](Phosphor::SourceId removed) {
                queued.push_back(
                        {true, removed, session.info(removed) != nullptr, session.hierarchyDb(removed) != nullptr});
            },
            Qt::QueuedConnection);

    session.removeSource(id);
    QCoreApplication::processEvents();

    EXPECT_EQ(events, (std::vector<RemovalEvent>{{true, id, true, true}, {false, id, false, false}}));
    EXPECT_EQ(topSignals, 3U);
    EXPECT_EQ(queued, (std::vector<RemovalEvent>{{true, id, false, false}}));
}

TEST(Session, AboutToBeRemovedPrecedesRemovedOnEveryPath) {
    const Phosphor::Test::TempDir tmp;
    const auto badPath = Phosphor::Test::writeFile(tmp.path() / "bad.vcd", Phosphor::Test::kBrokenVcd);
    const auto goodPath = Phosphor::Test::writeFile(tmp.path() / "a.vcd", Phosphor::Test::kSmallVcd);
    Phosphor::Session session{Phosphor::StoreCache{tmp.path() / "cache"}};
    const auto bad = openAndWait(session, badPath);
    std::vector<RemovalEvent> events;
    QObject::connect(&session, &Phosphor::Session::sourceAboutToBeRemoved, &session, [&](Phosphor::SourceId removed) {
        events.push_back({true, removed, session.info(removed) != nullptr, false});
    });
    QObject::connect(&session, &Phosphor::Session::sourceRemoved, &session, [&](Phosphor::SourceId removed) {
        events.push_back({false, removed, session.info(removed) != nullptr, false});
    });
    QSignalSpy removed{&session, &Phosphor::Session::sourceRemoved};

    // Сломанный источник удаляется сразу, загружающийся — когда его поток вернёт результат.
    session.removeSource(bad);
    const auto loading = session.openDump(goodPath);
    session.removeSource(loading);
    ASSERT_TRUE(QTest::qWaitFor([&] { return removed.count() == 2; }, kWaitMs));

    EXPECT_EQ(events,
            (std::vector<RemovalEvent>{{true, bad, true, false}, {false, bad, false, false},
                    {true, loading, true, false}, {false, loading, false, false}}));
}

TEST(Session, CancelReadySourceIsNoOp) {
    const Phosphor::Test::TempDir tmp;
    const auto dump = Phosphor::Test::writeFile(tmp.path() / "a.vcd", Phosphor::Test::kSmallVcd);
    Phosphor::Session session{Phosphor::StoreCache{tmp.path() / "cache"}};
    const auto id = openAndWait(session, dump);
    const QSignalSpy removed{&session, &Phosphor::Session::sourceRemoved};

    session.cancelLoad(id);
    QCoreApplication::processEvents();

    EXPECT_EQ(removed.count(), 0);
    EXPECT_EQ(session.sourceIds(), std::vector{id});
    ASSERT_NE(session.info(id), nullptr);
    EXPECT_EQ(session.info(id)->state, Phosphor::SourceState::READY);
    EXPECT_NE(session.hierarchyDb(id), nullptr);
}

TEST(Session, TotalRangeIsHullInFemtoseconds) {
    const Phosphor::Test::TempDir tmp;
    const auto nsDump = Phosphor::Test::writeFile(tmp.path() / "ns.vcd", Phosphor::Test::kSmallVcd);
    const auto psDump = Phosphor::Test::writeFile(tmp.path() / "ps.vcd", kPicosecondVcd);
    Phosphor::Session session{Phosphor::StoreCache{tmp.path() / "cache"}};
    const auto ns = openAndWait(session, nsDump);
    const auto ps = openAndWait(session, psDump);
    ASSERT_NE(session.hierarchyDb(ns), nullptr);
    ASSERT_NE(session.hierarchyDb(ps), nullptr);
    const auto nsAxis = session.axis(ns);
    const auto psAxis = session.axis(ps);
    ASSERT_TRUE(nsAxis.has_value());
    ASSERT_TRUE(psAxis.has_value());
    ASSERT_EQ(nsAxis->femtosecondsPerUnit(), 1'000'000);
    ASSERT_EQ(psAxis->femtosecondsPerUnit(), 1000);

    const auto range = session.totalRange();

    ASSERT_TRUE(range.has_value());
    // Начало — у наносекундного дампа (0), конец — у пикосекундного (не раньше 50 ns).
    EXPECT_EQ(range->begin, nsAxis->toGlobal(session.hierarchyDb(ns)->timeRange().begin));
    EXPECT_EQ(range->begin, 0);
    EXPECT_EQ(range->end, psAxis->toGlobal(session.hierarchyDb(ps)->timeRange().end));
    EXPECT_GE(range->end, 50'000'000);
    EXPECT_GT(range->end, nsAxis->toGlobal(session.hierarchyDb(ns)->timeRange().end));
}

TEST(Session, DestructorJoinsLoads) {
    const Phosphor::Test::TempDir tmp;
    const auto dump = Phosphor::Test::writeFile(tmp.path() / "a.vcd", Phosphor::Test::kSmallVcd);
    const Phosphor::StoreCache cache{tmp.path() / "cache"};

    {
        Phosphor::Session session{cache};
        std::ignore = session.openDump(dump);
        std::ignore = session.openDump(dump);
    }
    // Результаты, поставленные в очередь уже уничтоженной сессии, должны отброситься.
    QCoreApplication::processEvents();

    if (std::filesystem::exists(cache.directory())) {
        for (const auto& entry : std::filesystem::directory_iterator{cache.directory()})
            EXPECT_NE(entry.path().extension(), ".part") << entry.path();
    }
}
