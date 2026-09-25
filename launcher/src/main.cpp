//------------------------------------------------------------------------------
//
//      Russian Railway Simulator (RRS) launcer
//      (c) maisvendoo, 17/12/2018
//
//------------------------------------------------------------------------------
/*!
 * \file
 * \brief Russian Railway Simulator (RRS) launcer
 * \copyright maisvendoo
 * \author maisvendoo
 * \date 17/12/2018
 */

#include    "mainwindow.h"
#include    <QApplication>
#include    <QTranslator>
#include    <crash-handler.h>

/*!
 * \fn
 * \brief Entry point
 */
//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
#include <QDir>
#include <QFileInfo>

int main(int argc, char *argv[])
{
    // Меняем cwd на директорию exe — все относительные пути
    // (crash-handler, плагины Qt, cwd-зависимые данные) будут от неё
    QDir::setCurrent(QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath());

    setup_crash_handler("../logs/launcher-crash.log");

    QApplication a(argc, argv);
    QTranslator translator;

    if (translator.load("launcher.ru_RU.qm", ":/translations/translations"))
    {
        a.installTranslator(&translator);
    }

    MainWindow w;
    w.show();

    return a.exec();
}
