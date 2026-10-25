#include "app/signal_mime.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <limits>
#include <optional>

namespace Phosphor {

namespace {

constexpr QLatin1StringView kSourceKey{"source"};
constexpr QLatin1StringView kPathKey{"path"};

// Не число, дробное, отрицательное или слишком большое значение — не SourceId. Нецелое число и
// не-число читаются как -1 и отсеиваются вместе с отрицательными.
std::optional<SourceId> toSourceId(const QJsonValue& value) {
    const auto number = value.toInteger(-1);
    if (number < 0 || number > std::numeric_limits<SourceId>::max())
        return std::nullopt;
    return static_cast<SourceId>(number);
}

}  // namespace

QByteArray encodeSignalRefs(std::span<const SignalRef> refs) {
    QJsonArray array;
    for (const auto& ref : refs) {
        array.append(QJsonObject{
                {kSourceKey, static_cast<double>(ref.source)},
                {kPathKey, QString::fromStdString(ref.path)},
        });
    }
    return QJsonDocument{array}.toJson(QJsonDocument::Compact);
}

std::vector<SignalRef> decodeSignalRefs(const QByteArray& data) {
    const auto document = QJsonDocument::fromJson(data);
    std::vector<SignalRef> refs;
    if (!document.isArray())
        return refs;
    for (const auto& item : document.array()) {
        const auto object = item.toObject();
        const auto source = toSourceId(object.value(kSourceKey));
        const auto path = object.value(kPathKey);
        if (!source || !path.isString() || path.toString().isEmpty())
            continue;
        refs.push_back(SignalRef{*source, path.toString().toStdString()});
    }
    return refs;
}

}  // namespace Phosphor
