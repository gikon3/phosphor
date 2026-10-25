#include "core/error.hpp"

namespace Phosphor {

QString errorText(const std::exception& e) {
    if (dynamic_cast<const Error*>(&e) != nullptr)
        return QString::fromUtf8(e.what());
    return QString::fromLocal8Bit(e.what());
}

std::string systemMessage(const std::error_code& ec) {
    return QString::fromLocal8Bit(ec.message()).toStdString();
}

}  // namespace Phosphor
