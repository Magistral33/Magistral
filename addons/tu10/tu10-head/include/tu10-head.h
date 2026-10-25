#ifndef     TU10_HEAD_H
#define     TU10_HEAD_H

#include    "vehicle.h"
#include    "tu10-diesel.h"

class CoilALSN;
class SpeedMap;

//------------------------------------------------------------------------------
// ID сигналов обратной связи (feedback_signals).
// Индексы совпадают с <SignalID> в XML-анимациях
// RRS/data/animations/tu10/*.xml
//------------------------------------------------------------------------------
enum FeedbackSignalId
{
    FS_SPEED        = 0,    // скорость (для спидометра и колёс)
    FS_REVERSOR     = 1,    // положение реверсора: -1, 0, +1
    FS_THROTTLE     = 2,    // положение контроллера: 0..1
    FS_BRAKE        = 3,    // тормоз: 0..1
    FS_DIESEL_RPM   = 4,    // обороты дизеля: 0..1 (нормализовано)
    FS_MAIN_RES     = 5,    // напорная магистраль: 0..1 (0..1.0 МПа)
    FS_BRAKE_LINE   = 6,    // тормозная магистраль: 0..1 (0..1.0 МПа)
    FS_BRAKE_CYL    = 7,    // тормозные цилиндры: 0..1 (0..0.4 МПа)
    FS_CURRENT      = 8,    // тахометр (вместо амперметра): 0..1 (0..2150 об/мин)
    FS_KRAN_MAIN    = 9,    // кран машиниста (395): 0..1 (5 положений)
    FS_KRAN_LOCO    = 10,   // кран вспомогательного тормоза (254): 0..1 (6 положений)
    FS_MASS         = 11,   // ключ «Масса»
    FS_ESUD         = 12,   // АВ «ЭСУД»
    FS_PRIB_PANEL   = 13,   // АВ «Приборная панель»
    FS_CONTROL      = 14,   // тумблер «Управление»
    FS_START_BTN    = 15,   // кнопка «Пуск»
    FS_STOP_BTN     = 16,   // кнопка «Стоп»
    FS_DIAG_BTN     = 17    // кнопка «Диагностика»
};

//------------------------------------------------------------------------------
// Р“РѕР»РѕРІРЅРѕР№ РІР°РіРѕРЅ РўРЈ10
//------------------------------------------------------------------------------
class Tu10Head : public Vehicle
{
    Q_OBJECT

public:

    Tu10Head(QObject *parent = nullptr);

    virtual ~Tu10Head();

    virtual void initialization() override;
    virtual void loadConfig(QString cfg_path) override;
    virtual void process(const simulator_time_t &t, const double &dt) override;

protected:

    Tu10Diesel  *diesel = nullptr;

    virtual void stepKeysControl(double t, double dt);
    virtual void stepCabControls(double dt);
    virtual void applyTraction(const double &dt);
    virtual void stepPneumatics(const double &dt);

    virtual double mainResist(const double &velocity) override;
    virtual double wheelrailFriction(const double &velocity) override;

    // --- РЎРѕСЃС‚РѕСЏРЅРёРµ СѓРїСЂР°РІР»РµРЅРёСЏ ---
    double  throttle        = 0.0;
    double  throttle_rate   = 0.25;
    int     direction       = 0;
    bool    prev_R          = false;
    double  brake           = 0.0;
    double  brake_rate      = 0.5;
    double  brake_force_max = 12000.0;

    // --- РўСЏРіР° ---
    double  gear_ratio         = 15.0;
    double  gear_efficiency    = 0.85;
    double  max_traction_force = 62700.0;   // 62,7 кН по инструкции (мю = 0,33)

    // --- Пневматика и ток (показания приборов) ---
    double  main_res_pressure   = 0.80;     // МПа - напорная магистраль
    double  brake_line_pressure = 0.50;     // МПа - тормозная магистраль
    double  brake_cyl_pressure  = 0.00;     // МПа - тормозные цилиндры
    double  traction_current    = 0.0;      // А   - ток ТЭД

    // --- Управление органами кабины (IOController -> control_inputs) ---
    bool    prev_ctrl_km    = false;    // ID 100 - контроллер машиниста
    bool    prev_ctrl_rvs   = false;    // ID 101 - реверсор
    bool    prev_ctrl_brake = false;    // ID 102 - кран машиниста (395)
    bool    prev_ctrl_loco  = false;    // ID 103 - кран 254
    bool    cab_start       = false;    // ID 104 - пуск дизеля (кнопка)
    bool    cab_stop        = false;    // ID 105 - стоп дизеля (кнопка)

    // --- Положения кранов (по инструкции) ---
    // Кран машиниста 395: 0-I отпуск, 1-II поездное, 2-III перекрыша,
    //                     3-IV служебное, 4-V экстренное
    int     kran_main       = 1;
    // Кран вспомогательного тормоза 254: 0 - отпуск, 1..4 - ступени, 5 - полное
    int     kran_loco       = 0;

    bool    prev_Space      = false;
    bool    prev_H          = false;
    bool    prev_B          = false;

    // --- Панель №4: последовательность пуска по инструкции ---
    bool    mass_on         = false;    // ключ «Масса»
    bool    esud_on         = false;    // АВ «ЭСУД»
    bool    prib_panel_on   = false;    // АВ «Приборная панель»
    bool    control_on      = false;    // тумблер «Управление» (с блокировкой)
    bool    cab_diag        = false;    // кнопка «Диагностика»
    double  diag_timer      = 0.0;      // самотест ЭСУД, с
    bool    diag_ready      = false;    // лампа диагностики погасла
    double  hint_timer      = 0.0;      // чтобы не спамить в журнал
    double  start_pulse     = 0.0;      // импульс кнопки «Пуск», с
    double  stop_pulse      = 0.0;      // импульс кнопки «Стоп», с
    bool    prev_start_btn  = false;
    bool    prev_stop_btn   = false;
    bool    prev_S          = false;
    bool    prev_T          = false;
    bool    prev_klub_K     = false;   // K - подтверждение бдительности
    bool    prev_klub_J     = false;   // J - РМП (режим П/М)
    bool    prev_klub_I     = false;   // I - ВК (переход с «К» на «Б»)
    bool    prev_klub_U     = false;   // U - К20 (Vдоп 20 км/ч)

    // Путь к конфигу вагона: нужен, чтобы прочитать disel.xml после
    // создания дизеля (loadConfig вызывается раньше initialization).
    QString config_path = "";

    // Отладка
    double  dbg_timer = 0.0;

    // --- КЛУБ-У ---
    CoilALSN *alsn_coil = nullptr;      // приёмная катушка АЛС
    SpeedMap *speedmap  = nullptr;      // ограничения скорости по маршруту
    double  prev_velocity = 0.0;
    double  cur_acceleration = 0.0;     // м/с^2

    // --- Клавиатура и режимы КЛУБ-У (ID 150..175) ---
    bool    prev_klub_keys[26] = {false};
    QString klub_input;                 // набранное на клавиатуре число
    double  klub_coord_offset = 0.0;    // поправка координаты, м
    bool    klub_ind_p        = true;   // индикатор «П» (сигнал 188), поездной
    bool    klub_ind_m        = false;  // индикатор «М» (сигнал 187), маневровый
    bool    klub_cassette     = false;  // кассета (189)
    bool    klub_zapret       = false;  // запрет отпуска (180), ручной
    bool    klub_side         = false;  // боковое направление (185)
    int     klub_limit_offset = 0;      // поправка следующего ограничения
    double  klub_flash_timer  = 0.0;    // проверка ламп, с

    // Сигнал локомотивного светофора БИЛ: 0=К, 1=КЖ, 2=Ж, 3=З, 4=Б
    int     klub_signal       = 4;
    int     klub_prev_signal  = -1;     // для однократной проверки при смене
    int     klub_prev_alsn    = -1;     // предыдущий сигнал АЛС (для «ВК»/«К20»)
    bool    klub_k20          = false;  // кнопка «К20»: Vдоп 20 км/ч
    bool    klub_vk           = false;  // кнопка «ВК»: с «К» на «Б»
    double  klub_v_dop        = 50.0;   // Vдоп для индикации (191)
    double  klub_v_tsel       = 50.0;   // Vцел для индикации (192)

    // Контроль скорости: свисток ЭПК при превышении Vдоп на 1 км/ч
    bool    klub_whistle      = false;
    double  klub_whistle_rest = 0.0;    // остаток до экстренного, с

    // Проверка бдительности (п. 5.4)
    bool    klub_check_active = false;  // идёт проверка (сигнал 186)
    bool    klub_check_single = false;  // однократная (без второго этапа)
    bool    klub_check_horn   = false;  // второй этап: свисток ЭПК
    double  klub_check_rest   = 0.0;    // остаток текущего этапа, с
    double  klub_next_check   = 0.0;    // до следующей периодической, с
    int     klub_band         = -1;     // группа интервалов (2: 30-40, 4: 60-90)
    bool    klub_moving       = false;  // локомотив уже двигался

    // Контроль трогания: 70 с от тяговой позиции до движения (п. 5.3.1)
    double  klub_traction_timer = 0.0;

    // Тормоз
    double  klub_brake           = 0.0;     // автоторможение, 0..1 (доля 0,38 МПа)
    bool    klub_auto_brake      = false;   // КЛУБ держит тормоз
    bool    klub_autostop        = false;   // автостоп до подтверждения клавишей К

    /// Предел скорости по маршруту, но не выше конструкционной, км/ч
    double klubSpeedLimit() const;

    /// Контроль скорости и проверка бдительности
    void stepKlubSafety(double dt);
    /// Начать проверку бдительности (однократную или периодическую)
    void klubStartCheck(bool periodic);
    /// Экстренное торможение через ЭПК (автостоп)
    void klubEmergency(const QString &reason);
    /// Случайный интервал периодических проверок для группы, с
    static double klubRandomInterval(int band);

    /// Клавиатура КЛУБ-У: обработка нажатий (ID 150..175)
    void stepKlubKeys(double dt);

    /// Одиночное нажатие клавиши КЛУБ-У
    void onKlubKey(int id);

    /// Заполнить аналоговые сигналы для дисплея КЛУБ-У (club-u-display)
    void fillKlubSignals(std::vector<float> *as, const simulator_time_t &t);
};

#endif // TU10_HEAD_H
