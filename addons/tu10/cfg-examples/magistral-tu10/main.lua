--[[
    Магистраль — тестовый сценарий ТУ10
    Простейший сценарий: ставит ТУ10 на путь, ничего не делает
]]

-- Время старта
setTime("12:00")

-- Описываем поезд с нашим ТУ10
tu10_train = TrainData.new()
tu10_train.name = "ТУ10-001"
tu10_train.config = "tu10-test"
tu10_train.traj = "track_a_p1a"       -- путь 1А станции А
tu10_train.coord = 1690.0
tu10_train.dir = 1

-- Регистрируем поезд в RRS
setTrain(tu10_train)
