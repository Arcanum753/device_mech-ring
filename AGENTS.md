# device_mech-ring — механические часы с боем

> **Опциональное устройство.** Подключается через `src_filter` + `build_flags`.
> **Работоспособно только в составе сборки, содержащей ядро** (см. `../../TRS.md` §2.1).
> **Для сборки необходимо клонировать само устройство и все указанные в его `.ini` модули**
> (см. `../../TRS.md` §2.2).

Бой часов (молоток/звонковое колесо): парковка по метке датчика, калибровка по шагам,
поворот N полных оборотов с настраиваемыми паузами, а в начале нового часа (в окне
`time_begin..time_end`) — отбивание соответствующего числа ударов.

- **Репозиторий:** https://github.com/Arcanum753/device_mech-ring
- **Папка:** `src/device_mech-ring/`
- **Флаг активации:** `-D DEVICE_RINGMECH=1`
- **Registry:** `object=device_mech_ring`, `define=DEVICE_RINGMECH`, `web=1`, `loop=0` (без `namespace`/`res`/`prio`)
- **Env:** `esp32_clock-mech_ring` (extends `env:esp32`); `src_filter` включает `module_udp`, `module_otaclient`, `device_clock-mech`, `module_ds3231`, `device_mech-ring`
- **Только для платформы:** ESP32
- **Зависит от модулей:** `module_udp`, `module_otaclient`, `module_ds3231`, `device_clock-mech`
- **Зависит от ядра:** `core_web`, `core_sys`, `core_json`, `core_led`

## Назначение

Бой часов (молоток/звонковое колесо): парковка по метке датчика, калибровка по шагам,
поворот N полных оборотов с настраиваемыми паузами, а в начале нового часа (в окне
`time_begin..time_end`) — отбивание соответствующего числа ударов.

## Функциональные требования

- FR-RINGMECH-1: Два режима `enable_status`; парковка (`cmdHome`), подсчёт оборотов (`cmdCount`), поворот (`cmdTurn`).
- FR-RINGMECH-2: Отбивание часа: число ударов = текущий час, только в окне `time_begin..time_end`.
- FR-RINGMECH-3: Паузы боя `ringPauseOne`, `ringPauseTwo`; начальное положение `firstPosition`.
- FR-RINGMECH-4: Источник времени — `core_sys.timeNow()` (подсистема виртуального времени); период `pollInterval`, лимит `errorLimitSteps`. Поле `timeSource` удалено.
- FR-RINGMECH-5: Датчик `SENS=32` (`INPUT_PULLUP`, срабатывание по LOW), подсветка `SENS_LED=27`, STEP=17, EN=16.
- FR-RINGMECH-6: Требует наличия `device_clock-mech` в сборке.
- FR-RINGMECH-7: Сохранение конфига — web `handleSave` и терминал `r-save`; bus-функции `save` нет.

## Аппаратные интерфейсы

| Интерфейс | Выводы по умолчанию | Примечание |
|-----------|---------------------|------------|
| Бой часов | STEP=17, EN=16, SENS=32 | `SENS` — `INPUT_PULLUP`, срабатывание по LOW |
| Подсветка датчиков | SENS_LED=27 | канонически — `../device_clock-mech/AGENTS.md` |
| DS3231 SQW/INT# | 13 (`DS3231_SQW_PIN`) | см. `../module_ds3231/AGENTS.md` |

## Веб-интерфейс

Маршруты: `POST /ring-mech/save`, `GET /ring-mech/info`; GET:
`/ring-mech/turn|en|sens|home|count|status|reset|ver`.
Страница `ring-mech.html` (пункт меню — `web/_menu.html`).

## Конфигурация

`/config_ring-mech.json` — `enable_status`, `stepsPerRevolution`, `pollInterval`,
`errorLimitSteps`, `ringPauseOne`, `ringPauseTwo`, `firstPosition`, `time_begin`, `time_end`.

## Терминальные команды

| Команда | Назначение | Доступность |
|---------|-----------|-------------|
| `r-*` | отладка (хоминг/поворот/датчик), `r-save` — сохранение конфига | только `DEVICE_RINGMECH` |

## Слоистая структура

Из `../../LAYERS.md`: `device_mech-ring` — часы с боем: хоминг, ротация, звонок; сейчас
define'ы, enum'ы, `strRingMechConfig` + автомат; выделить `_types.h`, `_engine.cpp`.

## Зависимости модулей

Указаны в `.ini` (`src_filter`) и должны быть склонированы для сборки:
- `module_udp` — `../module_udp/AGENTS.md`
- `module_otaclient` — `../module_otaclient/AGENTS.md`
- `module_ds3231` — `../module_ds3231/AGENTS.md`
- `device_clock-mech` — `../device_clock-mech/AGENTS.md`

## Критерии приёмки

- См. AC-13 (каноническое описание — в `../device_clock-mech/AGENTS.md`): калибровка по датчикам, установка/отслеживание времени; сохранение только по явному действию.

## Тестирование

- HIL-стенд (уровень 5): механизм боя с датчиком и шаговым двигателем (см. `../../TESTING.md`).

## Ссылки

- Ядро и конвенции: `../../TRS.md`
- Слоистая структура: `../../LAYERS.md`
- Общие утилиты: `../../TRS.md` §3.1.12 (`common/`)
- Сборка: `../../BUILD.md`
- Реестр компонентов: `../../INVENTORY.md`

> Если устройство читается вне дерева ядра (standalone), корневые документы доступны в
> репозитории ядра avr-fota.
