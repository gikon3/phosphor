#pragma once

#include <QMainWindow>
#include <QString>
#include <map>

#include "core/source_id.hpp"

class QCloseEvent;
class QDockWidget;
class QTreeView;

namespace Phosphor {

class HierarchyModel;
class Renderer;
class Session;
class View;
class WavePanel;

// Главное окно: меню, дерево иерархии в доке и статус загрузок. Сессия, вид и рендерер окну не
// принадлежат и должны его пережить. Источники, уже открытые в сессии, окно подхватывает как новые;
// копии БД для чтения у готовых из них ещё не должны быть забраны.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(Session& session, View& view, Renderer& renderer, QWidget* parent = nullptr);

protected:
    // Геометрия и раскладка доков сохраняются при закрытии, а восстанавливаются в конструкторе.
    // Место — настройки приложения по умолчанию: их задают имена организации и приложения
    // и формат по умолчанию у QSettings.
    void closeEvent(QCloseEvent* event) override;

private:
    // Меню ссылаются на док и панель волн, поэтому создаются после них.
    void createMenus();
    void createHierarchyDock();
    void openDumps();
    // Выбранные в дереве сигналы с собственным потоком — в конец вида, в порядке выбора.
    void addSelectedSignals();

    void adoptExistingSources();
    // Открытый дамп не должен оказаться в скрытом доке: закрытый док иначе
    // восстанавливается закрытым, и вернуть его нечем, кроме меню вида.
    void revealHierarchy();
    void showLoading(SourceId id);
    void hideLoading(SourceId id);
    // Вид сразу показывает все дампы, но только если до этого показывать было нечего (ни у одного
    // готового дампа не было диапазона времени): окно, выбранное пользователем, новый источник не
    // сбивает.
    void handleReady(SourceId id);
    void attachToRenderer(SourceId id);
    void fitWindowToSources();
    void reportFailure(SourceId id, const QString& error);

    void restoreSettings();
    void saveSettings() const;

private:
    Session* session_;
    View* view_;
    Renderer* renderer_;
    WavePanel* panel_ = nullptr;
    HierarchyModel* model_ = nullptr;
    QTreeView* tree_ = nullptr;
    QDockWidget* hierarchyDock_ = nullptr;
    // Виджет статус-бара каждой идущей загрузки.
    std::map<SourceId, QWidget*> loading_;
    // Окно уже подогнано под непустой диапазон готовых источников; сбрасывается, когда с удалением
    // источников диапазона не остаётся.
    bool rangeShown_ = false;
};

}  // namespace Phosphor
