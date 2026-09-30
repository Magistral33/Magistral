#ifndef     TU10_DIESEL_H
#define     TU10_DIESEL_H

#include    "device.h"

//------------------------------------------------------------------------------
// Дизель ТУ10
//------------------------------------------------------------------------------
class Tu10Diesel : public Device
{
public:

    Tu10Diesel(QObject *parent = nullptr);

    virtual ~Tu10Diesel();

    /// Текущая угловая скорость дизеля, рад/с
    double getOmega() const;

    /// Текущие обороты, об/мин
    double getRPM() const;

    /// Момент дизеля, Н*м
    double getTorque() const;

    /// Дизель запущен
    bool isRunning() const;

    /// Задать обороты (команда от МПСУ)
    void setTargetRPM(double rpm);

    /// Команда "Старт"
    void setStartCommand(bool state);

    /// Команда "Стоп"
    void setStopCommand(bool state);

    /// Состояние звука дизеля
    virtual sound_state_t getSoundState(size_t idx = 0) const override;

    /// Сигнал звука дизеля
    virtual float getSoundSignal(size_t idx = 0) const override;

protected:

    /// Уравнения состояния
    virtual void ode_system(const state_vector_t &Y,
                            state_vector_t &dYdt,
                            double t) override;

    /// Загрузка конфига из XML
    virtual void load_config(CfgReader &cfg) override;

    /// Подготовка шага
    virtual void preStep(state_vector_t &Y, double t) override;

    /// Постобработка
    virtual void postStep(state_vector_t &Y, double t) override;

    /// Реакция на клавиши
    virtual void stepKeysControl(double t, double dt) override;

private:

    /// Параметры дизеля из XML
    double  Mc          = 300.0;    // Н*м - номинальный момент
    double  J           = 10.0;     // кг*м^2 - момент инерции вала
    double  omega_min   = 20.0;     // рад/с - мин. угловая скорость
    double  omega_max   = 210.0;    // рад/с - макс. угловая скорость
    double  start_time  = 8.0;      // с - время запуска
    double  Qmax        = 0.125;    // кг/с - макс. подача топлива

    // Кривая момента (полином)
    double  K0 = 0.125;
    double  K1 = 3.0e-4;
    double  K2 = 0.3;
    double  K3 = 7.0e4;
    double  K4 = 0.01;

    /// Состояние дизеля
    bool    is_running      = false;
    bool    start_command   = false;
    bool    stop_command    = false;
    double  start_timer     = 0.0;
    double  target_omega    = 0.0;

    /// Индекс состояния: 0 - omega (угловая скорость)
    enum { IDX_OMEGA = 0 };

    /// Создание звука
    sound_state_t sound_state;

    /// Вспомогательная функция - момент дизеля
    double dieselTorque(double omega) const;
};

#endif // TU10_DIESEL_H

