#ifndef     GRAPHSETTINGSWINDOW_H
#define     GRAPHSETTINGSWINDOW_H

#include    <QWidget>

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
namespace Ui
{
    class SettingsWidgets;
}

//------------------------------------------------------------------------------
// Виджеты вкладки «Настройки». Логика настроек графики живёт в MainWindow,
// класс нужен только чтобы сгенерировать и содержать интерфейс вкладки.
//------------------------------------------------------------------------------
class GraphSettingsWindow : public QWidget
{
    Q_OBJECT

public:

    explicit GraphSettingsWindow(QWidget *parent = nullptr);

    ~GraphSettingsWindow();

    Ui::SettingsWidgets *getUi() const { return ui; }

private:

    Ui::SettingsWidgets *ui;
};

#endif // GRAPHSETTINGSWINDOW_H
