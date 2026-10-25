#include    "tu10-head.h"
#include    "CfgReader.h"
#include    "physics.h"
#include    "filesystem.h"
#include    <Journal.h>

#include    <QFileInfo>
#include    <QRandomGenerator>

#include    <core/get_module.h>

#include "ALSN-coil.h"
#include "speedmap.h"

// Панель №4 (Масса/ЭСУД/Приборная панель/Управление/Пуск/Стоп/Диагностика)
// временно убрана из модели. Пока её нет, пуск дизеля работает напрямую
// клавишей S или кнопкой «Пуск» без блокировок цепочки.
static const bool kPanel4Installed = false;

// --- КЛУБ-У: уставки по «Инструкции по эксплуатации КЛУБ-У» (МПС РФ, 2002) ---
// Конструкционная скорость ТУ10: маршрутное ограничение выше не имеет смысла.
static const double KLUB_MAX_SPEED_KMH = 50.0;
// Превышение Vдоп, при котором включается свисток ЭПК (п. 5.5.1.2), км/ч.
static const double KLUB_WHISTLE_OVER = 1.0;
// Время на подтверждение бдительности и на снижение скорости после
// свистка: (6 ± 0,5) с (п. 5.4.2, 5.4.3, 5.5.1.2).
static const double KLUB_CHECK_TIMEOUT = 6.0;
// Время от установки контроллера в тяговую позицию до трогания (п. 5.3.1).
static const double KLUB_START_TIMEOUT = 70.0;
// Скорость по белому сигналу «Б» (п. 5.5.4.6) и в маневровом режиме (п. 5.7.1).
static const double KLUB_VBEL_KMH = 40.0;
static const double KLUB_MANEUVER_KMH = 60.0;

//------------------------------------------------------------------------------
Tu10Head::Tu10Head(QObject *parent) : Vehicle(parent)
{
    setModuleName("tu10-head");
}

//------------------------------------------------------------------------------
Tu10Head::~Tu10Head()
{
}

//------------------------------------------------------------------------------
void Tu10Head::initialization()
{
    Journal::instance()->info("Tu10Head: initialization");

    diesel = new Tu10Diesel(this);
    diesel->setName("Diesel");

    // Конфиг дизеля (disel.xml) читаем здесь: loadConfig() движок вызывает
    // раньше initialization(), когда объект дизеля ещё не создан.
    const QString cfg_dir = QFileInfo(config_path).absolutePath();
    if (!cfg_dir.isEmpty())
    {
        diesel->setCustomConfigDir(cfg_dir);
        diesel->read_config("disel", cfg_dir);
    }

    // КЛУБ-У: приёмник АЛС и карта ограничений скорости.
    // Оба устройства - железнодорожные коннекторы, их опрашивает симулятор
    // через траектории trajectory-ALSN и trajectory-speedmap.
    alsn_coil = new CoilALSN(this);
    alsn_coil->setDirection(1);
    addRailwayConnector(alsn_coil, getLength() / 2.0);

    speedmap = new SpeedMap(this);
    speedmap->setDirection(1);
    addRailwayConnector(speedmap, getLength() / 2.0);

    Journal::instance()->info("Tu10Head: devices created");
}

//------------------------------------------------------------------------------
void Tu10Head::loadConfig(QString cfg_path)
{
    Journal::instance()->info("Tu10Head: loadConfig " + cfg_path);
    config_path = cfg_path;
}

//------------------------------------------------------------------------------
void Tu10Head::process(const simulator_time_t &t, const double &dt)
{
    stepCabControls(dt);
    stepKeysControl(t.simulation_seconds, dt);

    if (diesel) {
        diesel->step(t.simulation_seconds, dt);
    }

    applyTraction(dt);
    stepPneumatics(dt);

    // --- КЛУБ-У: опрос катушки АЛС и карты ограничений ---
    if (dt > 1e-6) {
        // Ускорение сглаживаем: скорость приходит квантованной, и «сырое»
        // значение скачет на ±0.6 м/с2, из-за чего табло КЛУБ-У мигает.
        const double raw_acceleration = (velocity - prev_velocity) / dt;
        cur_acceleration += (raw_acceleration - cur_acceleration) * qMin(1.0, 3.0 * dt);
        prev_velocity = velocity;
    }
    if (alsn_coil) alsn_coil->step(t.simulation_seconds, dt);
    if (speedmap)  speedmap->step(t.simulation_seconds, dt);

    // --- КЛУБ-У: контроль скорости и проверка бдительности ---
    stepKlubSafety(dt);

    // --- Отдаём сигналы во viewer напрямую в analogSignal ---
    std::vector<float>* as = getAnalogSignals();
    if (as)
    {
        if (as->size() < 100) as->resize(100, 0.0f);

        static double _as_log_t = 0.0;
        _as_log_t += dt;
        if (_as_log_t >= 2.0) {
            _as_log_t = 0.0;
            Journal::instance()->info(QString("Tu10Head AS: speed=%1 dir=%2 thr=%3 brk=%4 | main=%5 line=%6 cyl=%7 rpm=%8 | k395=%9 k254=%10 | m=%11 e=%12 pp=%13 u=%14 st=%15")
                .arg((*as)[FS_SPEED], 0, 'f', 2)
                .arg((*as)[FS_REVERSOR], 0, 'f', 2)
                .arg((*as)[FS_THROTTLE], 0, 'f', 2)
                .arg((*as)[FS_BRAKE], 0, 'f', 2)
                .arg((*as)[FS_MAIN_RES], 0, 'f', 2)
                .arg((*as)[FS_BRAKE_LINE], 0, 'f', 2)
                .arg((*as)[FS_BRAKE_CYL], 0, 'f', 2)
                .arg((*as)[FS_CURRENT], 0, 'f', 2)
                .arg(kran_main)
                .arg(kran_loco)
                .arg(mass_on ? 1 : 0)
                .arg(esud_on ? 1 : 0)
                .arg(prib_panel_on ? 1 : 0)
                .arg(control_on ? 1 : 0)
                .arg(cab_start ? 1 : 0));
        }
        // Скорость: км/ч -> 0..1 (диапазон 0..60 км/ч)
        double speed_kmh = qAbs(velocity) * 3.6;
        float  speed_norm = static_cast<float>(qBound(0.0, speed_kmh / 60.0, 1.0));

        if (as->size() > (size_t)FS_SPEED)      (*as)[FS_SPEED]      = speed_norm;
        if (as->size() > (size_t)FS_REVERSOR)   (*as)[FS_REVERSOR]   = static_cast<float>(direction);
        if (as->size() > (size_t)FS_THROTTLE)   (*as)[FS_THROTTLE]   = static_cast<float>(throttle);
        if (as->size() > (size_t)FS_BRAKE)      (*as)[FS_BRAKE]      = static_cast<float>(brake);

        // Обороты дизеля: 0..2150 -> 0..1
        if (diesel) {
            double rpm = diesel->getRPM();
            float rpm_norm = static_cast<float>(qBound(0.0, rpm / 2150.0, 1.0));
            if (as->size() > (size_t)FS_DIESEL_RPM) (*as)[FS_DIESEL_RPM] = rpm_norm;
        }

        // --- Показания приборов кабины ---
        // Напорная магистраль: 0..1.0 МПа
        if (as->size() > (size_t)FS_MAIN_RES)
            (*as)[FS_MAIN_RES] = static_cast<float>(qBound(0.0, main_res_pressure / 1.0, 1.0));

        // Тормозная магистраль: 0..1.0 МПа
        if (as->size() > (size_t)FS_BRAKE_LINE)
            (*as)[FS_BRAKE_LINE] = static_cast<float>(qBound(0.0, brake_line_pressure / 1.0, 1.0));

        // Тормозные цилиндры: 0..0.4 МПа
        if (as->size() > (size_t)FS_BRAKE_CYL)
            (*as)[FS_BRAKE_CYL] = static_cast<float>(qBound(0.0, brake_cyl_pressure / 0.4, 1.0));

        // Тахометр на месте «ТОК»: 0..2150 об/мин
        if (as->size() > (size_t)FS_CURRENT)
            (*as)[FS_CURRENT] = static_cast<float>(qBound(0.0, (diesel ? diesel->getRPM() : 0.0) / 2150.0, 1.0));

        // Положения кранов - для анимации рукояток
        if (as->size() > (size_t)FS_KRAN_MAIN)
            (*as)[FS_KRAN_MAIN] = static_cast<float>(kran_main / 4.0);
        if (as->size() > (size_t)FS_KRAN_LOCO)
            (*as)[FS_KRAN_LOCO] = static_cast<float>(kran_loco / 5.0);

        // Панель №4 - для анимации ключей, тумблеров и кнопок
        if (as->size() > (size_t)FS_MASS)       (*as)[FS_MASS]       = mass_on       ? 1.0f : 0.0f;
        if (as->size() > (size_t)FS_ESUD)       (*as)[FS_ESUD]       = esud_on       ? 1.0f : 0.0f;
        if (as->size() > (size_t)FS_PRIB_PANEL) (*as)[FS_PRIB_PANEL] = prib_panel_on ? 1.0f : 0.0f;
        if (as->size() > (size_t)FS_CONTROL)    (*as)[FS_CONTROL]    = control_on    ? 1.0f : 0.0f;
        if (as->size() > (size_t)FS_START_BTN)  (*as)[FS_START_BTN]  = cab_start     ? 1.0f : 0.0f;
        if (as->size() > (size_t)FS_STOP_BTN)   (*as)[FS_STOP_BTN]   = cab_stop      ? 1.0f : 0.0f;
        if (as->size() > (size_t)FS_DIAG_BTN)   (*as)[FS_DIAG_BTN]   = cab_diag      ? 1.0f : 0.0f;

        // --- данные для дисплея КЛУБ-У ---
        fillKlubSignals(as, t);

        // отладочный вывод КЛУБ-У (раз в 2 с)
        static double _klub_log_t = 0.0;
        _klub_log_t += dt;
        if (_klub_log_t >= 2.0) {
            _klub_log_t = 0.0;
            Journal::instance()->info(QString("Tu10Head KLUB: size=%1 code=%2 spd=%3 lim=%4 coord=%5 dist=%6 time=%7 acc=%8")
                .arg(static_cast<int>(as->size()))
                .arg((*as)[182], 0, 'f', 0)
                .arg((*as)[190], 0, 'f', 1)
                .arg((*as)[191], 0, 'f', 0)
                .arg((*as)[179], 0, 'f', 1)
                .arg((*as)[194], 0, 'f', 1)
                .arg((*as)[206], 0, 'f', 0)
                .arg((*as)[199], 0, 'f', 2));
        }
    }
}

//------------------------------------------------------------------------------
// Пневматика и ток тяговых электродвигателей.
// Величины нужны только для показаний приборов в кабине, на динамику вагона
// влияет по-прежнему brake (положение крана).
//------------------------------------------------------------------------------
void Tu10Head::stepPneumatics(const double &dt)
{
    const bool running = (diesel && diesel->isRunning());

    // Компрессор наполняет напорную магистраль, пока работает дизель
    if (running)
    {
        main_res_pressure += 0.02 * dt;
        if (main_res_pressure > 0.90) main_res_pressure = 0.90;
    }

    // --- Тормозная магистраль: положение крана машиниста 395 ---
    // I отпуск и II поездное - зарядка 0,50 МПа; III перекрыша - держим
    // достигнутое давление; IV служебное - разрядка до 0,32; V экстренное - 0,05.
    double target_line = brake_line_pressure;
    switch (kran_main)
    {
    case 0:  target_line = 0.50; break;     // I  отпуск
    case 1:  target_line = 0.50; break;     // II поездное
    case 2:  target_line = brake_line_pressure; break;  // III перекрыша
    case 3:  target_line = 0.32; break;     // IV служебное
    default: target_line = 0.05; break;     // V  экстренное
    }
    const double k_line = (target_line < brake_line_pressure) ? 1.6 : 0.7;
    brake_line_pressure += (target_line - brake_line_pressure) * qMin(1.0, k_line * dt);

    // --- Тормозные цилиндры ---
    // От разрядки магистрали (воздухораспределитель 292М) плюс прямое
    // торможение краном 254 (0..5 -> 0..0,38 МПа).
    double cyl_from_line = (0.50 - brake_line_pressure) * 1.15;
    if (cyl_from_line < 0.0) cyl_from_line = 0.0;
    const double cyl_from_254 = (kran_loco / 5.0) * 0.38;
    double target_cyl = qMax(cyl_from_line, cyl_from_254);
    if (target_cyl > 0.38) target_cyl = 0.38;

    // Автоторможение КЛУБ-У: действует независимо от крана 395
    // (служебное - около 0,21 МПа, экстренное - 0,38 МПа).
    if (klub_brake > 0.0)
    {
        const double cyl_from_klub = klub_brake * 0.38;
        if (cyl_from_klub > target_cyl) target_cyl = cyl_from_klub;
    }

    const double k_cyl = (target_cyl > brake_cyl_pressure) ? 1.2 : 0.8;
    brake_cyl_pressure += (target_cyl - brake_cyl_pressure) * qMin(1.0, k_cyl * dt);
    if (brake_cyl_pressure < 0.0) brake_cyl_pressure = 0.0;

    // Эффективность тормоза для динамики: 0..1 по давлению в цилиндрах
    brake = qBound(0.0, brake_cyl_pressure / 0.38, 1.0);

    // Расход воздуха из напорной магистрали на торможение
    main_res_pressure -= brake_cyl_pressure * 0.004 * dt;
    if (main_res_pressure < 0.40) main_res_pressure = 0.40;

    // Ток ТЭД: растёт с положением контроллера и оборотами дизеля
    double target_current = 0.0;
    if (running && throttle > 0.0)
    {
        const double rpm_norm = qBound(0.0, diesel->getRPM() / 2150.0, 1.0);
        target_current = throttle * 1000.0 * (0.35 + 0.65 * rpm_norm);
    }
    traction_current += (target_current - traction_current) * qMin(1.0, 2.5 * dt);
    if (traction_current < 0.0) traction_current = 0.0;
}

//------------------------------------------------------------------------------
// Управление органами кабины (мышь через IOController).
// Движок пишет нажатия в control_inputs[кабина][ID], модуль их читает.
//
// ВАЖНО: IOController ищет управляемый объект по имени МЕША, которое
// возвращает picker (у мешей в glTF есть суффикс .001/.002/.004), а не по
// имени узла. Поэтому ObjectName в tu10-cabine1.xml заданы как
// CAB1_KM_MAIN_HANDLE_POS.002 и т.д.
//
// ID (см. cfg/vehicles/tu10-head/tu10-cabine1.xml):
//   100 - контроллер машиниста: удерживать ЛКМ - набор тяги
//   101 - реверсор:             клик - переключить ход (0 -> +1 -> -1 -> 0)
//   102 - кран тормоза (254):   удерживать ЛКМ - набор торможения
//   103 - кран локомотива (394):удерживать ЛКМ - отпуск тормоза
//   110 - пуск дизеля (кнопка панели №4, пока в модели нет геометрии)
//   111 - стоп дизеля (кнопка панели №4, пока в модели нет геометрии)
//------------------------------------------------------------------------------
void Tu10Head::stepCabControls(double dt)
{
    cab_start = false;
    cab_stop  = false;

    if (control_inputs.empty()) return;

    // Орган включён, если он включён хотя бы в одной кабине
    auto cabValue = [this](const int id) -> bool
    {
        for (const auto &cab : control_inputs)
        {
            if (cab.value(id, 0.0f) > 0.5f) return true;
        }
        return false;
    };

    // Фронт: +1 - нажатие, -1 - отпускание
    auto edge = [&cabValue](const int id, bool &prev) -> int
    {
        const bool cur = cabValue(id);
        int e = 0;
        if      (cur && !prev) e =  1;
        else if (!cur && prev) e = -1;
        prev = cur;
        return e;
    };

    // Контроллер машиниста (КМ): пока держим ЛКМ на рукоятке - плавно
    // набираем тягу. Отпустили - тяга остаётся набранной.
    if (cabValue(100))
    {
        throttle = qMin(1.0, throttle + throttle_rate * dt);
    }

    // Реверсор: каждое нажатие переключает ход
    if (edge(101, prev_ctrl_rvs) > 0)
    {
        if      (direction ==  0) direction =  1;
        else if (direction ==  1) direction = -1;
        else                      direction =  0;
        Journal::instance()->info(QString("Tu10Head: reversor (cabine) -> %1").arg(direction));
    }

    // Кран машиниста 395: каждое нажатие - следующее положение
    // I отпуск -> II поездное -> III перекрыша -> IV служебное -> V экстренное
    if (edge(102, prev_ctrl_brake) > 0)
    {
        kran_main = (kran_main + 1) % 5;
        Journal::instance()->info(QString("Tu10Head: kran 395 -> %1").arg(kran_main));
    }

    // Кран вспомогательного тормоза 254: 0..5
    if (edge(103, prev_ctrl_loco) > 0)
    {
        kran_loco = (kran_loco + 1) % 6;
        Journal::instance()->info(QString("Tu10Head: kran 254 -> %1").arg(kran_loco));
    }

    // Кнопки пуска и останова дизеля (моментальные - формируем импульс,
    // чтобы потерянный отпуск мыши не «залипал»)
    cab_start = cabValue(110);
    cab_stop  = cabValue(111);
    if (cab_start && !prev_start_btn) start_pulse = 1.0;
    if (cab_stop  && !prev_stop_btn)  stop_pulse  = 1.0;
    prev_start_btn = cab_start;
    prev_stop_btn  = cab_stop;

    // --- Панель №4: последовательность по инструкции ---
    mass_on       = cabValue(106);
    esud_on       = cabValue(107);
    prib_panel_on = cabValue(108);
    cab_diag      = cabValue(112);

    // «Управление» работает только при включённых «Массе» и «ЭСУД»
    control_on = cabValue(109) && mass_on && esud_on;

    // Клавиатура КЛУБ-У
    stepKlubKeys(dt);
}

//------------------------------------------------------------------------------
// Клавиатура КЛУБ-У (БИП-УТ): ID 150..175 из tu10-cabine1.xml.
//
// Назначение клавиш по приложению 3 инструкции КЛУБ-У:
//   0..9        - набор числа (до 4 цифр)
//   + / -       - увеличить/уменьшить набранное число
//   П           - режим ввода номера пути
//   Л           - проверка ламп (все ячейки на 2 с)
//   К           - проверка бдительности (РБ)
//   ВК          - выключение сигнала «К», переход на «Б»
//   И           - значок кассеты
//   РМП         - выбор режима «Поездной / Маневровый» (на стоянке)
//   ПОВТЯР      - повтор: короткое мигание зелёных ячеек
//   ОТМР        - направление «прямо/бок»
//   ОС          - отмена действующего ограничения
//   К20         - Vдоп 20 км/ч при сигналах «КЖ» и «К»
//   >О<         - очистка набранного числа
//   F           - самоконтроль индикации (все ячейки на 2 с)
//   вверх/вниз  - поправка следующего ограничения +-5 км/ч
//------------------------------------------------------------------------------
void Tu10Head::stepKlubKeys(double dt)
{
    if (klub_flash_timer > 0.0)
    {
        klub_flash_timer = qMax(0.0, klub_flash_timer - dt);
    }

    if (control_inputs.empty()) return;

    auto keyValue = [this](const int id) -> bool
    {
        for (const auto &cab : control_inputs)
        {
            if (cab.value(id, 0.0f) > 0.5f) return true;
        }
        return false;
    };

    for (int i = 0; i < 26; ++i)
    {
        const int id = 150 + i;
        const bool cur = keyValue(id);

        if (cur && !prev_klub_keys[i])
        {
            onKlubKey(id);
        }

        prev_klub_keys[i] = cur;
    }
}

//------------------------------------------------------------------------------
void Tu10Head::onKlubKey(int id)
{
    Journal *j = Journal::instance();

    j->info(QString("Tu10Head KLUB: клавиша ID %1, набрано '%2'").arg(id).arg(klub_input));

    // Цифры 0..9 (ID 150..159 идут в порядке клавиатуры: 5 6 7 8 9 0 1 2 3 4)
    static const char DIGITS[] = "5678901234";
    if (id >= 150 && id <= 159)
    {
        if (klub_input.size() >= 4) klub_input.remove(0, 1);
        klub_input.append(QLatin1Char(DIGITS[id - 150]));
        return;
    }

    int value = klub_input.isEmpty() ? 0 : klub_input.toInt();

    switch (id)
    {
    case 160:   // +
        klub_input = QString::number(value + 1);
        break;

    case 161:   // -
        klub_input = QString::number(qMax(0, value - 1));
        break;

    case 162:   // П - режим ввода номера пути
        j->info("Tu10Head KLUB: режим ввода номера пути");
        break;

    case 163:   // Л
        klub_flash_timer = 2.0;
        j->info("Tu10Head KLUB: проверка ламп");
        break;

    case 164:   // ВК - выключение сигнала «К» и переход на «Б»
        if (klub_signal == 0 && !klub_ind_m)
        {
            klub_vk = true;
            j->info("Tu10Head KLUB: сигнал «К» выключен, переход на «Б» (Vдоп 40)");
        }
        else
        {
            j->info("Tu10Head KLUB: «ВК» действует при сигнале «К»");
        }
        klub_input.clear();
        break;

    case 165:   // И
        klub_cassette = !klub_cassette;
        j->info(QString("Tu10Head KLUB: кассета = %1").arg(klub_cassette));
        break;

    case 166:   // К - проверка бдительности (РБ)
        if (klub_autostop)
        {
            // Снятие автостопа: тормоз отпускается плавно в stepKlubSafety
            klub_autostop = false;
            klub_auto_brake = false;
            klub_check_active = false;
            klub_check_horn = false;
            klub_next_check = klubRandomInterval(klub_band);
            j->info("Tu10Head KLUB: автостоп снят");
        }
        else if (klub_check_active)
        {
            const bool single = klub_check_single;
            klub_check_active = false;
            klub_check_horn = false;
            klub_check_rest = 0.0;
            klub_next_check = klubRandomInterval(klub_band);
            j->info(single
                ? "Tu10Head KLUB: однократная проверка бдительности подтверждена"
                : "Tu10Head KLUB: периодическая проверка бдительности подтверждена");
        }
        else
        {
            j->info("Tu10Head KLUB: проверка бдительности не требуется");
        }
        break;

    case 167:   // РМП - режим «Поездной / Маневровый», только на стоянке
        if (qAbs(velocity) * 3.6 > 1.0)
        {
            j->info("Tu10Head KLUB: режим движения меняется только на стоянке");
            break;
        }
        klub_ind_m = !klub_ind_m;
        klub_ind_p = !klub_ind_m;
        klub_k20 = false;
        klub_vk = false;
        klub_prev_signal = -1;
        klub_prev_alsn = -1;
        klub_band = -1;
        klub_next_check = 0.0;
        j->info(QString("Tu10Head KLUB: режим %1")
                .arg(klub_ind_m ? "маневровый (М)" : "поездной (П)"));
        break;

    case 168:   // ПОВТЯР
        klub_flash_timer = 0.6;
        j->info("Tu10Head KLUB: повтор");
        break;

    case 169:   // ОТМР
        klub_side = !klub_side;
        j->info(QString("Tu10Head KLUB: направление %1")
                .arg(klub_side ? "бок" : "прямо"));
        break;

    case 170:   // ОС - отмена действующего ограничения
        klub_k20 = false;
        klub_limit_offset = 0;
        klub_coord_offset = 0.0;
        klub_input.clear();
        j->info("Tu10Head KLUB: ограничение отменено");
        break;

    case 171:   // К20 - проезд светофора с запрещающим при «КЖ»/«К»
        if (klub_signal == 0 || klub_signal == 1)
        {
            klub_k20 = true;
            j->info("Tu10Head KLUB: «К20» - Vдоп 20 км/ч");
        }
        else
        {
            j->info("Tu10Head KLUB: «К20» действует при сигналах «КЖ» и «К»");
        }
        break;

    case 172:   // >О<
        klub_input.clear();
        break;

    case 173:   // вверх
        klub_limit_offset += 5;
        j->info(QString("Tu10Head KLUB: следующее ограничение %1 км/ч")
                .arg(QString::number(klub_limit_offset)));
        break;

    case 174:   // вниз
        klub_limit_offset -= 5;
        j->info(QString("Tu10Head KLUB: следующее ограничение %1 км/ч")
                .arg(QString::number(klub_limit_offset)));
        break;

    case 175:   // F
        klub_flash_timer = 2.0;
        j->info("Tu10Head KLUB: самоконтроль индикации");
        break;

    default:
        break;
    }
}

//------------------------------------------------------------------------------
// Предел скорости для КЛУБ-У: ограничение по маршруту, но не выше
// конструкционной скорости ТУ10. Если карта маршрута молчит - 50 км/ч.
//------------------------------------------------------------------------------
double Tu10Head::klubSpeedLimit() const
{
    double v_limit = 0.0;
    if (speedmap) v_limit = speedmap->getCurrentLimit();

    if (v_limit <= 0.0 || v_limit > KLUB_MAX_SPEED_KMH)
    {
        v_limit = KLUB_MAX_SPEED_KMH;
    }

    return v_limit;
}

//------------------------------------------------------------------------------
// «Старшинство» сигнала БИЛ: чем больше, тем более запрещающий.
//------------------------------------------------------------------------------
static int klubSignalRank(const int sig)
{
    switch (sig)
    {
    case 0: return 4;   // К
    case 1: return 3;   // КЖ
    case 2: return 2;   // Ж
    case 3: return 1;   // З
    default: return 0;  // Б
    }
}

//------------------------------------------------------------------------------
// Группа интервалов периодических проверок:
//   2 - 30..40 с, 4 - 60..90 с, -1 - проверки не производятся.
//------------------------------------------------------------------------------
static int klubBandFor(const int sig, const bool maneuver,
                       const double v_kmh, const double v_tsel)
{
    if (maneuver) return 4;                     // п. 5.7.3
    switch (sig)
    {
    case 0:                                     // «К»
    case 1: return 2;                           // «КЖ»
    case 2: return (v_kmh > v_tsel) ? 2 : -1;   // «Ж» (п. 5.5.2.3)
    case 4: return 4;                           // «Б» (п. 5.5.5.3)
    default: return -1;                         // «З» — проверок нет
    }
}

//------------------------------------------------------------------------------
// КЛУБ-У: контроль скорости и проверка бдительности по инструкции.
//
// Сигналы БИЛ: «К», «КЖ», «Ж», «З», «Б»; для каждого свои Vдоп и Vцел
// (табл. 3.3.4.2). При превышении Vдоп на 1 км/ч раздаётся свисток ЭПК,
// и если за 6 ± 0,5 с скорость не снижена — экстренное торможение
// (п. 5.5.1.2).
//
// Проверки бдительности: однократные (смена сигнала на более запрещающий,
// трогание при «К», «КЖ», «Б») и периодические. Интервалы: «Ж» (при
// превышении Vцел), «КЖ», «К» — 30-40 с; «Б» и маневровый режим — 60-90 с;
// при «З» проверки не производятся (п. 5.4, 5.5, 5.7). На РБ даётся
// 6 ± 0,5 с, для периодической после этого — свисток ЭПК и ещё 6 с на РБС,
// затем экстренное торможение.
//
// Трогание: если контроллер в тяговой позиции, а движения нет 70 с —
// срыв ЭПК (п. 5.3.1).
//------------------------------------------------------------------------------
void Tu10Head::stepKlubSafety(const double dt)
{
    Journal *j = Journal::instance();
    const double v_kmh = qAbs(velocity) * 3.6;

    // --- 1. Сигнал локомотивного светофора и режим движения ---
    int alsn = 4;                                   // «Б» (белый)
    if (!klub_ind_m && alsn_coil && alsn_coil->getFrequency() > 0.0)
    {
        alsn = static_cast<int>(alsn_coil->getCode());  // 0=К,1=КЖ,2=Ж,3=З
        if (alsn < 0 || alsn > 3) alsn = 0;
    }

    // Ручные разрешения («ВК», «К20») действуют до смены сигнала АЛС.
    if (klub_prev_alsn >= 0 && alsn != klub_prev_alsn)
    {
        klub_vk = false;
        if (alsn == 2 || alsn == 3) klub_k20 = false;
    }
    klub_prev_alsn = alsn;

    int sig = alsn;                                 // «ВК»: с «К» на «Б»
    if (!klub_ind_m && klub_vk && alsn == 0) sig = 4;
    klub_signal = sig;

    // --- 2. Допустимая и целевая скорости (табл. 3.3.4.2) ---
    const double v_route = klubSpeedLimit();
    double v_dop = v_route;
    double v_tsel = v_route;

    if (klub_ind_m)
    {
        v_dop = v_tsel = KLUB_MANEUVER_KMH;         // п. 5.7.1
    }
    else
    {
        switch (sig)
        {
        case 0:                                     // «К»
            v_tsel = 0.0;
            v_dop = 20.0;
            break;

        case 1:                                     // «КЖ»: Vцел 20, Vдоп от Vжел
            v_tsel = 20.0;
            {
                double dist = 0.0;
                if (speedmap) dist = speedmap->getNextLimitDistance();
                if (dist <= 0.0 && alsn_coil) dist = alsn_coil->getNextSignalDistance();
                if (dist > 0.0)
                {
                    const double k = qBound(0.0, dist / 300.0, 1.0);
                    v_dop = 20.0 + (v_route - 20.0) * k;
                }
            }
            break;

        case 4:                                     // «Б»
            v_dop = v_tsel = KLUB_VBEL_KMH;
            break;

        default:                                    // «Ж», «З»
            break;
        }
        if (klub_k20 && (sig == 0 || sig == 1)) v_dop = 20.0;   // кнопка «К20»
    }
    klub_v_dop = v_dop;
    klub_v_tsel = v_tsel;

    // --- 3. Превышение Vдоп: свисток ЭПК, через 6 с — экстренное ---
    if (!klub_autostop && v_kmh > v_dop + KLUB_WHISTLE_OVER)
    {
        if (!klub_whistle)
        {
            klub_whistle = true;
            klub_whistle_rest = KLUB_CHECK_TIMEOUT;
            j->info(QString("Tu10Head KLUB: превышение Vдоп на %1 км/ч - свисток ЭПК")
                    .arg(v_kmh - v_dop, 0, 'f', 1));
        }
        klub_whistle_rest -= dt;
        if (klub_whistle_rest <= 0.0)
        {
            klub_whistle = false;
            klubEmergency("скорость не снижена после свистка ЭПК");
        }
    }
    else if (klub_whistle)
    {
        klub_whistle = false;
        j->info("Tu10Head KLUB: скорость снижена - свисток ЭПК прекращён");
    }

    // Тормоз после снятия автостопа отпускается плавно.
    if (!klub_auto_brake && klub_brake > 0.0)
    {
        klub_brake = qMax(0.0, klub_brake - 0.4 * dt);
    }

    // --- 4. Проверка бдительности (п. 5.4) ---
    const bool moving = v_kmh > 1.0;

    // 4.1 Однократная при смене сигнала на более запрещающий.
    if (!klub_check_active && !klub_autostop && moving && klub_prev_signal >= 0 &&
        klubSignalRank(sig) > klubSignalRank(klub_prev_signal))
    {
        klubStartCheck(false);
    }
    klub_prev_signal = sig;

    // 4.2 Однократная при трогании при «К», «КЖ», «Б» (п. 5.3.2);
    //     в маневровом режиме отменена (п. 5.7.2).
    if (moving && !klub_moving && !klub_ind_m && !klub_check_active && !klub_autostop &&
        (sig == 0 || sig == 1 || sig == 4))
    {
        klubStartCheck(false);
    }
    klub_moving = moving;

    // 4.3 Периодические проверки: интервал зависит от сигнала и режима.
    const int band = klubBandFor(sig, klub_ind_m, v_kmh, v_tsel);
    if (!moving || klub_autostop || band < 0)
    {
        klub_band = -1;
        klub_next_check = 0.0;
    }
    else if (!klub_check_active)
    {
        if (band != klub_band)
        {
            klub_band = band;
            klub_next_check = klubRandomInterval(band);
        }
        klub_next_check -= dt;
        if (klub_next_check <= 0.0)
        {
            klubStartCheck(true);
        }
    }

    // 4.4 Отсчёт времени на ответ; для периодической — свисток и ещё 6 с.
    if (klub_check_active)
    {
        klub_check_rest -= dt;
        if (klub_check_rest <= 0.0)
        {
            if (!klub_check_single && !klub_check_horn)
            {
                klub_check_horn = true;
                klub_check_rest = KLUB_CHECK_TIMEOUT;
                j->info("Tu10Head KLUB: бдительность не подтверждена - свисток ЭПК");
            }
            else
            {
                const bool single = klub_check_single;
                klub_check_active = false;
                klub_check_horn = false;
                klubEmergency(single
                    ? "однократная проверка бдительности не подтверждена"
                    : "бдительность не подтверждена");
            }
        }
    }

    // --- 5. Контроль трогания: 70 с в тяге без движения (п. 5.3.1) ---
    if (!klub_autostop && throttle > 0.02 && v_kmh < 1.0)
    {
        klub_traction_timer += dt;
        if (klub_traction_timer >= KLUB_START_TIMEOUT)
        {
            klub_traction_timer = 0.0;
            klubEmergency("трогание не начато за 70 с");
        }
    }
    else
    {
        klub_traction_timer = 0.0;
    }

    if (klub_autostop)
    {
        klub_brake = 1.0;
        klub_auto_brake = true;
    }
}

//------------------------------------------------------------------------------
// Начать проверку бдительности: однократную или периодическую.
//------------------------------------------------------------------------------
void Tu10Head::klubStartCheck(const bool periodic)
{
    klub_check_active = true;
    klub_check_single = !periodic;
    klub_check_horn = false;
    klub_check_rest = KLUB_CHECK_TIMEOUT;

    Journal::instance()->info(periodic
        ? "Tu10Head KLUB: периодическая проверка бдительности - нажмите РБ (К)"
        : "Tu10Head KLUB: однократная проверка бдительности - нажмите РБ (К)");
}

//------------------------------------------------------------------------------
// Экстренное торможение через ЭПК: тормоз держится до нажатия РБ (К).
//------------------------------------------------------------------------------
void Tu10Head::klubEmergency(const QString &reason)
{
    klub_autostop = true;
    klub_brake = 1.0;
    klub_auto_brake = true;
    klub_check_active = false;
    klub_check_horn = false;
    klub_whistle = false;
    klub_traction_timer = 0.0;

    Journal::instance()->info(
        QString("Tu10Head KLUB: %1 - экстренное торможение (автостоп)").arg(reason));
}

//------------------------------------------------------------------------------
// Случайный интервал периодических проверок: 30-40 с или 60-90 с.
//------------------------------------------------------------------------------
double Tu10Head::klubRandomInterval(const int band)
{
    if (band == 2) return static_cast<double>(QRandomGenerator::global()->bounded(30, 41));
    if (band == 4) return static_cast<double>(QRandomGenerator::global()->bounded(60, 91));
    return 0.0;
}

//------------------------------------------------------------------------------
void Tu10Head::stepKeysControl(double t, double dt)
{
    Q_UNUSED(t)
    if (!diesel) return;

    // --- Самотест ЭСУД по инструкции: пока идёт (2 с), пуск заблокирован ---
    if (mass_on && esud_on && prib_panel_on) {
        diag_timer += dt;
    } else {
        diag_timer = 0.0;
    }
    if (cab_diag) diag_timer = 0.0;            // повторный самотест
    diag_ready = (diag_timer >= 2.0);

    // --- Пуск / стоп дизеля: клавиши S/T или кнопки на панели №4 ---
    const bool cur_S = getKeyState(KEY_S);
    if (cur_S && !prev_S) start_pulse = 1.0;
    prev_S = cur_S;

    const bool cur_T = getKeyState(KEY_T);
    if (cur_T && !prev_T) stop_pulse = 1.0;
    prev_T = cur_T;

    if (start_pulse > 0.0) start_pulse = qMax(0.0, start_pulse - dt);
    if (stop_pulse  > 0.0) stop_pulse  = qMax(0.0, stop_pulse  - dt);

    const bool can_start = kPanel4Installed ? (control_on && diag_ready) : true;
    diesel->setStartCommand(start_pulse > 0.0 && can_start);
    diesel->setStopCommand (stop_pulse  > 0.0);

    if (start_pulse > 0.0 && !can_start) {
        hint_timer += dt;
        if (hint_timer > 0.5) {
            hint_timer = 0.0;
            Journal::instance()->info("Tu10Head: pusк заблокирован - нужны Масса, ЭСУД, Приборная панель, Управление и конец диагностики");
        }
    } else {
        hint_timer = 0.0;
    }

    // --- Штурвал тяги: W = прибавить, A = убавить ---
    if (getKeyState(KEY_W)) {
        throttle = qMin(1.0, throttle + throttle_rate * dt);
    }
    if (getKeyState(KEY_A)) {
        throttle = qMax(0.0, throttle - throttle_rate * dt);
    }

    // --- Реверсор: R переключает по кругу 0 -> +1 -> -1 -> 0 ---
    bool cur_R = getKeyState(KEY_R);
    if (cur_R && !prev_R) {
        if      (direction ==  0) direction =  1;
        else if (direction ==  1) direction = -1;
        else                      direction =  0;
        Journal::instance()->info(QString("Tu10Head: reversor -> %1").arg(direction));
    }
    prev_R = cur_R;

    // --- Краны ---
    // Space - следующее положение крана машиниста 395,
    // H - отпуск (II поездное + кран 254 в 0), B - экстренное (V).
    const bool cur_Space = getKeyState(KEY_Space);
    if (cur_Space && !prev_Space) {
        kran_main = (kran_main + 1) % 5;
        Journal::instance()->info(QString("Tu10Head: kran 395 -> %1").arg(kran_main));
    }
    prev_Space = cur_Space;

    const bool cur_H = getKeyState(KEY_H);
    if (cur_H && !prev_H) {
        kran_main = 1;
        kran_loco = 0;
        Journal::instance()->info("Tu10Head: otpusk (kran 395 -> II, 254 -> 0)");
    }
    prev_H = cur_H;

    const bool cur_B = getKeyState(KEY_B);
    if (cur_B && !prev_B) {
        kran_main = 4;
        Journal::instance()->info("Tu10Head: ekstrennoe (kran 395 -> V)");
    }
    prev_B = cur_B;

    // --- КЛУБ-У: подтверждение бдительности клавишей K (дублирует «К») ---
    const bool cur_K = getKeyState(KEY_K);
    if (cur_K && !prev_klub_K)
    {
        onKlubKey(166);
    }
    prev_klub_K = cur_K;

    // --- КЛУБ-У: РМП (J), ВК (I), К20 (U) ---
    const bool cur_J = getKeyState(KEY_J);
    if (cur_J && !prev_klub_J) onKlubKey(167);
    prev_klub_J = cur_J;

    const bool cur_I = getKeyState(KEY_I);
    if (cur_I && !prev_klub_I) onKlubKey(164);
    prev_klub_I = cur_I;

    const bool cur_U = getKeyState(KEY_U);
    if (cur_U && !prev_klub_U) onKlubKey(171);
    prev_klub_U = cur_U;

    // --- Отладка: раз в секунду печатаем состояние ---
    dbg_timer += dt;
    if (dbg_timer >= 1.0) {
        dbg_timer = 0.0;
        Journal::instance()->info(QString("Tu10Head: throttle=%1 dir=%2 brake=%3")
            .arg(throttle, 0, 'f', 2)
            .arg(direction)
            .arg(brake, 0, 'f', 2));
    }

    // --- Целевые обороты дизеля (по инструкции: ХХ 600, номинал 1900) ---
    double target_rpm = 600.0 + throttle * (1900.0 - 600.0);
    diesel->setTargetRPM(target_rpm);
}

//------------------------------------------------------------------------------
void Tu10Head::applyTraction(const double &dt)
{
    Q_UNUSED(dt)
    if (!diesel) return;

    double F_traction = 0.0;
    if (diesel->isRunning() && direction != 0 && throttle > 0.0) {
        double M_wheel = diesel->getTorque() * gear_ratio * gear_efficiency * throttle;
        double R_wheel = rk.empty() ? 0.3 : rk[0];
        double F_wheel = M_wheel / R_wheel;

        // Ограничение по мощности дизеля (230 л.с. = 169 кВт): F <= P / v
        const double v = qMax(qAbs(velocity), 0.8);
        const double F_power = 169000.0 / v;
        if (F_wheel > F_power) F_wheel = F_power;

        // Ограничение по сцеплению: 62,7 кН при трогании (мю = 0,33)
        if (F_wheel > max_traction_force) F_wheel = max_traction_force;

        F_traction = direction * F_wheel;

        // Конструкционная скорость по инструкции - 50 км/ч (13,9 м/с)
        if (direction > 0 && velocity >=  13.9) F_traction = 0.0;
        if (direction < 0 && velocity <= -13.9) F_traction = 0.0;
    }

    // КЛУБ-У при автоторможении отключает тягу, иначе на полном ходу
    // тормоз уравновешивается силой тяги и скорость не падает.
    if (klub_auto_brake) F_traction = 0.0;

    double F_brake = 0.0;
    if (brake > 0.0 && qAbs(velocity) > 0.01) {
        F_brake = -((velocity > 0.0) ? 1.0 : -1.0) * brake * brake_force_max;
    }

    setActiveCommonForce(0, F_traction + F_brake);
}

//------------------------------------------------------------------------------
double Tu10Head::mainResist(const double &velocity)
{
    return Vehicle::mainResist(velocity);
}

//------------------------------------------------------------------------------
double Tu10Head::wheelrailFriction(const double &velocity)
{
    return Vehicle::wheelrailFriction(velocity);
}

//------------------------------------------------------------------------------
GET_MODULE(Tu10Head)

//------------------------------------------------------------------------------
// Заполняет аналоговые сигналы, которые читает дисплей КЛУБ-У
// (modules/ep1m/club-u-display.dll). Индексы и смысл восстановлены
// дизассемблированием ClubUDisplay::update():
//
//   146..153 - имя станции (8 символов, коды Unicode)
//   154..177 - имя цели/маршрута (24 символа)
//   178      - время по расписанию, с от полуночи
//   179      - координата по маршруту, км (DLL печатает '%0.3f')
//   180      - запрет отпуска тормозов
//   181      - активная кабина: 1.0 для дисплея с SignalID 144,
//              2.0 для дисплея с SignalID 145
//   182      - сигнал БИЛ: 1=К, 2=КЖ, 3=Ж, 4=З, 0=Б
//   183      - число зелёных ячеек в показании (0 при «Б»)
//   184      - индикатор "прямо", 185 - "бок"
//   186      - проверка бдительности / свисток / автостоп
//               187 - "М", 188 - "П", 189 - кассета
//   190      - скорость, км/ч
//   191      - Vдоп, км/ч
//   192      - Vцел, км/ч
//   193      - реверсор: 1 вперёд, -1 назад, 0 нейтраль
//   194      - расстояние до светофора, м
//   195..197 - давление ТМ и УР (кабина 1 / кабина 2), кгс/см2
//   199      - ускорение, м/с2
//   206      - текущее время, с от полуночи
//------------------------------------------------------------------------------
void Tu10Head::fillKlubSignals(std::vector<float> *as, const simulator_time_t &t)
{
    if (!as) return;

    // Дисплей читает сигнал 206, поэтому массив должен быть не короче 207.
    const size_t KLUB_SIGNALS = 224;
    if (as->size() < KLUB_SIGNALS) as->resize(KLUB_SIGNALS, 0.0f);

    // Названия - кодами Unicode, чтобы не зависеть от кодировки исходника.
    // "ПОЛИГОН" (7 символов) и "МАГИСТРАЛЬ" (10).
    const QString station = QString::fromUtf16(
        u"\u041F\u041E\u041B\u0418\u0413\u041E\u041D");
    const QString target = QString::fromUtf16(
        u"\u041C\u0410\u0413\u0418\u0421\u0422\u0420\u0410\u041B\u042C");

    for (size_t i = 0; i < 8; ++i)
    {
        (*as)[146 + i] = (i < static_cast<size_t>(station.size()))
                ? static_cast<float>(station.at(static_cast<int>(i)).unicode())
                : 32.0f;
    }
    for (size_t i = 0; i < 24; ++i)
    {
        (*as)[154 + i] = (i < static_cast<size_t>(target.size()))
                ? static_cast<float>(target.at(static_cast<int>(i)).unicode())
                : 32.0f;
    }

    // Время суток (секунды от полуночи). server_time_t хранит время
    // с точностью до 0.1 мс, getSecondsSinceMidnight() не константный.
    server_time_t st = t.time;
    const float sec_of_day = static_cast<float>(st.getSecondsSinceMidnight());
    (*as)[178] = sec_of_day;    // время по расписанию
    (*as)[206] = sec_of_day;    // текущее время

    // Активная кабина: DLL показывает блоки только при ненулевом сигнале
    // с индексом, равным SignalID дисплея (144 — кабина 1).
    (*as)[144] = 1.0f;

    // Координата дисплею нужна в километрах (0…9999.999) с точностью
    // до метра: DLL форматирует её как '%0.3f'. Клавиатурой можно ввести
    // поправку координаты (клавиши «ВК»/«ОС»).
    (*as)[179] = static_cast<float>((getTrainCoord() + klub_coord_offset) / 1000.0);

    (*as)[180] = (klub_zapret || klub_auto_brake) ? 1.0f : 0.0f;  // запрет отпуска
    (*as)[181] = 1.0f;          // номер активной кабины (для 144 — 1.0)

    double alsn_dist = 0.0;
    if (alsn_coil) alsn_dist = alsn_coil->getNextSignalDistance();

    // Сигнал БИЛ: 1=К, 2=КЖ, 3=Ж, 4=З; 0 — «Б» (белый).
    (*as)[182] = static_cast<float>(klub_signal < 4 ? klub_signal + 1 : 0);
    int cells = (klub_signal < 4) ? klub_signal + 1 : 0;
    if (klub_flash_timer > 0.0) cells = 4;           // проверка ламп/повтор
    (*as)[183] = static_cast<float>(cells);          // число свободных блок-участков

    (*as)[184] = klub_side ? 0.0f : 1.0f;  // прямо
    (*as)[185] = klub_side ? 1.0f : 0.0f;  // бок
    (*as)[186] = (klub_check_active || klub_whistle || klub_autostop) ? 1.0f : 0.0f;  // проверка бдительности
    (*as)[187] = klub_ind_m ? 1.0f : 0.0f;      // "М"
    (*as)[188] = klub_ind_p ? 1.0f : 0.0f;      // "П"
    (*as)[189] = klub_cassette ? 1.0f : 0.0f;   // кассета

    const double v_kmh = qAbs(velocity) * 3.6;
    (*as)[190] = static_cast<float>(v_kmh);

    (*as)[191] = static_cast<float>(klub_v_dop);
    (*as)[192] = static_cast<float>(klub_v_tsel);

    (*as)[193] = static_cast<float>(direction);

    // Расстояние до цели: основное — до следующего ограничения по карте
    // пути; если карта молчит, остаётся расстояние до светофора (АЛС).
    double dist_to_target = 0.0;
    if (speedmap) dist_to_target = speedmap->getNextLimitDistance();
    if (dist_to_target <= 0.0) dist_to_target = alsn_dist;
    (*as)[194] = static_cast<float>(dist_to_target);

    // 1 кгс/см2 = 0.0980665 МПа
    const double to_kgf = 1.0 / 0.0980665;
    const float tm = static_cast<float>(brake_line_pressure * to_kgf);
    (*as)[195] = tm;    // ТМ
    (*as)[196] = tm;    // УР (кабина 1)
    (*as)[197] = tm;    // УР (кабина 2)

    (*as)[199] = static_cast<float>(cur_acceleration);
}
