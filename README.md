# ScriptMerlin

ScriptMerlin — встраиваемая скриптовая библиотека на C++20. Она позволяет загружать и выполнять скрипты в приложении, регистрировать доступные скрипту C++-функции и передавать в скрипт объекты хоста.

## Возможности

- компиляция и выполнение скриптов через виртуальную машину;
- функции, переменные, числа, строки, таблицы и ссылки;
- препроцессор с директивами `#def` и `#include`;
- регистрация нативных функций и объектов приложения;
- RAII-владение строками, таблицами, ссылками и объектами хоста;
- собственные файловая система и логгер;
- встроенные библиотеки стандартных и игровых функций.

## Требования

- CMake 3.20 или новее;
- компилятор с поддержкой C++20.

## Сборка

```sh
cmake -S . -B build
cmake --build build
```

Пример приложения и юнит-тесты включены по умолчанию. Чтобы собрать только библиотеку:

```sh
cmake -S . -B build -DSME_EXAMPLE=OFF -DSME_TEST=OFF
cmake --build build
```

Юнит-тесты используют GoogleTest, который CMake загружает при включённой опции `SME_TEST`.

## Быстрый старт

```cpp
#include <sme/sme.h>

#include <iostream>
#include <span>

int main()
{
    sme::Engine engine;

    engine.RegisterNative("add", [](sme::Engine&, std::span<const sme::Value> args) {
        if (args.size() != 2) {
            throw sme::RError("add expects 2 arguments");
        }
        const auto* left = std::get_if<int>(&args[0]);
        const auto* right = std::get_if<int>(&args[1]);
        if (!left || !right) {
            throw sme::RError("add expects integer arguments");
        }
        return sme::integer(*left + *right);
    });

    engine.loadSource(R"(
        fn main() {
            return add(20, 22);
        }
    )");

    const sme::Value result = engine.call("main");
    std::cout << engine.toString(result) << '\n';
}
```

В примере скрипт вызывает зарегистрированную C++-функцию и возвращает `42`. Нативная функция получает уже вычисленные аргументы в `std::span<const sme::Value>`; проверяйте их количество и типы перед чтением.

## Подключение в CMake

### Как подпроект

```cmake
add_subdirectory(path/to/ScriptMerlin)
target_link_libraries(my_app PRIVATE ScriptMerlin::ScriptMerlin)
```

Чтобы при добавлении проекта не собирать его пример и тесты:

```cmake
set(SME_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SME_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(path/to/ScriptMerlin)
```

### Установка и `find_package`

Установите библиотеку:

```sh
cmake -S . -B build -DSME_EXAMPLE=OFF -DSME_TEST=OFF
cmake --build build
cmake --install build --prefix /path/to/install
```

Затем подключите установленный пакет в CMake-проекте:

```cmake
find_package(ScriptMerlin CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE ScriptMerlin::ScriptMerlin)
```

Если пакет установлен в нестандартный каталог, укажите его при конфигурации:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/install
```

Основной публичный заголовок — `<sme/sme.h>`. Для отдельных частей API можно подключать заголовки из каталога `include/sme`.

## Интеграция с приложением

- `Engine::RegisterNative` регистрирует C++-функцию, вызываемую из скрипта.
- `Engine::bind` передаёт объект приложения как глобальную переменную скрипта. Для интеграции объект реализует интерфейс `sme::HostObject`.
- `Engine::useFileSystem` позволяет задать файловую систему, используемую для загрузки скриптов и `#include`.
- `Engine::useLog` подключает логгер для вывода и сообщений движка.
- `Engine::loadSource` загружает исходный код из строки, а `Engine::load` — из файла.
- `Engine::call` вызывает функцию скрипта по имени.

Строки хранятся непосредственно в `Value`; при взятии ссылки строковый слот
переводится на `std::shared_ptr<std::string>`. Таблицы хранятся как разделяемый
`std::vector<Value>` в `Value` и читаются напрямую, без `Engine`:
`std::get<sme::TableHandle>(value)->at(index)`. Для разделяемой ссылки на
отдельное значение используйте `sme::shared(value)`; целевое значение доступно
через `*std::get<sme::SharedValue>(link)`. Объекты хоста также используют
разделяемое RAII-владение, поэтому значения могут безопасно пережить следующий
вызов и даже уничтожение `Engine`. Явные якоря времени жизни не нужны.
`HostObject::release()` также сохранён как
совместимый виртуальный метод, но движок его не вызывает; время жизни объекта
определяется `std::shared_ptr`.

Чтобы shared ownership оставался ограниченным временем жизни внешних значений,
циклическое владение таблицами запрещено и приводит к `sme::RError`.

Ошибки выполнения скрипта представлены исключением `sme::RError`.
