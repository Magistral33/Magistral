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

    // Списки тени заполняются сразу при создании вкладки: иначе поле
    // «Качество теней» успевает показаться пустым
    ui->cbSwadowsQuality->addItem(tr("Низкое"));
    ui->cbSwadowsQuality->addItem(tr("Среднее"));
    ui->cbSwadowsQuality->addItem(tr("Высокое"));
    ui->cbSwadowsQuality->addItem(tr("Очень высокое"));
    ui->cbSwadowsQuality->addItem(tr("Настраиваемое"));
    ui->cbSwadowsQuality->setCurrentIndex(1);

    ui->cbShadowsMap->addItem("1024");
    ui->cbShadowsMap->addItem("2048");
    ui->cbShadowsMap->addItem("4096");
    ui->cbShadowsMap->setCurrentIndex(1);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
GraphSettingsWindow::~GraphSettingsWindow()
{
    delete ui;
}
