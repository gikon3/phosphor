#include "core/cancelling_builder.hpp"

#include <cstdint>
#include <utility>

#include "core/error.hpp"
#include "wasafe/model/database.hpp"

namespace Phosphor {

namespace {

constexpr std::uint32_t kChangesPerCheck = 4096;

class CancellingBuilder final : public WaSafe::Builder {
public:
    CancellingBuilder(WaSafe::Builder& sink, std::stop_token stop) : sink_{&sink}, stop_{std::move(stop)} {}

    void setTimeScale(WaSafe::TimeScale scale) override { sink_->setTimeScale(scale); }

    WaSafe::ScopeId beginScope(std::string_view name, WaSafe::ScopeKind kind) override {
        return sink_->beginScope(name, kind);
    }

    void endScope() override { sink_->endScope(); }

    WaSafe::SignalId declareVar(std::string_view name, WaSafe::Type type, std::optional<WaSafe::SignalId> alias,
            std::span<WaSafe::SignalId> leaves) override {
        return sink_->declareVar(name, std::move(type), alias, leaves);
    }

    void headerDone() override { sink_->headerDone(); }

    void setTime(WaSafe::TimeStamp time) override {
        throwIfStopped();
        sink_->setTime(time);
    }

    void valueChange(WaSafe::SignalId id, WaSafe::ValueView value) override {
        if (++changes_ % kChangesPerCheck == 0)
            throwIfStopped();
        sink_->valueChange(id, value);
    }

    void finish() override { sink_->finish(); }

    WaSafe::Database takeDatabase() override { return sink_->takeDatabase(); }

private:
    void throwIfStopped() const {
        if (stop_.stop_requested())
            throw Cancelled{"Operation cancelled"};
    }

private:
    WaSafe::Builder* sink_;
    std::stop_token stop_;
    std::uint32_t changes_ = 0;
};

}  // namespace

std::unique_ptr<WaSafe::Builder> makeCancellingBuilder(WaSafe::Builder& sink, std::stop_token stop) {
    return std::make_unique<CancellingBuilder>(sink, std::move(stop));
}

}  // namespace Phosphor
