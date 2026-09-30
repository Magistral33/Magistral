#include    "tu10-diesel.h"
#include    "CfgReader.h"
#include    "physics.h"
#include    <Journal.h>

Tu10Diesel::Tu10Diesel(QObject *parent) : Device(parent)
{
    setName("Tu10Diesel");
}

Tu10Diesel::~Tu10Diesel()
{
}

//------------------------------------------------------------------------------
// Загрузка конфига из XML
//------------------------------------------------------------------------------
void Tu10Diesel::load_config(CfgReader &cfg)
{
    QString secName = "Device";

    cfg.getDouble(secName, "Mc",         Mc);
    cfg.getDouble(secName, "J",          J);
    cfg.getDouble(secName, "omega_min",  omega_min);
    cfg.getDouble(secName, "omega_max",  omega_max);
    cfg.getDouble(secName, "start_time", start_time);
    cfg.getDouble(secName, "Qmax",       Qmax);
    cfg.getDouble(secName, "K0",         K0);
    cfg.getDouble(secName, "K1",         K1);
    cfg.getDouble(secName, "K2",         K2);
    cfg.getDouble(secName, "K3",         K3);
    cfg.getDouble(secName, "K4",         K4);

    Journal::instance()->info("Tu10Diesel: config loaded");
}

//------------------------------------------------------------------------------
// Момент дизеля как функция угловой скорости
//------------------------------------------------------------------------------
double Tu10Diesel::dieselTorque(double omega) const
{
    if (!is_running) return 0.0;
    if (omega < omega_min) omega = omega_min;
    if (omega > omega_max) omega = omega_max;

    // Полиномиальная кривая: M = Mc * (1 - K2 * (omega/omega_max - K3/omega - K4))
    // Упрощённо для старта — используем момент близкий к номинальному
    return Mc * (1.0 - K2 * (omega / omega_max));
}

//------------------------------------------------------------------------------
// Уравнения состояния (ODE)
//------------------------------------------------------------------------------
void Tu10Diesel::ode_system(const state_vector_t &Y,
                             state_vector_t &dYdt,
                             double t)
{
    Q_UNUSED(t)
    double omega = Y[IDX_OMEGA];

    // J * dω/dt = M_diesel - M_load
    // Пока нагрузка не подключена (нет гидропередачи) — считаем её нулевой
    double M_diesel = dieselTorque(omega);
    double M_load   = 0.0;

    dYdt[IDX_OMEGA] = (M_diesel - M_load) / J;
}

//------------------------------------------------------------------------------
// Подготовка шага
//------------------------------------------------------------------------------
void Tu10Diesel::preStep(state_vector_t &Y, double t)
{
    Q_UNUSED(Y)
    Q_UNUSED(t)
}

//------------------------------------------------------------------------------
// Постобработка
//------------------------------------------------------------------------------
void Tu10Diesel::postStep(state_vector_t &Y, double t)
{
    Q_UNUSED(t)

    double omega = Y[IDX_OMEGA];

    // Ограничения по угловой скорости
    if (is_running) {
        if (omega < omega_min) {
            Y[IDX_OMEGA] = omega_min;
        }
        if (omega > omega_max) {
            Y[IDX_OMEGA] = omega_max;
        }
    } else {
        if (omega < 0.0) Y[IDX_OMEGA] = 0.0;
    }
}

//------------------------------------------------------------------------------
// Реакция на клавиши
//------------------------------------------------------------------------------
void Tu10Diesel::stepKeysControl(double t, double dt)
{
    Q_UNUSED(t)

    // Логика пуска/останова
    if (start_command && !is_running) {
        start_timer += dt;
        if (start_timer >= start_time) {
            is_running = true;
            start_timer = 0.0;
            y[IDX_OMEGA] = omega_min;
            Journal::instance()->info("Tu10Diesel: engine started");
        }
    } else if (!start_command) {
        start_timer = 0.0;
    }

    if (stop_command && is_running) {
        is_running = false;
        Journal::instance()->info("Tu10Diesel: engine stopped");
    }
}

//------------------------------------------------------------------------------
// Публичные геттеры
//------------------------------------------------------------------------------
double Tu10Diesel::getOmega() const
{
    return y[IDX_OMEGA];
}

double Tu10Diesel::getRPM() const
{
    return getOmega() * 60.0 / (2.0 * Physics::PI);
}

double Tu10Diesel::getTorque() const
{
    return dieselTorque(getOmega());
}

bool Tu10Diesel::isRunning() const
{
    return is_running;
}

void Tu10Diesel::setTargetRPM(double rpm)
{
    target_omega = rpm * 2.0 * Physics::PI / 60.0;
}

void Tu10Diesel::setStartCommand(bool state)
{
    start_command = state;
}

void Tu10Diesel::setStopCommand(bool state)
{
    stop_command = state;
}

//------------------------------------------------------------------------------
// Звук
//------------------------------------------------------------------------------
sound_state_t Tu10Diesel::getSoundState(size_t idx) const
{
    Q_UNUSED(idx)
    return sound_state;
}

float Tu10Diesel::getSoundSignal(size_t idx) const
{
    Q_UNUSED(idx)
    if (!is_running) return sound_state_t::createSoundSignal(false);

    // Относительный уровень звука: 0.0 - холостой ход, 1.0 - максимум
    double level = (getOmega() - omega_min) / (omega_max - omega_min);
    if (level < 0.0) level = 0.0;
    if (level > 1.0) level = 1.0;

    return sound_state_t::createSoundSignal(true, level);
}

