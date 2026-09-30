#include    "tu10-head.h"
#include    "CfgReader.h"
#include    "physics.h"
#include    "filesystem.h"
#include    <Journal.h>

#include    <core/get_module.h>

//------------------------------------------------------------------------------
// Конструктор
//------------------------------------------------------------------------------
Tu10Head::Tu10Head(QObject *parent) : Vehicle(parent)
{
    setModuleName("tu10-head");
}

//------------------------------------------------------------------------------
// Деструктор
//------------------------------------------------------------------------------
Tu10Head::~Tu10Head()
{
}

//------------------------------------------------------------------------------
// Инициализация: создаём Device'ы и регистрируем их в вагоне
//------------------------------------------------------------------------------
void Tu10Head::initialization()
{
    Journal::instance()->info("Tu10Head: initialization");

    // Создаём дизель
    diesel = new Tu10Diesel(this);
    diesel->setName("Diesel");
    diesel->setCustomConfigDir(config_dir);

    // Регистрируем как forward-connector (упрощённо — пока просто в системе)
    // Позже — правильная топология пневматики и тяги
    // addFwdConnector(diesel);  // раскомментируем, когда будем подключать к тормозам

    Journal::instance()->info("Tu10Head: devices created");
}

//------------------------------------------------------------------------------
// Загрузка конфигурации вагона
//------------------------------------------------------------------------------
void Tu10Head::loadConfig(QString cfg_path)
{
    Journal::instance()->info("Tu10Head: loadConfig " + cfg_path);

    // Загружаем конфиг дизеля, если он создан
    if (diesel) {
        diesel->read_config("disel", config_dir);
    }
}

//------------------------------------------------------------------------------
// Главный цикл вагона
//------------------------------------------------------------------------------
void Tu10Head::process(const simulator_time_t &t, const double &dt)
{
    // Шаг дизеля (ODE)
    if (diesel) {
        diesel->step(t.simulation_seconds, dt);
    }
}

//------------------------------------------------------------------------------
// Обработка клавиш
//------------------------------------------------------------------------------
void Tu10Head::stepKeysControl(double t, double dt)
{
    Q_UNUSED(t)
    Q_UNUSED(dt)

    if (!diesel) return;

    // Пока — простая логика: клавиша "S" (Start) и "T" (sTop)
    // В дальнейшем — через control_signals от тумблеров
    bool start = getKeyState(KEY_S);
    bool stop  = getKeyState(KEY_T);

    diesel->setStartCommand(start);
    diesel->setStopCommand(stop);
}

//------------------------------------------------------------------------------
// Сопротивление движению (базовое)
//------------------------------------------------------------------------------
double Tu10Head::mainResist(const double &velocity)
{
    return Vehicle::mainResist(velocity);
}

//------------------------------------------------------------------------------
// Трение колесо-рельс (базовое)
//------------------------------------------------------------------------------
double Tu10Head::wheelrailFriction(const double &velocity)
{
    return Vehicle::wheelrailFriction(velocity);
}

//------------------------------------------------------------------------------
// Точка входа DLL — макрос из core/get_module.h
//------------------------------------------------------------------------------
GET_MODULE(Tu10Head)


