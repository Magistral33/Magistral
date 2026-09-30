#ifndef     TU10_HEAD_H
#define     TU10_HEAD_H

#include    "vehicle.h"
#include    "tu10-diesel.h"

//------------------------------------------------------------------------------
// Головной вагон ТУ10
//------------------------------------------------------------------------------
class Tu10Head : public Vehicle
{
    Q_OBJECT

public:

    Tu10Head(QObject *parent = nullptr);

    virtual ~Tu10Head();

    /// Инициализация — создаём и регистрируем все Device'ы
    virtual void initialization() override;

    /// Загрузка конфигурации вагона
    virtual void loadConfig(QString cfg_path) override;

    /// Главный цикл вагона
    virtual void process(const simulator_time_t &t, const double &dt) override;

protected:

    /// Дизель
    Tu10Diesel  *diesel = nullptr;

    /// Обработка клавиш (старт/стоп дизеля)
    virtual void stepKeysControl(double t, double dt);

    /// Сопротивление движению (можно оставить базовое)
    virtual double mainResist(const double &velocity) override;

    /// Трение колесо-рельс
    virtual double wheelrailFriction(const double &velocity) override;
};

#endif // TU10_HEAD_H

