# Устанавливаем переменную TARGET равной имени проекта
set (TARGET tu10-head)

# Задаем имя проекта
project (${TARGET})

# Qt-шное барахло для метаобъектного компилятора
set(CMAKE_AUTOMOC ON)

# Путь, куда надо сложить собранное
set (LIBRARY_OUTPUT_PATH "../../../../modules/tu10")

# Без префиксов к имени бинарника
set (CMAKE_SHARED_MODULE_PREFIX "")

# Ищем зависимости Qt6
find_package(Qt6 COMPONENTS Xml REQUIRED)

# Пути к заголовкам и сорцам
file (GLOB INCLUDES "./include/*.h")
file (GLOB EQP_INCLUDES "../tu10-equipment/include/*.h")
file (GLOB SOURCES "./src/*.cpp")
file (GLOB EQP_SOURCES "../tu10-equipment/src/*.cpp")

# Компиляция библиотеки
add_library (${TARGET} SHARED ${SOURCES} ${INCLUDES} ${EQP_SOURCES} ${EQP_INCLUDES})

# Пути поиска заголовков
target_include_directories (${TARGET} PRIVATE ./include/)
target_include_directories (${TARGET} PRIVATE ../tu10-equipment/include/)
target_include_directories (${TARGET} PRIVATE ../../../common-headers)

# Компонуемся с библиотеками
target_link_libraries (${TARGET} Qt6::Xml)
target_link_libraries (${TARGET} CfgReader)
target_link_libraries (${TARGET} physics)
target_link_libraries (${TARGET} vehicle)
target_link_libraries (${TARGET} device)
target_link_libraries (${TARGET} filesystem)
target_link_libraries (${TARGET} core)
