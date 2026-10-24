#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/time_mapper.hpp"
#include "core/value_format.hpp"
#include "wasafe/model/database.hpp"

namespace Phosphor {

enum class SpanKind : std::uint8_t {
    NONE,   // значения ещё нет
    LOW,    // бит 0
    HIGH,   // бит 1
    X,      // бит x либо вектор, где есть x/z (подпись — label)
    Z,      // бит z
    VALUE,  // определённое значение (подпись — label)
    DENSE,  // в пикселе два и более изменений
};

// Полуинтервал пикселей [x0, x1) с одним значением.
struct DigitalSpan {
    int x0;
    int x1;
    SpanKind kind;
    std::string label;
};

struct DigitalFrame {
    bool isBit = false;  // сигнал — один logic-бит
    std::vector<DigitalSpan> spans;
};

// Прореживает изменения узла в окне до спанов шириной в пиксели. Пиксель момента t — тот,
// в чьё отображение время→пиксель t попадает (целая часть координаты); пиксель с одним
// изменением принадлежит новому значению, с двумя и более — DENSE. Изменение не позже
// window.begin считается начальным значением; в остальных пикселях изменение ровно в их
// начале принадлежит им. Как только пиксель набрал два изменения,
// остаток пикселя пропускается и чтение продолжается с начала следующего, поэтому цена кадра
// не зависит от числа изменений в плотных пикселях. Подпись формируется только для
// выданного спана; соседние спаны с одинаковым значением сливаются. isBit определяется типом
// сигнала и не зависит от окна. При widthPx <= 0 или пустом окне spans пуст.
DigitalFrame buildDigitalFrame(const WaSafe::Database& db, WaSafe::NodeId node, const TimeAxis& axis, TimeWindow window,
        int widthPx, const RowFormat& fmt);

}  // namespace Phosphor
