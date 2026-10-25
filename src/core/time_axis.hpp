#pragma once

#include <cstdint>
#include <string>

#include "wasafe/core/time.hpp"

namespace Phosphor {

// Общая ось времени ядра в фемтосекундах. Фемтосекунда — самая мелкая единица wasafe,
// поэтому добавление нового дампа не требует пересчёта уже сохранённых времён.
using GlobalTime = std::int64_t;

// Перевод между сеткой одного дампа и общей осью.
class TimeAxis {
public:
    // Бросает Error, если масштаб мельче фемтосекунды, множитель не положителен
    // или количество фемтосекунд в единице не помещается в int64.
    static TimeAxis fromScale(WaSafe::TimeScale scale);

    // Результат насыщается к границам int64; kNoTime переходит в INT64_MIN — то же значение,
    // что и насыщение вниз.
    [[nodiscard]] GlobalTime toGlobal(WaSafe::TimeStamp t) const noexcept;

    // Ближайшая метка сетки не позже t.
    [[nodiscard]] WaSafe::TimeStamp toLocalFloor(GlobalTime t) const noexcept;

    // Ближайшая метка сетки не раньше t.
    [[nodiscard]] WaSafe::TimeStamp toLocalCeil(GlobalTime t) const noexcept;

    [[nodiscard]] std::int64_t femtosecondsPerUnit() const noexcept;

private:
    explicit TimeAxis(std::int64_t femtosecondsPerUnit) noexcept;

private:
    std::int64_t femtosecondsPerUnit_;
};

// Время с автоматически выбранной единицей (fs, ps, ns, us, ms, s): до трёх знаков
// после точки, без хвостовых нулей. Знак отрицательных значений сохраняется.
std::string formatTime(GlobalTime t);

// То же, но знаков после точки столько, чтобы вес последнего знака был не крупнее resolution:
// времена, отличающиеся на resolution, печатаются по-разному, а крупный шаг лишних знаков не
// добавляет. Значение округляется до этого знака. Неположительный resolution — 1 fs.
std::string formatTime(GlobalTime t, GlobalTime resolution);

}  // namespace Phosphor
