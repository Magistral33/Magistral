#include    "graphsettingswindow.h"
#include    "ui_graphsettingswindow.h"

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
GraphSettingsWindow::GraphSettingsWindow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingsWidgets)
{
    ui->setupUi(this);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
GraphSettingsWindow::~GraphSettingsWindow()
{
    delete ui;
}
