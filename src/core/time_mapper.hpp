#pragma once

#include "core/time_axis.hpp"

namespace Phosphor {

// Видимое окно времени [begin, end).
struct TimeWindow {
    GlobalTime begin = 0;
    GlobalTime end = 0;

    friend bool operator==(const TimeWindow&, const TimeWindow&) = default;
};

// Наименьшая ширина окна: на предельном зуме окно не схлопывается в точку.
inline constexpr GlobalTime kMinWindowSpan = 1;

// Координата времени t в пикселях окна шириной widthPx. Для пустого окна или
// widthPx <= 0 возвращает 0.
double timeToPixel(TimeWindow w, int widthPx, GlobalTime t) noexcept;

// Обратное преобразование с округлением до фемтосекунды. Для пустого окна или
// widthPx <= 0 возвращает w.begin; результат насыщается к границам int64.
GlobalTime pixelToTime(TimeWindow w, int widthPx, double x) noexcept;

// Масштабирует окно в factor раз (factor < 1 приближает), сохраняя положение pivot
// внутри окна. Ширина не меньше kMinWindowSpan; окно не выходит за границы int64.
// Некорректное окно или factor возвращаются без изменений.
TimeWindow zoomAround(TimeWindow w, GlobalTime pivot, double factor) noexcept;

// Сдвигает окно на delta, сохраняя ширину; у границ int64 сдвиг упирается в границу.
TimeWindow scrollBy(TimeWindow w, GlobalTime delta) noexcept;

}  // namespace Phosphor
