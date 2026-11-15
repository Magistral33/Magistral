//------------------------------------------------------------------------------
//
//      RSS launcher main window
//      (c) maisvendoo, 17/12/2018
//
//------------------------------------------------------------------------------
/*!
 * \file
 * \brief RSS launcher main window
 * \copyright maisvendoo
 * \author maisvendoo
 * \date 17/12/2018
 */

#include    "mainwindow.h"
#include    "datetime.h"
#include    "train-waypoint-widget.h"
#include    "ui_mainwindow.h"
#include    "ui_graphsettingswindow.h"

#include    <QPushButton>
#include    <QStyle>
#include    <QDir>
#include    <QDirIterator>
#include    <QStringList>
#include    <QTextStream>

#include    "filesystem.h"
#include    "CfgReader.h"
#include    "find-settings.h"

#include    "platform.h"
#include    "styles.h"

#include    "pdfviewer.h"

#include    <system-diagnostic.h>
#include    <graphsettingswindow.h>

const   QString MainWindow::WIDTH = "Width";
const   QString MainWindow::HEIGHT = "Height";
const   QString MainWindow::FULLSCREEN = "FullScreen";
const   QString MainWindow::FOV_Y = "FovY";
const   QString MainWindow::ZNEAR = "zNear";
const   QString MainWindow::ZFAR = "zFar";
const   QString MainWindow::SCREEN_NUM = "ScreenNumber";
const   QString MainWindow::WIN_DECOR = "WindowDecoration";
const   QString MainWindow::DOUBLE_BUFF = "DoubleBuffer";
const   QString MainWindow::VSYNC = "VSync";
const   QString MainWindow::NOTIFY_LEVEL = "NofifyLevel";
const   QString MainWindow::VIEW_DIST = "ViewDistance";
const   QString MainWindow::MAX_FPS = "MaxFPS";
const   QString MainWindow::PHYSICAL_DEVICE = "PhysicalDevice";
const   QString MainWindow::PHYSICAL_DEVICE_VENDOR_ID = "PhysicalDeviceVendorID";
const   QString MainWindow::PHYSICAL_DEVICE_DEVICE_ID = "PhysicalDeviceDeviceID";
const   QString MainWindow::SAMPLES = "Samples";
const   QString MainWindow::DEPTH_FORMAT = "depthFormat";
const   QString MainWindow::SHADOW = "Shadow";
const   QString MainWindow::SHADOWS_PRESET = "ShadowsPreset";
const   QString MainWindow::SHADOW_DISTANCE = "ShadowDistance";
const   QString MainWindow::SHADOW_CASCADE = "ShadowCascade";
const   QString MainWindow::SHADOW_RESOLUTION = "ShadowResolution";


const   QString MainWindow::STARTUP_SCN_SUBDIR = "startup";
const   QString MainWindow::AUTO_START_VIEWER = "AutoStartViewer";
const   QString MainWindow::AUTO_START_ROUTE_MAP = "AutoStartRouteMap";

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
static void repolish(QWidget *widget)
{
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    tbActiveTrains = new QToolBox(this);
    int idx_before_last = ui->vblActiveTrainsLayout->count() - 1;
    ui->vblActiveTrainsLayout->insertWidget(idx_before_last, tbActiveTrains);

    init();

    connect(ui->lwRoutes, &QListWidget::itemSelectionChanged,
            this, &MainWindow::slotRouteSelection);

    connect(ui->lwTrains, &QListWidget::itemSelectionChanged,
            this, &MainWindow::slotTrainSelection);

    connect(ui->pbStartServer, &QPushButton::pressed,
            this, &MainWindow::slotStartServerPressed);

    connect(&simulatorProc, &QProcess::started,
            this, &MainWindow::slotSimulatorStarted);

    connect(&viewerProc, &QProcess::started,
            this, &MainWindow::slotViewerStarted);

    connect(&mapProc, &QProcess::started,
            this, &MainWindow::slotMapStarted);

    connect(&simulatorProc, &QProcess::finished,
            this, &MainWindow::slotSimulatorFinished);

    connect(&viewerProc, &QProcess::finished,
            this, &MainWindow::slotViewerFinished);

    connect(&mapProc, &QProcess::finished,
            this, &MainWindow::slotMapFinished);

    connect(ui->pbConnectViewer, &QPushButton::pressed,
            this, &MainWindow::slotConnectViewerPressed);

    connect(ui->pbConnectMap, &QPushButton::pressed,
            this, &MainWindow::slotConnectMapPressed);

    connect(ui->cbSavedServers, &QComboBox::currentIndexChanged,
            this, &MainWindow::slotSelectSavedServer);

    connect(ui->leServerName, &QLineEdit::textChanged,
            this, &MainWindow::slotChangedServerSettings);

    connect(ui->sbIPv4_1, &QSpinBox::valueChanged,
            this, &MainWindow::slotChangedServerSettings);

    connect(ui->sbIPv4_2, &QSpinBox::valueChanged,
            this, &MainWindow::slotChangedServerSettings);

    connect(ui->sbIPv4_3, &QSpinBox::valueChanged,
            this, &MainWindow::slotChangedServerSettings);

    connect(ui->sbIPv4_4, &QSpinBox::valueChanged,
            this, &MainWindow::slotChangedServerSettings);

    connect(ui->sbIPv4_port, &QSpinBox::valueChanged,
            this, &MainWindow::slotChangedServerSettings);

    connect(ui->pbSaveServer, &QPushButton::pressed,
            this, &MainWindow::slotSaveServer);    

    setFocusPolicy(Qt::ClickFocus);

    loadSettingsGUI();

    loadConfig();

    QIcon icon(":/images/images/MAGISTRAL_logo.png");
    setWindowIcon(icon);

    ui->pbStartServer->setEnabled(false);
    is_start_button_to_stop_server = false;

    // Строка меню больше не нужна: «Настройки» переехали кнопкой в строку
    // вкладок, «Помощь» убрана. Меню «Инструменты» показывается, только
    // если в cfg/launcher.xml есть доступный инструмент — тогда строка
    // меню появляется снова (см. createToolsMenu).
    ui->menuBar->setVisible(false);

    connect(ui->cbScenario, &QComboBox::currentIndexChanged, this, &MainWindow::slotOnScenarioSelection);

    // Фото состава: картинка вписывается в свободную область карточки
    // с сохранением пропорций, исходный файл не меняется
    updateTrainPhoto();

    // Фон страниц вкладок: иначе под карточками просвечивает фон
    // QTabWidget (полоса другого оттенка)
    for (QWidget *page : { ui->tab_local_server, ui->tab_remote_server })
    {
        page->setAutoFillBackground(true);
        QPalette pal = page->palette();
        pal.setColor(QPalette::Window, QColor("#0E1014"));
        page->setPalette(pal);
    }

    ui->tbScenarioDescription->setTextInteractionFlags(Qt::NoTextInteraction);
    ui->tbScenarioDescription->setFocusPolicy(Qt::NoFocus);

    // Описания только для чтения: текст нельзя изменить (стереть или
    // дописать), выделение и прокрутка остаются доступны
    ui->ptRouteDescription->setReadOnly(true);
    ui->ptTrainDescription->setReadOnly(true);
    ui->tbScenarioDescription->setReadOnly(true);

        // РџСЂРµРґСЃС‚Р°СЂС‚РѕРІР°СЏ РґРёР°РіРЅРѕСЃС‚РёРєР° GPU вЂ” РІСЂРµРјРµРЅРЅРѕ РѕС‚РєР»СЋС‡РµРЅР° РґР»СЏ РѕС‚Р»Р°РґРєРё
    gpuDiagnostics();

    // Журнал диагностики показываем только при наличии сообщений
    ui->tbLogGPU->setVisible(!ui->tbLogGPU->toPlainText().trimmed().isEmpty());

    // Настройки графики — третья вкладка, её виджеты создаются здесь
    setGraphicsWidgets();

    createToolsMenu();
}

//------------------------------------------------------------------------------
// Настройки графики: вкладка «Настройки»
//------------------------------------------------------------------------------
void MainWindow::setGraphicsWidgets()
{
    settingsWindow = new GraphSettingsWindow(ui->tab_settings);
    Ui::SettingsWidgets *settings_ui = settingsWindow->getUi();

    connect(settings_ui->pbGraphApply, &QPushButton::released,
            this, &MainWindow::slotApplyGraphSettings);

    connect(settings_ui->pbGraphCancel, &QPushButton::released,
            this, &MainWindow::slotCancelGraphSettings);

    connect(settings_ui->cbWindowDecoration, &QCheckBox::stateChanged, this, [this, settings_ui](int){
        settings_ui->pbGraphApply->setEnabled(true);
    });

    connect(settings_ui->cbFullScreenMode, &QCheckBox::stateChanged, this, [this, settings_ui](int){
        settings_ui->pbGraphApply->setEnabled(true);
    });

    connect(settings_ui->sbWindowWidth, &QSpinBox::valueChanged, this, [this, settings_ui](int){
        settings_ui->pbGraphApply->setEnabled(true);
    });

    connect(settings_ui->sbWindowHeight, &QSpinBox::valueChanged, this, [this, settings_ui](int){
        settings_ui->pbGraphApply->setEnabled(true);
    });

    connect(settings_ui->cbLimitFPS, &QCheckBox::checkStateChanged, this, [this, settings_ui](Qt::CheckState state){

        settings_ui->pbGraphApply->setEnabled(true);
        settings_ui->sbMaxFPS->setEnabled(state == Qt::Checked);
    });

    connect(settings_ui->sbMaxFPS, &QSpinBox::valueChanged, this, [this, settings_ui](int){
        settings_ui->pbGraphApply->setEnabled(true);
    });

    connect(settings_ui->sbViewDistance, &QSpinBox::valueChanged, this, [this, settings_ui](int){
        settings_ui->pbGraphApply->setEnabled(true);
    });

    connect(settings_ui->sbDisplayNumber, &QSpinBox::valueChanged, this, [this, settings_ui](int){
        settings_ui->pbGraphApply->setEnabled(true);
    });

    connect(settings_ui->hsMSAA, &QSlider::valueChanged, this, [this, settings_ui](int){
        settings_ui->pbGraphApply->setEnabled(true);
        settings_ui->lMSAA->setText(QString("%1x").arg((1 << settings_ui->hsMSAA->value())));
    });



    // Списки заполняются до подключения обработчиков, чтобы не сработали
    // лишние сигналы при построении интерфейса
    settings_ui->cbSwadowsQuality->addItem(tr("Низкое"));
    settings_ui->cbSwadowsQuality->addItem(tr("Среднее"));
    settings_ui->cbSwadowsQuality->addItem(tr("Высокое"));
    settings_ui->cbSwadowsQuality->addItem(tr("Очень высокое"));
    settings_ui->cbSwadowsQuality->addItem(tr("Настраиваемое"));

    settings_ui->cbShadowsMap->addItem(tr("1024"));
    settings_ui->cbShadowsMap->addItem(tr("2048"));
    settings_ui->cbShadowsMap->addItem(tr("4096"));

    settings_ui->cbSwadowsQuality->setCurrentIndex(1);
    settings_ui->cbShadowsMap->setCurrentIndex(1);

    // Пресеты качества теней: каждому соответствует карта теней,
    // число каскадов и дальность
    connect(settings_ui->cbSwadowsQuality, &QComboBox::currentIndexChanged, this, [this, settings_ui](int idx){

        if (idx >= 0 && idx < static_cast<int>(shadowsPresets.size()))
        {
            auto sp = shadowsPresets[idx];

            settings_ui->cbShadowsMap->setCurrentIndex(sp.resolution_idx);
            settings_ui->hsShadowsCascades->setValue(sp.cascades);
            settings_ui->hsShadowsDistance->setValue(sp.distanse);
            settings_ui->lShadowsCascades->setText(QString::number(sp.cascades));
            settings_ui->lShadowsDistance->setText(QString::number(sp.distanse));
        }

        settings_ui->pbGraphApply->setEnabled(true);
    });

    connect(settings_ui->cbShowShadows, &QCheckBox::stateChanged, this, [this, settings_ui](int){

        const bool on = settings_ui->cbShowShadows->checkState() == Qt::CheckState::Checked;

        settings_ui->cbSwadowsQuality->setEnabled(on);
        settings_ui->cbShadowsMap->setEnabled(on);
        settings_ui->hsShadowsCascades->setEnabled(on);
        settings_ui->hsShadowsDistance->setEnabled(on);

        settings_ui->pbGraphApply->setEnabled(true);
    });

    connect(settings_ui->hsShadowsCascades, &QSlider::valueChanged, this, [this, settings_ui](int idx){
        settings_ui->lShadowsCascades->setText(QString("%1").arg(idx));
    });

    connect(settings_ui->hsShadowsDistance, &QSlider::valueChanged, this, [this, settings_ui](int idx){
        settings_ui->lShadowsDistance->setText(QString("%1").arg(idx));
    });

    connect(settings_ui->cbShadowsMap, &QComboBox::currentIndexChanged, this, [this, settings_ui](int idx){

        if (idx > 0 && idx < shadowMapResolution.size())
        {
            auto resolution = shadowMapResolution[idx];

            int index = 0;
            findSetting(SHADOW_RESOLUTION, fd_list, index);
            fd_list[index] = QPair<QString, QVariant>(SHADOW_RESOLUTION, resolution);
        }
    });

    // настройки перечитываются каждый раз, когда открывают вкладку
    // стартовые значения, чтобы вкладка не была пустой до чтения настроек
    settings_ui->sbWindowWidth->setRange(320, 10000);
    settings_ui->sbWindowHeight->setRange(240, 10000);
    settings_ui->sbMaxFPS->setRange(1, 1000);
    settings_ui->sbDisplayNumber->setRange(0, 8);
    settings_ui->hsMSAA->setRange(0, 3);

    settings_ui->pbGraphApply->setEnabled(false);

    connect(ui->twMain, &QTabWidget::currentChanged, this, [this, settings_ui](int idx)
    {
        if (idx == ui->twMain->indexOf(ui->tab_settings))
        {
            showSettingsTab();
        }
    });
}


//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
MainWindow::~MainWindow()
{
    delete settingsWindow;
    delete ui;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::setSettingsGPU(const gpus_info_list_t &gpus_info)
{
    Ui::SettingsWidgets *settings_ui = settingsWindow->getUi();

    this->gpus_info = gpus_info;

    if (gpus_info.empty())
    {
        return;
    }


    // Выбираем самый производительный GPU из найденных в качестве текущего
    int max_score = 0;
    int best_gpu_idx = -1;

    for (size_t i = 0; i < gpus_info.size(); ++i)
    {
        auto &gpu_info = gpus_info[i];

        if (gpu_info.score > max_score)
        {
            max_score = gpu_info.score;
            best_gpu_idx = i;
        }

}


    // Если видюха еще не настраивалась никогда
    if (current_gpu_idx == -1)
    {
        // выбираем лючшую из найденных
        current_gpu_idx = best_gpu_idx;

        setDefaultSettingsForChangedGPU(current_gpu_idx);

        // Сохраняем настройки
        saveGraphSettings(fd_list);
    }
    else
    {
        // Пытаемся найти GPU по аппаратному ID из конфига
        int saved_vendor_id = 0;
        int saved_device_id = 0;

        for (const auto& field : fd_list)
        {
            if (field.first == PHYSICAL_DEVICE_VENDOR_ID)
                saved_vendor_id = field.second.toInt();
            if (field.first == PHYSICAL_DEVICE_DEVICE_ID)
                saved_device_id = field.second.toInt();
        }

        int matched_idx = -1;
        if (saved_vendor_id != 0 && saved_device_id != 0)
        {
            for (size_t i = 0; i < gpus_info.size(); ++i)
            {
                if (gpus_info[i].vendorID == static_cast<uint32_t>(saved_vendor_id) &&
                    gpus_info[i].deviceID == static_cast<uint32_t>(saved_device_id))
                {
                    matched_idx = static_cast<int>(i);
                    break;
                }
            }
        }

        if (matched_idx >= 0)
        {
            current_gpu_idx = matched_idx;
        }
        else if (current_gpu_idx >= 0 && current_gpu_idx < static_cast<int>(gpus_info.size()))
        {
            // Fallback на старый индекс, если не нашли по ID
        }
        else
        {
            current_gpu_idx = best_gpu_idx;
        }

        

        auto &gpu_info = gpus_info[current_gpu_idx];

        // Устанавливаем максимальный уровень сглаживания MSAA который обеспечивает GPU
        int s = qRound(std::log2(gpu_info.framebufferColorSamplesCounts));
        settings_ui->hsMSAA->setMaximum(s);
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::loadGraphicsSettings(QString file_name)
{
    Ui::SettingsWidgets *settings_ui = settingsWindow->getUi();

    FileSystem &fs = FileSystem::getInstance();
    QString config_dir = QString(fs.getConfigDir().c_str());

    settings_path = config_dir + fs.separator() + file_name + ".xml";

    QString secName = "Viewer";

    CfgReader   cfg;

    fd_list.clear();

    if (cfg.load(settings_path))
    {
        int width = 0;
        cfg.getInt(secName, WIDTH, width);
        fd_list.append(QPair<QString, QVariant>(WIDTH, width));

        int height = 0;
        cfg.getInt(secName, HEIGHT, height);
        fd_list.append(QPair<QString, QVariant>(HEIGHT, height));

        int fullscreen = 0;
        cfg.getInt(secName, FULLSCREEN, fullscreen);
        fd_list.append(QPair<QString, QVariant>(FULLSCREEN, fullscreen));

        double fovY = 0;
        cfg.getDouble(secName, FOV_Y, fovY);
        fd_list.append(QPair<QString, QVariant>(FOV_Y, fovY));

        double zNear = 0;
        cfg.getDouble(secName, ZNEAR, zNear);
        fd_list.append(QPair<QString, QVariant>(ZNEAR, zNear));

        double zFar = 0;
        cfg.getDouble(secName, ZFAR, zFar);
        fd_list.append(QPair<QString, QVariant>(ZFAR, zFar));

        int screen_num = 0;
        cfg.getInt(secName, SCREEN_NUM, screen_num);
        fd_list.append(QPair<QString, QVariant>(SCREEN_NUM, screen_num));

        int win_decor = 0;
        cfg.getInt(secName, WIN_DECOR, win_decor);
        fd_list.append(QPair<QString, QVariant>(WIN_DECOR, win_decor));

        int double_buff = 0;
        cfg.getInt(secName, DOUBLE_BUFF, double_buff);
        fd_list.append(QPair<QString, QVariant>(DOUBLE_BUFF, double_buff));

        int vsync = 0;
        cfg.getInt(secName, VSYNC, vsync);
        fd_list.append(QPair<QString, QVariant>(VSYNC, vsync));

        int max_fps = 0;
        cfg.getInt(secName, MAX_FPS, max_fps);
        fd_list.append(QPair<QString, QVariant>(MAX_FPS, max_fps));

        cfg.getInt(secName, PHYSICAL_DEVICE, current_gpu_idx);
        fd_list.append(QPair<QString, QVariant>(PHYSICAL_DEVICE, current_gpu_idx));

        {
            int tmp = 0;
            cfg.getInt(secName, PHYSICAL_DEVICE_VENDOR_ID, tmp);
            fd_list.append(QPair<QString, QVariant>(PHYSICAL_DEVICE_VENDOR_ID, tmp));
        }
        {
            int tmp = 0;
            cfg.getInt(secName, PHYSICAL_DEVICE_DEVICE_ID, tmp);
            fd_list.append(QPair<QString, QVariant>(PHYSICAL_DEVICE_DEVICE_ID, tmp));
        }

        double view_dist = 0;
        cfg.getDouble(secName, VIEW_DIST, view_dist);
        fd_list.append(QPair<QString, QVariant>(VIEW_DIST, view_dist));

        int samples = 0;
        cfg.getInt(secName, SAMPLES, samples);
        fd_list.append(QPair<QString, QVariant>(SAMPLES, samples));

        int depthFormat = 0;
        cfg.getInt(secName, DEPTH_FORMAT, depthFormat);
        fd_list.append(QPair<QString, QVariant>(DEPTH_FORMAT, depthFormat));

        bool shadow = false;
        cfg.getBool(secName, SHADOW, shadow);
        fd_list.append(QPair<QString, QVariant>(SHADOW, shadow));

        int shadows_preset = 0;
        cfg.getInt(secName, SHADOWS_PRESET, shadows_preset);
        fd_list.append(QPair<QString, QVariant>(SHADOWS_PRESET, shadows_preset));

        double shadow_dist = 0;
        cfg.getDouble(secName, SHADOW_DISTANCE, shadow_dist);
        fd_list.append(QPair<QString, QVariant>(SHADOW_DISTANCE, qRound(shadow_dist)));

        int shadows_cascade = 0;
        cfg.getInt(secName, SHADOW_CASCADE, shadows_cascade);
        fd_list.append(QPair<QString, QVariant>(SHADOW_CASCADE, shadows_cascade));

        int shadows_resolution = 0;
        cfg.getInt(secName, SHADOW_RESOLUTION, shadows_resolution);
        fd_list.append(QPair<QString, QVariant>(SHADOW_RESOLUTION, shadows_resolution));

        updateGraphSettings(fd_list, settings_ui);
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::ulockShadowsSettings(Ui::SettingsWidgets *settings_ui, bool lock)
{
    settings_ui->cbShadowsMap->setEnabled(lock);
    settings_ui->hsShadowsCascades->setEnabled(lock);
    settings_ui->hsShadowsDistance->setEnabled(lock);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::updateGraphSettings(FieldsDataList &fd_list, Ui::SettingsWidgets *settings_ui)
{
    settings_ui->sbWindowWidth->setValue(findSetting(WIDTH, fd_list).second.toInt());
    settings_ui->sbWindowHeight->setValue(findSetting(HEIGHT, fd_list).second.toInt());

    findSetting(FULLSCREEN, fd_list).second == 1 ?
        settings_ui->cbFullScreenMode->setCheckState(Qt::CheckState::Checked) :
        settings_ui->cbFullScreenMode->setCheckState(Qt::CheckState::Unchecked);

    findSetting(WIN_DECOR, fd_list).second == 1 ?
        settings_ui->cbWindowDecoration->setCheckState(Qt::CheckState::Checked) :
        settings_ui->cbWindowDecoration->setCheckState(Qt::CheckState::Unchecked);

    findSetting(VSYNC, fd_list).second == 1 ?
        settings_ui->cbLimitFPS->setCheckState(Qt::CheckState::Unchecked) :
        settings_ui->cbLimitFPS->setCheckState(Qt::CheckState::Checked);

    settings_ui->sbMaxFPS->setValue(findSetting(MAX_FPS, fd_list).second.toInt());

    

    settings_ui->sbDisplayNumber->setValue(findSetting(SCREEN_NUM, fd_list).second.toInt());

    settings_ui->sbViewDistance->setValue(findSetting(VIEW_DIST, fd_list).second.toInt());

    int samples = findSetting(SAMPLES, fd_list).second.toInt();
    int s = qRound(std::log2(samples));
    settings_ui->hsMSAA->setValue(s);    

    bool shadow = findSetting(SHADOW, fd_list).second.toBool();
    shadow ? settings_ui->cbShowShadows->setCheckState(Qt::CheckState::Checked) : settings_ui->cbShowShadows->setCheckState(Qt::CheckState::Unchecked);
    settings_ui->cbSwadowsQuality->setEnabled(true);

    int shadows_preset = findSetting(SHADOWS_PRESET, fd_list).second.toInt();

    if (shadows_preset > 0 && shadows_preset < settings_ui->cbSwadowsQuality->count())
    {
        settings_ui->cbSwadowsQuality->setCurrentIndex(shadows_preset);
    }

    if (shadows_preset >= shadowsPresets.size())
    {
        ulockShadowsSettings(settings_ui, true);
    }
    else
    {
        ulockShadowsSettings(settings_ui, false);
    }

    for (size_t i = 0; i < shadowMapResolution.size(); ++i)
    {
        if (shadowMapResolution[i] == findSetting(SHADOW_RESOLUTION, fd_list).second.toInt())
        {
            settings_ui->cbShadowsMap->setCurrentIndex(i);
        }
    }

    settings_ui->hsShadowsCascades->setValue(findSetting(SHADOW_CASCADE, fd_list).second.toInt());
    settings_ui->lShadowsCascades->setText(QString("%1").arg(settings_ui->hsShadowsCascades->value()));

    settings_ui->hsShadowsDistance->setValue(findSetting(SHADOW_DISTANCE, fd_list).second.toInt());
    settings_ui->lShadowsDistance->setText(QString("%2").arg(settings_ui->hsShadowsDistance->value()));

    settings_ui->pbGraphApply->setEnabled(false);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::applyGraphSettings(FieldsDataList &fd_list,
                                             Ui::SettingsWidgets *settings_ui)
{
    int idx = 0;

    findSetting(WIDTH, fd_list, idx);
    fd_list[idx] = QPair<QString, QVariant>(WIDTH, settings_ui->sbWindowWidth->value());

    findSetting(HEIGHT, fd_list, idx);
    fd_list[idx] = QPair<QString, QVariant>(HEIGHT, settings_ui->sbWindowHeight->value());

    findSetting(FULLSCREEN, fd_list, idx);
    if (settings_ui->cbFullScreenMode->checkState() == Qt::CheckState::Checked)
    {
        fd_list[idx] = QPair<QString, QVariant>(FULLSCREEN, 1);
    }
    else
    {
        fd_list[idx] = QPair<QString, QVariant>(FULLSCREEN, 0);
    }

    findSetting(VSYNC, fd_list, idx);
    if (settings_ui->cbLimitFPS->checkState() == Qt::CheckState::Checked)
    {
        fd_list[idx] = QPair<QString, QVariant>(VSYNC, 0);
    }
    else
    {
        fd_list[idx] = QPair<QString, QVariant>(VSYNC, 1);
    }

    findSetting(MAX_FPS, fd_list, idx);
    fd_list[idx] = QPair<QString, QVariant>(MAX_FPS, settings_ui->sbMaxFPS->value());

    findSetting(PHYSICAL_DEVICE, fd_list, idx);
    fd_list[idx] = QPair<QString, QVariant>(PHYSICAL_DEVICE, current_gpu_idx);

    int gpu_idx = current_gpu_idx;
    if (gpu_idx >= 0 && gpu_idx < static_cast<int>(gpus_info.size()))
    {
        findSetting(PHYSICAL_DEVICE_VENDOR_ID, fd_list, idx);
        fd_list[idx] = QPair<QString, QVariant>(PHYSICAL_DEVICE_VENDOR_ID, static_cast<uint32_t>(gpus_info[gpu_idx].vendorID));

        findSetting(PHYSICAL_DEVICE_DEVICE_ID, fd_list, idx);
        fd_list[idx] = QPair<QString, QVariant>(PHYSICAL_DEVICE_DEVICE_ID, static_cast<uint32_t>(gpus_info[gpu_idx].deviceID));
    }

    findSetting(WIN_DECOR, fd_list, idx);
    if (settings_ui->cbWindowDecoration->checkState() == Qt::CheckState::Checked)
    {
        fd_list[idx] = QPair<QString, QVariant>(WIN_DECOR, 1);
    }
    else
    {
        fd_list[idx] = QPair<QString, QVariant>(WIN_DECOR, 0);
    }

    findSetting(SCREEN_NUM, fd_list, idx);
    fd_list[idx] = QPair<QString, QVariant>(SCREEN_NUM, settings_ui->sbDisplayNumber->value());

    findSetting(VIEW_DIST, fd_list, idx);
    fd_list[idx] = QPair<QString, QVariant>(VIEW_DIST, settings_ui->sbViewDistance->value());

    findSetting(SAMPLES, fd_list, idx);
    int samples = (1 << settings_ui->hsMSAA->value());
    fd_list[idx] = QPair<QString, QVariant>(SAMPLES, samples);

    findSetting(SHADOW, fd_list, idx);
    fd_list[idx] = QPair<QString, QVariant>(SHADOW, settings_ui->cbShowShadows->checkState() == Qt::CheckState::Checked ? true : false);

    findSetting(SHADOW_RESOLUTION, fd_list, idx);
    fd_list[idx] = QPair<QString, QVariant>(SHADOW_RESOLUTION, shadowMapResolution[settings_ui->cbShadowsMap->currentIndex()]);

    findSetting(SHADOW_CASCADE, fd_list, idx);
    fd_list[idx] = QPair<QString, QVariant>(SHADOW_CASCADE, settings_ui->hsShadowsCascades->value());

    findSetting(SHADOW_DISTANCE, fd_list, idx);
    fd_list[idx] = QPair<QString, QVariant>(SHADOW_DISTANCE, settings_ui->hsShadowsDistance->value());

    findSetting(SHADOWS_PRESET, fd_list, idx);
    fd_list[idx] = QPair<QString, QVariant>(SHADOWS_PRESET, settings_ui->cbSwadowsQuality->currentIndex());
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::saveGraphSettings(FieldsDataList &fd_list)
{
    CfgEditor editor;

    editor.editFile(settings_path, "Viewer", fd_list);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::showSettingsTab()
{
    // при каждом показе вкладки перечитываем настройки с диска
    loadGraphicsSettings("settings");

    Ui::SettingsWidgets *settings_ui = settingsWindow->getUi();

    // Индекс пресета мог прийти из настроек невалидным — тогда ставим
    // «Среднее», чтобы поле не оставалось пустым
    if (settings_ui->cbSwadowsQuality->currentIndex() < 0)
    {
        settings_ui->cbSwadowsQuality->setCurrentIndex(1);
    }

    if (settings_ui->cbShadowsMap->currentIndex() < 0)
    {
        settings_ui->cbShadowsMap->setCurrentIndex(1);
    }

    settings_ui->pbGraphApply->setEnabled(false);

    // список видеокарт может быть ещё не заполнен (проверка Vulkan
    // не выполнялась) — тогда заполняем его здесь
    if (settingsWindow != nullptr && !gpus_info.empty())
    {
        setSettingsGPU(gpus_info);
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::setDefaultSettingsForChangedGPU(int gpu_idx)
{
    Ui::SettingsWidgets *settings_ui = settingsWindow->getUi();

    auto &gpu_info = gpus_info[gpu_idx];    

    applyGraphSettings(fd_list, settings_ui);
    updateGraphSettings(fd_list, settings_ui);
}


//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::slotApplyGraphSettings()
{
    Ui::SettingsWidgets *settings_ui = settingsWindow->getUi();

    applyGraphSettings(fd_list, settings_ui);

    updateGraphSettings(fd_list, settings_ui);

    settings_ui->pbGraphApply->setEnabled(false);

    saveGraphSettings(fd_list);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::slotCancelGraphSettings()
{
    updateGraphSettings(fd_list, settingsWindow->getUi());
    settingsWindow->getUi()->pbGraphApply->setEnabled(false);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::updateTrainPhoto()
{
    QLabel *photo = ui->lblTrainPhoto;
    const QPixmap source(":/images/images/magnistral_train.png");

    if (photo == nullptr || source.isNull())
    {
        return;
    }

    QWidget *card = photo->parentWidget();

    // Свободное место внутри карточки: вычитаем заголовок и поля
    QRect card_rect = card->contentsRect();

    // Фото вписывается целиком с сохранением пропорций: не больше ширины
    // карточки и не больше её высоты (исходник не меняется)
    QSize area(card_rect.width() - 24, card_rect.height() - 40);

    if (area.width() < 32 || area.height() < 32)
    {
        return;
    }

    // картинка масштабируется и центрируется в карточке (alignment в .ui)
    QPixmap scaled = source.scaled(area, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    photo->setPixmap(scaled);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    updateTrainPhoto();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    updateTrainPhoto();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::init()
{
    const FileSystem &fs = FileSystem::getInstance();

    loadRoutesList(fs.getRouteRootDir());
    loadTrainsList(fs.getTrainsDir());
    loadServersList(fs.getConfigDir());    
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::loadRoutesList(const std::string &routesDir)
{
    QDir routes(QString(routesDir.c_str()));
    QDirIterator route_dirs(routes.path(), QStringList(), QDir::NoDotAndDotDot | QDir::Dirs);

    while (route_dirs.hasNext())
    {
        route_info_t route_info;
        route_info.route_dir_full_path = route_dirs.next();
        route_info.route_dir_name = route_dirs.fileName();

        CfgReader cfg;

        if (cfg.load(route_info.route_dir_full_path + QDir::separator() + "description.xml"))
        {
            QString secName = "Route";

            cfg.getString(secName, "Title", route_info.route_title);
            cfg.getString(secName, "Description", route_info.route_description);
        }

        loadTrajectories(route_info);
        loadTrainPositions(route_info);
        loadScenarios(route_info);

        routes_info.emplace_back(std::move(route_info));
    }

    for (const route_info_t& route_info : routes_info)
    {
        ui->lwRoutes->addItem(route_info.route_title);
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::loadTrainsList(const std::string &trainsDir)
{
    QDir    trains(QString(trainsDir.c_str()));
    QDirIterator train_files(trains.path(), QStringList() << "*.xml", QDir::NoDotAndDotDot | QDir::Files);

    while (train_files.hasNext())
    {
        train_info_t train_info;
        QString fullPath = train_files.next();
        QFileInfo fileInfo(fullPath);

        train_info.train_config_path = fileInfo.baseName();

        CfgReader cfg;

        if (cfg.load(fullPath))
        {
            QString secName = "Common";

            cfg.getString(secName, "Title", train_info.train_title);
            cfg.getString(secName, "Description", train_info.description);
        }

        trains_info.push_back(train_info);
        ui->lwTrains->addItem(train_info.train_title);
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::loadServersList(const std::string &cfgDir)
{
    saved_servers.clear();
    ui->cbSavedServers->clear();

    saved_servers_path = QString(cfgDir.c_str()) + QDir::separator() + QString(SAVED_SERVERS_FILE.c_str());
    CfgReader cfg;
    if (cfg.load(saved_servers_path))
    {
        QDomNode server_node = cfg.getFirstSection("Server");
        while (!server_node.isNull())
        {
            server_info_t server = server_info_t();

            QString tmp_host_address = "";
            int tmp_port = 0;

            cfg.getString(server_node, "Name", server.server_name);

            if (cfg.getString(server_node, "HostAddr", tmp_host_address))
                server.setHostAddress(tmp_host_address);

            if (cfg.getInt(server_node, "port", tmp_port))
                server.ipv4_port = static_cast<uint16_t>(tmp_port);

            saved_servers.insert(server.server_name, server);

            server_node = cfg.getNextSection();
        }
    }

    if (saved_servers.empty())
    {
        server_info_t local_server = server_info_t();
        saved_servers.insert(local_server.server_name, local_server);
    }

    for (auto& ss : saved_servers)
    {
        ui->cbSavedServers->addItem(ss.server_name + " (" + ss.getHostAddressAndPort() + ")");
    }

    slotSelectSavedServer(0);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::loadTrajectories(route_info_t &route_info)
{
    route_info.trajectrories.clear();

    QString traj_dir_path = route_info.route_dir_full_path + QDir::separator() +
                            "topology" + QDir::separator() +
                            "trajectories";

    QDir traj_dir(traj_dir_path);
    QDirIterator traj_files(traj_dir.path(), QStringList() << "*.traj", QDir::NoDotAndDotDot | QDir::Files);
    while (traj_files.hasNext())
    {
        trajectory_info_t traj_info;
        QString fullPath = traj_files.next();
        QFileInfo fileInfo(fullPath);
        traj_info.name = fileInfo.baseName();
        route_info.trajectrories.push_back(traj_info);
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::loadTrainPositions(route_info_t &route_info)
{
    route_info.fwd_train_positions.clear();
    route_info.bwd_train_positions.clear();

    QString path = route_info.route_dir_full_path + QDir::separator() +
                   "topology" + QDir::separator() +
                   "waypoints.conf";

    QFile waypoints_file(path);

    if (!waypoints_file.open(QIODevice::ReadOnly))
    {
        return;
    }

    QTextStream stream(&waypoints_file);

    while (!stream.atEnd())
    {
        QString line = stream.readLine();
        QStringList tokens = line.split('\t');

        train_position_t tp;
        tp.name = tokens[0];
        tp.trajectory_name = tokens[1];
        tp.direction = tokens[2].toInt();
        tp.traj_coord = tokens[3].toDouble();
        tp.railway_coord = tokens[4].toDouble();

        if (tp.direction > 0)
        {
            route_info.fwd_train_positions.push_back(tp);
        }
        else
        {
            route_info.bwd_train_positions.push_back(tp);
        }
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::loadScenarios(route_info_t &route_info)
{
    route_info.scenarios.clear();

    QString scenarios_path = route_info.route_dir_full_path + QDir::separator()
                             + "scenarios";

    scenarios_path = QDir::toNativeSeparators(scenarios_path);

    QDir scenarios_dir(scenarios_path);

    if (!scenarios_dir.exists())
    {
        return;
    }

    // РџРµСЂРµР±РёСЂР°РµРј РІСЃРµ РїРѕРґРєР°С‚Р°Р»РѕРіРё РІ РїР°РїРєРµ scenarios РґР°РЅРЅРѕРіРѕ РјР°СЂС€СЂСѓС‚Р°
    QDirIterator it(scenarios_path, QDir::Dirs | QDir::NoDotAndDotDot);

    while (it.hasNext())
    {
        // РџСЂРѕРІРµСЂСЏРµРј РЅР°Р»РёС‡РёРµ РІ РЅР°Р№РґРµРЅРЅРѕРј РїРѕРґРєР°С‚Р°Р»РѕРіРµ С„Р°Р№Р»Р° main.lua
        QString abs_path = it.next();
        QString main_path = QDir::toNativeSeparators(abs_path + QDir::separator() + "main.lua");
        QFile main_file(main_path);

        if (main_file.exists())
        {
            scenario_t scn;

            // Р С‚РѕР»СЊРєРѕ РІ СЃР»СѓС‡Р°Рµ РЅР°Р»РёС‡РёСЏ С‚Р°РєРѕРІРѕРіРѕ - РґРѕР±Р°РІР»СЏРµРј РєР°С‚Р°Р»РѕРі РІ СЃРїРёСЃРѕРє
            // РґРѕСЃС‚СѓРїРЅС‹С… СЃС†РµРЅР°СЂРёРµРІ
            scn.scenario_name = scenarios_dir.relativeFilePath(abs_path);

            // РџСЂРѕРїСѓСЃРєР°РµРј startup-СЃС†РµРЅР°СЂРёР№, С‡С‚РѕР±С‹ СЋР·РµСЂСѓ РіР»Р°Р·Р° РЅРµ РјРѕР·РѕР»РёР»
            if (scn.scenario_name == STARTUP_SCN_SUBDIR)
            {
                continue;
            }

            // Р§РёС‚Р°РµРј РѕРїРёСЃР°РЅРёРµ СЃС†РµРЅР°СЂРёСЏ РёР· README.md
            QString desc_path = QDir::toNativeSeparators(abs_path + QDir::separator() + "README.md");
            scn.scenario_description = loadScenarioDescription(desc_path);

            route_info.scenarios.push_back(scn);
        }
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QString MainWindow::loadScenarioDescription(QString path)
{
    QString desc = "";

    QFile file(path);

    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        desc = QString::fromUtf8(file.readAll());
    }

    return desc;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::clearActiveTrainsList()
{
    if (!is_start_button_to_stop_server)
        ui->pbStartServer->setEnabled(false);



    active_trains.clear();

    while (tbActiveTrains->count() > 0)
    {
        const TrainWaypointWidget *tww = dynamic_cast<TrainWaypointWidget *>(tbActiveTrains->widget(0));
        disconnect(tww, &TrainWaypointWidget::activeTrainChanged,
                   this, &MainWindow::slotUpdateActiveTrains);
        disconnect(tww, &TrainWaypointWidget::trainConfigChanged,
                   this, &MainWindow::slotTrainConfigChanged);

        tbActiveTrains->removeItem(0);
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::reloadScenariosList()
{
    ui->cbScenario->clear();
    ui->cbScenario->addItem(tr("<Not_selected>"));

    for (auto& sc : routes_info[selected_route_idx].scenarios)
    {
        ui->cbScenario->addItem(sc.scenario_name);
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::showTrainsConfigTip()
{
    if (trainsConfigTip != nullptr)
    {
        return;
    }

    QVBoxLayout *layout = qobject_cast<QVBoxLayout *>(ui->frame->layout());

    if (layout == nullptr)
    {
        layout = new QVBoxLayout(this);
        ui->frame->setLayout(layout);
    }

    layout->setAlignment(Qt::AlignCenter);

    trainsConfigTip = new QLabel(tr("Trains positions are defined in scenario"), ui->frame);
    trainsConfigTip->setAlignment(Qt::AlignCenter);

    layout->addWidget(trainsConfigTip);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::hideTrainsConfigsTip()
{
    if (trainsConfigTip == nullptr)
    {
        return;
    }

    QVBoxLayout *layout = qobject_cast<QVBoxLayout *>(ui->frame->layout());

    if (layout == nullptr)
    {
        return;
    }

    layout->removeWidget(trainsConfigTip);
    trainsConfigTip->deleteLater();
    trainsConfigTip = nullptr;

    layout->deleteLater();
    layout = nullptr;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QString getCenteredHtml(QString msg, QString color = "#ff6b6b")
{
    if (color.isEmpty())
        return QString("<p style='text-align: center;'>%1</p>").arg(msg);

    return QString("<p style='text-align: center; color: %1;'>%2</p>")
        .arg(color, msg);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::gpuDiagnostics()
{
    // РџРѕР»СѓС‡Р°РµРј СЃРїРёСЃРѕРє GPU
    auto status = getInfoGPUs(gpus_info);

    start_viewer_allowed = false;
    ui->tbLogGPU->clear();

    switch (status)
    {
    case GPU_STATE_VULKAN_LOADER_NOT_FOUND_ERROR:
        {
        ui->tbLogGPU->setHtml(getCenteredHtml(tr("Start graphics client is impossible: missing Vulkan loader in your system. Check that you have lastest version of driver for your GPU from offcial vendor site.")));
            break;
        }
    case GPU_STATE_VK_INSTANCE_ERROR:
        {
            ui->tbLogGPU->setHtml(getCenteredHtml(tr("Start graphics client is impossible: can not create vkInstance. Check that you have lastest version of driver for your GPU from offcaial vendor site.")));
            break;
        }

    case GPU_STATE_VK_ENUM_PHYSICAL_DEVICE_ERROR:
        {
            ui->tbLogGPU->setHtml(getCenteredHtml(tr("Start graphics client is impossible: GPU driver error, can't get information about GPUs. Check that you have lastest version of driver for your GPU from offcial vendor site")));
            break;
        }

    case GPU_STATE_NO_CAPABLE_DEVICES_ERROR:
        {
            ui->tbLogGPU->setHtml(getCenteredHtml(tr("Start graphics client is impossible: not find GPU devices capable with Vulkan API.")));
            break;
        }

    case GPU_STATE_GET_DEVICES_LIST_ERROR:
        {
            ui->tbLogGPU->setHtml(getCenteredHtml(tr("Start graphics client is impossible: GPU driver error, can't get GPUs list. Check that you have lastest version of driver for your GPU from offcial vendor site")));
            break;
        }

    case GPU_STATE_READY:
        {
            if (gpus_info.empty())
            {
                break;
            }

            // РїРѕСЃР»РµРґРЅРёР№ СЂСѓР±РµР¶ - РїСЂРѕРІРµСЂРєР° РІРµСЂСЃРёРё РћРЎ. РќР° С‚РѕС‚ СЃР»СѓС‡Р°Р№, РєРѕРіРґР° СѓСЃС‚СЂРѕР№СЃС‚РІРѕ
            // РЅР°РґРµР¶РЅРѕ РґРµС‚РµРєС‚РёСЂСѓРµС‚СЃСЏ РєР°Рє Vulkan-СЃРѕРІРјРµСЃС‚РёРјРѕРµ, РЅРѕ РЅР° Р·Р°РІРµРґРѕРјРѕ СЃРѕРІРјРµСЃС‚РёРјРѕРј РѕР±СЂСѓРґРѕРІР°РЅРёРё
            // РЅРµ Р·Р°РїСѓСЃРєР°РµС‚ Рё РїР°РґР°РµС‚ РІСЊСЋРІРµСЂ, Р° РґСЂР°Р№РІРµСЂС‹ РЅРµ С…РѕС‚СЏС‚ СѓСЃС‚Р°РЅР°РІР»РёРІР°С‚СЊСЃСЏ
            // Р¶Р°Р»СѓСЏСЃСЊ РЅР° РІРµСЂСЃРёСЋ РћРЎ.
            // TODO: РїСЂРѕС‚РµСЃС‚РёСЂРѕРІР°С‚СЊ СЌС‚Рѕ!!! РёРЅР°С‡Рµ РѕРіСЂРµР±РµРј РѕС‚ СЋР·РµСЂРѕРІ РїРѕ РїРѕР»РЅРѕР№!
            size_t valid_gpus_count = 0;            
            std::vector<size_t> devices_with_problems;

            for (size_t i = 0; i < gpus_info.size(); ++i)
            {
                if (checkOperationSystemVersion(gpus_info[i].vendorID, winver, gpus_info[i].nameOS))
                {
                    ++valid_gpus_count;
                }
                else
                {
                    devices_with_problems.push_back(i);
                }
            }

            if (valid_gpus_count == 0)
            {
                ui->tbLogGPU->setHtml(getCenteredHtml(tr("Start graphics client is impossible: You operation system %1 is not capable with modern direvers of all your devices. Please, update you operation system").arg(gpus_info[0].nameOS)));
                return;
            }

            if (!devices_with_problems.empty())
            {
                QString devList = "";
                for (auto i : devices_with_problems)
                {
                    devList += gpus_info[i].deviceName + ", ";
                    ui->tbLogGPU->setHtml(getCenteredHtml(tr("WARNING: You devices ") + devList + tr(" may have a problems with start graphics client, becourse modern drivers for them not capable with your OS version"), "#ffff00"));
                }
            }

            start_viewer_allowed = true;

            if (settingsWindow != nullptr)
            {
                setSettingsGPU(gpus_info);
            }

            break;
        }
    }
}


//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::createToolsMenu()
{
    CfgReader cfg;
    FileSystem &fs = FileSystem::getInstance();
    std::string cfg_dir = fs.getConfigDir();
    std::string cfg_path = fs.combinePath(cfg_dir, "launcher.xml");

    if (!cfg.load(QString(cfg_path.c_str())))
    {
        return;
    }

    auto secNode = cfg.getFirstSection("Tool");

    QString binPath = QDir::toNativeSeparators(QString(fs.getBinaryDir().c_str()));

    while (!secNode.isNull())
    {
        QString toolName;
        cfg.getString(secNode, "Name", toolName);

        if (toolName.isEmpty())
        {
            secNode = cfg.getNextSection();
            continue;
        }

        QString toolPath = binPath + QDir::separator() + toolName + EXE_EXP;

        QFile toolFile(toolPath);

        if (!toolFile.exists())
        {
            secNode = cfg.getNextSection();
            continue;
        }

        QProcess *proc = new QProcess(this);
        proc->setWorkingDirectory(binPath);
        proc->setProgram(toolPath);

        QString args_str;
        cfg.getString(secNode, "CommandLine", args_str);
        QStringList args = args_str.split(' ');

        if (!args.empty())
        {
            proc->setArguments(args);
        }

        QString description;
        cfg.getString(secNode, "Description", description);

        if (description.isEmpty())
        {
            description = toolName;
        }

        toolProcs.push_back(proc);

        QAction *action = new QAction(description);

        connect(action, &QAction::triggered, this, [this](){

            QAction *ac = qobject_cast<QAction *>(sender());

            if (ac == nullptr)
            {
                return;
            }

            int idx = ui->menuTools->actions().indexOf(ac);

            if (idx >= 0 && idx < toolProcs.size())
            {
                if (toolProcs[idx] != nullptr && toolProcs[idx]->state() != QProcess::Running)
                {
                    toolProcs[idx]->start();
                }
            }
        });

        ui->menuTools->addAction(action);

        // Меню инструментов появляется с первым загруженным инструментом,
        // и вместе с ним показывается строка меню (без инструментов она
        // скрыта, чтобы не занимать место)
        ui->menuTools->menuAction()->setVisible(true);
        ui->menuBar->setVisible(true);

        secNode = cfg.getNextSection();
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::centerWindow(QWidget *window)
{
    if (!window || !window->parentWidget())
    {
        return;
    }

    QRect parentRect = window->parentWidget()->geometry();
    QRect myRect = window->geometry();
    int x = parentRect.x() + (parentRect.width() - myRect.width()) / 2;
    int y = parentRect.y() + (parentRect.height() - myRect.height()) / 2;
    window->move(x, y);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::updateOptions(FieldsDataList &fd_options)
{}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::applyOptions(FieldsDataList &fd_options, Ui::MainWindow *ui)
{}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::loadActiveTrainsList()
{
    // TODO: Р Р°Р·РЅС‹Рµ С‚РёРїС‹ СЃ size_t
    if ((selected_route_idx < 0) || (selected_route_idx >= routes_info.size()))
    {
        return;
    }

    reloadScenariosList();

    slotUpdateActiveTrains();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::saveOptions(const FieldsDataList &fd_options) const
{
    CfgEditor editor;

    editor.editFile(settings_path, "Launcher", fd_options);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::startSimulator()
{
    if (selectedRouteDirName.isEmpty())
    {
        return;
    }

    bool reset_start_config = false;

    slotUpdateActiveTrains(reset_start_config);

    if (active_trains.empty() && selected_scenario_idx < 0)
    {
        return;
    }

    QStringList args;
    args.clear();

    args << "--route=" + selectedRouteDirName;

    // Текущий момент: если выбран сценарий, время и погода задаются им
    QDateTime start_datetime_now = QDateTime::currentDateTime(QTimeZone::systemTimeZone());
    QDate start_date_now = start_datetime_now.date();
    QTime start_time_now = start_datetime_now.time();
    server_date_t start_date = server_date_t(
        static_cast<int16_t>(start_date_now.year()),
        static_cast<uint8_t>(start_date_now.month()),
        static_cast<uint8_t>(start_date_now.day()));
    server_time_t start_time = server_time_t(
        static_cast<uint8_t>(start_time_now.hour()),
        static_cast<uint8_t>(start_time_now.minute()),
        static_cast<uint8_t>(start_time_now.second()),
        static_cast<uint16_t>(start_time_now.msec()));
    std::int64_t start_datetime = simulator_time_t(start_date, start_time).data();

    args << "--start=" + QString::number(start_datetime);

    if (selected_scenario_idx >= 0)
    {
        args << "--scenario=" + routes_info[selected_route_idx].scenarios[selected_scenario_idx].scenario_name;
    }
    else
    {
        auto scnCode = createTmpScenarioCode(active_trains);
        createScenario(selectedRouteDirName, scnCode);
        args << "--scenario=" + STARTUP_SCN_SUBDIR;
    }

    const FileSystem &fs = FileSystem::getInstance();
    QString simPath = SIMULATOR_NAME + EXE_EXP;

    simulatorProc.setWorkingDirectory(QString(fs.getBinaryDir().c_str()));
    simulatorProc.start(QString::fromStdString(fs.getBinaryDir()) + '/' + simPath, args);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::startViewer(bool local)
{
    if (!start_viewer_allowed)
    {
        return;
    }

    server_info_t server;
    if (local)
    {
        server = server_info_t();
    }
    else
    {
        server.ipv4_1 = ui->sbIPv4_1->value();
        server.ipv4_2 = ui->sbIPv4_2->value();
        server.ipv4_3 = ui->sbIPv4_3->value();
        server.ipv4_4 = ui->sbIPv4_4->value();
        server.ipv4_port = ui->sbIPv4_port->value();
    }

    QStringList args;
    args << "--host-address" << server.getHostAddress();
    args << "--port" << QString::number(server.ipv4_port);

    const FileSystem &fs = FileSystem::getInstance();
    QString viewerPath = VIEWER_NAME + EXE_EXP;

    if (local)
    {
        viewerProc.setWorkingDirectory(QString(fs.getBinaryDir().c_str()));
        viewerProc.start(QString::fromStdString(fs.getBinaryDir()) + '/' + viewerPath, args);
    }
    else
    {
        QProcess *proc = new QProcess;
        connect(proc, &QProcess::finished, this, &MainWindow::slotAdditionalProcFinished);
        proc->setWorkingDirectory(QString(fs.getBinaryDir().c_str()));
        proc->start(QString::fromStdString(fs.getBinaryDir()) + '/' + viewerPath, args);
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::startMap(bool local)
{
    server_info_t server;
    if (local)
    {
        server = server_info_t();
    }
    else
    {
        server.ipv4_1 = ui->sbIPv4_1->value();
        server.ipv4_2 = ui->sbIPv4_2->value();
        server.ipv4_3 = ui->sbIPv4_3->value();
        server.ipv4_4 = ui->sbIPv4_4->value();
        server.ipv4_port = ui->sbIPv4_port->value();
    }

    QStringList args;
    args << "--host-address" << server.getHostAddress();
    args << "--port" << QString::number(server.ipv4_port);

    const FileSystem &fs = FileSystem::getInstance();
    QString mapPath = ROUTE_MAP_NAME + EXE_EXP;

    if (local)
    {
        mapProc.setWorkingDirectory(QString(fs.getBinaryDir().c_str()));
        mapProc.start(QString::fromStdString(fs.getBinaryDir()) + '/' + mapPath, args);
    }
    else
    {
        QProcess *proc = new QProcess;
        connect(proc, &QProcess::finished, this, &MainWindow::slotAdditionalProcFinished);
        proc->setWorkingDirectory(QString(fs.getBinaryDir().c_str()));
        proc->start(QString::fromStdString(fs.getBinaryDir()) + '/' + mapPath, args);
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::loadSettingsGUI()
{
    FileSystem &fs = FileSystem::getInstance();
    std::string cfg_dir = fs.getConfigDir();
    std::string cfg_path = fs.combinePath(cfg_dir, "gui-settings.xml");

    CfgReader cfg;

    if ( cfg.load(QString(cfg_path.c_str())) )
    {
        QString secName = "GUISettings";
        QString theme_name = "";

        if (!cfg.getString(secName, "Theme", theme_name))
        {
            theme_name = "dark-jedy";
        }

        std::string theme_dir = fs.getThemeDir();
        std::string theme_path = fs.combinePath(theme_dir, theme_name.toStdString() + ".qss");
        QString style_sheet = readStyleSheet(QString(theme_path.c_str()));

        this->setStyleSheet(style_sheet);
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::loadConfig()
{
    FileSystem &fs = FileSystem::getInstance();
    std::string cfg_dir = fs.getConfigDir();
    std::string cfg_path = fs.combinePath(cfg_dir, "launcher.xml");

    CfgReader cfg;

    if ( cfg.load(QString(cfg_path.c_str())) )
    {
        QString secName = "Launcher";
        QString theme_name = "";

        if (!cfg.getString(secName, "Theme", theme_name))
        {
            theme_name = "dark-jedy";
        }

        std::string theme_dir = fs.getThemeDir();
        std::string theme_path = fs.combinePath(theme_dir, theme_name.toStdString() + ".qss");
        QString style_sheet = readStyleSheet(QString(theme_path.c_str()));

        this->setStyleSheet(style_sheet);

        cfg.getInt(secName, "MinWinver", winver.majorVer);
        cfg.getInt(secName, "MinWinBuild_NVIDIA", winver.buildNvidia);
        cfg.getInt(secName, "MinWinBuild_AMD", winver.buildAMD);
        cfg.getInt(secName, "MinWinBuild_INTEL", winver.buildIntel);

        settings_path = QString(cfg_path.c_str());

        updateOptions(fd_options);
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::slotRouteSelection()
{
    int route_idx = ui->lwRoutes->currentRow();
    if (selected_route_idx == route_idx)
        return;

    // РћС‡РёСЃС‚РєР° РІС‹Р±СЂР°РЅРЅС‹С… Р°РєС‚РёРІРЅС‹С… РїРѕРµР·РґРѕРІ
    clearActiveTrainsList();
    ui->ptRouteDescription->clear();

    if (route_idx == -1)
    {
        selected_route_idx = -1;
        return;
    }

    selectedRouteDirName = routes_info[route_idx].route_dir_name;
    ui->ptRouteDescription->appendPlainText(routes_info[route_idx].route_description);

    trajectrories = &routes_info[route_idx].trajectrories;
    fwd_train_positions = &routes_info[route_idx].fwd_train_positions;
    bwd_train_positions = &routes_info[route_idx].bwd_train_positions;

    // Р—Р°РіСЂСѓР·РєР° РїСЂРµРґС‹РґСѓС‰РёС… РІС‹Р±СЂР°РЅРЅС‹С… Р°РєС‚РёРІРЅС‹С… РїРѕРµР·РґРѕРІ
    selected_route_idx = route_idx;
    loadActiveTrainsList();


}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::slotTrainSelection()
{
    size_t item_idx = static_cast<size_t>(ui->lwTrains->currentRow());

    ui->ptTrainDescription->clear();
    selectedTrain = trains_info[item_idx].train_config_path;
    ui->ptTrainDescription->appendPlainText(trains_info[item_idx].description);


}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::slotAddActiveTrain()
{
    if (ui->lwRoutes->currentRow() < 0)
        return;

    TrainWaypointWidget *tww = new TrainWaypointWidget(&trains_info,
                                                       trajectrories,
                                                       fwd_train_positions,
                                                       bwd_train_positions,
                                                       &icon_ok,
                                                       &icon_cancel,
                                                       &icon_warn,
                                                       this);

    int train_idx = ui->lwTrains->currentRow();
    if ((tww->cbTrainConfigSelect->count() > train_idx) && (train_idx >= 0))
        tww->cbTrainConfigSelect->setCurrentIndex(train_idx + 1);

    int new_item_idx = tbActiveTrains->addItem(tww, tww->getTrainName());
    tbActiveTrains->setCurrentIndex(new_item_idx);
    connect(tww, &TrainWaypointWidget::activeTrainChanged,
               this, &MainWindow::slotUpdateActiveTrains);
    connect(tww, &TrainWaypointWidget::trainConfigChanged,
            this, &MainWindow::slotTrainConfigChanged);

    slotUpdateActiveTrains();

    ui->cbScenario->setEnabled(false);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::slotDeleteActiveTrain()
{
    if (tbActiveTrains->count() <= 0)
    {
        return;
    }

    int cur = tbActiveTrains->currentIndex();
    TrainWaypointWidget *tww = dynamic_cast<TrainWaypointWidget *>(tbActiveTrains->widget(cur));
    if (tww)
    {
        tbActiveTrains->removeItem(cur);
        disconnect(tww, &TrainWaypointWidget::activeTrainChanged,
                   this, &MainWindow::slotUpdateActiveTrains);
        disconnect(tww, &TrainWaypointWidget::trainConfigChanged,
                   this, &MainWindow::slotTrainConfigChanged);
        delete tww;
    }

    slotUpdateActiveTrains();    

    if (active_trains.size() == 0)
    {
        ui->cbScenario->setEnabled(true);
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::slotTrainConfigChanged()
{
    TrainWaypointWidget *tww = dynamic_cast<TrainWaypointWidget *>(sender());
    if (tww)
    {
        int idx = tbActiveTrains->indexOf(tww);
        if (idx == -1)
            return;

        tbActiveTrains->setItemText(idx, tww->getTrainName());

        int train_idx = tww->cbTrainConfigSelect->currentIndex() - 1;
        // TODO: Р Р°Р·РЅС‹Рµ С‚РёРїС‹ СЃ size_t
        if ((train_idx >= 0) && (train_idx < trains_info.size()))
        {
            ui->lwTrains->setCurrentRow(train_idx, QItemSelectionModel::ClearAndSelect);
        }
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::slotUpdateActiveTrains(bool reset_start_config)
{
    (void)reset_start_config;

    active_trains.clear();

    int active_trains_count = tbActiveTrains->count();
    if (active_trains_count <= 0)
    {
        if (!is_start_button_to_stop_server)
            ui->pbStartServer->setEnabled(false);


        ui->cbScenario->setEnabled(true);

        return;
    }



    for (int i = 0; i < active_trains_count; ++i)
    {
        TrainWaypointWidget *tww = dynamic_cast<TrainWaypointWidget *>(tbActiveTrains->widget(i));
        if (tww)
        {
            active_train_t at = tww->getActiveTrainConfig();
            if (at.is_active)
            {
                active_trains.push_back(at);
                tbActiveTrains->setItemIcon(i, icon_ok);
            }
            else
            {
                tbActiveTrains->setItemIcon(i, icon_warn);
            }
        }
    }

    if (!is_start_button_to_stop_server)
        ui->pbStartServer->setEnabled(!active_trains.empty() || selected_scenario_idx >= 0);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::slotStartServerPressed()
{
    // Check button is stop running server mode
    if (is_start_button_to_stop_server)
    {
        terminateProcess(&simulatorProc);
        return;
    }

    // Check is route selected
    if (selectedRouteDirName.isEmpty())
    {
        return;
    }

    startSimulator();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::slotSimulatorStarted()
{
    is_start_button_to_stop_server = true;
    ui->pbStartServer->setProperty("state", "stop");
    repolish(ui->pbStartServer);
    ui->pbStartServer->setText(tr("Stop server"));
    ui->pbStartServer->setEnabled(true);

    // Просмотрщик и карта запускаются всегда автоматически
    startViewer();
    startMap();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::slotViewerStarted()
{}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::slotMapStarted()
{}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::slotConnectViewerPressed()
{
    bool local = false;
    startViewer(local);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::slotConnectMapPressed()
{
    bool local = false;
    startMap(local);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::slotSimulatorFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitCode)
    Q_UNUSED(exitStatus);

    is_start_button_to_stop_server = false;
    ui->pbStartServer->setProperty("state", QVariant());
    repolish(ui->pbStartServer);
    ui->pbStartServer->setText(tr("Start server"));

    if (active_trains.empty() || (ui->lwRoutes->currentRow() < 0))
    {
        ui->pbStartServer->setEnabled(false);
    }
    else
    {
        ui->pbStartServer->setEnabled(true);
    }

    if (selected_scenario_idx >= 0)
    {
        ui->pbStartServer->setEnabled(true);
    }
    if (viewerProc.state() != QProcess::NotRunning)
       terminateProcess(&viewerProc);

    if (mapProc.state() != QProcess::NotRunning)
        terminateProcess(&mapProc);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::slotViewerFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitCode)
    Q_UNUSED(exitStatus)

    if (simulatorProc.state() != QProcess::NotRunning)
        setFocusPolicy(Qt::StrongFocus);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::slotMapFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitCode)
    Q_UNUSED(exitStatus)

    if (simulatorProc.state() != QProcess::NotRunning)
        setFocusPolicy(Qt::StrongFocus);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::slotAdditionalProcFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitCode)
    Q_UNUSED(exitStatus)

    QProcess *proc = dynamic_cast<QProcess *>(sender());
    disconnect(proc, &QProcess::finished, this, &MainWindow::slotAdditionalProcFinished);
    delete proc;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::slotSelectSavedServer(int idx)
{
    server_info_t server = server_info_t();
    int i = 0;
    for (const auto& ss : saved_servers)
    {
        if (i == idx)
        {
            server = ss;
            break;
        }
        ++i;
    }
    ui->cbSavedServers->setCurrentIndex(idx);
    ui->leServerName->setText(server.server_name);
    ui->sbIPv4_1->setValue(server.ipv4_1);
    ui->sbIPv4_2->setValue(server.ipv4_2);
    ui->sbIPv4_3->setValue(server.ipv4_3);
    ui->sbIPv4_4->setValue(server.ipv4_4);
    ui->sbIPv4_port->setValue(server.ipv4_port);
    slotChangedServerSettings();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::slotChangedServerSettings()
{
    if (ui->leServerName->text().isEmpty())
    {
        ui->pbSaveServer->setText(tr("Save Server"));
        ui->pbSaveServer->setEnabled(false);
    }
    else
    {
        if (saved_servers.contains(ui->leServerName->text()))
        {
            ui->pbSaveServer->setText(tr("Rewrite Server"));

            server_info_t saved_server = saved_servers.value(ui->leServerName->text());
            if ((saved_server.ipv4_1 == ui->sbIPv4_1->value()) &&
                (saved_server.ipv4_2 == ui->sbIPv4_2->value()) &&
                (saved_server.ipv4_3 == ui->sbIPv4_3->value()) &&
                (saved_server.ipv4_4 == ui->sbIPv4_4->value()) &&
                (saved_server.ipv4_port == ui->sbIPv4_port->value()))
            {
                ui->pbSaveServer->setEnabled(false);
            }
            else
            {
                ui->pbSaveServer->setEnabled(true);
            }
        }
        else
        {
            ui->pbSaveServer->setEnabled(true);
            ui->pbSaveServer->setText(tr("Save Server"));
        }
    }
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::slotSaveServer()
{
    if (ui->leServerName->text().isEmpty())
        return;

    server_info_t server = server_info_t();
    server.server_name = ui->leServerName->text();
    server.ipv4_1 = ui->sbIPv4_1->value();
    server.ipv4_2 = ui->sbIPv4_2->value();
    server.ipv4_3 = ui->sbIPv4_3->value();
    server.ipv4_4 = ui->sbIPv4_4->value();
    server.ipv4_port = ui->sbIPv4_port->value();

    auto saved_it = saved_servers.insert(server.server_name, server);
    int idx = ui->cbSavedServers->currentIndex();

    CfgEditor editor;
    editor.openFileForWrite(saved_servers_path);
    editor.setIndentationFormat(-1);

    ui->cbSavedServers->clear();
    int i = 0;
    for (auto it = saved_servers.begin(); it != saved_servers.end(); ++it)
    {
        if (it == saved_it)
            idx = i;
        ++i;

        server_info_t current_server = it.value();
        ui->cbSavedServers->addItem(current_server.server_name + " (" + current_server.getHostAddressAndPort() + ")");

        FieldsDataList flist;
        flist.append(QPair<QString, QString>("Name", current_server.server_name));
        flist.append(QPair<QString, QString>("HostAddr", current_server.getHostAddress()));
        flist.append(QPair<QString, QString>("port", QString::number(current_server.ipv4_port)));
        editor.writeFile("Server", flist);
    }

    editor.closeFileAfterWrite();

    slotSelectSavedServer(idx);
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::slotOnScenarioSelection(int cur_idx)
{
    ui->tbScenarioDescription->clear();
    selected_scenario_idx = -1;

    if (cur_idx <= 0)
    {
        ui->pbStartServer->setEnabled(false);
        hideTrainsConfigsTip();

        return;
    }

    QString desc = routes_info[selected_route_idx].scenarios[cur_idx - 1].scenario_description;
    ui->tbScenarioDescription->setMarkdown(desc);

    selected_scenario_idx = cur_idx - 1;

    ui->pbStartServer->setEnabled(true);

    showTrainsConfigTip();

}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::slotSaveTrainsConfigAsScenario()
{
    // Панель сохранения сценария убрана из интерфейса: сценарий
    // создаётся только через стартовый набор, ручного сохранения нет
    return;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QString MainWindow::createLuaSetDate(QDateEdit *dateEdit)
{
    QString setDate = QString("setDate(\"%1.%2.%3\")")
                  .arg(dateEdit->dateTime().date().day(), 2, 10, QChar('0'))
                  .arg(dateEdit->dateTime().date().month(), 2, 10, QChar('0'))
                  .arg(dateEdit->dateTime().date().year(), 4, 10, QChar('0'));

    return setDate;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QString MainWindow::createLuaSetTime(QTimeEdit *timeEdit)
{
    QString setTime = QString("setTime(\"%1:%2:%3\")")
                  .arg(timeEdit->dateTime().time().hour(), 2, 10, QChar('0'))
                  .arg(timeEdit->dateTime().time().minute(), 2, 10, QChar('0'))
                  .arg(timeEdit->dateTime().time().second(), 2, 10, QChar('0'));

    return setTime;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QStringList MainWindow::createLuaSetTrain(size_t idx, const active_train_t &at)
{
    QStringList setTrainCode;
    setTrainCode.clear();

    QString varName = QString("train%1").arg(idx);

    setTrainCode.append(varName + QString(" = TrainData.new()"));
    setTrainCode.append(varName + QString(".name = \"%1\"").arg(idx));
    setTrainCode.append(varName + QString(".config = \"%1\"").arg(at.train_info.train_config_path));
    setTrainCode.append(varName + QString(".traj = \"%1\"").arg(at.train_position.trajectory_name));
    setTrainCode.append(varName + QString(".coord = %1").arg(at.train_position.traj_coord));
    setTrainCode.append(varName + QString(".dir = %1").arg(at.train_position.direction));

    if (at.is_autopilot_on)
    {
        setTrainCode.append(varName + QString(".auto = true"));
    }

    setTrainCode.append(QString("setTrain(%1)").arg(varName));

    return setTrainCode;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
QStringList MainWindow::createTmpScenarioCode(const std::vector<active_train_t> &active_trains)
{
    QStringList scnCode;
    scnCode.clear();

    for (size_t i = 0; i < active_trains.size(); ++i)
    {
        scnCode += createLuaSetTrain(i, active_trains[i]);
        scnCode.append("\n");
    }

    return scnCode;
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::createScenario(const QString &route_name,
                                const QStringList &scnCode,
                                const QString &scenario_name)
{
    if (scnCode.empty())
    {
        return;
    }

    const FileSystem &fs = FileSystem::getInstance();
    std::string route_path = fs.getRouteRootDir() + fs.separator()
                             + route_name.toStdString();

    QDir routeDir(route_path.c_str());

    if (!routeDir.exists())
    {
        return;
    }

    QString scn_subdir_name = "scenarios";

    if (!routeDir.exists(scn_subdir_name))
    {
        if (!routeDir.mkdir(scn_subdir_name))
        {
            return;
        }
    }

    std::string scenarios_path = route_path + fs.separator()
                                 + scn_subdir_name.toStdString();

    QDir scnDir(scenarios_path.c_str());

    if (!scnDir.exists(scenario_name))
    {
        if (!scnDir.mkdir(scenario_name))
        {
            return;
        }
    }

    std::string file_path = scenarios_path + fs.separator()
                            + scenario_name.toStdString()
                            + fs.separator() + "main.lua";

    QFile file(file_path.c_str());

    if (!file.open(QIODevice::Text | QIODevice::WriteOnly))
    {
        return;
    }

    QTextStream stream(&file);

    for (const auto &line : scnCode)
    {
        stream << line << "\n";
    }

    file.close();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
void MainWindow::closeEvent(QCloseEvent *event)
{
    if (simulatorProc.state() == QProcess::ProcessState::Running)
    {
        if (terminateProcess(&simulatorProc))
        {
            event->accept();
            return;
        }
        else
        {
            event->ignore();
            return;
        }
    }

    event->accept();
}

//------------------------------------------------------------------------------
//
//------------------------------------------------------------------------------
bool MainWindow::terminateProcess(QProcess *proc) const
{
    if (proc == nullptr)
    {
        return false;
    }

    // РџРѕСЃС‹Р»Р°РµРј РїСЂРѕС†РµСЃСЃСѓ SIGTERM
    proc->terminate();

    // Р•СЃР»Рё РѕРЅ РЅРµ Р·Р°РІРµСЂС€РёР»СЃСЏ
    if (!proc->waitForFinished(PROC_WAIT_TIMEOUT))
    {
        // РџРѕСЃС‹Р»Р°РµРј SIGKILL
        proc->kill();

        // Р–РґРµРј Р·Р°РІРµСЂС€РµРЅРёСЏ
        return proc->waitForFinished(PROC_WAIT_TIMEOUT);
    }

    return true;
}
