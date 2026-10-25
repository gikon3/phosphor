#pragma once

#include <QKeySequence>
#include <Qt>

// Все сочетания клавиш приложения. Раскладка предварительная: ориентир — SimVision для цифры и
// Virtuoso/ViVA для аналога, сверка с ними ещё впереди. Сочетания заданы кодами клавиш, а не
// строками и не QKeySequence::StandardKey: те разбираются через платформу, которой при
// статической инициализации ещё нет.
namespace Phosphor::Shortcuts {

inline const QKeySequence kOpen{Qt::CTRL | Qt::Key_O};
inline const QKeySequence kExit{Qt::CTRL | Qt::Key_Q};

// Добавить выбранные в дереве сигналы в конец вида: основной Enter и Enter цифрового блока.
inline const QKeySequence kAddSignals{Qt::Key_Return};
inline const QKeySequence kAddSignalsKeypad{Qt::Key_Enter};

// Удалить выделенные строки вида.
inline const QKeySequence kRemoveRows{Qt::Key_Delete};

// Приблизить: Ctrl+= и Ctrl++. «+» основной клавиатуры набирается с Shift, и платформы сообщают
// сочетание то с Shift, то без, поэтому заданы оба. Клавиши цифрового блока работают и без Ctrl.
inline const QKeySequence kZoomIn{Qt::CTRL | Qt::Key_Equal};
inline const QKeySequence kZoomInPlus{Qt::CTRL | Qt::Key_Plus};
inline const QKeySequence kZoomInShiftPlus{Qt::CTRL | Qt::SHIFT | Qt::Key_Plus};
inline const QKeySequence kZoomInKeypad{Qt::KeypadModifier | Qt::Key_Plus};
inline const QKeySequence kZoomOut{Qt::CTRL | Qt::Key_Minus};
inline const QKeySequence kZoomOutKeypad{Qt::KeypadModifier | Qt::Key_Minus};
inline const QKeySequence kZoomFit{Qt::CTRL | Qt::Key_0};

// Поставить курсор A или B в начало видимого окна.
inline const QKeySequence kCursorAToWindowStart{Qt::CTRL | Qt::SHIFT | Qt::Key_A};
inline const QKeySequence kCursorBToWindowStart{Qt::CTRL | Qt::SHIFT | Qt::Key_B};

}  // namespace Phosphor::Shortcuts
